#include "core/ProcessingMemoryEstimator.h"
#include "core/RawImageLoader.h"
#include "workers/ProcessingWorker.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <iostream>

namespace {

QStringList rawFiles(const QString& directory) {
    static const QSet<QString> extensions = {
        "nef", "cr2", "cr3", "arw", "dng", "raw", "orf", "raf", "pef", "rw2"
    };
    QStringList files;
    QDirIterator iterator(directory, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (extensions.contains(QFileInfo(path).suffix().toLower())) files.push_back(path);
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

bool validMethod(const QString& method) {
    return method == "average" || method == "median" ||
        method == "kappa-sigma" || method == "winsorized";
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("StarProcessorPipelineRunner");
    QCoreApplication::setApplicationVersion(STARPROCESSOR_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Run the production StarProcessor pipeline on a local RAW sequence.");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption inputOption({"i", "input"},
        "Directory containing one RAW sequence.", "directory");
    const QCommandLineOption outputOption({"o", "output"},
        "Directory for the TIFF and pipeline-report.json.", "directory");
    const QCommandLineOption limitOption("limit",
        "Use at most this many sorted RAW files.", "count", "0");
    const QCommandLineOption referenceOption("reference-index",
        "Zero-based reference index; defaults to the middle selected frame.", "index", "-1");
    const QCommandLineOption methodOption("method",
        "Stack method: average, median, kappa-sigma, or winsorized.",
        "name", "kappa-sigma");
    const QCommandLineOption kappaOption("kappa", "Sigma clipping kappa.",
                                         "value", "2.5");
    parser.addOptions({inputOption, outputOption, limitOption, referenceOption,
                       methodOption, kappaOption});
    parser.process(application);

    const QString input = QDir(parser.value(inputOption)).absolutePath();
    if (parser.value(inputOption).isEmpty() || !QDir(input).exists()) {
        std::cerr << "A valid --input directory is required.\n";
        return 2;
    }
    const QString output = parser.value(outputOption).isEmpty()
        ? QDir::current().filePath("pipeline-output")
        : QDir(parser.value(outputOption)).absolutePath();
    if (!QDir().mkpath(output)) {
        std::cerr << "Cannot create output directory.\n";
        return 2;
    }

    bool limitOk = false;
    const int limit = parser.value(limitOption).toInt(&limitOk);
    bool referenceOk = false;
    int referenceIndex = parser.value(referenceOption).toInt(&referenceOk);
    bool kappaOk = false;
    const double kappa = parser.value(kappaOption).toDouble(&kappaOk);
    const QString method = parser.value(methodOption).toLower();
    if (!limitOk || limit < 0 || !referenceOk || referenceIndex < -1 ||
        !kappaOk || kappa <= 0.0 || !validMethod(method)) {
        std::cerr << "Invalid --limit, --reference-index, --method, or --kappa.\n";
        return 2;
    }

    QStringList files = rawFiles(input);
    if (limit > 0 && files.size() > limit) files = files.mid(0, limit);
    if (files.size() < 2) {
        std::cerr << "At least two RAW files are required.\n";
        return 2;
    }
    if (referenceIndex < 0) referenceIndex = files.size() / 2;
    if (referenceIndex >= files.size()) {
        std::cerr << "--reference-index is outside the selected sequence.\n";
        return 2;
    }

    RawImageLoader loader;
    RawImageLoader::Metadata metadata;
    if (!loader.loadMetadata(files.front().toStdString(), metadata)) {
        std::cerr << "Cannot read first-frame metadata.\n";
        return 3;
    }
    const uint64_t estimatedBytes = ProcessingMemoryEstimator::estimatePeakBytes(
        metadata.width, metadata.height, files.size(), false);
    const uint64_t scratchBytes = ProcessingMemoryEstimator::estimateScratchDiskBytes(
        metadata.width, metadata.height, files.size(), false);

    ProcessingWorker::Params params;
    params.stackMethod = method;
    params.kappaValue = kappa;
    params.outputFormat = "tiff16";
    params.outputPath = output;

    ProcessingWorker worker(files, files[referenceIndex], params);
    QObject::connect(&worker, &ProcessingWorker::stageMessage, &application,
                     [](const QString& message) {
                         std::cout << "[stage] " << message.toStdString() << std::endl;
                     }, Qt::DirectConnection);
    int lastProgress = -10;
    QObject::connect(&worker, &ProcessingWorker::progress, &application,
                     [&lastProgress](int progress) {
                         if (progress == 100 || progress >= lastProgress + 10) {
                             lastProgress = progress;
                             std::cout << "[progress] " << progress << "%" << std::endl;
                         }
                     }, Qt::DirectConnection);

    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    std::cout << "Frames: " << files.size()
              << ", reference: " << QFileInfo(files[referenceIndex]).fileName().toStdString()
              << ", method: " << method.toStdString()
              << ", estimated peak: " << estimatedBytes / kGiB << " GiB"
              << ", scratch: " << scratchBytes / kGiB << " GiB\n";
    QElapsedTimer timer;
    timer.start();
    worker.start();
    worker.wait();

    QJsonObject report;
    report["schemaVersion"] = 1;
    report["toolVersion"] = QCoreApplication::applicationVersion();
    report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["input"] = input;
    report["selectedFrames"] = files.size();
    report["referenceIndex"] = referenceIndex;
    report["referenceFile"] = QFileInfo(files[referenceIndex]).fileName();
    report["method"] = method;
    report["kappa"] = kappa;
    report["estimatedPeakBytes"] = QString::number(estimatedBytes);
    report["estimatedScratchBytes"] = QString::number(scratchBytes);
    report["elapsedMs"] = timer.elapsed();
    report["processedFrames"] = worker.stackedFrameCount();
    report["width"] = worker.stackedWidth();
    report["height"] = worker.stackedHeight();
    report["outputFile"] = worker.outputFile();
    report["error"] = worker.errorString();
    report["success"] = worker.errorString().isEmpty() && !worker.outputFile().isEmpty();

    const QString reportPath = QDir(output).filePath("pipeline-report.json");
    QFile reportFile(reportPath);
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Cannot write pipeline report.\n";
        return 4;
    }
    reportFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));

    // Release the full-resolution result before reporting completion. The TIFF
    // remains the durable artifact; the CLI does not need to retain another copy.
    worker.takeStackedData().clear();
    if (!worker.errorString().isEmpty()) {
        std::cerr << "Pipeline failed: " << worker.errorString().toStdString() << '\n';
        return 5;
    }
    std::cout << "TIFF: " << worker.outputFile().toStdString() << '\n'
              << "Report: " << reportPath.toStdString() << '\n'
              << "Elapsed: " << timer.elapsed() / 1000.0 << " s\n";
    return 0;
}
