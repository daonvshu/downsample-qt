#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>
#include <QVector>
#include <QTextStream>

#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

#include "spscdataringbuffer.h"
#include "downsampler.h"

struct SamplePoint
{
    qint64 sequence;
    float value;
};

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename F>
void benchmarkDownsample(const char* testName, F&& func)
{
    constexpr int warmupRuns = 5;
    constexpr int measuredRuns = 1000;

    qint64 elapsedNs = 0;
    for (int i = 0; i < warmupRuns; ++i) {
        func(&elapsedNs);
    }

    qint64 totalElapsedNs = 0;
    qint64 minElapsedNs = std::numeric_limits<qint64>::max();
    qint64 maxElapsedNs = 0;
    for (int i = 0; i < measuredRuns; ++i) {
        func(&elapsedNs);
        totalElapsedNs += elapsedNs;
        minElapsedNs = qMin(minElapsedNs, elapsedNs);
        maxElapsedNs = qMax(maxElapsedNs, elapsedNs);
    }

    qDebug().noquote()
        << QStringLiteral("%1 avgNs=%2 minNs=%3 maxNs=%4 runs=%5")
               .arg(QString::fromLatin1(testName))
               .arg(totalElapsedNs / measuredRuns)
               .arg(minElapsedNs)
               .arg(maxElapsedNs)
               .arg(measuredRuns);
}

QVector<SamplePoint> makeSamples(qint64 baseSequence, qsizetype count)
{
    QVector<SamplePoint> samples(count);
    for (qsizetype i = 0; i < count; ++i) {
        samples[i] = SamplePoint{
            baseSequence + i,
            static_cast<float>(baseSequence) + static_cast<float>(i) * 0.5F,
        };
    }

    return samples;
}

void testBasicReadWrite()
{
    SpscDataRingBuffer<SamplePoint> buffer(4, 8);
    const auto samples = makeSamples(10, 3);

    require(buffer.push(samples.constData(), samples.size()), "basic push should succeed");

    QVector<SamplePoint> latest;
    require(buffer.tryReadLatest(latest), "basic read should succeed");
    require(latest.size() == samples.size(), "basic read size mismatch");

    for (qsizetype i = 0; i < samples.size(); ++i) {
        require(latest[i].sequence == samples[i].sequence, "basic read sequence mismatch");
        require(latest[i].value == samples[i].value, "basic read value mismatch");
    }

    require(!buffer.tryReadLatest(latest), "no unread data should remain after consume");
}

void testLatestWinsWhenProducerOverruns()
{
    SpscDataRingBuffer<SamplePoint> buffer(2, 8);

    const auto first = makeSamples(100, 2);
    const auto second = makeSamples(200, 2);
    const auto third = makeSamples(300, 2);

    require(buffer.push(first.constData(), first.size()), "first push should succeed");
    require(buffer.push(second.constData(), second.size()), "second push should succeed");
    require(buffer.push(third.constData(), third.size()), "third push should succeed");

    QVector<SamplePoint> latest;
    require(buffer.tryReadLatest(latest), "latest read after overrun should succeed");
    require(latest.size() == third.size(), "latest overrun size mismatch");

    for (qsizetype i = 0; i < third.size(); ++i) {
        require(latest[i].sequence == third[i].sequence, "overrun sequence mismatch");
        require(latest[i].value == third[i].value, "overrun value mismatch");
    }
}

