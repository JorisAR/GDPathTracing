#[compute]
#version 460

// Spatial denoiser (SVGF-style, without the temporal part, which the integrator already does).
//
// Runs on the accumulated history after the path tracing pass:
//   MODE_PREPARE  demodulate albedo and estimate per-pixel variance of the accumulated mean
//                 (temporal luminance moments, or a 3x3 spatial estimate while history is short).
//   MODE_FILTER   one a-trous wavelet iteration (5x5 B3 spline kernel, `step` pixels apart),
//                 edge-stopped by normal, distance and luminance relative to the variance.
//   MODE_FINAL    same as MODE_FILTER, then re-applies albedo and writes the color buffer.
//
// The luminance edge-stopping scales with sqrt(variance / samples): as the image converges the
// filter shrinks to nothing, so static scenes still converge to the unbiased result.
//
// Geometric edge-stopping uses the pixel compatibility heuristic of Junkins et al. 2026,
// "Compatibility-Guided Neighbor Selection for ReSTIR" (eqs. 14/15):
//   h(x, y) = exp(-|x - y| / s) * max(n_x . n_y, 0)^beta,   s = sqrt(omega * d^2 / pi)
// with x, y the primary hits reconstructed from the G-buffer and d the camera distance, so the
// position term is a world-space radius that scales with the pixel footprint. A plane-distance
// term (|n_x . (y - x)| against the pixel footprint) separates surfaces at different depths,
// which the paper leaves to the shift mapping but a filter has to do itself.
// The short-history variance estimate samples K candidates from a disk (R2 sequence + concentric
// mapping, randomized start, Section 5) weighted by h, instead of a binary-rejected 3x3.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define M_PI 3.141592653589793

#define MODE_PREPARE 0
#define MODE_FILTER 1
#define MODE_FINAL 2

layout(push_constant, std430) uniform Params {
    ivec2 size;
    int step;
    int mode;
    float sigma_luminance;
    float beta;        // normal exponent of the compatibility heuristic
    float sigma_plane; // plane distance tolerance, in pixel footprints per pixel of tap offset
    float omega;       // solid angle (sr) of the position term's world-space radius
    mat4 pixel_to_dir; // camera rotation * inverse projection: NDC -> camera-relative world direction
    uint frame;
    int candidates;    // K for the variance estimate
    float radius;      // disk radius in pixels for the variance estimate
    float pad;
} params;

layout(set = 0, binding = 0, rgba32f) uniform restrict readonly image2D history; // rgb mean, a samples
layout(set = 0, binding = 1, rgba32f) uniform restrict readonly image2D gbuffer; // xyz normal, w distance (-1 sky)
layout(set = 0, binding = 2, rgba32f) uniform restrict readonly image2D aux;     // rgb albedo, a mean lum^2
layout(set = 0, binding = 3, rgba32f) uniform restrict readonly image2D src;     // rgb illumination, a variance
layout(set = 0, binding = 4, rgba32f) uniform restrict writeonly image2D dst;
layout(set = 0, binding = 5, rgba16f) uniform restrict writeonly image2D color_image;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 safe_albedo(vec3 a) {
    return max(a, vec3(0.01));
}

bool inside(ivec2 p) {
    return all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, params.size));
}

// Camera-relative world direction through the centre of pixel p (not normalized).
vec3 pixel_dir(vec2 p) {
    vec2 ndc = p / vec2(params.size) * 2.0 - 1.0;
    vec4 v = params.pixel_to_dir * vec4(ndc, 0.5, 1.0);
    return v.xyz / v.w;
}

// Camera-relative world position of the primary hit of pixel p at camera distance d.
vec3 hit_position(ivec2 p, float d) {
    return normalize(pixel_dir(vec2(p) + 0.5)) * d;
}

// Angle subtended by one pixel around pixel p (radians, small angle).
float pixel_angle(ivec2 p) {
    vec3 a = normalize(pixel_dir(vec2(p) + 0.5));
    vec3 b = normalize(pixel_dir(vec2(p) + vec2(1.5, 0.5)));
    return length(b - a);
}

// Paper's compatibility heuristic h = h_p * h_n (eqs. 14, 15).
float compatibility(vec3 x, vec3 nx, float dx, vec3 y, vec3 ny) {
    float s = sqrt(params.omega / M_PI) * dx;
    float h_p = exp(-length(x - y) / max(s, 1e-6));
    float h_n = pow(max(dot(nx, ny), 0.0), params.beta);
    return h_p * h_n;
}

// R2 low-discrepancy sequence (Roberts 2018).
vec2 r2(uint i) {
    return fract(vec2(0.7548776662466927, 0.5698402909980532) * float(i) + 0.5);
}

// Concentric square -> disk mapping (Shirley and Chiu 1997).
vec2 concentric_disk(vec2 u) {
    u = u * 2.0 - 1.0;
    if (u.x == 0.0 && u.y == 0.0) return vec2(0.0);
    float r, phi;
    if (abs(u.x) > abs(u.y)) {
        r = u.x;
        phi = (M_PI / 4.0) * (u.y / u.x);
    } else {
        r = u.y;
        phi = (M_PI / 2.0) - (M_PI / 4.0) * (u.x / u.y);
    }
    return r * vec2(cos(phi), sin(phi));
}

uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void prepare(ivec2 pos) {
    vec4 h = imageLoad(history, pos);
    vec4 g = imageLoad(gbuffer, pos);
    vec4 a = imageLoad(aux, pos);
    vec3 albedo = safe_albedo(a.rgb);
    vec3 illum = h.rgb / albedo;
    float n = max(h.a, 1.0);
    float alb_l = max(luminance(albedo), 0.01);

    float variance;
    if (n >= 4.0 || g.w < 0.0) {
        // Temporal moments of the color luminance, converted to illumination units.
        float mean = luminance(h.rgb);
        variance = max(a.a - mean * mean, 0.0) / (alb_l * alb_l);
    } else {
        // Too few samples for moments: spatial variance over compatible neighbours. K candidates
        // from a disk, weighted by the compatibility heuristic (continuous, so a pixel with only
        // borderline neighbours still gets an estimate instead of none).
        vec3 x = hit_position(pos, g.w);
        float l0 = luminance(illum);
        float s1 = l0, s2 = l0 * l0, wsum = 1.0;
        uint start = pcg_hash(uint(pos.x) + pcg_hash(uint(pos.y) + pcg_hash(params.frame))) & 0xFFFFu;
        for (int k = 0; k < params.candidates; k++) {
            ivec2 q = pos + ivec2(round(concentric_disk(r2(start + uint(k))) * params.radius));
            if (!inside(q) || q == pos) continue;
            vec4 gq = imageLoad(gbuffer, q);
            if (gq.w < 0.0) continue;
            float w = compatibility(x, g.xyz, g.w, hit_position(q, gq.w), gq.xyz);
            if (w < 1e-3) continue;
            vec3 iq = imageLoad(history, q).rgb / safe_albedo(imageLoad(aux, q).rgb);
            float l = luminance(iq);
            s1 += l * w;
            s2 += l * l * w;
            wsum += w;
        }
        s1 /= wsum;
        s2 /= wsum;
        variance = max(s2 - s1 * s1, 0.0) * 4.0; // boost: the estimate is crude at first
    }
    // Variance of the accumulated mean, not of a single sample.
    imageStore(dst, pos, vec4(illum, variance / n));
}

void filter_step(ivec2 pos) {
    vec4 center = imageLoad(src, pos);
    vec4 g = imageLoad(gbuffer, pos);

    if (g.w < 0.0) { // sky: nothing to filter
        if (params.mode == MODE_FINAL) {
            imageStore(color_image, pos, vec4(imageLoad(history, pos).rgb, 1.0));
        } else {
            imageStore(dst, pos, center);
        }
        return;
    }

    // Blur the variance a little (3x3 gaussian) for a stable luminance edge-stop.
    float var_blur = 0.0;
    float var_w = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            ivec2 q = pos + ivec2(x, y);
            if (!inside(q)) continue;
            float k = (x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25);
            var_blur += imageLoad(src, q).a * k;
            var_w += k;
        }
    }
    var_blur /= var_w;

    // Primary hit and pixel footprint for the geometric edge-stopping.
    vec3 x = hit_position(pos, g.w);
    float footprint = pixel_angle(pos) * g.w; // world size of one pixel at the hit

    float l_center = luminance(center.rgb);
    float l_scale = params.sigma_luminance * sqrt(max(var_blur, 0.0)) + 1e-4;

    const float kernel[3] = float[3](3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);
    vec3 sum = vec3(0.0);
    float var_sum = 0.0;
    float w_sum = 0.0;
    for (int y_off = -2; y_off <= 2; y_off++) {
        for (int x_off = -2; x_off <= 2; x_off++) {
            ivec2 q = pos + ivec2(x_off, y_off) * params.step;
            if (!inside(q)) continue;
            vec4 gq = imageLoad(gbuffer, q);
            if (gq.w < 0.0) continue;
            vec4 sq = imageLoad(src, q);

            vec3 y = hit_position(q, gq.w);
            float tap_px = max(length(vec2(x_off, y_off)) * float(params.step), 1.0);
            float w_h = compatibility(x, g.xyz, g.w, y, gq.xyz);
            float w_plane = exp(-abs(dot(g.xyz, y - x)) / (params.sigma_plane * footprint * tap_px + 1e-5));
            float w_l = exp(-abs(l_center - luminance(sq.rgb)) / l_scale);
            float w = kernel[abs(x_off)] * kernel[abs(y_off)] * w_h * w_plane * w_l;

            sum += sq.rgb * w;
            var_sum += sq.a * w * w;
            w_sum += w;
        }
    }
    // w_sum > 0: the center tap always has weight kernel[0]^2.
    vec3 illum = sum / w_sum;
    float variance = var_sum / (w_sum * w_sum);

    if (params.mode == MODE_FINAL) {
        vec3 albedo = safe_albedo(imageLoad(aux, pos).rgb);
        imageStore(color_image, pos, vec4(illum * albedo, 1.0));
    } else {
        imageStore(dst, pos, vec4(illum, variance));
    }
}

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (!inside(pos)) return;
    if (params.mode == MODE_PREPARE) {
        prepare(pos);
    } else {
        filter_step(pos);
    }
}
