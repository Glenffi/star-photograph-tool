#include "workers/ProcessingWorker.h"
#include "workers/QuickPreviewWorker.h"

#include <QCoreApplication>

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
    check(worker.stackedFrameCount() == 0,
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
    check(worker.stackedFrameCount() == 0,
          "Rejected timelapse input should not report output frames");
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
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testEmptyInput();
    testCancellationBeforeStart();
    testRawLoaderFitsWorkerStack();
    testSingleFrameFailureUsesDedicatedPath();
    testTimelapseRejectsTooFewFrames();
    testDeepSkyRequiresCalibrationSets();
    testQuickPreviewNoOpAndCancellation();
    if (failures == 0) {
        std::cout << "All worker tests passed.\n";
        return 0;
    }
    return 1;
}
