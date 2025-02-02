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


//image stack
//frame count

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= width || pos.y >= height) return;

    vec3 color = imageLoad(screenTexture, pos).rgb;
    if(frame_count > 1) {
        color += imageLoad(frameBuffer, pos).rgb;
    }

    imageStore(frameBuffer, pos, vec4(color, 1.0));
    imageStore(screenTexture, pos, vec4(color / frame_count, 1.0));
}