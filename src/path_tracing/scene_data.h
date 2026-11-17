#ifndef PT_SCENE_DATA_H
#define PT_SCENE_DATA_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace PT
{

// CPU side copy of everything the GPU needs. Produced on the main thread by GeometryGroup3D,
// consumed on the render thread by PathTracingEffect. PackedByteArrays are copy-on-write, so
// taking a snapshot under the lock is cheap.
struct SceneSnapshot
{
    // Static geometry: only changes on GeometryGroup3D::build().
    uint64_t geometry_version = 0;
    godot::PackedByteArray bvh_nodes;
    godot::PackedByteArray triangles;
    godot::PackedByteArray triangle_data;
    godot::PackedByteArray materials;
    godot::PackedByteArray instance_materials;
    std::vector<godot::Ref<godot::Image>> textures; // RGBA8, all texture_resolution^2
    int texture_resolution = 1;

    // Dynamic data: changes whenever an instance or light moves / toggles visibility.
    uint64_t instance_version = 0;
    godot::PackedByteArray instances;
    godot::PackedByteArray tlas_nodes;
    godot::PackedByteArray emissive_triangles;
    godot::PackedByteArray punctual_lights;
    uint32_t instance_count = 0; // instances referenced by the TLAS
    uint32_t emissive_count = 0;
    uint32_t punctual_count = 0;
    float emissive_total_power = 0.0f;
};

class SceneData
{
  public:
    SceneSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return data;
    }

    template <typename F> void modify(F &&f)
    {
        std::lock_guard<std::mutex> lock(mutex);
        f(data);
    }

  private:
    std::mutex mutex;
    SceneSnapshot data;
};

using SceneDataPtr = std::shared_ptr<SceneData>;

} // namespace PT

#endif // PT_SCENE_DATA_H
