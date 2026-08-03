#include "ProcessingWorker.h"

#include "core/AutoOptimizeEngine.h"
#include "core/FrameQualityEvaluator.h"
#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/NoiseReductionEngine.h"
#include "core/PhotometricNormalizer.h"
#include "core/ProcessingMemoryEstimator.h"
#include "core/RawImageLoader.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarReducer.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

#ifdef Q_OS_MACOS
#include <fcntl.h>
#endif

namespace {

bool loadMaskPreview(const QString& path, QImage& image) {
    RawImageLoader loader;
    RawImageLoader::PreviewData preview;
    constexpr int kMaskPreviewLongSide = 2400;
    if (!loader.loadPreview(path.toStdString(), kMaskPreviewLongSide, preview)) {
        return false;
    }
    if (preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
        image = QImage::fromData(preview.bytes.data(),
                                 static_cast<int>(preview.bytes.size()), "JPEG");
    } else if (preview.width > 0 && preview.height > 0) {
        const QImage borrowed(preview.bytes.data(), preview.width, preview.height,
                              preview.width * 3, QImage::Format_RGB888);
        image = borrowed.copy();
    }
    if (image.isNull()) return false;
    if (std::max(image.width(), image.height()) > kMaskPreviewLongSide) {
        image = image.scaled(kMaskPreviewLongSide, kMaskPreviewLongSide,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return !image.isNull();
}

QString formatMemoryBytes(uint64_t bytes) {
    constexpr double kMiB = 1024.0 * 1024.0;
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    if (bytes < static_cast<uint64_t>(kGiB / 10.0)) {
        return QString("%1 MB").arg(bytes / kMiB, 0, 'f', 0);
    }
    return QString("%1 GB").arg(bytes / kGiB, 0, 'f', 1);
}

double medianValue(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const double lower =
        *std::max_element(values.begin(), values.begin() + middle);
    return (lower + values[middle]) * 0.5;
}

bool previewToLuminance(const RawImageLoader::PreviewData& preview,
                        std::vector<uint16_t>& luminance,
                        int& width, int& height) {
    QImage image;
    if (preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
        if (preview.bytes.size() >
            static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        image = QImage::fromData(
            preview.bytes.data(), static_cast<int>(preview.bytes.size()), "JPEG");
    } else {
        if (preview.width <= 0 || preview.height <= 0 ||
            preview.width > std::numeric_limits<int>::max() / 3 ||
            static_cast<size_t>(preview.width) >
                std::numeric_limits<size_t>::max() /
                    static_cast<size_t>(preview.height) / 3) {
            return false;
        }
        const size_t expected =
            static_cast<size_t>(preview.width) * preview.height * 3;
        if (preview.bytes.size() != expected) return false;
        image = QImage(preview.bytes.data(), preview.width, preview.height,
                       preview.width * 3, QImage::Format_RGB888).copy();
    }
    if (image.isNull()) return false;
    image = image.convertToFormat(QImage::Format_RGB888);
    constexpr int kQualityLongSide = 1200;
    if (std::max(image.width(), image.height()) > kQualityLongSide) {
        image = image.scaled(kQualityLongSide, kQualityLongSide,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    width = image.width();
    height = image.height();
    if (width <= 0 || height <= 0) return false;

    luminance.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const uint32_t weighted =
                static_cast<uint32_t>(row[x * 3]) * 13933 +
                static_cast<uint32_t>(row[x * 3 + 1]) * 46871 +
                static_cast<uint32_t>(row[x * 3 + 2]) * 4732;
            const uint16_t value8 = static_cast<uint16_t>(weighted / 65536);
            luminance[static_cast<size_t>(y) * width + x] = value8 * 257;
        }
    }
    return true;
}

bool cropRgb(const std::vector<uint16_t>& source,
             int sourceWidth, int sourceHeight,
             const AlignmentBounds& bounds,
             std::vector<uint16_t>& destination) {
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        bounds.x < 0 || bounds.y < 0 || bounds.width <= 0 ||
        bounds.height <= 0 || bounds.x + bounds.width > sourceWidth ||
        bounds.y + bounds.height > sourceHeight ||
        source.size() !=
            static_cast<size_t>(sourceWidth) * sourceHeight * 3) {
        return false;
    }
    std::vector<uint16_t> cropped(
        static_cast<size_t>(bounds.width) * bounds.height * 3);
    const size_t rowValues = static_cast<size_t>(bounds.width) * 3;
    for (int row = 0; row < bounds.height; ++row) {
        const size_t sourceOffset =
            (static_cast<size_t>(bounds.y + row) * sourceWidth + bounds.x) * 3;
        const size_t destinationOffset =
            static_cast<size_t>(row) * rowValues;
        std::memcpy(cropped.data() + destinationOffset,
                    source.data() + sourceOffset,
                    rowValues * sizeof(uint16_t));
    }
    destination = std::move(cropped);
    return true;
}

class DiskFrameStore {
public:
    explicit DiskFrameStore(const QString& prefix)
        : m_directory(QDir::tempPath() + "/" + prefix + "-XXXXXX") {}

    bool isValid() const { return m_directory.isValid(); }
    int frameCount() const { return m_files.size(); }

    bool append(const std::vector<uint16_t>& frame) {
        if (!isValid() || frame.empty() ||
            frame.size() > static_cast<size_t>(std::numeric_limits<qint64>::max()) /
                               sizeof(uint16_t)) {
            return false;
        }
        const QString path = m_directory.filePath(
            QString("frame-%1.rgb16").arg(m_files.size(), 4, 10, QLatin1Char('0')));
        QFile file(path);
        const qint64 bytes = static_cast<qint64>(frame.size() * sizeof(uint16_t));
        if (!file.open(QIODevice::WriteOnly)) return false;
        disableFileCache(file);
        if (file.write(reinterpret_cast<const char*>(frame.data()), bytes) != bytes) {
            return false;
        }
        m_files.push_back(path);
        return true;
    }

    bool readRows(int frameIndex, int width, int startRow, int rowCount,
                  std::vector<uint16_t>& output) const {
        if (frameIndex < 0 || frameIndex >= m_files.size() || width <= 0 ||
            startRow < 0 || rowCount <= 0) {
            return false;
        }
        const size_t rowValues = static_cast<size_t>(width) * 3;
        const size_t valueCount = rowValues * static_cast<size_t>(rowCount);
        if (valueCount > static_cast<size_t>(std::numeric_limits<qint64>::max()) /
                             sizeof(uint16_t)) {
            return false;
        }
        const qint64 offset = static_cast<qint64>(
            rowValues * static_cast<size_t>(startRow) * sizeof(uint16_t));
        const qint64 bytes = static_cast<qint64>(valueCount * sizeof(uint16_t));
        QFile file(m_files[frameIndex]);
        output.resize(valueCount);
        if (!file.open(QIODevice::ReadOnly)) return false;
        disableFileCache(file);
        return file.seek(offset) && file.read(reinterpret_cast<char*>(output.data()), bytes) == bytes;
    }

private:
    static void disableFileCache(QFile& file) {
#ifdef Q_OS_MACOS
        // Temporary frames are written once and consumed sequentially. Caching
        // multi-gigabyte files competes with image buffers and can trigger macOS
        // memory-pressure termination without improving this access pattern.
        if (file.handle() >= 0) fcntl(file.handle(), F_NOCACHE, 1);
#else
        Q_UNUSED(file);
#endif
    }

    QTemporaryDir m_directory;
    QStringList m_files;
};

bool splitFrames(const std::vector<std::vector<uint16_t>>& rgbFrames,
                 int width, int height,
                 std::vector<std::vector<uint16_t>>& red,
                 std::vector<std::vector<uint16_t>>& green,
                 std::vector<std::vector<uint16_t>>& blue) {
    red.clear();
    green.clear();
    blue.clear();
    red.reserve(rgbFrames.size());
    green.reserve(rgbFrames.size());
    blue.reserve(rgbFrames.size());

    for (const auto& rgb : rgbFrames) {
        ImageBufferUtils::RgbChannels channels;
        if (!ImageBufferUtils::splitRgb(rgb, width, height, channels)) return false;
        red.push_back(std::move(channels.red));
        green.push_back(std::move(channels.green));
        blue.push_back(std::move(channels.blue));
    }
    return true;
}

bool mergeChannels(std::vector<uint16_t> red, std::vector<uint16_t> green,
                   std::vector<uint16_t> blue, int width, int height,
                   std::vector<uint16_t>& output) {
    ImageBufferUtils::RgbChannels channels;
    channels.red = std::move(red);
    channels.green = std::move(green);
    channels.blue = std::move(blue);
    return ImageBufferUtils::mergeRgb(channels, width, height, output);
}

bool stackRgbWithMask(StackingEngine& stacker,
                      const std::vector<std::vector<uint16_t>>& aligned,
                      const std::vector<std::vector<uint16_t>>& originals,
                      int width, int height, StackingEngine::Method method,
                      double kappa, const std::vector<uint8_t>& mask,
                      std::vector<uint16_t>& output) {
    if (aligned.empty() || aligned.size() != originals.size()) return false;

    std::vector<std::vector<uint16_t>> alignedRed;
    std::vector<std::vector<uint16_t>> alignedGreen;
    std::vector<std::vector<uint16_t>> alignedBlue;
    std::vector<std::vector<uint16_t>> originalRed;
    std::vector<std::vector<uint16_t>> originalGreen;
    std::vector<std::vector<uint16_t>> originalBlue;
    if (!splitFrames(aligned, width, height, alignedRed, alignedGreen, alignedBlue) ||
        !splitFrames(originals, width, height, originalRed, originalGreen, originalBlue)) {
        return false;
    }

    std::vector<uint16_t> redResult;
    std::vector<uint16_t> greenResult;
    std::vector<uint16_t> blueResult;
    if (!stacker.stackWithMask(alignedRed, originalRed, width, height, method,
                               kappa, mask, redResult) ||
        !stacker.stackWithMask(alignedGreen, originalGreen, width, height, method,
                               kappa, mask, greenResult) ||
        !stacker.stackWithMask(alignedBlue, originalBlue, width, height, method,
                               kappa, mask, blueResult)) {
        return false;
    }
    return mergeChannels(std::move(redResult), std::move(greenResult),
                         std::move(blueResult), width, height, output);
}

bool stackCachedRgb(StackingEngine& stacker, const DiskFrameStore& aligned,
                    const DiskFrameStore* originals, int width, int height,
                    StackingEngine::Method method, double kappa,
                    const std::vector<uint8_t>* mask,
                    std::vector<uint16_t>& output,
                    const std::function<bool()>& cancelled,
                    const std::function<void(int)>& rowsCompleted) {
    if (aligned.frameCount() < 2 || width <= 0 || height <= 0 ||
        (originals && originals->frameCount() != aligned.frameCount()) ||
        (mask && mask->size() != static_cast<size_t>(width) * height)) {
        return false;
    }
    constexpr int kRowsPerChunk = 32;
    output.resize(static_cast<size_t>(width) * height * 3);
    std::vector<std::vector<uint16_t>> alignedChunk(
        static_cast<size_t>(aligned.frameCount()));
    std::vector<std::vector<uint16_t>> originalChunk(
        originals ? static_cast<size_t>(originals->frameCount()) : 0);
    std::vector<uint8_t> maskChunk;
    std::vector<uint16_t> stackedChunk;
    for (int startRow = 0; startRow < height; startRow += kRowsPerChunk) {
        if (cancelled()) return false;
        const int rowCount = std::min(kRowsPerChunk, height - startRow);
        for (int frame = 0; frame < aligned.frameCount(); ++frame) {
            if (!aligned.readRows(frame, width, startRow, rowCount,
                                  alignedChunk[static_cast<size_t>(frame)])) {
                return false;
            }
        }

        if (originals) {
            for (int frame = 0; frame < originals->frameCount(); ++frame) {
                if (!originals->readRows(frame, width, startRow, rowCount,
                                         originalChunk[static_cast<size_t>(frame)])) {
                    return false;
                }
            }
            maskChunk.assign(
                mask->begin() + static_cast<size_t>(startRow) * width,
                mask->begin() + static_cast<size_t>(startRow + rowCount) * width);
            if (!stackRgbWithMask(stacker, alignedChunk, originalChunk, width,
                                  rowCount, method, kappa, maskChunk, stackedChunk)) {
                return false;
            }
        } else if (!stacker.stackRgb(alignedChunk, width, rowCount, method,
                                     kappa, stackedChunk, true)) {
            return false;
        }

        const size_t destination = static_cast<size_t>(startRow) * width * 3;
        std::memcpy(output.data() + destination, stackedChunk.data(),
                    stackedChunk.size() * sizeof(uint16_t));
        rowsCompleted(startRow + rowCount);
    }
    return true;
}

StackingEngine::Method stackMethodFromName(const QString& name) {
    if (name == "median") return StackingEngine::Median;
    if (name == "kappa-sigma") return StackingEngine::KappaSigma;
    if (name == "winsorized") return StackingEngine::Winsorized;
    return StackingEngine::Average;
}

} // namespace

ProcessingWorker::ProcessingWorker(const QStringList& files,
                                   const QString& referenceFrame,
                                   const Params& params, QObject* parent)
    : QThread(parent)
    , m_files(files)
    , m_referenceFrame(referenceFrame)
    , m_params(params) {}

std::vector<uint16_t> ProcessingWorker::takeStackedData() {
    return std::move(m_stackedData);
}

double ProcessingWorker::averageAlignmentRms() const {
    const int alignedSources = m_affineFrameCount + m_homographyFrameCount;
    return alignedSources > 0 ? m_alignmentRmsSum / alignedSources : 0.0;
}

double ProcessingWorker::averagePhotometricGain() const {
    return m_photometricNormalizedFrameCount > 0
        ? m_photometricGainSum / m_photometricNormalizedFrameCount
        : 1.0;
}

void ProcessingWorker::requestCancel() {
    m_cancelRequested.store(true);
}

bool ProcessingWorker::stopIfCancelled() {
    if (!m_cancelRequested.load()) return false;
    m_wasCancelled = true;
    return true;
}

void ProcessingWorker::run() {
    m_errorString.clear();
    m_stackedData.clear();
    m_width = 0;
    m_height = 0;
    m_cropOffsetX = 0;
    m_cropOffsetY = 0;
    m_frameCount = 0;
    m_selectedReferenceIndex = -1;
    m_selectedReferenceFrame.clear();
    m_frameQualityMetrics.clear();
    m_qualityRejectedFiles.clear();
    m_wasCancelled = false;
    m_outputFile.clear();
    m_affineFrameCount = 0;
    m_homographyFrameCount = 0;
    m_alignmentRmsSum = 0.0;
    m_worstAlignmentP95 = 0.0;
    m_minimumGridCoverage = 0.0;
    m_stackingElapsedMs = 0;
    m_photometricNormalizedFrameCount = 0;
    m_photometricSkippedFrameCount = 0;
    m_photometricGainSum = 0.0;
    m_photometricMinGain = 1.0;
    m_photometricMaxGain = 1.0;
    m_photometricMaxAbsOffset = 0.0;
    m_photometricOutputAnchorGain = 1.0;
    m_photometricOutputAnchorMaxAbsOffset = 0.0;
    m_starReductionStats = {};
    emit progress(0);

    if (m_files.isEmpty()) {
        m_errorString = "没有可处理的图像";
        return;
    }

    emit stageMessage("检查图像与内存预算...");
    RawImageLoader loader;
    std::vector<RawImageLoader::Metadata> metadata;
    metadata.reserve(static_cast<size_t>(m_files.size()));
    size_t explicitReferenceIndex = std::numeric_limits<size_t>::max();
    for (int i = 0; i < m_files.size(); ++i) {
        if (stopIfCancelled()) return;
        RawImageLoader::Metadata item;
        if (!loader.loadMetadata(m_files[i].toStdString(), item)) {
            m_errorString = QString("无法读取元数据: %1")
                                .arg(QFileInfo(m_files[i]).fileName());
            return;
        }
        if (!m_referenceFrame.isEmpty() && m_files[i] == m_referenceFrame) {
            explicitReferenceIndex = static_cast<size_t>(i);
        }
        metadata.push_back(std::move(item));
    }

    const bool automaticReference =
        explicitReferenceIndex == std::numeric_limits<size_t>::max();
    size_t referenceIndex = automaticReference
        ? static_cast<size_t>(m_files.size() / 2)
        : explicitReferenceIndex;
    std::vector<bool> qualityRejected(static_cast<size_t>(m_files.size()), false);
    if (automaticReference || m_params.autoRejectLowQualityFrames) {
        emit stageMessage("快速评估帧质量...");
        m_frameQualityMetrics.resize(static_cast<size_t>(m_files.size()));
        for (int i = 0; i < m_files.size(); ++i) {
            if (stopIfCancelled()) return;
            RawImageLoader::PreviewData preview;
            std::vector<uint16_t> previewLuminance;
            int previewWidth = 0;
            int previewHeight = 0;
            if (loader.loadPreview(m_files[i].toStdString(), 1600, preview) &&
                previewToLuminance(preview, previewLuminance,
                                   previewWidth, previewHeight)) {
                FrameQualityEvaluator::evaluate(
                    previewLuminance, previewWidth, previewHeight,
                    m_frameQualityMetrics[static_cast<size_t>(i)]);
            }
            emit progress(static_cast<int>((i + 1) * 10.0 / m_files.size()));
        }

        FrameQualitySelection qualitySelection;
        if (FrameQualityEvaluator::selectSequence(
                m_frameQualityMetrics,
                static_cast<size_t>(m_files.size() / 2),
                m_params.autoRejectLowQualityFrames,
                qualitySelection)) {
            if (automaticReference) {
                referenceIndex = qualitySelection.referenceIndex;
            }
            qualityRejected = std::move(qualitySelection.rejected);
        }
    }
    // A manual reference is an explicit user decision and always remains in
    // the sequence even if its preview metrics look unusual.
    qualityRejected[referenceIndex] = false;
    for (int i = 0; i < m_files.size(); ++i) {
        if (qualityRejected[static_cast<size_t>(i)]) {
            m_qualityRejectedFiles.push_back(m_files[i]);
        }
    }
    m_selectedReferenceIndex = static_cast<int>(referenceIndex);
    m_selectedReferenceFrame = m_files[m_selectedReferenceIndex];
    if (automaticReference) {
        emit stageMessage(QString("自动参考帧：%1")
                              .arg(QFileInfo(m_selectedReferenceFrame).fileName()));
    }

    const int metadataWidth = metadata[referenceIndex].width;
    const int metadataHeight = metadata[referenceIndex].height;
    for (size_t i = 0; i < metadata.size(); ++i) {
        if (metadata[i].width != metadataWidth || metadata[i].height != metadataHeight) {
            m_errorString = QString("图像尺寸不匹配: %1")
                                .arg(QFileInfo(m_files[static_cast<int>(i)]).fileName());
            return;
        }
    }

    ProcessingMemoryEstimator::EstimateOptions estimateOptions;
    const int activeInputFrameCount =
        m_files.size() - m_qualityRejectedFiles.size();
    estimateOptions.frameCount = activeInputFrameCount;
    estimateOptions.skyGroundSeparation = m_params.skyGroundSepEnabled;
    estimateOptions.noiseReduction = m_params.noiseReductionEnabled;
    estimateOptions.dehaze = m_params.dewarpEnabled;
    estimateOptions.stretch = m_params.stretchEnabled;
    estimateOptions.starReduction = m_params.starReduceEnabled;
    const uint64_t estimatedBytes = ProcessingMemoryEstimator::estimatePeakBytes(
        metadataWidth, metadataHeight, estimateOptions);
    const ProcessingMemoryEstimator::SystemMemoryInfo memoryInfo =
        ProcessingMemoryEstimator::systemMemoryInfo();
    const uint64_t budgetBytes =
        ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
            memoryInfo.safeBudgetBytes, m_params.memoryBudgetBytes);
    if (estimatedBytes == 0 || estimatedBytes > budgetBytes) {
        m_errorString = QString("预计需要约 %1 内存，当前安全预算为 %2"
                                "（系统当前可用约 %3）。"
                                "请减少帧数，或关闭天地分离、去雾、降噪、缩星等高内存功能。")
                            .arg(formatMemoryBytes(estimatedBytes))
                            .arg(formatMemoryBytes(budgetBytes))
                            .arg(formatMemoryBytes(memoryInfo.availableBytes));
        return;
    }
    const uint64_t scratchBytes = ProcessingMemoryEstimator::estimateScratchDiskBytes(
        metadataWidth, metadataHeight, activeInputFrameCount,
        m_params.skyGroundSepEnabled);
    const QStorageInfo temporaryStorage(QDir::tempPath());
    const qint64 availableScratchBytes = temporaryStorage.bytesAvailable();
    // Keep 10% headroom for filesystem metadata and the final exported image.
    const uint64_t scratchHeadroom =
        scratchBytes / 10 + (scratchBytes % 10 == 0 ? 0 : 1);
    const bool scratchRequirementOverflow =
        scratchBytes > std::numeric_limits<uint64_t>::max() - scratchHeadroom;
    if (scratchBytes == 0 || !temporaryStorage.isValid() ||
        availableScratchBytes <= 0 || scratchRequirementOverflow ||
        static_cast<uint64_t>(availableScratchBytes) <
            scratchBytes + scratchHeadroom) {
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        m_errorString = QString("临时磁盘空间不足：处理缓存约需 %1 GB。")
                            .arg(scratchBytes / kGiB, 0, 'f', 1);
        return;
    }

    DiskFrameStore alignedCache("starprocessor-aligned");
    DiskFrameStore originalCache("starprocessor-original");
    if (!alignedCache.isValid() ||
        (m_params.skyGroundSepEnabled && !originalCache.isValid())) {
        m_errorString = "无法创建处理临时目录";
        return;
    }

    emit stageMessage("加载参考帧...");
    RawImageLoader::ImageData referenceImage;
    if (!loader.loadRaw(m_files[static_cast<int>(referenceIndex)].toStdString(),
                        referenceImage)) {
        m_errorString = QString("无法加载参考帧: %1")
                            .arg(QFileInfo(m_files[static_cast<int>(referenceIndex)]).fileName());
        return;
    }

    // LibRaw applies camera orientation during dcraw_process(). Pixel algorithms
    // must therefore use processed dimensions rather than header dimensions.
    int width = referenceImage.width;
    int height = referenceImage.height;
    m_width = width;
    m_height = height;
    if (width != metadataWidth || height != metadataHeight) {
        const uint64_t actualEstimatedBytes =
            ProcessingMemoryEstimator::estimatePeakBytes(
                width, height, estimateOptions);
        const ProcessingMemoryEstimator::SystemMemoryInfo actualMemoryInfo =
            ProcessingMemoryEstimator::systemMemoryInfo();
        const uint64_t actualBudgetBytes =
            ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
                actualMemoryInfo.safeBudgetBytes, m_params.memoryBudgetBytes);
        if (actualEstimatedBytes == 0 ||
            actualEstimatedBytes > actualBudgetBytes) {
            m_errorString = QString(
                "RAW 实际解码尺寸需要约 %1 内存，当前安全预算为 %2。")
                .arg(formatMemoryBytes(actualEstimatedBytes))
                .arg(formatMemoryBytes(actualBudgetBytes));
            return;
        }
    }

