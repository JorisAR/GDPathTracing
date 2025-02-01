#include "jarcs.h"
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

ComputeShader::ComputeShader(const String &shader_path)
{
    _rd = RenderingServer::get_singleton()->create_local_rendering_device();
    if (!_rd)
    {
        UtilityFunctions::printerr("Failed to create rendering device.");
        return;
    }

    Ref<RDShaderFile> shader_file = ResourceLoader::get_singleton()->load(shader_path);
    if (shader_file.is_null())
    {
        UtilityFunctions::printerr("Failed to load shader file.");
        return;
    }

    Ref<RDShaderSPIRV> spirv = shader_file->get_spirv();
    if (spirv.is_null())
    {
        UtilityFunctions::printerr("Failed to get SPIR-V from shader file.");
        return;
    }

    _shader = _rd->shader_create_from_spirv(spirv);
    if (!_shader.is_valid())
    {
        UtilityFunctions::printerr("Failed to create shader from SPIR-V.");
        return;
    }
    _pipeline = _rd->compute_pipeline_create(_shader);
    if (!_pipeline.is_valid())
    {
        UtilityFunctions::printerr("Failed to create compute pipeline.");
        return;
    }

#ifdef GDCS_VERBOSE
    UtilityFunctions::print("loaded shader successfully!");
#endif
      
    _initialized = true;
}

ComputeShader::~ComputeShader()
{
    _rd->free_rid(_shader);
    _rd->free_rid(_pipeline);
    for (auto &rid : _buffers)
    {
        _rd->free_rid(rid);
    }

    delete _rd;
}

//------------------------------------------------ STORAGE BUFFER ------------------------------------------------

RID ComputeShader::create_storage_buffer_uniform(const PackedByteArray &data, const int binding, const int set)
{
    //todo check if binding already exists, then return and print error.
    RID rid = _rd->storage_buffer_create(data.size(), data);
    _buffers.push_back(rid);
    Ref<RDUniform> uniform = memnew(RDUniform);
    uniform->set_binding(binding);
    uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    uniform->add_id(rid);

    // set binding
    _bindings[set].push_back(uniform);
    _uniforms_ready = false;
    return rid;
}

void ComputeShader::update_storage_buffer_uniform(const RID rid, const PackedByteArray &data)
{
    _rd->buffer_update(rid, 0, data.size(), data);
}

PackedByteArray ComputeShader::get_storage_buffer_uniform(RID rid) const
{
    return _rd->buffer_get_data(rid);
}

// template <typename T> 
// PackedByteArray ComputeShader::struct_to_packed_byte_array(const T &obj)
// {
//     static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

//     PackedByteArray byte_array;
//     byte_array.resize(sizeof(T));

//     std::memcpy(byte_array.ptrw(), &obj, sizeof(T));

//     return byte_array;
// }

//------------------------------------------------ TEXTURE 2D ------------------------------------------------

Ref<RDTextureFormat> ComputeShader::create_texture_format(const int width, const int height,
                                                     const RenderingDevice::DataFormat format)
{
    Ref<RDTextureFormat> result;
    result.instantiate();
    result->set_width(width);
    result->set_height(height);
    result->set_format(format);    
    result->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
    return result;
}

RID ComputeShader::create_image_uniform(const Ref<Image> &image, Ref<RDTextureFormat> &format, const int binding,
                                            const int set)
{
    Ref<RDTextureView> view = memnew(RDTextureView);
    TypedArray<PackedByteArray> data = {};
    data.push_back(image->get_data());

    RID rid = _rd->texture_create(format, view, data);

    Ref<RDUniform> uniform = memnew(RDUniform);
    uniform->set_binding(binding);
    uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    uniform->add_id(rid);

    // set binding
    _bindings[set].push_back(uniform);
    _uniforms_ready = false;

    return rid;
}

PackedByteArray ComputeShader::get_image_uniform_buffer(RID rid, const int layer) const
{    
    return _rd->texture_get_data(rid, layer);
}

void ComputeShader::finish_create_uniforms()
{
    if(_uniforms_ready) return;
    for (const auto &pair : _bindings)
    {
        auto set = _rd->uniform_set_create(pair.second, _shader, pair.first);
        _sets[pair.first] = set;
    }
    _uniforms_ready = true;
}

void ComputeShader::compute(const Vector3i groups)
{
    if(!check_ready()) return;
    auto list = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(list, _pipeline);
    for (const auto &pair : _sets)
    {
        _rd->compute_list_bind_uniform_set(list, pair.second, pair.first);
    }
    _rd->compute_list_dispatch(list, groups.x, groups.y, groups.z);
    _rd->compute_list_end();
    _rd->submit();
    _rd->sync();
}

RenderingDevice *ComputeShader::get_rendering_device() const
{
    return _rd;
}

bool ComputeShader::check_ready() const {
    if(!_rd) return false;
    if(!_initialized) {
        UtilityFunctions::printerr("Compute shader not properly initialized, fix previous errors.");
        return false;
    }
    if(!_uniforms_ready) {
        UtilityFunctions::printerr("Make sure to call finish_create_buffers once after creating all buffers");
        return false;
    }
    return _initialized && _uniforms_ready;
}