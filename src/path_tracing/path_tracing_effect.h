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
#include <vector>

using namespace godot;

namespace PT
{

enum Backend
{
    BACKEND_AUTO = 0,
    BACKEND_SOFTWARE = 1,
    BACKEND_HARDWARE = 2,
};

struct RenderSettings
{
    int backend = BACKEND_AUTO;
    int max_bounces = 4;
    int samples_per_pixel = 1;
    bool accumulate = true;
    bool temporal_reprojection = true;
    int temporal_max_history = 16;
    int max_samples = 0; // 0 = unlimited
    int max_portal_hops = 8;
    Color sky_top = Color(0.35f, 0.55f, 0.9f);
    Color sky_horizon = Color(0.85f, 0.9f, 1.0f);
    Color ground = Color(0.3f, 0.28f, 0.25f);
    float sky_energy = 1.0f;
    float firefly_clamp = 0.0f; // 0 = off
    int debug_mode = 0;

    // Settings whose change invalidates accumulated history.
    bool same_image(const RenderSettings &o) const
    {
        return backend == o.backend && max_bounces == o.max_bounces && samples_per_pixel == o.samples_per_pixel &&
               accumulate == o.accumulate && temporal_reprojection == o.temporal_reprojection &&
               max_portal_hops == o.max_portal_hops && sky_top == o.sky_top && sky_horizon == o.sky_horizon &&
               ground == o.ground && sky_energy == o.sky_energy && firefly_clamp == o.firefly_clamp &&
               debug_mode == o.debug_mode;
    }
};

// std140 mirror of FrameData in shaders/common.glsli.
struct FrameData
{
    float cam_to_world[16];
    float inv_projection[16];
    float prev_view_proj[16];
    float prev_cam_pos[4];
    float sky_top[4];
    float sky_horizon[4];
    float ground[4];
    int32_t size[2];
    uint32_t frame_index;
    uint32_t history_cap;
    uint32_t max_bounces;
    uint32_t samples_per_pixel;
    uint32_t instance_count;
    uint32_t emissive_count;
    uint32_t punctual_count;
    float emissive_total_power;
    float firefly_clamp;
    uint32_t debug_mode;
    uint32_t flags;
    uint32_t portal_count;
    uint32_t max_portal_hops;
    uint32_t pad0;
};
static_assert(sizeof(FrameData) == 320, "FrameData must match the std140 layout");

} // namespace PT

// Renders the path traced image straight into the scene's HDR color buffer on Godot's main
// RenderingDevice. Uses the hardware ray tracing pipeline when available (and allowed),
// otherwise the software BVH in a compute shader. Godot's tonemapper, exposure, glow etc. are applied afterwards as usual.
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
    void set_shader_files(const Ref<RDShaderFile> &software, const Ref<RDShaderFile> &hardware);
    void set_scene(const PT::SceneDataPtr &scene);
    void set_settings(const PT::RenderSettings &settings);
    void reset_accumulation();
    int get_sample_count() const;
    // 0 = not rendering yet, 1 = software, 2 = hardware.
    int get_active_backend() const;

  private:
    struct Pipeline
    {
        RID shader;
        RID pipeline;
        // Ray tracing pipeline only: shader binding table with one hit group range.
        RID sbt;
        int64_t sbt_range = 0;
        bool failed = false;
    };

    bool ensure_pipeline(RenderingDevice *rd, Pipeline &p, const Ref<RDShaderFile> &file, const char *name);
    bool ensure_raytracing_pipeline(RenderingDevice *rd, Pipeline &p, const Ref<RDShaderFile> &file);
    void ensure_sampler(RenderingDevice *rd);
    void upload_scene(RenderingDevice *rd, const PT::SceneSnapshot &snap, bool hardware);
    void build_hardware_structures(RenderingDevice *rd, const PT::SceneSnapshot &snap, bool geometry_changed,
                                   bool instances_changed);
    void free_hardware_structures(RenderingDevice *rd);
    void free_resources();

    // Shared between threads, guarded by `mutex`.
    std::mutex mutex;
    Ref<RDShaderFile> software_file;
    Ref<RDShaderFile> hardware_file;
    PT::SceneDataPtr scene;
    PT::RenderSettings settings;
    std::atomic<bool> reset_requested{false};
    std::atomic<int> sample_count{0};
    std::atomic<int> active_backend{0};

    // Render thread state.
    RenderingDevice *device = nullptr;
    Pipeline software;
    Pipeline hardware;
    RID sampler;
    RID texture_array;

    PT::GpuBuffer frame_buffer{PT::GpuBuffer::UNIFORM};
    PT::GpuBuffer bvh_buffer, triangle_buffer, triangle_data_buffer, material_buffer, instance_material_buffer;
    PT::GpuBuffer instance_buffer, tlas_buffer, emissive_buffer, light_buffer, portal_buffer;

    // Hardware acceleration structures.
    std::vector<RID> blas_vertex_buffers;
    std::vector<RID> blases;
    std::vector<uint32_t> blas_vertex_counts;
    RID hw_tlas;
    uint32_t hw_tlas_capacity = 0;
    uint64_t hw_geometry_version = 0;
    uint64_t hw_instance_version = 0;

    uint64_t geometry_version = 0;
    uint64_t instance_version = 0;
    uint32_t instance_count = 0;
    uint32_t emissive_count = 0;
    uint32_t punctual_count = 0;
    uint32_t portal_count = 0;
    float emissive_total_power = 0.0f;

    // Accumulation / temporal state.
    RID history_textures[2];
    RID gbuffer_textures[2];
    uint32_t parity = 0;
    uint32_t frame_index = 0;
    uint32_t frames_since_change = 0;
    uint32_t motion_cooldown = 0;
    bool has_previous = false;
    bool was_hardware = false;
    Transform3D last_cam_transform;
    Projection last_projection;
    PT::RenderSettings last_settings;
};

#endif // PATH_TRACING_EFFECT_H
