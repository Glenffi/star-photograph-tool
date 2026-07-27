#pragma once

#include "core/SkyGroundMask.h"
#include "core/StarReducer.h"

#include <QThread>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <vector>

class ProcessingWorker : public QThread {
    Q_OBJECT

public:
    struct Params {
        QString stackMethod = "average";
        double kappaValue = 2.5;
        bool dewarpEnabled = false;
        int dewarpStrength = 30;
        bool stretchEnabled = false;
        bool starReduceEnabled = false;
        int starReduceStrength = 70;
        QString outputFormat = "tiff16";
        QString outputPath;
        bool skyGroundSepEnabled = false;
        SkyGroundMask::Mode skyGroundMode = SkyGroundMask::AutoDetect;
        QString userMaskPath;
        int featherRadius = 20;
        uint64_t memoryBudgetBytes = 0; // 0 selects the platform recommendation.
    };

    ProcessingWorker(const QStringList& files, const QString& referenceFrame,
                     const Params& params, QObject* parent = nullptr);

    // These accessors are called by the GUI only after QThread::finished.
    std::vector<uint16_t> takeStackedData();
    int stackedWidth() const { return m_width; }
    int stackedHeight() const { return m_height; }
    int cropOffsetX() const { return m_cropOffsetX; }
    int cropOffsetY() const { return m_cropOffsetY; }
    int stackedFrameCount() const { return m_frameCount; }
    QString errorString() const { return m_errorString; }
    QString outputFile() const { return m_outputFile; }
    int affineFrameCount() const { return m_affineFrameCount; }
    int homographyFrameCount() const { return m_homographyFrameCount; }
    double averageAlignmentRms() const;
    double worstAlignmentP95() const { return m_worstAlignmentP95; }
    double minimumAlignmentGridCoverage() const { return m_minimumGridCoverage; }
    qint64 stackingElapsedMs() const { return m_stackingElapsedMs; }
    bool wasCancelled() const { return m_wasCancelled; }
    const StarReductionStats& starReductionStats() const {
        return m_starReductionStats;
    }

    void requestCancel();

signals:
    void progress(int value);
    void stageMessage(const QString& message);

protected:
    void run() override;

private:
    bool stopIfCancelled();

    QStringList m_files;
    QString m_referenceFrame;
    Params m_params;
    std::vector<uint16_t> m_stackedData;
    int m_width = 0;
    int m_height = 0;
    int m_cropOffsetX = 0;
    int m_cropOffsetY = 0;
    int m_frameCount = 0;
    QString m_errorString;
    QString m_outputFile;
    int m_affineFrameCount = 0;
    int m_homographyFrameCount = 0;
    double m_alignmentRmsSum = 0.0;
    double m_worstAlignmentP95 = 0.0;
    double m_minimumGridCoverage = 0.0;
    qint64 m_stackingElapsedMs = 0;
    std::atomic<bool> m_cancelRequested{false};
    bool m_wasCancelled = false;
    StarReductionStats m_starReductionStats;
};
