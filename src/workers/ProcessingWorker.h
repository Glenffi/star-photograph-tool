#pragma once

#include "core/FrameQualityEvaluator.h"
#include "core/RawCalibrationEngine.h"
#include "core/SkyGroundMask.h"
#include "core/StarReducer.h"

#include <QThread>
#include <QImage>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <vector>

class ProcessingWorker : public QThread {
    Q_OBJECT

public:
    struct Params {
        // Single-frame refinement bypasses quality selection, alignment and
        // stacking while retaining the same finishing and export stages.
        bool singleFrameMode = false;
        bool timelapseMode = false;
        bool deepSkyMode = false;
        QStringList darkFramePaths;
        QStringList flatFramePaths;
        QStringList biasFramePaths;
        int timelapseWindowSize = 5;
        int timelapseStrength = 80;
        int timelapseMotionProtection = 75;
        bool timelapseProtectGround = true;
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
        QString groundStackMethod = "average";
        int groundDetailStrength = 40;
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
    QImage takeBeforePreview();
    uint16_t beforePreviewBlackPoint() const { return m_beforePreviewBlackPoint; }
    uint16_t beforePreviewWhitePoint() const { return m_beforePreviewWhitePoint; }
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
    uint64_t timelapseMotionProtectedPixelEvaluations() const {
        return m_timelapseMotionProtectedPixelEvaluations;
    }
    int timelapseFlickerCorrectedFrames() const {
        return m_timelapseFlickerCorrectedFrames;
    }
    double timelapseMaximumFlickerGainChange() const {
        return m_timelapseMaximumFlickerGainChange;
    }
    double timelapseMaximumFlickerOffset() const {
        return m_timelapseMaximumFlickerOffset;
    }
    int calibratedLightFrameCount() const {
        return m_calibratedLightFrameCount;
    }
    uint64_t calibrationClippedLowPixels() const {
        return m_calibrationClippedLowPixels;
    }
    uint64_t calibrationClippedHighPixels() const {
        return m_calibrationClippedHighPixels;
    }
    uint64_t calibrationInvalidFlatPixels() const {
        return m_calibrationInvalidFlatPixels;
    }

    void requestCancel();

signals:
    void progress(int value);
    void stageMessage(const QString& message);

protected:
    void run() override;

private:
    bool stopIfCancelled();
    void runSingleFrame();
    void runTimelapse();
    bool buildDeepSkyCalibration(
        RawImageLoader& loader,
        const RawImageLoader::CfaImageData& referenceLight,
        RawCalibrationEngine::MasterFrames& masters);
    bool loadCalibratedRaw(
        RawImageLoader& loader, const QString& path,
        const RawCalibrationEngine::MasterFrames& masters,
        RawImageLoader::ImageData& image);
    bool finishResult(std::vector<uint16_t>& resultRgb, int width, int height,
                      std::vector<uint8_t>& mask);

    QStringList m_files;
    QString m_referenceFrame;
    Params m_params;
    std::vector<uint16_t> m_stackedData;
    // Bounded 8-bit preview captured after stacking/cropping and before the
    // optional finishing stages. It enables comparison without retaining a
    // second full-resolution 16-bit RGB frame in memory.
    QImage m_beforePreview;
    uint16_t m_beforePreviewBlackPoint = 0;
    uint16_t m_beforePreviewWhitePoint = 65535;
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
    uint64_t m_timelapseMotionProtectedPixelEvaluations = 0;
    int m_timelapseFlickerCorrectedFrames = 0;
    double m_timelapseMaximumFlickerGainChange = 0.0;
    double m_timelapseMaximumFlickerOffset = 0.0;
    int m_calibratedLightFrameCount = 0;
    uint64_t m_calibrationClippedLowPixels = 0;
    uint64_t m_calibrationClippedHighPixels = 0;
    uint64_t m_calibrationInvalidFlatPixels = 0;
};