void testConcurrentProducerConsumer()
{
    constexpr qsizetype totalWrites = 2000;
    constexpr qsizetype elementsPerWrite = 6;

    SpscDataRingBuffer<SamplePoint> buffer(8, elementsPerWrite);
    std::atomic<bool> producerDone{false};
    std::atomic<qint64> lastReadSequence{-1};

    std::thread producer([&buffer, &producerDone]() {
        for (qsizetype batch = 0; batch < totalWrites; ++batch) {
            auto payload = makeSamples(batch * 100, elementsPerWrite);
            require(buffer.push(payload.constData(), payload.size()), "concurrent push should succeed");
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&buffer, &producerDone, &lastReadSequence]() {
        QVector<SamplePoint> latest;

        while (!producerDone.load(std::memory_order_acquire)) {
            if (buffer.tryReadLatest(latest) && !latest.empty()) {
                const qint64 currentSequence = latest.back().sequence;
                const qint64 previousSequence = lastReadSequence.exchange(currentSequence, std::memory_order_acq_rel);
                require(currentSequence >= previousSequence, "consumer observed sequence regression");
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }

        while (buffer.tryReadLatest(latest)) {
            if (!latest.empty()) {
                const qint64 currentSequence = latest.back().sequence;
                const qint64 previousSequence = lastReadSequence.exchange(currentSequence, std::memory_order_acq_rel);
                require(currentSequence >= previousSequence, "final drain observed sequence regression");
            }
        }
    });

    producer.join();
    consumer.join();

    require(lastReadSequence.load(std::memory_order_acquire) >= 0, "consumer never received any payload");
}

void testMinMaxWithoutXMatchesTsDownsample()
{
    QVector<double> y(100);
    for (qsizetype i = 0; i < y.size(); ++i) {
        y[i] = static_cast<double>(i);
    }

    const QVector<qsizetype> expected{0, 19, 20, 39, 40, 59, 60, 79, 80, 99};
    benchmarkDownsample("testMinMaxWithoutXMatchesTsDownsample", [&y, &expected](qint64* elapsedNs) {
        const auto indices = Downsampler::downsample(y, 10, DownsampleAlgorithm::MinMax, 4, elapsedNs);
        require(indices == expected, "MinMax without x mismatch");
    });
}

void testM4WithoutXMatchesTsDownsample()
{
    QVector<double> y(100);
    for (qsizetype i = 0; i < y.size(); ++i) {
        y[i] = static_cast<double>(i);
    }

    const QVector<qsizetype> expected{0, 0, 33, 33, 34, 34, 66, 66, 67, 67, 99, 99};
    benchmarkDownsample("testM4WithoutXMatchesTsDownsample", [&y, &expected](qint64* elapsedNs) {
        const auto indices = Downsampler::downsample(y, 12, DownsampleAlgorithm::M4, 4, elapsedNs);
        require(indices == expected, "M4 without x mismatch");
    });
}

void testLttbWithoutXMatchesTsDownsample()
{
    const QVector<double> y{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    const QVector<qsizetype> expected{0, 1, 5, 9};
    benchmarkDownsample("testLttbWithoutXMatchesTsDownsample", [&y, &expected](qint64* elapsedNs) {
        const auto indices = Downsampler::downsample(y, 4, DownsampleAlgorithm::Lttb, 4, elapsedNs);
        require(indices == expected, "LTTB without x mismatch");
    });
}

void testMinMaxLttbWithoutXMatchesTsDownsample()
{
    const QVector<double> y{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    const QVector<qsizetype> expected{0, 1, 5, 9};
    benchmarkDownsample("testMinMaxLttbWithoutXMatchesTsDownsample", [&y, &expected](qint64* elapsedNs) {
        const auto indices =
            Downsampler::downsample(y, 4, DownsampleAlgorithm::MinMaxLttb, 2, elapsedNs);
        require(indices == expected, "MinMaxLTTB without x mismatch");
    });
}

void testMinMaxWithXGapMatchesTsDownsample()
{
    QVector<double> x(100);
    QVector<double> y(100);
    for (qsizetype i = 0; i < 100; ++i) {
        x[i] = i > 50 ? static_cast<double>(i + 50) : static_cast<double>(i);
        y[i] = static_cast<double>(i);
    }

    const QVector<qsizetype> expected{0, 29, 30, 50, 51, 69, 70, 99};
    benchmarkDownsample("testMinMaxWithXGapMatchesTsDownsample", [&x, &y, &expected](qint64* elapsedNs) {
        const auto indices = Downsampler::downsample(x, y, 10, DownsampleAlgorithm::MinMax, 4, elapsedNs);
        require(indices == expected, "MinMax with x gap mismatch");
    });
}

void testM4WithXGapMatchesTsDownsample()
{
    QVector<double> x(100);
    QVector<double> y(100);
    for (qsizetype i = 0; i < 100; ++i) {
        x[i] = i > 50 ? static_cast<double>(i + 50) : static_cast<double>(i);
        y[i] = static_cast<double>(i);
    }

    const QVector<qsizetype> expected{0, 0, 29, 29, 30, 30, 50, 50, 51, 51, 69, 69, 70, 70, 99, 99};
    benchmarkDownsample("testM4WithXGapMatchesTsDownsample", [&x, &y, &expected](qint64* elapsedNs) {
        const auto indices = Downsampler::downsample(x, y, 20, DownsampleAlgorithm::M4, 4, elapsedNs);
        require(indices == expected, "M4 with x gap mismatch");
    });
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream stream(stdout);
    QTextStream errorStream(stderr);

    try {
        testBasicReadWrite();
        stream << "PASS testBasicReadWrite" << Qt::endl;

        testLatestWinsWhenProducerOverruns();
        stream << "PASS testLatestWinsWhenProducerOverruns" << Qt::endl;

        testConcurrentProducerConsumer();
        stream << "PASS testConcurrentProducerConsumer" << Qt::endl;

        testMinMaxWithoutXMatchesTsDownsample();
        stream << "PASS testMinMaxWithoutXMatchesTsDownsample" << Qt::endl;

        testM4WithoutXMatchesTsDownsample();
        stream << "PASS testM4WithoutXMatchesTsDownsample" << Qt::endl;

        testLttbWithoutXMatchesTsDownsample();
        stream << "PASS testLttbWithoutXMatchesTsDownsample" << Qt::endl;

        testMinMaxLttbWithoutXMatchesTsDownsample();
        stream << "PASS testMinMaxLttbWithoutXMatchesTsDownsample" << Qt::endl;

        testMinMaxWithXGapMatchesTsDownsample();
        stream << "PASS testMinMaxWithXGapMatchesTsDownsample" << Qt::endl;

        testM4WithXGapMatchesTsDownsample();
        stream << "PASS testM4WithXGapMatchesTsDownsample" << Qt::endl;
    } catch (const std::exception& exception) {
        errorStream << "FAIL " << exception.what() << Qt::endl;
        return 1;
    }

    return 0;
}
