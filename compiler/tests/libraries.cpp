#include <array>
#include <algorithm>
#include <numeric>
#include <span>
volatile unsigned input = 3;

template <class Range> unsigned total(const Range& r) {
    return std::accumulate(r.begin(), r.end(), 0u);
}

extern "C" unsigned kernel() {
    std::array<unsigned, 6> values{};
    unsigned seed = input;
    for (unsigned i = 0; i < values.size(); ++i) {
        values[i] = i + seed;
    }
    std::transform(values.begin(), values.end(), values.begin(), [](unsigned x) {
        return x * x + 1;
    });
    std::span<const unsigned> view(values);
    return total(view);
}
