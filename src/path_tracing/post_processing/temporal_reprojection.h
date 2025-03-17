#ifndef TEMPORAL_REPROJECTION_H
#define TEMPORAL_REPROJECTION_H

#include "gdcs/include/gdcs.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/projection.hpp>

using namespace godot;

class TemporalReprojection
{

    struct RenderParameters // match the struct on the gpu
    {
        float deltaMatrix[16];
        int width;
        int height;
        unsigned int frame_count;
        float blendFactor = 0.75f;
        float nearPlane = 0.01f;
        float farPlane = 1000.0f;

        PackedByteArray to_packed_byte_array()
        {
            PackedByteArray byte_array;
            byte_array.resize(sizeof(RenderParameters));
            std::memcpy(byte_array.ptrw(), this, sizeof(RenderParameters));
            return byte_array;
        }
    };

  public:
    TemporalReprojection();
    ~TemporalReprojection();

    void init(RenderingDevice *rd, const RID original_screen_texture_rid, const RID original_depth_texture_rid,
              const Vector2i size);

    void render(Transform3D view_matrix, Projection projection_matrix);

  private:
    ComputeShader *cs = nullptr;
    Ref<Image> frame_buffer_image_1;
    Ref<ImageTexture> frame_buffer_texture_1;
    Ref<Image> frame_buffer_image_2;
    Ref<ImageTexture> frame_buffer_texture_2;

    RenderParameters render_parameters;

    Projection previous_vp;

    // BUFFER IDs
    RID render_parameters_rid;
    RID screen_texture_rid;
    RID depth_texture_rid;
    RID frame_buffer_rid_1;
    RID frame_buffer_rid_2;
};

#endif // TEMPORAL_REPROJECTION_H