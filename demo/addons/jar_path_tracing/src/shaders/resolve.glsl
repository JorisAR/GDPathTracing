#[compute]
#version 460

// Temporal resolve: blends this frame's path traced color into the accumulated history, reprojecting
// the history with the motion written by the tracing pass and rejecting it where the surface
// changed. Also accumulates albedo and luminance moments for the denoiser, and writes the
// (undenoised) result to the scene color buffer.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define FLAG_REPROJECT 1u // reproject the history (otherwise read it at the same pixel)
#define FLAG_COPY_ONLY 2u // nothing was traced this frame: just present the history
#define FLAG_RESET 4u     // discard the history
#define FLAG_STATIC 8u    // nothing moved for a while: keep the history at the same pixel, no surface test

layout(push_constant, std430) uniform Params {
    ivec2 size;
    uint flags;
    uint history_cap; // max history length used for blending (1 = no accumulation)
} params;

layout(set = 0, binding = 0, rgba32f) uniform restrict readonly image2D frame_color;
layout(set = 0, binding = 1, rgba32f) uniform restrict readonly image2D frame_motion;
layout(set = 0, binding = 2, rgba32f) uniform restrict readonly image2D frame_albedo;
layout(set = 0, binding = 3, rgba32f) uniform restrict readonly image2D gbuffer_cur;  // xyz normal, w distance (-1 sky)
layout(set = 0, binding = 4, rgba32f) uniform restrict readonly image2D gbuffer_prev;
layout(set = 0, binding = 5, rgba32f) uniform restrict readonly image2D history_in;   // rgb mean, a samples
layout(set = 0, binding = 6, rgba32f) uniform restrict readonly image2D aux_in;       // rgb albedo, a mean lum^2
layout(set = 0, binding = 7, rgba32f) uniform restrict writeonly image2D history_out;
layout(set = 0, binding = 8, rgba32f) uniform restrict writeonly image2D aux_out;
layout(set = 0, binding = 9, rgba16f) uniform restrict writeonly image2D color_image;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// History (rgb mean, a samples) for `pos`; `aux` gets the matching albedo/moments. Zero = rejected.
vec4 fetch_history(ivec2 pos, out vec4 aux) {
    aux = vec4(0.0);
    if ((params.flags & FLAG_RESET) != 0u) return vec4(0.0);
    // Static view: the history is this pixel's. No surface test, because with jittered camera rays an
    // edge pixel sees different surfaces from frame to frame; rejecting those would restart it every
    // frame and leave single-sample speckles along every edge.
    if ((params.flags & (FLAG_REPROJECT | FLAG_STATIC)) != FLAG_REPROJECT) {
        aux = imageLoad(aux_in, pos);
        return imageLoad(history_in, pos);
    }

    vec4 motion = imageLoad(frame_motion, pos);
    if (motion.w <= 0.0) return vec4(0.0);
    vec4 g = imageLoad(gbuffer_cur, pos);
    float expected = motion.z;
    ivec2 base = ivec2(floor(motion.xy));
    vec2 f = motion.xy - vec2(base);

    vec4 sum = vec4(0.0);
    vec4 aux_sum = vec4(0.0);
    float weight_sum = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 tap = base + ivec2(i & 1, i >> 1);
        if (any(lessThan(tap, ivec2(0))) || any(greaterThanEqual(tap, params.size))) continue;
        float w = ((i & 1) == 1 ? f.x : 1.0 - f.x) * ((i >> 1) == 1 ? f.y : 1.0 - f.y);
        vec4 gp = imageLoad(gbuffer_prev, tap);
        bool valid;
        if (expected < 0.0) {
            valid = gp.w < 0.0; // sky only matches sky
        } else {
            valid = gp.w >= 0.0 && abs(gp.w - expected) < 0.03 * expected + 0.02 && dot(gp.xyz, g.xyz) > 0.8;
        }
        if (!valid) continue;
        sum += imageLoad(history_in, tap) * w;
        aux_sum += imageLoad(aux_in, tap) * w;
        weight_sum += w;
    }
    if (weight_sum < 0.05) return vec4(0.0);
    aux = aux_sum / weight_sum;
    return sum / weight_sum;
}

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pos, params.size))) return;

    if ((params.flags & FLAG_COPY_ONLY) != 0u) {
        imageStore(color_image, pos, vec4(imageLoad(history_in, pos).rgb, 1.0));
        return;
    }

    vec3 color = imageLoad(frame_color, pos).rgb;
    vec3 albedo = imageLoad(frame_albedo, pos).rgb;

    vec4 aux_history;
    vec4 history = fetch_history(pos, aux_history);
    float n = min(history.a + 1.0, float(max(params.history_cap, 1u)));
    vec3 result = mix(history.rgb, color, 1.0 / n);
    float l = luminance(color);
    vec4 aux = mix(aux_history, vec4(albedo, l * l), 1.0 / n);

    imageStore(history_out, pos, vec4(result, n));
    imageStore(aux_out, pos, aux);
    imageStore(color_image, pos, vec4(result, 1.0));
}
