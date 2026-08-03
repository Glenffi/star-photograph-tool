#pragma once

#include "core/FrameQualityEvaluator.h"
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
        bool autoRejectLowQualityFrames = true;
        bool photometricNormalizationEnabled = true;
        bool noiseReductionEnabled = false;
        int noiseReductionStrength = 30;
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
        // Optional diagnostic artifact. The GUI leaves this empty; the sample
        // runner uses it to preserve the exact mask consumed by stacking.
        QString skyGroundMaskOutputPath;
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
    int selectedReferenceIndex() const { return m_selectedReferenceIndex; }
    QString selectedReferenceFrame() const { return m_selectedReferenceFrame; }
    const std::vector<FrameQualityMetrics>& frameQualityMetrics() const {
        return m_frameQualityMetrics;
    }
    QStringList qualityRejectedFiles() const { return m_qualityRejectedFiles; }
    QString errorString() const { return m_errorString; }
    QString outputFile() const { return m_outputFile; }
    int affineFrameCount() const { return m_affineFrameCount; }
    int homographyFrameCount() const { return m_homographyFrameCount; }
    double averageAlignmentRms() const;
    double worstAlignmentP95() const { return m_worstAlignmentP95; }
    double minimumAlignmentGridCoverage() const { return m_minimumGridCoverage; }
    qint64 stackingElapsedMs() const { return m_stackingElapsedMs; }
    int photometricNormalizedFrameCount() const {
        return m_photometricNormalizedFrameCount;
    }
    int photometricSkippedFrameCount() const {
        return m_photometricSkippedFrameCount;
    }
    double averagePhotometricGain() const;
    double minimumPhotometricGain() const { return m_photometricMinGain; }
    double maximumPhotometricGain() const { return m_photometricMaxGain; }
    double maximumPhotometricOffset() const { return m_photometricMaxAbsOffset; }
    double photometricOutputAnchorGain() const {
        return m_photometricOutputAnchorGain;
    }
    double photometricOutputAnchorOffset() const {
        return m_photometricOutputAnchorMaxAbsOffset;
    }
    bool wasCancelled() const { return m_wasCancelled; }
    const StarReductionStats& starReductionStats() const {
        return m_starReductionStats;
    }
    double skyGroundSkyFraction() const { return m_skyGroundSkyFraction; }
    QString skyGroundMaskSource() const { return m_skyGroundMaskSource; }

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
    int m_selectedReferenceIndex = -1;
    QString m_selectedReferenceFrame;
    std::vector<FrameQualityMetrics> m_frameQualityMetrics;
    QStringList m_qualityRejectedFiles;
    QString m_errorString;
    QString m_outputFile;
    int m_affineFrameCount = 0;
    int m_homographyFrameCount = 0;
    double m_alignmentRmsSum = 0.0;
    double m_worstAlignmentP95 = 0.0;
    double m_minimumGridCoverage = 0.0;
    qint64 m_stackingElapsedMs = 0;
    int m_photometricNormalizedFrameCount = 0;
    int m_photometricSkippedFrameCount = 0;
    double m_photometricGainSum = 0.0;
    double m_photometricMinGain = 1.0;
    double m_photometricMaxGain = 1.0;
    double m_photometricMaxAbsOffset = 0.0;
    double m_photometricOutputAnchorGain = 1.0;
    double m_photometricOutputAnchorMaxAbsOffset = 0.0;
    std::atomic<bool> m_cancelRequested{false};
    bool m_wasCancelled = false;
    StarReductionStats m_starReductionStats;
    double m_skyGroundSkyFraction = 0.0;
    QString m_skyGroundMaskSource;
};
