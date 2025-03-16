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
    min = min.min(point);
    max = max.max(point);
}

bool BoundingBox::intersect(const BoundingBox &other) const
{
    return !(min.x > other.max.x || max.x < other.min.x || min.y > other.max.y || max.y < other.min.y ||
             min.z > other.max.z || max.z < other.min.z);
}

BoundingBox BVHBuilder::compute_bounding_box(const std::vector<Triangle> &triangles, const int start,
                                             const int end) const
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

float BVHBuilder::EvaluateSAH(const std::vector<Triangle> &triangles, const BVHNode &node, const int axis,
                              float &bestSplit) const
{
    const int BINS = 8;
    struct Bin
    {
        BoundingBox bounds;
        int count = 0;
    };
    Bin bins[BINS];

    // Calculate bin dimensions
    float minBound = node.aabbMin[axis];
    float maxBound = node.aabbMax[axis];
    float range = maxBound - minBound;
    if (range < 1e-6f)
        return 1e+30f; // Degenerate node

    float invRange = 1.0f / range;

    // Bin triangles
    for (uint32_t i = 0; i < node.tri_count; i++)
    {
        const Triangle &tri = triangles[node.first_tri_index + i];
        float centroid = tri.centroid[axis];
        int binIdx = std::clamp(int(BINS * (centroid - minBound) * invRange), 0, BINS - 1);
        bins[binIdx].count++;
        bins[binIdx].bounds.extend(tri.vertices[0]);
        bins[binIdx].bounds.extend(tri.vertices[1]);
        bins[binIdx].bounds.extend(tri.vertices[2]);
    }

    // Accumulate from left and right
    float bestCost =  1e+30f;
    BoundingBox leftAccum[BINS];
    int leftCount[BINS];

    // Left-to-right pass
    BoundingBox leftBox;
    int countLeft = 0;
    for (int i = 0; i < BINS - 1; i++)
    {
        leftBox.extend(bins[i].bounds.min);
        leftBox.extend(bins[i].bounds.max);
        countLeft += bins[i].count;
        leftAccum[i] = leftBox;
        leftCount[i] = countLeft;
    }

    // Right-to-left pass
    BoundingBox rightBox;
    int countRight = 0;
    for (int i = BINS - 1; i > 0; i--)
    {
        rightBox.extend(bins[i].bounds.min);
        rightBox.extend(bins[i].bounds.max);
        countRight += bins[i].count;
        float cost = leftAccum[i - 1].area() * leftCount[i - 1] + rightBox.area() * countRight;

        if (cost < bestCost)
        {
            bestCost = cost;
            bestSplit = minBound + (float(i) / BINS) * range;
        }
    }

    return bestCost;
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
    if (node.tri_count <= 4) // leaf
    {
        return node_index;
    }
    // determine split axis using SAH
    float bestSplit, bestCost = 1e30f;
    int bestAxis = -1;

    for (int axis = 0; axis < 3; axis++)
    {
        float split;
        float cost = EvaluateSAH(triangles, node, axis, split);
        if (cost < bestCost)
        {
            bestCost = cost;
            bestSplit = split;
            bestAxis = axis;
        }
    }

    // Dont split if cost would be greater
    vec4 e = node.aabbMax - node.aabbMin;
    float parentArea = e.x * e.y + e.y * e.z + e.z * e.x;
    float parentCost = node.tri_count * parentArea;
    if (bestCost * 0.8f >= parentCost) // allow slightly worse splits
        return node_index;

    // Partition the triangles around the split position
    int i = start;
    int j = end - 1;
    while (i <= j)
    {
        float centroid = triangles[i].centroid[bestAxis];
        if (centroid < bestSplit)
            i++;
        else
            std::swap(triangles[i], triangles[j--]);
    }

    // Ensure the partitioning isn't degenerate
    int left_count = i - start;
    // if (left_count == 0 || left_count == node.tri_count)
    //     return node_index;

    // median split if partitioning is degenerate
    if (left_count == 0 || left_count == node.tri_count)
    {
        int mid = start + (end - start) / 2;
        std::nth_element(
            triangles.begin() + start, triangles.begin() + mid, triangles.begin() + end,
            [bestAxis](const Triangle &a, const Triangle &b) { return a.centroid[bestAxis] < b.centroid[bestAxis]; });
        i = mid;
    }

    // UtilityFunctions::print(node_index);
    nodes[node_index].left_child = build_recursive(nodes, triangles, start, i);
    nodes[node_index].right_child = build_recursive(nodes, triangles, i, end);
    nodes[node_index].tri_count = 0;

    return node_index;
}

