#ifndef PROGRESSIVE_RENDERING_H
#define PROGRESSIVE_RENDERING_H

#include "gdcs/include/gdcs.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/transform3d.hpp>

using namespace godot;

class ProgressiveRendering
{

    struct RenderParameters // match the struct on the gpu
    {
        int width;
        int height;
        unsigned int frame_count;

        PackedByteArray to_packed_byte_array()
        {
            PackedByteArray byte_array;
            byte_array.resize(sizeof(RenderParameters));
            std::memcpy(byte_array.ptrw(), this, sizeof(RenderParameters));
            return byte_array;
        }
    };

  public:
    ProgressiveRendering();
    ~ProgressiveRendering();

    void init(RenderingDevice *rd, const RID original_screen_texture_rid, const Ref<RDTextureView> screen_texture_view, const Vector2i size);

    void render(Transform3D camera_transform);

  private:
    ComputeShader *cs = nullptr;
    Ref<Image> frame_buffer_image;
    Ref<ImageTexture> frame_buffer_texture;

    RenderParameters render_parameters;

    Transform3D previous_transform;

    // BUFFER IDs
    RID render_parameters_rid;
    RID screen_texture_rid;
    RID frame_buffer_rid;
};

#endif // PATH_TRACING_CAMERA_H