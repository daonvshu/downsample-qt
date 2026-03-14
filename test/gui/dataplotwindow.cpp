#include "dataplotwindow.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <cstring>

namespace
{
constexpr qsizetype LoaderChunkSamples = 1 << 16;
constexpr int PollIntervalMs = 50;
constexpr int ReplotDebounceMs = 30;
constexpr qsizetype MinimumRenderSamples = 400;
}

DataPlotWindow::DataPlotWindow(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    setupPlot();

    m_pollTimer.setInterval(PollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &DataPlotWindow::pollLoadingState);
    m_pollTimer.start();

    m_replotDebounceTimer.setSingleShot(true);
    m_replotDebounceTimer.setInterval(ReplotDebounceMs);
    connect(&m_replotDebounceTimer, &QTimer::timeout, this, &DataPlotWindow::updatePlotForCurrentView);
}

DataPlotWindow::~DataPlotWindow()
{
    stopLoader();
}

void DataPlotWindow::setupUi()
{
    setWindowTitle(QStringLiteral("Downsample GUI Test"));
    resize(1280, 720);

    auto* rootLayout = new QVBoxLayout(this);
    auto* topBarLayout = new QHBoxLayout();

    auto* openButton = new QPushButton(QStringLiteral("Open Binary Double File"), this);
    auto* algorithmComboBox = new QComboBox(this);
    auto* statusLabel = new QLabel(QStringLiteral("Select a file to begin."), this);
    auto* metricsLabel = new QLabel(QStringLiteral("No data loaded."), this);
    statusLabel->setWordWrap(true);
    metricsLabel->setWordWrap(true);

    algorithmComboBox->addItem(QStringLiteral("MinMax"), static_cast<int>(DownsampleAlgorithm::MinMax));
    algorithmComboBox->addItem(QStringLiteral("M4"), static_cast<int>(DownsampleAlgorithm::M4));
    algorithmComboBox->addItem(QStringLiteral("LTTB"), static_cast<int>(DownsampleAlgorithm::Lttb));
    algorithmComboBox->addItem(QStringLiteral("MinMaxLTTB"), static_cast<int>(DownsampleAlgorithm::MinMaxLttb));
    algorithmComboBox->setCurrentIndex(3);

    topBarLayout->addWidget(openButton);
    topBarLayout->addWidget(algorithmComboBox);
    topBarLayout->addWidget(statusLabel, 1);
    topBarLayout->addWidget(metricsLabel, 1);

    auto* plot = new QCustomPlot(this);

    rootLayout->addLayout(topBarLayout);
    rootLayout->addWidget(plot, 1);

    m_openButton = openButton;
    m_statusLabel = statusLabel;
    m_metricsLabel = metricsLabel;
    m_algorithmComboBox = algorithmComboBox;
    m_plot = plot;

    connect(openButton, &QPushButton::clicked, this, &DataPlotWindow::openFile);
    connect(algorithmComboBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &DataPlotWindow::schedulePlotUpdate);
}

void DataPlotWindow::setupPlot()
{
    m_plot->addGraph();
    m_plot->legend->setVisible(false);
    m_plot->xAxis->setLabel(QStringLiteral("Sample Index"));
    m_plot->yAxis->setLabel(QStringLiteral("Value"));
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->axisRect()->setRangeDrag(Qt::Horizontal);
    m_plot->axisRect()->setRangeZoom(Qt::Horizontal);

    connect(m_plot->xAxis, qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
            this, &DataPlotWindow::schedulePlotUpdate);
}

void DataPlotWindow::openFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Binary Double File"),
        QString(),
        QStringLiteral("Binary Files (*.bin *.dat *.raw);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    startLoader(filePath);
}

void DataPlotWindow::resetState()
{
    m_loadingFinished.store(false, std::memory_order_release);
    m_loading.store(false, std::memory_order_release);
    m_loadedSamples.store(0, std::memory_order_release);
    m_totalSamples.store(0, std::memory_order_release);
    m_lastErrorFlag.store(0, std::memory_order_release);
    m_lastErrorMessage.clear();
    m_currentFilePath.clear();
    std::atomic_store_explicit(&m_loadedData, std::shared_ptr<const QVector<double>>{}, std::memory_order_release);

    QVector<PreviewChunkStats> ignored;
    while (m_previewRingBuffer.tryReadLatest(ignored)) {
    }

    m_plot->graph(0)->data()->clear();
    m_plot->replot();
    updateMetricsText(0, 0);
}

void DataPlotWindow::stopLoader()
{
    m_stopRequested.store(true, std::memory_order_release);
    if (m_loaderThread.joinable()) {
        m_loaderThread.join();
    }
    m_stopRequested.store(false, std::memory_order_release);
}

