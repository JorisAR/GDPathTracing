#include "path_tracing_effect.h"

#include <godot_cpp/classes/rd_acceleration_structure_geometry.hpp>
#include <godot_cpp/classes/rd_acceleration_structure_instance.hpp>
#include <godot_cpp/classes/rd_hit_group.hpp>
#include <godot_cpp/classes/rd_pipeline_shader.hpp>
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

// StringNames must not be constructed before godot-cpp is initialized, so no globals of that type.
const char *CONTEXT = "jar_path_tracing";
const char *HISTORY_NAMES[2] = {"history0", "history1"};
const char *GBUFFER_NAMES[2] = {"gbuffer0", "gbuffer1"};
const char *AUX_NAMES[2] = {"aux0", "aux1"};
const char *DENOISE_NAMES[2] = {"denoise0", "denoise1"};
const char *FRAME_COLOR = "frame_color";
const char *FRAME_MOTION = "frame_motion";
const char *FRAME_ALBEDO = "frame_albedo";
const char *GI_HIT = "gi_hit";
// Primary hit material for the ReSTIR passes (rgba32ui, half floats): diffuse albedo, roughness, F0.
const char *SURFACE = "surface";
const char *GI_SAMPLE = "gi_sample";
const char *GI_RADIANCE = "gi_radiance";
const char *RESERVOIR_A_NAMES[2] = {"reservoir_a0", "reservoir_a1"};
const char *RESERVOIR_B_NAMES[2] = {"reservoir_b0", "reservoir_b1"};
const char *RESERVOIR_TEMP_A = "reservoir_temp_a";
const char *RESERVOIR_TEMP_B = "reservoir_temp_b";
const char *DI_HIT_A = "di_hit_a";
const char *DI_HIT_B = "di_hit_b";
const char *DI_RESERVOIR_A_NAMES[2] = {"di_reservoir_a0", "di_reservoir_a1"};
const char *DI_RESERVOIR_B_NAMES[2] = {"di_reservoir_b0", "di_reservoir_b1"};
const char *DI_RESERVOIR_TEMP_A = "di_reservoir_temp_a";
const char *DI_RESERVOIR_TEMP_B = "di_reservoir_temp_b";

// Mirror of the push constant block in restir.glsli.
struct RestirParams
{
    uint32_t mode;
    uint32_t flags;
    int32_t candidates;
    int32_t neighbors;
    float radius;
    float omega;
    float beta;
    float max_confidence;
    int32_t di_candidates;
    int32_t pad[3];
};
static_assert(sizeof(RestirParams) == 48, "RestirParams must match restir.glsli");
enum RestirMode : uint32_t
{
    RESTIR_TEMPORAL = 0,
    RESTIR_SPATIAL = 1,
    RESTIR_DI_TEMPORAL = 2,
    RESTIR_DI_SPATIAL = 3,
};
constexpr uint32_t RESTIR_FLAG_TEMPORAL = 1u;
constexpr uint32_t RESTIR_FLAG_UNIFORM = 2u;

// Mirror of the push constant block in resolve.glsl.
struct ResolveParams
{
    int32_t size[2];
    uint32_t flags;
    uint32_t history_cap;
};

// Mirror of the push constant block in denoise.glsl.
struct DenoiseParams
{
    int32_t size[2];
    int32_t step;
    int32_t mode;
    float sigma_luminance;
    float beta;        // normal exponent of the compatibility heuristic (Junkins et al. 2026, eq. 15)
    float sigma_plane; // plane distance tolerance in pixel footprints
    float omega;       // solid angle of the heuristic's world-space radius
    float pixel_to_dir[16];
    uint32_t frame;
    int32_t candidates;
    float radius;
    float pad;
};
static_assert(sizeof(DenoiseParams) == 112, "DenoiseParams must match denoise.glsl");
enum DenoiseMode : int32_t
{
    DENOISE_PREPARE = 0,
    DENOISE_FILTER = 1,
    DENOISE_FINAL = 2,
};

enum FrameFlags : uint32_t
{
    FLAG_REPROJECT = 1u,
    FLAG_COPY_ONLY = 2u,
    FLAG_RESET = 4u,
    FLAG_STATIC = 8u, // nothing moved for two frames: no reprojection or surface tests
};

// FrameData::flags (common.glsli).
constexpr uint32_t FRAME_FLAG_RESTIR = 1u;
constexpr uint32_t FRAME_FLAG_ENVIRONMENT = 2u;
constexpr uint32_t FRAME_FLAG_RESTIR_DI = 4u;

constexpr uint32_t UNLIMITED_HISTORY = 1u << 30;

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

RID ensure_texture(const Ref<RenderSceneBuffersRD> &buffers, const char *name, const Vector2i &size,
                   RenderingDevice::DataFormat format = RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT)
{
    if (!buffers->has_texture(CONTEXT, name))
    {
        buffers->create_texture(CONTEXT, name, format,
                                RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT,
                                RenderingDevice::TEXTURE_SAMPLES_1, size, 1, 1, true, false);
    }
    return buffers->get_texture(CONTEXT, name);
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
    ClassDB::bind_method(D_METHOD("get_active_backend"), &PathTracingEffect::get_active_backend);
}

