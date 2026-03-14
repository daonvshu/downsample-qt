#pragma once

#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <atomic>
#include <memory>
#include <thread>

#include "downsampler.h"
#include "qcustomplot.h"
#include "spscdataringbuffer.h"

class QLabel;
class QComboBox;
class QPushButton;

class DataPlotWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit DataPlotWindow(QWidget* parent = nullptr);
    ~DataPlotWindow() override;

private slots:
    void openFile();
    void pollLoadingState();
    void schedulePlotUpdate();
    void updatePlotForCurrentView();

private:
    struct PreviewChunkStats
    {
        qint64 chunkStartIndex = 0;
        qsizetype chunkSize = 0;
        double minValue = 0.0;
        double maxValue = 0.0;
    };

    void setupUi();
    void setupPlot();
    void resetState();
    void stopLoader();
    void startLoader(const QString& filePath);
    void finishLoadingIfReady();
    void updateStatusText(const QString& text);
    void updateMetricsText(qsizetype visibleSamples, qsizetype renderedSamples);
    void renderSubset(qsizetype startIndex, qsizetype endIndex);
    DownsampleAlgorithm currentAlgorithm() const;

    QPointer<QPushButton> m_openButton;
    QPointer<QLabel> m_statusLabel;
    QPointer<QLabel> m_metricsLabel;
    QPointer<QComboBox> m_algorithmComboBox;
    QPointer<QCustomPlot> m_plot;
    QTimer m_pollTimer;
    QTimer m_replotDebounceTimer;

    std::thread m_loaderThread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_loading{false};
    std::atomic<bool> m_loadingFinished{false};
    std::atomic<qint64> m_loadedSamples{0};
    std::atomic<qint64> m_totalSamples{0};
    std::atomic<qint64> m_lastErrorFlag{0};
    QString m_lastErrorMessage;
    QString m_currentFilePath;

    std::shared_ptr<const QVector<double>> m_loadedData;
    SpscDataRingBuffer<PreviewChunkStats> m_previewRingBuffer{16, 1};

    bool m_plotUpdatePending = false;
};