    std::vector<uint16_t> referenceLuminance;
    if (!ImageBufferUtils::extractLuminance(referenceImage.data, width, height,
                                            referenceLuminance)) {
        m_errorString = "参考帧 RGB 数据无效";
        return;
    }

    PhotometricReferenceProfile photometricReference;
    bool photometricReferenceReady = false;
    if (m_params.photometricNormalizationEnabled) {
        emit stageMessage("建立帧间光度参考...");
        photometricReferenceReady =
            PhotometricNormalizer::buildReferenceProfile(
                referenceImage.data, width, height, photometricReference);
        if (!photometricReferenceReady) {
            qWarning() << "光度参考建立失败，本次处理跳过帧间光度匹配";
        }
    }

    emit stageMessage("参考帧星点检测...");
    StarDetector detector;
    DetectionOptions evaluationDetectionOptions;
    evaluationDetectionOptions.spatiallyBalanced = true;
    evaluationDetectionOptions.maxCandidates = 300;
    evaluationDetectionOptions.maxStars = 250;
    std::vector<StarPoint> referenceDetectedStars;
    if (!detector.detect(referenceLuminance, width, height,
                         referenceDetectedStars, evaluationDetectionOptions)) {
        m_errorString = "参考帧星点检测失败";
        return;
    }
    const size_t referenceFitCount =
        std::min<size_t>(50, referenceDetectedStars.size());
    std::vector<StarPoint> referenceStars(
        referenceDetectedStars.begin(),
        referenceDetectedStars.begin() + referenceFitCount);
    // Keep model fitting and quality evaluation disjoint when the field has
    // enough stars. Sparse fields fall back to reuse so legacy small inputs
    // can still be aligned and judged by the reduced-sample gates.
    std::vector<StarPoint> referenceEvaluationStars =
        referenceDetectedStars.size() >= referenceFitCount + 12
            ? std::vector<StarPoint>(
                  referenceDetectedStars.begin() + referenceFitCount,
                  referenceDetectedStars.end())
            : referenceDetectedStars;
    emit progress(20);

