#ifndef BHV_H
#define BHV_H

#include "vec.h"
#include "../utils.h"
#include <algorithm>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

//uncomment to print some info about bvh
// #define VERBOSE_BVH_BUILDING

using namespace godot;

namespace BVH
{

struct Triangle
{
    vec4 vertices[3];
    vec4 centroid;
    vec4 normals[3];
    vec2 uvs[3];
    unsigned int materialIndex;
};

struct BoundingBox
{
    vec4 min;
    vec4 max;
    BoundingBox();

    void extend(const vec4 &point);
    bool intersect(const BoundingBox &other) const;
    float area() const
    {
        vec4 d = max - min;
        return d.x * d.y + d.y * d.z + d.z * d.x;
    }
};

struct BVHNode
{
    vec4 aabbMin;
    vec4 aabbMax;
    unsigned int left_child;
    unsigned int right_child;
    unsigned int first_tri_index;
    unsigned int tri_count;
};

struct TLASNode
{
    vec3 aabbMin;
    unsigned int leftRight; // 2x16 bits //if 0, it is a leaf node
    vec3 aabbMax;
    unsigned int blas;
};

struct BLASInstance
{
    float transform[16];
    float inverse_transform[16];
    vec4 aabbMin;
    vec4 aabbMax;
    unsigned int blas_index; // index to the right BLAS BVH node
    unsigned int material[3];

    void set_materials(const std::vector<int> &material_ids)
    {
        for (size_t i = 0; i < std::min((int)material_ids.size(), 3); i++)
        {
            material[i] = material_ids[i];
        }        
    }

    // have an array of material ids? say up to 4/8/16 or something
    void set_transform(const godot::Transform3D &t, const std::vector<BVHNode> &nodes)
    {
        Utils::transform_to_float(transform, t);  
        Utils::transform_to_float(inverse_transform, t.affine_inverse());  
        update_aabb(t, nodes[blas_index]);
    }

  private:
    void update_aabb(const godot::Transform3D &t, const BVHNode &node)
    {
        // Update the AABB
        aabbMin = vec4(1e34f, 1e34f, 1e34f, 1.0f);
        aabbMax = vec4(-1e34f, -1e34f, -1e34f, 1.0f);
        vec4 bmin = node.aabbMin;
        vec4 bmax = node.aabbMax;
        for (size_t i = 0; i < 8; i++)
        {
            vec4 corner = vec4(i & 1 ? bmax.x : bmin.x, i & 2 ? bmax.y : bmin.y, i & 4 ? bmax.z : bmin.z, 1.0f);
            vec4 transformed_corner = vec4(0.0f, 0.0f, 0.0f, 1.0f);
            // #pragma unroll
            for (int j = 0; j < 4; j++)
            {
                // #pragma unroll
                for (int k = 0; k < 4; k++)
                {
                    transformed_corner[j] += transform[k * 4 + j] * corner[k];
                }
            }
            transformed_corner = transformed_corner * (2.0f / transformed_corner.w);

            aabbMin = aabbMin.min(transformed_corner);
            aabbMax = aabbMax.max(transformed_corner);
        }
    }
};

class BVHBuilder
{
  public:
    unsigned int BuildBVH(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles,
                          const Ref<ArrayMesh> &arrayMesh);
    void print_tree(const std::vector<BVHNode> &nodes);
    // float EvaluateSAH()

  private:
    BoundingBox compute_bounding_box(const std::vector<Triangle> &triangles, const int start, const int end) const;
    float EvaluateSAH(const std::vector<Triangle> &triangles, const BVHNode &node, const int axis,
                      float& bestSplit) const;
    unsigned int build_recursive(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles, int start, int end);
};

class TLAS
{
  public:
    void build(std::vector<TLASNode> &nodes, const std::vector<BLASInstance> &blasInstances);
    void print_tree(const std::vector<TLASNode> &nodes);

  private:
    int FindBestMatch(const std::vector<TLASNode> &tlasNodes, const std::vector<int> &list, const int N,
                      const int A) const;
};

} // namespace BVH

#endif // BHV_H
