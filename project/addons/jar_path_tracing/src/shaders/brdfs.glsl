#define M_PI 3.141592653589793238462643

vec3 fresnel_schlick(vec3 f0, vec3 f90, float cosine_theta) {
    float factor = 1.0 - cosine_theta;
    float factor_squared = factor * factor;
    float factor_fifth = factor_squared * factor_squared * factor;
    return mix(f0, f90, factor_fifth);
}

vec3 brdf(ShadingInfo shading, vec3 light_dir) {
    float n_dot_light = dot(shading.normal, light_dir);
    float n_dot_view = shading.lambert_out;
    
    if (min(n_dot_light, n_dot_view) < 0.0) return vec3(0.0);
    
    vec3 half_vector = normalize(light_dir + shading.out_dir);
    float half_dot_view = dot(half_vector, shading.out_dir);
    
    float f90 = (half_dot_view * half_dot_view) * (2.0 * shading.roughness) + 0.5;
    float diffuse_fresnel = fresnel_schlick(vec3(1.0), vec3(f90), n_dot_view).x
                          * fresnel_schlick(vec3(1.0), vec3(f90), n_dot_light).x;
    
    vec3 brdf = diffuse_fresnel * shading.diffuse_albedo;
    
    float half_dot_normal = dot(half_vector, shading.normal);
    float roughness_sq = shading.roughness * shading.roughness;
    float denominator = half_dot_normal * (roughness_sq - 1.0) + 1.0;
    float distribution = roughness_sq / (denominator * denominator);
    
    float masking = n_dot_light * sqrt((n_dot_view - roughness_sq * n_dot_view) * n_dot_view + roughness_sq);
    float shadowing = n_dot_view * sqrt((n_dot_light - roughness_sq * n_dot_light) * n_dot_light + roughness_sq);
    float geometry = 0.5 / (masking + shadowing);
    
    vec3 specular_fresnel = fresnel_schlick(shading.fresnel_0, vec3(1.0), max(0.0, half_dot_view));
    brdf += distribution * geometry * specular_fresnel;
    
    return brdf / M_PI;
}

vec3 sample_ggx_vndf(vec3 view_dir, vec2 roughness, vec2 random_sample) {
    vec3 transformed_view = normalize(vec3(view_dir.xy * roughness, view_dir.z));
    float phi = 2.0 * M_PI * random_sample.x;
    float z = 1.0 - random_sample.y * (1.0 + transformed_view.z);
    
    float sin_theta = sqrt(max(0.0, 1.0 - z * z));
    vec3 hemisphere_sample = vec3(sin_theta * cos(phi), sin_theta * sin(phi), z);
    
    vec3 half_vector = normalize(vec3(
        (hemisphere_sample + transformed_view).xy * roughness,
        (hemisphere_sample + transformed_view).z
    ));
    
    return half_vector;
}

float get_ggx_vndf_density(float n_dot_view, float half_dot_normal, float half_dot_view, float roughness) {
    if (half_dot_normal < 0.0) return 0.0;
    
    float roughness_sq = roughness * roughness;
    float inv_roughness_sq = 1.0 - roughness_sq;
    float denominator = n_dot_view + sqrt(roughness_sq + inv_roughness_sq * n_dot_view * n_dot_view);
    
    float d_vis = max(0.0, half_dot_view) * (2.0 / M_PI) / denominator;
    float m_sq_term = 1.0 - inv_roughness_sq * half_dot_normal * half_dot_normal;
    
    return d_vis * roughness_sq / (m_sq_term * m_sq_term);
}

vec3 sample_ggx_in_dir(vec3 view_dir, float roughness, vec2 random_sample) {
    vec3 half_vector = sample_ggx_vndf(view_dir, vec2(roughness), random_sample);
    return -reflect(view_dir, half_vector);
}

float get_ggx_in_dir_density(float n_dot_view, vec3 view_dir, vec3 light_dir, vec3 normal, float roughness) {
    vec3 half_vector = normalize(light_dir + view_dir);
    float half_dot_view = dot(half_vector, view_dir);
    float half_dot_normal = dot(half_vector, normal);
    
    float density = get_ggx_vndf_density(n_dot_view, half_dot_normal, half_dot_view, roughness);
    return density / (4.0 * half_dot_view);
}

mat3 get_shading_space(vec3 normal) {
    float sign = normal.z > 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    
    return mat3(
        vec3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x),
        vec3(b, sign + normal.y * normal.y * a, -normal.y),
        normal
    );
}

vec3 sample_hemisphere_psa(vec2 random_sample) {
    float phi = 2.0 * M_PI * random_sample.x;
    float radius = sqrt(random_sample.y);
    float z = sqrt(1.0 - radius * radius);
    
    return vec3(radius * cos(phi), radius * sin(phi), z);
}

float get_hemisphere_psa_density(float sampled_z) {
    return max(0.0, sampled_z) / M_PI;
}

float get_diffuse_sampling_probability(ShadingInfo shading) {
    float luminance = dot(shading.diffuse_albedo, vec3(0.2126, 0.7152, 0.0722));
    return min(0.5, luminance);
}

vec3 sample_brdf(ShadingInfo shading, vec2 random_sample) {
    mat3 tangent_to_world = get_shading_space(shading.normal);
    float diffuse_prob = get_diffuse_sampling_probability(shading);
    
    vec3 sampled_dir;
    if (random_sample.x < diffuse_prob) {
        random_sample.x /= diffuse_prob;
        sampled_dir = tangent_to_world * sample_hemisphere_psa(random_sample);
    } else {
        random_sample.x = (random_sample.x - diffuse_prob) / (1.0 - diffuse_prob);
        vec3 local_view = transpose(tangent_to_world) * shading.out_dir;
        vec3 local_light = sample_ggx_in_dir(local_view, shading.roughness, random_sample);
        sampled_dir = tangent_to_world * local_light;
    }
    
    return sampled_dir;
}

float get_brdf_density(ShadingInfo shading, vec3 sampled_dir) {
    float diffuse_prob = get_diffuse_sampling_probability(shading);
    float specular_density = get_ggx_in_dir_density(
        shading.lambert_out, shading.out_dir, sampled_dir, shading.normal, shading.roughness
    );
    float diffuse_density = get_hemisphere_psa_density(dot(shading.normal, sampled_dir));
    
    return mix(specular_density, diffuse_density, diffuse_prob);
}