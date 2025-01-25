#include "path_tracing_camera.h"

void PathTracingCamera::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_fov"), &PathTracingCamera::get_fov);
    ClassDB::bind_method(D_METHOD("set_fov", "value"), &PathTracingCamera::set_fov);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fov"), "set_fov", "get_fov");

    ClassDB::bind_method(D_METHOD("get_num_bounces"), &PathTracingCamera::get_num_bounces);
    ClassDB::bind_method(D_METHOD("set_num_bounces", "value"), &PathTracingCamera::set_num_bounces);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "num_bounces"), "set_num_bounces", "get_num_bounces");

    ClassDB::bind_method(D_METHOD("get_output_texture"), &PathTracingCamera::get_output_texture);
    ClassDB::bind_method(D_METHOD("set_output_texture", "value"), &PathTracingCamera::set_output_texture);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "output_texture", PROPERTY_HINT_NODE_TYPE, "TextureRect"),
                 "set_output_texture", "get_output_texture");

    ClassDB::bind_method(D_METHOD("get_geometry_group"), &PathTracingCamera::get_geometry_group);
    ClassDB::bind_method(D_METHOD("set_geometry_group", "value"), &PathTracingCamera::set_geometry_group);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "geometry_group", PROPERTY_HINT_NODE_TYPE, "GeometryGroup3D"),
                 "set_geometry_group", "get_geometry_group");
}

void PathTracingCamera::_notification(int p_what)
{
    if (godot::Engine::get_singleton()->is_editor_hint())
    {
        return;
    }
    switch (p_what)
    {
    case NOTIFICATION_ENTER_TREE: {
        set_process_internal(true);

        break;
    }
    case NOTIFICATION_EXIT_TREE: {
        set_process_internal(false);

        break;
    }
    case NOTIFICATION_READY: {
        init();
    }
    case NOTIFICATION_INTERNAL_PROCESS: {
        render();
        break;
    }
    }
}

float PathTracingCamera::get_fov() const
{
    return fov;
}

void PathTracingCamera::set_fov(float value)
{
    fov = value;
}

int PathTracingCamera::get_num_bounces() const
{
    return num_bounces;
}

void PathTracingCamera::set_num_bounces(int value)
{
    num_bounces = value;
}

TextureRect *PathTracingCamera::get_output_texture() const
{
    return output_texture_rect;
}

void PathTracingCamera::set_output_texture(TextureRect *value)
{
    output_texture_rect = value;
}

GeometryGroup3D *PathTracingCamera::get_geometry_group() const
{
    return geometry_group;
}

void PathTracingCamera::set_geometry_group(GeometryGroup3D *value)
{
    geometry_group = value;
}

RenderParameters PathTracingCamera::GetRenderParameters()
{
    auto basis = get_global_transform().get_basis();
    return RenderParameters(1920, 1080, fov, geometry_group->get_triangle_count(), geometry_group->get_blas_count(), get_global_position(),
                            basis.get_column(2), basis.get_column(0), basis.get_column(1));
}

void PathTracingCamera::init()
{
    // setup geometry
    if (geometry_group == nullptr)
    {
        UtilityFunctions::printerr("No geometry group set.");
        return;
    }

    geometry_group->build();

    // setup compute shader
    cs = new ComputeShader("res://addons/jar_path_tracing/src/shaders/main.glsl");
    //--------- GENERAL BUFFERS ---------
    { // input general buffer
        render_parameters_rid =
            cs->create_storage_buffer_uniform(GetRenderParameters().to_packed_byte_array(), 0, 0); // cs-> add render params buffer
    }

    { // output texture
        auto output_format = cs->create_texture_format(1920, 1080, RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
        if (output_texture_rect == nullptr)
        {
            UtilityFunctions::printerr("No output texture set.");
            return;
        }
        output_image = Image::create(1920, 1080, false, Image::FORMAT_RGBA8);
        output_texture = ImageTexture::create_from_image(output_image);
        output_texture_rect->set_texture(output_texture);
        output_texture_rid = cs->create_image_uniform(output_image, output_format, 1, 0);
    }

    //--------- SCENE STORAGE ---------
    {
        triangles_rid = cs->create_storage_buffer_uniform(geometry_group->get_triangles_buffer(), 0, 1);
        materials_rid = cs->create_storage_buffer_uniform(geometry_group->get_materials_buffer(), 1, 1);
        bvh_tree_rid = cs->create_storage_buffer_uniform(geometry_group->get_bvh_buffer(), 2, 1);
        blas_rid = cs->create_storage_buffer_uniform(geometry_group->get_blas_buffer(), 3, 1);
    }

    cs->finish_create_uniforms();
}

void PathTracingCamera::clear_compute_shader()
{
}

void PathTracingCamera::render()
{
    if (cs == nullptr || !cs->check_ready())
        return;
    //update rendering parameters
    cs->update_storage_buffer_uniform(render_parameters_rid, GetRenderParameters().to_packed_byte_array());

    //render
    Vector2i Size = {1920, 1080};
    cs->compute({static_cast<int32_t>(std::ceil(Size.x / 32.0f)), static_cast<int32_t>(std::ceil(Size.y / 32.0f)), 1});
    output_image->set_data(Size.x, Size.y, false, Image::FORMAT_RGBA8,
                           cs->get_image_uniform_buffer(output_texture_rid));
    output_texture->update(output_image);
    // load texture data?
}
