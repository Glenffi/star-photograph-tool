#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/PreviewToneMapper.h"
#include "core/ProcessingMemoryEstimator.h"
#include "core/RawImageLoader.h"
#include "core/StarDetector.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

const QStringList kCategories = {
    "galaxy-sequence", "sky-ground", "portrait", "difficult", "other-camera"
};

struct ReferenceFrame {
    bool valid = false;
    QString path;
    int width = 0;
    int height = 0;
    std::vector<StarPoint> stars;
};

struct Summary {
    int files = 0;
    int metadataPassed = 0;
    int previewPassed = 0;
    int fullDecodePassed = 0;
    int starDetectionPassed = 0;
    int alignmentAttempted = 0;
    int alignmentPassed = 0;
};

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

QString relativePath(const QString& root, const QString& path) {
    return QDir(root).relativeFilePath(path);
}

QString safeBaseName(const QString& filePath) {
    QString name = QFileInfo(filePath).completeBaseName();
    for (qsizetype i = 0; i < name.size(); ++i) {
        QChar character = name[i];
        if (!character.isLetterOrNumber() && character != '-' && character != '_') {
            name[i] = '_';
        }
    }
    return name.left(80);
}

bool saveRgbImage(const uint8_t* bytes, int width, int height,
                  const QString& path, const char* format) {
    if (!bytes || width <= 0 || height <= 0) return false;
    const QImage borrowed(bytes, width, height, width * 3, QImage::Format_RGB888);
    QDir().mkpath(QFileInfo(path).absolutePath());
    return borrowed.copy().save(path, format);
}

bool saveBrowserPreview(const RawImageLoader::PreviewData& preview,
                        const QString& path) {
    QImage image;
    if (preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
        image = QImage::fromData(preview.bytes.data(),
                                 static_cast<int>(preview.bytes.size()), "JPEG");
    } else if (preview.width > 0 && preview.height > 0 && !preview.bytes.empty()) {
        const QImage borrowed(preview.bytes.data(), preview.width, preview.height,
                              preview.width * 3, QImage::Format_RGB888);
        image = borrowed.copy();
    }
    if (image.isNull()) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    return image.save(path, "JPG", 90);
}

QJsonObject metadataJson(const RawImageLoader::Metadata& metadata) {
    QJsonObject json;
    json["width"] = metadata.width;
    json["height"] = metadata.height;
    json["orientation"] = metadata.height > metadata.width ? "portrait" : "landscape";
    json["cameraModel"] = QString::fromStdString(metadata.cameraModel);
    json["iso"] = metadata.iso;
    json["exposureSeconds"] = metadata.exposureTime;
    json["aperture"] = metadata.aperture;
    json["focalLengthMm"] = metadata.focalLength;
    json["timestamp"] = QString::fromStdString(metadata.timestamp);
    return json;
}

QJsonObject transformJson(const AlignmentTransform& transform) {
    QJsonObject json;
    json["a"] = transform.a;
    json["b"] = transform.b;
    json["c"] = transform.c;
    json["d"] = transform.d;
    json["e"] = transform.e;
    json["f"] = transform.f;
    return json;
}

