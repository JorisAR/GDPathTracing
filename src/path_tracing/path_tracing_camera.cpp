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

void PathTracingCamera::init()
{
    //we want to use one RD for all shaders relevant to the camera.
    _rd = RenderingServer::get_singleton()->create_local_rendering_device();

    // setup geometry
    if (geometry_group == nullptr)
    {
        UtilityFunctions::printerr("No geometry group set.");
        return;
    }

    geometry_group->build();

    { // setup parameters
        render_parameters.width = 1920;
        render_parameters.height = 1080;
        render_parameters.fov = fov;
        render_parameters.triangleCount = geometry_group->get_triangle_count();
        render_parameters.blasCount = geometry_group->get_blas_count();
        projection_matrix = Projection::create_perspective(fov, static_cast<float>(render_parameters.width) / render_parameters.height, 0.1f, 1000.0f, true);
        camera.set_camera_transform(get_global_transform().affine_inverse(), projection_matrix);
    }

    // setup compute shader
    cs = new ComputeShader("res://addons/jar_path_tracing/src/shaders/main.glsl", _rd, {"#define TESTe"});
    //--------- GENERAL BUFFERS ---------
    { // input general buffer
        render_parameters_rid = cs->create_storage_buffer_uniform(render_parameters.to_packed_byte_array(), 1, 0);
        camera_rid = cs->create_storage_buffer_uniform(camera.to_packed_byte_array(), 2, 0);
    }

    Ref<RDTextureView> output_texture_view = memnew(RDTextureView);
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
        
        output_texture_rid = cs->create_image_uniform(output_image, output_format, output_texture_view, 0, 0);
    }

    //--------- SCENE STORAGE ---------
    {
        triangles_rid = cs->create_storage_buffer_uniform(geometry_group->get_triangles_buffer(), 0, 1);
        materials_rid = cs->create_storage_buffer_uniform(geometry_group->get_materials_buffer(), 1, 1);
        bvh_tree_rid = cs->create_storage_buffer_uniform(geometry_group->get_bvh_buffer(), 2, 1);
        blas_rid = cs->create_storage_buffer_uniform(geometry_group->get_blas_buffer(), 3, 1);
        tlas_rid = cs->create_storage_buffer_uniform(geometry_group->get_tlas_buffer(), 4, 1);
    }

    cs->finish_create_uniforms();

    progressive_renderer = new ProgressiveRendering();
    progressive_renderer->init(_rd, output_texture_rid, output_texture_view);
}

void PathTracingCamera::clear_compute_shader()
{
}

void PathTracingCamera::render()
{
    if (cs == nullptr || !cs->check_ready())
        return;
    // update rendering parameters
    camera.set_camera_transform(get_global_transform(), projection_matrix);
    camera.frame_index++;
    cs->update_storage_buffer_uniform(camera_rid, camera.to_packed_byte_array());

    // render
    Vector2i Size = {1920, 1080};
    cs->compute({static_cast<int32_t>(std::ceil(Size.x / 32.0f)), static_cast<int32_t>(std::ceil(Size.y / 32.0f)), 1});
    
    {// post processing
        progressive_renderer->render(get_global_transform());
    }
    
    output_image->set_data(Size.x, Size.y, false, Image::FORMAT_RGBA8,
                           cs->get_image_uniform_buffer(output_texture_rid));
    output_texture->update(output_image);
    // load texture data?
}
