#include "core/MasterFrameIO.h"
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
using Outcome = FrameReceipt::Outcome;
constexpr int kDngWidth = 512;
constexpr int kDngHeight = 384;

struct SyntheticDngOptions {
    size_t starCount = 16;
    double starFalloff = 6.0;
    bool exposureMetadata = false;
};

// Paint in display coordinates, then store the inverse rotation in the RAW.
bool writeSyntheticDng(const QString& path, uint16_t orientation, int second,
                       int starOffsetX, int groundLevel, SyntheticDngOptions options = {}) {
#ifdef Q_OS_WIN
    TIFF* handle = TIFFOpenW(path.toStdWString().c_str(), "w");
#else
    TIFF* handle = TIFFOpen(QFile::encodeName(path).constData(), "w");
#endif
    const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiff(handle, TIFFClose);
    if (!tiff) return false;

    uint64_t exifOffset = 0;
    if (options.exposureMetadata) {
        const uint16_t iso = 800;
        TIFFCreateEXIFDirectory(handle);
        if (!TIFFSetField(handle, EXIFTAG_ISOSPEEDRATINGS, 1, &iso) ||
            !TIFFSetField(handle, EXIFTAG_EXPOSURETIME, 30.0) ||
            !TIFFWriteCustomDirectory(handle, &exifOffset)) {
            return false;
        }
        TIFFCreateDirectory(handle);
    }

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
        !TIFFSetField(handle, TIFFTAG_ASSHOTNEUTRAL, 3, neutral) ||
        (exifOffset != 0 && !TIFFSetField(handle, TIFFTAG_EXIFIFD, exifOffset))) {
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
    const int radius = options.starFalloff > 6.0 ? 12 : 5;
    for (size_t i = 0; i < std::min(stars.size(), options.starCount); ++i) {
        const int x = stars[i].x() + starOffsetX;
        const int y = stars[i].y();
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                scene[static_cast<size_t>(y + dy) * width + x + dx] +=
                    static_cast<uint16_t>((5000 + 160 * i) *
                        std::exp(-(dx * dx + dy * dy) / options.starFalloff));
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
                                  const std::array<int, 3>& offsets,
                                  SyntheticDngOptions options = {}) {
    if (!QDir().mkpath(directory.absolutePath())) return {};
    const QStringList names = {"C_early.dng", "A_middle.dng", "B_late.dng"};
    const std::array<int, 3> groundLevels{320, 780, 450};
    QStringList files;
    for (int i = 0; i < names.size(); ++i) {
        const QString path = directory.filePath(names[i]);
        if (!writeSyntheticDng(path, orientation, i * 10, offsets[i], groundLevels[i], options)) {
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

void checkUsedReceipt(const ProcessingWorker& worker, const QStringList& order,
                      int referenceIndex, bool sequence) {
    const FrameReceipt receipt = worker.frameReceipt(order, {});
    check(receipt.rows.size() == order.size() && receipt.sequence == sequence &&
              receipt.deliverables == (sequence ? order.size() : 1) &&
              receipt.usedCount() == order.size() && receipt.submittedCount() == order.size() &&
              receipt.skippedCount() == 0 && receipt.manualCount() == 0,
          "A wholly successful job should have exactly its adopted sources and written deliverables");
    for (qsizetype i = 0; i < receipt.rows.size() && i < order.size(); ++i) {
        const auto& row = receipt.rows.at(i);
        check(row.sourcePath == order.at(i) && row.outcome == Outcome::Used &&
                  row.reference == (i == referenceIndex) && row.outputNumber == (sequence ? i + 1 : 0),
              "Receipt must bind the exact source, reference and number in worker order");
    }
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
        checkUsedReceipt(worker, ordered, 0, false);
        for (const auto& row : worker.frameReceipt(ordered, {}).rows) {
            check(row.detectedStars == -1 && row.qualityScore == -1.0 && row.reasonCodes.isEmpty(),
                  "Star-trail accumulation should not invent optional alignment or quality diagnostics");
        }
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
    check(worker.frameReceipt(files, {}).rows.isEmpty(),
          "Accepted pairwise alignments are not a successful receipt if the shared crop fails");

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
    checkUsedReceipt(pair, {files[1], files[2]}, 0, false);
}

void testSingleFrameReceipt() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Single-frame receipt needs a writable directory");
    if (!temporary.isValid()) return;

    const QString original = temporary.filePath("same.dng");
    const QString otherWithSameName = temporary.filePath("other/same.dng");
    const bool fixturesReady = QDir(temporary.path()).mkdir("other") &&
        writeSyntheticDng(original, ORIENTATION_TOPLEFT, 0, 0, 320) &&
        writeSyntheticDng(otherWithSameName, ORIENTATION_TOPLEFT, 10, 1, 450);
    check(fixturesReady, "Distinct originals with the same basename should be generated");
    if (!fixturesReady) return;

    QString selected = original;
    QString multipleEntry;
#ifdef Q_OS_UNIX
    selected = temporary.filePath("first-link/same.dng");
    multipleEntry = temporary.filePath("second-link/same.dng");
    const QDir root(temporary.path());
    const bool linksReady = root.mkdir("first-link") && root.mkdir("second-link") &&
        QFile::link(original, selected) && QFile::link(original, multipleEntry);
    check(linksReady, "Two distinct project links should point at the same physical RAW");
    if (!linksReady) return;
#endif
    const QString unsubmitted = temporary.filePath("never-read.dng");
    const QString excluded = temporary.filePath("manual.dng");
    QStringList liveProject{otherWithSameName, selected, excluded, unsubmitted};
    if (!multipleEntry.isEmpty()) liveProject.append(multipleEntry);
    const QStringList projectSnapshot = liveProject;
    QStringList liveExclusions{excluded};
    const QStringList exclusionSnapshot = liveExclusions;

    ProcessingWorker::Params params;
    params.singleFrameMode = true;
    params.autoLensChromaticAberration = false;
    params.outputPath = temporary.filePath("output");
    ProcessingWorker worker({selected, unsubmitted}, otherWithSameName, params);
    check(worker.frameReceipt(projectSnapshot, exclusionSnapshot).rows.isEmpty(),
          "An unstarted worker must not publish a receipt");
    bool prematureReceiptChecked = false;
    QObject::connect(&worker, &ProcessingWorker::progress, &worker, [&](int value) {
        if (value != 100) return;
        prematureReceiptChecked = true;
        check(worker.frameReceipt(projectSnapshot, exclusionSnapshot).rows.isEmpty(),
              "Even committed image output must wait for the worker to finish before publishing the receipt");
    }, Qt::DirectConnection);
    worker.start();
    liveProject = {unsubmitted};
    liveExclusions = {selected};
    if (!waitForProcessing(worker)) return;

    const auto receipt = worker.frameReceipt(projectSnapshot, exclusionSnapshot);
    check(prematureReceiptChecked && worker.errorString().isEmpty() &&
              QFileInfo::exists(worker.outputFile()) && receipt.deliverables == 1 &&
              !receipt.sequence && receipt.submittedCount() == 1 && receipt.usedCount() == 1 &&
              receipt.skippedCount() == 0 && receipt.manualCount() == 1,
          "Single refinement only submits the actual source and keeps launch-time exclusions outside skips");
    QStringList expected{selected, otherWithSameName, excluded, unsubmitted};
    if (!multipleEntry.isEmpty()) expected.append(multipleEntry);
    check(receipt.rows.size() == expected.size(), "Single receipt should list every project entry exactly once");
    for (qsizetype i = 0; i < receipt.rows.size() && i < expected.size(); ++i) {
        const auto& row = receipt.rows.at(i);
        check(row.sourcePath == expected.at(i) && row.reference == (i == 0) && row.outputNumber == 0 &&
                  row.outcome == (i == 0 ? Outcome::Used : i == 2 ? Outcome::ManualExcluded : Outcome::NotSubmitted) &&
                  row.detectedStars == -1 && row.qualityScore == -1.0 && row.reasonCodes.isEmpty(),
              "Single receipt must preserve distinct paths, project order for leftovers, and unknown diagnostics");
    }

    params.outputPath = original; // A decoded file exists here, not an output directory.
    ProcessingWorker failed({selected}, {}, params);
    failed.start();
    if (!waitForProcessing(failed)) return;
    check(!failed.errorString().isEmpty() && failed.outputFrameCount() == 1 &&
              failed.frameReceipt(projectSnapshot, exclusionSnapshot).rows.isEmpty(),
          "Successful decode and a legacy frame count are not enough when single output could not be written");
}

void testStackReceiptSkips() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Stack receipt needs a writable directory");
    if (!temporary.isValid()) return;
    // The quality pass detects stars on half-size RAW previews.
    const auto good = makeSyntheticSequence(QDir(temporary.path()), ORIENTATION_TOPLEFT,
                                            {-2, 0, 2}, {16, 12.0});
    const QString outlier = temporary.filePath("wide-stars.dng");
    const QString blank = temporary.filePath("blank.dng");
    const QString few = temporary.filePath("one-star.dng");
    const QString unreadable = temporary.filePath("will-not-load.dng");
    const bool fixturesReady = good.size() == 3 && QFile::copy(good[0], unreadable) &&
        writeSyntheticDng(outlier, ORIENTATION_TOPLEFT, 25, 0, 320, {9, 50.0}) &&
        writeSyntheticDng(blank, ORIENTATION_TOPLEFT, 30, 0, 1400, {0}) &&
        writeSyntheticDng(few, ORIENTATION_TOPLEFT, 35, 0, 1400, {1});
    check(fixturesReady, "Stack quality and alignment fixtures should be generated");
    if (!fixturesReady) return;

    ProcessingWorker::Params params;
    params.autoLensChromaticAberration = false;
    params.photometricNormalizationEnabled = false;
    params.outputPath = temporary.filePath("output");
    const QStringList files{few, good[0], unreadable, good[1], outlier, blank, good[2]};
    ProcessingWorker worker(files, good[1], params);
    bool movedRaw = false;
    QObject::connect(&worker, &ProcessingWorker::phaseChanged, &worker, [&](Phase phase) {
        // Metadata and quality previews have loaded, but source RAW decoding has not started.
        if (phase == Phase::Alignment) movedRaw = QFile::rename(unreadable, unreadable + ".moved");
    }, Qt::DirectConnection);
    worker.start();
    if (!waitForProcessing(worker)) return;
    const auto receipt = worker.frameReceipt(files, {});
    if (worker.qualityRejectedFiles() != QStringList{outlier}) {
        for (const auto& metrics : worker.frameQualityMetrics()) {
            std::cerr << "Quality fixture: valid=" << metrics.valid << ", stars=" << metrics.usableStars
                      << ", fwhm=" << metrics.medianFwhm << ", score=" << metrics.score << '\n';
        }
    }
    check(movedRaw && worker.errorString().isEmpty() && QFileInfo::exists(worker.outputFile()) &&
              receipt.deliverables == 1 && receipt.rows.size() == files.size() &&
              receipt.submittedCount() == 7 && receipt.usedCount() == 3 && receipt.skippedCount() == 4 &&
              worker.qualityRejectedFiles() == QStringList{outlier} && worker.skippedFrames().size() == 3,
          "Stack should count quality rejection once alongside the real RAW, detection and alignment skips");
    if (receipt.rows.size() != files.size()) return;

    const std::array<Outcome, 7> outcomes{Outcome::AlignmentFailed, Outcome::Used,
        Outcome::RawLoadFailed, Outcome::Used, Outcome::QualityRejected, Outcome::NoStars, Outcome::Used};
    for (qsizetype i = 0; i < files.size(); ++i) {
        const auto& row = receipt.rows.at(i);
        check(row.sourcePath == files.at(i) && row.outcome == outcomes[static_cast<size_t>(i)] &&
                  row.reference == (i == 3) && row.outputNumber == 0,
              "Stack statuses and the selected reference must stay on their own input rows");
    }
    check(receipt.rows.at(0).reasonCodes == QStringList{"no-fit-model"} && receipt.rows.at(0).detectedStars == 1 &&
              receipt.rows.at(2).reasonCodes == QStringList{"raw-decode-failed"} && receipt.rows.at(2).detectedStars == -1 &&
              receipt.rows.at(3).detectedStars > 0 &&
              receipt.rows.at(4).reasonCodes == QStringList{"quality-outlier"} && receipt.rows.at(4).detectedStars == -1 &&
              receipt.rows.at(5).reasonCodes == QStringList{"no-alignment-stars"} && receipt.rows.at(5).detectedStars == 0,
          "Only formal detection counts belong in the receipt; skip reasons must reflect the failed stage");
    const auto& quality = worker.frameQualityMetrics();
    check(quality.size() == 7 && quality[4].valid && quality[4].detectedStars > 0 &&
              receipt.rows.at(4).qualityScore == quality[4].score && receipt.rows.at(4).qualityScore < 0.0 &&
              receipt.rows.at(5).qualityScore == -1.0,
          "Quality rejection should preserve the measured score without borrowing its preview star count");
}

void testSkyGroundReceipt() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Sky-ground receipt needs a writable directory");
    if (!temporary.isValid()) return;
    const auto good = makeSyntheticSequence(QDir(temporary.path()), ORIENTATION_TOPLEFT, {-2, 0, 2});
    check(good.size() == 3, "Sky-ground Light fixtures should be generated");
    if (good.size() != 3) return;

    ProcessingWorker::Params params;
    params.autoLensChromaticAberration = false;
    params.autoRejectLowQualityFrames = false;
    params.photometricNormalizationEnabled = false;
    params.skyGroundSepEnabled = true;
    params.editedSkyGroundMaskWidth = kDngWidth;
    params.editedSkyGroundMaskHeight = kDngHeight;
    params.editedSkyGroundMaskSourcePath = good[1];
    params.editedSkyGroundMask.assign(kDngWidth * kDngHeight, 0);
    std::fill_n(params.editedSkyGroundMask.begin(), kDngWidth * kDngHeight * 2 / 3, uint8_t{255});
    params.outputPath = temporary.filePath("output");
    const QStringList files{good[2], good[0], good[1]};
    ProcessingWorker worker(files, good[1], params);
    worker.start();
    if (!waitForProcessing(worker)) return;
    check(worker.errorString().isEmpty() && worker.skyGroundMaskSource() == "guided-preview" &&
              QFileInfo::exists(worker.outputFile()),
          "Sky-ground stack should actually finish using the reference-bound edited mask");
    checkUsedReceipt(worker, files, 2, false);
}

void testDeepSkyReceipt() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Deep-sky receipt needs a writable directory");
    if (!temporary.isValid()) return;
    const QString first = temporary.filePath("light-one.dng");
    const QString second = temporary.filePath("light-two.dng");
    const SyntheticDngOptions lightOptions{16, 6.0, true};
    RawImageLoader loader;
    RawImageLoader::CfaImageData light;
    const bool lightsReady = writeSyntheticDng(first, ORIENTATION_TOPLEFT, 0, 0, 320, lightOptions) &&
        writeSyntheticDng(second, ORIENTATION_TOPLEFT, 10, 1, 320, lightOptions) &&
        loader.loadRawCfa(first, light) && light.iso == 800 && light.exposureTime == 30.0;
    check(lightsReady, "Synthetic deep-sky Lights should provide real Bayer geometry and exposure metadata");
    if (!lightsReady) return;

    ProcessingWorker::Params params;
    params.deepSkyMode = true;
    params.autoRejectLowQualityFrames = false;
    params.photometricNormalizationEnabled = false;
    params.autoLensChromaticAberration = false;
    params.outputPath = temporary.filePath("output");
    params.masterDarkPath = temporary.filePath("master-dark.spmaster");
    params.masterFlatPath = temporary.filePath("master-flat.spmaster");
    RawCalibrationEngine::MasterFrame dark;
    RawCalibrationEngine::MasterFrame flat;
    using Role = RawCalibrationEngine::MasterRole;
    QString error;
    const bool mastersReady = RawCalibrationEngine::createMasterFrame(
            Role::Dark, light, std::vector<float>(light.data.size(), 0.0f), false, dark) &&
        RawCalibrationEngine::createMasterFrame(
            Role::Flat, light, std::vector<float>(light.data.size(), 1.0f), true, flat) &&
        MasterFrameIO::save(params.masterDarkPath, dark, error) &&
        MasterFrameIO::save(params.masterFlatPath, flat, error);
    check(mastersReady, "Neutral masters must pass the genuine calibration-file writer");
    if (!mastersReady) return;

    const QStringList lights{second, first};
    ProcessingWorker worker(lights, first, params);
    worker.start();
    if (!waitForProcessing(worker)) return;
    check(worker.errorString().isEmpty() && worker.calibratedLightFrameCount() == 2 &&
              QFileInfo::exists(worker.outputFile()),
          "Deep sky must actually calibrate and export with the synthetic masters");
    checkUsedReceipt(worker, lights, 1, false);
}

