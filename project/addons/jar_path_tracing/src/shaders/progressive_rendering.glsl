#[compute]
#version 460

//in/out image
layout(set = 0, binding = 1, rgba8) restrict uniform image2D screenTexture;
// layout(set = 0, binding = 1) restrict buffer ScreenTexture{
//     float screenTexture[];
// };

layout(set = 0, binding = 2, rgba32f) restrict uniform image2D frameBuffer;

layout(std430, set = 0, binding = 0) restrict buffer Params {
    uint width;
    uint height;
    uint frame_count;
};

// ACES approximation from https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= width || pos.y >= height) return;

    vec3 radiance = imageLoad(screenTexture, pos).rgb;
    if(frame_count > 1) {
        radiance += imageLoad(frameBuffer, pos).rgb;
    }
    imageStore(frameBuffer, pos, vec4(radiance, 1.0));

    vec3 avgRadiance = radiance / frame_count;

    float exposure = 1.0f;

    vec3 color = acesFilm(avgRadiance * exposure);

    imageStore(screenTexture, pos, vec4(color, 1.0));
}