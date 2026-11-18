#ifndef PT_ENVIRONMENT_H
#define PT_ENVIRONMENT_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <memory>

namespace PT
{

// Equirectangular environment map prepared for the GPU: linear RGBA32F texels (at most 2048 wide,
// box filtered down from the source) and the tables for importance sampling it.
//
// alias holds one (float keep probability, uint alias) pair per texel (row major), an alias table
// (alias_table.h) over the texel weights luminance * sin(theta). The solid angle pdf of a texel is
//   luminance * width * height / (integral * 2 * pi^2),
// with `integral` the sum of all weights.
struct EnvironmentData
{
    godot::Ref<godot::Image> image; // FORMAT_RGBAF
    godot::PackedByteArray alias;   // (float, uint) per texel
    int width = 0;
    int height = 0;
    float integral = 0.0f;
    uint64_t version = 0;
};

using EnvironmentPtr = std::shared_ptr<const EnvironmentData>;

// Returns null if the texture has no usable image.
EnvironmentPtr build_environment(const godot::Ref<godot::Texture2D> &texture, uint64_t version);

} // namespace PT

#endif // PT_ENVIRONMENT_H