    // Full-resolution aligned frames are cached on disk. Keeping both decoded
    // and aligned sequences resident makes peak RAM grow linearly twice and is
    // unsafe for ordinary 30-60 MP sequences.
    if (!alignedCache.append(referenceImage.data) ||
        (m_params.skyGroundSepEnabled && !originalCache.append(referenceImage.data))) {
        m_errorString = "无法写入参考帧临时缓存（请检查磁盘空间）";
        return;
    }
    referenceImage.data.clear();
    referenceImage.data.shrink_to_fit();
    if (!m_params.skyGroundSepEnabled) {
        referenceLuminance.clear();
        referenceLuminance.shrink_to_fit();
    }

    emit stageMessage("逐帧加载与对齐...");
    ImageAligner aligner;
    std::vector<AlignmentTransform> acceptedTransforms;
    acceptedTransforms.reserve(static_cast<size_t>(m_files.size() - 1));
    std::vector<PhotometricModel> acceptedPhotometricModels;
    acceptedPhotometricModels.reserve(
        static_cast<size_t>(m_files.size() - 1));
    for (int i = 0; i < m_files.size(); ++i) {
        if (stopIfCancelled()) return;
        if (qualityRejected[static_cast<size_t>(i)]) continue;
        if (static_cast<size_t>(i) == referenceIndex) continue;

        RawImageLoader::ImageData sourceImage;
        if (!loader.loadRaw(m_files[i].toStdString(), sourceImage)) {
            qWarning() << "RAW 加载失败，跳过:" << m_files[i];
            continue;
        }
        if (sourceImage.width != width || sourceImage.height != height) {
            m_errorString = QString("图像尺寸不匹配: %1")
                                .arg(QFileInfo(m_files[i]).fileName());
            return;
        }

        std::vector<uint16_t> sourceLuminance;
        if (!ImageBufferUtils::extractLuminance(sourceImage.data, width, height,
                                                sourceLuminance)) {
            m_errorString = QString("RGB 数据无效: %1")
                                .arg(QFileInfo(m_files[i]).fileName());
            return;
        }
        std::vector<StarPoint> sourceDetectedStars;
        if (!detector.detect(sourceLuminance, width, height,
                             sourceDetectedStars, evaluationDetectionOptions)) {
            qWarning() << "星点检测失败，跳过:" << m_files[i];
            continue;
        }
        const size_t sourceFitCount =
            std::min<size_t>(50, sourceDetectedStars.size());
        std::vector<StarPoint> sourceStars(
            sourceDetectedStars.begin(),
            sourceDetectedStars.begin() + sourceFitCount);
        std::vector<StarPoint> sourceEvaluationStars =
            sourceDetectedStars.size() >= sourceFitCount + 12
                ? std::vector<StarPoint>(
                      sourceDetectedStars.begin() + sourceFitCount,
                      sourceDetectedStars.end())
                : sourceDetectedStars;

        AlignmentTransform transform;
        AlignmentQuality alignmentQuality;
        AlignmentOptions alignmentModelOptions;
        alignmentModelOptions.imageWidth = width;
        alignmentModelOptions.imageHeight = height;
        alignmentModelOptions.evaluationReferenceStars =
            &referenceEvaluationStars;
        alignmentModelOptions.evaluationSourceStars = &sourceEvaluationStars;
        if (!aligner.align(referenceStars, sourceStars, transform,
                           &alignmentQuality, alignmentModelOptions)) {
            qWarning() << "对齐失败，跳过:" << m_files[i];
            continue;
        }
        sourceLuminance.clear();
        sourceLuminance.shrink_to_fit();

        std::vector<uint16_t> alignedFrame;
        if (!aligner.applyTransformRgb(sourceImage.data, width, height, transform,
                                       alignedFrame)) {
            qWarning() << "变换应用失败，跳过:" << m_files[i];
            continue;
        }
        if (photometricReferenceReady) {
            PhotometricModel model;
            if (PhotometricNormalizer::estimate(
                    photometricReference, alignedFrame, model)) {
                const bool alignedNormalized =
                    PhotometricNormalizer::applyInPlace(
                        alignedFrame, width, height, model);
                const bool originalNormalized =
                    !m_params.skyGroundSepEnabled ||
                    PhotometricNormalizer::applyInPlace(
                        sourceImage.data, width, height, model);
                if (!alignedNormalized || !originalNormalized) {
                    m_errorString = "帧间光度匹配应用失败";
                    return;
                }
                ++m_photometricNormalizedFrameCount;
                acceptedPhotometricModels.push_back(model);
                m_photometricGainSum += model.gain;
                if (m_photometricNormalizedFrameCount == 1) {
                    m_photometricMinGain = model.gain;
                    m_photometricMaxGain = model.gain;
                } else {
                    m_photometricMinGain =
                        std::min(m_photometricMinGain, model.gain);
                    m_photometricMaxGain =
                        std::max(m_photometricMaxGain, model.gain);
                }
                for (double offset : model.offsets) {
                    m_photometricMaxAbsOffset = std::max(
                        m_photometricMaxAbsOffset, std::abs(offset));
                }
            } else {
                ++m_photometricSkippedFrameCount;
                qWarning() << "光度模型不可靠，保留原亮度:"
                           << m_files[i];
            }
        } else if (m_params.photometricNormalizationEnabled) {
            ++m_photometricSkippedFrameCount;
        }
        if (!alignedCache.append(alignedFrame) ||
            (m_params.skyGroundSepEnabled && !originalCache.append(sourceImage.data))) {
            m_errorString = "无法写入对齐临时缓存（请检查磁盘空间）";
            return;
        }
        // Only report frames that were successfully resampled and persisted.
        // This keeps diagnostics consistent with the frames consumed by stacking.
        if (transform.model == AlignmentModel::Homography) {
            ++m_homographyFrameCount;
        } else {
            ++m_affineFrameCount;
        }
        acceptedTransforms.push_back(transform);
        m_alignmentRmsSum += alignmentQuality.rmsError;
        m_worstAlignmentP95 =
            std::max(m_worstAlignmentP95, alignmentQuality.p95Error);
        if (m_affineFrameCount + m_homographyFrameCount == 1) {
            m_minimumGridCoverage = alignmentQuality.gridCoverage;
        } else {
            m_minimumGridCoverage =
                std::min(m_minimumGridCoverage, alignmentQuality.gridCoverage);
        }
        alignedFrame.clear();
        alignedFrame.shrink_to_fit();
        sourceImage.data.clear();
        sourceImage.data.shrink_to_fit();
        emit progress(20 + static_cast<int>((i + 1) * 30.0 / m_files.size()));
    }

