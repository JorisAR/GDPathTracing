#include "geometry_group3d.h"

#include "path_tracing/portal3d.h"
#include "rd/rd_util.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <deque>

using namespace PT;

namespace
{

void write_rows(float *dst, const Transform3D &t)
{
    for (int r = 0; r < 3; r++)
    {
        dst[r * 4 + 0] = t.basis.rows[r].x;
        dst[r * 4 + 1] = t.basis.rows[r].y;
        dst[r * 4 + 2] = t.basis.rows[r].z;
        dst[r * 4 + 3] = t.origin[r];
    }
}

float luminance(const Color &c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

void set3(float *dst, const Vector3 &v)
{
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
}

} // namespace

GeometryGroup3D::GeometryGroup3D()
{
    scene_data = std::make_shared<SceneData>();
}

// ----------------------------------- PROPERTIES -----------------------------------

void GeometryGroup3D::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("build"), &GeometryGroup3D::build);
    ClassDB::bind_method(D_METHOD("update_dynamic", "force"), &GeometryGroup3D::update_dynamic, DEFVAL(false));

    ClassDB::bind_method(D_METHOD("get_default_material"), &GeometryGroup3D::get_default_material);
    ClassDB::bind_method(D_METHOD("set_default_material", "value"), &GeometryGroup3D::set_default_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D"),
                 "set_default_material", "get_default_material");

    ClassDB::bind_method(D_METHOD("get_texture_resolution"), &GeometryGroup3D::get_texture_resolution);
    ClassDB::bind_method(D_METHOD("set_texture_resolution", "value"), &GeometryGroup3D::set_texture_resolution);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_resolution", PROPERTY_HINT_RANGE, "16,4096,1"),
                 "set_texture_resolution", "get_texture_resolution");

    ClassDB::bind_method(D_METHOD("get_track_transforms"), &GeometryGroup3D::get_track_transforms);
    ClassDB::bind_method(D_METHOD("set_track_transforms", "value"), &GeometryGroup3D::set_track_transforms);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "track_transforms"), "set_track_transforms", "get_track_transforms");

    ClassDB::bind_method(D_METHOD("get_triangle_count"), &GeometryGroup3D::get_triangle_count);
    ClassDB::bind_method(D_METHOD("get_instance_count"), &GeometryGroup3D::get_instance_count);
    ClassDB::bind_method(D_METHOD("get_emissive_triangle_count"), &GeometryGroup3D::get_emissive_triangle_count);
}

Ref<BaseMaterial3D> GeometryGroup3D::get_default_material() const
{
    return default_material;
}
void GeometryGroup3D::set_default_material(const Ref<BaseMaterial3D> &value)
{
    default_material = value;
}
int GeometryGroup3D::get_texture_resolution() const
{
    return texture_resolution;
}
void GeometryGroup3D::set_texture_resolution(int value)
{
    texture_resolution = CLAMP(value, 16, 4096);
}
bool GeometryGroup3D::get_track_transforms() const
{
    return track_transforms;
}
void GeometryGroup3D::set_track_transforms(bool value)
{
    track_transforms = value;
}
int GeometryGroup3D::get_triangle_count() const
{
    return static_cast<int>(total_triangles);
}
int GeometryGroup3D::get_instance_count() const
{
    return static_cast<int>(visible_instances);
}
int GeometryGroup3D::get_emissive_triangle_count() const
{
    return static_cast<int>(emissive_triangle_count);
}

void GeometryGroup3D::_notification(int p_what)
{
    if (Engine::get_singleton()->is_editor_hint())
    {
        // In the editor the group only does work while a PathTracingCamera previews it.
        if (p_what == NOTIFICATION_INTERNAL_PROCESS && built && editor_preview_users > 0)
            update_dynamic(false);
        return;
    }

    switch (p_what)
    {
    case NOTIFICATION_READY:
        // Run after gameplay scripts so this frame's movement is picked up.
        set_process_priority(1000);
        set_process_internal(true);
        build();
        break;
    case NOTIFICATION_INTERNAL_PROCESS:
        if (built && track_transforms)
            update_dynamic(false);
        break;
    }
}

// ----------------------------------- COLLECTION -----------------------------------

