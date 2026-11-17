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

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define MODE_PREPARE 0
#define MODE_FILTER 1
#define MODE_FINAL 2

layout(push_constant, std430) uniform Params {
    ivec2 size;
    int step;
    int mode;
    float sigma_luminance;
    float sigma_normal;
    float sigma_depth;
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
        // Too few samples for moments: spatial variance over similar neighbours.
        float s1 = 0.0, s2 = 0.0, wsum = 0.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec2 q = pos + ivec2(x, y);
                if (!inside(q)) continue;
                vec4 gq = imageLoad(gbuffer, q);
                if (gq.w < 0.0 || dot(gq.xyz, g.xyz) < 0.9) continue;
                vec3 iq = imageLoad(history, q).rgb / safe_albedo(imageLoad(aux, q).rgb);
                float l = luminance(iq);
                s1 += l;
                s2 += l * l;
                wsum += 1.0;
            }
        }
        s1 /= max(wsum, 1.0);
        s2 /= max(wsum, 1.0);
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

    // Screen-space distance gradient, so slanted surfaces are not treated as edges.
    float d_right = inside(pos + ivec2(1, 0)) ? imageLoad(gbuffer, pos + ivec2(1, 0)).w : g.w;
    float d_left = inside(pos - ivec2(1, 0)) ? imageLoad(gbuffer, pos - ivec2(1, 0)).w : g.w;
    float d_up = inside(pos + ivec2(0, 1)) ? imageLoad(gbuffer, pos + ivec2(0, 1)).w : g.w;
    float d_down = inside(pos - ivec2(0, 1)) ? imageLoad(gbuffer, pos - ivec2(0, 1)).w : g.w;
    float grad = 0.5 * max(abs(d_right - d_left), abs(d_up - d_down));
    if (d_right < 0.0 || d_left < 0.0 || d_up < 0.0 || d_down < 0.0) grad = 0.0;

    float l_center = luminance(center.rgb);
    float l_scale = params.sigma_luminance * sqrt(max(var_blur, 0.0)) + 1e-4;

    const float kernel[3] = float[3](3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);
    vec3 sum = vec3(0.0);
    float var_sum = 0.0;
    float w_sum = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            ivec2 q = pos + ivec2(x, y) * params.step;
            if (!inside(q)) continue;
            vec4 gq = imageLoad(gbuffer, q);
            if (gq.w < 0.0) continue;
            vec4 sq = imageLoad(src, q);

            float w_n = pow(max(dot(g.xyz, gq.xyz), 0.0), params.sigma_normal);
            float w_z = exp(-abs(g.w - gq.w) /
                            (params.sigma_depth * grad * length(vec2(x, y) * float(params.step)) + 0.002 * g.w + 1e-4));
            float w_l = exp(-abs(l_center - luminance(sq.rgb)) / l_scale);
            float w = kernel[abs(x)] * kernel[abs(y)] * w_n * w_z * w_l;

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
