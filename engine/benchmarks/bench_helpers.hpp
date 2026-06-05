#pragma once
#include <algorithm>
#include <vector>

namespace bench {

// Percentile function templates — decay to raw function pointers, which is
// what benchmark::ComputeStatistics(StatisticsFunc*) requires.
template <int P>
double percentile(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    size_t idx = static_cast<size_t>(P / 100.0 * (s.size() - 1));
    return s[idx];
}

} // namespace bench
