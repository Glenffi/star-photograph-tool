#include "core/ProcessingMemoryEstimator.h"
#include "core/PreviewToneMapper.h"
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
#include <QImage>
#include <QSet>

#include <algorithm>
#include <array>
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

uint16_t histogramPercentile(const std::vector<uint64_t>& histogram,
                             uint64_t total, uint64_t numerator,
                             uint64_t denominator) {
    if (total == 0 || denominator == 0) return 0;
    const uint64_t target = std::min(
        total - 1, ((total - 1) / denominator) * numerator +
                       (((total - 1) % denominator) * numerator) / denominator);
    uint64_t cumulative = 0;
    for (size_t value = 0; value < histogram.size(); ++value) {
        cumulative += histogram[value];
        if (cumulative > target) return static_cast<uint16_t>(value);
    }
    return 65535;
}

QJsonObject resultStatistics(const std::vector<uint16_t>& rgb) {
    QJsonObject result;
    if (rgb.empty() || rgb.size() % 3 != 0) return result;
    const uint64_t pixels = rgb.size() / 3;
    std::array<std::vector<uint64_t>, 3> channels;
    for (auto& histogram : channels) histogram.resize(65536);
    std::vector<uint64_t> luminance(65536);
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        const size_t base = pixel * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            ++channels[channel][rgb[base + channel]];
        }
        const uint32_t value =
            13933U * rgb[base] + 46871U * rgb[base + 1] +
            4732U * rgb[base + 2];
        ++luminance[value >> 16];
    }

    QJsonArray medians;
    for (const auto& histogram : channels) {
        medians.append(histogramPercentile(histogram, pixels, 1, 2));
    }
    result["channelMedians"] = medians;
    const std::array<std::pair<const char*, std::pair<uint64_t, uint64_t>>, 9>
        requested = {{
            {"p0_1", {1, 1000}}, {"p1", {1, 100}},
            {"p10", {1, 10}}, {"p50", {1, 2}},
            {"p90", {9, 10}}, {"p99", {99, 100}},
            {"p99_5", {995, 1000}}, {"p99_95", {9995, 10000}},
            {"p99_99", {9999, 10000}}
        }};
    QJsonObject luminancePercentiles;
    for (const auto& [name, fraction] : requested) {
        luminancePercentiles[name] = histogramPercentile(
            luminance, pixels, fraction.first, fraction.second);
    }
    result["luminancePercentiles"] = luminancePercentiles;
    return result;
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
    const QCommandLineOption startIndexOption(
        "start-index",
        "Skip this many sorted RAW files before applying --limit.",
        "index", "0");
    const QCommandLineOption singleOption(
        "single", "Refine the first RAW without alignment or stacking.");
    const QCommandLineOption timelapseOption(
        "timelapse", "Denoise every RAW with an aligned temporal window.");
    const QCommandLineOption darkDirectoryOption(
        "dark-dir", "Directory containing matched Dark RAW frames.",
        "directory");
    const QCommandLineOption flatDirectoryOption(
        "flat-dir", "Directory containing Flat RAW frames.",
        "directory");
    const QCommandLineOption biasDirectoryOption(
        "bias-dir", "Directory containing Bias RAW frames.",
        "directory");
    const QCommandLineOption timelapseWindowOption(
        "timelapse-window", "Temporal window size: 3 or 5.", "count", "5");
    const QCommandLineOption timelapseStrengthOption(
        "timelapse-strength", "Temporal denoise strength from 0 to 100.",
        "value", "80");
    const QCommandLineOption timelapseMotionProtectionOption(
        "timelapse-motion-protection",
        "Protect clouds, foliage, and lights from temporal ghosting (0-100).",
        "value", "75");
    const QCommandLineOption timelapseNoGroundOption(
        "timelapse-no-ground",
        "Treat the complete timelapse frame as sky; do not protect fixed ground.");
    const QCommandLineOption starTrailOption(
        "star-trail",
        "Composite at least three fixed-tripod RAW frames into star trails.");
    const QCommandLineOption starTrailCometStrengthOption(
        "star-trail-comet-strength",
        "Star-trail comet taper strength from 0 to 100; 0 uses Lighten.",
        "value", "0");
    const QCommandLineOption starTrailReverseOption(
        "star-trail-reverse",
        "Reverse the frame order used by the star-trail comet taper.");
    const QCommandLineOption starTrailNoGroundProtectionOption(
        "star-trail-no-ground-protection",
        "Apply the star-trail composite to the complete frame without fixed-ground protection.");
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
    const QCommandLineOption modifiedCameraColorOption(
        "restore-modified-camera-color",
        "Restore a neutral color response for BCF/astronomy-modified cameras.");
    const QCommandLineOption modifiedCameraColorStrengthOption(
        "modified-camera-color-strength",
        "Modified-camera color correction strength (0-100).",
        "value", "100");
    const QCommandLineOption modifiedCameraGrayPointOption(
        "modified-camera-gray-point",
        "Manual normalized gray point as x,y; implies color restoration.",
        "x,y");
    const QCommandLineOption stretchOption(
        "stretch",
        "Apply background neutralization and linked RGB Arcsinh stretch.");
    const QCommandLineOption starReduceOption(
        "star-reduce-strength",
        "Enable star reduction at strength 1-100; 0 disables it.",
        "value", "0");
    parser.addOptions({inputOption, outputOption, limitOption,
                       startIndexOption, singleOption,
                       timelapseOption, darkDirectoryOption,
                       flatDirectoryOption, biasDirectoryOption,
                       timelapseWindowOption,
                       timelapseStrengthOption,
                       timelapseMotionProtectionOption,
                       timelapseNoGroundOption,
                       starTrailOption,
                       starTrailCometStrengthOption,
                       starTrailReverseOption,
                       starTrailNoGroundProtectionOption,
                       referenceOption,
                       methodOption, kappaOption, memoryBudgetOption,
                       noPhotometricNormalizationOption,
                       noQualityRejectionOption,
                       skyGroundOption, skyGroundMaskOption,
                       skyGroundFeatherOption, groundMethodOption,
                       groundDetailOption,
                       denoiseOption, dehazeOption,
                       modifiedCameraColorOption,
                       modifiedCameraColorStrengthOption,
                       modifiedCameraGrayPointOption, stretchOption,
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
    bool startIndexOk = false;
    const int startIndex =
        parser.value(startIndexOption).toInt(&startIndexOk);
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
    bool modifiedCameraColorStrengthOk = false;
    const int modifiedCameraColorStrength =
        parser.value(modifiedCameraColorStrengthOption).toInt(
            &modifiedCameraColorStrengthOk);
    const QString modifiedCameraGrayPointText =
        parser.value(modifiedCameraGrayPointOption).trimmed();
    double modifiedCameraGrayPointX = 0.5;
    double modifiedCameraGrayPointY = 0.5;
    bool modifiedCameraGrayPointOk = true;
    if (!modifiedCameraGrayPointText.isEmpty()) {
        const QStringList components =
            modifiedCameraGrayPointText.split(',');
        bool xOk = false;
        bool yOk = false;
        if (components.size() == 2) {
            modifiedCameraGrayPointX = components[0].toDouble(&xOk);
            modifiedCameraGrayPointY = components[1].toDouble(&yOk);
        }
        modifiedCameraGrayPointOk = xOk && yOk &&
            modifiedCameraGrayPointX >= 0.0 &&
            modifiedCameraGrayPointX <= 1.0 &&
            modifiedCameraGrayPointY >= 0.0 &&
            modifiedCameraGrayPointY <= 1.0;
    }
    const bool modifiedCameraColorEnabled =
        parser.isSet(modifiedCameraColorOption) ||
        !modifiedCameraGrayPointText.isEmpty();
    const bool singleFrameMode = parser.isSet(singleOption);
    const bool timelapseMode = parser.isSet(timelapseOption);
    const bool starTrailMode = parser.isSet(starTrailOption);
    bool starTrailCometStrengthOk = false;
    const int starTrailCometStrength =
        parser.value(starTrailCometStrengthOption).toInt(
            &starTrailCometStrengthOk);
    const bool starTrailReverse = parser.isSet(starTrailReverseOption);
    const bool starTrailProtectGround =
        !parser.isSet(starTrailNoGroundProtectionOption);
    const QString darkDirectory = parser.value(darkDirectoryOption);
    const QString flatDirectory = parser.value(flatDirectoryOption);
    const QString biasDirectory = parser.value(biasDirectoryOption);
    const bool deepSkyCalibration = !darkDirectory.isEmpty() ||
        !flatDirectory.isEmpty() || !biasDirectory.isEmpty();
    const bool timelapseProtectGround =
        !parser.isSet(timelapseNoGroundOption);
    bool timelapseWindowOk = false;
    const int timelapseWindow =
        parser.value(timelapseWindowOption).toInt(&timelapseWindowOk);
    bool timelapseStrengthOk = false;
    const int timelapseStrength =
        parser.value(timelapseStrengthOption).toInt(&timelapseStrengthOk);
    bool timelapseMotionProtectionOk = false;
    const int timelapseMotionProtection =
        parser.value(timelapseMotionProtectionOption).toInt(
            &timelapseMotionProtectionOk);
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
    if (deepSkyCalibration &&
        (darkDirectory.isEmpty() || flatDirectory.isEmpty() ||
         biasDirectory.isEmpty())) {
        std::cerr << "Deep-sky calibration requires --dark-dir, --flat-dir, "
                     "and --bias-dir together.\n";
        return 2;
    }
    if (deepSkyCalibration &&
        (!QDir(darkDirectory).exists() || !QDir(flatDirectory).exists() ||
         !QDir(biasDirectory).exists())) {
        std::cerr << "Every Dark, Flat, and Bias directory must exist.\n";
        return 2;
    }
    if (deepSkyCalibration &&
        (singleFrameMode || timelapseMode || starTrailMode ||
         skyGroundEnabled)) {
        std::cerr << "Deep-sky calibration cannot be combined with --single, "
                     "--timelapse, --star-trail, or sky/ground separation.\n";
        return 2;
    }
    if (starTrailMode &&
        (singleFrameMode || timelapseMode || skyGroundEnabled)) {
        std::cerr << "--star-trail cannot be combined with --single, "
                     "--timelapse, deep-sky calibration, or --sky-ground.\n";
        return 2;
    }
    if (starTrailMode && starReduceStrength > 0) {
        std::cerr << "--star-trail cannot be combined with "
                     "--star-reduce-strength.\n";
        return 2;
    }
    if (timelapseMode && modifiedCameraColorEnabled) {
        std::cerr << "Modified-camera color restoration is not yet available "
                     "for timelapse sequence output.\n";
        return 2;
    }
    if (!limitOk || limit < 0 || !startIndexOk || startIndex < 0 ||
        !referenceOk || referenceIndex < -1 ||
        !kappaOk || kappa <= 0.0 || !memoryBudgetOk ||
        !denoiseOk || denoiseStrength < 0 || denoiseStrength > 70 ||
        !dehazeOk || dehazeStrength < 0 || dehazeStrength > 100 ||
        !modifiedCameraColorStrengthOk ||
        modifiedCameraColorStrength < 0 ||
        modifiedCameraColorStrength > 100 ||
        !modifiedCameraGrayPointOk ||
        !starReduceOk || starReduceStrength < 0 ||
        starReduceStrength > 100 ||
        !starTrailCometStrengthOk || starTrailCometStrength < 0 ||
        starTrailCometStrength > 100 ||
        !skyGroundFeatherOk || skyGroundFeather < 0 ||
        skyGroundFeather > 50 ||
        (!skyGroundMask.isEmpty() && !QFileInfo::exists(skyGroundMask)) ||
        !validGroundMethod(groundMethod) ||
        !groundDetailOk || groundDetailStrength < 0 ||
        groundDetailStrength > 70 ||
        memoryBudgetMiB >
            std::numeric_limits<uint64_t>::max() / (1024ULL * 1024ULL) ||
        !validMethod(method) ||
        (singleFrameMode && timelapseMode) ||
        (timelapseMode && skyGroundEnabled) ||
        !timelapseWindowOk ||
        (timelapseWindow != 3 && timelapseWindow != 5) ||
        !timelapseStrengthOk || timelapseStrength < 0 ||
        timelapseStrength > 100 ||
        !timelapseMotionProtectionOk || timelapseMotionProtection < 0 ||
        timelapseMotionProtection > 100) {
        std::cerr << "Invalid or incompatible --start-index, --limit, "
                     "--reference-index, "
                     "--method, --kappa, "
                     "--memory-budget-mib, --denoise-strength, "
                     "--dehaze-strength, modified-camera color, or "
                     "--star-reduce-strength/timelapse/star-trail/sky-ground "
                     "options.\n";
        return 2;
    }
    const uint64_t requestedMemoryBudgetBytes =
        static_cast<uint64_t>(memoryBudgetMiB) * 1024ULL * 1024ULL;

    QStringList files = rawFiles(input);
    if (startIndex >= files.size()) {
        std::cerr << "--start-index is outside the input sequence.\n";
        return 2;
    }
    if (startIndex > 0) files = files.mid(startIndex);
    if (limit > 0 && files.size() > limit) files = files.mid(0, limit);
    if (singleFrameMode && files.size() > 1) files = files.mid(0, 1);
    const int minimumFrames = singleFrameMode
        ? 1 : ((timelapseMode || starTrailMode) ? 3 : 2);
    if (files.size() < minimumFrames) {
        std::cerr << (singleFrameMode
            ? "At least one RAW file is required.\n"
            : timelapseMode
                ? "At least three RAW files are required for timelapse.\n"
                : starTrailMode
                    ? "At least three RAW files are required for star trails.\n"
                : "At least two RAW files are required.\n");
        return 2;
    }
    if (referenceIndex >= files.size()) {
        std::cerr << "--reference-index is outside the selected sequence.\n";
        return 2;
    }
    const QStringList darkFrames = deepSkyCalibration
        ? rawFiles(darkDirectory) : QStringList();
    const QStringList flatFrames = deepSkyCalibration
        ? rawFiles(flatDirectory) : QStringList();
    const QStringList biasFrames = deepSkyCalibration
        ? rawFiles(biasDirectory) : QStringList();
    if (deepSkyCalibration &&
        (darkFrames.size() < 3 || flatFrames.size() < 3 ||
         biasFrames.size() < 3)) {
        std::cerr << "Deep-sky calibration requires at least three RAW files "
                     "in each Dark, Flat, and Bias directory.\n";
        return 2;
    }

    RawImageLoader loader;
    RawImageLoader::Metadata metadata;
    if (!loader.loadMetadata(files.front().toStdString(), metadata)) {
        std::cerr << "Cannot read first-frame metadata.\n";
        return 3;
    }
    ProcessingMemoryEstimator::EstimateOptions estimateOptions;
    // Star trails stream source frames and do not retain a stacking chunk per
    // input frame. One frame still lets the estimator cover decode, composite,
    // optional ground protection, and finishing-stage resident buffers.
    estimateOptions.frameCount = starTrailMode ? 1 : files.size();
    estimateOptions.skyGroundSeparation =
        (skyGroundEnabled && !singleFrameMode && !timelapseMode) ||
        (starTrailMode && starTrailProtectGround);
    estimateOptions.noiseReduction = denoiseStrength > 0;
    estimateOptions.modifiedCameraColor = modifiedCameraColorEnabled;
    estimateOptions.dehaze = dehazeStrength > 0;
    estimateOptions.stretch = stretchEnabled;
    estimateOptions.starReduction = starReduceStrength > 0;
    estimateOptions.rawCalibration = deepSkyCalibration;
    const uint64_t estimatedBytes = timelapseMode
        ? ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
              metadata.width, metadata.height, timelapseWindow,
              timelapseProtectGround, timelapseMotionProtection > 0)
        : ProcessingMemoryEstimator::estimatePeakBytes(
              metadata.width, metadata.height, estimateOptions);
    const uint64_t scratchBytes = (singleFrameMode || starTrailMode) ? 0
        : ProcessingMemoryEstimator::estimateScratchDiskBytes(
              metadata.width, metadata.height, files.size(),
              skyGroundEnabled && !timelapseMode);

    ProcessingWorker::Params params;
    params.singleFrameMode = singleFrameMode;
    params.timelapseMode = timelapseMode;
    params.starTrailMode = starTrailMode;
    params.deepSkyMode = deepSkyCalibration;
    params.darkFramePaths = darkFrames;
    params.flatFramePaths = flatFrames;
    params.biasFramePaths = biasFrames;
    params.timelapseWindowSize = timelapseWindow;
    params.timelapseStrength = timelapseStrength;
    params.timelapseMotionProtection = timelapseMotionProtection;
    params.timelapseProtectGround = timelapseProtectGround;
    params.starTrailCometStrength = starTrailCometStrength;
    params.starTrailReverse = starTrailReverse;
    params.starTrailProtectGround = starTrailProtectGround;
    params.stackMethod = method;
    params.kappaValue = kappa;
    params.outputFormat = "tiff16";
    params.outputPath = output;
    params.memoryBudgetBytes = requestedMemoryBudgetBytes;
    params.skyGroundSepEnabled =
        skyGroundEnabled && !singleFrameMode && !timelapseMode;
    params.skyGroundMode = skyGroundMask.isEmpty()
        ? SkyGroundMask::AutoDetect : SkyGroundMask::UserMask;
    params.userMaskPath = skyGroundMask;
    params.groundStackMethod = groundMethod;
    params.groundDetailStrength = skyGroundEnabled ? groundDetailStrength : 0;
    params.featherRadius = skyGroundFeather;
    const QString generatedSkyGroundMask =
        (params.skyGroundSepEnabled ||
         (params.timelapseMode && params.timelapseProtectGround) ||
         (params.starTrailMode && params.starTrailProtectGround))
        ? QDir(output).filePath("sky-ground-mask.png") : QString();
    params.skyGroundMaskOutputPath = generatedSkyGroundMask;
    params.autoRejectLowQualityFrames = !singleFrameMode && !timelapseMode &&
        !starTrailMode && !parser.isSet(noQualityRejectionOption);
    params.photometricNormalizationEnabled = !singleFrameMode &&
        !parser.isSet(noPhotometricNormalizationOption);
    params.noiseReductionEnabled = denoiseStrength > 0;
    params.noiseReductionStrength = denoiseStrength;
    params.modifiedCameraColorEnabled = modifiedCameraColorEnabled;
    params.modifiedCameraColor.strength = modifiedCameraColorStrength;
    if (!modifiedCameraGrayPointText.isEmpty()) {
        params.modifiedCameraColor.neutralMode =
            ModifiedCameraNeutralMode::ManualPoint;
        params.modifiedCameraColor.manualPointX = modifiedCameraGrayPointX;
        params.modifiedCameraColor.manualPointY = modifiedCameraGrayPointY;
    }
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
    const QString effectiveMethod = starTrailMode
        ? (starTrailCometStrength > 0
               ? QStringLiteral("star-trail-comet")
               : QStringLiteral("star-trail-lighten"))
        : timelapseMode
            ? QStringLiteral("temporal-mad-weighted-mean") : method;
    std::cout << "Frames: " << files.size()
              << ", reference: "
              << (referenceIndex >= 0
                      ? QFileInfo(files[referenceIndex]).fileName().toStdString()
                      : std::string("automatic"))
              << ", method: " << effectiveMethod.toStdString()
              << ", estimated peak: " << estimatedBytes / kGiB << " GiB"
              << ", available memory: " << memoryInfo.availableBytes / kGiB << " GiB"
              << ", budget: " << effectiveMemoryBudget / kGiB << " GiB"
              << ", scratch: " << scratchBytes / kGiB << " GiB\n";
    QElapsedTimer timer;
    timer.start();
    worker.start();
    worker.wait();

    QJsonObject report;
    report["schemaVersion"] = 17;
    report["toolVersion"] = QCoreApplication::applicationVersion();
    report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["input"] = input;
    report["startIndex"] = startIndex;
    report["selectedFrames"] = files.size();
    report["singleFrameMode"] = singleFrameMode;
    report["timelapseMode"] = timelapseMode;
    report["starTrailMode"] = starTrailMode;
    report["starTrailCometStrength"] = starTrailCometStrength;
    report["starTrailReverse"] = starTrailReverse;
    report["starTrailProtectGround"] = starTrailProtectGround;
    report["deepSkyCalibrationEnabled"] = deepSkyCalibration;
    report["darkFrames"] = darkFrames.size();
    report["flatFrames"] = flatFrames.size();
    report["biasFrames"] = biasFrames.size();
    report["calibratedLightFrames"] = worker.calibratedLightFrameCount();
    report["calibrationClippedLowPixels"] = QString::number(
        worker.calibrationClippedLowPixels());
    report["calibrationClippedHighPixels"] = QString::number(
        worker.calibrationClippedHighPixels());
    report["calibrationInvalidFlatPixels"] = QString::number(
        worker.calibrationInvalidFlatPixels());
    QJsonArray calibrationPreflightWarnings;
    for (const QString& warning : worker.calibrationPreflightWarnings()) {
        calibrationPreflightWarnings.append(warning);
    }
    report["calibrationPreflightWarnings"] =
        calibrationPreflightWarnings;
    report["timelapseWindow"] = timelapseWindow;
    report["timelapseStrength"] = timelapseStrength;
    report["timelapseMotionProtection"] = timelapseMotionProtection;
    report["timelapseProtectGround"] = params.timelapseProtectGround;
    report["timelapseMotionProtectedPixelEvaluations"] = QString::number(
        worker.timelapseMotionProtectedPixelEvaluations());
    report["timelapseFlickerCorrectedFrames"] =
        worker.timelapseFlickerCorrectedFrames();
    report["timelapseMaximumFlickerGainChange"] =
        worker.timelapseMaximumFlickerGainChange();
    report["timelapseMaximumFlickerOffset"] =
        worker.timelapseMaximumFlickerOffset();
    report["requestedReferenceIndex"] = referenceIndex;
    report["referenceIndex"] = worker.selectedReferenceIndex();
    report["referenceFile"] =
        QFileInfo(worker.selectedReferenceFrame()).fileName();
    report["autoQualityRejectionEnabled"] =
        params.autoRejectLowQualityFrames;
    const bool sequenceGroundApplied = (timelapseMode || starTrailMode) &&
        !worker.skyGroundMaskSource().isEmpty();
    report["skyGroundSeparationEnabled"] =
        params.skyGroundSepEnabled || sequenceGroundApplied;
    report["skyGroundMode"] = params.skyGroundSepEnabled
        ? (skyGroundMask.isEmpty() ? "automatic" : "user-mask")
        : sequenceGroundApplied ? "automatic" : "disabled";
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
    const double medianFrameEllipticity =
        FrameQualityEvaluator::medianValidEllipticity(qualityMetrics);
    const bool starShapeWarning =
        !singleFrameMode && !timelapseMode && !starTrailMode &&
        medianFrameEllipticity >= 0.22;
    report["medianFrameEllipticity"] = medianFrameEllipticity;
    report["starShapeWarning"] = starShapeWarning;
    report["starShapeWarningReason"] = starShapeWarning
        ? QString::fromUtf8("星点偏长，请检查单帧曝光拖线或镜头像差")
        : QString();
    if (starShapeWarning) {
        std::cerr << "Warning: median star ellipticity is "
                  << medianFrameEllipticity
                  << "; inspect single-frame trailing or lens aberration."
                  << std::endl;
    }
    QJsonArray skippedFrames;
    for (const ProcessingWorker::SkippedFrameInfo& skipped :
         worker.skippedFrames()) {
        QJsonObject item;
        item["file"] = QFileInfo(skipped.filePath).fileName();
        item["stage"] = skipped.stage;
        item["reason"] = skipped.reason;
        item["detectedStars"] = skipped.detectedStars;
        auto appendAlignmentCandidate = [](bool evaluated, int matchedStars,
                                           double rms, double p95,
                                           int eligibleCells, int coveredCells,
                                           double gridCoverage,
                                           const QStringList& reasons) {
            QJsonObject candidate;
            candidate["evaluated"] = evaluated;
            candidate["matchedStars"] = matchedStars;
            candidate["rms"] = rms;
            candidate["p95"] = p95;
            candidate["eligibleCells"] = eligibleCells;
            candidate["coveredCells"] = coveredCells;
            candidate["gridCoverage"] = gridCoverage;
            QJsonArray failureReasons;
            for (const QString& reason : reasons) {
                failureReasons.append(reason);
            }
            candidate["failureReasons"] = failureReasons;
            return candidate;
        };
        item["affine"] = appendAlignmentCandidate(
            skipped.affineEvaluated, skipped.affineMatchedStars,
            skipped.affineRms, skipped.affineP95,
            skipped.affineEligibleCells, skipped.affineCoveredCells,
            skipped.affineGridCoverage, skipped.affineFailureReasons);
        item["homography"] = appendAlignmentCandidate(
            skipped.homographyEvaluated, skipped.homographyMatchedStars,
            skipped.homographyRms, skipped.homographyP95,
            skipped.homographyEligibleCells,
            skipped.homographyCoveredCells,
            skipped.homographyGridCoverage,
            skipped.homographyFailureReasons);
        skippedFrames.append(item);
    }
    report["skippedFrames"] = skippedFrames;
    report["skippedFrameCount"] = skippedFrames.size();
    report["method"] = effectiveMethod;
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
    report["modifiedCameraColorEnabled"] = modifiedCameraColorEnabled;
    report["modifiedCameraColorStrength"] = modifiedCameraColorStrength;
    report["modifiedCameraColorMode"] =
        modifiedCameraGrayPointText.isEmpty() ? "automatic" : "manual";
    const ModifiedCameraColorStats& colorStats =
        worker.modifiedCameraColorStats();
    report["modifiedCameraColorApplied"] = colorStats.applied;
    report["modifiedCameraColorSampleCount"] = QString::number(
        colorStats.sampleCount);
    QJsonArray neutralSample;
    QJsonArray colorGains;
    for (size_t channel = 0; channel < 3; ++channel) {
        neutralSample.append(colorStats.neutralSample[channel]);
        colorGains.append(colorStats.gains[channel]);
    }
    report["modifiedCameraColorNeutralSample"] = neutralSample;
    report["modifiedCameraColorGains"] = colorGains;
    report["modifiedCameraColorClippedChannelValues"] = QString::number(
        colorStats.clippedChannelValues);
    report["modifiedCameraColorUsedManualPoint"] =
        colorStats.usedManualPoint;
    QJsonArray colorSamplePoint;
    colorSamplePoint.append(colorStats.samplePointX);
    colorSamplePoint.append(colorStats.samplePointY);
    report["modifiedCameraColorSamplePoint"] = colorSamplePoint;
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
    report["starReductionDefringedPixels"] =
        QString::number(reductionStats.defringedPixels);
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
    report["timelapseAlignedPairs"] = timelapseMode
        ? worker.affineFrameCount() + worker.homographyFrameCount() : 0;
    report["averageAlignmentRms"] = worker.averageAlignmentRms();
    report["worstAlignmentP95"] = worker.worstAlignmentP95();
    report["minimumAlignmentGridCoverage"] =
        worker.minimumAlignmentGridCoverage();
    report["width"] = worker.stackedWidth();
    report["height"] = worker.stackedHeight();
    report["quickPreviewAvailable"] = worker.quickPreviewAvailable();
    report["quickPreviewMaskAvailable"] =
        worker.quickPreviewMaskAvailable();
    report["quickPreviewWidth"] = worker.quickPreviewWidth();
    report["quickPreviewHeight"] = worker.quickPreviewHeight();
    report["cropOffsetX"] = worker.cropOffsetX();
    report["cropOffsetY"] = worker.cropOffsetY();
    report["outputFile"] = worker.outputFile();
    report["error"] = worker.errorString();
    report["success"] = worker.errorString().isEmpty() && !worker.outputFile().isEmpty();

    std::vector<uint16_t> resultRgb = worker.takeStackedData();
    const bool hasFinishingStage =
        (params.noiseReductionEnabled && params.noiseReductionStrength > 0) ||
        params.modifiedCameraColorEnabled || params.dewarpEnabled ||
        params.stretchEnabled ||
        (params.skyGroundSepEnabled && params.groundDetailStrength > 0) ||
        (params.starReduceEnabled && params.starReduceStrength > 0);
    const PreviewImage8 preview = hasFinishingStage
        ? PreviewToneMapper::mapRgb16WithRange(
              resultRgb, worker.stackedWidth(), worker.stackedHeight(),
              0, 65535, 2400)
        : PreviewToneMapper::mapRgb16(
              resultRgb, worker.stackedWidth(), worker.stackedHeight(), 2400);
    const QString previewPath = QDir(output).filePath("result-preview.png");
    bool previewSaved = false;
    if (!preview.rgb.empty()) {
        const QImage borrowed(
            preview.rgb.data(), preview.width, preview.height,
            preview.width * 3, QImage::Format_RGB888);
        previewSaved = borrowed.copy().save(previewPath, "PNG");
    }
    report["previewSaved"] = previewSaved;
    report["previewFile"] = previewSaved ? previewPath : QString();
    report["previewBlackPoint"] = preview.blackPoint;
    report["previewWhitePoint"] = preview.whitePoint;
    report["previewMode"] = hasFinishingStage
        ? "fixed-full-range" : "automatic-range";
    report["resultStatistics"] = resultStatistics(resultRgb);

    const QString reportPath = QDir(output).filePath("pipeline-report.json");
    QFile reportFile(reportPath);
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Cannot write pipeline report.\n";
        return 4;
    }
    reportFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));

    // The owned result is released here; the TIFF and bounded PNG remain as
    // durable artifacts for numeric and visual regression checks.
    resultRgb.clear();
    if (!worker.errorString().isEmpty()) {
        std::cerr << "Pipeline failed: " << worker.errorString().toStdString() << '\n';
        return 5;
    }
    std::cout << "Output: " << worker.outputFile().toStdString() << '\n'
              << "Report: " << reportPath.toStdString() << '\n'
              << "Elapsed: " << timer.elapsed() / 1000.0 << " s\n";
    return 0;
}
