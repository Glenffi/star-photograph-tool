#include "core/ProcessingMemoryEstimator.h"
#include "core/RawImageLoader.h"
#include "workers/ProcessingWorker.h"
#include "workers/QuickPreviewWorker.h"
#include "workers/ExportWorker.h"
#include "workers/HistoryPreviewWorker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPoint>
#include <QTemporaryDir>
#include <QTranslator>

#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
}

using Phase = ProcessingWorker::Phase;
constexpr int kDngWidth = 512;
constexpr int kDngHeight = 384;

// Paint in display coordinates, then store the inverse rotation in the RAW.
bool writeSyntheticDng(const QString& path, uint16_t orientation, int second,
                       int starOffsetX, int groundLevel) {
#ifdef Q_OS_WIN
    TIFF* handle = TIFFOpenW(path.toStdWString().c_str(), "w");
#else
    TIFF* handle = TIFFOpen(QFile::encodeName(path).constData(), "w");
#endif
    const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiff(handle, TIFFClose);
    if (!tiff) return false;

    const uint8_t version[] = {1, 4, 0, 0};
    const uint16_t cfaDimensions[] = {2, 2};
    const uint8_t cfaPattern[] = {0, 1, 1, 2};
    const uint32_t whiteLevel = 16383;
    const float colorMatrix[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float neutral[] = {1, 1, 1};
    const QByteArray timestamp = QByteArray("2026:08:31 21:00:") +
        QByteArray::number(second).rightJustified(2, '0');
    if (!TIFFSetField(handle, TIFFTAG_IMAGEWIDTH, kDngWidth) ||
        !TIFFSetField(handle, TIFFTAG_IMAGELENGTH, kDngHeight) ||
        !TIFFSetField(handle, TIFFTAG_BITSPERSAMPLE, 16) ||
        !TIFFSetField(handle, TIFFTAG_SAMPLESPERPIXEL, 1) ||
        !TIFFSetField(handle, TIFFTAG_COMPRESSION, COMPRESSION_NONE) ||
        !TIFFSetField(handle, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA) ||
        !TIFFSetField(handle, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) ||
        !TIFFSetField(handle, TIFFTAG_ROWSPERSTRIP, kDngHeight) ||
        !TIFFSetField(handle, TIFFTAG_ORIENTATION, orientation) ||
        !TIFFSetField(handle, TIFFTAG_DATETIME, timestamp.constData()) ||
        !TIFFSetField(handle, TIFFTAG_DNGVERSION, version) ||
        !TIFFSetField(handle, TIFFTAG_UNIQUECAMERAMODEL, "Nebulens Synthetic Bayer") ||
        !TIFFSetField(handle, TIFFTAG_CFAREPEATPATTERNDIM, cfaDimensions) ||
        !TIFFSetField(handle, TIFFTAG_CFAPATTERN, 4, cfaPattern) ||
        !TIFFSetField(handle, TIFFTAG_WHITELEVEL, 1, &whiteLevel) ||
        !TIFFSetField(handle, TIFFTAG_COLORMATRIX1, 9, colorMatrix) ||
        !TIFFSetField(handle, TIFFTAG_ASSHOTNEUTRAL, 3, neutral)) {
        return false;
    }

    const bool portrait = orientation == ORIENTATION_RIGHTTOP ||
                          orientation == ORIENTATION_LEFTBOT;
    const int width = portrait ? kDngHeight : kDngWidth;
    const int height = portrait ? kDngWidth : kDngHeight;
    std::vector<uint16_t> scene(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            scene[static_cast<size_t>(y) * width + x] =
                (y < height * 2 / 3 ? 1400 : groundLevel) + x / 12;
        }
    }
    // Every star remains visible after either of the 144-pixel test shifts.
    const std::array<QPoint, 16> stars{{
        {159, 51}, {176, 117}, {183, 224}, {170, 320},
        {197, 79}, {211, 169}, {218, 289}, {243, 44},
        {257, 139}, {254, 248}, {269, 345}, {295, 93},
        {303, 209}, {321, 305}, {340, 161}, {349, 67}
    }};
    for (size_t i = 0; i < stars.size(); ++i) {
        const int x = stars[i].x() + starOffsetX;
        const int y = stars[i].y();
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                scene[static_cast<size_t>(y + dy) * width + x + dx] +=
                    static_cast<uint16_t>((5000 + 160 * i) *
                        std::exp(-(dx * dx + dy * dy) / 6.0));
            }
        }
    }
    std::array<uint16_t, kDngWidth> row;
    for (int y = 0; y < kDngHeight; ++y) {
        for (int x = 0; x < kDngWidth; ++x) {
            const int displayX = orientation == ORIENTATION_RIGHTTOP
                ? kDngHeight - 1 - y : portrait ? y : x;
            const int displayY = orientation == ORIENTATION_LEFTBOT
                ? kDngWidth - 1 - x : portrait ? x : y;
            row[static_cast<size_t>(x)] =
                scene[static_cast<size_t>(displayY) * width + displayX];
        }
        if (TIFFWriteScanline(handle, row.data(), y, 0) < 0) return false;
    }
    return TIFFWriteDirectory(handle) == 1;
}

