#ifndef PATH_TRACING_EFFECT_H
#define PATH_TRACING_EFFECT_H

#include "path_tracing/scene_data.h"
#include "rd/rd_util.h"

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <atomic>
#include <mutex>

using namespace godot;

namespace PT
{

struct RenderSettings
{
    int max_bounces = 4;
    int samples_per_pixel = 1;
    bool accumulate = true;
    int max_samples = 0; // 0 = unlimited
    Color sky_top = Color(0.35f, 0.55f, 0.9f);
    Color sky_horizon = Color(0.85f, 0.9f, 1.0f);
    Color ground = Color(0.3f, 0.28f, 0.25f);
    float sky_energy = 1.0f;
    float firefly_clamp = 0.0f; // 0 = off
    int debug_mode = 0;

    bool operator==(const RenderSettings &o) const
    {
        return max_bounces == o.max_bounces && samples_per_pixel == o.samples_per_pixel &&
               accumulate == o.accumulate && max_samples == o.max_samples && sky_top == o.sky_top &&
               sky_horizon == o.sky_horizon && ground == o.ground && sky_energy == o.sky_energy &&
               firefly_clamp == o.firefly_clamp && debug_mode == o.debug_mode;
    }
    bool operator!=(const RenderSettings &o) const
    {
        return !(*this == o);
    }
};

// std140 mirror of FrameData in shaders/common.glsli.
struct FrameData
{
    float cam_to_world[16];
    float inv_projection[16];
    float sky_top[4];
    float sky_horizon[4];
    float ground[4];
    int32_t size[2];
    uint32_t frame_index;
    uint32_t sample_index;
    uint32_t max_bounces;
    uint32_t samples_per_pixel;
    uint32_t instance_count;
    uint32_t emissive_count;
    uint32_t punctual_count;
    float emissive_total_power;
    float firefly_clamp;
    uint32_t debug_mode;
    uint32_t flags;
    uint32_t pad[3];
};
static_assert(sizeof(FrameData) == 240, "FrameData must match the std140 layout");

} // namespace PT

// Renders the path traced image straight into the scene's HDR color buffer on Godot's main
// RenderingDevice. No CPU readback, no extra presentation pass: Godot's tonemapper, exposure,
// glow etc. are applied afterwards as usual.
class PathTracingEffect : public CompositorEffect
{
    GDCLASS(PathTracingEffect, CompositorEffect);

  protected:
    static void _bind_methods();
    void _notification(int p_what);

  public:
    PathTracingEffect();

    void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

    // Main thread API.
    void set_shader_file(const Ref<RDShaderFile> &shader_file);
    void set_scene(const PT::SceneDataPtr &scene);
    void set_settings(const PT::RenderSettings &settings);
    void reset_accumulation();
    int get_sample_count() const;

  private:
    bool ensure_pipeline(RenderingDevice *rd);
    void upload_scene(RenderingDevice *rd, const PT::SceneSnapshot &snap);
    void free_resources();

    // Shared between threads, guarded by `mutex`.
    std::mutex mutex;
    Ref<RDShaderFile> shader_file;
    PT::SceneDataPtr scene;
    PT::RenderSettings settings;
    std::atomic<bool> reset_requested{false};
    std::atomic<int> sample_count{0};

    // Render thread state.
    RenderingDevice *device = nullptr;
    RID shader;
    RID pipeline;
    RID sampler;
    RID texture_array;
    RID accum_texture;
    bool shader_failed = false;

    PT::GpuBuffer frame_buffer{PT::GpuBuffer::UNIFORM};
    PT::GpuBuffer bvh_buffer, triangle_buffer, triangle_data_buffer, material_buffer, instance_material_buffer;
    PT::GpuBuffer instance_buffer, tlas_buffer, emissive_buffer, light_buffer;

    uint64_t geometry_version = 0;
    uint64_t instance_version = 0;
    uint32_t instance_count = 0;
    uint32_t emissive_count = 0;
    uint32_t punctual_count = 0;
    float emissive_total_power = 0.0f;

    uint32_t frame_index = 0;
    uint32_t sample_index = 0;
    Transform3D last_cam_transform;
    Projection last_projection;
    PT::RenderSettings last_settings;
};

#endif // PATH_TRACING_EFFECT_H