void PathTracingEffect::_notification(int p_what)
{
    if (p_what == NOTIFICATION_PREDELETE)
        free_resources();
}

void PathTracingEffect::set_shader_files(const Ref<RDShaderFile> &p_software, const Ref<RDShaderFile> &p_hardware,
                                         const Ref<RDShaderFile> &p_resolver, const Ref<RDShaderFile> &p_denoiser,
                                         const Ref<RDShaderFile> &p_restir, const Ref<RDShaderFile> &p_restir_hw)
{
    std::lock_guard<std::mutex> lock(mutex);
    software_file = p_software;
    hardware_file = p_hardware;
    resolve_file = p_resolver;
    denoise_file = p_denoiser;
    restir_file = p_restir;
    restir_hw_file = p_restir_hw;
}

void PathTracingEffect::set_scene(const SceneDataPtr &p_scene)
{
    std::lock_guard<std::mutex> lock(mutex);
    scene = p_scene;
}

void PathTracingEffect::set_environment(const EnvironmentPtr &p_environment)
{
    std::lock_guard<std::mutex> lock(mutex);
    environment = p_environment;
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

int PathTracingEffect::get_active_backend() const
{
    return active_backend.load();
}

void PathTracingEffect::free_hardware_structures(RenderingDevice *rd)
{
    if (hw_tlas.is_valid())
        rd->free_rid(hw_tlas);
    hw_tlas = RID();
    hw_tlas_capacity = 0;
    for (RID &b : blases)
        if (b.is_valid())
            rd->free_rid(b);
    for (RID &v : blas_vertex_buffers)
        if (v.is_valid())
            rd->free_rid(v);
    blases.clear();
    blas_vertex_buffers.clear();
    blas_vertex_counts.clear();
    hw_geometry_version = 0;
    hw_instance_version = 0;
}

void PathTracingEffect::free_resources()
{
    if (device == nullptr)
        return;
    RenderingDevice *rd = device;
    free_hardware_structures(rd);
    // Freeing a shader also frees its pipeline and the cached uniform sets that depend on it.
    for (Pipeline *p : {&software, &hardware, &resolver, &denoiser})
        free_pipeline(rd, *p);
    for (int i = 0; i < 4; i++)
    {
        free_pipeline(rd, restir_sw[i]);
        free_pipeline(rd, restir_hw[i]);
    }
    if (sampler.is_valid())
        rd->free_rid(sampler);
    if (texture_array.is_valid())
        rd->free_rid(texture_array);
    if (data_texture_array.is_valid())
        rd->free_rid(data_texture_array);
    data_texture_array = RID();
    if (environment_texture.is_valid())
        rd->free_rid(environment_texture);
    environment_texture = RID();
    environment_version = ~0ull;
    environment_cdf.free(rd);
    for (GpuBuffer *b : {&frame_buffer, &bvh_buffer, &triangle_buffer, &triangle_data_buffer, &material_buffer,
                         &instance_material_buffer, &instance_buffer, &tlas_buffer, &emissive_buffer, &light_buffer,
                         &portal_buffer, &restir_params[0], &restir_params[1],
                         &restir_params[2], &restir_params[3]})
        b->free(rd);
    sampler = RID();
    texture_array = RID();
    device = nullptr;
}

void PathTracingEffect::free_pipeline(RenderingDevice *rd, Pipeline &p)
{
    if (p.sbt.is_valid())
    {
        rd->hit_sbt_range_free(p.sbt, p.sbt_range);
        rd->free_rid(p.sbt);
        if (p.pipeline.is_valid())
            rd->free_rid(p.pipeline); // ray tracing pipeline
    }
    if (p.shader.is_valid())
        rd->free_rid(p.shader);
    p = Pipeline();
}

bool PathTracingEffect::ensure_pipeline(RenderingDevice *rd, Pipeline &p, const Ref<RDShaderFile> &file,
                                        const char *name, const StringName &version)
{
    if (p.pipeline.is_valid())
        return true;
    if (p.failed || file.is_null())
        return false;

    Ref<RDShaderSPIRV> spirv = file->get_spirv(version);
    String err = spirv.is_valid() ? spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE)
                                  : String("no SPIR-V");
    if (!err.is_empty())
    {
        UtilityFunctions::printerr("[PathTracing] ", name, " shader compile error:\n", err);
        p.failed = true;
        return false;
    }
    p.shader = rd->shader_create_from_spirv(spirv, name);
    if (!p.shader.is_valid())
    {
        p.failed = true;
        return false;
    }
    p.pipeline = rd->compute_pipeline_create(p.shader);
    if (!p.pipeline.is_valid())
    {
        p.failed = true;
        return false;
    }
    return true;
}

void PathTracingEffect::ensure_sampler(RenderingDevice *rd)
{
    if (sampler.is_valid())
        return;
    Ref<RDSamplerState> ss;
    ss.instantiate();
    ss->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    ss->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    ss->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
    ss->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
    sampler = rd->sampler_create(ss);
}

