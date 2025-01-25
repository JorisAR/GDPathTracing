#ifndef GDCS_H
#define GDCS_H

//comment out if you dont want it to be verbose!
#define GDCS_VERBOSE

#include <godot_cpp/classes/texture3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <type_traits>
#include <cstring>

using namespace godot;

class ComputeShader {
public:
    ComputeShader(const String& shader_path);
    ~ComputeShader();

    //storage buffers
    RID create_storage_buffer_uniform(const PackedByteArray& buffer, const int binding, const int set = 0);
    void update_storage_buffer_uniform(const RID rid, const PackedByteArray& data);
    PackedByteArray get_storage_buffer_uniform(RID rid) const;
    // template <typename T>
    // PackedByteArray struct_to_packed_byte_array(const T& obj);

    
    //2d textures
    Ref<RDTextureFormat> create_texture_format(const int width, const int height, const RenderingDevice::DataFormat format);
    RID create_image_uniform(const Ref<Image>& image, Ref<RDTextureFormat>& format, const int binding, const int set = 0);
    PackedByteArray get_image_uniform_buffer(RID rid, const int layer = 0) const;
    // RID create_texture2d_uniform(const Ref<ImageTexture>& image, Ref<RDTextureFormat>& format, const int binding, const int set = 0);
    // RID create_texture2d_sampler_uniform(const Ref<Image>& image, Ref<RDTextureFormat>& format, const int binding, const int set = 0);


    //3d textures
    //todo

    void finish_create_uniforms();

    bool check_ready() const;
    void compute(const Vector3i groups);

    RenderingDevice* get_rendering_device() const;

private:
    RenderingDevice* _rd;
    RID _shader;
    RID _pipeline;
    std::vector<RID> _buffers;
    std::unordered_map<unsigned int, TypedArray<RDUniform>> _bindings;
    std::unordered_map<unsigned int, RID> _sets;

    bool _initialized = false;
    bool _uniforms_ready = false;
};

#endif // GDCS_H
