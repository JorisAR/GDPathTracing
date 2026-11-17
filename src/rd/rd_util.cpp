#include "rd_util.h"

using namespace godot;

namespace PT
{

bool GpuBuffer::upload(RenderingDevice *rd, const PackedByteArray &data)
{
    uint64_t size = static_cast<uint64_t>(data.size());
    uint64_t needed = size < 16 ? 16 : (size + 15) & ~uint64_t(15);
    bool recreated = false;

    if (!buffer.is_valid() || needed > capacity)
    {
        free(rd);
        // Grow with some headroom so per-frame size changes do not reallocate every time.
        capacity = needed + needed / 4;
        capacity = (capacity + 15) & ~uint64_t(15);
        PackedByteArray init;
        init.resize(static_cast<int64_t>(capacity));
        init.fill(0);
        if (kind == UNIFORM)
            buffer = rd->uniform_buffer_create(static_cast<uint32_t>(capacity), init);
        else
            buffer = rd->storage_buffer_create(static_cast<uint32_t>(capacity), init);
        recreated = true;
    }

    if (size > 0)
        rd->buffer_update(buffer, 0, static_cast<uint32_t>(size), data);
    return recreated;
}

void GpuBuffer::free(RenderingDevice *rd)
{
    if (buffer.is_valid() && rd != nullptr)
        rd->free_rid(buffer);
    buffer = RID();
    capacity = 0;
}

Ref<RDUniform> make_uniform(RenderingDevice::UniformType type, int binding, std::initializer_list<RID> ids)
{
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(type);
    u->set_binding(binding);
    for (const RID &id : ids)
        u->add_id(id);
    return u;
}

} // namespace PT