QStringList makeSyntheticSequence(const QDir& directory, uint16_t orientation,
                                  const std::array<int, 3>& offsets) {
    if (!QDir().mkpath(directory.absolutePath())) return {};
    const QStringList names = {"C_early.dng", "A_middle.dng", "B_late.dng"};
    const std::array<int, 3> groundLevels{320, 780, 450};
    QStringList files;
    for (int i = 0; i < names.size(); ++i) {
        const QString path = directory.filePath(names[i]);
        if (!writeSyntheticDng(path, orientation, i * 10, offsets[i], groundLevels[i])) {
            return {};
        }
        files.append(path);
    }
    return files;
}

void recordPhases(ProcessingWorker& worker, std::vector<Phase>& phases) {
    QObject::connect(&worker, &ProcessingWorker::phaseChanged, &worker,
                     [&phases](Phase phase) { phases.push_back(phase); },
                     Qt::QueuedConnection);
}

bool waitForProcessing(ProcessingWorker& worker) {
    if (worker.wait(15000)) {
        QCoreApplication::processEvents();
        return true;
    }
    worker.requestCancel();
    worker.wait();
    check(false, "Synthetic sequence worker should finish promptly");
    return false;
}

void testTimelapseDiskBudget() {
    using Estimator = ProcessingMemoryEstimator;
    using Shortfall = Estimator::TimelapseDiskBudget::Shortfall;
    constexpr int count = 236;
    constexpr uint64_t scratchPayload = 6000ULL * 4000 * 6 * count;
    const auto tiff = Estimator::estimateTimelapseDiskBudget(6000, 4000, count, false);
    const auto png = Estimator::estimateTimelapseDiskBudget(6000, 4000, count, true);
    check(tiff.scratchBytes == png.scratchBytes &&
              tiff.scratchBytes >= scratchPayload + scratchPayload / 10,
          "Both formats should reserve all RGB16 caches and headroom");
    check(tiff.outputBytes > tiff.scratchBytes &&
              png.outputBytes < tiff.outputBytes &&
              png.outputBytes > (6000ULL * 3 + 1) * 4000 * count,
          "TIFF16 should budget strips; PNG8 should budget incompressible rows");
    check(tiff.sharedBytes == tiff.scratchBytes + tiff.outputBytes &&
              tiff.shortfall(true, tiff.sharedBytes, tiff.sharedBytes - 1) == Shortfall::SharedVolume &&
              tiff.shortfall(true, tiff.sharedBytes, tiff.sharedBytes) == Shortfall::None,
          "A shared volume needs the sum, using the smaller availability snapshot");
    check(png.shortfall(true, png.sharedBytes, png.sharedBytes) == Shortfall::None &&
              tiff.shortfall(true, png.sharedBytes, png.sharedBytes) == Shortfall::SharedVolume,
          "One shared volume can have room for PNG8 but not TIFF16");
    check(tiff.shortfall(false, tiff.scratchBytes, tiff.outputBytes) == Shortfall::None &&
              tiff.shortfall(false, tiff.scratchBytes - 1, tiff.outputBytes) == Shortfall::Scratch &&
              tiff.shortfall(false, tiff.scratchBytes, tiff.outputBytes - 1) == Shortfall::Output,
          "Different volumes should pass or fail independently");
    const auto small = Estimator::estimateTimelapseDiskBudget(1, 1, 3, true, 4096, 65536);
    check(small.scratchBytes > 3 * 4096 && small.outputBytes > 3 * 65536,
          "Small files still need at least one allocation unit each");
    const int largest = std::numeric_limits<int>::max();
    check(Estimator::estimateTimelapseDiskBudget(0, 10, 3, true).sharedBytes == 0 &&
              Estimator::estimateTimelapseDiskBudget(largest, largest, 3, false).sharedBytes == 0 &&
              Estimator::estimateTimelapseDiskBudget(largest, 256, largest, true).sharedBytes == 0,
          "Invalid sizes and overflowing frame or sequence budgets must not wrap");
}

