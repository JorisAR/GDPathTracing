#include "path_tracing_camera.h"

#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

static const char *SHADER_PATH = "res://addons/jar_path_tracing/src/shaders/main.glsl";
static const char *HW_SHADER_PATH = "res://addons/jar_path_tracing/src/shaders/main_hw.glsl";
static const char *RESOLVE_SHADER_PATH = "res://addons/jar_path_tracing/src/shaders/resolve.glsl";
static const char *DENOISE_SHADER_PATH = "res://addons/jar_path_tracing/src/shaders/denoise.glsl";

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

    ADD_GROUP("Denoising", "");
    PT_PROP(Variant::BOOL, denoise, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::INT, denoise_iterations, PROPERTY_HINT_RANGE, "1,6,1")

    ADD_GROUP("Sky", "");
    PT_PROP(Variant::COLOR, sky_top_color, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::COLOR, sky_horizon_color, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::COLOR, ground_color, PROPERTY_HINT_NONE, "")
    PT_PROP(Variant::FLOAT, sky_energy, PROPERTY_HINT_RANGE, "0,16,0.01,or_greater")
#undef PT_PROP

    ADD_GROUP("Editor", "");
    ClassDB::bind_method(D_METHOD("get_editor_preview"), &PathTracingCamera::get_editor_preview);
    ClassDB::bind_method(D_METHOD("set_editor_preview", "value"), &PathTracingCamera::set_editor_preview);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "editor_preview"), "set_editor_preview", "get_editor_preview");
    ClassDB::bind_method(D_METHOD("get_rebuild_scene_button"), &PathTracingCamera::get_rebuild_scene_button);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "rebuild_scene_button", PROPERTY_HINT_TOOL_BUTTON,
                              "Rebuild Scene,Reload", PROPERTY_USAGE_EDITOR),
                 "", "get_rebuild_scene_button");
    ClassDB::bind_method(D_METHOD("rebuild_scene"), &PathTracingCamera::rebuild_scene);

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
    {
        switch (p_what)
        {
        case NOTIFICATION_READY:
            set_process_internal(true);
            update_editor_preview();
            break;
        case NOTIFICATION_INTERNAL_PROCESS:
            if (editor_preview)
                update_editor_preview(); // follows the editor camera, group changes etc.
            break;
        case NOTIFICATION_EXIT_TREE:
            stop_editor_preview();
            break;
        }
        return;
    }

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

GeometryGroup3D *PathTracingCamera::resolve_group()
{
    if (geometry_group != nullptr)
        return geometry_group;
    if (get_tree() == nullptr)
        return nullptr;
    Node *root = Engine::get_singleton()->is_editor_hint() ? get_tree()->get_edited_scene_root()
                                                           : get_tree()->get_current_scene();
    return root != nullptr ? find_group(root) : nullptr;
}

Ref<PathTracingEffect> PathTracingCamera::create_effect()
{
    Ref<RDShaderFile> shader_file = ResourceLoader::get_singleton()->load(SHADER_PATH);
    if (shader_file.is_null())
    {
        UtilityFunctions::printerr("[PathTracing] could not load ", SHADER_PATH);
        return Ref<PathTracingEffect>();
    }
    // Optional: only used when the GPU supports the ray tracing pipeline.
    Ref<RDShaderFile> hw_shader_file = ResourceLoader::get_singleton()->load(HW_SHADER_PATH);
    Ref<RDShaderFile> resolve_file = ResourceLoader::get_singleton()->load(RESOLVE_SHADER_PATH);
    Ref<RDShaderFile> denoise_file = ResourceLoader::get_singleton()->load(DENOISE_SHADER_PATH);

    Ref<PathTracingEffect> e;
    e.instantiate();
    e->set_shader_files(shader_file, hw_shader_file, resolve_file, denoise_file);
    return e;
}

