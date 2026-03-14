#include "downsampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace
{
constexpr double Epsilon = 1e-12;

double sequentialAddMul(double startValue, double addValue, std::size_t mul, double epsilon)
{
    const double halfMul = static_cast<double>(mul) / 2.0;
    return startValue + addValue * halfMul + addValue * halfMul + epsilon;
}

void validateYInput(const std::vector<double>& y)
{
    if (y.empty()) {
        throw std::invalid_argument("y must not be empty");
    }
}

void validateXYInput(const std::vector<double>& x, const std::vector<double>& y)
{
    if (x.size() != y.size()) {
        throw std::invalid_argument("x and y must have the same length");
    }

    validateYInput(y);

    if (!std::is_sorted(x.begin(), x.end())) {
        throw std::invalid_argument("x must be sorted in ascending order");
    }
}

std::pair<std::size_t, std::size_t> argMinMax(const std::vector<double>& values, std::size_t start, std::size_t end)
{
    std::size_t minIndex = start;
    std::size_t maxIndex = start;

    for (std::size_t i = start + 1; i < end; ++i) {
        if (values[i] < values[minIndex]) {
            minIndex = i;
        }
        if (values[i] > values[maxIndex]) {
            maxIndex = i;
        }
    }

    return {minIndex, maxIndex};
}

std::vector<std::pair<std::size_t, std::size_t>> equidistantBins(
    const std::vector<double>& x,
    std::size_t binCount)
{
    if (binCount < 2) {
        throw std::invalid_argument("binCount must be at least 2");
    }

    const double valueStep = (x.back() / static_cast<double>(binCount)) -
                             (x.front() / static_cast<double>(binCount));
    const double firstValue = x.front();

    std::vector<std::pair<std::size_t, std::size_t>> bins;
    bins.reserve(binCount);

    std::size_t index = 0;
    for (std::size_t i = 0; i < binCount; ++i) {
        const std::size_t startIndex = index;
        const double searchValue = sequentialAddMul(firstValue, valueStep, i + 1, Epsilon);
        if (x[startIndex] >= searchValue) {
            continue;
        }

        const auto upper = std::upper_bound(x.begin() + static_cast<std::ptrdiff_t>(index), x.end(), searchValue);
        index = static_cast<std::size_t>(std::distance(x.begin(), upper));
        bins.emplace_back(startIndex, index);
    }

    return bins;
}

std::vector<std::size_t> allIndices(std::size_t count)
{
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    return indices;
}

std::vector<double> gatherValues(const std::vector<double>& values, const std::vector<std::size_t>& indices)
{
    std::vector<double> gathered;
    gathered.reserve(indices.size());

    for (std::size_t index : indices) {
        gathered.push_back(values[index]);
    }

    return gathered;
}

void validateMinMaxNOut(const std::vector<double>& y, std::size_t nOut)
{
    validateYInput(y);
    if (nOut == 0 || (nOut % 2) != 0) {
        throw std::invalid_argument("MinMax requires nOut to be a positive even number");
    }
}

void validateM4NOut(const std::vector<double>& y, std::size_t nOut)
{
    validateYInput(y);
    if (nOut == 0 || (nOut % 4) != 0) {
        throw std::invalid_argument("M4 requires nOut to be a positive multiple of 4");
    }
}

void validateLttbNOut(const std::vector<double>& y, std::size_t nOut)
{
    validateYInput(y);
    if (nOut < 3) {
        throw std::invalid_argument("LTTB requires nOut to be at least 3");
    }
}

std::vector<std::size_t> minMaxWithoutX(const std::vector<double>& y, std::size_t nOut)
{
    validateMinMaxNOut(y, nOut);
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const double blockSize = static_cast<double>(y.size() - 1) / static_cast<double>(nOut / 2);
    std::vector<std::size_t> indices(nOut);

    std::size_t startIndex = 0;
    for (std::size_t i = 0; i < nOut / 2; ++i) {
        const double end = blockSize * static_cast<double>(i + 1);
        const std::size_t endIndex = static_cast<std::size_t>(end) + 1;
        const auto [minIndex, maxIndex] = argMinMax(y, startIndex, endIndex);

        if (minIndex < maxIndex) {
            indices[2 * i] = minIndex;
            indices[2 * i + 1] = maxIndex;
        } else {
            indices[2 * i] = maxIndex;
            indices[2 * i + 1] = minIndex;
        }

        startIndex = endIndex;
    }

    return indices;
}

std::vector<std::size_t> minMaxWithX(
    const std::vector<double>& x,
    const std::vector<double>& y,
    std::size_t nOut)
{
    validateXYInput(x, y);
    validateMinMaxNOut(y, nOut);
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const auto bins = equidistantBins(x, nOut / 2);
    std::vector<std::size_t> indices;
    indices.reserve(nOut);

    for (const auto& [startIndex, endIndex] : bins) {
        if (endIndex <= startIndex + 2) {
            for (std::size_t i = startIndex; i < endIndex; ++i) {
                indices.push_back(i);
            }
            continue;
        }

        const auto [minIndex, maxIndex] = argMinMax(y, startIndex, endIndex);
        if (minIndex < maxIndex) {
            indices.push_back(minIndex);
            indices.push_back(maxIndex);
        } else {
            indices.push_back(maxIndex);
            indices.push_back(minIndex);
        }
    }

    return indices;
}

std::vector<std::size_t> m4WithoutX(const std::vector<double>& y, std::size_t nOut)
{
    validateM4NOut(y, nOut);
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const double blockSize = static_cast<double>(y.size() - 1) / static_cast<double>(nOut / 4);
    std::vector<std::size_t> indices(nOut);

    std::size_t startIndex = 0;
    for (std::size_t i = 0; i < nOut / 4; ++i) {
        const double end = blockSize * static_cast<double>(i + 1);
        const std::size_t endIndex = static_cast<std::size_t>(end) + 1;
        const auto [minIndex, maxIndex] = argMinMax(y, startIndex, endIndex);

        indices[4 * i] = startIndex;
        if (minIndex < maxIndex) {
            indices[4 * i + 1] = minIndex;
            indices[4 * i + 2] = maxIndex;
        } else {
            indices[4 * i + 1] = maxIndex;
            indices[4 * i + 2] = minIndex;
        }
        indices[4 * i + 3] = endIndex - 1;

        startIndex = endIndex;
    }

    return indices;
}

std::vector<std::size_t> m4WithX(
    const std::vector<double>& x,
    const std::vector<double>& y,
    std::size_t nOut)
{
    validateXYInput(x, y);
    validateM4NOut(y, nOut);
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const auto bins = equidistantBins(x, nOut / 4);
    std::vector<std::size_t> indices;
    indices.reserve(nOut);

    for (const auto& [startIndex, endIndex] : bins) {
        if (endIndex <= startIndex + 4) {
            for (std::size_t i = startIndex; i < endIndex; ++i) {
                indices.push_back(i);
            }
            continue;
        }

        const auto [minIndex, maxIndex] = argMinMax(y, startIndex, endIndex);
        indices.push_back(startIndex);
        if (minIndex < maxIndex) {
            indices.push_back(minIndex);
            indices.push_back(maxIndex);
        } else {
            indices.push_back(maxIndex);
            indices.push_back(minIndex);
        }
        indices.push_back(endIndex - 1);
    }

    return indices;
}

std::vector<std::size_t> lttbWithX(
    const std::vector<double>& x,
    const std::vector<double>& y,
    std::size_t nOut)
{
    validateXYInput(x, y);
    validateLttbNOut(y, nOut);
    if (nOut >= x.size()) {
        return allIndices(x.size());
    }

    const double every = static_cast<double>(x.size() - 2) / static_cast<double>(nOut - 2);
    std::size_t a = 0;
    std::vector<std::size_t> sampled(nOut);
    sampled[0] = 0;

    for (std::size_t i = 0; i < nOut - 2; ++i) {
        const std::size_t avgRangeStart = static_cast<std::size_t>(every * static_cast<double>(i + 1)) + 1;
        const std::size_t avgRangeEnd =
            std::min(static_cast<std::size_t>(every * static_cast<double>(i + 2)) + 1, x.size());

        double avgY = 0.0;
        for (std::size_t j = avgRangeStart; j < avgRangeEnd; ++j) {
            avgY += y[j];
        }
        avgY /= static_cast<double>(avgRangeEnd - avgRangeStart);
        const double avgX = (x[avgRangeEnd - 1] + x[avgRangeStart]) / 2.0;

        const std::size_t rangeOffs = static_cast<std::size_t>(every * static_cast<double>(i)) + 1;
        const std::size_t rangeTo = avgRangeStart;

        const double pointAx = x[a];
        const double pointAy = y[a];
        const double d1 = pointAx - avgX;
        const double d2 = avgY - pointAy;
        const double offset = d1 * pointAy + d2 * pointAx;

        double maxArea = -1.0;
        std::size_t nextA = a;
        for (std::size_t j = rangeOffs; j < rangeTo; ++j) {
            const double area = std::abs(d1 * y[j] + d2 * x[j] - offset);
            if (area > maxArea) {
                maxArea = area;
                nextA = j;
            }
        }

        a = nextA;
        sampled[i + 1] = a;
    }

    sampled[nOut - 1] = y.size() - 1;
    return sampled;
}

std::vector<std::size_t> lttbWithoutX(const std::vector<double>& y, std::size_t nOut)
{
    validateLttbNOut(y, nOut);
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const std::vector<double> x = [] (std::size_t count) {
        std::vector<double> indices(count);
        std::iota(indices.begin(), indices.end(), 0.0);
        return indices;
    }(y.size());

    return lttbWithX(x, y, nOut);
}

std::vector<std::size_t> minMaxLttbWithoutX(
    const std::vector<double>& y,
    std::size_t nOut,
    std::size_t minmaxRatio)
{
    validateLttbNOut(y, nOut);
    if (minmaxRatio <= 1) {
        throw std::invalid_argument("minmaxRatio must be greater than 1");
    }

    if (y.size() / nOut > minmaxRatio) {
        std::vector<double> innerY(y.begin() + 1, y.end() - 1);
        auto index = minMaxWithoutX(innerY, nOut * minmaxRatio);
        for (std::size_t& element : index) {
            ++element;
        }
        index.insert(index.begin(), 0);
        index.push_back(y.size() - 1);

        const auto reducedY = gatherValues(y, index);
        std::vector<double> reducedX(index.size());
        std::transform(index.begin(), index.end(), reducedX.begin(), [] (std::size_t value) {
            return static_cast<double>(value);
        });

        const auto selectedReduced = lttbWithX(reducedX, reducedY, nOut);
        std::vector<std::size_t> selected;
        selected.reserve(selectedReduced.size());
        for (std::size_t reducedIndex : selectedReduced) {
            selected.push_back(index[reducedIndex]);
        }
        return selected;
    }

    return lttbWithoutX(y, nOut);
}

std::vector<std::size_t> minMaxLttbWithX(
    const std::vector<double>& x,
    const std::vector<double>& y,
    std::size_t nOut,
    std::size_t minmaxRatio)
{
    validateXYInput(x, y);
    validateLttbNOut(y, nOut);
    if (minmaxRatio <= 1) {
        throw std::invalid_argument("minmaxRatio must be greater than 1");
    }

    if (x.size() / nOut > minmaxRatio) {
        std::vector<double> innerX(x.begin() + 1, x.end() - 1);
        std::vector<double> innerY(y.begin() + 1, y.end() - 1);
        auto index = minMaxWithX(innerX, innerY, nOut * minmaxRatio);
        for (std::size_t& element : index) {
            ++element;
        }
        index.insert(index.begin(), 0);
        index.push_back(x.size() - 1);

        const auto reducedX = gatherValues(x, index);
        const auto reducedY = gatherValues(y, index);
        const auto selectedReduced = lttbWithX(reducedX, reducedY, nOut);

        std::vector<std::size_t> selected;
        selected.reserve(selectedReduced.size());
        for (std::size_t reducedIndex : selectedReduced) {
            selected.push_back(index[reducedIndex]);
        }
        return selected;
    }

    return lttbWithX(x, y, nOut);
}
}