void testPortraitStarTrails() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Portrait tests need a writable directory");
    if (!temporary.isValid()) return;

    for (uint16_t orientation : {ORIENTATION_RIGHTTOP, ORIENTATION_LEFTBOT}) {
        const QString root = temporary.filePath(QString::number(orientation));
        const QStringList ordered = makeSyntheticSequence(QDir(root), orientation, {0, 1, 2});
        check(ordered.size() == 3, "Portrait DNGs should be generated");
        if (ordered.size() != 3) return;
        ProcessingWorker::Params params;
        params.starTrailMode = true;
        params.photometricNormalizationEnabled = false;
        params.groundDetailStrength = 0;
        params.outputPath = root;
        params.skyGroundMaskOutputPath = QDir(root).filePath("mask.png");
        std::vector<Phase> phases;
        ProcessingWorker worker({ordered[1], ordered[2], ordered[0]}, {}, params);
        recordPhases(worker, phases);
        worker.start();
        if (!waitForProcessing(worker)) return;

        check(worker.errorString().isEmpty() && worker.outputFrameCount() == 3 &&
                  worker.stackedWidth() == kDngHeight && worker.stackedHeight() == kDngWidth,
              "Both portrait orientations should produce three-frame vertical star trails");
        check(worker.orderedFilePaths() == ordered && worker.selectedReferenceFrame() == ordered[0],
              "Star-trail accessor and reference should follow capture order");
        check(phases == std::vector<Phase>{Phase::Preparation, Phase::Stacking,
                                          Phase::Adjustment, Phase::Output},
              "Star trails should skip alignment and emit queued finishing and export phases");
        const QImage mask(params.skyGroundMaskOutputPath);
        const bool validMask = mask.size() == QSize(kDngHeight, kDngWidth);
        check(validMask && qGray(mask.pixel(40, 80)) == 255 && qGray(mask.pixel(40, 440)) == 0,
              "Portrait mask should place sky above foreground in decoded coordinates");

        const auto pixels = worker.takeStackedData();
        std::vector<uint16_t> exported;
        int width = 0;
        int height = 0;
        check(ImageExporter::loadTiffRgb16(worker.outputFile(), exported, width, height) &&
                  width == kDngHeight && height == kDngWidth && exported == pixels,
              "The exported TIFF16 should retain portrait dimensions and composite pixels");
        RawImageLoader loader;
        RawImageLoader::ImageData image;
        const size_t groundValue = (440 * kDngHeight + 40) * 3 + 1;
        unsigned groundMean = 0;
        bool groundSamplesReady = true;
        for (int i = 0; i < ordered.size(); ++i) {
            if (!loader.loadRaw(ordered[i], image) || image.data.size() <= groundValue) {
                groundSamplesReady = false;
                break;
            }
            groundMean = (i * groundMean + image.data[groundValue] + (i + 1) / 2) / (i + 1);
        }
        check(groundSamplesReady && pixels.size() > groundValue &&
                  pixels[groundValue] == groundMean,
              "Ground blending should average the same pixel in all rotated frames");

        if (orientation == ORIENTATION_RIGHTTOP) {
            const QString landscape = QDir(root).filePath("landscape.dng");
            check(writeSyntheticDng(landscape, ORIENTATION_TOPLEFT, 20, 0, 320),
                  "Mixed-orientation size mismatch fixture should be generated");
            ProcessingWorker mixed({ordered[0], ordered[1], landscape}, {}, params);
            mixed.start();
            if (!waitForProcessing(mixed)) return;
            check(mixed.errorString() == QCoreApplication::translate(
                      "Nebulens", "星轨序列尺寸不一致: %1").arg("landscape.dng") &&
                      mixed.outputFile().isEmpty(),
                  "A decoded size mismatch should be reported separately from RAW decoding failure");
        }
    }
}

