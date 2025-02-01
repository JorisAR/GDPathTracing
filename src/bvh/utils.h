#ifndef BHV_UTILS_H
#define BHV_UTILS_H

#include <algorithm>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

using namespace godot;

namespace BVH
{
inline void transform_to_float(float* target, const Transform3D &t)
{
    // Set the transform matrix
    target[0] = t.basis.get_column(0).x;
    target[1] = t.basis.get_column(0).y;
    target[2] = t.basis.get_column(0).z;
    target[3] = 0.0f;

    target[4] = t.basis.get_column(1).x;
    target[5] = t.basis.get_column(1).y;
    target[6] = t.basis.get_column(1).z;
    target[7] = 0.0f;

    target[8] = t.basis.get_column(2).x;
    target[9] = t.basis.get_column(2).y;
    target[10] = t.basis.get_column(2).z;
    target[11] = 0.0f;

    target[12] = t.origin.x;
    target[13] = t.origin.y;
    target[14] = t.origin.z;
    target[15] = 1.0f;
}

inline void projection_to_float(float* target, const Projection &t)
{
    // Set the transform matrix
    for (size_t i = 0; i < 4; i++)
    {
        target[i * 4] = t.columns[i].x;
        target[i * 4 + 1] = t.columns[i].y;
        target[i * 4 + 2] = t.columns[i].z;
        target[i * 4 + 3] = t.columns[i].w;
    }
}

} // namespace BVH

#endif // BHV_UTILS_H
