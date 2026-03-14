#include "downsampler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
constexpr double Epsilon = 1e-12;

// Mirrors tsdownsample's overflow-safe step accumulation used for x-bin edges.
double sequentialAddMul(double startValue, double addValue, qsizetype mul, double epsilon)
{
    const double halfMul = static_cast<double>(mul) / 2.0;
    return startValue + addValue * halfMul + addValue * halfMul + epsilon;
}

bool isValidYInput(const QVector<double>& y)
{
    return !y.isEmpty();
}

bool isValidXYInput(const QVector<double>& x, const QVector<double>& y)
{
    return x.size() == y.size() && isValidYInput(y) && std::is_sorted(x.begin(), x.end());
}

QPair<qsizetype, qsizetype> argMinMax(const QVector<double>& values, qsizetype start, qsizetype end)
{
    qsizetype minIndex = start;
    qsizetype maxIndex = start;

    for (qsizetype i = start + 1; i < end; ++i) {
        if (values[i] < values[minIndex]) {
            minIndex = i;
        }
        if (values[i] > values[maxIndex]) {
            maxIndex = i;
        }
    }

    return {minIndex, maxIndex};
}

// Splits sorted x coordinates into value-space bins so gaps in x can produce
// fewer output points, matching tsdownsample behaviour.
QVector<QPair<qsizetype, qsizetype>> equidistantBins(const QVector<double>& x, qsizetype binCount)
{
    if (binCount < 2) {
        return {};
    }

    const double valueStep = (x.back() / static_cast<double>(binCount)) -
                             (x.front() / static_cast<double>(binCount));
    const double firstValue = x.front();

    QVector<QPair<qsizetype, qsizetype>> bins;
    bins.reserve(binCount);

    qsizetype index = 0;
    for (qsizetype i = 0; i < binCount; ++i) {
        const qsizetype startIndex = index;
        const double searchValue = sequentialAddMul(firstValue, valueStep, i + 1, Epsilon);
        if (x[startIndex] >= searchValue) {
            continue;
        }

        const auto upper = std::upper_bound(x.begin() + index, x.end(), searchValue);
        index = static_cast<qsizetype>(std::distance(x.begin(), upper));
        bins.append({startIndex, index});
    }

    return bins;
}

QVector<qsizetype> allIndices(qsizetype count)
{
    QVector<qsizetype> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    return indices;
}

QVector<double> gatherValues(const QVector<double>& values, const QVector<qsizetype>& indices)
{
    QVector<double> gathered;
    gathered.reserve(indices.size());

    for (qsizetype index : indices) {
        gathered.append(values[index]);
    }

    return gathered;
}

bool isValidMinMaxNOut(const QVector<double>& y, qsizetype nOut)
{
    return isValidYInput(y) && nOut > 0 && (nOut % 2) == 0;
}

bool isValidM4NOut(const QVector<double>& y, qsizetype nOut)
{
    return isValidYInput(y) && nOut > 0 && (nOut % 4) == 0;
}

bool isValidLttbNOut(const QVector<double>& y, qsizetype nOut)
{
    return isValidYInput(y) && nOut >= 3;
}

