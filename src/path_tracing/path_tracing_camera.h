#ifndef PATH_TRACING_CAMERA_H
#define PATH_TRACING_CAMERA_H

#include "geometry_group3d.h"
#include "gdcs/include/gdcs.h"
#include "temporal_reprojection.h"
#include "progressive_rendering.h"
#include "render_parameters.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/display_server.hpp>

using namespace godot;

class PathTracingCamera : public Node3D
{
    GDCLASS(PathTracingCamera, Node3D);

  public:
    enum Denoising {
        PROGRESSIVE_RENDERING,
        TEMPORAL_REPROJECTION,
        NONE
    };

    struct RenderParameters // match the struct on the gpu
    {
        Vector4 backgroundColor;
        int width;
        int height;
        float fov;
        unsigned int triangleCount;
        unsigned int blasCount;

        PackedByteArray to_packed_byte_array()
        {
            PackedByteArray byte_array;
            byte_array.resize(sizeof(RenderParameters));
            std::memcpy(byte_array.ptrw(), this, sizeof(RenderParameters));
            return byte_array;
        }
    };

  protected:
    static void _bind_methods();

  public:
    void _notification(int what);

    float get_fov() const;
    void set_fov(float value);

    // int get_num_bounces() const;
    // void set_num_bounces(int value);

    TextureRect *get_output_texture() const;
    void set_output_texture(TextureRect *value);

    GeometryGroup3D *get_geometry_group() const;
    void set_geometry_group(GeometryGroup3D *value);

    Denoising get_denoising_mode() const;
    void set_denoising_mode(Denoising mode);

  private:
    void init();
    void clear_compute_shader();
    void render();

    float fov = 90.0f;
    // int num_bounces = 4;

    ComputeShader *cs = nullptr;
    ProgressiveRendering *progressive_renderer = nullptr;
    TemporalReprojection *temporal_reprojection = nullptr;
    GeometryGroup3D *geometry_group = nullptr;
    TextureRect *output_texture_rect = nullptr;
    Ref<Image> output_image;
    Ref<Image> depth_image;
    Ref<ImageTexture> output_texture;

    RenderParameters render_parameters;
    Camera camera;
    Projection projection_matrix;

    // BUFFER IDs
    RID output_texture_rid;
    RID depth_texture_rid;
    RID render_parameters_rid;
    RID camera_rid;
    RID triangles_geometry_rid;
    RID triangles_data_rid;
    RID materials_rid;
    RID bvh_tree_rid;
    RID blas_rid;
    RID tlas_rid;
    RID texture_array_rid;

    RenderingDevice *_rd;

    Denoising denoising_mode = PROGRESSIVE_RENDERING; // Default option
};

VARIANT_ENUM_CAST(PathTracingCamera::Denoising);

#endif // PATH_TRACING_CAMERA_H