void testSmallCommonIntersection() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Crop test needs a writable directory");
    if (!temporary.isValid()) return;
    const auto files = makeSyntheticSequence(QDir(temporary.path()), ORIENTATION_TOPLEFT,
                                              {-144, 0, 144});
    check(files.size() == 3, "Large-shift DNGs should be generated");
    if (files.size() != 3) return;
    ProcessingWorker::Params params;
    params.outputPath = temporary.filePath("output");
    params.autoLensChromaticAberration = false;
    params.autoRejectLowQualityFrames = false;
    params.photometricNormalizationEnabled = false;
    std::vector<Phase> phases;
    ProcessingWorker worker(files, files[1], params);
    recordPhases(worker, phases);
    worker.start();
    if (!waitForProcessing(worker)) return;
    check(worker.affineFrameCount() + worker.homographyFrameCount() == 2,
          "Both 28-percent shifts should pass pairwise alignment");
    check(worker.errorString() == QCoreApplication::translate(
              "Nebulens", "对齐后共同有效区域不足，无法堆栈。请移除偏移过大的帧后重试。") &&
              worker.outputFile().isEmpty() && worker.takeStackedData().empty() &&
              phases == std::vector<Phase>{Phase::Preparation, Phase::Alignment},
          "A rejected common crop must stop before stacking or export");

    phases.clear();
    ProcessingWorker pair({files[1], files[2]}, files[1], params);
    recordPhases(pair, phases);
    pair.start();
    if (!waitForProcessing(pair)) return;
    check(pair.errorString().isEmpty() && pair.cropOffsetX() > 0 &&
              pair.stackedWidth() >= kDngWidth / 2 && pair.stackedWidth() < kDngWidth &&
              QFileInfo::exists(pair.outputFile()) &&
              phases == std::vector<Phase>{Phase::Preparation, Phase::Alignment,
                  Phase::Stacking, Phase::Adjustment, Phase::Output},
          "A valid common region should still crop and export through all phases");
}