QVector<qsizetype> minMaxWithoutX(const QVector<double>& y, qsizetype nOut)
{
    if (!isValidMinMaxNOut(y, nOut)) {
        return {};
    }
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const double blockSize = static_cast<double>(y.size() - 1) / static_cast<double>(nOut / 2);
    QVector<qsizetype> indices(nOut);

    qsizetype startIndex = 0;
    for (qsizetype i = 0; i < nOut / 2; ++i) {
        const double end = blockSize * static_cast<double>(i + 1);
        const qsizetype endIndex = static_cast<qsizetype>(end) + 1;
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

QVector<qsizetype> minMaxWithX(const QVector<double>& x, const QVector<double>& y, qsizetype nOut)
{
    if (!isValidXYInput(x, y) || !isValidMinMaxNOut(y, nOut)) {
        return {};
    }
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const auto bins = equidistantBins(x, nOut / 2);
    QVector<qsizetype> indices;
    indices.reserve(nOut);

    for (const auto& [startIndex, endIndex] : bins) {
        if (endIndex <= startIndex + 2) {
            for (qsizetype i = startIndex; i < endIndex; ++i) {
                indices.append(i);
            }
            continue;
        }

        const auto [minIndex, maxIndex] = argMinMax(y, startIndex, endIndex);
        if (minIndex < maxIndex) {
            indices.append(minIndex);
            indices.append(maxIndex);
        } else {
            indices.append(maxIndex);
            indices.append(minIndex);
        }
    }

    return indices;
}

// M4 keeps first/min/max/last for each bucket.
QVector<qsizetype> m4WithoutX(const QVector<double>& y, qsizetype nOut)
{
    if (!isValidM4NOut(y, nOut)) {
        return {};
    }
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const double blockSize = static_cast<double>(y.size() - 1) / static_cast<double>(nOut / 4);
    QVector<qsizetype> indices(nOut);

    qsizetype startIndex = 0;
    for (qsizetype i = 0; i < nOut / 4; ++i) {
        const double end = blockSize * static_cast<double>(i + 1);
        const qsizetype endIndex = static_cast<qsizetype>(end) + 1;
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

QVector<qsizetype> m4WithX(const QVector<double>& x, const QVector<double>& y, qsizetype nOut)
{
    if (!isValidXYInput(x, y) || !isValidM4NOut(y, nOut)) {
        return {};
    }
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    const auto bins = equidistantBins(x, nOut / 4);
    QVector<qsizetype> indices;
    indices.reserve(nOut);

    for (const auto& [startIndex, endIndex] : bins) {
        if (endIndex <= startIndex + 4) {
            for (qsizetype i = startIndex; i < endIndex; ++i) {
                indices.append(i);
            }
            continue;
        }

        const auto [minIndex, maxIndex] = argMinMax(y, startIndex, endIndex);
        indices.append(startIndex);
        if (minIndex < maxIndex) {
            indices.append(minIndex);
            indices.append(maxIndex);
        } else {
            indices.append(maxIndex);
            indices.append(minIndex);
        }
        indices.append(endIndex - 1);
    }

    return indices;
}

// LTTB keeps first and last point and selects interior points by maximal
// triangle area with the next bucket average.
QVector<qsizetype> lttbWithX(const QVector<double>& x, const QVector<double>& y, qsizetype nOut)
{
    if (!isValidXYInput(x, y) || !isValidLttbNOut(y, nOut)) {
        return {};
    }
    if (nOut >= x.size()) {
        return allIndices(x.size());
    }

    const double every = static_cast<double>(x.size() - 2) / static_cast<double>(nOut - 2);
    qsizetype a = 0;
    QVector<qsizetype> sampled(nOut);
    sampled[0] = 0;

    for (qsizetype i = 0; i < nOut - 2; ++i) {
        const qsizetype avgRangeStart = static_cast<qsizetype>(every * static_cast<double>(i + 1)) + 1;
        const qsizetype avgRangeEnd =
            std::min(static_cast<qsizetype>(every * static_cast<double>(i + 2)) + 1, x.size());

        double avgY = 0.0;
        for (qsizetype j = avgRangeStart; j < avgRangeEnd; ++j) {
            avgY += y[j];
        }
        avgY /= static_cast<double>(avgRangeEnd - avgRangeStart);
        const double avgX = (x[avgRangeEnd - 1] + x[avgRangeStart]) / 2.0;

        const qsizetype rangeOffs = static_cast<qsizetype>(every * static_cast<double>(i)) + 1;
        const qsizetype rangeTo = avgRangeStart;

        const double pointAx = x[a];
        const double pointAy = y[a];
        const double d1 = pointAx - avgX;
        const double d2 = avgY - pointAy;
        const double offset = d1 * pointAy + d2 * pointAx;

        double maxArea = -1.0;
        qsizetype nextA = a;
        for (qsizetype j = rangeOffs; j < rangeTo; ++j) {
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

QVector<qsizetype> lttbWithoutX(const QVector<double>& y, qsizetype nOut)
{
    if (!isValidLttbNOut(y, nOut)) {
        return {};
    }
    if (nOut >= y.size()) {
        return allIndices(y.size());
    }

    QVector<double> x(y.size());
    std::iota(x.begin(), x.end(), 0.0);
    return lttbWithX(x, y, nOut);
}

QVector<qsizetype> minMaxLttbWithoutX(const QVector<double>& y, qsizetype nOut, qsizetype minmaxRatio)
{
    if (!isValidLttbNOut(y, nOut) || minmaxRatio <= 1) {
        return {};
    }

    if (y.size() / nOut > minmaxRatio) {
        QVector<double> innerY;
        innerY.reserve(y.size() - 2);
        for (qsizetype i = 1; i < y.size() - 1; ++i) {
            innerY.append(y[i]);
        }
        auto index = minMaxWithoutX(innerY, nOut * minmaxRatio);
        for (qsizetype& element : index) {
            ++element;
        }
        index.prepend(0);
        index.append(y.size() - 1);

        const auto reducedY = gatherValues(y, index);
        QVector<double> reducedX(index.size());
        std::transform(index.begin(), index.end(), reducedX.begin(), [](qsizetype value) {
            return static_cast<double>(value);
        });

        const auto selectedReduced = lttbWithX(reducedX, reducedY, nOut);
        QVector<qsizetype> selected;
        selected.reserve(selectedReduced.size());
        for (qsizetype reducedIndex : selectedReduced) {
            selected.append(index[reducedIndex]);
        }
        return selected;
    }

    return lttbWithoutX(y, nOut);
}

QVector<qsizetype> minMaxLttbWithX(
    const QVector<double>& x,
    const QVector<double>& y,
    qsizetype nOut,
    qsizetype minmaxRatio)
{
    if (!isValidXYInput(x, y) || !isValidLttbNOut(y, nOut) || minmaxRatio <= 1) {
        return {};
    }

    if (x.size() / nOut > minmaxRatio) {
        QVector<double> innerX;
        QVector<double> innerY;
        innerX.reserve(x.size() - 2);
        innerY.reserve(y.size() - 2);
        for (qsizetype i = 1; i < x.size() - 1; ++i) {
            innerX.append(x[i]);
            innerY.append(y[i]);
        }
        auto index = minMaxWithX(innerX, innerY, nOut * minmaxRatio);
        for (qsizetype& element : index) {
            ++element;
        }
        index.prepend(0);
        index.append(x.size() - 1);

        const auto reducedX = gatherValues(x, index);
        const auto reducedY = gatherValues(y, index);
        const auto selectedReduced = lttbWithX(reducedX, reducedY, nOut);

        QVector<qsizetype> selected;
        selected.reserve(selectedReduced.size());
        for (qsizetype reducedIndex : selectedReduced) {
            selected.append(index[reducedIndex]);
        }
        return selected;
    }

    return lttbWithX(x, y, nOut);
}
}

QVector<qsizetype> Downsampler::downsample(
    const QVector<double>& y,
    qsizetype nOut,
    DownsampleAlgorithm algorithm,
    qsizetype minmaxRatio)
{
    // Returns source indexes instead of copied samples so callers can reuse the
    // original buffers and apply the indexes to multiple aligned channels.
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

    return {};
}

QVector<qsizetype> Downsampler::downsample(
    const QVector<double>& x,
    const QVector<double>& y,
    qsizetype nOut,
    DownsampleAlgorithm algorithm,
    qsizetype minmaxRatio)
{
    // x must be sorted and aligned with y.
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

    return {};
}