bool PathTracingEffect::ensure_raytracing_pipeline(RenderingDevice *rd, Pipeline &p, const Ref<RDShaderFile> &file,
                                                   const char *name, const StringName &version)
{
    if (p.pipeline.is_valid())
        return true;
    if (p.failed || file.is_null())
        return false;

    Ref<RDShaderSPIRV> spirv = file->get_spirv(version);
    if (spirv.is_null())
    {
        p.failed = true;
        return false;
    }
    const RenderingDevice::ShaderStage stages[] = {
        RenderingDevice::SHADER_STAGE_RAYGEN, RenderingDevice::SHADER_STAGE_MISS,
        RenderingDevice::SHADER_STAGE_CLOSEST_HIT, RenderingDevice::SHADER_STAGE_ANY_HIT};
    for (RenderingDevice::ShaderStage stage : stages)
    {
        String err = spirv->get_stage_compile_error(stage);
        if (!err.is_empty())
        {
            UtilityFunctions::printerr("[PathTracing] ", name, " shader compile error (stage ", (int)stage, "):\n",
                                       err);
            p.failed = true;
            return false;
        }
    }
    p.shader = rd->shader_create_from_spirv(spirv, name);
    if (!p.shader.is_valid())
    {
        p.failed = true;
        return false;
    }

    // One shader holds the raygen, miss and closest-hit stages.
    Ref<RDPipelineShader> ps;
    ps.instantiate();
    ps->set_shader(p.shader);
    Ref<RDHitGroup> hit_group;
    hit_group.instantiate();
    hit_group->set_closest_hit_shader(ps);
    // Alpha tested geometry: the any-hit stage discards cutout texels (non-opaque BLASes only).
    if (!spirv->get_stage_bytecode(RenderingDevice::SHADER_STAGE_ANY_HIT).is_empty())
        hit_group->set_any_hit_shader(ps);
    TypedArray<RDPipelineShader> raygen, miss;
    raygen.push_back(ps);
    miss.push_back(ps);
    TypedArray<RDHitGroup> hit_groups;
    hit_groups.push_back(hit_group);
    p.pipeline = rd->raytracing_pipeline_create(raygen, miss, hit_groups, 1);
    if (!p.pipeline.is_valid())
    {
        p.failed = true;
        return false;
    }
    p.sbt = rd->hit_sbt_create(p.pipeline, 16);
    p.sbt_range = rd->hit_sbt_range_alloc(p.sbt, 1);
    PackedInt32Array groups;
    groups.push_back(0);
    rd->hit_sbt_range_update(p.sbt, p.sbt_range, 0, groups);
    return true;
}