void testConcurrentTimelapseOutputs() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Timelapse tests need a writable directory");
    if (!temporary.isValid()) return;
    const auto ordered = makeSyntheticSequence(QDir(temporary.path()), ORIENTATION_TOPLEFT,
                                                {-2, 0, 2});
    check(ordered.size() == 3, "Timelapse DNGs should be generated");
    if (ordered.size() != 3) return;
    const QStringList input{ordered[1], ordered[2], ordered[0]};

    class EnglishStatus final : public QTranslator {
    public:
        bool isEmpty() const override { return false; }
        QString translate(const char* context, const char* sourceText,
                          const char*, int) const override {
            if (std::strcmp(context, "Nebulens") == 0 &&
                std::strcmp(sourceText, "预分析延时序列...") == 0) {
                return QStringLiteral("Analyzing time-lapse sequence...");
            }
            return {};
        }
    } translator;
    QCoreApplication::installTranslator(&translator);

    ProcessingWorker::Params params;
    params.timelapseMode = true;
    params.timelapseWindowSize = 3;
    params.timelapseProtectGround = false;
    params.photometricNormalizationEnabled = false;
    params.outputPath = temporary.filePath("new/output/root");
    ProcessingWorker tiff(input, {}, params);
    params.outputFormat = "png8";
    ProcessingWorker png(input, {}, params);
    std::vector<Phase> tiffPhases;
    std::vector<Phase> pngPhases;
    bool englishStatusSeen = false;
    recordPhases(tiff, tiffPhases);
    recordPhases(png, pngPhases);
    QObject::connect(&png, &ProcessingWorker::stageMessage, &png,
                     [&](const QString& message) {
                         englishStatusSeen |= message == "Analyzing time-lapse sequence...";
                     });
    tiff.start();
    png.start();
    const bool tiffDone = waitForProcessing(tiff);
    const bool pngDone = waitForProcessing(png);
    QCoreApplication::removeTranslator(&translator);
    if (!tiffDone || !pngDone) return;

    const std::vector<Phase> expected{Phase::Preparation, Phase::Alignment,
        Phase::Stacking, Phase::Output, Phase::Stacking, Phase::Output,
        Phase::Stacking, Phase::Output};
    check(englishStatusSeen && tiffPhases == expected && pngPhases == expected,
          "Each timelapse frame should emit its compute and output phases independently of text locale");
    check(tiff.errorString().isEmpty() && png.errorString().isEmpty() &&
              tiff.outputFrameCount() == 3 && png.outputFrameCount() == 3 &&
              !tiff.outputFile().isEmpty() && tiff.outputFile() != png.outputFile(),
          "Concurrent timelapses targeting one root must use separate directories");
    for (auto* worker : {&tiff, &png}) {
        check(worker->orderedFilePaths() == ordered && worker->selectedReferenceIndex() == 1 &&
                  worker->selectedReferenceFrame() == ordered[1],
              "Ordered input, reference index and reference file should identify the same frames");
        if (worker->outputFile().isEmpty()) continue;
        const QDir output(worker->outputFile());
        const QString extension = worker == &tiff ? ".tiff" : ".png";
        check(output.entryList(QDir::Files) == QStringList{
                  "0001_C_early_denoised" + extension, "0002_A_middle_denoised" + extension,
                  "0003_B_late_denoised" + extension},
              "Each exclusive sequence directory should contain only its chronologically numbered frames");
    }

    ProcessingWorker cancelled(input, {}, params);
    QObject::connect(&cancelled, &ProcessingWorker::phaseChanged, &cancelled,
                     [&](Phase phase) {
                         if (phase == Phase::Output) cancelled.requestCancel();
                     }, Qt::DirectConnection);
    cancelled.start();
    if (!waitForProcessing(cancelled)) return;
    check(cancelled.wasCancelled() && cancelled.outputFile().isEmpty() &&
              QDir(params.outputPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 2,
          "Cancelling before the first export should remove only the empty task directory");
}

void testEmptyInput() {
    ProcessingWorker::Params params;
    ProcessingWorker worker({}, {}, params);
    worker.start();
    check(worker.wait(3000), "Empty-input worker should finish promptly");
    check(!worker.wasCancelled(), "Empty input is an error, not cancellation");
    check(!worker.errorString().isEmpty(), "Empty-input worker should expose an error");
    check(worker.takeStackedData().empty(), "Failed worker should not expose image data");
}

void testCancellationBeforeStart() {
    ProcessingWorker::Params params;
    ProcessingWorker worker({"not-read.raw"}, "not-read.raw", params);
    worker.requestCancel();
    worker.start();
    check(worker.wait(3000), "Pre-cancelled worker should finish promptly");
    check(worker.wasCancelled(), "Pre-start cancellation should be preserved by run()");
    check(worker.errorString().isEmpty(), "Cancellation should not be reported as an error");
}

void testExportWorkerLifecycle() {
    QTemporaryDir directory;
    check(directory.isValid(),
          "Export-worker temporary directory should be available");
    if (!directory.isValid()) return;

    auto image = std::make_shared<std::vector<uint16_t>>(16 * 16 * 3, 4096);
    const QString path = directory.filePath("background.png");
    ExportWorker worker(image, 16, 16, path, ImageExporter::Png8);
    worker.start();
    check(worker.wait(3000), "Export worker should finish promptly");
    check(worker.succeeded() && QFileInfo::exists(path),
          "Export worker should write a valid image off the caller thread");
}

