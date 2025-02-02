#include "jarcs.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <regex>

ComputeShader::ComputeShader(const String &shader_path, RenderingDevice *rd, const std::vector<String> args)
{
    if (rd == nullptr)
        _rd = RenderingServer::get_singleton()->create_local_rendering_device();
    else
        _rd = rd;

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

    // If arguments are included, use the custom shaderfile loader that injects them into the shaderfile,
    // useful to add the following for instance: #define YOUR_CONSTANT_HERE
    if (args.size() > 0)
    {
        Ref<RDShaderSource> source = LoadShaderFile(shader_path, args);
        if (source.is_valid())
        {
            // UtilityFunctions::printerr(source->get_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE));
            spirv = _rd->shader_compile_spirv_from_source(source, false);
        }
    }

    // Ref<RDShaderSPIRV> spirv = shader_file->get_spirv();
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
        if (rid.is_valid())
            _rd->free_rid(rid);
    }

    delete _rd;
}

//------------------------------------------------ STORAGE BUFFER ------------------------------------------------

RID ComputeShader::create_storage_buffer_uniform(const PackedByteArray &data, const int binding, const int set)
{
    // todo check if binding already exists, then return and print error.
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
    result->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
                           RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
    return result;
}

RID ComputeShader::create_image_uniform(const Ref<Image> &image, const Ref<RDTextureFormat> &format,
                                        const Ref<RDTextureView> &view, const int binding, const int set)
{
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

RID ComputeShader::create_shared_image_uniform(RID original_rid, const Ref<RDTextureView> &view, const int binding,
                                               const int set)
{
    RID rid = _rd->texture_create_shared(view, original_rid);
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

void ComputeShader::add_existing_buffer(const RID rid, const RenderingDevice::UniformType uniform_type,
                                        const int binding, const int set)
{
    // todo check if binding already exists, then return and print error.

    // _buffers.push_back(rid);
    Ref<RDUniform> uniform = memnew(RDUniform);
    uniform->set_binding(binding);
    uniform->set_uniform_type(uniform_type);
    uniform->add_id(rid);

    // set binding
    _bindings[set].push_back(uniform);
    _uniforms_ready = false;
}

void ComputeShader::finish_create_uniforms()
{
    if (_uniforms_ready)
        return;
    for (const auto &pair : _bindings)
    {
        auto set = _rd->uniform_set_create(pair.second, _shader, pair.first);
        _sets[pair.first] = set;
    }
    _uniforms_ready = true;
}

void ComputeShader::compute(const Vector3i groups)
{
    if (!check_ready())
        return;
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

bool ComputeShader::check_ready() const
{
    if (!_rd)
        return false;
    if (!_initialized)
    {
        UtilityFunctions::printerr("Compute shader not properly initialized, fix previous errors.");
        return false;
    }
    if (!_uniforms_ready)
    {
        UtilityFunctions::printerr("Make sure to call finish_create_buffers once after creating all buffers");
        return false;
    }
    return _initialized && _uniforms_ready;
}

// ----------------------------- LOADING -----------------------------------------

String ComputeShader::LoadShaderString(const String &shader_path)
{
    Ref<FileAccess> file = FileAccess::open(shader_path, FileAccess::READ);
    if (file.is_null())
    {
        UtilityFunctions::printerr("Cannot read shader: " + shader_path);
        return "";
    }

    // Read the shader code
    String file_txt = file->get_as_text();
    if (file_txt.find("#[compute]") >= 0)
        file_txt = file_txt.erase(0, 10); // Remove header

    // Regular expression to find #include statements
    const String INCLUDE = "#include";
    const String SPACE = " ";

    int include_pos = file_txt.find(INCLUDE);
    while (include_pos >= 0)
    {
        int start_pos = include_pos + String(INCLUDE).length();
        int end_pos = file_txt.find("\n", start_pos);
        // ensure it doesnt find "//" before "\n" when looking towards the left
        String line_before_include = file_txt.substr(0, include_pos);
        int last_newline_pos = line_before_include.rfind("\n");
        int comment_pos = line_before_include.rfind("//");
        if (comment_pos >= 0 && last_newline_pos >= 0 && comment_pos > last_newline_pos)
        {
            // This line is commented out, skip to the next #include
            include_pos = file_txt.find("#include", end_pos);
            continue;
        }

        String include_line = file_txt.substr(start_pos, end_pos - start_pos).strip_edges();
        String include_path = include_line.replace("\"", "").strip_edges();
        include_path = shader_path.get_base_dir() + "/" + include_path;
        String include_code = LoadShaderString(include_path);
        file_txt = file_txt.replace(INCLUDE + SPACE + include_line, include_code);
        include_pos = file_txt.find(INCLUDE);
    }

    return file_txt;
}

//preprocesses shader source.
Ref<RDShaderSource> ComputeShader::LoadShaderFile(const String &shader_path, const std::vector<String> &args)
{
    String file_txt = LoadShaderString(shader_path);

    // Insert args at the start of the file_txt on separate lines
    const String NL = "\n";
    String a = NL;
    for (const String &arg : args)
    {
        a += arg + NL;
    }
    // ensure the #version flag exists before any arguments.
    int version_pos = file_txt.find("#version");
    if (version_pos >= 0)
    {
        int end_pos = file_txt.find("\n", version_pos);
        file_txt = file_txt.insert(end_pos, a);
    }
    else
    {
        file_txt = file_txt.insert(0, a);
    }

    Ref<RDShaderSource> shader_file;
    shader_file.instantiate();

    // Parse shader versions from text
    shader_file->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, file_txt);
    return shader_file;
}
