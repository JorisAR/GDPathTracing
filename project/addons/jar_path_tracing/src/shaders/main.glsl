#[compute]
#version 460

#include "camera_utils.glsl"
#include "brdfs.glsl"


// ----------------------------------- STRUCTS -----------------------------------

struct Light {
    vec4 position;
    vec4 color;
};

struct Triangle {
    vec4 vertices[3];
    vec4 normals[3];
    vec2 uvs[3];
    uint materialIndex;
};

struct Ray {
    vec3 d;
    vec3 o;
    vec3 rD;
};

struct Material
{
    vec4 diffuse_albedo;
    float metallic;
    float roughness;
    // float ior;
    // float transmission;
    // float emission;
};

struct BVHNode {
    vec4 aabbMin;
    vec4 aabbMax;
    uint left_child;
    uint right_child;
    uint first_tri_index;
    uint tri_count;
};

struct TLASNode
{//interleaved to ensure vec3 16 byte alignment
    vec3 aabbMin;
    uint leftRight; // 2x16 bits //if 0, it is a leaf node
    vec3 aabbMax;
    uint blas;
};

struct HitInfo {
    vec3 position;
    float t;
    uint blas;
    uint triangle;
    vec2 barycentrics;
    bool front;
    vec3 out_dir; //i suppose this is essentially fragment-camera direction, though only literally for the direct shading point.
};

struct ShadingInfo {
    vec3 position;
    vec3 normal;
    vec3 out_dir;
    float lambert_out;
    vec3 emission;
    vec3 diffuse_albedo;
    vec3 fresnel_0;
    float roughness;
};

struct BLASInstance
{
    mat4 transform;
    mat4 inverse_transform;
    vec4 aabbMin;
    vec4 aabbMax;
    uint root; //index to the right BLAS BVH node
    uint materials[3];
    //have an array of material ids? say up to 4/8/16 or something
};

// ----------------------------------- GENERAL STORAGE -----------------------------------

layout(set = 0, binding = 0, r8) restrict uniform writeonly image2D outputImage;

layout(std430, set = 0, binding = 1) restrict buffer Params {
    vec4 background; //rgb, brightness
    int width;
    int height;
    float fov;
    uint triangleCount;
    uint blas_count;
} params;

layout(std430, set = 0, binding = 2) restrict buffer Camera {
    mat4 vp;
    mat4 ivp;
    vec4 position;
    uint frame_index;
} camera;


// ----------------------------------- STORAGE BUFFERS -----------------------------------


layout(set = 1, binding = 0, std430) restrict buffer Triangles
{
    Triangle triangles[];
};

layout(set = 1, binding = 1, std430) restrict buffer Materials
{
    Material materials[];
};

layout(set = 1, binding = 2, std430) restrict buffer BVHTree
{
    BVHNode bvhTree[];
};

layout(set = 1, binding = 3, std430) restrict buffer BLASInstances
{
    BLASInstance blas_instances[];
};

layout(set = 1, binding = 4, std430) restrict buffer TLASInstances
{
    TLASNode tlas_nodes[];
};

// ----------------------------------- FUNCTIONS -----------------------------------

vec2 pcg2d(inout uvec2 seed) {
	// PCG2D, as described here: https://jcgt.org/published/0009/03/02/
	seed = 1664525u * seed + 1013904223u;
	seed.x += 1664525u * seed.y;
	seed.y += 1664525u * seed.x;
	seed ^= (seed >> 16u);
	seed.x += 1664525u * seed.y;
	seed.y += 1664525u * seed.x;
	seed ^= (seed >> 16u);
	// Multiply by 2^-32 to get floats
	return vec2(seed) * 2.32830643654e-10; 
}

vec3 sampleSky(const vec3 direction) {
    float t = 0.5 * (direction.y + 1.0);
    return mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
}

vec3 phongShading(const vec3 cameraPosition, const vec3 position, const vec3 normal, Light light) { 
    vec3 lightDir = normalize(light.position.xyz - position); 
    float diff = max(dot(normal, lightDir), 0.0); 
    vec3 diffuse = diff * light.color.rgb; 
    vec3 viewDir = normalize(-cameraPosition); 
    vec3 reflectDir = reflect(lightDir, normal); 
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0); 
    vec3 specular = spec * light.color.rgb; 
    return diffuse + specular; 
}


