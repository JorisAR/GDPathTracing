#ifndef BHV_VEC_H
#define BHV_VEC_H

#include <algorithm>
#include <godot_cpp/variant/string.hpp>
#include <string>

namespace BVH
{

struct alignas(8) vec2
{
    float u, v;

    vec2(float u = 0, float v = 0) : u(u), v(v)
    {
    }

    inline vec2 operator+(const vec2 &other) const
    {
        return vec2(u + other.u, v + other.v);
    }

    inline vec2 operator-(const vec2 &other) const
    {
        return vec2(u - other.u, v - other.v);
    }

    inline vec2 operator*(float scalar) const
    {
        return vec2(u * scalar, v * scalar);
    }

    inline vec2 operator/(float scalar) const
    {
        return vec2(u / scalar, v / scalar);
    }

    inline bool operator==(const vec2 &other) const
    {
        return u == other.u && v == other.v;
    }
};

struct alignas(16) vec4
{
    float x, y, z, w;

    vec4(float x = 0, float y = 0, float z = 0, float w = 1.0f) : x(x), y(y), z(z), w(w)
    {
    }

    inline vec4 operator+(const vec4 &other) const
    {
        return vec4(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    inline vec4 operator-(const vec4 &other) const
    {
        return vec4(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    inline vec4 operator*(float scalar) const
    {
        return vec4(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    inline vec4 operator/(float scalar) const
    {
        return vec4(x / scalar, y / scalar, z / scalar, w / scalar);
    }

    inline bool operator==(const vec4 &other) const
    {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    inline vec4 max(const vec4 &other) const
    {
        return vec4(std::max(x, other.x), std::max(y, other.y), std::max(z, other.z), std::max(w, other.w));
    }

    inline vec4 min(const vec4 &other) const
    {
        return vec4(std::min(x, other.x), std::min(y, other.y), std::min(z, other.z), std::min(w, other.w));
    }

    inline godot::String toString() const
    {
        return "{" + godot::String(std::to_string(x).c_str()) + ", " + godot::String(std::to_string(y).c_str()) + ", " +
               godot::String(std::to_string(z).c_str()) + ", " + godot::String(std::to_string(w).c_str()) + "}";
    }

    inline float &operator[](int index)
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        case 2:
            return z;
        case 3:
            return w;
        default:
            return x;
        }
    }

    inline const float &operator[](int index) const
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        case 2:
            return z;
        case 3:
            return w;
        default:
            return x;
        }
    }
};

struct vec3
{
    float x, y, z;

    vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z)
    {
    }
    vec3(vec4 vec) : x(vec.x), y(vec.y), z(vec.z)
    {
    }

    inline vec3 operator+(const vec3 &other) const
    {
        return vec3(x + other.x, y + other.y, z + other.z);
    }

    inline vec3 operator-(const vec3 &other) const
    {
        return vec3(x - other.x, y - other.y, z - other.z);
    }

    inline vec3 operator*(float scalar) const
    {
        return vec3(x * scalar, y * scalar, z * scalar);
    }

    inline vec3 operator/(float scalar) const
    {
        return vec3(x / scalar, y / scalar, z / scalar);
    }

    inline bool operator==(const vec3 &other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }

    inline bool operator<(const vec3 &other) const
    {
        return (x < other.x) || (x == other.x && y < other.y) || (x == other.x && y == other.y && z < other.z);
    }

    inline vec3 max(const vec3 &other) const
    {
        return vec3(std::max(x, other.x), std::max(y, other.y), std::max(z, other.z));
    }

    inline vec3 min(const vec3 &other) const
    {
        return vec3(std::min(x, other.x), std::min(y, other.y), std::min(z, other.z));
    }

    inline godot::String toString() const
    {
        return "{" + godot::String(std::to_string(x).c_str()) + ", " + godot::String(std::to_string(y).c_str()) + ", " +
               godot::String(std::to_string(z).c_str()) + "}";
    }

    inline float &operator[](int index)
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        case 2:
            return z;
        default:
            return x;
        }
    }

    inline const float &operator[](int index) const
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        case 2:
            return z;
        default:
            return x;
        }
    }
};

} // namespace BVH

#endif // BHV_VEC_H