unsigned int BVHBuilder::BuildBVH(std::vector<BVHNode> &nodes, std::vector<Triangle> &triangles,
                                  const Ref<ArrayMesh> &arrayMesh)
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
            }
            tri.materialIndex = l;
            tri.centroid = (tri.vertices[0] + tri.vertices[1] + tri.vertices[2]) * 0.33333333f;
            triangles.push_back(tri);
        }
    }
    int end = triangles.size();

#ifdef VERBOSE_BVH_BUILDING
    UtilityFunctions::print("Build recursive using: start: " + godot::String(std::to_string(start).c_str()) +
                            ", end: " + godot::String(std::to_string(end).c_str()));
#endif

    // Step 2: Build the BVH using the added triangles
    return build_recursive(nodes, triangles, start, end);
}

#define print_as_tree

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

void TLAS::build(std::vector<TLASNode> &tlasNodes, const std::vector<BLASInstance> &blasInstances)
{
    int blasCount = blasInstances.size();
    tlasNodes.reserve(blasCount * 2 - 1);

    // Reserve slot 0 for the root
    TLASNode rootNode;
    tlasNodes.push_back(rootNode);

    // Assign a TLAS leaf node to each BLAS
    std::vector<int> nodeIdx;
    nodeIdx.reserve(blasCount);
    int nodesUsed = 1;

    for (size_t i = 0; i < blasCount; i++)
    {
        TLASNode node;
        node.aabbMin = vec3(blasInstances[i].aabbMin);
        node.aabbMax = vec3(blasInstances[i].aabbMax);
        node.blas = i;
        node.leftRight = 0; // makes it a leaf
        tlasNodes.push_back(node);
        nodeIdx.push_back(nodesUsed++);
    }

    // Use agglomerative clustering to build the TLAS
    int A = 0, B = FindBestMatch(tlasNodes, nodeIdx, blasCount, A);
    while (blasCount > 1)
    {
        int C = FindBestMatch(tlasNodes, nodeIdx, blasCount, B);
        if (A == C)
        {
            int nodeIdxA = nodeIdx[A], nodeIdxB = nodeIdx[B];
            TLASNode &nodeA = tlasNodes[nodeIdxA];
            TLASNode &nodeB = tlasNodes[nodeIdxB];
            TLASNode newNode;
            newNode.leftRight = nodeIdxA + (nodeIdxB << 16);
            newNode.aabbMin = nodeA.aabbMin.min(nodeB.aabbMin);
            newNode.aabbMax = nodeA.aabbMax.max(nodeB.aabbMax);
            tlasNodes.push_back(newNode);
            nodeIdx[A] = nodesUsed++;
            nodeIdx[B] = nodeIdx[--blasCount];
            B = FindBestMatch(tlasNodes, nodeIdx, blasCount, A);
        }
        else
        {
            A = B;
            B = C;
        }
    }

    // Set the root node
    tlasNodes[0] = tlasNodes[nodeIdx[A]];
}

inline int TLAS::FindBestMatch(const std::vector<TLASNode> &tlasNodes, const std::vector<int> &list, const int N,
                               const int A) const
{
    float smallest = 1e30f;
    int bestB = -1;
    for (int B = 0; B < N; B++)
    {
        if (B != A)
        {
            vec3 bmax = tlasNodes[list[A]].aabbMax.max(tlasNodes[list[B]].aabbMax);
            vec3 bmin = tlasNodes[list[A]].aabbMin.min(tlasNodes[list[B]].aabbMin);
            vec3 e = bmax - bmin;
            float surfaceArea = e.x * e.y + e.y * e.z + e.z * e.x;
            if (surfaceArea < smallest)
            {
                smallest = surfaceArea;
                bestB = B;
            }
        }
    }
    return bestB;
}

void TLAS::print_tree(const std::vector<TLASNode> &nodes)
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
        unsigned int left_child = nodes[i].leftRight & 0xFFFF;
        unsigned int right_child = nodes[i].leftRight >> 16;
        unsigned int blas = nodes[i].blas;
        UtilityFunctions::print("{Node: l: " + godot::String(std::to_string(left_child).c_str()) +
                                ", r: " + godot::String(std::to_string(right_child).c_str()) +
                                ", b: " + godot::String(std::to_string(blas).c_str()) + ", m" +
                                nodes[i].aabbMin.toString() + ", M" + nodes[i].aabbMax.toString() + "}");
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
