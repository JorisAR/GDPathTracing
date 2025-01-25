#ifndef BHV_VEC_H
#define BHV_VEC_H

namespace BVH {

struct alignas(8) vec2 {
    float u, v;

    vec2(float u = 0, float v = 0) : u(u), v(v) {}

    inline vec2 operator+(const vec2& other) const {
        return vec2(u + other.u, v + other.v);
    }

    inline vec2 operator-(const vec2& other) const {
        return vec2(u - other.u, v - other.v);
    }

    inline vec2 operator*(float scalar) const {
        return vec2(u * scalar, v * scalar);
    }

    inline vec2 operator/(float scalar) const {
        return vec2(u / scalar, v / scalar);
    }

    inline bool operator==(const vec2& other) const {
        return u == other.u && v == other.v;
    }
};

struct alignas(16) vec3 {
    float x, y, z;

    vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

    inline vec3 operator+(const vec3& other) const {
        return vec3(x + other.x, y + other.y, z + other.z);
    }

    inline vec3 operator-(const vec3& other) const {
        return vec3(x - other.x, y - other.y, z - other.z);
    }

    inline vec3 operator*(float scalar) const {
        return vec3(x * scalar, y * scalar, z * scalar);
    }

    inline vec3 operator/(float scalar) const {
        return vec3(x / scalar, y / scalar, z / scalar);
    }

    inline bool operator==(const vec3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    inline bool operator<(const vec3& other) const {
        return (x < other.x) || (x == other.x && y < other.y) || (x == other.x && y == other.y && z < other.z);
    }
};

struct alignas(16) vec4 {
    float x, y, z, w;

    vec4(float x = 0, float y = 0, float z = 0, float w = 1.0f) : x(x), y(y), z(z), w(w) {}

    inline vec4 operator+(const vec4& other) const {
        return vec4(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    inline vec4 operator-(const vec4& other) const {
        return vec4(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    inline vec4 operator*(float scalar) const {
        return vec4(x * scalar, y * scalar, z * scalar, w * scalar);
    }

    inline vec4 operator/(float scalar) const {
        return vec4(x / scalar, y / scalar, z / scalar, w / scalar);
    }

    inline bool operator==(const vec4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    inline float& operator[](int index) {
        switch (index) {
            case 0: return x;
            case 1: return y;
            case 2: return z;
            case 3: return w;
            default: return x;
        }
    }

    inline const float& operator[](int index) const {
        switch (index) {
            case 0: return x;
            case 1: return y;
            case 2: return z;
            case 3: return w;
            default: return x;
        }
    }
};

} // namespace BVH

#endif // BHV_VEC_H