    if (alignedCache.frameCount() < 2) {
        m_errorString = "对齐后可用帧数不足（<2），无法堆栈";
        return;
    }
    emit progress(55);
    if (stopIfCancelled()) return;

    emit stageMessage("堆栈中...");
    StackingEngine stacker;
    const StackingEngine::Method method = stackMethodFromName(m_params.stackMethod);
    std::vector<uint16_t> resultRgb;
    std::vector<uint8_t> mask;
    if (m_params.skyGroundSepEnabled) {
        emit stageMessage("生成天地蒙版...");
        bool maskReady = false;
        if (m_params.skyGroundMode == SkyGroundMask::AutoDetect) {
            QImage preview;
            if (loadMaskPreview(m_selectedReferenceFrame, preview)) {
                maskReady = SkyGroundMask::autoDetectPreview(
                    preview, width, height, mask, m_params.featherRadius);
            }
            if (maskReady) {
                m_skyGroundMaskSource = "embedded-preview";
            } else {
                maskReady = SkyGroundMask::autoDetect(
                    referenceLuminance, width, height, mask,
                    m_params.featherRadius);
                if (maskReady) m_skyGroundMaskSource = "linear-fallback";
            }
        } else {
            maskReady = SkyGroundMask::loadUserMask(
                m_params.userMaskPath.toStdString(), width, height, mask,
                m_params.featherRadius);
            if (maskReady) m_skyGroundMaskSource = "user-mask";
        }
        if (!maskReady) {
            m_errorString = m_params.skyGroundMode == SkyGroundMask::AutoDetect
                ? "天地蒙版自动检测失败" : "无法加载用户蒙版";
            return;
        }
        const double maskSum = std::accumulate(mask.begin(), mask.end(), 0.0);
        m_skyGroundSkyFraction = maskSum / (255.0 * mask.size());
        if (!m_params.skyGroundMaskOutputPath.isEmpty()) {
            const QImage borrowedMask(mask.data(), width, height, width,
                                      QImage::Format_Grayscale8);
            if (!borrowedMask.copy().save(m_params.skyGroundMaskOutputPath)) {
                m_errorString = "无法保存天地蒙版诊断图";
                return;
            }
        }
        emit stageMessage("天地分离堆栈...");
    }
    QElapsedTimer stackingTimer;
    stackingTimer.start();
    const bool stacked = stackCachedRgb(
        stacker, alignedCache,
        m_params.skyGroundSepEnabled ? &originalCache : nullptr,
        width, height, method, m_params.kappaValue,
        m_params.skyGroundSepEnabled ? &mask : nullptr, resultRgb,
        [this]() { return stopIfCancelled(); },
        [this, height](int rows) {
            emit progress(55 + static_cast<int>(rows * 25.0 / height));
        });
    m_stackingElapsedMs = stackingTimer.elapsed();
    if (!stacked) {
        if (m_wasCancelled) return;
        m_errorString = m_params.skyGroundSepEnabled
            ? "天地分离堆栈失败" : "堆栈失败";
        return;
    }
    m_frameCount = alignedCache.frameCount();
    emit progress(80);
    if (stopIfCancelled()) return;

