#include "bvh.h"

namespace BVH
{

BoundingBox::BoundingBox()
{
    min = vec4(std::numeric_limits<float>::max());
    max = vec4(std::numeric_limits<float>::min());
}

void BoundingBox::extend(const vec4 &point)
{
    min = vec4(std::min(min.x, point.x), std::min(min.y, point.y), std::min(min.z, point.z));
    max = vec4(std::max(max.x, point.x), std::max(max.y, point.y), std::max(max.z, point.z));
}

bool BoundingBox::intersect(const BoundingBox &other) const
{
    return !(min.x > other.max.x || max.x < other.min.x || min.y > other.max.y || max.y < other.min.y ||
             min.z > other.max.z || max.z < other.min.z);
}

BoundingBox BVHBuilder::compute_bounding_box(const std::vector<Triangle> &triangles, int start, int end)
{
    BoundingBox bbox;
    for (int i = start; i < end; i++)
    {
        const Triangle &tri = triangles[i];
        for (int j = 0; j < 3; j++)
        {
            bbox.extend(tri.vertices[j]);
        }
    }
    return bbox;
}

unsigned int BVHBuilder::build_recursive(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles, int start,
                                         int end)
{
    if (start >= end)
        return 0;

    int node_index = nodes.size();
    nodes.push_back(BVHNode());
    BVHNode &node = nodes[node_index];

    BoundingBox bbox = compute_bounding_box(triangles, start, end);
    node.aabbMin = bbox.min;
    node.aabbMax = bbox.max;
    node.left_child = 0;
    node.right_child = 0;
    node.first_tri_index = start;
    node.tri_count = end - start;
    if (node.tri_count == 1) // leaf
    {
        return node_index;
    }
    // Choose the axis with the largest extent
    vec4 extent = bbox.max - bbox.min;
    int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
    float splitPos = 0.5f * (bbox.min[axis] + bbox.max[axis]);

    // Partition the triangles around the split position
    int i = start;
    int j = end - 1;
    while (i <= j)
    {
        vec4 centroid = (triangles[i].vertices[0] + triangles[i].vertices[1] + triangles[i].vertices[2]) / 3.0;
        if (centroid[axis] < splitPos)
            i++;
        else
            std::swap(triangles[i], triangles[j--]);
    }

    // Ensure the partitioning isn't degenerate
    int left_count = i - start;
    if (left_count == 0 || left_count == node.tri_count) return node_index;

    // UtilityFunctions::print(node_index);
    nodes[node_index].left_child = build_recursive(nodes, triangles, start, i);
    nodes[node_index].right_child = build_recursive(nodes, triangles, i, end);
    nodes[node_index].tri_count = 0;

    return node_index;
}

unsigned int BVHBuilder::BuildBVH(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles, const Ref<ArrayMesh> &arrayMesh)
{
    int start = triangles.size();

    for (int l = 0; l < arrayMesh->get_surface_count(); l++)
    {
        auto array = arrayMesh->surface_get_arrays(l);
        PackedInt32Array indices = array[Mesh::ARRAY_INDEX]; // we might not always have an index array
        PackedVector3Array vertices = array[Mesh::ARRAY_VERTEX];
        PackedVector3Array normals = array[Mesh::ARRAY_NORMAL];
        PackedVector2Array uvs = array[Mesh::ARRAY_TEX_UV];
        for (int i = 0; i < indices.size(); i += 3)
        {
            Triangle tri;
            for (int j = 0; j < 3; j++)
            {
                tri.vertices[j] =
                    vec4(vertices[indices[i + j]].x, vertices[indices[i + j]].y, vertices[indices[i + j]].z);
                tri.normals[j] = vec4(normals[indices[i + j]].x, normals[indices[i + j]].y, normals[indices[i + j]].z);
                tri.uvs[j] = vec2(uvs[indices[i + j]].x, uvs[indices[i + j]].y);
                // tri.materialIndex = l;
            }
            triangles.push_back(tri);
        }
    }
    int end = triangles.size();

    UtilityFunctions::print("Build recursive using: start: " + godot::String(std::to_string(start).c_str()) +
                            ", end: " + godot::String(std::to_string(end).c_str()));

    // Step 2: Build the BVH using the added triangles
    return build_recursive(nodes, triangles, start, end);
}

// #define print_as_tree

void BVHBuilder::print_tree(const std::vector<BVHNode> &nodes)
{
#ifdef print_as_tree
    std::vector<int> stack;
    stack.push_back(0);
    while (true)
    {
        if (stack.empty())
            break;
        int i = stack.back();
        stack.pop_back();

#else
    for (size_t i = 0; i < nodes.size(); i++)
    {
#endif
        unsigned int left_child = nodes[i].left_child;
        unsigned int right_child = nodes[i].right_child;
        unsigned int first_tri_index = nodes[i].first_tri_index;
        unsigned int tri_count = nodes[i].tri_count;
        UtilityFunctions::print("{Node: l: " + godot::String(std::to_string(left_child).c_str()) +
                                ", r: " + godot::String(std::to_string(right_child).c_str()) +
                                ", t: " + godot::String(std::to_string(first_tri_index).c_str()) +
                                ", c: " + godot::String(std::to_string(tri_count).c_str()) + "}");
#ifdef print_as_tree
        if (left_child != 0)
        {
            stack.push_back(left_child);
        }
        if (right_child != 0)
        {
            stack.push_back(right_child);
        }
#endif
    }
}

} // namespace BVH
