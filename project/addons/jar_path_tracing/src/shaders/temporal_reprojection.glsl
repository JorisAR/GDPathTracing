#[compute]
#version 460

layout(std430, set = 0, binding = 0) restrict buffer Params {
    mat4 reprojectionMatrix;
    uint width;
    uint height;
    uint frameCount;
    float blendFactor;
    float nearPlane;
    float farPlane;
};

layout(set = 0, binding = 1, rgba8) uniform image2D screenTexture;
layout(set = 0, binding = 2, r32f) uniform readonly image2D depthTexture;
layout(set = 0, binding = 3, rgba32f) uniform image2D frameBuffer1;
layout(set = 0, binding = 4, rgba32f) uniform image2D frameBuffer2;



// ACES tone mapping function
vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

layout(local_size_x = 32, local_size_y = 32) in;
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= width || pos.y >= height) return;

    vec3 currentColor = imageLoad(screenTexture, pos).rgb;
    float depth = imageLoad(depthTexture, pos).r;

    vec4 ndc = vec4(
        (pos.x + 0.5) / width * 2.0 - 1.0, 
        (pos.y + 0.5) / height * -2.0 + 1.0, 
        depth, 
        1.0
    );

    bool useFirstBuffer = (frameCount % 2) == 0;

    vec3 reprojectedColor = currentColor;
    if (frameCount > 0) {
        vec4 prevClipPos = reprojectionMatrix * ndc;
        prevClipPos.xyz /= prevClipPos.w;

        vec2 prevUV = vec2(
            (prevClipPos.x + 1.0) * 0.5,
            (1.0 - prevClipPos.y) * 0.5 // Flip Y
        );
        ivec2 prevPos = ivec2(prevUV * vec2(width, height));

        if(prevPos.x >= 0 && prevPos.x < width && prevPos.y >= 0 && prevPos.y < height && abs(imageLoad(depthTexture, prevPos).r - prevClipPos.z) < 0.1f) {
            reprojectedColor = useFirstBuffer ? imageLoad(frameBuffer1, prevPos).rgb : imageLoad(frameBuffer2, prevPos).rgb;
        }           
    }

    vec3 blendedColor = mix(currentColor, reprojectedColor, 0.75);

    useFirstBuffer ? imageStore(frameBuffer2, pos, vec4(blendedColor, 1.0)) : imageStore(frameBuffer1, pos, vec4(blendedColor, 1.0));

    vec3 finalColor = acesFilm(blendedColor);

    imageStore(screenTexture, pos, vec4(finalColor, 1.0));
}