ShadingInfo get_shading_data(const HitInfo h) {
    ShadingInfo s;
    Triangle tri = triangles[h.triangle];
    BLASInstance b = blas_instances[h.blas];
    Material material = materials[b.materials[tri.materialIndex]];

    s.position = (b.transform * vec4(h.position, 1.0)).xyz;//transform position to global space
    s.out_dir = h.out_dir;
    //vec2 uv = tri.uvs[0] * (1.0 - u - v) + tri.uvs[1] * u + tri.uvs[2] * v;
    float u = h.barycentrics.x;
    float v = h.barycentrics.y;
    s.normal = (tri.normals[0] * (1.0 - u - v) + tri.normals[1] * u + tri.normals[2] * v).xyz;//transform normal to global space
    s.normal = (b.transform * vec4(s.normal, 0.0)).xyz;
    s.lambert_out = 1.0; //specularity i think
    s.emission = vec3(0);
    s.fresnel_0 = vec3(0); //unsure
    s.diffuse_albedo = material.diffuse_albedo.rgb;
    
   
    return s;
}

bool intersectTriangle(const Ray ray, const uint tri_index, in out HitInfo hitInfo) {
    Triangle tri = triangles[tri_index];
    vec3 v0 = tri.vertices[0].xyz;
    vec3 v1 = tri.vertices[1].xyz;
    vec3 v2 = tri.vertices[2].xyz;

    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;

    vec3 pvec = cross(ray.d, edge2);
    float det = dot(edge1, pvec);

    if (abs(det) < 1e-8) return false;
    float invDet = 1.0 / det;

    vec3 tvec = ray.o - v0;

    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) return false;

    vec3 qvec = cross(tvec, edge1);

    float v = dot(ray.d, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;

    float t = dot(edge2, qvec) * invDet;
    if(t < 0.0 || t > hitInfo.t) return false;

    hitInfo.position = ray.o + t * ray.d;
    hitInfo.t = t;
    hitInfo.triangle = tri_index;
    hitInfo.barycentrics = vec2(u, v);
    hitInfo.out_dir = -ray.d;
    hitInfo.front = true;//depends on order of indices i guess?, i suppose something like: 0 <= dot(ray_dir, cross(edge1, edge2))

    // hitInfo.normal = (tri.normals[0] * (1.0 - u - v) + tri.normals[1] * u + tri.normals[2] * v).xyz;
    // hitInfo.uv = tri.uvs[0] * (1.0 - u - v) + tri.uvs[1] * u + tri.uvs[2] * v;
    // hitInfo.t = t;
    // hitInfo.materialIndex = tri.materialIndex;

    return true;
}

float intersectAABB(const in Ray ray, const vec3 bmin, const vec3 bmax )
{
    float tx1 = (bmin.x - ray.o.x) * ray.rD.x, tx2 = (bmax.x - ray.o.x) * ray.rD.x;
    float tmin = min( tx1, tx2 ), tmax = max( tx1, tx2 );
    float ty1 = (bmin.y - ray.o.y) * ray.rD.y, ty2 = (bmax.y - ray.o.y) * ray.rD.y;
    tmin = max( tmin, min( ty1, ty2 ) ), tmax = min( tmax, max( ty1, ty2 ) );
    float tz1 = (bmin.z - ray.o.z) * ray.rD.z, tz2 = (bmax.z - ray.o.z) * ray.rD.z;
    tmin = max( tmin, min( tz1, tz2 ) ), tmax = min( tmax, max( tz1, tz2 ) );
    if (tmax >= tmin && tmax > 0) return tmin; else return 1e30f;//&& tmin < ray.t
}

bool ray_trace_blas(const uint root, const Ray ray, in out HitInfo hitInfo)
{
    uint stack[64];
    uint stackPtr = 0;
    stack[stackPtr++] = root;

    while (stackPtr > 0) {
        BVHNode node = bvhTree[stack[--stackPtr]];
        // hitInfo.steps++;

        if (node.tri_count > 0) { //isleaf
            for (uint i = 0; i < node.tri_count; i++) {
                intersectTriangle(ray, node.first_tri_index + i, hitInfo);
            }
            continue;
        }
        BVHNode childL = bvhTree[node.left_child];
        BVHNode childR = bvhTree[node.right_child];
        float d1 = intersectAABB(ray, childL.aabbMin.xyz, childL.aabbMax.xyz);
        float d2 = intersectAABB(ray, childR.aabbMin.xyz, childR.aabbMax.xyz);
        if(d1 < d2) {
            if(d1 < 1e30f) {
                stack[stackPtr++] = node.left_child;
                if(d2 != 1e30f) stack[stackPtr++] = node.right_child;
            }
        }
        else {
            if(d2 < 1e30f) {
                stack[stackPtr++] = node.right_child;
                if(d1 != 1e30f) stack[stackPtr++] = node.left_child;
            }
        }       
    }

    return hitInfo.t < 1e9;
}