QJsonObject inspectFile(const QString& sampleRoot,
                        const QString& path,
                        const QString& outputRoot,
                        bool quick,
                        ReferenceFrame& reference,
                        Summary& summary) {
    QJsonObject result;
    const QString sampleRelativePath = relativePath(sampleRoot, path);
    const QString previewGroup = QFileInfo(sampleRelativePath).path();
    result["path"] = sampleRelativePath;
    ++summary.files;

    RawImageLoader loader;
    RawImageLoader::Metadata metadata;
    QElapsedTimer timer;
    timer.start();
    const bool metadataOk = loader.loadMetadata(path.toStdString(), metadata);
    result["metadataMs"] = static_cast<double>(timer.elapsed());
    result["metadataOk"] = metadataOk;
    if (metadataOk) {
        ++summary.metadataPassed;
        result["metadata"] = metadataJson(metadata);
    }

    RawImageLoader::PreviewData preview;
    timer.restart();
    const bool previewOk = loader.loadPreview(path.toStdString(), 1600, preview);
    result["previewMs"] = static_cast<double>(timer.elapsed());
    result["previewOk"] = previewOk;
    if (previewOk) {
        ++summary.previewPassed;
        result["previewWidth"] = preview.width;
        result["previewHeight"] = preview.height;
        const QString previewPath = QDir(outputRoot).filePath(
            "previews/" + previewGroup + "/" + safeBaseName(path) + "-browser.jpg");
        result["previewSaved"] = saveBrowserPreview(preview, previewPath);
        result["previewOutput"] = relativePath(outputRoot, previewPath);
    }

    if (quick) {
        result["fullDecode"] = "skipped-quick-mode";
        return result;
    }

    RawImageLoader::ImageData image;
    timer.restart();
    const bool fullOk = loader.loadRaw(path.toStdString(), image);
    result["fullDecodeMs"] = static_cast<double>(timer.elapsed());
    result["fullDecodeOk"] = fullOk;
    if (!fullOk) return result;

    ++summary.fullDecodePassed;
    result["processedWidth"] = image.width;
    result["processedHeight"] = image.height;
    result["orientationApplied"] = metadataOk &&
        metadata.width == image.height && metadata.height == image.width;

    const PreviewImage8 fullPreview = PreviewToneMapper::mapRgb16(
        image.data, image.width, image.height, 2048);
    const QString fullPreviewPath = QDir(outputRoot).filePath(
        "previews/" + previewGroup + "/" + safeBaseName(path) + "-linear-preview.png");
    const bool fullPreviewSaved = !fullPreview.rgb.empty() &&
        saveRgbImage(fullPreview.rgb.data(), fullPreview.width, fullPreview.height,
                     fullPreviewPath, "PNG");
    result["fullPreviewSaved"] = fullPreviewSaved;
    result["fullPreviewOutput"] = relativePath(outputRoot, fullPreviewPath);

    std::vector<uint16_t> luminance;
    const bool luminanceOk = ImageBufferUtils::extractLuminance(
        image.data, image.width, image.height, luminance);
    result["luminanceOk"] = luminanceOk;
    image.data.clear();
    image.data.shrink_to_fit();
    if (!luminanceOk) return result;

    DetectionOptions options;
    options.spatiallyBalanced = true;
    options.maxCandidates = 30;
    options.maxStars = 30;
    std::vector<StarPoint> stars;
    StarDetector detector;
    timer.restart();
    const bool starsOk = detector.detect(luminance, image.width, image.height,
                                         stars, options);
    result["starDetectionMs"] = static_cast<double>(timer.elapsed());
    result["starDetectionOk"] = starsOk;
    result["alignmentStarCount"] = static_cast<int>(stars.size());
    if (starsOk) ++summary.starDetectionPassed;

    if (!reference.valid && starsOk) {
        reference.valid = true;
        reference.path = path;
        reference.width = image.width;
        reference.height = image.height;
        reference.stars = stars;
        result["alignment"] = "reference";
        result["alignmentReference"] = relativePath(sampleRoot, reference.path);
    } else if (reference.valid && starsOk) {
        result["alignmentReference"] = relativePath(sampleRoot, reference.path);
        ++summary.alignmentAttempted;
        if (reference.width != image.width || reference.height != image.height) {
            result["alignment"] = "dimension-mismatch";
        } else {
            AlignmentTransform transform;
            ImageAligner aligner;
            timer.restart();
            const bool aligned = aligner.align(reference.stars, stars, transform);
            result["alignmentMs"] = static_cast<double>(timer.elapsed());
            result["alignment"] = aligned ? "passed" : "failed";
            if (aligned) {
                ++summary.alignmentPassed;
                result["transform"] = transformJson(transform);
            }
        }
    } else {
        result["alignment"] = "not-attempted";
    }
    return result;
}

QJsonObject summaryJson(const Summary& summary) {
    QJsonObject json;
    json["files"] = summary.files;
    json["metadataPassed"] = summary.metadataPassed;
    json["previewPassed"] = summary.previewPassed;
    json["fullDecodePassed"] = summary.fullDecodePassed;
    json["starDetectionPassed"] = summary.starDetectionPassed;
    json["alignmentAttempted"] = summary.alignmentAttempted;
    json["alignmentPassed"] = summary.alignmentPassed;
    json["alignmentRate"] = summary.alignmentAttempted > 0
        ? static_cast<double>(summary.alignmentPassed) / summary.alignmentAttempted
        : 0.0;
    return json;
}

