#include "core/ImageExporter.h"
#include "workers/ExportWorker.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <array>
#include <iostream>
#include <memory>

namespace {

constexpr int kWidth = 17;
constexpr int kHeight = 12;
constexpr int kTimeoutMs = 10'000;
int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
}

std::vector<uint16_t> pixels(uint16_t value) {
    return std::vector<uint16_t>(kWidth * kHeight * 3, value);
}

QStringList entries(const QDir& directory) {
    return directory.entryList(QDir::AllEntries | QDir::Hidden |
                               QDir::System | QDir::NoDotAndDotDot);
}

bool matchesTiff(const QString& path, uint16_t value) {
    if (path.isEmpty()) return false;
    std::vector<uint16_t> loaded;
    int width = 0;
    int height = 0;
    return ImageExporter::loadTiffRgb16(path, loaded, width, height) &&
           width == kWidth && height == kHeight && loaded == pixels(value);
}

int testSameNameAndCancellation() {
    QTemporaryDir root;
    const QString subdirectory = QStringLiteral("\u661F\u7A7A");
    check(root.isValid() && QDir(root.path()).mkdir(subdirectory),
          "A Unicode output directory should be available");
    if (!root.isValid() || !QDir(root.filePath(subdirectory)).exists()) return 0;

    const QDir directory(root.filePath(subdirectory));
    const QString name = QStringLiteral("\u94F6\u6CB3.test_single.tiff");
    const QString preferred = directory.filePath(name);
    constexpr uint16_t original = 0x1234;
    constexpr uint16_t newer = 0xABCD;
    QString actual;
    int publicationPoll = 0;
    const bool seeded = ImageExporter::exportNewRgb16(
        pixels(original), kWidth, kHeight, preferred, ImageExporter::Tiff16,
        actual, [&]() { ++publicationPoll; return false; });
    check(seeded && actual == preferred && matchesTiff(preferred, original),
          "A free preferred name must contain the original 16-bit pixels");
    if (!seeded) return 0;

    check(publicationPoll > 4, "Cancellation must be polled during encoding");
    for (const int stopAt : {1, 4, publicationPoll}) {
        int polls = 0;
        bool stagedImageReady = false;
        actual = preferred;
        const bool published = ImageExporter::exportNewRgb16(
            pixels(newer), kWidth, kHeight, preferred, ImageExporter::Tiff16,
            actual, [&]() {
                if (++polls != stopAt) return false;
                if (stopAt == publicationPoll) {
                    QStringList inProgress = entries(directory);
                    inProgress.removeAll(name);
                    stagedImageReady = inProgress.size() == 1 && matchesTiff(
                        directory.filePath(inProgress.front()), newer);
                }
                return true;
            });
        check(!published && actual.isEmpty() && polls == stopAt &&
                  entries(directory) == QStringList{name} &&
                  matchesTiff(preferred, original),
              "Cancellation must leave only the original file and pixels");
        if (stopAt == publicationPoll) {
            check(stagedImageReady,
                  "Late cancellation must discard a complete sibling temp file");
        }
    }

    check(ImageExporter::exportNewRgb16(
              pixels(newer), kWidth, kHeight, preferred, ImageExporter::Tiff16,
              actual) &&
              actual == directory.filePath(
                  QStringLiteral("\u94F6\u6CB3.test_single_2.tiff")) &&
              matchesTiff(actual, newer) && matchesTiff(preferred, original),
          "Same-name exports must retain their distinct pixels and history suffix");

    const QStringList finished = entries(directory);
    check(finished.size() == 2, "Successful delivery must not leave temp files");
    const QString tooLong = directory.filePath(QString(320, QLatin1Char('x')) +
                                               QStringLiteral(".tiff"));
    check(!ImageExporter::exportNewRgb16(
              pixels(newer), kWidth, kHeight, tooLong, ImageExporter::Tiff16,
              actual) &&
              actual.isEmpty() && entries(directory) == finished &&
              matchesTiff(preferred, original),
          "Publication failure must clear the receipt and remove its temp file");
    return publicationPoll;
}

int raceWriter(const QString& preferred, uint16_t value, int publicationPoll) {
    QString actual;
    int polls = 0;
    if (!ImageExporter::exportNewRgb16(
            pixels(value), kWidth, kHeight, preferred, ImageExporter::Tiff16,
            actual, [&]() {
                if (++polls != publicationPoll) return false;
                // Pause after staging so every process contends for a free name.
                std::cout << "ready\n" << std::flush;
                return std::cin.get() != 'g';
            })) {
        return 1;
    }
    std::cout << actual.toUtf8().constData() << '\n';
    return 0;
}

