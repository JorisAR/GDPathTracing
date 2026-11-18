#include "environment.h"
#include "alias_table.h"

#include <godot_cpp/core/math.hpp>

#include <cmath>
#include <vector>

using namespace godot;

namespace PT
{

static float luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

EnvironmentPtr build_environment(const Ref<Texture2D> &texture, uint64_t version)
{
    if (texture.is_null())
        return nullptr;
    Ref<Image> img = texture->get_image();
    if (img.is_null() || img->is_empty())
        return nullptr;
    img = img->duplicate();
    if (img->is_compressed())
        img->decompress();
    img->clear_mipmaps();
    img->convert(Image::FORMAT_RGBAF);

    int w = img->get_width();
    int h = img->get_height();
    PackedByteArray bytes = img->get_data();
    std::vector<float> px(static_cast<size_t>(w) * h * 4);
    memcpy(px.data(), bytes.ptr(), px.size() * sizeof(float));

    // Box filter by halves: keeps the energy of small bright features (the sun) that a bilinear
    // resize would skip.
    while (w > 2048 && w >= 2 && h >= 2)
    {
        const int nw = w / 2, nh = h / 2;
        std::vector<float> down(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++)
                {
                    float sum = 0.0f;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                            sum += px[((static_cast<size_t>(2 * y + dy) * w) + (2 * x + dx)) * 4 + c];
                    down[(static_cast<size_t>(y) * nw + x) * 4 + c] = sum * 0.25f;
                }
        px.swap(down);
        w = nw;
        h = nh;
    }

    auto env = std::make_shared<EnvironmentData>();
    env->width = w;
    env->height = h;
    env->version = version;

    // Alias table over all texels, weights luminance * sin(theta).
    std::vector<double> weights(static_cast<size_t>(w) * h);
    double total = 0.0;
    for (int y = 0; y < h; y++)
    {
        const double sin_theta = std::sin(3.14159265358979 * (y + 0.5) / h);
        for (int x = 0; x < w; x++)
        {
            const float *p = &px[(static_cast<size_t>(y) * w + x) * 4];
            float l = luminance(p[0], p[1], p[2]);
            if (!(l > 0.0f) || !std::isfinite(l))
                l = 0.0f;
            const double weight = static_cast<double>(l) * sin_theta;
            weights[static_cast<size_t>(y) * w + x] = weight;
            total += weight;
        }
    }
    env->integral = static_cast<float>(total);

    std::vector<float> prob;
    std::vector<uint32_t> alias;
    build_alias_table(weights, prob, alias);
    PackedByteArray table_bytes;
    table_bytes.resize(static_cast<int64_t>(weights.size() * 8));
    uint8_t *dst = table_bytes.ptrw();
    for (size_t i = 0; i < weights.size(); i++)
    {
        memcpy(dst + i * 8, &prob[i], 4);
        memcpy(dst + i * 8 + 4, &alias[i], 4);
    }
    env->alias = table_bytes;

    PackedByteArray pixel_bytes;
    pixel_bytes.resize(static_cast<int64_t>(px.size() * sizeof(float)));
    memcpy(pixel_bytes.ptrw(), px.data(), px.size() * sizeof(float));
    env->image = Image::create_from_data(w, h, false, Image::FORMAT_RGBAF, pixel_bytes);
    return env;
}

} // namespace PT
