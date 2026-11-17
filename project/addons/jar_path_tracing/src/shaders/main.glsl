#[compute]
#version 460

// Software path tracer, executed as a CompositorEffect on Godot's main RenderingDevice.
// Writes linear HDR radiance into the scene color buffer; Godot's own tonemapper,
// exposure and glow run afterwards.

#include "common.glsli"
#include "brdfs.glsli"
#include "trace_sw.glsli"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// ----------------------------------- SCENE QUERIES -----------------------------------

vec3 sample_sky(vec3 d) {
    if (d.y >= 0.0) {
        return mix(frame.sky_horizon.rgb, frame.sky_top.rgb, sqrt(d.y));
    }
    return mix(frame.sky_horizon.rgb, frame.ground.rgb, sqrt(min(1.0, -d.y * 4.0)));
}

float triangle_world_area(const Instance inst, const Triangle tri) {
    vec3 e1 = xform_vector(inst.o2w, tri.e1.xyz);
    vec3 e2 = xform_vector(inst.o2w, tri.e2.xyz);
    return 0.5 * length(cross(e1, e2));
}

ShadingInfo get_shading(const Ray ray, const Hit h) {
    ShadingInfo s;
    Instance inst = instances[h.instance];
    Triangle tri = triangles[h.triangle];
    TriangleData td = triangle_data[h.triangle];
    Material m = materials[instance_materials[inst.material_offset + td.surface]];

    float u = h.bary.x;
    float v = h.bary.y;
    float w = 1.0 - u - v;

    s.position = ray.o + ray.d * h.t;
    s.out_dir = -ray.d;

    vec3 ng = normalize(xform_normal(inst.w2o, cross(tri.e1.xyz, tri.e2.xyz)));
    vec3 ns = normalize(xform_normal(inst.w2o, td.n0 * w + td.n1.xyz * u + td.n2.xyz * v));
    if (any(isnan(ns)) || dot(ns, ns) < 0.5) ns = ng;
    if (dot(ng, s.out_dir) < 0.0) { // back face: flip both normals towards the viewer
        ng = -ng;
        ns = -ns;
    }
    // Shading normals can point away from the viewer near silhouettes; fall back to the geometric one.
    if (dot(ns, s.out_dir) < 1e-3) ns = ng;
    s.geo_normal = ng;
    s.normal = ns;
    s.lambert_out = max(dot(ns, s.out_dir), 1e-4);

    vec2 uv = (td.uv0 * w + td.uv1 * u + td.uv2 * v) * m.uv_scale_offset.xy + m.uv_scale_offset.zw;
    vec3 albedo = m.albedo.rgb;
    if (m.albedo_texture >= 0) {
        albedo *= textureLod(albedo_textures, vec3(uv, float(m.albedo_texture)), 0.0).rgb;
    }

    s.emission = m.emission.rgb;
    s.emission_luminance = m.emission.a;
    s.fresnel_0 = mix(vec3(0.04), albedo, m.metallic);
    s.diffuse_albedo = albedo * (1.0 - m.metallic);
    float alpha = m.roughness * m.roughness;
    s.roughness = max(alpha, 0.01);
    s.area = triangle_world_area(inst, tri);
    return s;
}

vec3 offset_origin(const ShadingInfo s, vec3 dir) {
    // Push the origin off the surface on the side the new ray leaves through.
    float scale = 1e-4 * max(1.0, max(max(abs(s.position.x), abs(s.position.y)), abs(s.position.z)));
    return s.position + s.geo_normal * (dot(dir, s.geo_normal) >= 0.0 ? scale : -scale);
}

float power_heuristic(float a, float b) {
    float a2 = a * a;
    float b2 = b * b;
    return a2 / max(a2 + b2, 1e-20);
}

// Probability (solid angle) that next event estimation picks the point `s` as seen from `from`.
float emissive_light_pdf(const ShadingInfo s, vec3 from) {
    if (frame.emissive_count == 0u || frame.emissive_total_power <= 0.0) return 0.0;
    vec3 to_light = s.position - from;
    float dist2 = dot(to_light, to_light);
    float cos_l = abs(dot(s.geo_normal, normalize(to_light)));
    if (cos_l < 1e-6) return 0.0;
    float pmf = s.emission_luminance * s.area / frame.emissive_total_power;
    return pmf * dist2 / (cos_l * s.area);
}

