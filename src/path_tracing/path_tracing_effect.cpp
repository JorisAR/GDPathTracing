#include "path_tracing_effect.h"

#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace PT;

namespace
{

// StringNames must not be constructed before godot-cpp is initialized, so no globals here.
const char *CONTEXT = "jar_path_tracing";
const char *ACCUM_NAME = "accumulation";

enum FrameFlags : uint32_t
{
    FLAG_ACCUMULATE = 1u,
    FLAG_COPY_ONLY = 2u,
};

void write_transform(float *dst, const Transform3D &t)
{
    for (int c = 0; c < 3; c++)
    {
        Vector3 col = t.basis.get_column(c);
        dst[c * 4 + 0] = col.x;
        dst[c * 4 + 1] = col.y;
        dst[c * 4 + 2] = col.z;
        dst[c * 4 + 3] = 0.0f;
    }
    dst[12] = t.origin.x;
    dst[13] = t.origin.y;
    dst[14] = t.origin.z;
    dst[15] = 1.0f;
}

void write_projection(float *dst, const Projection &p)
{
    for (int c = 0; c < 4; c++)
    {
        dst[c * 4 + 0] = p.columns[c].x;
        dst[c * 4 + 1] = p.columns[c].y;
        dst[c * 4 + 2] = p.columns[c].z;
        dst[c * 4 + 3] = p.columns[c].w;
    }
}

void write_color(float *dst, const Color &c, float energy)
{
    Color l = c.srgb_to_linear();
    dst[0] = l.r * energy;
    dst[1] = l.g * energy;
    dst[2] = l.b * energy;
    dst[3] = 1.0f;
}

bool projection_equal(const Projection &a, const Projection &b)
{
    for (int i = 0; i < 4; i++)
        if (a.columns[i] != b.columns[i])
            return false;
    return true;
}

} // namespace

PathTracingEffect::PathTracingEffect()
{
    set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
}

void PathTracingEffect::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("reset_accumulation"), &PathTracingEffect::reset_accumulation);
    ClassDB::bind_method(D_METHOD("get_sample_count"), &PathTracingEffect::get_sample_count);
}

void PathTracingEffect::_notification(int p_what)
{
    if (p_what == NOTIFICATION_PREDELETE)
        free_resources();
}

void PathTracingEffect::set_shader_file(const Ref<RDShaderFile> &p_shader_file)
{
    std::lock_guard<std::mutex> lock(mutex);
    shader_file = p_shader_file;
}

void PathTracingEffect::set_scene(const SceneDataPtr &p_scene)
{
    std::lock_guard<std::mutex> lock(mutex);
    scene = p_scene;
}

void PathTracingEffect::set_settings(const RenderSettings &p_settings)
{
    std::lock_guard<std::mutex> lock(mutex);
    settings = p_settings;
}

void PathTracingEffect::reset_accumulation()
{
    reset_requested = true;
}

int PathTracingEffect::get_sample_count() const
{
    return sample_count.load();
}

void PathTracingEffect::free_resources()
{
    if (device == nullptr)
        return;
    RenderingDevice *rd = device;
    // Freeing the shader also frees the pipeline and cached uniform sets that depend on it.
    if (shader.is_valid())
        rd->free_rid(shader);
    if (sampler.is_valid())
        rd->free_rid(sampler);
    if (texture_array.is_valid())
        rd->free_rid(texture_array);
    frame_buffer.free(rd);
    bvh_buffer.free(rd);
    triangle_buffer.free(rd);
    triangle_data_buffer.free(rd);
    material_buffer.free(rd);
    instance_material_buffer.free(rd);
    instance_buffer.free(rd);
    tlas_buffer.free(rd);
    emissive_buffer.free(rd);
    light_buffer.free(rd);
    shader = RID();
    pipeline = RID();
    sampler = RID();
    texture_array = RID();
    device = nullptr;
}