void PathTracingCamera::setup()
{
    geometry_group = resolve_group();
    if (geometry_group == nullptr)
    {
        UtilityFunctions::printerr("[PathTracing] PathTracingCamera has no GeometryGroup3D.");
        return;
    }
    effect = create_effect();
    if (effect.is_null())
        return;
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

// ----------------------------------- EDITOR PREVIEW -----------------------------------

void PathTracingCamera::update_editor_preview()
{
    if (!editor_preview || !is_inside_tree())
    {
        stop_editor_preview();
        return;
    }
    EditorInterface *editor = EditorInterface::get_singleton();
    GeometryGroup3D *group = resolve_group();
    if (editor == nullptr || group == nullptr)
        return;

    // Follow the group (it may have been assigned or replaced).
    if (preview_group != ObjectID(group->get_instance_id()))
    {
        if (GeometryGroup3D *old = Object::cast_to<GeometryGroup3D>(ObjectDB::get_instance(preview_group)))
            old->end_editor_preview();
        group->begin_editor_preview();
        preview_group = ObjectID(group->get_instance_id());
    }

    if (effect.is_null())
    {
        effect = create_effect();
        if (effect.is_null())
            return;
    }

    // Path trace the main editor 3D viewport through its own camera. The compositor lives only on
    // that internal camera, so nothing is saved into the scene.
    SubViewport *viewport = editor->get_editor_viewport_3d(0);
    Camera3D *camera = viewport != nullptr ? viewport->get_camera_3d() : nullptr;
    if (camera == nullptr)
        return;
    if (preview_camera != ObjectID(camera->get_instance_id()))
    {
        if (Camera3D *old = Object::cast_to<Camera3D>(ObjectDB::get_instance(preview_camera)))
            old->set_compositor(Ref<Compositor>());
        Ref<Compositor> compositor;
        compositor.instantiate();
        TypedArray<CompositorEffect> effects;
        effects.push_back(effect);
        compositor->set_compositor_effects(effects);
        camera->set_compositor(compositor);
        preview_camera = ObjectID(camera->get_instance_id());
    }

    if (!preview_active)
    {
        // Accumulation needs a continuous redraw; the editor normally only redraws on changes.
        saved_low_processor_mode = OS::get_singleton()->is_in_low_processor_usage_mode();
        OS::get_singleton()->set_low_processor_usage_mode(false);
        preview_active = true;
    }

    geometry_group = group;
    push_settings();
}

void PathTracingCamera::stop_editor_preview()
{
    if (Camera3D *camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(preview_camera)))
        camera->set_compositor(Ref<Compositor>());
    preview_camera = ObjectID();
    if (GeometryGroup3D *group = Object::cast_to<GeometryGroup3D>(ObjectDB::get_instance(preview_group)))
        group->end_editor_preview();
    preview_group = ObjectID();
    if (preview_active)
    {
        OS::get_singleton()->set_low_processor_usage_mode(saved_low_processor_mode);
        preview_active = false;
    }
    effect.unref();
}

bool PathTracingCamera::get_editor_preview() const
{
    return editor_preview;
}
void PathTracingCamera::set_editor_preview(bool value)
{
    editor_preview = value;
    if (Engine::get_singleton()->is_editor_hint() && is_inside_tree())
        update_editor_preview();
}

Callable PathTracingCamera::get_rebuild_scene_button()
{
    return callable_mp(this, &PathTracingCamera::rebuild_scene);
}

void PathTracingCamera::rebuild_scene()
{
    if (GeometryGroup3D *group = resolve_group())
        group->build();
    reset_accumulation();
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
bool PathTracingCamera::get_denoise() const
{
    return settings.denoise;
}
void PathTracingCamera::set_denoise(bool value)
{
    settings.denoise = value;
}
int PathTracingCamera::get_denoise_iterations() const
{
    return settings.denoise_iterations;
}
void PathTracingCamera::set_denoise_iterations(int value)
{
    settings.denoise_iterations = CLAMP(value, 1, 6);
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