void PathTracingEffect::build_hardware_structures(RenderingDevice *rd, const SceneSnapshot &snap,
                                                  bool geometry_changed, bool instances_changed)
{
    if (geometry_changed || blases.empty())
    {
        free_hardware_structures(rd);
        for (size_t mesh = 0; mesh < snap.blas_vertices.size(); mesh++)
        {
            const PackedByteArray &verts = snap.blas_vertices[mesh];
            // Meshes with alpha tested triangles are non-opaque, so the any-hit shader runs for them.
            const bool opaque = mesh >= snap.blas_opaque.size() || snap.blas_opaque[mesh];
            uint32_t vertex_count = static_cast<uint32_t>(verts.size() / 12);
            RID vb = rd->vertex_buffer_create(
                static_cast<uint32_t>(verts.size()), verts,
                RenderingDevice::BUFFER_CREATION_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT |
                    RenderingDevice::BUFFER_CREATION_DEVICE_ADDRESS_BIT);
            Ref<RDAccelerationStructureGeometry> geo;
            geo.instantiate();
            if (opaque)
                geo->set_flags(RenderingDevice::ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT);
            geo->set_vertex_buffer(vb);
            geo->set_vertex_offset(0);
            geo->set_vertex_stride(12);
            geo->set_vertex_count(vertex_count);
            geo->set_vertex_format(RenderingDevice::DATA_FORMAT_R32G32B32_SFLOAT);
            TypedArray<RDAccelerationStructureGeometry> geos;
            geos.push_back(geo);
            RID blas = rd->blas_create(geos, RenderingDevice::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
            if (blas.is_valid())
                rd->blas_build(blas);
            blas_vertex_buffers.push_back(vb);
            blases.push_back(blas);
            blas_vertex_counts.push_back(vertex_count);
        }
        instances_changed = true;
    }

    if (!instances_changed || blases.empty())
        return;

    uint32_t needed = MAX(1u, static_cast<uint32_t>(snap.hw_instances.size()));
    if (!hw_tlas.is_valid() || needed > hw_tlas_capacity)
    {
        if (hw_tlas.is_valid())
            rd->free_rid(hw_tlas);
        hw_tlas_capacity = MAX(needed + needed / 2, 16u);
        hw_tlas = rd->tlas_create(hw_tlas_capacity, RenderingDevice::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
    }

    TypedArray<RDAccelerationStructureInstance> list;
    for (const HwInstance &hi : snap.hw_instances)
    {
        if (hi.mesh >= blases.size() || !blases[hi.mesh].is_valid())
            continue;
        Ref<RDAccelerationStructureInstance> inst;
        inst.instantiate();
        inst->set_transform(hi.transform);
        inst->set_id(hi.index);
        inst->set_mask(0xFF);
        inst->set_blas(blases[hi.mesh]);
        inst->set_hit_sbt_range(hardware.sbt_range);
        inst->set_flags(RenderingDevice::ACCELERATION_STRUCTURE_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT);
        list.push_back(inst);
    }
    if (list.is_empty())
    {
        // Keep the TLAS valid with an instance that no ray can hit (mask 0).
        Ref<RDAccelerationStructureInstance> inst;
        inst.instantiate();
        inst->set_mask(0);
        inst->set_blas(blases[0]);
        inst->set_hit_sbt_range(hardware.sbt_range);
        list.push_back(inst);
    }
    rd->tlas_build(hw_tlas, list);
}

void PathTracingEffect::upload_scene(RenderingDevice *rd, const SceneSnapshot &snap, bool use_hardware)
{
    bool geometry_changed = snap.geometry_version != geometry_version;
    bool instances_changed = snap.instance_version != instance_version;

    if (geometry_changed)
    {
        geometry_version = snap.geometry_version;
        bvh_buffer.upload(rd, snap.bvh_nodes);
        triangle_buffer.upload(rd, snap.triangles);
        triangle_data_buffer.upload(rd, snap.triangle_data);
        material_buffer.upload(rd, snap.materials);
        instance_material_buffer.upload(rd, snap.instance_materials);

        // Colors are sRGB; roughness, metallic and normal maps are linear data.
        auto make_array = [&](const std::vector<Ref<Image>> &images, RenderingDevice::DataFormat format) {
            if (images.empty())
                return RID();
            Ref<RDTextureFormat> fmt;
            fmt.instantiate();
            fmt->set_format(format);
            fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D_ARRAY);
            fmt->set_width(snap.texture_resolution);
            fmt->set_height(snap.texture_resolution);
            fmt->set_array_layers(static_cast<uint32_t>(images.size()));
            fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                                RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
            TypedArray<PackedByteArray> layers;
            for (const Ref<Image> &img : images)
                layers.push_back(img->get_data());
            Ref<RDTextureView> view;
            view.instantiate();
            return rd->texture_create(fmt, view, layers);
        };
        for (RID *t : {&texture_array, &data_texture_array})
        {
            if (t->is_valid())
                rd->free_rid(*t);
            *t = RID();
        }
        texture_array = make_array(snap.textures, RenderingDevice::DATA_FORMAT_R8G8B8A8_SRGB);
        data_texture_array = make_array(snap.data_textures, RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
    }

    if (instances_changed)
    {
        instance_version = snap.instance_version;
        instance_buffer.upload(rd, snap.instances);
        tlas_buffer.upload(rd, snap.tlas_nodes);
        emissive_buffer.upload(rd, snap.emissive_triangles);
        light_buffer.upload(rd, snap.punctual_lights);
        portal_buffer.upload(rd, snap.portals);
        instance_count = snap.instance_count;
        emissive_count = snap.emissive_count;
        punctual_count = snap.punctual_count;
        portal_count = snap.portal_count;
        emissive_total_power = snap.emissive_total_power;
    }

    if (use_hardware)
    {
        bool hw_geo = hw_geometry_version != geometry_version;
        bool hw_inst = hw_instance_version != instance_version;
        if (hw_geo || hw_inst)
        {
            build_hardware_structures(rd, snap, hw_geo, hw_inst);
            hw_geometry_version = geometry_version;
            hw_instance_version = instance_version;
        }
    }
}

void PathTracingEffect::run_denoiser(RenderingDevice *rd, const Ref<RenderSceneBuffersRD> &buffers,
                                     const Vector2i &size, uint32_t slot, int iterations,
                                     const Transform3D &cam_transform, const Projection &projection)
{
    RID tmp[2] = {ensure_texture(buffers, DENOISE_NAMES[0], size), ensure_texture(buffers, DENOISE_NAMES[1], size)};
    RID color = buffers->get_color_layer(0);

    DenoiseParams params{};
    params.size[0] = size.x;
    params.size[1] = size.y;
    params.sigma_luminance = 4.0f;
    // The paper uses beta = 8 to pick ReSTIR neighbours; a filter averages radiance directly, so
    // creases need a much sharper normal falloff.
    params.beta = 64.0f;
    params.sigma_plane = 1.0f;
    params.omega = 0.05f; // paper's value
    write_projection(params.pixel_to_dir,
                     Projection(Transform3D(cam_transform.basis, Vector3())) * projection.inverse());
    params.frame = frame_index;
    params.candidates = 32; // K, paper's recommended 8..32
    params.radius = 8.0f;

    // Pass 0 prepares tmp[0]; filter pass i reads tmp[i % 2] and writes tmp[(i + 1) % 2]; the
    // last one writes the color buffer instead.
    const int passes = 1 + iterations;
    for (int pass = 0; pass < passes; pass++)
    {
        RID src = pass == 0 ? tmp[1] : tmp[(pass - 1) % 2];
        RID dst = pass == 0 ? tmp[0] : tmp[pass % 2];
        params.mode = pass == 0 ? DENOISE_PREPARE : (pass == passes - 1 ? DENOISE_FINAL : DENOISE_FILTER);
        params.step = pass == 0 ? 1 : 1 << (pass - 1);

        TypedArray<RDUniform> uniforms;
        uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 0, {history_textures[slot]}));
        uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 1, {gbuffer_textures[slot]}));
        uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 2, {aux_textures[slot]}));
        uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 3, {src}));
        uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 4, {dst}));
        uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 5, {color}));
        dispatch_compute(rd, denoiser, uniforms, to_bytes(&params, 1), size);
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
    EnvironmentPtr env;
    RenderSettings current;
    Ref<RDShaderFile> sw_file, hw_file, rs_file, dn_file, rt_file, rt_hw_file;
    {
        std::lock_guard<std::mutex> lock(mutex);
        scene_ptr = scene;
        env = environment;
        current = settings;
        sw_file = software_file;
        hw_file = hardware_file;
        rs_file = resolve_file;
        dn_file = denoise_file;
        rt_file = restir_file;
        rt_hw_file = restir_hw_file;
    }
    if (!scene_ptr)
        return;

    // Pick the backend: hardware ray tracing pipeline when allowed and supported, software otherwise.
    bool use_hardware = false;
    if (current.backend != BACKEND_SOFTWARE && rd->has_feature(RenderingDevice::SUPPORTS_RAYTRACING_PIPELINE))
        use_hardware = ensure_raytracing_pipeline(rd, hardware, hw_file, "jar_path_tracing_hw");
    if (!use_hardware && current.backend == BACKEND_HARDWARE)
    {
        static bool warned = false;
        if (!warned)
            UtilityFunctions::push_warning("[PathTracing] hardware ray tracing unavailable, using software tracing");
        warned = true;
    }
    if (!use_hardware && !ensure_pipeline(rd, software, sw_file, "jar_path_tracing_sw"))
        return;
    if (!ensure_pipeline(rd, resolver, rs_file, "jar_path_tracing_resolve"))
        return;
    const Pipeline &pipe = use_hardware ? hardware : software;

    Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
    RenderSceneData *scene_data = p_render_data->get_render_scene_data();
    if (buffers.is_null() || scene_data == nullptr)
        return;
    const Vector2i size = buffers->get_internal_size();
    if (size.x <= 0 || size.y <= 0)
        return;

    bool reset = reset_requested.exchange(false);

    // Environment map: texture + importance sampling tables, re-uploaded when it changes.
    const uint64_t env_version = env ? env->version : 0;
    if (env_version != environment_version || !environment_texture.is_valid())
    {
        if (environment_texture.is_valid())
            rd->free_rid(environment_texture);
        Ref<RDTextureFormat> fmt;
        fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
        fmt->set_width(env ? env->width : 1);
        fmt->set_height(env ? env->height : 1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
        TypedArray<PackedByteArray> layers;
        if (env)
        {
            layers.push_back(env->image->get_data());
            environment_cdf.upload(rd, env->alias);
        }
        else
        {
            PackedByteArray black;
            black.resize(16);
            black.fill(0);
            layers.push_back(black);
            PackedByteArray one; // a single texel that keeps itself
            one.resize(8);
            one.encode_float(0, 1.0f);
            one.encode_u32(4, 0);
            environment_cdf.upload(rd, one);
        }
        Ref<RDTextureView> view;
        view.instantiate();
        environment_texture = rd->texture_create(fmt, view, layers);
        environment_version = env_version;
        reset = true;
    }
    if (use_hardware != was_hardware)
        reset = true;
    was_hardware = use_hardware;

    // Upload whatever changed on the main thread.
    SceneSnapshot snap = scene_ptr->snapshot();
    const uint64_t prev_geometry = geometry_version;
    const uint64_t prev_instances = instance_version;
    upload_scene(rd, snap, use_hardware);
    const bool scene_moved = instance_version != prev_instances;
    if (geometry_version != prev_geometry)
        reset = true;
    if (!bvh_buffer.rid().is_valid() || !instance_buffer.rid().is_valid() || !texture_array.is_valid() ||
        !data_texture_array.is_valid())
        return;
    if (use_hardware && !hw_tlas.is_valid())
        return;
    active_backend = use_hardware ? 2 : 1;

    // History targets live in the render buffers, so they follow viewport resizes automatically.
    for (int i = 0; i < 2; i++)
    {
        RID h = ensure_texture(buffers, HISTORY_NAMES[i], size);
        RID g = ensure_texture(buffers, GBUFFER_NAMES[i], size);
        RID a = ensure_texture(buffers, AUX_NAMES[i], size);
        if (h != history_textures[i] || g != gbuffer_textures[i] || a != aux_textures[i])
            reset = true;
        history_textures[i] = h;
        gbuffer_textures[i] = g;
        aux_textures[i] = a;
    }

    const Transform3D cam_transform = scene_data->get_cam_transform();
    const Projection projection = scene_data->get_cam_projection();
    const bool camera_moved = cam_transform != last_cam_transform || !projection_equal(projection, last_projection);
    if (!current.same_image(last_settings))
        reset = true;

    const bool temporal = current.accumulate && current.temporal_reprojection;
    if (!has_previous)
        reset = true;
    const bool current_reset = reset; // settings/scene/resolution: invalidates ReSTIR reservoirs too
    if (!temporal && (camera_moved || scene_moved))
        reset = true; // plain accumulation restarts on any change

    const bool changed = reset || camera_moved || scene_moved;
    if (changed)
    {
        frames_since_change = 0;
        motion_cooldown = 2;
    }
    else if (motion_cooldown > 0)
    {
        motion_cooldown--;
    }

    // While things move, keep a short rolling history; once static, accumulate without limit.
    uint32_t history_cap;
    if (!current.accumulate)
        history_cap = 1;
    else if (temporal && motion_cooldown > 0)
        history_cap = static_cast<uint32_t>(MAX(current.temporal_max_history, 1));
    else
        history_cap = current.max_samples > 0 ? static_cast<uint32_t>(current.max_samples) : UNLIMITED_HISTORY;

    const bool done = current.accumulate && current.max_samples > 0 && !changed && motion_cooldown == 0 &&
                      frames_since_change >= static_cast<uint32_t>(current.max_samples);

    FrameData fd{};
    write_transform(fd.cam_to_world, cam_transform);
    write_projection(fd.inv_projection, projection.inverse());
    const Transform3D prev_cam = has_previous ? last_cam_transform : cam_transform;
    const Projection prev_proj = has_previous ? last_projection : projection;
    write_projection(fd.prev_view_proj, prev_proj * Projection(prev_cam.affine_inverse()));
    fd.prev_cam_pos[0] = prev_cam.origin.x;
    fd.prev_cam_pos[1] = prev_cam.origin.y;
    fd.prev_cam_pos[2] = prev_cam.origin.z;
    fd.prev_cam_pos[3] = 1.0f;
    write_color(fd.sky_top, current.sky_top, current.sky_energy);
    write_color(fd.sky_horizon, current.sky_horizon, current.sky_energy);
    write_color(fd.ground, current.ground, current.sky_energy);
    fd.size[0] = size.x;
    fd.size[1] = size.y;
    fd.frame_index = frame_index++;
    fd.max_bounces = static_cast<uint32_t>(MAX(current.max_bounces, 0));
    fd.samples_per_pixel = static_cast<uint32_t>(MAX(current.samples_per_pixel, 1));
    fd.instance_count = instance_count;
    fd.emissive_count = emissive_count;
    fd.punctual_count = punctual_count;
    fd.emissive_total_power = emissive_total_power;
    fd.firefly_clamp = current.firefly_clamp;
    fd.debug_mode = static_cast<uint32_t>(current.debug_mode);
    fd.portal_count = portal_count;
    fd.max_portal_hops = static_cast<uint32_t>(MAX(current.max_portal_hops, 0));
    fd.env_params[0] = current.environment_energy;
    fd.env_params[1] = current.environment_rotation;
    fd.env_params[2] = env ? env->integral : 0.0f;
    fd.env_size[0] = env ? env->width : 1;
    fd.env_size[1] = env ? env->height : 1;

    // ReSTIR GI needs its pass for the active backend; without it the trace pass keeps full paths.
    bool restir = (current.restir_gi || current.restir_di) && current.debug_mode == 0;
    static const char *const RESTIR_VERSIONS[4] = {"gi_temporal", "gi_spatial", "di_temporal", "di_spatial"};
    for (int i = 0; i < 4 && restir; i++)
        restir = use_hardware ? ensure_raytracing_pipeline(rd, restir_hw[i], rt_hw_file, "jar_path_tracing_restir_hw",
                                                           RESTIR_VERSIONS[i])
                              : ensure_pipeline(rd, restir_sw[i], rt_file, "jar_path_tracing_restir", RESTIR_VERSIONS[i]);
    const bool restir_gi = restir && current.restir_gi;
    const bool restir_di = restir && current.restir_di;
    fd.flags = (restir_gi ? FRAME_FLAG_RESTIR : 0u) | (restir_di ? FRAME_FLAG_RESTIR_DI : 0u) |
               (env && env->integral > 0.0f ? FRAME_FLAG_ENVIRONMENT : 0u);
    frame_buffer.upload(rd, to_bytes(&fd, 1));

    last_cam_transform = cam_transform;
    last_projection = projection;
    last_settings = current;
    has_previous = true;

    const uint32_t write_slot = parity;
    const uint32_t read_slot = 1u - parity;

    const RID frame_color = ensure_texture(buffers, FRAME_COLOR, size);
    const RID frame_motion = ensure_texture(buffers, FRAME_MOTION, size);
    const RID frame_albedo = ensure_texture(buffers, FRAME_ALBEDO, size);
    const RID color_image = buffers->get_color_layer(0);

    const RID gi_hit = ensure_texture(buffers, GI_HIT, size);
    const RID surface = ensure_texture(buffers, SURFACE, size, RenderingDevice::DATA_FORMAT_R32G32B32A32_UINT);
    const RID gi_sample = ensure_texture(buffers, GI_SAMPLE, size);
    const RID gi_radiance = ensure_texture(buffers, GI_RADIANCE, size);
    const RID reservoir_temp_a = ensure_texture(buffers, RESERVOIR_TEMP_A, size);
    const RID reservoir_temp_b = ensure_texture(buffers, RESERVOIR_TEMP_B, size);
    const RID di_hit_a = ensure_texture(buffers, DI_HIT_A, size);
    const RID di_hit_b = ensure_texture(buffers, DI_HIT_B, size);
    const RID di_temp_a = ensure_texture(buffers, DI_RESERVOIR_TEMP_A, size);
    const RID di_temp_b = ensure_texture(buffers, DI_RESERVOIR_TEMP_B, size);
    for (int i = 0; i < 2; i++)
    {
        RID a = ensure_texture(buffers, DI_RESERVOIR_A_NAMES[i], size);
        RID b = ensure_texture(buffers, DI_RESERVOIR_B_NAMES[i], size);
        if (a != di_reservoir_a[i] || b != di_reservoir_b[i])
            di_reservoirs_valid = false;
        di_reservoir_a[i] = a;
        di_reservoir_b[i] = b;
    }
    for (int i = 0; i < 2; i++)
    {
        RID a = ensure_texture(buffers, RESERVOIR_A_NAMES[i], size);
        RID b = ensure_texture(buffers, RESERVOIR_B_NAMES[i], size);
        if (a != reservoir_a[i] || b != reservoir_b[i])
            reservoirs_valid = false;
        reservoir_a[i] = a;
        reservoir_b[i] = b;
    }

    if (!done)
    {
        TypedArray<RDUniform> set0;
        set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 0, {frame_buffer.rid()}));
        const RID images[] = {frame_color, frame_motion, frame_albedo, gbuffer_textures[write_slot],
                              gi_hit,      gi_sample,    gi_radiance,  di_hit_a,
                              di_hit_b};
        for (int i = 0; i < 9; i++)
            set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, i + 1, {images[i]}));
        set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 10, {surface}));
        rd->capture_timestamp("PT begin");
        dispatch_scene(rd, pipe, use_hardware, size, set0, PackedByteArray());
        rd->capture_timestamp("PT trace");

        if (restir)
        {
            // Temporal reuse merges the new samples with last frame's final reservoirs (read slot)
            // into scratch reservoirs; the spatial pass resamples those across neighbours, adds the
            // shaded GI to the frame color and writes this frame's final reservoirs (write slot).
            TypedArray<RDUniform> rset0;
            rset0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 0, {frame_buffer.rid()}));
            const RID rimages[] = {frame_color,
                                   frame_motion,
                                   gbuffer_textures[write_slot],
                                   gbuffer_textures[read_slot],
                                   gi_hit,
                                   gi_sample,
                                   gi_radiance,
                                   reservoir_a[read_slot],
                                   reservoir_b[read_slot],
                                   reservoir_temp_a,
                                   reservoir_temp_b};
            for (int i = 0; i < 11; i++)
                rset0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, i + 1, {rimages[i]}));
            rset0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 13, {reservoir_a[write_slot]}));
            rset0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 14, {reservoir_b[write_slot]}));
            const RID di_images[] = {di_hit_a,
                                     di_hit_b,
                                     di_reservoir_a[read_slot],
                                     di_reservoir_b[read_slot],
                                     di_temp_a,
                                     di_temp_b,
                                     di_reservoir_a[write_slot],
                                     di_reservoir_b[write_slot]};
            for (int i = 0; i < 8; i++)
                rset0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 15 + i, {di_images[i]}));
            rset0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, 23, {surface}));

            RestirParams params{};
            // Reservoir reuse only needs valid motion, so it also runs without accumulation.
            const bool reuse_ok = current.temporal_reprojection && !current_reset;
            const uint32_t selection = current.restir_guided_selection ? 0u : RESTIR_FLAG_UNIFORM;
            params.di_candidates = current.restir_di_candidates;
            params.candidates = current.restir_candidates;
            params.neighbors = current.restir_neighbors;
            params.radius = current.restir_radius;
            params.omega = 0.05f; // paper's values
            params.beta = 8.0f;
            params.max_confidence = 20.0f;
            // GI first, then DI; both add their shading to the frame color.
            for (uint32_t mode : {RESTIR_TEMPORAL, RESTIR_SPATIAL, RESTIR_DI_TEMPORAL, RESTIR_DI_SPATIAL})
            {
                const bool di_mode = mode >= RESTIR_DI_TEMPORAL;
                if (di_mode ? !restir_di : !restir_gi)
                    continue;
                const bool reuse = reuse_ok && (di_mode ? di_reservoirs_valid : reservoirs_valid);
                params.flags = (reuse ? RESTIR_FLAG_TEMPORAL : 0u) | selection;
                params.mode = mode;
                restir_params[mode].upload(rd, to_bytes(&params, 1));
                TypedArray<RDUniform> set0 = rset0.duplicate();
                set0.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER, 12,
                                            {restir_params[mode].rid()}));
                dispatch_scene(rd, use_hardware ? restir_hw[mode] : restir_sw[mode], use_hardware, size, set0,
                               PackedByteArray());
                static const char *const MODE_NAMES[] = {"PT restir gi temporal", "PT restir gi spatial",
                                                         "PT restir di temporal", "PT restir di spatial"};
                rd->capture_timestamp(MODE_NAMES[mode]);
            }
        }
        reservoirs_valid = restir_gi;
        di_reservoirs_valid = restir_di;
    }

    // Temporal resolve: history (read slot) + this frame -> history (write slot) and the color buffer.
    {
        ResolveParams params{};
        params.size[0] = size.x;
        params.size[1] = size.y;
        params.flags = (temporal ? FLAG_REPROJECT : 0u) | (done ? FLAG_COPY_ONLY : 0u) | (reset ? FLAG_RESET : 0u) |
                       (!changed && motion_cooldown == 0 ? FLAG_STATIC : 0u);
        params.history_cap = history_cap;
        const RID images[] = {frame_color,
                              frame_motion,
                              frame_albedo,
                              gbuffer_textures[write_slot],
                              gbuffer_textures[read_slot],
                              history_textures[read_slot],
                              aux_textures[read_slot],
                              history_textures[write_slot],
                              aux_textures[write_slot],
                              color_image};
        TypedArray<RDUniform> uniforms;
        for (int i = 0; i < 10; i++)
            uniforms.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_IMAGE, i, {images[i]}));
        dispatch_compute(rd, resolver, uniforms, to_bytes(&params, 1), size);
        rd->capture_timestamp("PT resolve");
    }

    // Spatial denoising of the accumulated image. When `done`, the history was not rewritten this
    // frame and the final image lives in the read slot.
    if (current.denoise && current.denoise_iterations > 0 && current.debug_mode == 0 &&
        ensure_pipeline(rd, denoiser, dn_file, "jar_path_tracing_denoise"))
    {
        run_denoiser(rd, buffers, size, done ? read_slot : write_slot, current.denoise_iterations, cam_transform,
                     projection);
        rd->capture_timestamp("PT denoise");
    }

    if (!done)
    {
        parity = 1u - parity;
        frames_since_change++;
    }
    sample_count = static_cast<int>(current.accumulate ? frames_since_change : 1);
}

