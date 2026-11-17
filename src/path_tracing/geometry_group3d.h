#ifndef GEOMETRY_GROUP3D_H
#define GEOMETRY_GROUP3D_H

#include "bvh/bvh.h"
#include "path_tracing/gpu_types.h"
#include "path_tracing/scene_data.h"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <vector>

using namespace godot;

// Collects every MeshInstance3D and light below this node and converts them into the data the
// path tracer needs. Geometry (BLAS, materials, textures) is built once in build(); instance
// transforms, visibility and lights are tracked every frame so objects can move.
class GeometryGroup3D : public Node3D
{
    GDCLASS(GeometryGroup3D, Node3D);

  protected:
    static void _bind_methods();
    void _notification(int p_what);

  public:
    GeometryGroup3D();

    // Rebuilds everything. Call after adding/removing meshes or changing materials at runtime.
    void build();
    // Re-reads transforms, visibility and lights; cheap when nothing moved.
    void update_dynamic(bool force = false);

    PT::SceneDataPtr get_scene_data() const
    {
        return scene_data;
    }

    Ref<BaseMaterial3D> get_default_material() const;
    void set_default_material(const Ref<BaseMaterial3D> &value);

    int get_texture_resolution() const;
    void set_texture_resolution(int value);

    bool get_track_transforms() const;
    void set_track_transforms(bool value);

    int get_triangle_count() const;
    int get_instance_count() const;
    int get_emissive_triangle_count() const;

  private:
    struct MeshEntry
    {
        Ref<Mesh> mesh;
        uint32_t bvh_root = 0;
        uint32_t triangle_offset = 0;
        uint32_t triangle_count = 0;
        uint32_t surface_count = 0;
        AABB local_bounds;
        std::vector<PT::GpuBVHNode> nodes;      // mesh-local indices, root at 0
        std::vector<PT::BuildTriangle> tris;    // in BVH order
        std::vector<PT::GpuTriangleData> data;  // in BVH order
        std::vector<uint32_t> surfaces;         // per triangle, in BVH order
    };

    struct InstanceEntry
    {
        ObjectID node;
        uint32_t mesh = 0;
        uint32_t material_offset = 0;
        bool emissive = false;
        bool visible = false;
        Transform3D transform;
        Transform3D prev_transform;
    };

    void collect(std::vector<Node *> &mesh_nodes, std::vector<Node *> &light_nodes);
    uint32_t get_material_index(const Ref<Material> &material);
    int get_texture_index(const Ref<Texture2D> &texture);
    int add_mesh(const Ref<Mesh> &mesh);
    PackedByteArray build_light_buffer(uint32_t &count);
    PackedByteArray build_portal_buffer(uint32_t &count);

    PT::SceneDataPtr scene_data;

    Ref<BaseMaterial3D> default_material;
    int texture_resolution = 512;
    bool track_transforms = true;

    std::vector<MeshEntry> meshes;
    std::vector<InstanceEntry> instance_entries;
    std::vector<ObjectID> light_nodes;
    std::vector<ObjectID> portal_nodes;

    std::vector<Ref<Material>> material_refs;
    std::vector<PT::GpuMaterial> materials;
    std::vector<uint32_t> instance_materials;
    std::vector<Ref<Texture2D>> texture_refs;

    uint32_t total_triangles = 0;
    uint32_t visible_instances = 0;
    uint32_t emissive_triangle_count = 0;
    PackedByteArray last_light_bytes;
    PackedByteArray last_portal_bytes;
    bool moved_last_update = false;
    bool built = false;
};

#endif // GEOMETRY_GROUP3D_H
