#ifndef RENDER_PARAMETERS
#define RENDER_PARAMETERS

#include "bvh/utils.h"
#include "bvh/vec.h"
#include <cstring> // for std::memcpy
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector4.hpp>

using namespace godot;

struct Camera
{
    float vp[16];
    float ivp[16];
    BVH::vec4 position;
    unsigned int frame_index;

    void set_camera_transform(Transform3D &model, Projection &projection)
    {
        // Vector3 cameraPosition = transform.origin;
        // auto cameraForward = transform.basis.get_column(2);
        // auto cameraRight = transform.basis.get_column(0);
        // auto cameraUp = transform.basis.get_column(1);

        // this->cameraPosition = Vector4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
        // this->cameraForward = Vector4(cameraForward.x, cameraForward.y, cameraForward.z, 0.0f);
        // this->cameraRight = Vector4(cameraRight.x, cameraRight.y, cameraRight.z, 0.0f);
        // this->cameraUp = Vector4(cameraUp.x, cameraUp.y, cameraUp.z, 0.0f);
        position = BVH::vec4(model.origin.x, model.origin.y, model.origin.z, 1.0f);
        Projection t = projection * model.affine_inverse();
        BVH::projection_to_float(vp, t);
        BVH::projection_to_float(ivp, t.inverse());
    }

    PackedByteArray to_packed_byte_array()
    {
        PackedByteArray byte_array;
        byte_array.resize(sizeof(Camera));
        std::memcpy(byte_array.ptrw(), this, sizeof(Camera));
        return byte_array;
    }
};

struct GpuMaterial
{
    BVH::vec4 albedo;
    BVH::vec4 emission;
    float metallic;
    float roughness;
    int albedo_texture_index;
    float padding[5];
};

struct GpuTriangleGeometry
{
    BVH::vec4 vertices[3];
};

struct GpuTriangleData
{
    BVH::vec3 n1;
    unsigned int material_index;
    BVH::vec4 n2;
    BVH::vec4 n3;
    BVH::vec2 uvs[3];
};


#endif // RENDER_PARAMETERS
