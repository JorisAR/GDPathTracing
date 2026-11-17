#include "portal3d.h"

#include <godot_cpp/core/object.hpp>

void Portal3D::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_size"), &Portal3D::get_size);
    ClassDB::bind_method(D_METHOD("set_size", "value"), &Portal3D::set_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");

    ClassDB::bind_method(D_METHOD("get_linked_portal"), &Portal3D::get_linked_portal);
    ClassDB::bind_method(D_METHOD("set_linked_portal", "value"), &Portal3D::set_linked_portal);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "linked_portal", PROPERTY_HINT_NODE_TYPE, "Portal3D"),
                 "set_linked_portal", "get_linked_portal");

    ClassDB::bind_method(D_METHOD("get_tint"), &Portal3D::get_tint);
    ClassDB::bind_method(D_METHOD("set_tint", "value"), &Portal3D::set_tint);
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");

    ClassDB::bind_method(D_METHOD("get_teleport_transform"), &Portal3D::get_teleport_transform);
    ClassDB::bind_method(D_METHOD("is_crossing", "from", "to"), &Portal3D::is_crossing);
}

Vector2 Portal3D::get_size() const
{
    return size;
}
void Portal3D::set_size(const Vector2 &value)
{
    size = value.max(Vector2(0.01f, 0.01f));
}
Portal3D *Portal3D::get_linked_portal() const
{
    return Object::cast_to<Portal3D>(ObjectDB::get_instance(linked));
}
void Portal3D::set_linked_portal(Portal3D *value)
{
    linked = value != nullptr ? ObjectID(value->get_instance_id()) : ObjectID();
}
Color Portal3D::get_tint() const
{
    return tint;
}
void Portal3D::set_tint(const Color &value)
{
    tint = value;
}

Transform3D Portal3D::get_teleport_transform() const
{
    Portal3D *other = get_linked_portal();
    if (other == nullptr || !is_inside_tree() || !other->is_inside_tree())
        return Transform3D();
    // Entering this portal's front (moving along -Z) must leave the other's front (moving along +Z),
    // so flip 180 degrees around the local Y axis in between.
    Transform3D flip(Basis(Vector3(0, 1, 0), Math::PI), Vector3());
    return other->get_global_transform() * flip * get_global_transform().affine_inverse();
}

bool Portal3D::is_crossing(const Vector3 &from, const Vector3 &to) const
{
    Transform3D inv = get_global_transform().affine_inverse();
    Vector3 a = inv.xform(from);
    Vector3 b = inv.xform(to);
    if (!(a.z >= 0.0f && b.z < 0.0f))
        return false;
    float t = a.z / (a.z - b.z);
    Vector3 p = a + (b - a) * t;
    return Math::abs(p.x) <= size.x * 0.5f && Math::abs(p.y) <= size.y * 0.5f;
}
