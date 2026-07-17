#include "workers/ProcessingWorker.h"

#include <QCoreApplication>

#include <iostream>

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

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testEmptyInput();
    testCancellationBeforeStart();
    testRawLoaderFitsWorkerStack();
    if (failures == 0) {
        std::cout << "All worker tests passed.\n";
        return 0;
    }
    return 1;
}