void testHistoryPreviewWorker() {
    QTemporaryDir directory;
    check(directory.isValid(),
          "History-preview temporary directory should be available");
    if (!directory.isValid()) return;

    constexpr int width = 24;
    constexpr int height = 12;
    std::vector<uint16_t> rgb(
        static_cast<size_t>(width) * height * 3, 8192);
    const QString path = directory.filePath("20260822_120000_stacked.tiff");
    check(ImageExporter::exportRgb16(
              rgb, width, height, path, ImageExporter::Tiff16),
          "History-preview TIFF fixture should be writable");

    QImage preview;
    QString failure;
    HistoryPreviewWorker worker(path);
    QObject::connect(&worker, &HistoryPreviewWorker::loaded,
                     [&](const QString&, const QImage& image) {
                         preview = image;
                     });
    QObject::connect(&worker, &HistoryPreviewWorker::failed,
                     [&](const QString&, const QString& reason) {
                         failure = reason;
                     });
    worker.start();
    check(worker.wait(3000),
          "History-preview worker should finish promptly");
    check(failure.isEmpty() && !preview.isNull() &&
              preview.size() == QSize(width, height),
          "History-preview worker should return a bounded RGB image");
}

void testRawLoaderFitsWorkerStack() {
    ProcessingWorker::Params params;
    ProcessingWorker worker({"not-a-real-file.raw"}, "not-a-real-file.raw", params);
    worker.start();
    check(worker.wait(3000), "RAW metadata failure should finish without overflowing worker stack");
    check(!worker.errorString().isEmpty(), "Unreadable RAW input should expose an error");
}

void testSingleFrameFailureUsesDedicatedPath() {
    ProcessingWorker::Params params;
    params.singleFrameMode = true;
    ProcessingWorker worker({"not-a-real-file.raw"}, {}, params);
    worker.start();
    check(worker.wait(3000), "Single-frame metadata failure should finish promptly");
    check(!worker.errorString().isEmpty(),
          "Unreadable single-frame input should expose an error");
    check(worker.outputFrameCount() == 0,
          "Failed single-frame processing should not report a completed frame");
}

void testTimelapseRejectsTooFewFrames() {
    ProcessingWorker::Params params;
    params.timelapseMode = true;
    ProcessingWorker worker(
        {"first.raw", "second.raw"}, "first.raw", params);
    worker.start();
    check(worker.wait(3000),
          "Short timelapse input should finish promptly");
    check(!worker.wasCancelled(),
          "Short timelapse input is an error, not cancellation");
    check(worker.errorString().contains("3"),
          "Timelapse input error should explain the three-frame minimum");
    check(worker.outputFrameCount() == 0,
          "Rejected timelapse input should not report output frames");
}

void testStarTrailRejectsTooFewFrames() {
    ProcessingWorker::Params params;
    params.starTrailMode = true;
    ProcessingWorker worker(
        {"first.raw", "second.raw"}, "first.raw", params);
    worker.start();
    check(worker.wait(3000),
          "Short star-trail input should finish promptly");
    check(!worker.wasCancelled(),
          "Short star-trail input is an error, not cancellation");
    check(worker.errorString().contains("3"),
          "Star-trail input error should explain the three-frame minimum");
    check(worker.outputFrameCount() == 0,
          "Rejected star-trail input should not report output frames");
}

void testDeepSkyRequiresCalibrationSets() {
    ProcessingWorker::Params params;
    params.deepSkyMode = true;
    ProcessingWorker worker(
        {"first.raw", "second.raw"}, "first.raw", params);
    worker.start();
    check(worker.wait(3000),
          "Deep-sky calibration validation should finish promptly");
    check(worker.errorString().contains("Bias") &&
              worker.errorString().contains("Dark") &&
              worker.errorString().contains("Flat"),
          "Deep-sky input should identify every required calibration set");
    check(worker.calibratedLightFrameCount() == 0,
          "Rejected deep-sky input should not report calibrated lights");
}

void testDedicatedModesAreMutuallyExclusive() {
    ProcessingWorker::Params params;
    params.singleFrameMode = true;
    params.starTrailMode = true;
    ProcessingWorker worker({"not-read.raw"}, {}, params);
    worker.start();
    check(worker.wait(3000),
          "Conflicting dedicated modes should finish promptly");
    check(worker.errorString().contains(QString::fromUtf8("不能同时启用")),
          "Conflicting dedicated modes should expose an explicit error");
}

