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
    float alpha_tested; // 1 if some instance's material alpha tests this triangle (cutout textures)
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
    float tangent_x; // per-triangle tangent (object space, from the UVs) for normal maps
    float n2[3];
    float tangent_y;
    float uv0[2];
    float uv1[2];
    float uv2[2];
    float tangent_z;
    float bitangent_sign;
};
static_assert(sizeof(GpuTriangleData) == 80, "GpuTriangleData must be 80 bytes");

enum MaterialFlags : uint32_t
{
    MATERIAL_FLAG_EMISSION_MULTIPLY = 1u << 0, // emission = color * texture (else color + texture)
    MATERIAL_FLAG_NORMAL_DIRECTX = 1u << 1,    // DirectX convention normal map (green = +v, i.e. down)
};

struct GpuMaterial
{
    float albedo[4];   // linear rgb, a unused
    float emission[4]; // linear rgb * energy, a = luminance of emission
    float metallic;
    float roughness;
    int32_t albedo_texture; // layer in the texture array, -1 if none
    uint32_t flags;
    float uv_scale_offset[4]; // uv1_scale.xy, uv1_offset.xy
    int32_t emission_texture; // layer in the texture array, -1 if none
    float emission_energy;    // multiplies the emission texture (Godot's emission_energy_multiplier)
    // Layers in the linear (data) texture array, -1 if none.
    int32_t roughness_texture;
    int32_t metallic_texture;
    int32_t normal_texture;
    uint32_t channels;  // bits 0-2: roughness channel, 3-5: metallic channel (Godot's TextureChannel)
    float alpha_cutoff; // > 0: alpha scissor threshold; < 0: stochastic alpha; 0 = opaque
    float normal_scale;
};
static_assert(sizeof(GpuMaterial) == 96, "GpuMaterial must be 96 bytes");

enum InstanceFlags : uint32_t
{
    INSTANCE_FLAG_EMISSIVE = 1u << 0,
};

// Mesh instance. Matrices are stored as 3 rows of a 3x4 affine matrix.
struct GpuInstance
{
    float object_to_world[12];
    float world_to_object[12];
    float prev_object_to_world[12]; // previous frame, for motion vectors
    uint32_t bvh_root;        // index of the BLAS root node
    uint32_t material_offset; // offset into the instance material table
    uint32_t triangle_offset; // offset of the mesh's first triangle (for light lookups)
    uint32_t flags;
    // Emissive instances: index of the mesh's first triangle in the emissive triangle buffer, which
    // holds every triangle of the instance (power 0 for non-emitting ones), so a triangle found by
    // a BSDF ray finds the exact power the light sampler used.
    uint32_t emissive_offset;
    uint32_t pad[3];
};
static_assert(sizeof(GpuInstance) == 176, "GpuInstance must be 176 bytes");

// Emissive triangle used for next event estimation, self-contained so that sampling it takes one
// lookup (world space corners, UVs and material) instead of instance -> triangle -> material.
// Selected proportional to power with an alias table (alias_table.h): entry floor(u * n) is kept
// with probability alias_prob, otherwise its alias is used.
struct GpuEmissiveTriangle
{
    float v0[3];      // world space
    float power;      // luminance(emission) * world-space area
    float e1[3];      // world space v1 - v0
    float alias_prob; // probability of keeping this entry
    float e2[3];      // world space v2 - v0
    uint32_t alias;   // entry used otherwise
    float uv0[2];
    float uv1[2];
    float uv2[2];
    uint32_t material; // index into the material buffer
    uint32_t pad;
};
static_assert(sizeof(GpuEmissiveTriangle) == 80, "GpuEmissiveTriangle must be 80 bytes");

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

// Portal rectangle (local z = 0 plane, |x| <= half_size.x, |y| <= half_size.y, front faces +z).
struct GpuPortal
{
    float world_to_local[12];
    float teleport[12]; // world -> world for rays entering through the front
    float tint[4];
    float half_size[2];
    uint32_t linked;
    uint32_t pad;
};
static_assert(sizeof(GpuPortal) == 128, "GpuPortal must be 128 bytes");

} // namespace PT

#endif // PT_GPU_TYPES_H
