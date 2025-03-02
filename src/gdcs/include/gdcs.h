#ifndef GDCS_H
#define GDCS_H

// comment out if you dont want it to be verbose!
#define GDCS_VERBOSE

#include <cstring>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/texture3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <type_traits>

using namespace godot;

class ComputeShader
{
  public:
    ComputeShader(const String &shader_path, RenderingDevice *rd = nullptr, const std::vector<String> args = {});
    ~ComputeShader();

    // storage buffers
    RID create_storage_buffer_uniform(const PackedByteArray &buffer, const int binding, const int set = 0);
    void update_storage_buffer_uniform(const RID rid, const PackedByteArray &data);
    PackedByteArray get_storage_buffer_uniform(RID rid) const;
    // template <typename T>
    // PackedByteArray struct_to_packed_byte_array(const T& obj);

    // 2d textures
    Ref<RDTextureFormat> create_texture_format(const int width, const int height,
                                               const RenderingDevice::DataFormat format);
    RID create_image_uniform(const Ref<Image> &image, const Ref<RDTextureFormat> &format,
                             const Ref<RDTextureView> &view, const int binding, const int set = 0);

    PackedByteArray get_image_uniform_buffer(RID rid, const int layer = 0) const;

    // 2d layered textures
    RID create_layered_image_uniform(const std::vector<Ref<Image>> &image, const Ref<RDTextureFormat> &format,
      const Ref<RDTextureView> &view, const int binding, const int set = 0);

    // 3d textures
    // todo

    // general
    void add_existing_buffer(const RID rid, const RenderingDevice::UniformType uniform_type, const int binding,
                             const int set = 0);

    void finish_create_uniforms();

    bool check_ready() const;
    Ref<RDShaderSource> LoadShaderFile(const String &shader_path, const std::vector<String> &args = {});
    void compute(const Vector3i groups);

    RenderingDevice *get_rendering_device() const;

  private:
    String LoadShaderString(const String &shader_path);
    RenderingDevice *_rd;
    RID _shader;
    RID _pipeline;
    std::vector<RID> _buffers;
    std::unordered_map<unsigned int, TypedArray<RDUniform>> _bindings;
    std::unordered_map<unsigned int, RID> _sets;

    bool _initialized = false;
    bool _uniforms_ready = false;
};

#endif // GDCS_H
