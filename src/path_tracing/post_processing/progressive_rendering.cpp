#include "progressive_rendering.h"
#include <godot_cpp/variant/utility_functions.hpp>

ProgressiveRendering::ProgressiveRendering()
{
}

ProgressiveRendering::~ProgressiveRendering()
{
    if(cs != nullptr)
        delete cs;
}

void ProgressiveRendering::init(RenderingDevice *rd, const RID original_screen_texture_rid, const Ref<RDTextureView> screen_texture_view, const Vector2i size)
{
    // godot::UtilityFunctions::print(original_screen_texture_rid);
    screen_texture_rid = original_screen_texture_rid;
    { // setup parameters
        render_parameters.width = size.x;
        render_parameters.height = size.y;
        render_parameters.frame_count = 1;
    }

    // setup compute shader
    cs = new ComputeShader("res://addons/jar_path_tracing/src/shaders/progressive_rendering.glsl", rd);
    //--------- GENERAL BUFFERS ---------
    { // input general buffer
        render_parameters_rid = cs->create_storage_buffer_uniform(render_parameters.to_packed_byte_array(), 0, 0);
        
        cs->add_existing_buffer(screen_texture_rid, RenderingDevice::UNIFORM_TYPE_IMAGE, 1, 0);
        // cs->create_shared_image_uniform(screen_texture_rid, screen_texture_view, 1, 0);
    }

    { // frame_buffer texture
        auto frame_buffer_format = cs->create_texture_format(size.x, size.y, RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
        Ref<RDTextureView> frame_buffer_texture_view = memnew(RDTextureView);
        frame_buffer_image = Image::create(size.x, size.y, false, Image::FORMAT_RGBAF);
        frame_buffer_texture = ImageTexture::create_from_image(frame_buffer_image);
        frame_buffer_rid = cs->create_image_uniform(frame_buffer_image, frame_buffer_format, frame_buffer_texture_view, 2, 0);
    }



    cs->finish_create_uniforms();
}

void ProgressiveRendering::render(Transform3D camera_transform)
{
    // camera.get_global_position
    if (cs == nullptr || !cs->check_ready())
        return;
    // update rendering parameters
    bool camera_moved = !previous_transform.is_equal_approx(camera_transform);
    previous_transform = camera_transform;

    if(camera_moved) {
        render_parameters.frame_count = 1;
    } else {
        render_parameters.frame_count++;
    }
    cs->update_storage_buffer_uniform(render_parameters_rid, render_parameters.to_packed_byte_array());

    // render
    Vector2i Size = {render_parameters.width, render_parameters.height};
    cs->compute({static_cast<int32_t>(std::ceil(Size.x / 32.0f)), static_cast<int32_t>(std::ceil(Size.y / 32.0f)), 1});
}
