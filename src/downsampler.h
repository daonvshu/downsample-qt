#pragma once

#include <QtTypes>
#include <QVector>

/**
 * \brief Supported downsampling algorithms.
 *
 * MinMax keeps the minimum and maximum sample of each bucket.
 * M4 keeps the first, minimum, maximum, and last sample of each bucket.
 * Lttb applies the Largest-Triangle-Three-Buckets algorithm.
 * MinMaxLttb performs a MinMax preselection step before running LTTB.
 */
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
    /**
     * \brief Downsamples a single y-series and returns indexes into the source data.
     *
     * \param y Source y values to downsample.
     * \param nOut Requested output point count. For MinMax it should be even, for
     * M4 it should be a multiple of 4, and for LTTB it should be at least 3.
     * \param algorithm Algorithm used to choose representative indexes.
     * \param minmaxRatio Only used by MinMaxLttb. When y.size() / nOut is larger
     * than this ratio, the algorithm first performs MinMax preselection and then
     * runs LTTB on the reduced set.
     * \param elapsedNs Optional output parameter that receives the total
     * execution time in nanoseconds.
     * \return Source indexes in ascending order for the selected points. Returns
     * an empty QVector when the input is invalid.
     */
    static QVector<qsizetype> downsample(
        const QVector<double>& y,
        qsizetype nOut,
        DownsampleAlgorithm algorithm,
        qsizetype minmaxRatio = 4,
        qint64* elapsedNs = nullptr);

    /**
     * \brief Downsamples an x/y series pair and returns indexes into the source data.
     *
     * \param x Source x values. Must be sorted in ascending order and have the
     * same length as y.
     * \param y Source y values aligned element-by-element with x.
     * \param nOut Requested output point count. For MinMax it should be even, for
     * M4 it should be a multiple of 4, and for LTTB it should be at least 3.
     * \param algorithm Algorithm used to choose representative indexes.
     * \param minmaxRatio Only used by MinMaxLttb. When x.size() / nOut is larger
     * than this ratio, the algorithm first performs MinMax preselection and then
     * runs LTTB on the reduced set.
     * \param elapsedNs Optional output parameter that receives the total
     * execution time in nanoseconds.
     * \return Source indexes in ascending order for the selected points. For
     * MinMax and M4 with x-gaps, the returned size can be smaller than nOut.
     * Returns an empty QVector when the input is invalid.
     */
    static QVector<qsizetype> downsample(
        const QVector<double>& x,
        const QVector<double>& y,
        qsizetype nOut,
        DownsampleAlgorithm algorithm,
        qsizetype minmaxRatio = 4,
        qint64* elapsedNs = nullptr);
};
