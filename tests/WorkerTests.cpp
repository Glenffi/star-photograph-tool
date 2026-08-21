#include "workers/ProcessingWorker.h"
#include "workers/QuickPreviewWorker.h"
#include "workers/ExportWorker.h"
#include "workers/HistoryPreviewWorker.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
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