void GeometryGroup3D::collect(std::vector<Node *> &mesh_nodes, std::vector<Node *> &light_nodes_out)
{
    std::deque<Node *> queue;
    queue.push_back(this);
    while (!queue.empty())
    {
        Node *current = queue.front();
        queue.pop_front();
        for (int i = 0; i < current->get_child_count(); i++)
        {
            Node *child = current->get_child(i);
            if (Object::cast_to<GeometryGroup3D>(child) != nullptr)
                continue; // nested groups manage their own subtree
            if (Object::cast_to<MeshInstance3D>(child) != nullptr)
                mesh_nodes.push_back(child);
            else if (Object::cast_to<Portal3D>(child) != nullptr)
                portal_nodes.push_back(ObjectID(child->get_instance_id()));
            else if (Object::cast_to<DirectionalLight3D>(child) != nullptr ||
                     Object::cast_to<OmniLight3D>(child) != nullptr || Object::cast_to<SpotLight3D>(child) != nullptr)
                light_nodes_out.push_back(child);
            queue.push_back(child);
        }
    }
}

void GeometryGroup3D::begin_editor_preview()
{
    if (editor_preview_users++ == 0)
    {
        set_process_priority(1000);
        set_process_internal(true);
    }
    if (!built)
        build();
}

void GeometryGroup3D::end_editor_preview()
{
    editor_preview_users = MAX(editor_preview_users - 1, 0);
    if (editor_preview_users == 0)
        set_process_internal(false);
}

int GeometryGroup3D::get_texture_index(const Ref<Texture2D> &texture)
{
    if (texture.is_null())
        return -1;
    for (size_t i = 0; i < texture_refs.size(); i++)
        if (texture_refs[i] == texture)
            return static_cast<int>(i);
    texture_refs.push_back(texture);
    return static_cast<int>(texture_refs.size() - 1);
}

