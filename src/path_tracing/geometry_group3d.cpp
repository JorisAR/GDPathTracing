#include "geometry_group3d.h"

GeometryGroup3D::GeometryGroup3D() : texture_array_resolution(512) // Initialize with default value
{
}

int GeometryGroup3D::get_blas_count()
{
    return blas_instances.size();
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

int GeometryGroup3D::get_tlas_node_count()
{
    return tlas_nodes.size();
}

template <typename T> PackedByteArray get_buffer(const std::vector<T> &vec)
{
    PackedByteArray byte_array;
    byte_array.resize(vec.size() * sizeof(T));
    std::memcpy(byte_array.ptrw(), vec.data(), vec.size() * sizeof(T));
    return byte_array;
}

PackedByteArray GeometryGroup3D::get_triangles_geometry_buffer()
{
    return get_buffer(triangles_geometry);
}

PackedByteArray GeometryGroup3D::get_triangles_data_buffer()
{
    return get_buffer(triangles_data);
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
    return get_buffer(blas_instances);
}

PackedByteArray GeometryGroup3D::get_tlas_buffer()
{
    return get_buffer(tlas_nodes);
}

std::vector<Ref<Image>> GeometryGroup3D::get_textures_buffer()
{
    return textures;
}

Ref<StandardMaterial3D> GeometryGroup3D::get_default_material() const
{
    return default_material;
}

void GeometryGroup3D::set_default_material(Ref<StandardMaterial3D> value)
{
    default_material = value;
}

int GeometryGroup3D::get_texture_array_resolution() const
{
    return texture_array_resolution;
}

void GeometryGroup3D::set_texture_array_resolution(int value)
{
    texture_array_resolution = value;
}

void GeometryGroup3D::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_default_material"), &GeometryGroup3D::get_default_material);
    ClassDB::bind_method(D_METHOD("set_default_material", "value"), &GeometryGroup3D::set_default_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_material", PROPERTY_HINT_RESOURCE_TYPE, "StandardMaterial3D"),
                 "set_default_material", "get_default_material");

    ClassDB::bind_method(D_METHOD("get_texture_array_resolution"), &GeometryGroup3D::get_texture_array_resolution);
    ClassDB::bind_method(D_METHOD("set_texture_array_resolution", "value"), &GeometryGroup3D::set_texture_array_resolution);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_array_resolution"), "set_texture_array_resolution", "get_texture_array_resolution");
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

unsigned int GeometryGroup3D::get_material_index(const Ref<Material> &material)
{
    // find it in initial references
    for (size_t i = 0; i < initial_material_references.size(); i++)
        if (initial_material_references[i].ptr() == material.ptr())
            return i;
        
    
    // try to convert and return its index, else return default material index.
    Ref<StandardMaterial3D> new_material = Ref<StandardMaterial3D>(Object::cast_to<StandardMaterial3D>(*material));
    if (new_material.is_null())
        return 0;
        
    initial_material_references.push_back(material);
    material_references.push_back(new_material);
    return material_references.size() - 1;
}

int GeometryGroup3D::get_texture_index(const Ref<Texture2D> &texture)
{
    if (texture.is_null())
        return -1;

    for (size_t i = 0; i < texture_references.size(); i++)
        if (texture_references[i].ptr() == texture.ptr())
            return i;

    texture_references.push_back(texture);
    return texture_references.size() - 1;
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
                    if (mesh_instance->get_material_override().is_valid())
                    {
                        unsigned int material_id = get_material_index(mesh_instance->get_material_override());
                        for (size_t i = 0; i < mesh_instance->get_surface_override_material_count(); i++)
                        {
                            material_ids.push_back(material_id);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < mesh_instance->get_surface_override_material_count(); i++)
                        {
                            material_ids.push_back(get_material_index(mesh_instance->get_surface_override_material(i)));
                        }
                    }

                    node_references.push_back({mesh_instance, mesh_id, material_ids});
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
    initial_material_references.clear();
    tlas_nodes.clear();
    bvh_nodes.clear();
    blas_instances.clear();
    // ensure existence of some default material
    if (default_material.is_null())
    {
        default_material.instantiate();
        default_material->set_albedo(Color(0.5f, 0.5f, 0.5f));
        default_material->set_roughness(0.5f);
        default_material->set_metallic(0.0f);
    }
    initial_material_references.push_back(default_material);
    material_references.push_back(default_material);

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
#ifdef VERBOSE_BVH_BUILDING
    UtilityFunctions::print("geometry, nodes, material references:");
    UtilityFunctions::print(final_geometry_references.size());
    UtilityFunctions::print(node_references.size());
    UtilityFunctions::print(material_references.size());
#endif

    { // materials
        materials.clear();
        // convert standard materials to gpu materials
        for (size_t i = 0; i < material_references.size(); i++)
        {
            Ref<StandardMaterial3D> material = material_references[i];
            if (material.is_valid())
            {
                GpuMaterial gpu_material;
                auto a = material->get_albedo();
                gpu_material.albedo = BVH::vec4(a.r, a.g, a.b, 1.0f);
                gpu_material.metallic = material->get_metallic();
                gpu_material.roughness = material->get_roughness();
                auto e = material->get_emission();

                gpu_material.emission = BVH::vec4(e.r, e.g, e.b, material->get_emission_energy_multiplier());

                gpu_material.albedo_texture_index =
                    get_texture_index(material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
                materials.push_back(gpu_material);
            }
        }
        // //combine textures into a single large texture
        for (size_t i = 0; i < texture_references.size(); i++){
            auto image = texture_references[i]->get_image();
            image->clear_mipmaps();
            image->decompress();
            image->resize(texture_array_resolution, texture_array_resolution);
            textures.push_back(image);
        }
        if(textures.size() <= 0){
            textures.push_back(Image::create(texture_array_resolution, texture_array_resolution, false, Image::FORMAT_RGBA8));
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
#ifdef VERBOSE_BVH_BUILDING
    UtilityFunctions::print("nodes, triangles, materials:");
    UtilityFunctions::print(bvh_nodes.size());
    UtilityFunctions::print(triangles.size());
    UtilityFunctions::print(materials.size());
#endif

    // create blas instance for each node:
    for (size_t i = 0; i < node_references.size(); i++)
    {
        if (node_references[i].node == nullptr)
        {
            continue;
        }
        BLASInstance blas_instance;
        blas_instance.blas_index = root_ids[node_references[i].mesh_id];
        blas_instance.set_materials(node_references[i].material_ids);
        blas_instance.set_transform(node_references[i].node->get_global_transform(), bvh_nodes);

        blas_instances.push_back(blas_instance);
#ifdef VERBOSE_BVH_BUILDING
        UtilityFunctions::print("root:");
        UtilityFunctions::print(blas_instance.blas_index);
        UtilityFunctions::print(blas_instance.material[0]);
        UtilityFunctions::print(blas_instance.material[1]);
        UtilityFunctions::print(blas_instance.material[2]);
#endif
    }
#ifdef VERBOSE_BVH_BUILDING
    UtilityFunctions::print("Blas:");
    UtilityFunctions::print(blas_instances.size());
#endif

    // builder.print_tree(bvh_nodes);

    // create tlas tree; https://jacco.ompf2.com/2022/05/13/how-to-build-a-bvh-part-6-all-together-now/
    TLAS tlas;
    tlas.build(tlas_nodes, blas_instances);
#ifdef VERBOSE_BVH_BUILDING
    tlas.print_tree(tlas_nodes);
#endif

    triangles_geometry.clear();
    triangles_data.clear();
    // once done building, populate the GPU triangle arrays
    for (size_t i = 0; i < triangles.size(); i++)
    {
        Triangle tri = triangles[i];
        triangles_geometry.push_back(GpuTriangleGeometry{tri.vertices[0], tri.vertices[1], tri.vertices[2]});
        triangles_data.push_back(GpuTriangleData{tri.normals[0], tri.materialIndex, tri.normals[1], tri.normals[2],
                                                 tri.uvs[0], tri.uvs[1], tri.uvs[2]});
    }
}