void testConcurrentTimelapseOutputs() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Timelapse tests need a writable directory");
    if (!temporary.isValid()) return;
    const auto ordered = makeSyntheticSequence(QDir(temporary.path()), ORIENTATION_TOPLEFT,
                                                {-2, 0, 2});
    check(ordered.size() == 3, "Timelapse DNGs should be generated");
    if (ordered.size() != 3) return;
    const bool noStars = writeSyntheticDng(ordered[2], ORIENTATION_TOPLEFT, 20, 0, 1400, {0});
    check(noStars, "A target without any aligned sky neighbors should still have its own output");
    if (!noStars) return;
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
        checkUsedReceipt(*worker, ordered, 1, true);
        const auto receipt = worker->frameReceipt(input, {});
        check(receipt.rows.size() == 3 && receipt.rows.at(2).detectedStars == 0 &&
                  receipt.rows.at(2).outcome == Outcome::Used && receipt.rows.at(2).reasonCodes.isEmpty(),
              "A skipped neighbor transform is not a skipped source if its own output was written");
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
    check(cancelled.frameReceipt(input, {}).rows.isEmpty(),
          "An early cancellation must not publish a receipt");

    ProcessingWorker partial(input, {}, params);
    int outputPhases = 0;
    QObject::connect(&partial, &ProcessingWorker::phaseChanged, &partial,
                     [&](Phase phase) {
                         if (phase == Phase::Output && ++outputPhases == 2) partial.requestCancel();
                     }, Qt::DirectConnection);
    partial.start();
    if (!waitForProcessing(partial)) return;
    check(partial.wasCancelled() && partial.outputFrameCount() == 1 &&
              !partial.outputFile().isEmpty() && QDir(partial.outputFile()).entryList(QDir::Files).size() == 1 &&
              partial.frameReceipt(input, {}).rows.isEmpty(),
          "A cancelled timelapse keeps its first committed frame without publishing a formal receipt");
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
    testSingleFrameReceipt();
    testStackReceiptSkips();
    testSkyGroundReceipt();
    testDeepSkyReceipt();
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
