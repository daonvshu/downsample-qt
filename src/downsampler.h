#pragma once

#include <cstddef>
#include <vector>

enum class DownsampleAlgorithm
{
    MinMax,
    M4,
    Lttb,
    MinMaxLttb,
};

class Downsampler final
{
public:
    static std::vector<std::size_t> downsample(
        const std::vector<double>& y,
        std::size_t nOut,
        DownsampleAlgorithm algorithm,
        std::size_t minmaxRatio = 4);

    static std::vector<std::size_t> downsample(
        const std::vector<double>& x,
        const std::vector<double>& y,
        std::size_t nOut,
        DownsampleAlgorithm algorithm,
        std::size_t minmaxRatio = 4);
};