uint pick_emissive_triangle(float u) {
    // Binary search on the inclusive CDF.
    uint lo = 0u;
    uint hi = frame.emissive_count - 1u;
    while (lo < hi) {
        uint mid = (lo + hi) >> 1u;
        if (emissive_triangles[mid].cdf < u) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

// ----------------------------------- DIRECT LIGHTING -----------------------------------

vec3 sample_emissive(const ShadingInfo s, inout uint rng) {
    EmissiveTriangle light = emissive_triangles[pick_emissive_triangle(rand(rng))];
    Instance inst = instances[light.instance];
    Triangle tri = triangles[light.triangle];
    TriangleData td = triangle_data[light.triangle];
    Material m = materials[instance_materials[inst.material_offset + td.surface]];

    vec2 r = rand2(rng);
    if (r.x + r.y > 1.0) r = 1.0 - r;
    vec3 p_obj = tri.v0.xyz + tri.e1.xyz * r.x + tri.e2.xyz * r.y;
    vec3 p = xform_point(inst.o2w, p_obj);
    vec3 e1 = xform_vector(inst.o2w, tri.e1.xyz);
    vec3 e2 = xform_vector(inst.o2w, tri.e2.xyz);
    vec3 n_cross = cross(e1, e2);
    float area = 0.5 * length(n_cross);
    vec3 nl = n_cross / (2.0 * area);

    vec3 origin = offset_origin(s, p - s.position);
    vec3 to_light = p - origin;
    float dist2 = dot(to_light, to_light);
    float dist = sqrt(dist2);
    vec3 wi = to_light / dist;

    float cos_s = dot(s.normal, wi);
    float cos_l = abs(dot(nl, wi)); // emitters are two-sided
    if (cos_s <= 0.0 || cos_l < 1e-6 || dot(s.geo_normal, wi) <= 0.0) return vec3(0.0);

    float pmf = light.power / frame.emissive_total_power;
    float pdf_light = pmf * dist2 / (cos_l * area);
    if (!(pdf_light > 0.0)) return vec3(0.0);

    Ray shadow;
    shadow.o = origin;
    shadow.d = wi;
    if (trace_any(shadow, dist * (1.0 - 1e-4))) return vec3(0.0);

    vec3 f = brdf(s, wi) * cos_s;
    float pdf_bsdf = get_brdf_density(s, wi);
    float weight = power_heuristic(pdf_light, pdf_bsdf);
    return f * m.emission.rgb * weight / pdf_light;
}

float godot_omni_attenuation(float dist, float range, float decay) {
    float nd = dist / max(range, 1e-4);
    nd *= nd;
    nd *= nd;
    nd = max(1.0 - nd, 0.0);
    nd *= nd;
    return nd * pow(max(dist, 1e-4), -decay);
}

vec3 sample_punctual(const ShadingInfo s, const PunctualLight light, inout uint rng) {
    vec3 wi;
    float dist;
    vec3 radiance;
    if (light.type == LIGHT_DIRECTIONAL) {
        vec3 l = -light.direction;
        if (light.radius > 0.0) { // soft shadows: sample a cone around the sun direction
            mat3 b = basis_from_normal(l);
            vec2 disk = sample_disk(rand2(rng)) * light.radius;
            l = normalize(b * vec3(disk, 1.0));
        }
        wi = l;
        dist = INF;
        radiance = light.color;
    } else {
        vec3 target = light.position;
        if (light.radius > 0.0) target += sample_sphere(rand2(rng)) * light.radius;
        vec3 to_light = target - s.position;
        dist = length(to_light);
        if (dist < 1e-5 || dist > light.range) return vec3(0.0);
        wi = to_light / dist;
        radiance = light.color * godot_omni_attenuation(dist, light.range, light.attenuation);
        if (light.type == LIGHT_SPOT) {
            float c = dot(-wi, light.direction);
            radiance *= smoothstep(light.spot_cos_outer, light.spot_cos_inner, c);
        }
    }
    float cos_s = dot(s.normal, wi);
    if (cos_s <= 0.0 || dot(s.geo_normal, wi) <= 0.0 || luminance(radiance) <= 0.0) return vec3(0.0);

    Ray shadow;
    shadow.o = offset_origin(s, wi);
    shadow.d = wi;
    if (trace_any(shadow, dist == INF ? INF : dist * (1.0 - 1e-4))) return vec3(0.0);
    return brdf(s, wi) * cos_s * radiance;
}

// ----------------------------------- INTEGRATOR -----------------------------------

vec3 clamp_contribution(vec3 c, uint depth) {
    if (depth == 0u || frame.firefly_clamp <= 0.0) return c;
    float l = luminance(c);
    return l > frame.firefly_clamp ? c * (frame.firefly_clamp / l) : c;
}

vec3 path_trace(Ray ray, inout uint rng) {
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    float prev_bsdf_pdf = 0.0;
    vec3 prev_position = ray.o;

    for (uint depth = 0u; depth <= frame.max_bounces; depth++) {
        Hit hit;
        if (!trace_closest(ray, INF, hit)) {
            radiance += clamp_contribution(throughput * sample_sky(ray.d), depth);
            break;
        }
        ShadingInfo s = get_shading(ray, hit);

        // Emission found by BSDF sampling, MIS-weighted against next event estimation.
        if (s.emission_luminance > 0.0) {
            float weight = 1.0;
            if (depth > 0u) {
                float pdf_light = emissive_light_pdf(s, prev_position);
                weight = power_heuristic(prev_bsdf_pdf, pdf_light);
            }
            radiance += clamp_contribution(throughput * s.emission * weight, depth);
        }

        if (depth == frame.max_bounces) break;

        // Next event estimation.
        vec3 direct = vec3(0.0);
        if (frame.emissive_count > 0u) {
            direct += sample_emissive(s, rng);
        }
        for (uint i = 0u; i < frame.punctual_count; i++) {
            direct += sample_punctual(s, punctual_lights[i], rng);
        }
        radiance += clamp_contribution(throughput * direct, depth + 1u);

        // Continue the path.
        vec3 wi = sample_brdf(s, rand2(rng));
        float cos_in = dot(s.normal, wi);
        if (cos_in <= 0.0 || dot(s.geo_normal, wi) <= 0.0) break;
        float pdf = get_brdf_density(s, wi);
        if (!(pdf > 1e-8)) break;
        throughput *= brdf(s, wi) * cos_in / pdf;

        // Russian roulette.
        if (depth >= 2u) {
            float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (rand(rng) > p) break;
            throughput /= p;
        }
        if (any(isnan(throughput)) || any(isinf(throughput))) break;

        prev_bsdf_pdf = pdf;
        prev_position = s.position;
        ray.o = offset_origin(s, wi);
        ray.d = wi;
    }
    return radiance;
}

vec3 debug_view(Ray ray) {
    Hit hit;
    bool found = trace_closest(ray, INF, hit);
    if (frame.debug_mode == DEBUG_TRAVERSAL) {
        float x = clamp(float(hit.steps) / 128.0, 0.0, 1.0);
        return vec3(x, x * x, 1.0 - x) * x;
    }
    if (!found) return sample_sky(ray.d);
    ShadingInfo s = get_shading(ray, hit);
    if (frame.debug_mode == DEBUG_NORMALS) return s.normal * 0.5 + 0.5;
    return s.diffuse_albedo + s.fresnel_0 * 0.0;
}

Ray camera_ray(vec2 pixel) {
    // Godot's RD projection already accounts for the flipped Vulkan framebuffer y axis.
    vec2 ndc = pixel / vec2(frame.size) * 2.0 - 1.0;
    vec4 view = frame.inv_projection * vec4(ndc, 0.5, 1.0);
    view.xyz /= view.w;
    Ray r;
    r.o = frame.cam_to_world[3].xyz;
    r.d = normalize(mat3(frame.cam_to_world) * normalize(view.xyz));
    return r;
}

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= frame.size.x || pos.y >= frame.size.y) return;

    if ((frame.flags & FLAG_COPY_ONLY) != 0u) {
        imageStore(color_image, pos, vec4(imageLoad(accum_image, pos).rgb, 1.0));
        return;
    }

    uint rng = pcg_hash(uint(pos.x) + pcg_hash(uint(pos.y) + pcg_hash(frame.frame_index)));

    vec3 color = vec3(0.0);
    uint spp = max(frame.samples_per_pixel, 1u);
    for (uint i = 0u; i < spp; i++) {
        vec2 pixel = vec2(pos) + rand2(rng);
        Ray ray = camera_ray(pixel);
        vec3 c = frame.debug_mode == DEBUG_NONE ? path_trace(ray, rng) : debug_view(camera_ray(vec2(pos) + 0.5));
        if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0);
        color += c;
    }
    color /= float(spp);

    if ((frame.flags & FLAG_ACCUMULATE) != 0u) {
        if (frame.sample_index > 0u) {
            vec3 prev = imageLoad(accum_image, pos).rgb;
            color = prev + (color - prev) / float(frame.sample_index + 1u);
        }
        imageStore(accum_image, pos, vec4(color, 1.0));
    }
    imageStore(color_image, pos, vec4(color, 1.0));
}