void testCrossProcessCompetition(int publicationPoll) {
    QTemporaryDir root;
    check(root.isValid(), "A process race directory should be available");
    if (!root.isValid()) return;

    const QString name = QStringLiteral("20261126_193000_star_trail.tiff");
    const QString preferred = root.filePath(name);
    constexpr std::array<uint16_t, 4> values{1003, 2048, 4197, 8192};
    std::array<QProcess, values.size()> children;
    for (size_t index = 0; index < children.size(); ++index) {
        children[index].start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--race-writer"), preferred,
             QString::number(values[index]), QString::number(publicationPoll)});
    }
    for (auto& child : children) {
        if (child.waitForStarted(kTimeoutMs)) {
            while (!child.canReadLine() && child.waitForReadyRead(kTimeoutMs)) {}
        }
        const bool ready = child.readLine().trimmed() == "ready";
        check(ready, "Each independent writer must reach the publication barrier");
        if (!ready) return;
    }

    const QDir directory(root.path());
    const QStringList historyFilter{QStringLiteral("*_star_trail*.tiff")};
    check(directory.entryList(historyFilter, QDir::Files).isEmpty(),
          "Staging must not expose an unfinished result at a history path");
    for (auto& child : children) {
        child.write("g", 1);
        child.closeWriteChannel();
    }

    QSet<QString> paths;
    for (size_t index = 0; index < children.size(); ++index) {
        QProcess& child = children[index];
        const bool finished = child.state() == QProcess::NotRunning ||
                              child.waitForFinished(kTimeoutMs);
        check(finished && child.exitStatus() == QProcess::NormalExit &&
                  child.exitCode() == 0,
              "Each competing process must deliver its own file");
        const QString path = QString::fromUtf8(
            child.readAllStandardOutput().trimmed());
        check(matchesTiff(path, values[index]),
              "The returned path must contain that process's own complete pixels");
        paths.insert(path);
    }
    const auto count = static_cast<qsizetype>(children.size());
    check(paths.size() == count && paths.contains(preferred) &&
              directory.entryList(historyFilter, QDir::Files).size() == count &&
              entries(directory).size() == count,
          "The race must retain every result without exposing or leaking temp files");
}

void testExplicitPathCompatibility() {
    QTemporaryDir root;
    check(root.isValid(), "An explicit-path test directory should be available");
    if (!root.isValid()) return;

    const QString path = root.filePath(QStringLiteral("sequence_0001.tiff"));
    check(ImageExporter::exportRgb16(pixels(19), kWidth, kHeight, path) &&
              matchesTiff(path, 19),
          "Explicit TIFF export must still write the requested path");
    int polls = 0;
    check(!ImageExporter::exportRgb16(
              pixels(53), kWidth, kHeight, path, ImageExporter::Tiff16,
              [&]() { return ++polls == 4; }) &&
              matchesTiff(path, 19),
          "Cancelled explicit-path export must preserve the old file");
    check(ImageExporter::exportRgb16(pixels(53), kWidth, kHeight, path) &&
              matchesTiff(path, 53) && entries(QDir(root.path())).size() == 1,
          "Successful explicit-path export may replace the requested file");
}

void testExportWorkerReceipt() {
    QTemporaryDir root;
    check(root.isValid(), "A manual output directory should be available");
    if (!root.isValid()) return;

    const QString preferred = root.filePath("20261126_193000_stacked_export.png");
    using Format = ImageExporter::Format;
    QString actual;
    int publicationPoll = 0;
    const bool seeded = ImageExporter::exportNewRgb16(
        pixels(0), kWidth, kHeight, preferred, Format::Png8,
        actual, [&]() { ++publicationPoll; return false; });
    check(seeded && actual == preferred,
          "The old PNG should be available");
    if (!seeded) return;

    const QStringList before = entries(QDir(root.path()));
    int polls = 0;
    check(!ImageExporter::exportNewRgb16(
              pixels(65535), kWidth, kHeight, preferred, Format::Png8,
              actual, [&]() { return ++polls == publicationPoll; }) &&
              actual.isEmpty() && entries(QDir(root.path())) == before &&
              QImage(preferred).pixelColor(0, 0) == QColor(Qt::black),
          "Late PNG cancellation must leave the existing file intact");

    auto image = std::make_shared<std::vector<uint16_t>>(pixels(65535));
    ExportWorker worker(image, kWidth, kHeight, preferred, Format::Png8);
    check(worker.outputPath().isEmpty(), "A pending worker has no delivery receipt");
    worker.start();
    check(worker.wait(kTimeoutMs) && worker.succeeded() &&
              !worker.wasCancelled() &&
              worker.outputPath() == root.filePath(
                  "20261126_193000_stacked_export_2.png"),
          "A successful worker must report the collision-resolved path");
    const QImage oldImage(preferred);
    const QImage newImage(worker.outputPath());
    check(oldImage.size() == QSize(kWidth, kHeight) &&
              newImage.size() == QSize(kWidth, kHeight) &&
              oldImage.pixelColor(0, 0) == QColor(Qt::black) &&
              newImage.pixelColor(0, 0) == QColor(Qt::white),
          "Manual PNG export must preserve the old file and write the new pixels");

    ExportWorker cancelled(image, kWidth, kHeight, preferred, Format::Png8);
    cancelled.requestCancel();
    cancelled.start();
    check(cancelled.wait(kTimeoutMs) && cancelled.wasCancelled() &&
              !cancelled.succeeded() && cancelled.outputPath().isEmpty(),
          "A cancelled worker must not report the preferred path");

    ExportWorker failed(image, kWidth, kHeight,
                        root.filePath("missing/out.png"), Format::Png8);
    failed.start();
    check(failed.wait(kTimeoutMs) && !failed.wasCancelled() &&
              !failed.succeeded() && failed.outputPath().isEmpty() &&
              entries(QDir(root.path())).size() == 2,
          "A failed worker must not report a path or leave any extra file");
}

} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() == 5 && arguments[1] == QStringLiteral("--race-writer")) {
        return raceWriter(arguments[2], arguments[3].toUShort(),
                          arguments[4].toInt());
    }

    const int publicationPoll = testSameNameAndCancellation();
    if (publicationPoll > 0) testCrossProcessCompetition(publicationPoll);
    testExplicitPathCompatibility();
    testExportWorkerReceipt();
    if (failures != 0) return 1;
    std::cout << "All output delivery tests passed.\n";
    return 0;
}
