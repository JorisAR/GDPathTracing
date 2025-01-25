#[compute]
#version 460

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

struct Material
{
    vec4 albedo;
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

struct HitInfo {
    vec3 position;
    vec3 normal;
    vec2 uv;
    uint materialIndex;
    float t;
    int steps;
};

struct BLASInstance
{
    mat4 transform;
    mat4 inverse_transform;
    uint root; //index to the right BLAS BVH node
    uint material_index;
    uint padding[2];
    //have an array of material ids? say up to 4/8/16 or something
};

// ----------------------------------- GENERAL STORAGE -----------------------------------

layout(std430, set = 0, binding = 0) restrict buffer Params {
    int width;
    int height;
    float fov;
    uint triangleCount;
    vec4 cameraPosition;
    vec4 cameraUp;
    vec4 cameraRight;
    vec4 cameraForward;    
    uint blas_count;
} params;

layout(set = 0, binding = 1, r8) restrict uniform writeonly image2D outputImage;


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
    BLASInstance instances[];
};

// ----------------------------------- FUNCTIONS -----------------------------------

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

bool intersectTriangle(const vec3 orig, const vec3 dir, const Triangle tri, in out HitInfo hitInfo) {
    vec3 v0 = tri.vertices[0].xyz;
    vec3 v1 = tri.vertices[1].xyz;
    vec3 v2 = tri.vertices[2].xyz;

    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;

    vec3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);

    if (abs(det) < 1e-8) return false;
    float invDet = 1.0 / det;

    vec3 tvec = orig - v0;

    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) return false;

    vec3 qvec = cross(tvec, edge1);

    float v = dot(dir, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;

    float t = dot(edge2, qvec) * invDet;
    if(t < 0.0 || t > hitInfo.t) return false;

    hitInfo.position = orig + t * dir;
    hitInfo.normal = (tri.normals[0] * (1.0 - u - v) + tri.normals[1] * u + tri.normals[2] * v).xyz;
    hitInfo.uv = tri.uvs[0] * (1.0 - u - v) + tri.uvs[1] * u + tri.uvs[2] * v;
    hitInfo.t = t;
    // hitInfo.materialIndex = tri.materialIndex;
    return true;
}

bool intersectBoundingBox(const vec3 orig, const vec3 dir, const vec3 invDir, const vec3 aabbMin, const vec3 aabbMax) {
    vec3 t1 = (aabbMin - orig) * invDir;
    vec3 t2 = (aabbMax - orig) * invDir;

    vec3 tmin_vec = min(t1, t2);
    vec3 tmax_vec = max(t1, t2);

    float tmin = max(max(tmin_vec.x, tmin_vec.y), tmin_vec.z);
    float tmax = min(min(tmax_vec.x, tmax_vec.y), tmax_vec.z);

    return tmax > max(tmin, 0.0);
}

#define USE_BVH

#ifdef USE_BVH

bool raytrace_blas(const uint root, const vec3 o, const vec3 d, in out HitInfo hitInfo)
{
    vec3 invDir = 1.0 / d;
    uint stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = root;

    while (stackPtr > 0) {
        uint nodeIndex = stack[--stackPtr];
        BVHNode node = bvhTree[nodeIndex];
        hitInfo.steps++;

        if (!intersectBoundingBox(o, d, invDir, node.aabbMin.xyz, node.aabbMax.xyz)) 
            continue;
            
        if (node.tri_count > 0) {
            for (uint i = 0; i < node.tri_count; i++) {
                Triangle tri = triangles[node.first_tri_index + i];
                intersectTriangle(o, d, tri, hitInfo);
            }
        } else {
            if (node.left_child > 0) stack[stackPtr++] = node.left_child;
            if (node.right_child > 0) stack[stackPtr++] = node.right_child;
        }
        
    }

    return hitInfo.t < 1e9;
}

bool raytrace(const vec3 o, const vec3 d, in out HitInfo hitInfo) {
    hitInfo.t = 1e9;
    hitInfo.steps = 0;
    float minT = 1e9;
    for(int i = 0; i < params.blas_count; i++) {
        BLASInstance b = instances[i];
        vec4 b_o = b.inverse_transform * vec4(o, 1.0);
        vec4 d_o = b.inverse_transform * vec4(d, 0.0);
        
        raytrace_blas(b.root, b_o.xyz, d_o.xyz, hitInfo);

        if (hitInfo.t < minT) {
            minT = hitInfo.t;
            hitInfo.materialIndex = b.material_index;
        }
    }
    return hitInfo.t < 1e9;
}
#else
bool raytrace(const vec3 o, const vec3 d, in out HitInfo hitInfo)
{
    hitInfo.t = 1e9;
    for(int i = 0; i < params.triangleCount; i++) {
        Triangle tri = triangles[i];
        if (intersectTriangle(o, d, tri, hitInfo)) {
            //we may get closer one
        }
    }
    return hitInfo.t < 1e9;
}
#endif

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= params.width || pos.y >= params.height) return;

    vec3 cameraPosition = params.cameraPosition.xyz;
    vec2 uv = (vec2(pos) / vec2(params.width, params.height)) * 2.0 - 1.0;
    float aspectRatio = float(params.width) / float(params.height);
    vec3 rayDir = normalize(uv.x * params.cameraRight.xyz * aspectRatio + -uv.y * params.cameraUp.xyz - params.cameraForward.xyz * 2.5);

    Light light = {vec4(0.0, 4.0, 0.0, 1.0), vec4(1.0, 1.0, 1.0, 1.0)};

    vec3 color = vec3(0.0f);
    HitInfo hitInfo;
    if(raytrace(cameraPosition, rayDir, hitInfo))
    {
        // color = hitInfo.normal;//get material id
        Material material = materials[hitInfo.materialIndex];
        // Material material = materials[1];
        color = material.albedo.rgb;
        color = color * phongShading(cameraPosition, hitInfo.position, hitInfo.normal, light);
        // color = vec3(material.roughness);
    }
    // color = vec3(hitInfo.steps / 256.0);

    imageStore(outputImage, pos, vec4(color, 1.0));
}