void testStarTrailRejectsStarReduction() {
    ProcessingWorker::Params params;
    params.starTrailMode = true;
    params.starReduceEnabled = true;
    params.starReduceStrength = 70;
    ProcessingWorker worker(
        {"first.raw", "second.raw", "third.raw"}, {}, params);
    worker.start();
    check(worker.wait(3000),
          "Star-trail and star-reduction conflict should finish promptly");
    check(worker.errorString().contains(QString::fromUtf8("缩星")),
          "Star-trail and star-reduction conflict should be explicit");
}

void testQuickPreviewNoOpAndCancellation() {
    auto source = std::make_shared<const std::vector<uint16_t>>(
        std::vector<uint16_t>{
            100, 200, 300, 400, 500, 600,
            700, 800, 900, 1000, 1100, 1200});
    FinishingOptions options;
    QuickPreviewWorker worker(
        source, 2, 2, nullptr, options, 17);
    worker.start();
    check(worker.wait(3000),
          "No-op quick preview should finish promptly");
    check(worker.errorString().isEmpty() && !worker.wasCancelled(),
          "No-op quick preview should succeed");
    check(worker.generation() == 17 && worker.takeResult() == *source,
          "No-op quick preview should preserve pixels and generation");

    QuickPreviewWorker cancelled(
        source, 2, 2, nullptr, options, 18);
    cancelled.requestCancel();
    cancelled.start();
    check(cancelled.wait(3000),
          "Pre-cancelled quick preview should finish promptly");
    check(cancelled.wasCancelled() && cancelled.errorString().isEmpty() &&
              cancelled.takeResult().empty(),
          "Cancelled quick preview should expose no stale result");

    constexpr int colorWidth = 32;
    constexpr int colorHeight = 32;
    std::vector<uint16_t> colorSource(
        static_cast<size_t>(colorWidth) * colorHeight * 3);
    for (size_t pixel = 0; pixel < colorSource.size() / 3; ++pixel) {
        colorSource[pixel * 3] = 6000;
        colorSource[pixel * 3 + 1] = 2200;
        colorSource[pixel * 3 + 2] = 1200;
    }
    auto sharedColor =
        std::make_shared<const std::vector<uint16_t>>(colorSource);
    FinishingOptions colorOptions;
    colorOptions.modifiedCameraColorEnabled = true;
    colorOptions.modifiedCameraColor.neutralMode =
        ModifiedCameraNeutralMode::ManualPoint;
    colorOptions.modifiedCameraColor.manualPointX = 0.25;
    colorOptions.modifiedCameraColor.manualPointY = 0.25;
    colorOptions.modifiedCameraColor.strength = 60;
    colorOptions.basicAdjustments.temperature = 12;
    colorOptions.basicAdjustments.exposureTenths = 3;
    colorOptions.basicAdjustments.vibrance = 18;
    colorOptions.basicAdjustments.sharpening = 25;
    std::vector<uint16_t> expected = colorSource;
    FinishingResult expectedResult;
    check(FinishingPipeline::process(
              expected, colorWidth, colorHeight, nullptr,
              colorOptions, expectedResult),
          "Direct manual gray-point finishing should succeed");
    QuickPreviewWorker colorWorker(
        sharedColor, colorWidth, colorHeight, nullptr,
        colorOptions, 19);
    colorWorker.start();
    check(colorWorker.wait(3000) && colorWorker.errorString().isEmpty() &&
              colorWorker.takeResult() == expected,
          "Quick preview should match the shared finishing pipeline exactly");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testTimelapseDiskBudget();
    testPortraitStarTrails();
    testSmallCommonIntersection();
    testConcurrentTimelapseOutputs();
    testEmptyInput();
    testCancellationBeforeStart();
    testExportWorkerLifecycle();
    testHistoryPreviewWorker();
    testRawLoaderFitsWorkerStack();
    testSingleFrameFailureUsesDedicatedPath();
    testTimelapseRejectsTooFewFrames();
    testStarTrailRejectsTooFewFrames();
    testDeepSkyRequiresCalibrationSets();
    testDedicatedModesAreMutuallyExclusive();
    testStarTrailRejectsStarReduction();
    testQuickPreviewNoOpAndCancellation();
    if (failures == 0) {
        std::cout << "All worker tests passed.\n";
        return 0;
    }
    return 1;
}
