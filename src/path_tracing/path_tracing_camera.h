#ifndef PATH_TRACING_CAMERA_H
#define PATH_TRACING_CAMERA_H

#include "path_tracing/geometry_group3d.h"
#include "path_tracing/path_tracing_effect.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// A Camera3D that renders its view with the path tracer. At runtime it attaches a
// PathTracingEffect to its Compositor (creating one if needed) and feeds it scene data from the
// assigned GeometryGroup3D. In the editor, `editor_preview` path traces the editor's 3D viewport
// with this camera's settings.
class PathTracingCamera : public Camera3D
{
    GDCLASS(PathTracingCamera, Camera3D);

  public:
    enum Backend
    {
        BACKEND_AUTO = 0,
        BACKEND_SOFTWARE = 1,
        BACKEND_HARDWARE = 2,
    };

    enum DebugView
    {
        DEBUG_NONE = 0,
        DEBUG_NORMALS = 1,
        DEBUG_ALBEDO = 2,
        DEBUG_TRAVERSAL = 3,
    };

  protected:
    static void _bind_methods();
    void _notification(int p_what);

  public:
    GeometryGroup3D *get_geometry_group() const;
    void set_geometry_group(GeometryGroup3D *value);

    bool get_path_tracing_enabled() const;
    void set_path_tracing_enabled(bool value);

    int get_max_bounces() const;
    void set_max_bounces(int value);
    int get_samples_per_pixel() const;
    void set_samples_per_pixel(int value);
    bool get_accumulate() const;
    void set_accumulate(bool value);
    int get_max_samples() const;
    void set_max_samples(int value);
    float get_firefly_clamp() const;
    void set_firefly_clamp(float value);
    DebugView get_debug_view() const;
    void set_debug_view(DebugView value);

    Color get_sky_top_color() const;
    void set_sky_top_color(const Color &value);
    Color get_sky_horizon_color() const;
    void set_sky_horizon_color(const Color &value);
    Color get_ground_color() const;
    void set_ground_color(const Color &value);
    float get_sky_energy() const;
    void set_sky_energy(float value);

    Backend get_backend() const;
    void set_backend(Backend value);
    bool get_temporal_reprojection() const;
    void set_temporal_reprojection(bool value);
    int get_temporal_max_history() const;
    void set_temporal_max_history(int value);
    int get_max_portal_hops() const;
    void set_max_portal_hops(int value);
    bool get_denoise() const;
    void set_denoise(bool value);
    int get_denoise_iterations() const;
    void set_denoise_iterations(int value);
    String get_active_backend() const;

    int get_sample_count() const;
    void reset_accumulation();

    bool get_editor_preview() const;
    void set_editor_preview(bool value);
    Callable get_rebuild_scene_button();
    void rebuild_scene();

  private:
    void setup();
    void push_settings();
    GeometryGroup3D *resolve_group();
    Ref<PathTracingEffect> create_effect();
    void update_editor_preview();
    void stop_editor_preview();

    GeometryGroup3D *geometry_group = nullptr;
    bool path_tracing_enabled = true;
    PT::RenderSettings settings;
    Ref<PathTracingEffect> effect;

    // Editor preview state.
    bool editor_preview = false;
    bool preview_active = false;
    ObjectID preview_camera;          // the editor viewport camera we attached to
    ObjectID preview_group;           // the group we called begin_editor_preview() on
    bool saved_low_processor_mode = true;
};

VARIANT_ENUM_CAST(PathTracingCamera::DebugView);
VARIANT_ENUM_CAST(PathTracingCamera::Backend);

#endif // PATH_TRACING_CAMERA_H
