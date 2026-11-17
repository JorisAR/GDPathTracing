#ifndef PT_BVH_H
#define PT_BVH_H

#include "path_tracing/gpu_types.h"

#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

namespace PT
{

struct BuildTriangle
{
    godot::Vector3 v0, v1, v2;
};

// Bottom level BVH: binned SAH builder producing compact 32 byte nodes where
// the two children of an interior node are stored next to each other.
class BLASBuilder
{
  public:
    static constexpr int MAX_DEPTH = 32;     // must match BLAS_STACK_SIZE in the shader
    static constexpr int BINS = 16;
    static constexpr int MAX_LEAF_SIZE = 4;

    // Builds a BVH over `tris` and appends its nodes to `nodes`. Child indices are absolute.
    // `order` receives the permutation of the input triangles (leaf ranges index into the
    // permuted list, offset by `triangle_offset`). Returns the index of the root node.
    static uint32_t build(const std::vector<BuildTriangle> &tris, uint32_t triangle_offset,
                          std::vector<GpuBVHNode> &nodes, std::vector<uint32_t> &order);
};

// Top level BVH over instance bounds, built with agglomerative clustering
// (Walter et al., as described by Jacco Bikker). Rebuilt on the CPU whenever an instance moves.
class TLASBuilder
{
  public:
    // `bounds[i]` is the world AABB of instance `instance_ids[i]`.
    // The root is always written to nodes[0]. With no instances a single empty leaf is emitted
    // whose bounds can never be hit.
    static void build(const std::vector<godot::AABB> &bounds, const std::vector<uint32_t> &instance_ids,
                      std::vector<GpuTLASNode> &nodes);
};

} // namespace PT

#endif // PT_BVH_H
