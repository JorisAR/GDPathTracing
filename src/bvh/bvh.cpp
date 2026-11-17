#include "bvh.h"

#include <algorithm>
#include <cfloat>
#include <numeric>

using namespace godot;

namespace PT
{

namespace
{

struct Bounds
{
    Vector3 min = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector3 max = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    void grow(const Vector3 &p)
    {
        min = min.min(p);
        max = max.max(p);
    }
    void grow(const Bounds &b)
    {
        min = min.min(b.min);
        max = max.max(b.max);
    }
    float area() const
    {
        Vector3 e = max - min;
        if (e.x < 0.0f)
            return 0.0f;
        return e.x * e.y + e.y * e.z + e.z * e.x;
    }
};

void write_bounds(GpuBVHNode &n, const Bounds &b)
{
    n.aabb_min[0] = b.min.x;
    n.aabb_min[1] = b.min.y;
    n.aabb_min[2] = b.min.z;
    n.aabb_max[0] = b.max.x;
    n.aabb_max[1] = b.max.y;
    n.aabb_max[2] = b.max.z;
}

struct BuildItem
{
    uint32_t node;  // absolute node index
    uint32_t first; // range in `order`
    uint32_t count;
    int depth;
};

} // namespace

uint32_t BLASBuilder::build(const std::vector<BuildTriangle> &tris, uint32_t triangle_offset,
                            std::vector<GpuBVHNode> &nodes, std::vector<uint32_t> &order)
{
    const uint32_t tri_count = static_cast<uint32_t>(tris.size());
    order.resize(tri_count);
    std::iota(order.begin(), order.end(), 0u);

    std::vector<Vector3> centroids(tri_count);
    std::vector<Bounds> tri_bounds(tri_count);
    for (uint32_t i = 0; i < tri_count; i++)
    {
        const BuildTriangle &t = tris[i];
        centroids[i] = (t.v0 + t.v1 + t.v2) / 3.0f;
        tri_bounds[i].grow(t.v0);
        tri_bounds[i].grow(t.v1);
        tri_bounds[i].grow(t.v2);
    }

    const uint32_t root = static_cast<uint32_t>(nodes.size());
    nodes.push_back(GpuBVHNode{});

    if (tri_count == 0)
    {
        // Empty mesh. Callers skip instancing these, but emit a well-formed leaf with inverted
        // bounds (never hit by the slab test) just in case.
        Bounds empty;
        write_bounds(nodes[root], empty);
        nodes[root].left_first = triangle_offset;
        nodes[root].tri_count = 1;
        return root;
    }

    std::vector<BuildItem> stack;
    stack.push_back({root, 0, tri_count, 0});

    while (!stack.empty())
    {
        BuildItem item = stack.back();
        stack.pop_back();

        Bounds bounds, centroid_bounds;
        for (uint32_t i = item.first; i < item.first + item.count; i++)
        {
            bounds.grow(tri_bounds[order[i]]);
            centroid_bounds.grow(centroids[order[i]]);
        }
        write_bounds(nodes[item.node], bounds);

        auto make_leaf = [&]() {
            nodes[item.node].left_first = triangle_offset + item.first;
            nodes[item.node].tri_count = item.count;
        };

        if (item.count <= 2 || item.depth >= MAX_DEPTH - 1)
        {
            make_leaf();
            continue;
        }

        // Binned SAH over centroid bounds.
        float best_cost = FLT_MAX;
        int best_axis = -1;
        int best_split = 0;
        for (int axis = 0; axis < 3; axis++)
        {
            float cmin = centroid_bounds.min[axis];
            float cmax = centroid_bounds.max[axis];
            if (cmax - cmin < 1e-7f)
                continue;
            float scale = BINS / (cmax - cmin);

            Bounds bin_bounds[BINS];
            uint32_t bin_count[BINS] = {};
            for (uint32_t i = item.first; i < item.first + item.count; i++)
            {
                uint32_t t = order[i];
                int b = std::min(BINS - 1, int((centroids[t][axis] - cmin) * scale));
                bin_count[b]++;
                bin_bounds[b].grow(tri_bounds[t]);
            }

            float left_area[BINS - 1];
            uint32_t left_count[BINS - 1];
            Bounds acc;
            uint32_t cnt = 0;
            for (int i = 0; i < BINS - 1; i++)
            {
                acc.grow(bin_bounds[i]);
                cnt += bin_count[i];
                left_area[i] = acc.area();
                left_count[i] = cnt;
            }
            acc = Bounds();
            cnt = 0;
            for (int i = BINS - 1; i > 0; i--)
            {
                acc.grow(bin_bounds[i]);
                cnt += bin_count[i];
                if (left_count[i - 1] == 0 || cnt == 0)
                    continue;
                float cost = left_area[i - 1] * left_count[i - 1] + acc.area() * cnt;
                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_axis = axis;
                    best_split = i;
                }
            }
        }

        // Compare against the cost of keeping this node as a leaf (intersection cost ~ triangle count).
        float leaf_cost = bounds.area() * item.count;
        bool small_enough = item.count <= MAX_LEAF_SIZE;
        if (best_axis < 0 || (small_enough && best_cost >= leaf_cost))
        {
            if (best_axis < 0 && item.count > MAX_LEAF_SIZE)
            {
                // All centroids coincide: fall back to an object median split.
                best_axis = -2;
            }
            else
            {
                make_leaf();
                continue;
            }
        }

        uint32_t mid;
        if (best_axis >= 0)
        {
            float cmin = centroid_bounds.min[best_axis];
            float scale = BINS / (centroid_bounds.max[best_axis] - cmin);
            auto begin = order.begin() + item.first;
            auto end = begin + item.count;
            auto it = std::partition(begin, end, [&](uint32_t t) {
                int b = std::min(BINS - 1, int((centroids[t][best_axis] - cmin) * scale));
                return b < best_split;
            });
            mid = static_cast<uint32_t>(it - order.begin());
        }
        else
        {
            mid = item.first + item.count / 2;
        }
        if (mid == item.first || mid == item.first + item.count)
            mid = item.first + item.count / 2;

        uint32_t left = static_cast<uint32_t>(nodes.size());
        nodes.push_back(GpuBVHNode{});
        nodes.push_back(GpuBVHNode{});
        nodes[item.node].left_first = left;
        nodes[item.node].tri_count = 0;

        stack.push_back({left + 1, mid, item.first + item.count - mid, item.depth + 1});
        stack.push_back({left, item.first, mid - item.first, item.depth + 1});
    }

