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
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <iostream>
#include <limits>

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

bool validGroundMethod(const QString& method) {
    return method == "reference" || method == "median" || method == "average";
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
        "Zero-based reference index; -1 selects the best frame automatically.", "index", "-1");
    const QCommandLineOption methodOption("method",
        "Stack method: average, median, kappa-sigma, or winsorized.",
        "name", "kappa-sigma");
    const QCommandLineOption kappaOption("kappa", "Sigma clipping kappa.",
                                         "value", "2.5");
    const QCommandLineOption memoryBudgetOption(
        "memory-budget-mib",
        "Override the automatic memory budget in MiB; 0 keeps automatic mode.",
        "mib", "0");
    const QCommandLineOption noPhotometricNormalizationOption(
        "no-photometric-normalization",
        "Disable frame-to-reference exposure and background color matching.");
    const QCommandLineOption noQualityRejectionOption(
        "no-quality-rejection",
        "Keep severe quality outliers; automatic reference selection still runs.");
    const QCommandLineOption skyGroundOption(
        "sky-ground",
        "Enable automatic sky/ground separation for fixed-tripod sequences.");
    const QCommandLineOption skyGroundMaskOption(
        "sky-ground-mask",
        "Use this user mask instead of automatic sky/ground detection.",
        "path");
    const QCommandLineOption skyGroundFeatherOption(
        "sky-ground-feather",
        "Sky/ground mask feather radius in pixels (0-50).",
        "pixels", "20");
    const QCommandLineOption groundMethodOption(
        "ground-method",
        "Ground compositing: reference, median, or average.",
        "name", "average");
    const QCommandLineOption groundDetailOption(
        "ground-detail-strength",
        "Ground-only luminance detail recovery (0-70).",
        "value", "40");
    const QCommandLineOption denoiseOption(
        "denoise-strength",
        "Enable linear RGB multiscale denoise at strength 1-70; 0 disables it.",
        "value", "0");
    const QCommandLineOption dehazeOption(
        "dehaze-strength",
        "Enable RGB-linked dehaze at strength 1-100; 0 disables it.",
        "value", "0");
    const QCommandLineOption stretchOption(
        "stretch",
        "Apply background neutralization and linked RGB Arcsinh stretch.");
    const QCommandLineOption starReduceOption(
        "star-reduce-strength",
        "Enable star reduction at strength 1-100; 0 disables it.",
        "value", "0");
    parser.addOptions({inputOption, outputOption, limitOption, referenceOption,
                       methodOption, kappaOption, memoryBudgetOption,
                       noPhotometricNormalizationOption,
                       noQualityRejectionOption,
                       skyGroundOption, skyGroundMaskOption,
                       skyGroundFeatherOption, groundMethodOption,
                       groundDetailOption,
                       denoiseOption, dehazeOption, stretchOption,
                       starReduceOption});
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
    bool memoryBudgetOk = false;
    const qulonglong memoryBudgetMiB =
        parser.value(memoryBudgetOption).toULongLong(&memoryBudgetOk);
    bool denoiseOk = false;
    const int denoiseStrength =
        parser.value(denoiseOption).toInt(&denoiseOk);
    bool dehazeOk = false;
    const int dehazeStrength = parser.value(dehazeOption).toInt(&dehazeOk);
    bool starReduceOk = false;
    const int starReduceStrength =
        parser.value(starReduceOption).toInt(&starReduceOk);
    const bool stretchEnabled = parser.isSet(stretchOption);
    bool skyGroundFeatherOk = false;
    const int skyGroundFeather =
        parser.value(skyGroundFeatherOption).toInt(&skyGroundFeatherOk);
    const QString skyGroundMask = parser.value(skyGroundMaskOption);
    const bool skyGroundEnabled =
        parser.isSet(skyGroundOption) || !skyGroundMask.isEmpty();
    const QString groundMethod = parser.value(groundMethodOption).toLower();
    bool groundDetailOk = false;
    const int groundDetailStrength =
        parser.value(groundDetailOption).toInt(&groundDetailOk);
    const QString method = parser.value(methodOption).toLower();
    if (!limitOk || limit < 0 || !referenceOk || referenceIndex < -1 ||
        !kappaOk || kappa <= 0.0 || !memoryBudgetOk ||
        !denoiseOk || denoiseStrength < 0 || denoiseStrength > 70 ||
        !dehazeOk || dehazeStrength < 0 || dehazeStrength > 100 ||
        !starReduceOk || starReduceStrength < 0 ||
        starReduceStrength > 100 ||
        !skyGroundFeatherOk || skyGroundFeather < 0 ||
        skyGroundFeather > 50 ||
        (!skyGroundMask.isEmpty() && !QFileInfo::exists(skyGroundMask)) ||
        !validGroundMethod(groundMethod) ||
        !groundDetailOk || groundDetailStrength < 0 ||
        groundDetailStrength > 70 ||
        memoryBudgetMiB >
            std::numeric_limits<uint64_t>::max() / (1024ULL * 1024ULL) ||
        !validMethod(method)) {
        std::cerr << "Invalid --limit, --reference-index, --method, --kappa, "
                     "--memory-budget-mib, --denoise-strength, "
                     "--dehaze-strength, or "
                     "--star-reduce-strength/sky-ground options.\n";
        return 2;
    }
    const uint64_t requestedMemoryBudgetBytes =
        static_cast<uint64_t>(memoryBudgetMiB) * 1024ULL * 1024ULL;

    QStringList files = rawFiles(input);
    if (limit > 0 && files.size() > limit) files = files.mid(0, limit);
    if (files.size() < 2) {
        std::cerr << "At least two RAW files are required.\n";
        return 2;
    }
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
    ProcessingMemoryEstimator::EstimateOptions estimateOptions;
    estimateOptions.frameCount = files.size();
    estimateOptions.skyGroundSeparation = skyGroundEnabled;
    estimateOptions.noiseReduction = denoiseStrength > 0;
    estimateOptions.dehaze = dehazeStrength > 0;
    estimateOptions.stretch = stretchEnabled;
    estimateOptions.starReduction = starReduceStrength > 0;
    const uint64_t estimatedBytes =
        ProcessingMemoryEstimator::estimatePeakBytes(
            metadata.width, metadata.height, estimateOptions);
    const uint64_t scratchBytes = ProcessingMemoryEstimator::estimateScratchDiskBytes(
        metadata.width, metadata.height, files.size(), skyGroundEnabled);

    ProcessingWorker::Params params;
    params.stackMethod = method;
    params.kappaValue = kappa;
    params.outputFormat = "tiff16";
    params.outputPath = output;
    params.memoryBudgetBytes = requestedMemoryBudgetBytes;
    params.skyGroundSepEnabled = skyGroundEnabled;
    params.skyGroundMode = skyGroundMask.isEmpty()
        ? SkyGroundMask::AutoDetect : SkyGroundMask::UserMask;
    params.userMaskPath = skyGroundMask;
    params.groundStackMethod = groundMethod;
    params.groundDetailStrength = skyGroundEnabled ? groundDetailStrength : 0;
    params.featherRadius = skyGroundFeather;
    const QString generatedSkyGroundMask = skyGroundEnabled
        ? QDir(output).filePath("sky-ground-mask.png") : QString();
    params.skyGroundMaskOutputPath = generatedSkyGroundMask;
    params.autoRejectLowQualityFrames =
        !parser.isSet(noQualityRejectionOption);
    params.photometricNormalizationEnabled =
        !parser.isSet(noPhotometricNormalizationOption);
    params.noiseReductionEnabled = denoiseStrength > 0;
    params.noiseReductionStrength = denoiseStrength;
    params.dewarpEnabled = dehazeStrength > 0;
    params.dewarpStrength = dehazeStrength;
    params.stretchEnabled = stretchEnabled;
    params.starReduceEnabled = starReduceStrength > 0;
    params.starReduceStrength = starReduceStrength;

    const QString requestedReference = referenceIndex >= 0
        ? files[referenceIndex] : QString();
    ProcessingWorker worker(files, requestedReference, params);
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
    const ProcessingMemoryEstimator::SystemMemoryInfo memoryInfo =
        ProcessingMemoryEstimator::systemMemoryInfo();
    const uint64_t effectiveMemoryBudget =
        ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
            memoryInfo.safeBudgetBytes, requestedMemoryBudgetBytes);
    std::cout << "Frames: " << files.size()
              << ", reference: "
              << (referenceIndex >= 0
                      ? QFileInfo(files[referenceIndex]).fileName().toStdString()
                      : std::string("automatic"))
              << ", method: " << method.toStdString()
              << ", estimated peak: " << estimatedBytes / kGiB << " GiB"
              << ", available memory: " << memoryInfo.availableBytes / kGiB << " GiB"
              << ", budget: " << effectiveMemoryBudget / kGiB << " GiB"
              << ", scratch: " << scratchBytes / kGiB << " GiB\n";
    QElapsedTimer timer;
    timer.start();
    worker.start();
    worker.wait();

    QJsonObject report;
    report["schemaVersion"] = 7;
    report["toolVersion"] = QCoreApplication::applicationVersion();
    report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["input"] = input;
    report["selectedFrames"] = files.size();
    report["requestedReferenceIndex"] = referenceIndex;
    report["referenceIndex"] = worker.selectedReferenceIndex();
    report["referenceFile"] =
        QFileInfo(worker.selectedReferenceFrame()).fileName();
    report["autoQualityRejectionEnabled"] =
        params.autoRejectLowQualityFrames;
    report["skyGroundSeparationEnabled"] = skyGroundEnabled;
    report["skyGroundMode"] = skyGroundEnabled
        ? (skyGroundMask.isEmpty() ? "automatic" : "user-mask")
        : "disabled";
    report["skyGroundMask"] = skyGroundMask;
    report["skyGroundMaskOutput"] = generatedSkyGroundMask;
    report["skyGroundFeatherRadius"] = skyGroundFeather;
    report["groundStackMethod"] = groundMethod;
    report["groundDetailStrength"] = params.groundDetailStrength;
    report["skyGroundSkyFraction"] = worker.skyGroundSkyFraction();
    report["skyGroundMaskSource"] = worker.skyGroundMaskSource();
    const QStringList rejectedFiles = worker.qualityRejectedFiles();
    report["qualityRejectedFrames"] = rejectedFiles.size();
    const QSet<QString> rejectedSet(rejectedFiles.begin(), rejectedFiles.end());
    QJsonArray qualityFrames;
    const std::vector<FrameQualityMetrics>& qualityMetrics =
        worker.frameQualityMetrics();
    for (int index = 0; index < files.size(); ++index) {
        QJsonObject quality;
        quality["index"] = index;
        quality["file"] = QFileInfo(files[index]).fileName();
        quality["rejected"] = rejectedSet.contains(files[index]);
        if (static_cast<size_t>(index) < qualityMetrics.size()) {
            const FrameQualityMetrics& metrics =
                qualityMetrics[static_cast<size_t>(index)];
            quality["valid"] = metrics.valid;
            quality["detectedStars"] = metrics.detectedStars;
            quality["usableStars"] = metrics.usableStars;
            quality["medianFwhm"] = metrics.medianFwhm;
            quality["medianEllipticity"] = metrics.medianEllipticity;
            quality["medianFlux"] = metrics.medianFlux;
            quality["backgroundMedian"] = metrics.backgroundMedian;
            quality["backgroundNoise"] = metrics.backgroundNoise;
            quality["clippedFraction"] = metrics.clippedFraction;
            quality["score"] = metrics.score;
        } else {
            quality["valid"] = false;
        }
        qualityFrames.append(quality);
    }
    report["frameQuality"] = qualityFrames;
    report["method"] = method;
    report["kappa"] = kappa;
    report["photometricNormalizationEnabled"] =
        params.photometricNormalizationEnabled;
    report["photometricNormalizedFrames"] =
        worker.photometricNormalizedFrameCount();
    report["photometricSkippedFrames"] =
        worker.photometricSkippedFrameCount();
    report["photometricAverageGain"] = worker.averagePhotometricGain();
    report["photometricMinimumGain"] = worker.minimumPhotometricGain();
    report["photometricMaximumGain"] = worker.maximumPhotometricGain();
    report["photometricMaximumAbsoluteOffset"] =
        worker.maximumPhotometricOffset();
    report["photometricOutputAnchorGain"] =
        worker.photometricOutputAnchorGain();
    report["photometricOutputAnchorMaximumAbsoluteOffset"] =
        worker.photometricOutputAnchorOffset();
    report["denoiseStrength"] = denoiseStrength;
    report["dehazeStrength"] = dehazeStrength;
    report["stretchEnabled"] = stretchEnabled;
    report["starReduceStrength"] = starReduceStrength;
    const StarReductionStats& reductionStats = worker.starReductionStats();
    report["starReductionDetectedStars"] =
        QString::number(reductionStats.detectedStars);
    report["starReductionProcessedStars"] =
        QString::number(reductionStats.processedStars);
    report["starReductionStronglySuppressedStars"] =
        QString::number(reductionStats.stronglySuppressedStars);
    report["starReductionAffectedPixels"] =
        QString::number(reductionStats.affectedPixels);
    report["starReductionAverageInputFwhm"] =
        reductionStats.averageInputFwhm;
    report["starReductionRadiusScale"] = reductionStats.radiusScale;
    report["estimatedPeakBytes"] = QString::number(estimatedBytes);
    report["estimatedScratchBytes"] = QString::number(scratchBytes);
    report["physicalMemoryBytes"] = QString::number(memoryInfo.totalBytes);
    report["availableMemoryBytes"] = QString::number(memoryInfo.availableBytes);
    report["memoryBudgetBytes"] = QString::number(effectiveMemoryBudget);
    report["memoryBudgetMode"] =
        requestedMemoryBudgetBytes > 0 ? "override" : "automatic";
    report["elapsedMs"] = timer.elapsed();
    report["stackingElapsedMs"] = worker.stackingElapsedMs();
    const double stackedChannelSamples =
        static_cast<double>(worker.stackedWidth()) * worker.stackedHeight() * 3.0;
    report["stackingMillionChannelSamplesPerSecond"] =
        worker.stackingElapsedMs() > 0
            ? stackedChannelSamples / worker.stackingElapsedMs() / 1000.0
            : 0.0;
    report["processedFrames"] = worker.stackedFrameCount();
    report["referenceFrames"] = worker.stackedFrameCount() > 0 ? 1 : 0;
    report["transformedSourceFrames"] =
        worker.affineFrameCount() + worker.homographyFrameCount();
    report["affineAlignedFrames"] = worker.affineFrameCount();
    report["homographyAlignedFrames"] = worker.homographyFrameCount();
    report["averageAlignmentRms"] = worker.averageAlignmentRms();
    report["worstAlignmentP95"] = worker.worstAlignmentP95();
    report["minimumAlignmentGridCoverage"] =
        worker.minimumAlignmentGridCoverage();
    report["width"] = worker.stackedWidth();
    report["height"] = worker.stackedHeight();
    report["cropOffsetX"] = worker.cropOffsetX();
    report["cropOffsetY"] = worker.cropOffsetY();
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
