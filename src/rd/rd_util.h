#ifndef PT_RD_UTIL_H
#define PT_RD_UTIL_H

// Small helpers around RenderingDevice; replaces the old GDCS dependency.

#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <initializer_list>

namespace PT
{

// A storage or uniform buffer that grows on demand. Contents are replaced on every upload.
class GpuBuffer
{
  public:
    enum Kind
    {
        STORAGE,
        UNIFORM,
    };

    explicit GpuBuffer(Kind kind = STORAGE) : kind(kind)
    {
    }

    // Uploads `data`, recreating the buffer if it is too small. Empty data allocates a small
    // dummy buffer so the binding stays valid. Returns true if the RID changed.
    bool upload(godot::RenderingDevice *rd, const godot::PackedByteArray &data);
    void free(godot::RenderingDevice *rd);

    godot::RID rid() const
    {
        return buffer;
    }

  private:
    Kind kind;
    godot::RID buffer;
    uint64_t capacity = 0;
};

godot::Ref<godot::RDUniform> make_uniform(godot::RenderingDevice::UniformType type, int binding,
                                           std::initializer_list<godot::RID> ids);

template <typename T> godot::PackedByteArray to_bytes(const T *data, size_t count)
{
    godot::PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(count * sizeof(T)));
    if (count > 0)
        memcpy(bytes.ptrw(), data, count * sizeof(T));
    return bytes;
}

template <typename Vec> godot::PackedByteArray vector_to_bytes(const Vec &v)
{
    return to_bytes(v.data(), v.size());
}

} // namespace PT

#endif // PT_RD_UTIL_H