std::vector<std::size_t> Downsampler::downsample(
    const std::vector<double>& y,
    std::size_t nOut,
    DownsampleAlgorithm algorithm,
    std::size_t minmaxRatio)
{
    switch (algorithm) {
    case DownsampleAlgorithm::MinMax:
        return minMaxWithoutX(y, nOut);
    case DownsampleAlgorithm::M4:
        return m4WithoutX(y, nOut);
    case DownsampleAlgorithm::Lttb:
        return lttbWithoutX(y, nOut);
    case DownsampleAlgorithm::MinMaxLttb:
        return minMaxLttbWithoutX(y, nOut, minmaxRatio);
    }

    throw std::invalid_argument("Unsupported downsample algorithm");
}

std::vector<std::size_t> Downsampler::downsample(
    const std::vector<double>& x,
    const std::vector<double>& y,
    std::size_t nOut,
    DownsampleAlgorithm algorithm,
    std::size_t minmaxRatio)
{
    switch (algorithm) {
    case DownsampleAlgorithm::MinMax:
        return minMaxWithX(x, y, nOut);
    case DownsampleAlgorithm::M4:
        return m4WithX(x, y, nOut);
    case DownsampleAlgorithm::Lttb:
        return lttbWithX(x, y, nOut);
    case DownsampleAlgorithm::MinMaxLttb:
        return minMaxLttbWithX(x, y, nOut, minmaxRatio);
    }

    throw std::invalid_argument("Unsupported downsample algorithm");
}
