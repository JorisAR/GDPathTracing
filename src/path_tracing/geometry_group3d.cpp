#include "geometry_group3d.h"

GeometryGroup3D::GeometryGroup3D()
{
}

int GeometryGroup3D::get_blas_count()
{
    return blasInstances.size();
}

int GeometryGroup3D::get_material_count()
{
    return materials.size();
}

int GeometryGroup3D::get_triangle_count()
{
    return triangles.size();
}

int GeometryGroup3D::get_bvh_node_count()
{
    return bvh_nodes.size();
}

template <typename T> PackedByteArray get_buffer(const std::vector<T> &vec)
{
    PackedByteArray byte_array;
    byte_array.resize(vec.size() * sizeof(T));
    std::memcpy(byte_array.ptrw(), vec.data(), vec.size() * sizeof(T));
    return byte_array;
}

PackedByteArray GeometryGroup3D::get_triangles_buffer()
{
    return get_buffer(triangles);
}

PackedByteArray GeometryGroup3D::get_materials_buffer()
{
    return get_buffer(materials);
}

PackedByteArray GeometryGroup3D::get_bvh_buffer()
{
    return get_buffer(bvh_nodes);
}

PackedByteArray GeometryGroup3D::get_blas_buffer()
{
    return get_buffer(blasInstances);
}

void GeometryGroup3D::_bind_methods()
{
    // ClassDB::bind_method(D_METHOD("build"), &GeometryGroup3D::build);
}

void GeometryGroup3D::_notification(int p_what)
{
    if (godot::Engine::get_singleton()->is_editor_hint())
    {
        return;
    }
    if (p_what == NOTIFICATION_READY)
    {
        // build();
    }
}

void GeometryGroup3D::collect_mesh_instances()
{
    std::queue<Node *> node_queue;
    node_queue.push(this);

    while (!node_queue.empty())
    {
        Node *current_node = node_queue.front();
        node_queue.pop();

        for (int i = 0; i < current_node->get_child_count(); i++)
        {
            Node *child = current_node->get_child(i);

            if (auto mesh_instance = Object::cast_to<MeshInstance3D>(child))
            {
                Ref<Mesh> mesh = mesh_instance->get_mesh();
                if (mesh.is_valid())
                {
                    // Valid mesh, first check if it exists already, else add it
                    bool found = false;
                    int mesh_id = -1;
                    for (size_t j = 0; j < initial_geometry_references.size(); ++j)
                    {
                        if (initial_geometry_references[j].ptr() == mesh.ptr())
                        {
                            found = true;
                            mesh_id = static_cast<int>(j);
                            break;
                        }
                    }
                    if (!found)
                    {
                        mesh_id = static_cast<int>(initial_geometry_references.size());
                        initial_geometry_references.push_back(mesh);
                    }
                    // material
                    std::vector<int> material_ids;
                    mesh_instance->get_material_override();
                    for (size_t i = 0; i < mesh_instance->get_surface_override_material_count(); i++)
                    {
                        // todo add indidivual materials here
                    }
                    // for now, just the single override
                    Ref<StandardMaterial3D> material =
                        mesh.is_valid() ? Ref<StandardMaterial3D>(Object::cast_to<StandardMaterial3D>(
                                              *(mesh_instance->get_material_override())))
                                        : Ref<StandardMaterial3D>();
                    if (material.is_null())
                    {
                        UtilityFunctions::print("material null");
                        material_ids.push_back(0);
                    }
                    else
                    {
                        UtilityFunctions::print("material not null");
                        material_references.push_back(material);
                        material_ids.push_back(material_references.size());
                    }

                    node_references.push_back({mesh_instance, mesh_id, material_ids[0]});
                }
            }

            if (Object::cast_to<GeometryGroup3D>(child) == nullptr)
            {
                node_queue.push(child);
            }
        }
    }
}

Ref<ArrayMesh> mesh_to_array_mesh(const Ref<Mesh> mesh)
{
    int surface_count = mesh->get_surface_count();
    auto st = memnew(SurfaceTool);
    for (int i = 0; i < surface_count; i++)
    {
        st->create_from(mesh, i);
    }
    auto result = st->commit();
    return result;
}

void GeometryGroup3D::build()
{
    initial_geometry_references.clear();
    final_geometry_references.clear();
    node_references.clear();
    material_references.clear();

    collect_mesh_instances();

    for (const auto &mesh : initial_geometry_references)
    {
        Ref<ArrayMesh> arr_mesh =
            mesh.is_valid() ? Ref<ArrayMesh>(Object::cast_to<ArrayMesh>(*(mesh))) : Ref<ArrayMesh>();
        if (arr_mesh.is_null())
        {
            arr_mesh = mesh_to_array_mesh(mesh);
        }
        if (arr_mesh.is_valid())
        {
            final_geometry_references.push_back(arr_mesh);
        }
    }
    UtilityFunctions::print("geometry, nodes, material references:");
    UtilityFunctions::print(final_geometry_references.size());
    UtilityFunctions::print(node_references.size());
    UtilityFunctions::print(material_references.size());

    { // materials
        materials.clear();
        // default material
        // only if the user does not set a default material for this group
        materials.push_back({BVH::vec4(0.5f, 0.5f, 0.5f, 1.0f), 0.0f, 0.5f});

        // convert standard materials to gpu materials
        for (size_t i = 0; i < material_references.size(); i++)
        {
            Ref<StandardMaterial3D> material = material_references[i];
            if (material.is_valid())
            {
                GpuMaterial gpu_material;
                gpu_material.albedo =
                    BVH::vec4(material->get_albedo().r, material->get_albedo().g, material->get_albedo().b, 1.0f);
                gpu_material.metallic = material->get_metallic();
                gpu_material.roughness = material->get_roughness();
                materials.push_back(gpu_material);
            }
        }
    }
    // build the bvh for each unique mesh
    std::vector<unsigned int> root_ids;

    BVHBuilder builder;
    for (size_t i = 0; i < final_geometry_references.size(); i++)
    {
        unsigned int root = builder.BuildBVH(bvh_nodes, triangles, final_geometry_references[i]);
        root_ids.push_back(root);
    }
    UtilityFunctions::print("nodes, triangles, materials:");
    UtilityFunctions::print(bvh_nodes.size());
    UtilityFunctions::print(triangles.size());
    UtilityFunctions::print(materials.size());

    // create blas instance for each node:
    for (size_t i = 0; i < node_references.size(); i++)
    {
        if (node_references[i].node == nullptr)
        {
            continue;
        }
        BLASInstance blas_instance;
        blas_instance.blas_index = root_ids[node_references[i].mesh_id];
        blas_instance.material_index = node_references[i].material_id;
        blas_instance.set_transform(node_references[i].node->get_global_transform());

        blasInstances.push_back(blas_instance);

        UtilityFunctions::print("root:");
        UtilityFunctions::print(blas_instance.blas_index);
    }
    UtilityFunctions::print("Blas:");
    UtilityFunctions::print(blasInstances.size());

    // builder.print_tree(bvh_nodes);

    // Now you can use the collected geometry references to create one large geometry buffer.
}