void DataPlotWindow::startLoader(const QString& filePath)
{
    stopLoader();
    resetState();
    m_currentFilePath = filePath;

    updateStatusText(QStringLiteral("Loading %1 ...").arg(filePath));
    m_loading.store(true, std::memory_order_release);

    std::error_code errorCode;
    const auto fileSize = std::filesystem::file_size(filePath.toStdWString(), errorCode);
    if (!errorCode) {
        m_totalSamples.store(static_cast<qint64>(fileSize / sizeof(double)), std::memory_order_release);
    }

    m_loaderThread = std::thread([this, filePath]() {
        std::ifstream input(filePath.toStdWString(), std::ios::binary);
        if (!input) {
            m_lastErrorMessage = QStringLiteral("Failed to open file: %1").arg(filePath);
            m_lastErrorFlag.store(1, std::memory_order_release);
            m_loading.store(false, std::memory_order_release);
            m_loadingFinished.store(true, std::memory_order_release);
            return;
        }

        auto fullData = std::make_shared<QVector<double>>();
        const qint64 totalSamples = m_totalSamples.load(std::memory_order_acquire);
        if (totalSamples > 0 && totalSamples <= std::numeric_limits<qsizetype>::max()) {
            fullData->reserve(static_cast<qsizetype>(totalSamples));
        }

        QVector<double> chunk(LoaderChunkSamples);
        qint64 sampleOffset = 0;

        while (!m_stopRequested.load(std::memory_order_acquire) && input) {
            input.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size() * sizeof(double)));
            const std::streamsize readBytes = input.gcount();
            if (readBytes <= 0) {
                break;
            }

            const qsizetype readSamples = static_cast<qsizetype>(readBytes / static_cast<std::streamsize>(sizeof(double)));
            if (readSamples <= 0) {
                break;
            }

            const qsizetype previousSize = fullData->size();
            fullData->resize(previousSize + readSamples);
            std::memcpy(fullData->data() + previousSize,
                        chunk.constData(),
                        static_cast<std::size_t>(readSamples) * sizeof(double));

            PreviewChunkStats stats;
            stats.chunkStartIndex = sampleOffset;
            stats.chunkSize = readSamples;
            stats.minValue = chunk[0];
            stats.maxValue = chunk[0];
            for (qsizetype i = 1; i < readSamples; ++i) {
                stats.minValue = std::min(stats.minValue, chunk[i]);
                stats.maxValue = std::max(stats.maxValue, chunk[i]);
            }

            QVector<PreviewChunkStats> previewBlock(1);
            previewBlock[0] = stats;
            m_previewRingBuffer.push(previewBlock);

            sampleOffset += readSamples;
            m_loadedSamples.store(sampleOffset, std::memory_order_release);
        }

        std::atomic_store_explicit(&m_loadedData, std::shared_ptr<const QVector<double>>(fullData), std::memory_order_release);
        m_loading.store(false, std::memory_order_release);
        m_loadingFinished.store(true, std::memory_order_release);
    });
}

void DataPlotWindow::pollLoadingState()
{
    QVector<PreviewChunkStats> preview;
    if (m_previewRingBuffer.tryReadLatest(preview) && !preview.isEmpty()) {
        const auto& stats = preview.constFirst();
        updateStatusText(QStringLiteral("Loading... samples=%1 latestChunk=[%2, %3] min=%4 max=%5")
                             .arg(m_loadedSamples.load(std::memory_order_acquire))
                             .arg(stats.chunkStartIndex)
                             .arg(stats.chunkStartIndex + stats.chunkSize - 1)
                             .arg(stats.minValue)
                             .arg(stats.maxValue));
    }

    finishLoadingIfReady();
}

void DataPlotWindow::finishLoadingIfReady()
{
    if (!m_loadingFinished.load(std::memory_order_acquire)) {
        return;
    }

    m_loadingFinished.store(false, std::memory_order_release);

    if (m_lastErrorFlag.load(std::memory_order_acquire) != 0) {
        updateStatusText(m_lastErrorMessage);
        stopLoader();
        return;
    }

    auto data = std::atomic_load_explicit(&m_loadedData, std::memory_order_acquire);
    if (!data || data->isEmpty()) {
        updateStatusText(QStringLiteral("No samples loaded."));
        stopLoader();
        return;
    }

    updateStatusText(QStringLiteral("Loaded %1 samples. Use mouse wheel to zoom and drag horizontally.")
                         .arg(data->size()));
    updateMetricsText(data->size(), 0);

    m_plot->xAxis->setRange(0, data->size() - 1);
    schedulePlotUpdate();
}

void DataPlotWindow::schedulePlotUpdate()
{
    if (m_loading.load(std::memory_order_acquire)) {
        return;
    }

    m_plotUpdatePending = true;
    m_replotDebounceTimer.start();
}

