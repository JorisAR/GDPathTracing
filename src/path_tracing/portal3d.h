#ifndef PORTAL3D_H
#define PORTAL3D_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// A rectangular portal in the node's local XY plane, |x| <= size.x / 2, |y| <= size.y / 2.
// Rays that enter its front side (looking along -Z, i.e. coming from +Z) continue out of the
// front side of `linked_portal`. Place it below a GeometryGroup3D so the path tracer picks it up.
// The back side is see-through; put a wall or frame behind it.
class Portal3D : public Node3D
{
    GDCLASS(Portal3D, Node3D);

  protected:
    static void _bind_methods();

  public:
    Vector2 get_size() const;
    void set_size(const Vector2 &value);

    Portal3D *get_linked_portal() const;
    void set_linked_portal(Portal3D *value);

    Color get_tint() const;
    void set_tint(const Color &value);

    // World -> world transform that maps the space in front of this portal to the space in front
    // of the linked portal. Identity if unlinked.
    Transform3D get_teleport_transform() const;

    // True if the segment from -> to enters this portal through its front face.
    bool is_crossing(const Vector3 &from, const Vector3 &to) const;

  private:
    Vector2 size = Vector2(2.0f, 3.0f);
    ObjectID linked;
    Color tint = Color(1, 1, 1);
};

#endif // PORTAL3D_H