bool ray_trace_tlas(const Ray ray, inout HitInfo hitInfo)
{
    uint stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0;
    float minT = 1e9; //todo remove?

    while (stackPtr > 0) {
        TLASNode node = tlas_nodes[stack[--stackPtr]];
        // hitInfo.steps++;

        if(node.leftRight == 0){
            BLASInstance b = blas_instances[node.blas];
            Ray b_ray; 
            b_ray.o = (b.inverse_transform * vec4(ray.o, 1.0)).xyz;
            b_ray.d = (b.inverse_transform * vec4(ray.d, 0.0)).xyz;
            b_ray.rD = 1.0 / b_ray.d;
            ray_trace_blas(b.root, b_ray, hitInfo);

            if (hitInfo.t < minT) {
                hitInfo.blas = node.blas;                
                minT = hitInfo.t;
            }
            continue;
        } 
        uint left = node.leftRight & 0xFFFF;
        uint right = node.leftRight >> 16;
        TLASNode childL = tlas_nodes[left];
        TLASNode childR = tlas_nodes[right];
        float d1 = intersectAABB(ray, childL.aabbMin.xyz, childL.aabbMax.xyz);
        float d2 = intersectAABB(ray, childR.aabbMin.xyz, childR.aabbMax.xyz);
        if(d1 < d2) {
            if(d1 < 1e30f) {
                stack[stackPtr++] = left;
                if(d2 != 1e30f) stack[stackPtr++] = right;
            }
        }
        else {
            if(d2 < 1e30f) {
                stack[stackPtr++] = right;
                if(d1 != 1e30f) stack[stackPtr++] = left;
            }
        }          
    }

    return hitInfo.t < 1e9;
}
//traces scene, and returns shading data. true if scene hit, false if missed (i.e. hit the sky instead)
bool ray_trace(const Ray ray, out ShadingInfo s) {
    HitInfo hitInfo;
    hitInfo.t = 1e9;
    bool hit = ray_trace_tlas(ray, hitInfo);    

    if(hit)
    {
        // Material material = materials[hitInfo.materialIndex];
        // color = material.albedo.rgb;
        // vec3 cameraPosition = camera.position.xyz;
        // color = color * phongShading(cameraPosition, hitInfo.position, hitInfo.normal, light);
        s = get_shading_data(hitInfo);
        return true;
        // color = vec3(material.roughness);
    } else {
        s.emission = sampleSky(ray.d);        
        return false;
    }
}

vec3 path_trace(const vec3 o, const vec3 d, inout uvec2 seed) {
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0f);
    Ray ray;
    ray.o = o;
    ray.d = d;
    ray.rD = 1 / d;
    // [[unroll]]
    for (int i = 0; i < 4; i++) {
        ShadingInfo s;
        bool hit = ray_trace(ray, s);
        radiance += throughput * s.emission;
        if(hit) {
            // return s.diffuse_albedo;
            throughput *= s.diffuse_albedo;          

            ray.o = s.position + s.normal * 0.001;
            mat3 shading_to_world_space = get_shading_space(s.normal);
            vec3 sampled_dir = sample_hemisphere_psa(pcg2d(seed));
            ray.d = shading_to_world_space * sampled_dir;
            ray.d = 1 / ray.d;

            


        } else {
            // return s.emission;
            // return vec3(0);
            break;
        }
    }

    return radiance;
}


layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= params.width || pos.y >= params.height) return;
    uvec2 seed = uvec2(gl_GlobalInvocationID.xy) ^ uvec2(camera.frame_index << 16, (camera.frame_index + 237) << 16);

    vec2 screenPos = vec2(pos) / vec2(params.width, params.height) * 2.0 - 1.0;
    vec4 ndcPos = vec4(screenPos.x, -screenPos.y, 1.0, 1.0);
    vec4 worldPos = camera.ivp * ndcPos;
    worldPos /= worldPos.w;
    vec3 ray_origin = camera.position.xyz;
    vec3 ray_dir = normalize(worldPos.xyz - camera.position.xyz);

    // vec3 ray_origin = get_camera_ray_origin(screenPos, camera.ivp);
    // vec3 ray_dir = get_camera_ray_origin(screenPos, camera.vp);


    Light light = {vec4(0.0, 4.0, 0.0, 1.0), vec4(1.0, 1.0, 1.0, 1.0)};
    vec3 color = vec3(0.0f);
    // HitInfo hitInfo;

    color = path_trace(ray_origin, ray_dir, seed);

    // color = vec3(hitInfo.steps / 1024.0);
    // color = sampleSky(ray_dir);

    imageStore(outputImage, pos, vec4(color, 1.0));
}
