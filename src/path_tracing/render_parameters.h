#ifndef RENDER_PARAMETERS
#define RENDER_PARAMETERS

#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <cstring> // for std::memcpy
#include "bvh/vec.h"

using namespace godot;

struct RenderParameters // match the struct on the gpu
{
    int width;
    int height;
    float fov;
    unsigned int triangleCount;
    Vector4 cameraPosition;
    Vector4 cameraUp;
    Vector4 cameraRight;
    Vector4 cameraForward;
    unsigned int blasCount;
   

    RenderParameters(int width, int height, float fov, unsigned int triangleCount, unsigned int blasCount, Vector3 cameraPosition, Vector3 cameraForward, Vector3 cameraRight, Vector3 cameraUp)
        : width(width), height(height), fov(fov), triangleCount(triangleCount), blasCount(blasCount)
    {
        this->cameraPosition = Vector4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
        this->cameraForward = Vector4(cameraForward.x, cameraForward.y, cameraForward.z, 0.0f);
        this->cameraRight = Vector4(cameraRight.x, cameraRight.y, cameraRight.z, 0.0f);
        this->cameraUp = Vector4(cameraUp.x, cameraUp.y, cameraUp.z, 0.0f);
    }

    PackedByteArray to_packed_byte_array()
    {
        PackedByteArray byte_array;
        byte_array.resize(sizeof(RenderParameters));
        std::memcpy(byte_array.ptrw(), this, sizeof(RenderParameters));
        return byte_array;
    }
};

struct GpuMaterial // Struct to be uploaded to the GPU
{
    BVH::vec4 albedo;
    float metallic;
    float roughness;
};

#endif // RENDER_PARAMETERS