    if (!acceptedPhotometricModels.empty()) {
        // All normalized frames currently share the reference frame's
        // photometry. Re-anchor that common result to the median frame so one
        // unusually hazy or dark reference cannot set the final brightness.
        std::vector<double> gains = {1.0};
        std::array<std::vector<double>, 3> offsets;
        for (auto& channelOffsets : offsets) channelOffsets.push_back(0.0);
        for (const PhotometricModel& model : acceptedPhotometricModels) {
            gains.push_back(model.gain);
            for (int channel = 0; channel < 3; ++channel) {
                offsets[channel].push_back(model.offsets[channel]);
            }
        }
        const double anchorGain = medianValue(std::move(gains));
        PhotometricModel outputAnchor;
        outputAnchor.gain = 1.0 / anchorGain;
        for (int channel = 0; channel < 3; ++channel) {
            outputAnchor.offsets[channel] =
                -medianValue(std::move(offsets[channel])) / anchorGain;
            m_photometricOutputAnchorMaxAbsOffset = std::max(
                m_photometricOutputAnchorMaxAbsOffset,
                std::abs(outputAnchor.offsets[channel]));
        }
        if (PhotometricNormalizer::applyInPlace(
                resultRgb, width, height, outputAnchor)) {
            m_photometricOutputAnchorGain = outputAnchor.gain;
        } else {
            m_photometricOutputAnchorMaxAbsOffset = 0.0;
            qWarning() << "光度中位锚定不可靠，保留参考帧亮度";
        }
    }