void DataPlotWindow::updatePlotForCurrentView()
{
    if (!m_plotUpdatePending) {
        return;
    }
    m_plotUpdatePending = false;

    const auto data = std::atomic_load_explicit(&m_loadedData, std::memory_order_acquire);
    if (!data || data->isEmpty()) {
        return;
    }

    const qsizetype dataSize = data->size();
    qsizetype startIndex = std::max<qsizetype>(0, static_cast<qsizetype>(std::floor(m_plot->xAxis->range().lower)));
    qsizetype endIndex = std::min<qsizetype>(dataSize, static_cast<qsizetype>(std::ceil(m_plot->xAxis->range().upper)) + 1);

    if (endIndex <= startIndex) {
        startIndex = 0;
        endIndex = dataSize;
    }

    renderSubset(startIndex, endIndex);
}

void DataPlotWindow::updateStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

void DataPlotWindow::updateMetricsText(qsizetype visibleSamples, qsizetype renderedSamples)
{
    const auto data = std::atomic_load_explicit(&m_loadedData, std::memory_order_acquire);
    const qsizetype totalSamples = data ? data->size() : 0;
    const QString algorithmName = m_algorithmComboBox ? m_algorithmComboBox->currentText() : QStringLiteral("-");

    m_metricsLabel->setText(
        QStringLiteral("file=%1 total=%2 visible=%3 rendered=%4 algorithm=%5")
            .arg(m_currentFilePath.isEmpty() ? QStringLiteral("-") : m_currentFilePath)
            .arg(totalSamples)
            .arg(visibleSamples)
            .arg(renderedSamples)
            .arg(algorithmName));
}

DownsampleAlgorithm DataPlotWindow::currentAlgorithm() const
{
    if (!m_algorithmComboBox) {
        return DownsampleAlgorithm::MinMaxLttb;
    }

    return static_cast<DownsampleAlgorithm>(m_algorithmComboBox->currentData().toInt());
}

void DataPlotWindow::renderSubset(qsizetype startIndex, qsizetype endIndex)
{
    const auto data = std::atomic_load_explicit(&m_loadedData, std::memory_order_acquire);
    if (!data || endIndex <= startIndex) {
        return;
    }

    QVector<double> visibleY;
    visibleY.reserve(endIndex - startIndex);
    for (qsizetype i = startIndex; i < endIndex; ++i) {
        visibleY.append((*data)[i]);
    }

    const qsizetype visibleCount = visibleY.size();
    const qsizetype targetSamples =
        std::max<qsizetype>(MinimumRenderSamples, static_cast<qsizetype>(m_plot->viewport().width()) * 2);

    QVector<double> plotX;
    QVector<double> plotY;
    qsizetype renderedSamples = 0;

    if (visibleCount <= targetSamples) {
        plotX.reserve(visibleCount);
        plotY.reserve(visibleCount);
        for (qsizetype i = 0; i < visibleCount; ++i) {
            plotX.append(startIndex + i);
            plotY.append(visibleY[i]);
        }
        renderedSamples = visibleCount;
    } else {
        const DownsampleAlgorithm algorithm = currentAlgorithm();
        const qsizetype adjustedTargetSamples =
            algorithm == DownsampleAlgorithm::M4 ? std::max<qsizetype>(4, (targetSamples / 4) * 4) :
            algorithm == DownsampleAlgorithm::MinMax ? std::max<qsizetype>(2, (targetSamples / 2) * 2) :
            std::max<qsizetype>(3, targetSamples);
        const QVector<qsizetype> selected =
            Downsampler::downsample(visibleY, adjustedTargetSamples, algorithm, 4);
        plotX.reserve(selected.size());
        plotY.reserve(selected.size());
        for (qsizetype localIndex : selected) {
            plotX.append(startIndex + localIndex);
            plotY.append(visibleY[localIndex]);
        }
        renderedSamples = selected.size();
    }

    m_plot->graph(0)->setData(plotX, plotY, true);
    m_plot->graph(0)->setPen(QPen(QColor(20, 120, 220), 1.0));
    m_plot->xAxis->setRange(startIndex, endIndex - 1);

    auto minmax = std::minmax_element(plotY.begin(), plotY.end());
    if (minmax.first != plotY.end()) {
        double yMin = *minmax.first;
        double yMax = *minmax.second;
        if (qFuzzyCompare(yMin, yMax)) {
            yMin -= 1.0;
            yMax += 1.0;
        }
        const double margin = (yMax - yMin) * 0.05;
        m_plot->yAxis->setRange(yMin - margin, yMax + margin);
    }

    updateMetricsText(visibleCount, renderedSamples);
    m_plot->replot();
}
