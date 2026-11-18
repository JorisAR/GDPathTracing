#ifndef PT_ALIAS_TABLE_H
#define PT_ALIAS_TABLE_H

#include <cstdint>
#include <vector>

namespace PT
{

// Walker/Vose alias table: picks index i with probability weights[i] / sum(weights) from one
// uniform number and at most two lookups (O(1) on the GPU instead of a binary search over a CDF).
// Sampling: i = floor(u * n), keep i if frac(u * n) < prob[i], otherwise take alias[i].
// All-zero weights give a uniform table.
inline void build_alias_table(const std::vector<double> &weights, std::vector<float> &prob,
                              std::vector<uint32_t> &alias)
{
    const size_t n = weights.size();
    prob.assign(n, 1.0f);
    alias.resize(n);
    for (size_t i = 0; i < n; i++)
        alias[i] = static_cast<uint32_t>(i);
    if (n == 0)
        return;
    double total = 0.0;
    for (double w : weights)
        total += w > 0.0 ? w : 0.0;
    if (!(total > 0.0))
        return;

    std::vector<double> scaled(n);
    std::vector<uint32_t> small, large;
    small.reserve(n);
    large.reserve(n);
    for (size_t i = 0; i < n; i++)
    {
        scaled[i] = (weights[i] > 0.0 ? weights[i] : 0.0) * static_cast<double>(n) / total;
        (scaled[i] < 1.0 ? small : large).push_back(static_cast<uint32_t>(i));
    }
    while (!small.empty() && !large.empty())
    {
        const uint32_t s = small.back();
        small.pop_back();
        const uint32_t l = large.back();
        prob[s] = static_cast<float>(scaled[s]);
        alias[s] = l;
        scaled[l] = (scaled[l] + scaled[s]) - 1.0;
        if (scaled[l] < 1.0)
        {
            large.pop_back();
            small.push_back(l);
        }
    }
    // Leftovers are 1 up to rounding.
    for (uint32_t i : large)
        prob[i] = 1.0f;
    for (uint32_t i : small)
        prob[i] = 1.0f;
}

} // namespace PT

#endif // PT_ALIAS_TABLE_H