    AlignmentBounds commonBounds;
    if (aligner.commonValidBounds(
            acceptedTransforms, width, height, commonBounds) &&
        (commonBounds.x != 0 || commonBounds.y != 0 ||
         commonBounds.width != width || commonBounds.height != height)) {
        emit stageMessage("裁切共同有效区域...");
        std::vector<uint16_t> cropped;
        if (!cropRgb(resultRgb, width, height, commonBounds, cropped)) {
            m_errorString = "有效区域裁切失败";
            return;
        }
        resultRgb = std::move(cropped);
        m_cropOffsetX = commonBounds.x;
        m_cropOffsetY = commonBounds.y;
        width = commonBounds.width;
        height = commonBounds.height;
        m_width = width;
        m_height = height;
    }

    if (m_params.noiseReductionEnabled &&
        m_params.noiseReductionStrength > 0) {
        emit stageMessage("多尺度降噪...");
        std::vector<uint16_t> denoised;
        if (!NoiseReductionEngine::denoiseRgb(
                resultRgb, width, height,
                m_params.noiseReductionStrength, denoised)) {
            m_errorString = "RGB 多尺度降噪失败";
            return;
        }
        resultRgb = std::move(denoised);
        emit progress(85);
    }

    if (m_params.dewarpEnabled || m_params.stretchEnabled) {
        emit stageMessage("自动优化...");
        if (m_params.dewarpEnabled) {
            std::vector<uint16_t> output;
            if (!AutoOptimizeEngine::dehazeRgb(
                    resultRgb, width, height, m_params.dewarpStrength, output)) {
                m_errorString = "RGB 去雾失败";
                return;
            }
            resultRgb = std::move(output);
        }
        if (m_params.stretchEnabled) {
            std::vector<uint16_t> output;
            if (!AutoOptimizeEngine::stretchRgb(
                    resultRgb, width, height, output)) {
                m_errorString = "RGB 曲线拉伸失败";
                return;
            }
            resultRgb = std::move(output);
        }
        emit progress(90);
    }