bool PathTracingEffect::ensure_pipeline(RenderingDevice *rd)
{
    if (pipeline.is_valid())
        return true;
    if (shader_failed)
        return false;

    Ref<RDShaderFile> file;
    {
        std::lock_guard<std::mutex> lock(mutex);
        file = shader_file;
    }
    if (file.is_null())
        return false;

    Ref<RDShaderSPIRV> spirv = file->get_spirv();
    if (spirv.is_null())
    {
        UtilityFunctions::printerr("[PathTracing] shader has no SPIR-V");
        shader_failed = true;
        return false;
    }
    String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty())
    {
        UtilityFunctions::printerr("[PathTracing] shader compile error:\n", err);
        shader_failed = true;
        return false;
    }
    shader = rd->shader_create_from_spirv(spirv, "jar_path_tracing");
    if (!shader.is_valid())
    {
        shader_failed = true;
        return false;
    }
    pipeline = rd->compute_pipeline_create(shader);

    Ref<RDSamplerState> ss;
    ss.instantiate();
    ss->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    ss->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    ss->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
    ss->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
    sampler = rd->sampler_create(ss);
    return pipeline.is_valid();
}

void PathTracingEffect::upload_scene(RenderingDevice *rd, const SceneSnapshot &snap)
{
    if (snap.geometry_version != geometry_version)
    {
        geometry_version = snap.geometry_version;
        bvh_buffer.upload(rd, snap.bvh_nodes);
        triangle_buffer.upload(rd, snap.triangles);
        triangle_data_buffer.upload(rd, snap.triangle_data);
        material_buffer.upload(rd, snap.materials);
        instance_material_buffer.upload(rd, snap.instance_materials);

        if (texture_array.is_valid())
            rd->free_rid(texture_array);
        texture_array = RID();
        if (!snap.textures.empty())
        {
            Ref<RDTextureFormat> fmt;
            fmt.instantiate();
            fmt->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_SRGB);
            fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D_ARRAY);
            fmt->set_width(snap.texture_resolution);
            fmt->set_height(snap.texture_resolution);
            fmt->set_array_layers(static_cast<uint32_t>(snap.textures.size()));
            fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                                RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
            TypedArray<PackedByteArray> layers;
            for (const Ref<Image> &img : snap.textures)
                layers.push_back(img->get_data());
            Ref<RDTextureView> view;
            view.instantiate();
            texture_array = rd->texture_create(fmt, view, layers);
        }
    }

    if (snap.instance_version != instance_version)
    {
        instance_version = snap.instance_version;
        instance_buffer.upload(rd, snap.instances);
        tlas_buffer.upload(rd, snap.tlas_nodes);
        emissive_buffer.upload(rd, snap.emissive_triangles);
        light_buffer.upload(rd, snap.punctual_lights);
        instance_count = snap.instance_count;
        emissive_count = snap.emissive_count;
        punctual_count = snap.punctual_count;
        emissive_total_power = snap.emissive_total_power;
    }
}

void PathTracingEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
{
    if (p_effect_callback_type != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT || p_render_data == nullptr)
        return;

    RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd == nullptr)
        return;
    if (device != nullptr && device != rd)
        free_resources();
    device = rd;

    SceneDataPtr scene_ptr;
    RenderSettings current;
    {
        std::lock_guard<std::mutex> lock(mutex);
        scene_ptr = scene;
        current = settings;
    }
    if (!scene_ptr || !ensure_pipeline(rd))
        return;

    Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
    RenderSceneData *scene_data = p_render_data->get_render_scene_data();
    if (buffers.is_null() || scene_data == nullptr)
        return;

    const Vector2i size = buffers->get_internal_size();
    if (size.x <= 0 || size.y <= 0)
        return;

    bool reset = reset_requested.exchange(false);

    // Upload whatever changed on the main thread.
    SceneSnapshot snap = scene_ptr->snapshot();
    const uint64_t prev_geometry = geometry_version;
    const uint64_t prev_instances = instance_version;
    upload_scene(rd, snap);
    if (geometry_version != prev_geometry || instance_version != prev_instances)
        reset = true;
    if (!bvh_buffer.rid().is_valid() || !instance_buffer.rid().is_valid() || !texture_array.is_valid())
        return;

    // Accumulation target lives in the render buffers, so it follows viewport resizes automatically.
    if (!buffers->has_texture(CONTEXT, ACCUM_NAME))
    {
        buffers->create_texture(CONTEXT, ACCUM_NAME, RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT,
                                RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                                    RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT,
                                RenderingDevice::TEXTURE_SAMPLES_1, size, 1, 1, true, false);
    }
    RID accum = buffers->get_texture(CONTEXT, ACCUM_NAME);
    if (accum != accum_texture)
    {
        accum_texture = accum;
        reset = true;
    }

    const Transform3D cam_transform = scene_data->get_cam_transform();
    const Projection projection = scene_data->get_cam_projection();
    if (cam_transform != last_cam_transform || !projection_equal(projection, last_projection))
        reset = true;
    if (current != last_settings)
        reset = true;
    last_cam_transform = cam_transform;
    last_projection = projection;
    last_settings = current;

    if (reset || !current.accumulate)
        sample_index = 0;

    const bool done = current.accumulate && current.max_samples > 0 &&
                      sample_index >= static_cast<uint32_t>(current.max_samples);

    FrameData fd{};
    write_transform(fd.cam_to_world, cam_transform);
    write_projection(fd.inv_projection, projection.inverse());
    write_color(fd.sky_top, current.sky_top, current.sky_energy);
    write_color(fd.sky_horizon, current.sky_horizon, current.sky_energy);
    write_color(fd.ground, current.ground, current.sky_energy);
    fd.size[0] = size.x;
    fd.size[1] = size.y;
    fd.frame_index = frame_index++;
    fd.sample_index = sample_index;
    fd.max_bounces = static_cast<uint32_t>(MAX(current.max_bounces, 0));
    fd.samples_per_pixel = static_cast<uint32_t>(MAX(current.samples_per_pixel, 1));
    fd.instance_count = instance_count;
    fd.emissive_count = emissive_count;
    fd.punctual_count = punctual_count;
    fd.emissive_total_power = emissive_total_power;
    fd.firefly_clamp = current.firefly_clamp;
    fd.debug_mode = static_cast<uint32_t>(current.debug_mode);
    fd.flags = (current.accumulate ? FLAG_ACCUMULATE : 0u) | (done ? FLAG_COPY_ONLY : 0u);
    frame_buffer.upload(rd, to_bytes(&fd, 1));

    RID color = buffers->get_color_layer(0);

    TypedArray<RDUniform> set0;
    set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 0, {frame_buffer.rid()}));
    set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 1, {color}));
    set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, {accum}));

    TypedArray<RDUniform> set1;
    const RID storage[] = {bvh_buffer.rid(),       triangle_buffer.rid(), triangle_data_buffer.rid(),
                           material_buffer.rid(),  instance_material_buffer.rid(), instance_buffer.rid(),
                           tlas_buffer.rid(),      emissive_buffer.rid(), light_buffer.rid()};
    for (int i = 0; i < 9; i++)
        set1.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, i, {storage[i]}));

    TypedArray<RDUniform> set2;
    set2.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, {sampler, texture_array}));

    RID us0 = UniformSetCacheRD::get_cache(shader, 0, set0);
    RID us1 = UniformSetCacheRD::get_cache(shader, 1, set1);
    RID us2 = UniformSetCacheRD::get_cache(shader, 2, set2);

    int64_t list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(list, pipeline);
    rd->compute_list_bind_uniform_set(list, us0, 0);
    rd->compute_list_bind_uniform_set(list, us1, 1);
    rd->compute_list_bind_uniform_set(list, us2, 2);
    rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
    rd->compute_list_end();

    if (current.accumulate && !done)
        sample_index++;
    sample_count = static_cast<int>(current.accumulate ? sample_index : 1);
}
