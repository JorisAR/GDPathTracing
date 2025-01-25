#ifndef GEOMETRY_GROUP3D_H
#define GEOMETRY_GROUP3D_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/surface_tool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>
#include <queue>

#include "render_parameters.h"
#include "bvh/bvh.h"

using namespace godot;
using namespace BVH;

class GeometryGroup3D : public Node3D
{
    GDCLASS(GeometryGroup3D, Node3D);

  protected:
    static void _bind_methods();
    void _notification(int p_what);

  private:
    struct NodeReference // temporary struct to help build the BLASinstances
    {
        MeshInstance3D *node;
        int mesh_id;
        int material_id; // ensure that an invalid material (null or not standard) points to id 0
        // std::vector<int> material_ids; // ensure that an invalid material (null or not standard) points to id 0
    };

    std::vector<Ref<Mesh>> initial_geometry_references;
    std::vector<Ref<ArrayMesh>> final_geometry_references;
    std::vector<NodeReference> node_references;

    std::vector<Ref<StandardMaterial3D>> material_references; // first collect all materials
    std::vector<GpuMaterial> materials;               // always include a default material at index 0

    //Actual buffers to send to the GPU
    std::vector<BVHNode> bvh_nodes;
    std::vector<Triangle> triangles;
    std::vector<BLASInstance> blasInstances;

    void collect_mesh_instances();

  public:
    void build();
    GeometryGroup3D();

    int get_blas_count();
    int get_material_count();
    int get_triangle_count();
    int get_bvh_node_count();

    PackedByteArray get_triangles_buffer();
    PackedByteArray get_materials_buffer();
    PackedByteArray get_bvh_buffer();
    PackedByteArray get_blas_buffer();
};

#endif // GEOMETRY_GROUP3D_H