QHash<QString, QJsonObject> filesByPath(const QJsonObject& report) {
    QHash<QString, QJsonObject> files;
    for (const QJsonValue& categoryValue : report["categories"].toArray()) {
        for (const QJsonValue& fileValue : categoryValue.toObject()["files"].toArray()) {
            const QJsonObject file = fileValue.toObject();
            files.insert(file["path"].toString(), file);
        }
    }
    return files;
}

QJsonObject compareBaseline(const QJsonObject& baseline, const QJsonObject& current) {
    const QHash<QString, QJsonObject> baselineFiles = filesByPath(baseline);
    const QHash<QString, QJsonObject> currentFiles = filesByPath(current);
    QJsonArray failures;
    int compared = 0;

    auto addFailure = [&](const QString& path, const QString& reason) {
        QJsonObject failure;
        failure["path"] = path;
        failure["reason"] = reason;
        failures.append(failure);
    };

    for (auto iterator = baselineFiles.cbegin(); iterator != baselineFiles.cend(); ++iterator) {
        const QString path = iterator.key();
        if (!currentFiles.contains(path)) {
            addFailure(path, "sample-missing");
            continue;
        }
        ++compared;
        const QJsonObject previous = iterator.value();
        const QJsonObject now = currentFiles.value(path);
        for (const QString& field : {"metadataOk", "previewOk", "fullDecodeOk",
                                     "starDetectionOk"}) {
            if (previous[field].toBool() && !now[field].toBool()) {
                addFailure(path, field + " regressed from true to false");
            }
        }
        if (previous.contains("processedWidth") &&
            (previous["processedWidth"] != now["processedWidth"] ||
             previous["processedHeight"] != now["processedHeight"])) {
            addFailure(path, "processed dimensions changed");
        }
        if (previous["alignment"].toString() == "passed" &&
            now["alignment"].toString() != "passed") {
            addFailure(path, "alignment no longer passes");
        }
        const int previousStars = previous["alignmentStarCount"].toInt();
        const int currentStars = now["alignmentStarCount"].toInt();
        if (previousStars >= 6 && currentStars * 2 < previousStars) {
            addFailure(path, "alignment star count dropped by more than 50%");
        }
    }

    QJsonObject comparison;
    comparison["baselineFiles"] = baselineFiles.size();
    comparison["comparedFiles"] = compared;
    comparison["passed"] = failures.isEmpty();
    comparison["failures"] = failures;
    return comparison;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("StarProcessorSampleRegression");
    QCoreApplication::setApplicationVersion(STARPROCESSOR_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("Run StarProcessor regression checks on local RAW samples.");
    parser.addHelpOption();
    const QCommandLineOption samplesOption({"s", "samples"},
        "Root containing the five sample categories.", "directory");
    const QCommandLineOption outputOption({"o", "output"},
        "Directory for report.json and preview images.", "directory");
    const QCommandLineOption quickOption("quick",
        "Only validate metadata and browser previews; skip full AHD and alignment.");
    const QCommandLineOption baselineOption("baseline",
        "Compare against a previous report.json and fail on regressions.", "file");
    const QCommandLineOption strictOption("strict",
        "Fail when any sample cannot complete the checks enabled by this mode.");
    parser.addOptions({samplesOption, outputOption, quickOption, baselineOption, strictOption});
    parser.process(application);

    const QString sampleRoot = QDir(parser.value(samplesOption)).absolutePath();
    if (parser.value(samplesOption).isEmpty() || !QDir(sampleRoot).exists()) {
        std::cerr << "A valid --samples directory is required.\n";
        return 2;
    }
    const QString outputRoot = parser.value(outputOption).isEmpty()
        ? QDir::current().filePath("sample-regression-output")
        : QDir(parser.value(outputOption)).absolutePath();
    QDir().mkpath(outputRoot);
    const bool quick = parser.isSet(quickOption);

    QJsonObject report;
    report["schemaVersion"] = 1;
    report["toolVersion"] = QCoreApplication::applicationVersion();
    report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["sampleRoot"] = sampleRoot;
    report["mode"] = quick ? "quick" : "full";
    report["physicalMemoryBytes"] = QString::number(
        ProcessingMemoryEstimator::totalPhysicalMemoryBytes());
    report["recommendedBudgetBytes"] = QString::number(
        ProcessingMemoryEstimator::recommendedBudgetBytes());

    Summary total;
    QJsonArray categories;
    for (const QString& category : kCategories) {
        const QString categoryPath = QDir(sampleRoot).filePath(category);
        const QStringList files = rawFiles(categoryPath);
        Summary categorySummary;
        QHash<QString, ReferenceFrame> references;
        QJsonObject memoryEstimate;
        QJsonArray fileResults;
        for (const QString& file : files) {
            std::cout << "[" << category.toStdString() << "] "
                      << QFileInfo(file).fileName().toStdString() << std::endl;
            ReferenceFrame& reference = references[QFileInfo(file).absolutePath()];
            fileResults.append(inspectFile(sampleRoot, file, outputRoot,
                                           quick, reference, categorySummary));
        }

        if (!files.isEmpty()) {
            RawImageLoader metadataLoader;
            RawImageLoader::Metadata firstMetadata;
            if (metadataLoader.loadMetadata(files.front().toStdString(), firstMetadata)) {
                memoryEstimate["normalBytes"] = QString::number(
                    ProcessingMemoryEstimator::estimatePeakBytes(
                        firstMetadata.width, firstMetadata.height, files.size(), false));
                memoryEstimate["skyGroundBytes"] = QString::number(
                    ProcessingMemoryEstimator::estimatePeakBytes(
                        firstMetadata.width, firstMetadata.height, files.size(), true));
            }
        }

        total.files += categorySummary.files;
        total.metadataPassed += categorySummary.metadataPassed;
        total.previewPassed += categorySummary.previewPassed;
        total.fullDecodePassed += categorySummary.fullDecodePassed;
        total.starDetectionPassed += categorySummary.starDetectionPassed;
        total.alignmentAttempted += categorySummary.alignmentAttempted;
        total.alignmentPassed += categorySummary.alignmentPassed;

        QJsonObject categoryJson;
        categoryJson["name"] = category;
        QJsonArray referenceResults;
        QStringList sequenceDirectories = references.keys();
        sequenceDirectories.sort(Qt::CaseInsensitive);
        for (const QString& sequenceDirectory : sequenceDirectories) {
            const ReferenceFrame& reference = references[sequenceDirectory];
            QJsonObject referenceJson;
            referenceJson["sequence"] = relativePath(sampleRoot, sequenceDirectory);
            referenceJson["reference"] = reference.valid
                ? relativePath(sampleRoot, reference.path) : QString();
            referenceResults.append(referenceJson);
        }
        categoryJson["references"] = referenceResults;
        categoryJson["memoryEstimate"] = memoryEstimate;
        categoryJson["summary"] = summaryJson(categorySummary);
        categoryJson["files"] = fileResults;
        categories.append(categoryJson);
    }
    report["categories"] = categories;
    report["summary"] = summaryJson(total);

    bool baselinePassed = true;
    if (parser.isSet(baselineOption)) {
        QFile baselineFile(parser.value(baselineOption));
        if (!baselineFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Cannot read baseline: "
                      << parser.value(baselineOption).toStdString() << '\n';
            return 5;
        }
        QJsonParseError parseError;
        const QJsonDocument baselineDocument = QJsonDocument::fromJson(
            baselineFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !baselineDocument.isObject()) {
            std::cerr << "Invalid baseline JSON: "
                      << parseError.errorString().toStdString() << '\n';
            return 5;
        }
        const QJsonObject comparison = compareBaseline(baselineDocument.object(), report);
        baselinePassed = comparison["passed"].toBool();
        report["baselineComparison"] = comparison;
    }

    const QString reportPath = QDir(outputRoot).filePath("report.json");
    QFile reportFile(reportPath);
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Cannot write report: " << reportPath.toStdString() << '\n';
        return 3;
    }
    reportFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    reportFile.close();

    std::cout << "Report: " << reportPath.toStdString() << '\n';
    std::cout << "Files: " << total.files
              << ", full decode: " << total.fullDecodePassed
              << ", alignment: " << total.alignmentPassed
              << "/" << total.alignmentAttempted << '\n';
    if (total.files == 0) return 4;
    const bool strictPassed = total.metadataPassed == total.files &&
        total.previewPassed == total.files &&
        (quick || (total.fullDecodePassed == total.files &&
                   total.starDetectionPassed == total.files));
    if ((parser.isSet(strictOption) && !strictPassed) || !baselinePassed) return 6;
    return 0;
}
