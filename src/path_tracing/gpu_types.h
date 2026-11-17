#ifndef PT_GPU_TYPES_H
#define PT_GPU_TYPES_H

// Plain structs that are uploaded verbatim to GPU storage buffers.
// Every struct here has a 1:1 counterpart in shaders/common.glsl (std430 layout).
// Keep both in sync; the static_asserts catch accidental padding changes on the C++ side.

#include <cstdint>

namespace PT
{

// Compact 32 byte BVH node (both for BLAS nodes).
// Interior node: tri_count == 0, children are at left_first and left_first + 1.
// Leaf node:     tri_count  > 0, triangles are [left_first, left_first + tri_count).
struct GpuBVHNode
{
    float aabb_min[3];
    uint32_t left_first;
    float aabb_max[3];
    uint32_t tri_count;
};
static_assert(sizeof(GpuBVHNode) == 32, "GpuBVHNode must be 32 bytes");

// TLAS node. Interior: left/right are child node indices.
// Leaf: right == TLAS_LEAF, left is the instance index.
static constexpr uint32_t TLAS_LEAF = 0xFFFFFFFFu;
struct GpuTLASNode
{
    float aabb_min[3];
    uint32_t left;
    float aabb_max[3];
    uint32_t right;
};
static_assert(sizeof(GpuTLASNode) == 32, "GpuTLASNode must be 32 bytes");

// Triangle stored as v0 + edges, which is what the intersection test needs.
struct GpuTriangle
{
    float v0[3];
    float pad0;
    float e1[3];
    float pad1;
    float e2[3];
    float pad2;
};
static_assert(sizeof(GpuTriangle) == 48, "GpuTriangle must be 48 bytes");

// Per-triangle shading attributes (object space).
struct GpuTriangleData
{
    float n0[3];
    uint32_t surface; // surface index within the mesh, resolved via the instance material table
    float n1[3];
    float pad0;
    float n2[3];
    float pad1;
    float uv0[2];
    float uv1[2];
    float uv2[2];
    float pad2[2];
};
static_assert(sizeof(GpuTriangleData) == 80, "GpuTriangleData must be 80 bytes");

struct GpuMaterial
{
    float albedo[4];   // linear rgb, a unused
    float emission[4]; // linear rgb * energy, a = luminance of emission
    float metallic;
    float roughness;
    int32_t albedo_texture; // layer in the texture array, -1 if none
    uint32_t flags;
    float uv_scale_offset[4]; // uv1_scale.xy, uv1_offset.xy
};
static_assert(sizeof(GpuMaterial) == 64, "GpuMaterial must be 64 bytes");

enum InstanceFlags : uint32_t
{
    INSTANCE_FLAG_EMISSIVE = 1u << 0,
};

// Mesh instance. Matrices are stored as 3 rows of a 3x4 affine matrix.
struct GpuInstance
{
    float object_to_world[12];
    float world_to_object[12];
    uint32_t bvh_root;        // index of the BLAS root node
    uint32_t material_offset; // offset into the instance material table
    uint32_t triangle_offset; // offset of the mesh's first triangle (for light lookups)
    uint32_t flags;
};
static_assert(sizeof(GpuInstance) == 112, "GpuInstance must be 112 bytes");

// Emissive triangle used for next event estimation. Selected proportional to power with a CDF.
struct GpuEmissiveTriangle
{
    uint32_t instance;
    uint32_t triangle; // global triangle index
    float cdf;         // inclusive prefix sum of power / total power
    float power;       // luminance(emission) * world-space area
};
static_assert(sizeof(GpuEmissiveTriangle) == 16, "GpuEmissiveTriangle must be 16 bytes");

enum PunctualLightType : uint32_t
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_OMNI = 1,
    LIGHT_SPOT = 2,
};

struct GpuPunctualLight
{
    float position[3]; // world position (omni/spot)
    uint32_t type;
    float direction[3]; // direction the light travels (directional/spot), normalized
    float radius;       // directional: tan(angular radius); omni/spot: physical radius for soft shadows
    float color[3];     // linear rgb * energy
    float range;
    float spot_cos_outer;
    float spot_cos_inner;
    float attenuation; // Godot omni/spot attenuation exponent
    float pad;
};
static_assert(sizeof(GpuPunctualLight) == 64, "GpuPunctualLight must be 64 bytes");

} // namespace PT

#endif // PT_GPU_TYPES_H