uint32_t GeometryGroup3D::get_material_index(const Ref<Material> &material)
{
    Ref<BaseMaterial3D> base = material;
    if (base.is_null())
        return 0; // ShaderMaterial and friends fall back to the default material

    for (size_t i = 0; i < material_refs.size(); i++)
        if (material_refs[i] == material)
            return static_cast<uint32_t>(i);

    GpuMaterial m{};
    Color albedo = base->get_albedo().srgb_to_linear();
    m.albedo[0] = albedo.r;
    m.albedo[1] = albedo.g;
    m.albedo[2] = albedo.b;
    m.albedo[3] = 1.0f;
    if (base->get_feature(BaseMaterial3D::FEATURE_EMISSION))
    {
        Color e = base->get_emission().srgb_to_linear() * base->get_emission_energy_multiplier();
        m.emission[0] = e.r;
        m.emission[1] = e.g;
        m.emission[2] = e.b;
        m.emission[3] = luminance(e);
    }
    m.metallic = base->get_metallic();
    m.roughness = base->get_roughness();
    m.albedo_texture = get_texture_index(base->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
    m.flags = 0;
    Vector3 uv_scale = base->get_uv1_scale();
    Vector3 uv_offset = base->get_uv1_offset();
    m.uv_scale_offset[0] = uv_scale.x;
    m.uv_scale_offset[1] = uv_scale.y;
    m.uv_scale_offset[2] = uv_offset.x;
    m.uv_scale_offset[3] = uv_offset.y;

    material_refs.push_back(material);
    materials.push_back(m);
    return static_cast<uint32_t>(materials.size() - 1);
}

int GeometryGroup3D::add_mesh(const Ref<Mesh> &mesh)
{
    for (size_t i = 0; i < meshes.size(); i++)
        if (meshes[i].mesh == mesh)
            return static_cast<int>(i);

    MeshEntry entry;
    entry.mesh = mesh;
    entry.surface_count = static_cast<uint32_t>(mesh->get_surface_count());

    std::vector<BuildTriangle> tris;
    std::vector<GpuTriangleData> data;
    Ref<ArrayMesh> array_mesh = mesh;

    for (int s = 0; s < mesh->get_surface_count(); s++)
    {
        if (array_mesh.is_valid() && array_mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES)
            continue;
        Array arrays = mesh->surface_get_arrays(s);
        if (arrays.size() <= Mesh::ARRAY_INDEX)
            continue;
        PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
        PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
        PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
        PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
        const bool indexed = indices.size() > 0;
        const int64_t count = indexed ? indices.size() : vertices.size();
        const bool has_normals = normals.size() == vertices.size();
        const bool has_uvs = uvs.size() == vertices.size();

        for (int64_t i = 0; i + 2 < count; i += 3)
        {
            int idx[3];
            for (int j = 0; j < 3; j++)
                idx[j] = indexed ? indices[i + j] : static_cast<int>(i + j);

            BuildTriangle t{vertices[idx[0]], vertices[idx[1]], vertices[idx[2]]};
            Vector3 face = (t.v1 - t.v0).cross(t.v2 - t.v0);
            if (face.length_squared() <= 0.0f)
                continue; // degenerate
            face = face.normalized();

            GpuTriangleData d{};
            Vector3 n[3];
            Vector2 uv[3];
            for (int j = 0; j < 3; j++)
            {
                n[j] = has_normals ? normals[idx[j]] : face;
                uv[j] = has_uvs ? uvs[idx[j]] : Vector2();
            }
            set3(d.n0, n[0]);
            set3(d.n1, n[1]);
            set3(d.n2, n[2]);
            d.uv0[0] = uv[0].x;
            d.uv0[1] = uv[0].y;
            d.uv1[0] = uv[1].x;
            d.uv1[1] = uv[1].y;
            d.uv2[0] = uv[2].x;
            d.uv2[1] = uv[2].y;
            d.surface = static_cast<uint32_t>(s);
            tris.push_back(t);
            data.push_back(d);
        }
    }

    if (tris.empty())
        return -1;

    // Build the BVH in mesh-local index space; build() rebases the indices when flattening.
    std::vector<uint32_t> order;
    BLASBuilder::build(tris, 0, entry.nodes, order);

    entry.triangle_count = static_cast<uint32_t>(tris.size());
    entry.tris.resize(tris.size());
    entry.surfaces.resize(tris.size());
    entry.data.resize(tris.size());
    for (size_t i = 0; i < order.size(); i++)
    {
        entry.tris[i] = tris[order[i]];
        entry.data[i] = data[order[i]];
        entry.surfaces[i] = data[order[i]].surface;
    }
    const GpuBVHNode &root = entry.nodes[0];
    Vector3 bmin(root.aabb_min[0], root.aabb_min[1], root.aabb_min[2]);
    Vector3 bmax(root.aabb_max[0], root.aabb_max[1], root.aabb_max[2]);
    entry.local_bounds = AABB(bmin, bmax - bmin);

    meshes.push_back(std::move(entry));
    return static_cast<int>(meshes.size() - 1);
}

// ----------------------------------- BUILD -----------------------------------

void GeometryGroup3D::build()
{
    meshes.clear();
    instance_entries.clear();
    light_nodes.clear();
    portal_nodes.clear();
    material_refs.clear();
    materials.clear();
    instance_materials.clear();
    texture_refs.clear();
    last_light_bytes = PackedByteArray();
    last_portal_bytes = PackedByteArray();

    // Material 0 is the default material.
    Ref<BaseMaterial3D> def = default_material;
    if (def.is_null())
    {
        Ref<StandardMaterial3D> sm;
        sm.instantiate();
        sm->set_albedo(Color(0.8f, 0.8f, 0.8f));
        sm->set_roughness(0.5f);
        def = sm;
    }
    get_material_index(def);

    std::vector<Node *> mesh_nodes, light_list;
    collect(mesh_nodes, light_list);
    for (Node *n : light_list)
        light_nodes.push_back(ObjectID(n->get_instance_id()));

    for (Node *n : mesh_nodes)
    {
        MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
        Ref<Mesh> mesh = mi->get_mesh();
        if (mesh.is_null())
            continue;
        int mesh_index = add_mesh(mesh);
        if (mesh_index < 0)
            continue;

        InstanceEntry inst;
        inst.node = ObjectID(mi->get_instance_id());
        inst.mesh = static_cast<uint32_t>(mesh_index);
        inst.material_offset = static_cast<uint32_t>(instance_materials.size());
        for (int s = 0; s < mesh->get_surface_count(); s++)
        {
            uint32_t m = get_material_index(mi->get_active_material(s));
            instance_materials.push_back(m);
            if (materials[m].emission[3] > 0.0f)
                inst.emissive = true;
        }
        inst.visible = false;
        instance_entries.push_back(inst);
    }

    // Flatten all meshes into shared buffers, rebasing node and triangle indices.
    std::vector<GpuBVHNode> bvh_nodes;
    std::vector<GpuTriangle> gpu_tris;
    std::vector<GpuTriangleData> gpu_data;
    for (MeshEntry &m : meshes)
    {
        const uint32_t node_offset = static_cast<uint32_t>(bvh_nodes.size());
        m.triangle_offset = static_cast<uint32_t>(gpu_tris.size());
        m.bvh_root = node_offset;
        for (GpuBVHNode n : m.nodes)
        {
            n.left_first += n.tri_count > 0 ? m.triangle_offset : node_offset;
            bvh_nodes.push_back(n);
        }
        for (size_t i = 0; i < m.tris.size(); i++)
        {
            const BuildTriangle &t = m.tris[i];
            GpuTriangle g{};
            set3(g.v0, t.v0);
            set3(g.e1, t.v1 - t.v0);
            set3(g.e2, t.v2 - t.v0);
            gpu_tris.push_back(g);
            gpu_data.push_back(m.data[i]);
        }
    }

    total_triangles = static_cast<uint32_t>(gpu_tris.size());

    // Triangle soup per mesh for hardware BLASes (same order as the triangle buffers).
    std::vector<PackedByteArray> blas_vertices;
    for (const MeshEntry &m : meshes)
    {
        std::vector<float> v;
        v.reserve(m.tris.size() * 9);
        for (const BuildTriangle &t : m.tris)
        {
            const Vector3 corners[3] = {t.v0, t.v1, t.v2};
            for (const Vector3 &c : corners)
            {
                v.push_back(c.x);
                v.push_back(c.y);
                v.push_back(c.z);
            }
        }
        blas_vertices.push_back(vector_to_bytes(v));
    }

    // Textures: decompress, convert to RGBA8 and resize to a common resolution for the array.
    std::vector<Ref<Image>> images;
    for (const Ref<Texture2D> &tex : texture_refs)
    {
        Ref<Image> img = tex->get_image();
        if (img.is_valid())
            img = img->duplicate();
        if (img.is_null() || img->is_empty())
        {
            img = Image::create_empty(texture_resolution, texture_resolution, false, Image::FORMAT_RGBA8);
            img->fill(Color(1, 1, 1, 1));
        }
        if (img->is_compressed())
            img->decompress();
        img->clear_mipmaps();
        img->convert(Image::FORMAT_RGBA8);
        img->resize(texture_resolution, texture_resolution, Image::INTERPOLATE_BILINEAR);
        images.push_back(img);
    }
    if (images.empty())
    {
        Ref<Image> img = Image::create_empty(4, 4, false, Image::FORMAT_RGBA8);
        img->fill(Color(1, 1, 1, 1));
        images.push_back(img);
    }
    const int tex_res = images[0]->get_width();

    PackedByteArray bvh_bytes = vector_to_bytes(bvh_nodes);
    PackedByteArray tri_bytes = vector_to_bytes(gpu_tris);
    PackedByteArray data_bytes = vector_to_bytes(gpu_data);
    PackedByteArray mat_bytes = vector_to_bytes(materials);
    PackedByteArray inst_mat_bytes = vector_to_bytes(instance_materials);

    scene_data->modify([&](SceneSnapshot &s) {
        s.geometry_version++;
        s.bvh_nodes = bvh_bytes;
        s.triangles = tri_bytes;
        s.triangle_data = data_bytes;
        s.materials = mat_bytes;
        s.instance_materials = inst_mat_bytes;
        s.textures = images;
        s.texture_resolution = tex_res;
        s.blas_vertices = blas_vertices;
    });

    built = true;
    update_dynamic(true);

    UtilityFunctions::print("[PathTracing] built ", (int64_t)meshes.size(), " meshes, ", (int64_t)total_triangles,
                            " triangles, ", (int64_t)instance_entries.size(), " instances, ",
                            (int64_t)materials.size(), " materials, ", (int64_t)texture_refs.size(), " textures, ",
                            (int64_t)light_nodes.size(), " lights, ", (int64_t)portal_nodes.size(), " portals");
}

// ----------------------------------- PER FRAME -----------------------------------

PackedByteArray GeometryGroup3D::build_light_buffer(uint32_t &count)
{
    std::vector<GpuPunctualLight> lights;
    for (const ObjectID &id : light_nodes)
    {
        Light3D *light = Object::cast_to<Light3D>(ObjectDB::get_instance(id));
        if (light == nullptr || !light->is_visible_in_tree())
            continue;
        float energy = light->get_param(Light3D::PARAM_ENERGY);
        if (energy <= 0.0f)
            continue;

        GpuPunctualLight l{};
        Transform3D xf = light->get_global_transform();
        Vector3 forward = -xf.basis.get_column(2).normalized();
        // Godot pre-multiplies light energy by PI so that energy 1 lights a white diffuse surface to ~1.
        Color c = light->get_color().srgb_to_linear() * (energy * static_cast<float>(Math::PI));
        set3(l.color, Vector3(c.r, c.g, c.b));
        set3(l.position, xf.origin);
        set3(l.direction, forward);

        if (Object::cast_to<DirectionalLight3D>(light) != nullptr)
        {
            l.type = LIGHT_DIRECTIONAL;
            float angular = Math::deg_to_rad(light->get_param(Light3D::PARAM_SIZE));
            l.radius = std::tan(angular * 0.5f);
            l.range = 0.0f;
        }
        else if (OmniLight3D *omni = Object::cast_to<OmniLight3D>(light))
        {
            l.type = LIGHT_OMNI;
            l.radius = omni->get_param(Light3D::PARAM_SIZE);
            l.range = omni->get_param(Light3D::PARAM_RANGE);
            l.attenuation = omni->get_param(Light3D::PARAM_ATTENUATION);
        }
        else if (SpotLight3D *spot = Object::cast_to<SpotLight3D>(light))
        {
            l.type = LIGHT_SPOT;
            l.radius = spot->get_param(Light3D::PARAM_SIZE);
            l.range = spot->get_param(Light3D::PARAM_RANGE);
            l.attenuation = spot->get_param(Light3D::PARAM_ATTENUATION);
            float angle = Math::deg_to_rad(spot->get_param(Light3D::PARAM_SPOT_ANGLE));
            l.spot_cos_outer = std::cos(angle);
            l.spot_cos_inner = std::cos(angle * 0.9f);
        }
        lights.push_back(l);
    }
    count = static_cast<uint32_t>(lights.size());
    return vector_to_bytes(lights);
}

PackedByteArray GeometryGroup3D::build_portal_buffer(uint32_t &count)
{
    std::vector<GpuPortal> out;
    for (const ObjectID &id : portal_nodes)
    {
        Portal3D *portal = Object::cast_to<Portal3D>(ObjectDB::get_instance(id));
        if (portal == nullptr || !portal->is_visible_in_tree())
            continue;
        GpuPortal g{};
        write_rows(g.world_to_local, portal->get_global_transform().affine_inverse());
        Portal3D *other = portal->get_linked_portal();
        g.linked = other != nullptr && other->is_inside_tree() ? 1u : 0u;
        write_rows(g.teleport, portal->get_teleport_transform());
        Color tint = portal->get_tint().srgb_to_linear();
        g.tint[0] = tint.r;
        g.tint[1] = tint.g;
        g.tint[2] = tint.b;
        g.tint[3] = 1.0f;
        g.half_size[0] = portal->get_size().x * 0.5f;
        g.half_size[1] = portal->get_size().y * 0.5f;
        out.push_back(g);
    }
    count = static_cast<uint32_t>(out.size());
    return vector_to_bytes(out);
}

void GeometryGroup3D::update_dynamic(bool force)
{
    if (!built)
        return;

    bool moved = false;
    for (InstanceEntry &inst : instance_entries)
    {
        MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(inst.node));
        bool visible = mi != nullptr && mi->is_visible_in_tree();
        Transform3D xf = visible ? mi->get_global_transform() : inst.transform;
        inst.prev_transform = inst.transform; // last frame's transform, for motion vectors
        if (visible != inst.visible || (visible && xf != inst.transform))
        {
            moved = true;
            inst.visible = visible;
            inst.transform = xf;
        }
    }
    // After motion stops, upload once more so the previous transforms catch up (zero motion).
    bool instances_changed = force || moved || moved_last_update;
    moved_last_update = moved;

    uint32_t light_count = 0;
    PackedByteArray light_bytes = build_light_buffer(light_count);
    bool lights_changed = force || light_bytes != last_light_bytes;

    uint32_t portal_count = 0;
    PackedByteArray portal_bytes = build_portal_buffer(portal_count);
    bool portals_changed = force || portal_bytes != last_portal_bytes;

    if (!instances_changed && !lights_changed && !portals_changed)
        return;

    PackedByteArray instance_bytes, tlas_bytes, emissive_bytes;
    uint32_t tlas_instance_count = 0;
    uint32_t emissive_count = 0;
    float total_power = 0.0f;
    bool update_instances = instances_changed;

    std::vector<HwInstance> hw_instances;
    if (update_instances)
    {
        std::vector<GpuInstance> gpu_instances(instance_entries.size());
        std::vector<AABB> bounds;
        std::vector<uint32_t> ids;
        std::vector<GpuEmissiveTriangle> emissive;

        for (size_t i = 0; i < instance_entries.size(); i++)
        {
            const InstanceEntry &inst = instance_entries[i];
            const MeshEntry &mesh = meshes[inst.mesh];
            GpuInstance &g = gpu_instances[i];
            write_rows(g.object_to_world, inst.transform);
            write_rows(g.world_to_object, inst.transform.affine_inverse());
            write_rows(g.prev_object_to_world, inst.prev_transform);
            g.bvh_root = mesh.bvh_root;
            g.material_offset = inst.material_offset;
            g.triangle_offset = mesh.triangle_offset;
            g.flags = inst.emissive ? INSTANCE_FLAG_EMISSIVE : 0u;

            if (!inst.visible)
                continue;
            bounds.push_back(inst.transform.xform(mesh.local_bounds));
            ids.push_back(static_cast<uint32_t>(i));
            hw_instances.push_back({inst.transform, inst.mesh, static_cast<uint32_t>(i)});

            if (inst.emissive)
            {
                for (size_t t = 0; t < mesh.tris.size(); t++)
                {
                    const GpuMaterial &m = materials[instance_materials[inst.material_offset + mesh.surfaces[t]]];
                    if (m.emission[3] <= 0.0f)
                        continue;
                    const BuildTriangle &tri = mesh.tris[t];
                    Vector3 e1 = inst.transform.basis.xform(tri.v1 - tri.v0);
                    Vector3 e2 = inst.transform.basis.xform(tri.v2 - tri.v0);
                    float area = 0.5f * e1.cross(e2).length();
                    float power = m.emission[3] * area;
                    if (power <= 0.0f)
                        continue;
                    total_power += power;
                    emissive.push_back({static_cast<uint32_t>(i), mesh.triangle_offset + static_cast<uint32_t>(t),
                                        total_power, power});
                }
            }
        }
        for (GpuEmissiveTriangle &e : emissive)
            e.cdf /= total_power;
        if (!emissive.empty())
            emissive.back().cdf = 1.0f;

        std::vector<GpuTLASNode> tlas;
        TLASBuilder::build(bounds, ids, tlas);

        tlas_instance_count = static_cast<uint32_t>(ids.size());
        emissive_count = static_cast<uint32_t>(emissive.size());
        instance_bytes = vector_to_bytes(gpu_instances);
        tlas_bytes = vector_to_bytes(tlas);
        emissive_bytes = vector_to_bytes(emissive);
        visible_instances = tlas_instance_count;
        emissive_triangle_count = emissive_count;
    }

    last_light_bytes = light_bytes;
    last_portal_bytes = portal_bytes;
    scene_data->modify([&](SceneSnapshot &s) {
        s.instance_version++;
        s.portals = portal_bytes;
        s.portal_count = portal_count;
        if (update_instances)
        {
            s.hw_instances = hw_instances;
            s.instances = instance_bytes;
            s.tlas_nodes = tlas_bytes;
            s.emissive_triangles = emissive_bytes;
            s.instance_count = tlas_instance_count;
            s.emissive_count = emissive_count;
            s.emissive_total_power = total_power;
        }
        s.punctual_lights = light_bytes;
        s.punctual_count = light_count;
    });
}