    if (stopIfCancelled()) return;
    if (m_params.starReduceEnabled && m_params.starReduceStrength > 0) {
        emit stageMessage("缩星处理...");
        if (!StarReducer::reduce(resultRgb, width, height,
                                 m_params.starReduceStrength,
                                 &m_starReductionStats)) {
            qWarning() << "缩星处理失败，继续导出未缩星结果";
        } else {
            emit stageMessage(QString(
                "缩星完成：处理 %1 颗星，其中 %2 颗弱小星被清除或显著缩小")
                .arg(m_starReductionStats.processedStars)
                .arg(m_starReductionStats.stronglySuppressedStars));
        }
        emit progress(95);
    }

    m_stackedData = std::move(resultRgb);
    if (stopIfCancelled()) return;

    emit stageMessage("导出结果...");
    QString outputPath = m_params.outputPath;
    if (outputPath.isEmpty()) outputPath = QDir::homePath() + "/StarProcessor/Output";
    QDir().mkpath(outputPath);
    const bool png = m_params.outputFormat == "png8";
    const QString extension = png ? ".png" : ".tiff";
    m_outputFile = outputPath + "/" +
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_stacked" + extension;
    if (!ImageExporter::exportRgb16(m_stackedData, width, height,
                                    m_outputFile.toStdString(),
                                    png ? ImageExporter::Png8 : ImageExporter::Tiff16)) {
        m_outputFile.clear();
        m_errorString = "导出失败";
        return;
    }
    emit progress(100);
    emit stageMessage("处理完成");
}
