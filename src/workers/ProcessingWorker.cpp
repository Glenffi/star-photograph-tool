#include "ProcessingWorker.h"

#include "core/AutoOptimizeEngine.h"
#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/ProcessingMemoryEstimator.h"
#include "core/RawImageLoader.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarReducer.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

#ifdef Q_OS_MACOS
#include <fcntl.h>
#endif

namespace {

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
    for (int startRow = 0; startRow < height; startRow += kRowsPerChunk) {
        if (cancelled()) return false;
        const int rowCount = std::min(kRowsPerChunk, height - startRow);
        std::vector<std::vector<uint16_t>> alignedChunk(
            static_cast<size_t>(aligned.frameCount()));
        for (int frame = 0; frame < aligned.frameCount(); ++frame) {
            if (!aligned.readRows(frame, width, startRow, rowCount,
                                  alignedChunk[static_cast<size_t>(frame)])) {
                return false;
            }
        }

        std::vector<uint16_t> stackedChunk;
        if (originals) {
            std::vector<std::vector<uint16_t>> originalChunk(
                static_cast<size_t>(originals->frameCount()));
            for (int frame = 0; frame < originals->frameCount(); ++frame) {
                if (!originals->readRows(frame, width, startRow, rowCount,
                                         originalChunk[static_cast<size_t>(frame)])) {
                    return false;
                }
            }
            std::vector<uint8_t> maskChunk(
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
    m_frameCount = 0;
    m_wasCancelled = false;
    m_outputFile.clear();
    emit progress(0);

    if (m_files.isEmpty()) {
        m_errorString = "没有可处理的图像";
        return;
    }

    emit stageMessage("检查图像与内存预算...");
    RawImageLoader loader;
    std::vector<RawImageLoader::Metadata> metadata;
    metadata.reserve(static_cast<size_t>(m_files.size()));
    size_t referenceIndex = 0;
    for (int i = 0; i < m_files.size(); ++i) {
        if (stopIfCancelled()) return;
        RawImageLoader::Metadata item;
        if (!loader.loadMetadata(m_files[i].toStdString(), item)) {
            m_errorString = QString("无法读取元数据: %1")
                                .arg(QFileInfo(m_files[i]).fileName());
            return;
        }
        if (m_files[i] == m_referenceFrame) referenceIndex = static_cast<size_t>(i);
        metadata.push_back(std::move(item));
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

    const uint64_t estimatedBytes = ProcessingMemoryEstimator::estimatePeakBytes(
        metadataWidth, metadataHeight, m_files.size(), m_params.skyGroundSepEnabled);
    const uint64_t budgetBytes = m_params.memoryBudgetBytes > 0
        ? m_params.memoryBudgetBytes
        : ProcessingMemoryEstimator::recommendedBudgetBytes();
    if (estimatedBytes == 0 || estimatedBytes > budgetBytes) {
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        m_errorString = QString("预计需要约 %1 GB 内存，当前安全预算为 %2 GB。"
                                "请减少帧数或关闭天地分离。")
                            .arg(estimatedBytes / kGiB, 0, 'f', 1)
                            .arg(budgetBytes / kGiB, 0, 'f', 1);
        return;
    }
    const uint64_t scratchBytes = ProcessingMemoryEstimator::estimateScratchDiskBytes(
        metadataWidth, metadataHeight, m_files.size(), m_params.skyGroundSepEnabled);
    const QStorageInfo temporaryStorage(QDir::tempPath());
    const qint64 availableScratchBytes = temporaryStorage.bytesAvailable();
    // Keep 10% headroom for filesystem metadata and the final exported image.
    if (scratchBytes == 0 || !temporaryStorage.isValid() || availableScratchBytes <= 0 ||
        static_cast<uint64_t>(availableScratchBytes) < scratchBytes / 10 * 11) {
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
    const int width = referenceImage.width;
    const int height = referenceImage.height;
    m_width = width;
    m_height = height;

    std::vector<uint16_t> referenceLuminance;
    if (!ImageBufferUtils::extractLuminance(referenceImage.data, width, height,
                                            referenceLuminance)) {
        m_errorString = "参考帧 RGB 数据无效";
        return;
    }

    emit stageMessage("参考帧星点检测...");
    StarDetector detector;
    DetectionOptions alignmentOptions;
    alignmentOptions.spatiallyBalanced = true;
    alignmentOptions.maxCandidates = 50;
    alignmentOptions.maxStars = 50;
    std::vector<StarPoint> referenceStars;
    if (!detector.detect(referenceLuminance, width, height,
                         referenceStars, alignmentOptions)) {
        m_errorString = "参考帧星点检测失败";
        return;
    }
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
    for (int i = 0; i < m_files.size(); ++i) {
        if (stopIfCancelled()) return;
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
        std::vector<StarPoint> sourceStars;
        if (!detector.detect(sourceLuminance, width, height,
                             sourceStars, alignmentOptions)) {
            qWarning() << "星点检测失败，跳过:" << m_files[i];
            continue;
        }

        AlignmentTransform transform;
        if (!aligner.align(referenceStars, sourceStars, transform)) {
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
        if (!alignedCache.append(alignedFrame) ||
            (m_params.skyGroundSepEnabled && !originalCache.append(sourceImage.data))) {
            m_errorString = "无法写入对齐临时缓存（请检查磁盘空间）";
            return;
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
        const bool maskReady = m_params.skyGroundMode == SkyGroundMask::AutoDetect
            ? SkyGroundMask::autoDetect(referenceLuminance, width, height, mask,
                                        m_params.featherRadius)
            : SkyGroundMask::loadUserMask(m_params.userMaskPath.toStdString(),
                                          width, height, mask, m_params.featherRadius);
        if (!maskReady) {
            m_errorString = m_params.skyGroundMode == SkyGroundMask::AutoDetect
                ? "天地蒙版自动检测失败" : "无法加载用户蒙版";
            return;
        }
        emit stageMessage("天地分离堆栈...");
    }
    const bool stacked = stackCachedRgb(
        stacker, alignedCache,
        m_params.skyGroundSepEnabled ? &originalCache : nullptr,
        width, height, method, m_params.kappaValue,
        m_params.skyGroundSepEnabled ? &mask : nullptr, resultRgb,
        [this]() { return stopIfCancelled(); },
        [this, height](int rows) {
            emit progress(55 + static_cast<int>(rows * 25.0 / height));
        });
    if (!stacked) {
        if (m_wasCancelled) return;
        m_errorString = m_params.skyGroundSepEnabled
            ? "天地分离堆栈失败" : "堆栈失败";
        return;
    }
    m_frameCount = alignedCache.frameCount();
    emit progress(80);
    if (stopIfCancelled()) return;

    if (m_params.dewarpEnabled || m_params.stretchEnabled) {
        emit stageMessage("自动优化...");
        ImageBufferUtils::RgbChannels channels;
        if (!ImageBufferUtils::splitRgb(resultRgb, width, height, channels)) {
            m_errorString = "堆栈结果 RGB 数据无效";
            return;
        }
        if (m_params.dewarpEnabled) {
            std::vector<uint16_t> output;
            if (AutoOptimizeEngine::dehaze(channels.red, width, height,
                                           m_params.dewarpStrength, output)) {
                channels.red = std::move(output);
            }
            if (AutoOptimizeEngine::dehaze(channels.green, width, height,
                                           m_params.dewarpStrength, output)) {
                channels.green = std::move(output);
            }
            if (AutoOptimizeEngine::dehaze(channels.blue, width, height,
                                           m_params.dewarpStrength, output)) {
                channels.blue = std::move(output);
            }
        }
        if (m_params.stretchEnabled) {
            std::vector<uint16_t> output;
            if (AutoOptimizeEngine::stretchCurve(channels.red, width, height, output)) {
                channels.red = std::move(output);
            }
            if (AutoOptimizeEngine::stretchCurve(channels.green, width, height, output)) {
                channels.green = std::move(output);
            }
            if (AutoOptimizeEngine::stretchCurve(channels.blue, width, height, output)) {
                channels.blue = std::move(output);
            }
        }
        if (!ImageBufferUtils::mergeRgb(channels, width, height, resultRgb)) {
            m_errorString = "自动优化结果 RGB 数据无效";
            return;
        }
        emit progress(90);
    }

    if (stopIfCancelled()) return;
    if (m_params.starReduceEnabled && m_params.starReduceStrength > 0) {
        emit stageMessage("缩星处理...");
        if (!StarReducer::reduce(resultRgb, width, height,
                                 m_params.starReduceStrength)) {
            qWarning() << "缩星处理失败，继续导出未缩星结果";
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
