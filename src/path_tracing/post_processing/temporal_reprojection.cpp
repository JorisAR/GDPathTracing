#include "temporal_reprojection.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <utils.h>

TemporalReprojection::TemporalReprojection()
{
}

TemporalReprojection::~TemporalReprojection()
{
    if(cs != nullptr)
        delete cs;
}

void TemporalReprojection::init(RenderingDevice *rd, const RID original_screen_texture_rid, const RID original_depth_texture_rid, const Vector2i size)
{
    // godot::UtilityFunctions::print(original_screen_texture_rid);
    screen_texture_rid = original_screen_texture_rid;
    // depth_texture_rid = original_depth_texture_rid;

    { // setup parameters
        render_parameters.width = size.x;
        render_parameters.height = size.y;
        render_parameters.frame_count = 1;
    }

    // setup compute shader
    cs = new ComputeShader("res://addons/jar_path_tracing/src/shaders/temporal_reprojection.glsl", rd);
    //--------- GENERAL BUFFERS ---------
    { // input general buffer
        render_parameters_rid = cs->create_storage_buffer_uniform(render_parameters.to_packed_byte_array(), 0, 0);
        
        cs->add_existing_buffer(screen_texture_rid, RenderingDevice::UNIFORM_TYPE_IMAGE, 1, 0);
        cs->add_existing_buffer(original_depth_texture_rid, RenderingDevice::UNIFORM_TYPE_IMAGE, 2, 0);

    }

    { // frame_buffer texture
        auto frame_buffer_format = cs->create_texture_format(size.x, size.y, RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
        Ref<RDTextureView> frame_buffer_texture_view = memnew(RDTextureView);

        frame_buffer_image_1 = Image::create(size.x, size.y, false, Image::FORMAT_RGBAF);
        frame_buffer_image_2 = Image::create(size.x, size.y, false, Image::FORMAT_RGBAF);
        frame_buffer_texture_1 = ImageTexture::create_from_image(frame_buffer_image_1);
        frame_buffer_texture_2 = ImageTexture::create_from_image(frame_buffer_image_2);
        frame_buffer_rid_1 = cs->create_image_uniform(frame_buffer_image_1, frame_buffer_format, frame_buffer_texture_view, 3, 0);
        frame_buffer_rid_2 = cs->create_image_uniform(frame_buffer_image_2, frame_buffer_format, frame_buffer_texture_view, 4, 0);
    }

    cs->finish_create_uniforms();
}

void TemporalReprojection::render(Transform3D view_matrix, Projection projection_matrix)
{
    // camera.get_global_position
    if (cs == nullptr || !cs->check_ready())
        return;
    // update rendering parameters
    Projection vp = projection_matrix * Projection(view_matrix);
    Transform3D deltaMatrix = previous_vp * vp.inverse();
    previous_vp = vp;
    render_parameters.frame_count++;
    Utils::projection_to_float(render_parameters.deltaMatrix, deltaMatrix);    
    cs->update_storage_buffer_uniform(render_parameters_rid, render_parameters.to_packed_byte_array());

    // render
    Vector2i Size = {render_parameters.width, render_parameters.height};
    cs->compute({static_cast<int32_t>(std::ceil(Size.x / 32.0f)), static_cast<int32_t>(std::ceil(Size.y / 32.0f)), 1});
}
