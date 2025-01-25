#ifndef BHV_H
#define BHV_H

#include "vec.h"
#include <algorithm>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

using namespace godot;

namespace BVH
{

struct Triangle {
    vec4 vertices[3];
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
};

struct BVHNode {
    vec4 aabbMin;
    vec4 aabbMax;
    unsigned int left_child;
    unsigned int right_child;
    unsigned int first_tri_index;
    unsigned int tri_count;
};

struct BLASInstance {
    float transform[16];
    float inverse_transform[16];
    unsigned int blas_index; // index to the right BLAS BVH node
    unsigned int material_index;
    unsigned int padding[2];
    
    // have an array of material ids? say up to 4/8/16 or something

    void set_transform(const godot::Transform3D &t) {
        // Set the transform matrix
        transform[0] = t.basis.get_column(0).x;
        transform[1] = t.basis.get_column(0).y;
        transform[2] = t.basis.get_column(0).z;
        transform[3] = 0.0f;

        transform[4] = t.basis.get_column(1).x;
        transform[5] = t.basis.get_column(1).y;
        transform[6] = t.basis.get_column(1).z;
        transform[7] = 0.0f;

        transform[8] = t.basis.get_column(2).x;
        transform[9] = t.basis.get_column(2).y;
        transform[10] = t.basis.get_column(2).z;
        transform[11] = 0.0f;

        transform[12] = t.origin.x;
        transform[13] = t.origin.y;
        transform[14] = t.origin.z;
        transform[15] = 1.0f;

        // Set the inverse transform matrix
        godot::Transform3D inv_t = t.affine_inverse();

        inverse_transform[0] = inv_t.basis.get_column(0).x;
        inverse_transform[1] = inv_t.basis.get_column(0).y;
        inverse_transform[2] = inv_t.basis.get_column(0).z;
        inverse_transform[3] = 0.0f;

        inverse_transform[4] = inv_t.basis.get_column(1).x;
        inverse_transform[5] = inv_t.basis.get_column(1).y;
        inverse_transform[6] = inv_t.basis.get_column(1).z;
        inverse_transform[7] = 0.0f;

        inverse_transform[8] = inv_t.basis.get_column(2).x;
        inverse_transform[9] = inv_t.basis.get_column(2).y;
        inverse_transform[10] = inv_t.basis.get_column(2).z;
        inverse_transform[11] = 0.0f;

        inverse_transform[12] = inv_t.origin.x;
        inverse_transform[13] = inv_t.origin.y;
        inverse_transform[14] = inv_t.origin.z;
        inverse_transform[15] = 1.0f;
    }
};


class BVHBuilder
{
  public:
    unsigned int BuildBVH(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles, const Ref<ArrayMesh> &arrayMesh);
    void print_tree(const std::vector<BVHNode> &nodes);

  private:
    BoundingBox compute_bounding_box(const std::vector<Triangle> &triangles, int start, int end);
    unsigned int build_recursive(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles, int start, int end);
    
};

} // namespace BVH

#endif // BHV_H