void PathTracingEffect::dispatch_compute(RenderingDevice *rd, const Pipeline &p, const TypedArray<RDUniform> &uniforms,
                                         const PackedByteArray &push_constant, const Vector2i &size)
{
    RID set = UniformSetCacheRD::get_cache(p.shader, 0, uniforms);
    int64_t list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(list, p.pipeline);
    rd->compute_list_bind_uniform_set(list, set, 0);
    rd->compute_list_set_push_constant(list, push_constant, static_cast<uint32_t>(push_constant.size()));
    rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
    rd->compute_list_end();
}

void PathTracingEffect::dispatch_scene(RenderingDevice *rd, const Pipeline &pipe, bool use_hardware,
                                       const Vector2i &size, const TypedArray<RDUniform> &set0,
                                       const PackedByteArray &push_constant)
{
    TypedArray<RDUniform> set1;
    const RID storage[] = {bvh_buffer.rid(),      triangle_buffer.rid(), triangle_data_buffer.rid(),
                           material_buffer.rid(), instance_material_buffer.rid(), instance_buffer.rid(),
                           tlas_buffer.rid(),     emissive_buffer.rid(), light_buffer.rid(),
                           portal_buffer.rid()};
    for (int i = 0; i < 10; i++)
        set1.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, i, {storage[i]}));
    if (use_hardware)
        set1.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_ACCELERATION_STRUCTURE, 10, {hw_tlas}));
    set1.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER, 11, {environment_cdf.rid()}));

    ensure_sampler(rd);
    TypedArray<RDUniform> set2;
    set2.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, {sampler, texture_array}));
    set2.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, {sampler, data_texture_array}));
    set2.push_back(make_uniform(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, {sampler, environment_texture}));

    RID us0 = UniformSetCacheRD::get_cache(pipe.shader, 0, set0);
    RID us1 = UniformSetCacheRD::get_cache(pipe.shader, 1, set1);
    RID us2 = UniformSetCacheRD::get_cache(pipe.shader, 2, set2);

    if (use_hardware)
    {
        int64_t list = rd->raytracing_list_begin();
        rd->raytracing_list_bind_raytracing_pipeline(list, pipe.pipeline);
        rd->raytracing_list_bind_uniform_set(list, us0, 0);
        rd->raytracing_list_bind_uniform_set(list, us1, 1);
        rd->raytracing_list_bind_uniform_set(list, us2, 2);
        if (!push_constant.is_empty())
            rd->raytracing_list_set_push_constant(list, push_constant, static_cast<uint32_t>(push_constant.size()));
        rd->raytracing_list_trace_rays(list, 0, pipe.sbt, size.x, size.y, 1);
        rd->raytracing_list_end();
    }
    else
    {
        int64_t list = rd->compute_list_begin();
        rd->compute_list_bind_compute_pipeline(list, pipe.pipeline);
        rd->compute_list_bind_uniform_set(list, us0, 0);
        rd->compute_list_bind_uniform_set(list, us1, 1);
        rd->compute_list_bind_uniform_set(list, us2, 2);
        if (!push_constant.is_empty())
            rd->compute_list_set_push_constant(list, push_constant, static_cast<uint32_t>(push_constant.size()));
        rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
        rd->compute_list_end();
    }
}
