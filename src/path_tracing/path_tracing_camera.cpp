#include "path_tracing_camera.h"

#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

static const char *SHADER_PATH = "res://addons/jar_path_tracing/src/shaders/main.glsl";
static const char *HW_SHADER_PATH = "res://addons/jar_path_tracing/src/shaders/main_hw.glsl";

void PathTracingCamera::_bind_methods()
{
#define PT_PROP(type, name, hint, hint_string)                                                                     \
    ClassDB::bind_method(D_METHOD("get_" #name), &PathTracingCamera::get_##name);                                 \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &PathTracingCamera::set_##name);                        \
    ADD_PROPERTY(PropertyInfo(type, #name, hint, hint_string), "set_" #name, "get_" #name);

    PT_PROP(Variant::OBJECT, geometry_group, PROPERTY_HINT_NODE_TYPE, "GeometryGroup3D")
    PT_PROP(Variant::BOOL, path_tracing_enabled, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::INT, backend, PROPERTY_HINT_ENUM, "Auto,Software,Hardware")

    ADD_GROUP("Sampling", "");
    PT_PROP(Variant::INT, max_bounces, PROPERTY_HINT_RANGE, "0,32,1")
    PT_PROP(Variant::INT, samples_per_pixel, PROPERTY_HINT_RANGE, "1,64,1")
    PT_PROP(Variant::BOOL, accumulate, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::BOOL, temporal_reprojection, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::INT, temporal_max_history, PROPERTY_HINT_RANGE, "1,256,1")
    PT_PROP(Variant::INT, max_samples, PROPERTY_HINT_RANGE, "0,65536,1,or_greater")
    PT_PROP(Variant::FLOAT, firefly_clamp, PROPERTY_HINT_RANGE, "0,100,0.1,or_greater")
    PT_PROP(Variant::INT, debug_view, PROPERTY_HINT_ENUM, "None,Normals,Albedo,Traversal Cost")
    PT_PROP(Variant::INT, max_portal_hops, PROPERTY_HINT_RANGE, "0,64,1")

    ADD_GROUP("Sky", "");
    PT_PROP(Variant::COLOR, sky_top_color, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::COLOR, sky_horizon_color, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::COLOR, ground_color, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::FLOAT, sky_energy, PROPERTY_HINT_RANGE, "0,16,0.01,or_greater")
#undef PT_PROP

    ClassDB::bind_method(D_METHOD("get_sample_count"), &PathTracingCamera::get_sample_count);
    ClassDB::bind_method(D_METHOD("reset_accumulation"), &PathTracingCamera::reset_accumulation);
    ClassDB::bind_method(D_METHOD("get_active_backend"), &PathTracingCamera::get_active_backend);

    BIND_ENUM_CONSTANT(BACKEND_AUTO);
    BIND_ENUM_CONSTANT(BACKEND_SOFTWARE);
    BIND_ENUM_CONSTANT(BACKEND_HARDWARE);

    BIND_ENUM_CONSTANT(DEBUG_NONE);
    BIND_ENUM_CONSTANT(DEBUG_NORMALS);
    BIND_ENUM_CONSTANT(DEBUG_ALBEDO);
    BIND_ENUM_CONSTANT(DEBUG_TRAVERSAL);
}

void PathTracingCamera::_notification(int p_what)
{
    if (Engine::get_singleton()->is_editor_hint())
        return;

    switch (p_what)
    {
    case NOTIFICATION_READY:
        setup();
        set_process_priority(1001); // after GeometryGroup3D
        set_process_internal(true);
        break;
    case NOTIFICATION_INTERNAL_PROCESS:
        push_settings();
        break;
    }
}

static GeometryGroup3D *find_group(Node *node)
{
    if (GeometryGroup3D *g = Object::cast_to<GeometryGroup3D>(node))
        return g;
    for (int i = 0; i < node->get_child_count(); i++)
        if (GeometryGroup3D *g = find_group(node->get_child(i)))
            return g;
    return nullptr;
}

void PathTracingCamera::setup()
{
    if (geometry_group == nullptr && get_tree() != nullptr && get_tree()->get_current_scene() != nullptr)
        geometry_group = find_group(get_tree()->get_current_scene());
    if (geometry_group == nullptr)
    {
        UtilityFunctions::printerr("[PathTracing] PathTracingCamera has no GeometryGroup3D.");
        return;
    }

    Ref<RDShaderFile> shader_file = ResourceLoader::get_singleton()->load(SHADER_PATH);
    if (shader_file.is_null())
    {
        UtilityFunctions::printerr("[PathTracing] could not load ", SHADER_PATH);
        return;
    }
    // Optional: only used when the GPU supports the ray tracing pipeline.
    Ref<RDShaderFile> hw_shader_file = ResourceLoader::get_singleton()->load(HW_SHADER_PATH);

    effect.instantiate();
    effect->set_shader_files(shader_file, hw_shader_file);
    effect->set_scene(geometry_group->get_scene_data());

    Ref<Compositor> compositor = get_compositor();
    if (compositor.is_null())
    {
        compositor.instantiate();
        set_compositor(compositor);
    }
    TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
    effects.push_back(effect);
    compositor->set_compositor_effects(effects);
    push_settings();
}

void PathTracingCamera::push_settings()
{
    if (effect.is_null())
        return;
    effect->set_enabled(path_tracing_enabled);
    effect->set_settings(settings);
    if (geometry_group != nullptr)
        effect->set_scene(geometry_group->get_scene_data());
}

// ----------------------------------- PROPERTIES -----------------------------------

GeometryGroup3D *PathTracingCamera::get_geometry_group() const
{
    return geometry_group;
}
void PathTracingCamera::set_geometry_group(GeometryGroup3D *value)
{
    geometry_group = value;
}
bool PathTracingCamera::get_path_tracing_enabled() const
{
    return path_tracing_enabled;
}
void PathTracingCamera::set_path_tracing_enabled(bool value)
{
    path_tracing_enabled = value;
}
int PathTracingCamera::get_max_bounces() const
{
    return settings.max_bounces;
}
void PathTracingCamera::set_max_bounces(int value)
{
    settings.max_bounces = MAX(0, value);
}
int PathTracingCamera::get_samples_per_pixel() const
{
    return settings.samples_per_pixel;
}
void PathTracingCamera::set_samples_per_pixel(int value)
{
    settings.samples_per_pixel = MAX(1, value);
}
bool PathTracingCamera::get_accumulate() const
{
    return settings.accumulate;
}
void PathTracingCamera::set_accumulate(bool value)
{
    settings.accumulate = value;
}
int PathTracingCamera::get_max_samples() const
{
    return settings.max_samples;
}
void PathTracingCamera::set_max_samples(int value)
{
    settings.max_samples = MAX(0, value);
}
float PathTracingCamera::get_firefly_clamp() const
{
    return settings.firefly_clamp;
}
void PathTracingCamera::set_firefly_clamp(float value)
{
    settings.firefly_clamp = MAX(0.0f, value);
}
PathTracingCamera::DebugView PathTracingCamera::get_debug_view() const
{
    return static_cast<DebugView>(settings.debug_mode);
}
void PathTracingCamera::set_debug_view(DebugView value)
{
    settings.debug_mode = static_cast<int>(value);
}
Color PathTracingCamera::get_sky_top_color() const
{
    return settings.sky_top;
}
void PathTracingCamera::set_sky_top_color(const Color &value)
{
    settings.sky_top = value;
}
Color PathTracingCamera::get_sky_horizon_color() const
{
    return settings.sky_horizon;
}
void PathTracingCamera::set_sky_horizon_color(const Color &value)
{
    settings.sky_horizon = value;
}
Color PathTracingCamera::get_ground_color() const
{
    return settings.ground;
}
void PathTracingCamera::set_ground_color(const Color &value)
{
    settings.ground = value;
}
float PathTracingCamera::get_sky_energy() const
{
    return settings.sky_energy;
}
void PathTracingCamera::set_sky_energy(float value)
{
    settings.sky_energy = MAX(0.0f, value);
}

PathTracingCamera::Backend PathTracingCamera::get_backend() const
{
    return static_cast<Backend>(settings.backend);
}
void PathTracingCamera::set_backend(Backend value)
{
    settings.backend = static_cast<int>(value);
}
bool PathTracingCamera::get_temporal_reprojection() const
{
    return settings.temporal_reprojection;
}
void PathTracingCamera::set_temporal_reprojection(bool value)
{
    settings.temporal_reprojection = value;
}
int PathTracingCamera::get_temporal_max_history() const
{
    return settings.temporal_max_history;
}
void PathTracingCamera::set_temporal_max_history(int value)
{
    settings.temporal_max_history = MAX(1, value);
}
int PathTracingCamera::get_max_portal_hops() const
{
    return settings.max_portal_hops;
}
void PathTracingCamera::set_max_portal_hops(int value)
{
    settings.max_portal_hops = MAX(0, value);
}
String PathTracingCamera::get_active_backend() const
{
    int b = effect.is_valid() ? effect->get_active_backend() : 0;
    return b == 2 ? "Hardware (RT pipeline)" : (b == 1 ? "Software (compute BVH)" : "Inactive");
}

int PathTracingCamera::get_sample_count() const
{
    return effect.is_valid() ? effect->get_sample_count() : 0;
}

void PathTracingCamera::reset_accumulation()
{
    if (effect.is_valid())
        effect->reset_accumulation();
}