    return root;
}

void TLASBuilder::build(const std::vector<AABB> &bounds, const std::vector<uint32_t> &instance_ids,
                        std::vector<GpuTLASNode> &nodes)
{
    nodes.clear();
    const size_t n = bounds.size();
    nodes.reserve(std::max<size_t>(1, 2 * n));
    nodes.push_back(GpuTLASNode{}); // reserved for the root

    auto set_bounds = [](GpuTLASNode &node, const Vector3 &mn, const Vector3 &mx) {
        node.aabb_min[0] = mn.x;
        node.aabb_min[1] = mn.y;
        node.aabb_min[2] = mn.z;
        node.aabb_max[0] = mx.x;
        node.aabb_max[1] = mx.y;
        node.aabb_max[2] = mx.z;
    };

    if (n == 0)
    {
        set_bounds(nodes[0], Vector3(1, 1, 1), Vector3(-1, -1, -1));
        nodes[0].left = 0;
        nodes[0].right = TLAS_LEAF;
        return;
    }

    // Working set: node index plus cached bounds.
    std::vector<uint32_t> list(n);
    std::vector<Vector3> bmin(n), bmax(n);
    for (size_t i = 0; i < n; i++)
    {
        GpuTLASNode leaf{};
        Vector3 mn = bounds[i].position;
        Vector3 mx = bounds[i].position + bounds[i].size;
        set_bounds(leaf, mn, mx);
        leaf.left = instance_ids[i];
        leaf.right = TLAS_LEAF;
        list[i] = static_cast<uint32_t>(nodes.size());
        nodes.push_back(leaf);
        bmin[i] = mn;
        bmax[i] = mx;
    }

    auto find_best_match = [&](size_t count, size_t a) {
        float smallest = FLT_MAX;
        size_t best = a == 0 ? 1 : 0;
        for (size_t b = 0; b < count; b++)
        {
            if (b == a)
                continue;
            Vector3 e = bmax[a].max(bmax[b]) - bmin[a].min(bmin[b]);
            float area = e.x * e.y + e.y * e.z + e.z * e.x;
            if (area < smallest)
            {
                smallest = area;
                best = b;
            }
        }
        return best;
    };

    size_t count = n;
    if (count > 1)
    {
        size_t a = 0;
        size_t b = find_best_match(count, a);
        while (count > 1)
        {
            size_t c = find_best_match(count, b);
            if (a == c)
            {
                GpuTLASNode parent{};
                Vector3 mn = bmin[a].min(bmin[b]);
                Vector3 mx = bmax[a].max(bmax[b]);
                set_bounds(parent, mn, mx);
                parent.left = list[a];
                parent.right = list[b];
                list[a] = static_cast<uint32_t>(nodes.size());
                nodes.push_back(parent);
                bmin[a] = mn;
                bmax[a] = mx;

                count--;
                list[b] = list[count];
                bmin[b] = bmin[count];
                bmax[b] = bmax[count];
                if (a == count)
                    a = b;
                b = find_best_match(count, a);
            }
            else
            {
                a = b;
                b = c;
            }
        }
        nodes[0] = nodes[list[a]];
    }
    else
    {
        nodes[0] = nodes[list[0]];
    }
}

} // namespace PT
