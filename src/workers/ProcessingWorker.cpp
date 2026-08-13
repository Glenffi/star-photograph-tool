#include "ProcessingWorker.h"

#include "core/DeepSkyCalibrationPreflight.h"
#include "core/FinishingPipeline.h"
#include "core/FrameQualityEvaluator.h"
#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/MasterFrameIO.h"
#include "core/PhotometricNormalizer.h"
#include "core/ProcessingMemoryEstimator.h"
#include "core/PreviewToneMapper.h"
#include "core/RawCalibrationEngine.h"
#include "core/RawImageLoader.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarTrailEngine.h"
#include "core/TemporalPhotometricSmoother.h"
#include "core/TimelapseEngine.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
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
    if (!loader.loadPreview(path, kMaskPreviewLongSide, preview)) {
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

bool cropMask(const std::vector<uint8_t>& source,
              int sourceWidth, int sourceHeight,
              const AlignmentBounds& bounds,
              std::vector<uint8_t>& destination) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || bounds.x < 0 || bounds.y < 0 ||
        bounds.width <= 0 || bounds.height <= 0 ||
        bounds.x + bounds.width > sourceWidth ||
        bounds.y + bounds.height > sourceHeight ||
        source.size() != static_cast<size_t>(sourceWidth) * sourceHeight) {
        return false;
    }
    destination.resize(static_cast<size_t>(bounds.width) * bounds.height);
    for (int row = 0; row < bounds.height; ++row) {
        const size_t sourceOffset =
            static_cast<size_t>(bounds.y + row) * sourceWidth + bounds.x;
        const size_t destinationOffset = static_cast<size_t>(row) * bounds.width;
        std::copy_n(source.begin() + sourceOffset, bounds.width,
                    destination.begin() + destinationOffset);
    }
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

    bool readFrame(int frameIndex, size_t valueCount,
                   std::vector<uint16_t>& output) const {
        if (frameIndex < 0 || frameIndex >= m_files.size() || valueCount == 0 ||
            valueCount > static_cast<size_t>(std::numeric_limits<qint64>::max()) /
                             sizeof(uint16_t)) {
            return false;
        }
        const qint64 bytes = static_cast<qint64>(valueCount * sizeof(uint16_t));
        QFile file(m_files[frameIndex]);
        output.resize(valueCount);
        if (!file.open(QIODevice::ReadOnly)) return false;
        disableFileCache(file);
        return file.size() == bytes &&
            file.read(reinterpret_cast<char*>(output.data()), bytes) == bytes;
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
                      double kappa, StackingEngine::GroundMethod groundMethod,
                      const std::vector<uint8_t>& mask,
                      std::vector<uint16_t>& output) {
    if (aligned.empty() || originals.empty() ||
        (groundMethod != StackingEngine::GroundReferenceFrame &&
         aligned.size() != originals.size())) {
        return false;
    }

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
                               kappa, mask, redResult, groundMethod) ||
        !stacker.stackWithMask(alignedGreen, originalGreen, width, height, method,
                               kappa, mask, greenResult, groundMethod) ||
        !stacker.stackWithMask(alignedBlue, originalBlue, width, height, method,
                               kappa, mask, blueResult, groundMethod)) {
        return false;
    }
    return mergeChannels(std::move(redResult), std::move(greenResult),
                         std::move(blueResult), width, height, output);
}

bool stackCachedRgb(StackingEngine& stacker, const DiskFrameStore& aligned,
                    const DiskFrameStore* originals, int width, int height,
                    StackingEngine::Method method, double kappa,
                    StackingEngine::GroundMethod groundMethod,
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
    const int originalFramesToRead = originals
        ? (groundMethod == StackingEngine::GroundReferenceFrame
               ? 1 : originals->frameCount())
        : 0;
    std::vector<std::vector<uint16_t>> originalChunk(
        static_cast<size_t>(originalFramesToRead));
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
            for (int frame = 0; frame < originalFramesToRead; ++frame) {
                if (!originals->readRows(frame, width, startRow, rowCount,
                                         originalChunk[static_cast<size_t>(frame)])) {
                    return false;
                }
            }
            maskChunk.assign(
                mask->begin() + static_cast<size_t>(startRow) * width,
                mask->begin() + static_cast<size_t>(startRow + rowCount) * width);
            if (!stackRgbWithMask(stacker, alignedChunk, originalChunk, width,
                                  rowCount, method, kappa, groundMethod,
                                  maskChunk, stackedChunk)) {
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

StackingEngine::GroundMethod groundMethodFromName(const QString& name) {
    if (name == "reference") return StackingEngine::GroundReferenceFrame;
    if (name == "median") return StackingEngine::GroundMedian;
    return StackingEngine::GroundAverage;
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

std::vector<uint16_t> ProcessingWorker::takeQuickPreviewSource() {
    return std::move(m_quickPreviewSource);
}

std::vector<uint8_t> ProcessingWorker::takeQuickPreviewMask() {
    return std::move(m_quickPreviewMask);
}

QImage ProcessingWorker::takeBeforePreview() {
    return std::move(m_beforePreview);
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

bool ProcessingWorker::buildDeepSkyCalibration(
    RawImageLoader& loader,
    const RawImageLoader::CfaImageData& referenceLight,
    RawCalibrationEngine::MasterFrames& masters) {
    const size_t pixelCount = referenceLight.data.size();
    if (pixelCount == 0) {
        m_errorString = "参考 Light 的 Bayer 数据为空";
        return false;
    }
    const int totalCalibrationFrames = std::max(
        1, static_cast<int>(m_params.biasFramePaths.size() +
            m_params.darkFramePaths.size() +
            m_params.flatFramePaths.size() +
            m_params.darkFlatFramePaths.size()) +
            static_cast<int>(!m_params.masterBiasPath.isEmpty()) +
            static_cast<int>(!m_params.masterDarkPath.isEmpty()) +
            static_cast<int>(!m_params.masterFlatPath.isEmpty()) +
            static_cast<int>(!m_params.masterDarkFlatPath.isEmpty()));
    int processedCalibrationFrames = 0;
    auto reportCalibrationProgress = [&]() {
        ++processedCalibrationFrames;
        emit progress(10 + static_cast<int>(
            processedCalibrationFrames * 8.0 / totalCalibrationFrames));
    };

    masters = {};
    masters.width = referenceLight.width;
    masters.height = referenceLight.height;
    masters.rawWidth = referenceLight.rawWidth;
    masters.rawHeight = referenceLight.rawHeight;
    masters.topMargin = referenceLight.topMargin;
    masters.leftMargin = referenceLight.leftMargin;
    masters.iso = referenceLight.iso;
    masters.lightExposureTime = referenceLight.exposureTime;
    masters.cameraModel = referenceLight.cameraModel;
    masters.cfaPattern = referenceLight.cfaPattern;
    masters.saturation = referenceLight.saturation;

    auto descriptorFrom = [](const RawImageLoader::CfaImageData& frame) {
        RawImageLoader::CfaImageData descriptor = frame;
        descriptor.data.clear();
        descriptor.data.shrink_to_fit();
        return descriptor;
    };
    auto makeMaster = [](RawCalibrationEngine::MasterRole role,
                         const RawImageLoader::CfaImageData& source,
                         std::vector<float>&& data, bool normalizedFlat,
                         bool darkIncludesBias = true) {
        RawCalibrationEngine::MasterFrame master;
        master.role = role;
        master.width = source.width;
        master.height = source.height;
        master.rawWidth = source.rawWidth;
        master.rawHeight = source.rawHeight;
        master.topMargin = source.topMargin;
        master.leftMargin = source.leftMargin;
        master.iso = source.iso;
        master.exposureTime = source.exposureTime;
        master.cameraModel = source.cameraModel;
        master.cfaPattern = source.cfaPattern;
        master.saturation = source.saturation;
        master.normalizedFlat = normalizedFlat;
        master.darkIncludesBiasPedestal = darkIncludesBias;
        master.data = std::move(data);
        return master;
    };

    QString outputRoot = m_params.outputPath;
    if (outputRoot.isEmpty()) {
        outputRoot = QDir::homePath() + "/StarProcessor/Output";
    }
    std::unique_ptr<QTemporaryDir> pendingMasters;
    std::vector<QString> pendingMasterNames;
    if (m_params.saveGeneratedMasters) {
        if (!QDir().mkpath(outputRoot)) {
            m_errorString = "无法创建 Master 输出目录";
            return false;
        }
        pendingMasters = std::make_unique<QTemporaryDir>(
            QDir(outputRoot).filePath(".starprocessor-masters-XXXXXX"));
        if (!pendingMasters->isValid()) {
            m_errorString = "无法创建 Master 临时目录";
            return false;
        }
    }
    auto saveGenerated = [&](RawCalibrationEngine::MasterFrame& master,
                             const QString& roleName) {
        if (!pendingMasters) return true;
        const QString fileName = "master_" + roleName + ".spmaster";
        QString error;
        if (!MasterFrameIO::save(
                pendingMasters->filePath(fileName), master, error)) {
            m_errorString = QString("保存 Master %1 失败: %2")
                                .arg(roleName, error);
            return false;
        }
        pendingMasterNames.push_back(fileName);
        return true;
    };

    auto loadCompatible = [&](const QString& path, const QString& kind,
                              RawImageLoader::CfaImageData& frame) {
        if (!loader.loadRawCfa(path, frame)) {
            m_errorString = QString("无法解码%1: %2")
                .arg(kind, QFileInfo(path).fileName());
            return false;
        }
        std::string reason;
        if (!RawCalibrationEngine::compatible(
                referenceLight, frame, reason)) {
            m_errorString = QString("%1与 Light 不匹配: %2（%3）")
                .arg(kind, QFileInfo(path).fileName(),
                     QString::fromStdString(reason));
            return false;
        }
        return true;
    };

    auto loadMaster = [&](const QString& path,
                          RawCalibrationEngine::MasterRole expectedRole,
                          RawCalibrationEngine::MasterFrame& master) {
        QString error;
        if (!MasterFrameIO::load(path, master, error)) {
            m_errorString = QString("无法读取 %1: %2")
                                .arg(QFileInfo(path).fileName(), error);
            return false;
        }
        if (master.role != expectedRole) {
            m_errorString = QString("Master 类型不匹配: %1")
                                .arg(QFileInfo(path).fileName());
            return false;
        }
        reportCalibrationProgress();
        return true;
    };
    auto installMaster = [&](RawCalibrationEngine::MasterFrame& master) {
        std::string reason;
        if (!RawCalibrationEngine::installMasterFrame(
                referenceLight, master, masters, reason)) {
            m_errorString = QString("Master 与 Light 不兼容: %1")
                                .arg(QString::fromStdString(reason));
            return false;
        }
        return true;
    };

    if (!m_params.masterBiasPath.isEmpty()) {
        emit stageMessage("加载 Master Bias...");
        RawCalibrationEngine::MasterFrame master;
        if (!loadMaster(m_params.masterBiasPath,
                        RawCalibrationEngine::MasterRole::Bias, master) ||
            !installMaster(master)) return false;
    } else if (!m_params.biasFramePaths.isEmpty()) {
        emit stageMessage("生成 Master Bias...");
        RawCalibrationEngine::MeanAccumulator biasAccumulator(pixelCount);
        RawImageLoader::CfaImageData descriptor;
        for (const QString& path : m_params.biasFramePaths) {
            if (stopIfCancelled()) return false;
            RawImageLoader::CfaImageData frame;
            if (!loadCompatible(path, "Bias", frame) ||
                !biasAccumulator.add(frame.data)) {
                if (m_errorString.isEmpty()) {
                    m_errorString = "Master Bias 累加失败";
                }
                return false;
            }
            const double maximumBiasExposure = std::min(
                0.1, referenceLight.exposureTime * 0.01);
            if (frame.exposureTime <= 0.0 ||
                frame.exposureTime > maximumBiasExposure) {
                m_errorString = QString(
                    "Bias 必须使用相机最短曝光: %1（当前 %2 s）")
                    .arg(QFileInfo(path).fileName())
                    .arg(frame.exposureTime, 0, 'g', 6);
                return false;
            }
            if (descriptor.width == 0) descriptor = descriptorFrom(frame);
            reportCalibrationProgress();
        }
        std::vector<float> generated;
        if (!biasAccumulator.finish(generated)) {
            m_errorString = "Master Bias 生成失败";
            return false;
        }
        RawCalibrationEngine::MasterFrame master = makeMaster(
            RawCalibrationEngine::MasterRole::Bias, descriptor,
            std::move(generated), false);
        if (!master.complete() || !saveGenerated(master, "bias")) return false;
        masters.bias = std::move(master.data);
    }

    if (!m_params.masterDarkPath.isEmpty()) {
        emit stageMessage("加载 Master Dark...");
        RawCalibrationEngine::MasterFrame master;
        if (!loadMaster(m_params.masterDarkPath,
                        RawCalibrationEngine::MasterRole::Dark, master) ||
            !installMaster(master)) return false;
    } else {
        emit stageMessage("生成 Master Dark...");
        RawCalibrationEngine::MeanAccumulator darkAccumulator(pixelCount);
        RawImageLoader::CfaImageData descriptor;
        for (const QString& path : m_params.darkFramePaths) {
            if (stopIfCancelled()) return false;
            RawImageLoader::CfaImageData frame;
            if (!loadCompatible(path, "Dark", frame)) return false;
            if (!DeepSkyCalibrationPreflight::exposureMatches(
                    frame.exposureTime, referenceLight.exposureTime)) {
                m_errorString = QString(
                    "Dark 曝光必须与 Light 匹配: %1（Dark %2 s，Light %3 s）")
                    .arg(QFileInfo(path).fileName())
                    .arg(frame.exposureTime, 0, 'g', 6)
                    .arg(referenceLight.exposureTime, 0, 'g', 6);
                return false;
            }
            if (!darkAccumulator.add(frame.data)) {
                m_errorString = "Master Dark 累加失败";
                return false;
            }
            if (descriptor.width == 0) descriptor = descriptorFrom(frame);
            reportCalibrationProgress();
        }
        std::vector<float> generated;
        if (!darkAccumulator.finish(generated)) {
            m_errorString = "Master Dark 生成失败";
            return false;
        }
        RawCalibrationEngine::MasterFrame master = makeMaster(
            RawCalibrationEngine::MasterRole::Dark, descriptor,
            std::move(generated), false, true);
        if (!master.complete() || !saveGenerated(master, "dark")) return false;
        masters.dark = std::move(master.data);
        masters.darkIncludesBiasPedestal = true;
    }

    if (!m_params.masterFlatPath.isEmpty()) {
        emit stageMessage("加载 Master Flat...");
        RawCalibrationEngine::MasterFrame master;
        if (!loadMaster(m_params.masterFlatPath,
                        RawCalibrationEngine::MasterRole::Flat, master) ||
            !installMaster(master)) return false;
    } else {
        RawCalibrationEngine::MasterFrame darkFlatMaster;
        if (!m_params.masterDarkFlatPath.isEmpty()) {
            emit stageMessage("加载 Master Dark Flat...");
            if (!loadMaster(m_params.masterDarkFlatPath,
                            RawCalibrationEngine::MasterRole::DarkFlat,
                            darkFlatMaster)) return false;
            std::string reason;
            if (!RawCalibrationEngine::validateMasterFrame(
                    referenceLight, darkFlatMaster, reason)) {
                m_errorString = QString(
                    "Master Dark Flat 与 Light 不兼容: %1")
                                    .arg(QString::fromStdString(reason));
                return false;
            }
        } else if (!m_params.darkFlatFramePaths.isEmpty()) {
            emit stageMessage("生成 Master Dark Flat...");
            RawCalibrationEngine::MeanAccumulator darkFlatAccumulator(
                pixelCount);
            RawImageLoader::CfaImageData descriptor;
            for (const QString& path : m_params.darkFlatFramePaths) {
                if (stopIfCancelled()) return false;
                RawImageLoader::CfaImageData frame;
                if (!loadCompatible(path, "Dark Flat", frame) ||
                    !darkFlatAccumulator.add(frame.data)) {
                    if (m_errorString.isEmpty()) {
                        m_errorString = "Master Dark Flat 累加失败";
                    }
                    return false;
                }
                if (descriptor.width == 0) descriptor = descriptorFrom(frame);
                reportCalibrationProgress();
            }
            std::vector<float> generated;
            if (!darkFlatAccumulator.finish(generated)) {
                m_errorString = "Master Dark Flat 生成失败";
                return false;
            }
            darkFlatMaster = makeMaster(
                RawCalibrationEngine::MasterRole::DarkFlat, descriptor,
                std::move(generated), false, true);
            if (!darkFlatMaster.complete() ||
                !saveGenerated(darkFlatMaster, "dark_flat")) return false;
        }

        emit stageMessage("校准并生成 Master Flat...");
        RawCalibrationEngine::MeanAccumulator flatAccumulator(pixelCount);
        RawImageLoader::CfaImageData descriptor;
        for (const QString& path : m_params.flatFramePaths) {
            if (stopIfCancelled()) return false;
            RawImageLoader::CfaImageData frame;
            if (!loadCompatible(path, "Flat", frame)) return false;
            std::vector<float> normalized;
            const bool normalizedOk = !darkFlatMaster.data.empty()
                ? RawCalibrationEngine::normalizeFlat(
                      frame, darkFlatMaster, normalized)
                : RawCalibrationEngine::normalizeFlat(
                      frame, masters.bias, normalized);
            if (!normalizedOk ||
                !flatAccumulator.add(normalized)) {
                m_errorString = QString(
                    "Flat 曝光不足、接近饱和或无法归一化: %1")
                    .arg(QFileInfo(path).fileName());
                return false;
            }
            if (descriptor.width == 0) descriptor = descriptorFrom(frame);
            reportCalibrationProgress();
        }
        std::vector<float> generated;
        if (!flatAccumulator.finish(generated) ||
            !RawCalibrationEngine::finalizeMasterFlat(
                generated, referenceLight.width, referenceLight.height)) {
            m_errorString = "Master Flat 累加失败";
            return false;
        }
        RawCalibrationEngine::MasterFrame master = makeMaster(
            RawCalibrationEngine::MasterRole::Flat, descriptor,
            std::move(generated), true);
        if (!master.complete() || !saveGenerated(master, "flat")) return false;
        masters.flat = std::move(master.data);
    }

    if (!masters.complete()) {
        m_errorString = "深空 Master 组合不完整或彼此不兼容";
        return false;
    }

    // Publish a generated set only after every source and the final calibration
    // combination have passed. Renaming one directory on the same volume keeps
    // a failed run from leaving a half-written set in the user's output.
    if (pendingMasters && !pendingMasterNames.empty()) {
        const QString mastersRoot = QDir(outputRoot).filePath("Masters");
        if (!QDir().mkpath(mastersRoot)) {
            m_errorString = "无法创建 Masters 文件夹";
            return false;
        }
        const QString destination = QDir(mastersRoot).filePath(
            QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
        const QString stagingPath = pendingMasters->path();
        if (QFileInfo::exists(destination) ||
            !QDir().rename(stagingPath, destination)) {
            m_errorString = "无法提交生成的 Master 文件集";
            return false;
        }
        pendingMasters->setAutoRemove(false);
        for (const QString& fileName : pendingMasterNames) {
            m_generatedMasterFiles.append(
                QDir(destination).filePath(fileName));
        }
    }
    return true;
}

bool ProcessingWorker::loadCalibratedRaw(
    RawImageLoader& loader, const QString& path,
    const RawCalibrationEngine::MasterFrames& masters,
    RawImageLoader::ImageData& image) {
    RawImageLoader::CfaImageData light;
    if (!loader.loadRawCfa(path, light)) {
        m_errorString = QString("无法读取 Light Bayer 数据: %1")
            .arg(QFileInfo(path).fileName());
        return false;
    }
    if (!DeepSkyCalibrationPreflight::exposureMatches(
            light.exposureTime, masters.lightExposureTime)) {
        m_errorString = QString("Light 曝光与 Master Dark 不匹配: %1")
            .arg(QFileInfo(path).fileName());
        return false;
    }
    RawImageLoader::CfaImageData calibrated;
    RawCalibrationEngine::CalibrationStats stats;
    if (!RawCalibrationEngine::calibrateLight(
            light, masters, calibrated, &stats) ||
        !loader.processCalibratedCfa(path, calibrated, image)) {
        m_errorString = QString("Light 校准或去马赛克失败: %1")
            .arg(QFileInfo(path).fileName());
        return false;
    }
    ++m_calibratedLightFrameCount;
    m_calibrationClippedLowPixels += stats.clippedLowPixels;
    m_calibrationClippedHighPixels += stats.clippedHighPixels;
    m_calibrationInvalidFlatPixels += stats.invalidFlatPixels;
    return true;
}

void ProcessingWorker::run() {
    m_errorString.clear();
    m_stackedData.clear();
    m_beforePreview = QImage();
    m_beforePreviewBlackPoint = 0;
    m_beforePreviewWhitePoint = 65535;
    m_quickPreviewSource.clear();
    m_quickPreviewMask.clear();
    m_quickPreviewWidth = 0;
    m_quickPreviewHeight = 0;
    m_width = 0;
    m_height = 0;
    m_cropOffsetX = 0;
    m_cropOffsetY = 0;
    m_frameCount = 0;
    m_selectedReferenceIndex = -1;
    m_selectedReferenceFrame.clear();
    m_frameQualityMetrics.clear();
    m_qualityRejectedFiles.clear();
    m_skippedFrames.clear();
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
    m_modifiedCameraColorStats = {};
    m_skyGroundSkyFraction = 0.0;
    m_skyGroundMaskSource.clear();
    m_timelapseMotionProtectedPixelEvaluations = 0;
    m_timelapseFlickerCorrectedFrames = 0;
    m_timelapseMaximumFlickerGainChange = 0.0;
    m_timelapseMaximumFlickerOffset = 0.0;
    m_calibratedLightFrameCount = 0;
    m_calibrationClippedLowPixels = 0;
    m_calibrationClippedHighPixels = 0;
    m_calibrationInvalidFlatPixels = 0;
    m_calibrationPreflightWarnings.clear();
    m_generatedMasterFiles.clear();
    emit progress(0);

    if (m_files.isEmpty()) {
        m_errorString = "没有可处理的图像";
        return;
    }

    const int dedicatedModeCount =
        static_cast<int>(m_params.singleFrameMode) +
        static_cast<int>(m_params.timelapseMode) +
        static_cast<int>(m_params.starTrailMode) +
        static_cast<int>(m_params.deepSkyMode);
    if (dedicatedModeCount > 1) {
        m_errorString = "单张、延时、星轨和深空校准模式不能同时启用";
        return;
    }

    if (m_params.starTrailMode) {
        runStarTrail();
        return;
    }

    if (m_params.timelapseMode) {
        runTimelapse();
        return;
    }

    if (m_params.singleFrameMode) {
        runSingleFrame();
        return;
    }

    emit stageMessage("检查图像与内存预算...");
    RawImageLoader loader;
    if (m_params.deepSkyMode) {
        emit stageMessage("预检 Light 与校准来源...");
        DeepSkyCalibrationPreflight::Inputs inputs;
        inputs.lightPaths = m_files;
        inputs.darkPaths = m_params.darkFramePaths;
        inputs.flatPaths = m_params.flatFramePaths;
        inputs.biasPaths = m_params.biasFramePaths;
        inputs.darkFlatPaths = m_params.darkFlatFramePaths;
        if (!m_params.masterDarkPath.isEmpty()) {
            inputs.masterDarkPaths = {m_params.masterDarkPath};
        }
        if (!m_params.masterFlatPath.isEmpty()) {
            inputs.masterFlatPaths = {m_params.masterFlatPath};
        }
        if (!m_params.masterBiasPath.isEmpty()) {
            inputs.masterBiasPaths = {m_params.masterBiasPath};
        }
        if (!m_params.masterDarkFlatPath.isEmpty()) {
            inputs.masterDarkFlatPaths = {m_params.masterDarkFlatPath};
        }
        const DeepSkyCalibrationPreflight::Report preflight =
            DeepSkyCalibrationPreflight::inspect(loader, inputs);
        m_calibrationPreflightWarnings = preflight.warningMessages();
        if (preflight.hasErrors()) {
            m_errorString = preflight.userMessage();
            return;
        }
        emit stageMessage(
            m_calibrationPreflightWarnings.isEmpty()
                ? QString::fromUtf8("校准素材预检通过")
                : QString::fromUtf8("校准素材预检通过 · %1 项建议")
                      .arg(m_calibrationPreflightWarnings.size()));
    }
    std::vector<RawImageLoader::Metadata> metadata;
    metadata.reserve(static_cast<size_t>(m_files.size()));
    size_t explicitReferenceIndex = std::numeric_limits<size_t>::max();
    for (int i = 0; i < m_files.size(); ++i) {
        if (stopIfCancelled()) return;
        RawImageLoader::Metadata item;
        if (!loader.loadMetadata(m_files[i], item)) {
            m_errorString = QString("无法读取元数据: %1")
                                .arg(QFileInfo(m_files[i]).fileName());
            return;
        }
        if (!m_referenceFrame.isEmpty() && m_files[i] == m_referenceFrame) {
            explicitReferenceIndex = static_cast<size_t>(i);
        }
        metadata.push_back(std::move(item));
    }

    if (m_params.deepSkyMode) {
        const RawImageLoader::Metadata& first = metadata.front();
        for (int i = 1; i < static_cast<int>(metadata.size()); ++i) {
            const RawImageLoader::Metadata& item = metadata[static_cast<size_t>(i)];
            if (item.cameraModel != first.cameraModel || item.iso != first.iso ||
                !DeepSkyCalibrationPreflight::exposureMatches(
                    item.exposureTime, first.exposureTime)) {
                m_errorString = QString(
                    "深空 Light 必须来自同一相机、ISO 和曝光时间: %1")
                    .arg(QFileInfo(m_files[i]).fileName());
                return;
            }
        }
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
            if (loader.loadPreview(m_files[i], 1600, preview) &&
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
    if (!m_params.editedSkyGroundMask.empty()) {
        const QString maskSource = QFileInfo(
            m_params.editedSkyGroundMaskSourcePath).absoluteFilePath();
        const QString selectedSource = QFileInfo(
            m_selectedReferenceFrame).absoluteFilePath();
        if (maskSource.isEmpty() || maskSource != selectedSource) {
            m_errorString =
                "修补后的天地蒙版不属于最终参考帧，请重新检测并修补";
            return;
        }
    }
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
    estimateOptions.modifiedCameraColor =
        m_params.modifiedCameraColorEnabled;
    estimateOptions.dehaze = m_params.dewarpEnabled;
    estimateOptions.stretch = m_params.stretchEnabled;
    estimateOptions.basicAdjustments =
        m_params.basicAdjustments.hasToneOrColorAdjustments();
    estimateOptions.sharpening = m_params.basicAdjustments.hasSharpening();
    estimateOptions.starReduction = m_params.starReduceEnabled;
    estimateOptions.rawCalibration = m_params.deepSkyMode;
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

    RawCalibrationEngine::MasterFrames calibrationMasters;
    RawImageLoader::CfaImageData calibrationReference;
    if (m_params.deepSkyMode) {
        emit stageMessage("读取参考 Light 的 Bayer 数据...");
        if (!loader.loadRawCfa(
                m_files[static_cast<int>(referenceIndex)],
                calibrationReference)) {
            m_errorString = "参考 Light 不是当前支持的 2×2 Bayer RAW";
            return;
        }
        if (!buildDeepSkyCalibration(
                loader, calibrationReference, calibrationMasters)) {
            return;
        }
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
    bool referenceLoaded = false;
    if (m_params.deepSkyMode) {
        RawImageLoader::CfaImageData calibrated;
        RawCalibrationEngine::CalibrationStats stats;
        referenceLoaded = RawCalibrationEngine::calibrateLight(
                calibrationReference, calibrationMasters,
                calibrated, &stats) &&
            loader.processCalibratedCfa(
                m_files[static_cast<int>(referenceIndex)], calibrated,
                referenceImage);
        if (referenceLoaded) {
            ++m_calibratedLightFrameCount;
            m_calibrationClippedLowPixels += stats.clippedLowPixels;
            m_calibrationClippedHighPixels += stats.clippedHighPixels;
            m_calibrationInvalidFlatPixels += stats.invalidFlatPixels;
        }
        calibrationReference.data.clear();
        calibrationReference.data.shrink_to_fit();
    } else {
        referenceLoaded = loader.loadRaw(
            m_files[static_cast<int>(referenceIndex)], referenceImage);
    }
    if (!referenceLoaded) {
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

    // Build the reference horizon before aligning source frames. The same
    // mask must follow every accepted star transform; otherwise terrain that
    // moves with the sky-aligned RGB can leak above the final horizon.
    std::vector<uint8_t> mask;
    std::vector<uint8_t> stackSkyValidityMask;
    std::vector<uint8_t> stackSkyEligibilityMask;
    if (m_params.skyGroundSepEnabled) {
        emit stageMessage("生成天地蒙版...");
        bool maskReady = false;
        const bool editedMaskProvided =
            !m_params.editedSkyGroundMask.empty();
        if (editedMaskProvided) {
            maskReady = SkyGroundMask::prepareEditedMask(
                m_params.editedSkyGroundMask,
                m_params.editedSkyGroundMaskWidth,
                m_params.editedSkyGroundMaskHeight,
                width, height, mask, m_params.featherRadius);
            if (maskReady) m_skyGroundMaskSource = "guided-preview";
        } else if (m_params.skyGroundMode == SkyGroundMask::AutoDetect) {
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
                m_params.userMaskPath, width, height, mask,
                m_params.featherRadius);
            if (maskReady) m_skyGroundMaskSource = "user-mask";
        }
        if (!maskReady) {
            m_errorString = editedMaskProvided
                ? "修补后的天地蒙版尺寸或数据无效"
                : m_params.skyGroundMode == SkyGroundMask::AutoDetect
                    ? "天地蒙版自动检测失败" : "无法加载用户蒙版";
            return;
        }
        const double maskSum = std::accumulate(mask.begin(), mask.end(), 0.0);
        m_skyGroundSkyFraction = maskSum / (255.0 * mask.size());
        stackSkyValidityMask.resize(mask.size());
        stackSkyEligibilityMask.resize(mask.size());
        std::transform(
            mask.begin(), mask.end(), stackSkyValidityMask.begin(),
            [](uint8_t value) { return value >= 224 ? 255 : 0; });
        std::transform(
            mask.begin(), mask.end(), stackSkyEligibilityMask.begin(),
            [](uint8_t value) { return value >= 16 ? 255 : 0; });
        if (!m_params.skyGroundMaskOutputPath.isEmpty()) {
            const QImage borrowedMask(mask.data(), width, height, width,
                                      QImage::Format_Grayscale8);
            if (!borrowedMask.copy().save(m_params.skyGroundMaskOutputPath)) {
                m_errorString = "无法保存天地蒙版诊断图";
                return;
            }
        }
    }
    emit progress(20);

    // Full-resolution aligned frames are cached on disk. Keeping both decoded
    // and aligned sequences resident makes peak RAM grow linearly twice and is
    // unsafe for ordinary 30-60 MP sequences.
    if ((m_params.skyGroundSepEnabled &&
         !originalCache.append(referenceImage.data)) ||
        !alignedCache.append(referenceImage.data)) {
        m_errorString = "无法写入参考帧临时缓存（请检查磁盘空间）";
        return;
    }
    referenceImage.data.clear();
    referenceImage.data.shrink_to_fit();
    referenceLuminance.clear();
    referenceLuminance.shrink_to_fit();

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
        const bool sourceLoaded = m_params.deepSkyMode
            ? loadCalibratedRaw(
                  loader, m_files[i], calibrationMasters, sourceImage)
            : loader.loadRaw(m_files[i], sourceImage);
        if (!sourceLoaded) {
            if (m_params.deepSkyMode) return;
            SkippedFrameInfo skipped;
            skipped.filePath = m_files[i];
            skipped.stage = "raw-load";
            skipped.reason = "RAW 解码失败";
            m_skippedFrames.push_back(std::move(skipped));
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
            SkippedFrameInfo skipped;
            skipped.filePath = m_files[i];
            skipped.stage = "star-detection";
            skipped.reason = "未检测到可用于对齐的星点";
            m_skippedFrames.push_back(std::move(skipped));
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
            SkippedFrameInfo skipped;
            skipped.filePath = m_files[i];
            skipped.stage = "alignment";
            skipped.detectedStars = static_cast<int>(sourceDetectedStars.size());
            skipped.affineEvaluated = alignmentQuality.affineEvaluated;
            skipped.affineMatchedStars =
                alignmentQuality.affineCandidate.matchedStars;
            skipped.affineRms = alignmentQuality.affineCandidate.rmsError;
            skipped.affineP95 = alignmentQuality.affineCandidate.p95Error;
            skipped.affineEligibleCells =
                alignmentQuality.affineCandidate.eligibleCells;
            skipped.affineCoveredCells =
                alignmentQuality.affineCandidate.coveredCells;
            skipped.affineGridCoverage =
                alignmentQuality.affineCandidate.gridCoverage;
            for (const std::string& reason :
                 alignmentQuality.affineCandidate.failureReasons) {
                skipped.affineFailureReasons.append(
                    QString::fromStdString(reason));
            }
            skipped.homographyEvaluated =
                alignmentQuality.homographyEvaluated;
            skipped.homographyMatchedStars =
                alignmentQuality.homographyCandidate.matchedStars;
            skipped.homographyRms =
                alignmentQuality.homographyCandidate.rmsError;
            skipped.homographyP95 =
                alignmentQuality.homographyCandidate.p95Error;
            skipped.homographyEligibleCells =
                alignmentQuality.homographyCandidate.eligibleCells;
            skipped.homographyCoveredCells =
                alignmentQuality.homographyCandidate.coveredCells;
            skipped.homographyGridCoverage =
                alignmentQuality.homographyCandidate.gridCoverage;
            for (const std::string& reason :
                 alignmentQuality.homographyCandidate.failureReasons) {
                skipped.homographyFailureReasons.append(
                    QString::fromStdString(reason));
            }
            if (!skipped.affineEvaluated && !skipped.homographyEvaluated) {
                skipped.reason = "星点匹配未形成可拟合模型";
            } else {
                QStringList failedModels;
                if (skipped.affineEvaluated) {
                    failedModels.append(QString("Affine: %1")
                        .arg(skipped.affineFailureReasons.isEmpty()
                            ? QStringLiteral("未通过质量门限")
                            : skipped.affineFailureReasons.join(", ")));
                }
                if (skipped.homographyEvaluated) {
                    failedModels.append(QString("Homography: %1")
                        .arg(skipped.homographyFailureReasons.isEmpty()
                            ? QStringLiteral("未通过质量门限")
                            : skipped.homographyFailureReasons.join(", ")));
                }
                skipped.reason = failedModels.join("; ");
            }
            qWarning() << "对齐失败，跳过:" << m_files[i]
                       << skipped.reason;
            m_skippedFrames.push_back(std::move(skipped));
            continue;
        }
        sourceLuminance.clear();
        sourceLuminance.shrink_to_fit();

        std::vector<uint16_t> alignedFrame;
        if (!aligner.applyTransformRgb(sourceImage.data, width, height, transform,
                                       alignedFrame)) {
            SkippedFrameInfo skipped;
            skipped.filePath = m_files[i];
            skipped.stage = "resampling";
            skipped.reason = "变换重采样失败";
            m_skippedFrames.push_back(std::move(skipped));
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
        if (m_params.skyGroundSepEnabled) {
            if (!originalCache.append(sourceImage.data)) {
                m_errorString = "无法写入原位地景临时缓存（请检查磁盘空间）";
                return;
            }
            std::vector<uint8_t> alignedSourceMask;
            if (!aligner.applyTransformMask(
                    stackSkyValidityMask, width, height, transform,
                    alignedSourceMask) ||
                !ImageBufferUtils::excludeShiftedGroundInPlace(
                    alignedFrame, alignedSourceMask, stackSkyEligibilityMask,
                    width, height)) {
                m_errorString = "无法约束天空对齐中的地景样本";
                return;
            }
        }
        if (!alignedCache.append(alignedFrame)) {
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
    const StackingEngine::GroundMethod groundMethod =
        groundMethodFromName(m_params.groundStackMethod);
    std::vector<uint16_t> resultRgb;
    if (m_params.skyGroundSepEnabled) {
        emit stageMessage("天地分离堆栈...");
    }
    QElapsedTimer stackingTimer;
    stackingTimer.start();
    const bool stacked = stackCachedRgb(
        stacker, alignedCache,
        m_params.skyGroundSepEnabled ? &originalCache : nullptr,
        width, height, method, m_params.kappaValue,
        groundMethod,
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
        if (!mask.empty()) {
            std::vector<uint8_t> croppedMask;
            if (!cropMask(mask, width, height, commonBounds, croppedMask)) {
                m_errorString = "天地蒙版裁切失败";
                return;
            }
            mask = std::move(croppedMask);
        }
        m_cropOffsetX = commonBounds.x;
        m_cropOffsetY = commonBounds.y;
        width = commonBounds.width;
        height = commonBounds.height;
        m_width = width;
        m_height = height;
    }

    if (!finishResult(resultRgb, width, height, mask)) return;
}

void ProcessingWorker::runStarTrail() {
    if (m_files.size() < 3) {
        m_errorString = "星轨合成至少需要 3 张固定机位 RAW";
        return;
    }
    if (m_params.starReduceEnabled && m_params.starReduceStrength > 0) {
        m_errorString = "星轨合成不能同时启用缩星";
        return;
    }

    emit stageMessage("检查星轨序列与内存预算...");
    RawImageLoader loader;
    std::vector<RawImageLoader::Metadata> metadata;
    metadata.reserve(static_cast<size_t>(m_files.size()));
    for (int index = 0; index < m_files.size(); ++index) {
        if (stopIfCancelled()) return;
        RawImageLoader::Metadata item;
        if (!loader.loadMetadata(m_files[index], item)) {
            m_errorString = QString("无法读取元数据: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }
        if (!metadata.empty() &&
            (item.width != metadata.front().width ||
             item.height != metadata.front().height)) {
            m_errorString = QString("星轨序列尺寸不一致: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }
        metadata.push_back(std::move(item));
    }

    std::vector<int> chronologicalOrder(static_cast<size_t>(m_files.size()));
    std::iota(chronologicalOrder.begin(), chronologicalOrder.end(), 0);
    const bool allHaveTimestamps = std::all_of(
        metadata.begin(), metadata.end(),
        [](const RawImageLoader::Metadata& item) {
            return !item.timestamp.empty();
        });
    std::stable_sort(
        chronologicalOrder.begin(), chronologicalOrder.end(),
        [this, &metadata, allHaveTimestamps](int left, int right) {
            const std::string& leftTime = metadata[static_cast<size_t>(left)].timestamp;
            const std::string& rightTime = metadata[static_cast<size_t>(right)].timestamp;
            if (allHaveTimestamps && leftTime != rightTime) {
                return leftTime < rightTime;
            }
            return QFileInfo(m_files[left]).fileName().compare(
                       QFileInfo(m_files[right]).fileName(),
                       Qt::CaseInsensitive) < 0;
        });
    QStringList orderedFiles;
    orderedFiles.reserve(m_files.size());
    std::vector<RawImageLoader::Metadata> orderedMetadata;
    orderedMetadata.reserve(metadata.size());
    for (int index : chronologicalOrder) {
        orderedFiles.append(m_files[index]);
        orderedMetadata.push_back(std::move(metadata[static_cast<size_t>(index)]));
    }
    m_files = std::move(orderedFiles);
    metadata = std::move(orderedMetadata);

    const int width = metadata.front().width;
    const int height = metadata.front().height;
    ProcessingMemoryEstimator::EstimateOptions estimateOptions;
    // The compositor is streaming. One synthetic frame keeps the generic
    // estimator's finishing-stage model while avoiding a false frame-count
    // multiplier; sky/ground mode also reserves the online foreground mean.
    estimateOptions.frameCount = 1;
    estimateOptions.skyGroundSeparation = m_params.starTrailProtectGround;
    estimateOptions.noiseReduction = m_params.noiseReductionEnabled;
    estimateOptions.modifiedCameraColor = m_params.modifiedCameraColorEnabled;
    estimateOptions.dehaze = m_params.dewarpEnabled;
    estimateOptions.stretch = m_params.stretchEnabled;
    estimateOptions.basicAdjustments =
        m_params.basicAdjustments.hasToneOrColorAdjustments();
    estimateOptions.sharpening = m_params.basicAdjustments.hasSharpening();
    estimateOptions.starReduction = false;
    const uint64_t estimatedBytes = ProcessingMemoryEstimator::estimatePeakBytes(
        width, height, estimateOptions);
    const ProcessingMemoryEstimator::SystemMemoryInfo memoryInfo =
        ProcessingMemoryEstimator::systemMemoryInfo();
    const uint64_t budgetBytes =
        ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
            memoryInfo.safeBudgetBytes, m_params.memoryBudgetBytes);
    if (estimatedBytes == 0 || estimatedBytes > budgetBytes) {
        m_errorString = QString(
            "星轨合成预计需要约 %1 内存，当前安全预算为 %2"
            "（系统当前可用约 %3）。请关闭去雾或降噪后重试。")
                            .arg(formatMemoryBytes(estimatedBytes))
                            .arg(formatMemoryBytes(budgetBytes))
                            .arg(formatMemoryBytes(memoryInfo.availableBytes));
        return;
    }

    StarTrailEngine::Options trailOptions;
    trailOptions.cometStrength =
        std::clamp(m_params.starTrailCometStrength, 0, 100);
    trailOptions.mode = trailOptions.cometStrength > 0
        ? StarTrailEngine::Mode::Comet : StarTrailEngine::Mode::Lighten;
    trailOptions.direction = m_params.starTrailReverse
        ? StarTrailEngine::TailDirection::Reverse
        : StarTrailEngine::TailDirection::Forward;
    StarTrailEngine trailEngine;
    if (!trailEngine.initialize(width, height, trailOptions)) {
        m_errorString = QString::fromUtf8("无法初始化星轨合成器：%1")
            .arg(QString::fromLatin1(
                StarTrailEngine::errorMessage(trailEngine.lastError())));
        return;
    }

    bool protectGround = m_params.starTrailProtectGround;
    bool groundMaskReady = false;
    std::vector<uint8_t> skyMask;
    if (protectGround) {
        emit stageMessage("检测固定地景蒙版...");
        const int maskFrameIndex = m_files.size() / 2;
        QImage preview;
        if (loadMaskPreview(m_files[maskFrameIndex], preview) &&
            SkyGroundMask::autoDetectPreview(
                preview, width, height, skyMask, m_params.featherRadius)) {
            m_skyGroundMaskSource = "star-trail-auto-preview";
            groundMaskReady = true;
        }
    }

    emit stageMessage(
        trailOptions.mode == StarTrailEngine::Mode::Lighten
            ? QString::fromUtf8("逐帧累积连续星轨...")
            : QString::fromUtf8("逐帧累积彗星星轨..."));
    std::vector<uint16_t> groundAverage;
    PhotometricReferenceProfile photometricReference;
    bool photometricReferenceReady = false;
    size_t groundFrameCount = 0;
    for (int index = 0; index < m_files.size(); ++index) {
        if (stopIfCancelled()) return;
        RawImageLoader::ImageData image;
        if (!loader.loadRaw(m_files[index], image) ||
            image.width != width || image.height != height ||
            image.channels != 3) {
            m_errorString = QString("无法解码星轨 RAW: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }

        if (index == 0 && protectGround && !groundMaskReady) {
            std::vector<uint16_t> luminance;
            if (ImageBufferUtils::extractLuminance(
                    image.data, width, height, luminance) &&
                SkyGroundMask::autoDetect(
                    luminance, width, height, skyMask,
                    m_params.featherRadius)) {
                m_skyGroundMaskSource = "star-trail-auto-linear";
                groundMaskReady = true;
            } else {
                qWarning() << "星轨地平线检测失败，本次按纯天空序列合成";
                emit stageMessage("未检测到可靠地景，按纯天空星轨处理");
                try {
                    skyMask.assign(
                        static_cast<size_t>(width) * height, uint8_t{255});
                } catch (const std::bad_alloc&) {
                    m_errorString = "无法分配星轨全天空蒙版";
                    return;
                }
                m_skyGroundMaskSource = "star-trail-all-sky-fallback";
            }
        }

        if (index == 0 && m_params.photometricNormalizationEnabled) {
            photometricReferenceReady =
                PhotometricNormalizer::buildReferenceProfile(
                    image.data, width, height, photometricReference, 65536,
                    protectGround ? &skyMask : nullptr);
            if (!photometricReferenceReady) {
                qWarning() << "星轨参考帧光度模型不可用，保留原始帧亮度";
            }
        } else if (photometricReferenceReady) {
            PhotometricModel model;
            if (PhotometricNormalizer::estimate(
                    photometricReference, image.data, model) &&
                PhotometricNormalizer::applyInPlace(
                    image.data, width, height, model)) {
                ++m_photometricNormalizedFrameCount;
                m_photometricGainSum += model.gain;
                if (m_photometricNormalizedFrameCount == 1) {
                    m_photometricMinGain = model.gain;
                    m_photometricMaxGain = model.gain;
                } else {
                    m_photometricMinGain = std::min(
                        m_photometricMinGain, model.gain);
                    m_photometricMaxGain = std::max(
                        m_photometricMaxGain, model.gain);
                }
                for (double offset : model.offsets) {
                    m_photometricMaxAbsOffset = std::max(
                        m_photometricMaxAbsOffset, std::abs(offset));
                }
            } else {
                ++m_photometricSkippedFrameCount;
                qWarning() << "星轨帧光度匹配失败，保留原亮度:"
                           << m_files[index];
            }
        }

        if (!trailEngine.addFrame(image.data)) {
            m_errorString = QString::fromUtf8("星轨累积失败：%1")
                .arg(QString::fromLatin1(
                    StarTrailEngine::errorMessage(trailEngine.lastError())));
            return;
        }

        if (protectGround && groundMaskReady) {
            if (groundFrameCount == 0) {
                try {
                    groundAverage = image.data;
                } catch (const std::bad_alloc&) {
                    m_errorString = "无法分配地景降噪缓冲";
                    return;
                }
            } else {
                const uint64_t oldCount =
                    static_cast<uint64_t>(groundFrameCount);
                const uint64_t newCount = oldCount + 1;
                for (size_t value = 0; value < groundAverage.size(); ++value) {
                    if ((value & 0xfffffU) == 0 && stopIfCancelled()) return;
                    const uint64_t weighted =
                        static_cast<uint64_t>(groundAverage[value]) * oldCount +
                        image.data[value];
                    groundAverage[value] = static_cast<uint16_t>(
                        (weighted + newCount / 2) / newCount);
                }
            }
            ++groundFrameCount;
        }

        emit progress(10 + static_cast<int>(
            (index + 1) * 60.0 / m_files.size()));
    }

    std::vector<uint16_t> resultRgb;
    if (!trailEngine.render(resultRgb)) {
        m_errorString = QString::fromUtf8("星轨结果生成失败：%1")
            .arg(QString::fromLatin1(
                StarTrailEngine::errorMessage(trailEngine.lastError())));
        return;
    }
    trailEngine.reset();

    if (protectGround) {
        const uint64_t skySum = std::accumulate(
            skyMask.begin(), skyMask.end(), uint64_t{0});
        m_skyGroundSkyFraction = skyMask.empty() ? 0.0
            : static_cast<double>(skySum) /
                (255.0 * static_cast<double>(skyMask.size()));
        if (groundMaskReady) {
            emit stageMessage("融合降噪地景...");
            if (!ImageBufferUtils::blendSkyGroundInPlace(
                    resultRgb, groundAverage, skyMask, width, height)) {
                m_errorString = "星轨天空与地景融合失败";
                return;
            }
        }
        if (!m_params.skyGroundMaskOutputPath.isEmpty()) {
            const QImage borrowedMask(
                skyMask.data(), width, height, width,
                QImage::Format_Grayscale8);
            if (!borrowedMask.copy().save(m_params.skyGroundMaskOutputPath)) {
                m_errorString = "无法保存星轨地景蒙版诊断图";
                return;
            }
        }
    }

    m_width = width;
    m_height = height;
    m_frameCount = m_files.size();
    m_selectedReferenceIndex = 0;
    m_selectedReferenceFrame = m_files.front();
    emit progress(78);
    if (!finishResult(resultRgb, width, height, skyMask)) return;
}

void ProcessingWorker::runTimelapse() {
    if (m_files.size() < 3) {
        m_errorString = "延时序列降噪至少需要 3 张 RAW";
        return;
    }
    const int windowSize = m_params.timelapseWindowSize == 3 ? 3 : 5;
    const int temporalStrength = std::clamp(m_params.timelapseStrength, 0, 100);
    bool protectGround = m_params.timelapseProtectGround;

    emit stageMessage("预分析延时序列...");
    RawImageLoader loader;
    std::vector<RawImageLoader::Metadata> metadata;
    metadata.reserve(static_cast<size_t>(m_files.size()));
    for (int index = 0; index < m_files.size(); ++index) {
        if (stopIfCancelled()) return;
        RawImageLoader::Metadata item;
        if (!loader.loadMetadata(m_files[index], item)) {
            m_errorString = QString("无法读取元数据: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }
        if (!metadata.empty() &&
            (item.width != metadata.front().width ||
             item.height != metadata.front().height)) {
            m_errorString = QString("延时序列尺寸不一致: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }
        metadata.push_back(std::move(item));
        emit progress(1 + static_cast<int>(
            (index + 1) * 4.0 / m_files.size()));
    }

    const int metadataWidth = metadata.front().width;
    const int metadataHeight = metadata.front().height;
    if (metadataWidth <= 0 || metadataHeight <= 0 ||
        static_cast<size_t>(metadataWidth) >
            std::numeric_limits<size_t>::max() /
                static_cast<size_t>(metadataHeight) / 3) {
        m_errorString = "延时序列图像尺寸无效";
        return;
    }
    const uint64_t estimatedBytes =
        ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
            metadataWidth, metadataHeight, windowSize, protectGround,
            m_params.timelapseMotionProtection > 0);
    const ProcessingMemoryEstimator::SystemMemoryInfo memoryInfo =
        ProcessingMemoryEstimator::systemMemoryInfo();
    const uint64_t budgetBytes =
        ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
            memoryInfo.safeBudgetBytes, m_params.memoryBudgetBytes);
    if (estimatedBytes == 0 || estimatedBytes > budgetBytes) {
        m_errorString = QString("延时 %1 帧窗口预计需要约 %2 内存，当前安全预算为 %3。"
                                "请改用 3 帧窗口或关闭固定地景保护。")
                            .arg(windowSize)
                            .arg(formatMemoryBytes(estimatedBytes))
                            .arg(formatMemoryBytes(budgetBytes));
        return;
    }

    const uint64_t scratchBytes =
        ProcessingMemoryEstimator::estimateScratchDiskBytes(
            metadataWidth, metadataHeight, m_files.size(), false);
    const QStorageInfo temporaryStorage(QDir::tempPath());
    const uint64_t scratchHeadroom = scratchBytes / 10 +
        (scratchBytes % 10 == 0 ? 0 : 1);
    if (scratchBytes == 0 || !temporaryStorage.isValid() ||
        temporaryStorage.bytesAvailable() <= 0 ||
        scratchBytes > std::numeric_limits<uint64_t>::max() - scratchHeadroom ||
        static_cast<uint64_t>(temporaryStorage.bytesAvailable()) <
            scratchBytes + scratchHeadroom) {
        m_errorString = QString("临时磁盘空间不足：延时 RAW 缓存约需 %1。")
                            .arg(formatMemoryBytes(scratchBytes));
        return;
    }

    DiskFrameStore decodedCache("starprocessor-timelapse");
    if (!decodedCache.isValid()) {
        m_errorString = "无法创建延时处理临时目录";
        return;
    }

    emit stageMessage("预解码 RAW 并检测星点...");
    StarDetector detector;
    DetectionOptions detectionOptions;
    detectionOptions.spatiallyBalanced = true;
    detectionOptions.maxCandidates = 60;
    detectionOptions.maxStars = 30;
    detectionOptions.gridCols = 6;
    detectionOptions.gridRows = 4;
    std::vector<std::vector<StarPoint>> detectedStars(
        static_cast<size_t>(m_files.size()));
    int width = 0;
    int height = 0;
    size_t rgbValueCount = 0;
    for (int index = 0; index < m_files.size(); ++index) {
        if (stopIfCancelled()) return;
        RawImageLoader::ImageData image;
        if (!loader.loadRaw(m_files[index], image)) {
            m_errorString = QString("无法解码 RAW: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }
        if (index == 0) {
            width = image.width;
            height = image.height;
            if (width <= 0 || height <= 0 ||
                static_cast<size_t>(width) >
                    std::numeric_limits<size_t>::max() /
                        static_cast<size_t>(height) / 3) {
                m_errorString = "延时 RAW 实际解码尺寸无效";
                return;
            }
            rgbValueCount = static_cast<size_t>(width) * height * 3;
        } else if (image.width != width || image.height != height) {
            m_errorString = QString("RAW 实际解码尺寸不一致: %1")
                                .arg(QFileInfo(m_files[index]).fileName());
            return;
        }

        std::vector<uint16_t> luminance;
        if (ImageBufferUtils::extractLuminance(
                image.data, width, height, luminance)) {
            detector.detect(luminance, width, height,
                            detectedStars[static_cast<size_t>(index)],
                            detectionOptions);
        }
        if (!decodedCache.append(image.data)) {
            m_errorString = "延时 RAW 临时缓存写入失败";
            return;
        }
        emit progress(5 + static_cast<int>(
            (index + 1) * 20.0 / m_files.size()));
    }
    m_width = width;
    m_height = height;

    struct NeighborTransform {
        int sourceIndex = -1;
        AlignmentTransform transform;
    };
    std::vector<std::vector<NeighborTransform>> transforms(
        static_cast<size_t>(m_files.size()));
    ImageAligner aligner;
    int alignedPairCount = 0;
    const int radius = windowSize / 2;
    emit stageMessage("预分析邻近帧对齐...");
    for (int targetIndex = 0; targetIndex < m_files.size(); ++targetIndex) {
        if (stopIfCancelled()) return;
        const int start = std::max(0, targetIndex - radius);
        const int end = std::min(
            static_cast<int>(m_files.size()) - 1, targetIndex + radius);
        for (int sourceIndex = start; sourceIndex <= end; ++sourceIndex) {
            if (stopIfCancelled()) return;
            if (sourceIndex == targetIndex) continue;
            if (detectedStars[static_cast<size_t>(targetIndex)].size() < 3 ||
                detectedStars[static_cast<size_t>(sourceIndex)].size() < 3) {
                continue;
            }
            AlignmentTransform transform;
            AlignmentQuality quality;
            AlignmentOptions options;
            options.imageWidth = width;
            options.imageHeight = height;
            const bool aligned = aligner.align(
                    detectedStars[static_cast<size_t>(targetIndex)],
                    detectedStars[static_cast<size_t>(sourceIndex)],
                    transform, &quality, options);
            if (stopIfCancelled()) return;
            if (aligned) {
                transforms[static_cast<size_t>(targetIndex)].push_back(
                    {sourceIndex, transform});
                ++alignedPairCount;
                if (transform.model == AlignmentModel::Homography) {
                    ++m_homographyFrameCount;
                } else {
                    ++m_affineFrameCount;
                }
                m_alignmentRmsSum += quality.rmsError;
                m_worstAlignmentP95 = std::max(
                    m_worstAlignmentP95, quality.p95Error);
                if (alignedPairCount == 1) {
                    m_minimumGridCoverage = quality.gridCoverage;
                } else {
                    m_minimumGridCoverage = std::min(
                        m_minimumGridCoverage, quality.gridCoverage);
                }
            }
        }
        emit progress(25 + static_cast<int>(
            (targetIndex + 1) * 3.0 / m_files.size()));
    }
    if (alignedPairCount == 0 && !protectGround) {
        m_errorString = "延时序列的邻近帧均无法完成星点对齐";
        return;
    }

    std::vector<uint8_t> skyMask;
    if (protectGround) {
        if (stopIfCancelled()) return;
        const int maskFrameIndex = m_files.size() / 2;
        QImage preview;
        bool usedEmbeddedPreview =
            loadMaskPreview(m_files[maskFrameIndex], preview);
        bool maskReady = usedEmbeddedPreview &&
            SkyGroundMask::autoDetectPreview(
                preview, width, height, skyMask, m_params.featherRadius);
        if (!maskReady) {
            usedEmbeddedPreview = false;
            std::vector<uint16_t> maskFrame;
            std::vector<uint16_t> maskLuminance;
            maskReady = decodedCache.readFrame(
                            maskFrameIndex, rgbValueCount, maskFrame) &&
                ImageBufferUtils::extractLuminance(
                    maskFrame, width, height, maskLuminance) &&
                SkyGroundMask::autoDetect(
                    maskLuminance, width, height, skyMask,
                    m_params.featherRadius);
        }
        if (stopIfCancelled()) return;
        if (!maskReady) {
            qWarning() << "延时地平线检测失败，本次按纯天空序列处理";
            protectGround = false;
            skyMask.clear();
        } else {
            const uint64_t skySum = std::accumulate(
                skyMask.begin(), skyMask.end(), uint64_t{0});
            m_skyGroundSkyFraction = skyMask.empty() ? 0.0
                : static_cast<double>(skySum) /
                    (255.0 * static_cast<double>(skyMask.size()));
            m_skyGroundMaskSource = usedEmbeddedPreview
                ? "timelapse-auto-preview"
                : "timelapse-auto-linear";
            if (!m_params.skyGroundMaskOutputPath.isEmpty()) {
                const QImage borrowedMask(
                    skyMask.data(), width, height, width,
                    QImage::Format_Grayscale8);
                if (!borrowedMask.copy().save(
                        m_params.skyGroundMaskOutputPath)) {
                    m_errorString = "无法保存延时天地蒙版诊断图";
                    return;
                }
            }
        }
    }

    std::vector<TemporalPhotometricSmoother::Sample> sequencePhotometry(
        static_cast<size_t>(m_files.size()));
    if (m_params.photometricNormalizationEnabled) {
        emit stageMessage("分析跨帧亮度与色偏曲线...");
        const int anchorIndex = m_files.size() / 2;
        std::vector<uint16_t> anchorRgb;
        PhotometricReferenceProfile anchorProfile;
        const std::vector<uint8_t>* photometricMask =
            protectGround ? &skyMask : nullptr;
        const bool anchorReady = decodedCache.readFrame(
                anchorIndex, rgbValueCount, anchorRgb) &&
            PhotometricNormalizer::buildReferenceProfile(
                anchorRgb, width, height, anchorProfile, 65536,
                photometricMask, 160);
        if (stopIfCancelled()) return;
        if (anchorReady) {
            sequencePhotometry[static_cast<size_t>(anchorIndex)].valid = true;
            for (int index = 0; index < m_files.size(); ++index) {
                if (stopIfCancelled()) return;
                if (index != anchorIndex) {
                    std::vector<uint16_t> frameRgb;
                    PhotometricModel model;
                    if (decodedCache.readFrame(
                            index, rgbValueCount, frameRgb) &&
                        PhotometricNormalizer::estimate(
                            anchorProfile, frameRgb, model)) {
                        auto& sample = sequencePhotometry[
                            static_cast<size_t>(index)];
                        sample.valid = true;
                        sample.model = model;
                    }
                }
                emit progress(28 + static_cast<int>(
                    (index + 1) * 2.0 / m_files.size()));
            }
        }
    }

    QString rootOutput = m_params.outputPath;
    if (rootOutput.isEmpty()) {
        rootOutput = QDir::homePath() + "/StarProcessor/Output";
    }
    const QString outputDirectory = QDir(rootOutput).filePath(
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") +
        "_timelapse");
    if (stopIfCancelled()) return;
    if (!QDir().mkpath(outputDirectory)) {
        m_errorString = "无法创建延时序列输出目录";
        return;
    }
    m_outputFile = outputDirectory;
    m_selectedReferenceIndex = m_files.size() / 2;
    m_selectedReferenceFrame = m_files[m_selectedReferenceIndex];

    TimelapseEngine::Options temporalOptions;
    temporalOptions.windowSize = static_cast<size_t>(windowSize);
    temporalOptions.temporalSigma = windowSize == 3 ? 1.0 : 1.5;
    temporalOptions.madThreshold = 3.0;
    temporalOptions.minimumDeviation = 16.0;
    temporalOptions.strength = temporalStrength;
    temporalOptions.motionProtection = std::clamp(
        m_params.timelapseMotionProtection, 0, 100);
    TemporalPhotometricSmoother::Options flickerOptions;
    flickerOptions.windowSize = 5;
    flickerOptions.strength = 65.0;

    emit stageMessage("逐帧滑动窗口降噪...");
    int writtenFrames = 0;
    enum class NormalizationState {
        NotAttempted,
        Failed,
        Succeeded
    };
    std::vector<NormalizationState> normalizationStates(
        static_cast<size_t>(m_files.size()),
        NormalizationState::NotAttempted);
    for (int targetIndex = 0; targetIndex < m_files.size(); ++targetIndex) {
        if (stopIfCancelled()) return;
        const int start = std::max(0, targetIndex - radius);
        const int end = std::min(
            static_cast<int>(m_files.size()) - 1, targetIndex + radius);

        std::vector<uint16_t> targetRgb;
        if (!decodedCache.readFrame(
                targetIndex, rgbValueCount, targetRgb)) {
            m_errorString = "延时目标帧缓存读取失败";
            return;
        }
        PhotometricReferenceProfile targetProfile;
        const bool targetProfileReady =
            m_params.photometricNormalizationEnabled &&
            PhotometricNormalizer::buildReferenceProfile(
                targetRgb, width, height, targetProfile);
        auto normalizeToTarget = [&](std::vector<uint16_t>& rgb,
                                     int sourceIndex) {
            if (!m_params.photometricNormalizationEnabled) return;
            auto& state = normalizationStates[
                static_cast<size_t>(sourceIndex)];
            if (!targetProfileReady) {
                if (state != NormalizationState::Succeeded) {
                    state = NormalizationState::Failed;
                }
                return;
            }
            PhotometricModel model;
            if (!PhotometricNormalizer::estimate(
                    targetProfile, rgb, model)) {
                if (state != NormalizationState::Succeeded) {
                    state = NormalizationState::Failed;
                }
                return;
            }
            if (!PhotometricNormalizer::applyInPlace(
                    rgb, width, height, model)) {
                if (state != NormalizationState::Succeeded) {
                    state = NormalizationState::Failed;
                }
                return;
            }
            if (state == NormalizationState::Succeeded) return;
            state = NormalizationState::Succeeded;
            ++m_photometricNormalizedFrameCount;
            m_photometricGainSum += model.gain;
            if (m_photometricNormalizedFrameCount == 1) {
                m_photometricMinGain = model.gain;
                m_photometricMaxGain = model.gain;
            } else {
                m_photometricMinGain = std::min(
                    m_photometricMinGain, model.gain);
                m_photometricMaxGain = std::max(
                    m_photometricMaxGain, model.gain);
            }
            for (double offset : model.offsets) {
                m_photometricMaxAbsOffset = std::max(
                    m_photometricMaxAbsOffset, std::abs(offset));
            }
        };

        std::vector<std::vector<uint16_t>> skyFrames;
        std::vector<double> skyDistances;
        std::vector<std::vector<uint16_t>> groundFrames;
        std::vector<double> groundDistances;
        skyFrames.reserve(static_cast<size_t>(windowSize));
        skyDistances.reserve(static_cast<size_t>(windowSize));
        if (protectGround) {
            groundFrames.reserve(static_cast<size_t>(windowSize));
            groundDistances.reserve(static_cast<size_t>(windowSize));
        }
        size_t skyTargetIndex = 0;
        size_t groundTargetIndex = 0;

        for (int sourceIndex = start; sourceIndex <= end; ++sourceIndex) {
            if (stopIfCancelled()) return;
            std::vector<uint16_t> sourceRgb;
            if (!decodedCache.readFrame(
                    sourceIndex, rgbValueCount, sourceRgb)) {
                m_errorString = "延时邻近帧缓存读取失败";
                return;
            }
            const double distance = sourceIndex - targetIndex;

            if (sourceIndex == targetIndex) {
                skyTargetIndex = skyFrames.size();
                skyFrames.push_back(sourceRgb);
                skyDistances.push_back(0.0);
                if (protectGround) {
                    groundTargetIndex = groundFrames.size();
                    groundFrames.push_back(std::move(sourceRgb));
                    groundDistances.push_back(0.0);
                }
                continue;
            }

            if (protectGround) {
                // Sky and ground need different coordinate systems. Keep an
                // independent source copy so photometric matching for the
                // fixed ground cannot be applied a second time to aligned sky.
                std::vector<uint16_t> groundRgb = sourceRgb;
                normalizeToTarget(groundRgb, sourceIndex);
                groundFrames.push_back(std::move(groundRgb));
                groundDistances.push_back(distance);
            }

            const auto& targetTransforms =
                transforms[static_cast<size_t>(targetIndex)];
            const auto transformIt = std::find_if(
                targetTransforms.begin(), targetTransforms.end(),
                [sourceIndex](const NeighborTransform& item) {
                    return item.sourceIndex == sourceIndex;
                });
            if (transformIt == targetTransforms.end()) continue;

            std::vector<uint16_t> aligned;
            if (!aligner.applyTransformRgb(
                    sourceRgb, width, height,
                    transformIt->transform, aligned)) {
                continue;
            }
            if (stopIfCancelled()) return;
            normalizeToTarget(aligned, sourceIndex);
            skyFrames.push_back(std::move(aligned));
            skyDistances.push_back(distance);
        }

        auto makeViews = [](const std::vector<std::vector<uint16_t>>& frames,
                            const std::vector<double>& distances) {
            std::vector<TimelapseEngine::FrameView> views;
            views.reserve(frames.size());
            for (size_t index = 0; index < frames.size(); ++index) {
                views.push_back({frames[index].data(), frames[index].size(),
                                 distances[index]});
            }
            return views;
        };
        const auto skyViews = makeViews(skyFrames, skyDistances);
        TimelapseEngine::Result skyResult = TimelapseEngine::denoise(
            skyViews, width, height, skyTargetIndex, temporalOptions);
        if (stopIfCancelled()) return;
        if (!skyResult) {
            m_errorString = QString("延时天空降噪失败: %1")
                .arg(TimelapseEngine::errorMessage(skyResult.error));
            return;
        }
        m_timelapseMotionProtectedPixelEvaluations +=
            skyResult.motionProtectedPixels;
        std::vector<uint16_t> resultRgb = std::move(skyResult.rgb);

        if (protectGround) {
            const auto groundViews = makeViews(groundFrames, groundDistances);
            TimelapseEngine::Result groundResult = TimelapseEngine::denoise(
                groundViews, width, height, groundTargetIndex, temporalOptions);
            if (stopIfCancelled()) return;
            if (!groundResult || !ImageBufferUtils::blendSkyGroundInPlace(
                    resultRgb, groundResult.rgb, skyMask, width, height)) {
                m_errorString = "延时天地分离融合失败";
                return;
            }
            m_timelapseMotionProtectedPixelEvaluations +=
                groundResult.motionProtectedPixels;
        }

        PhotometricModel flickerCorrection;
        if (m_params.photometricNormalizationEnabled &&
            TemporalPhotometricSmoother::correctionForFrame(
                sequencePhotometry, static_cast<size_t>(targetIndex),
                flickerOptions, flickerCorrection)) {
            double maximumOffset = 0.0;
            for (double offset : flickerCorrection.offsets) {
                maximumOffset = std::max(maximumOffset, std::abs(offset));
            }
            const double gainChange = std::abs(flickerCorrection.gain - 1.0);
            if ((gainChange > 1e-5 || maximumOffset > 0.5) &&
                PhotometricNormalizer::applyInPlace(
                    resultRgb, width, height, flickerCorrection)) {
                ++m_timelapseFlickerCorrectedFrames;
                m_timelapseMaximumFlickerGainChange = std::max(
                    m_timelapseMaximumFlickerGainChange, gainChange);
                m_timelapseMaximumFlickerOffset = std::max(
                    m_timelapseMaximumFlickerOffset, maximumOffset);
            }
        }

        if (targetIndex == m_selectedReferenceIndex) {
            const PreviewImage8 previewBefore =
                PreviewToneMapper::mapRgb16WithRange(
                    targetRgb, width, height, 0, 65535, 2400);
            if (!previewBefore.rgb.empty()) {
                const QImage borrowed(
                    previewBefore.rgb.data(), previewBefore.width,
                    previewBefore.height, previewBefore.width * 3,
                    QImage::Format_RGB888);
                m_beforePreview = borrowed.copy();
                m_beforePreviewBlackPoint = previewBefore.blackPoint;
                m_beforePreviewWhitePoint = previewBefore.whitePoint;
            }
            m_stackedData = resultRgb;
        }

        const bool png = m_params.outputFormat == "png8";
        const QString extension = png ? ".png" : ".tiff";
        const QString baseName = QFileInfo(m_files[targetIndex]).completeBaseName();
        const QString outputName = QString("%1_%2_denoised%3")
            .arg(targetIndex + 1, 4, 10, QLatin1Char('0'))
            .arg(baseName, extension);
        const QString outputPath = QDir(outputDirectory).filePath(outputName);
        emit stageMessage(QString("输出第 %1/%2 张...")
                              .arg(targetIndex + 1).arg(m_files.size()));
        if (stopIfCancelled()) return;
        if (!ImageExporter::exportRgb16(
                resultRgb, width, height, outputPath,
                png ? ImageExporter::Png8 : ImageExporter::Tiff16,
                [this]() { return stopIfCancelled(); })) {
            if (m_wasCancelled) return;
            m_errorString = QString("延时序列导出失败: %1").arg(outputName);
            return;
        }
        ++writtenFrames;
        m_frameCount = writtenFrames;
        if (stopIfCancelled()) return;
        emit progress(30 + static_cast<int>(
            writtenFrames * 70.0 / m_files.size()));
    }

    if (m_params.photometricNormalizationEnabled) {
        m_photometricSkippedFrameCount = static_cast<int>(std::count(
            normalizationStates.begin(), normalizationStates.end(),
            NormalizationState::Failed));
    }
    if (stopIfCancelled()) return;
    emit progress(100);
    emit stageMessage(QString("延时序列处理完成：已输出 %1 张").arg(writtenFrames));
}

void ProcessingWorker::runSingleFrame() {
    m_params.skyGroundSepEnabled = false;
    const QString sourcePath = m_files.first();
    RawImageLoader loader;
    RawImageLoader::Metadata metadata;

    emit stageMessage("检查单张 RAW 与内存预算...");
    if (!loader.loadMetadata(sourcePath, metadata)) {
        m_errorString = QString("无法读取元数据: %1")
                            .arg(QFileInfo(sourcePath).fileName());
        return;
    }

    ProcessingMemoryEstimator::EstimateOptions options;
    options.frameCount = 1;
    options.noiseReduction = m_params.noiseReductionEnabled;
    options.modifiedCameraColor = m_params.modifiedCameraColorEnabled;
    options.dehaze = m_params.dewarpEnabled;
    options.stretch = m_params.stretchEnabled;
    options.basicAdjustments =
        m_params.basicAdjustments.hasToneOrColorAdjustments();
    options.sharpening = m_params.basicAdjustments.hasSharpening();
    options.starReduction = m_params.starReduceEnabled;

    auto fitsMemoryBudget = [this, &options](int width, int height) {
        const uint64_t estimated = ProcessingMemoryEstimator::estimatePeakBytes(
            width, height, options);
        const ProcessingMemoryEstimator::SystemMemoryInfo memoryInfo =
            ProcessingMemoryEstimator::systemMemoryInfo();
        const uint64_t budget =
            ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
                memoryInfo.safeBudgetBytes, m_params.memoryBudgetBytes);
        if (estimated != 0 && estimated <= budget) return true;
        m_errorString = QString("单张处理预计需要约 %1 内存，当前安全预算为 %2。"
                                "请关闭去雾、降噪或缩星后重试。")
                            .arg(formatMemoryBytes(estimated))
                            .arg(formatMemoryBytes(budget));
        return false;
    };

    if (!fitsMemoryBudget(metadata.width, metadata.height)) return;
    if (stopIfCancelled()) return;

    emit stageMessage("解码单张 RAW...");
    RawImageLoader::ImageData image;
    if (!loader.loadRaw(sourcePath, image)) {
        m_errorString = QString("无法解码 RAW: %1")
                            .arg(QFileInfo(sourcePath).fileName());
        return;
    }
    if ((image.width != metadata.width || image.height != metadata.height) &&
        !fitsMemoryBudget(image.width, image.height)) {
        return;
    }

    m_width = image.width;
    m_height = image.height;
    m_frameCount = 1;
    m_selectedReferenceIndex = 0;
    m_selectedReferenceFrame = sourcePath;
    emit progress(45);

    std::vector<uint16_t> resultRgb = std::move(image.data);
    std::vector<uint8_t> noMask;
    if (!finishResult(resultRgb, m_width, m_height, noMask)) return;
}

bool ProcessingWorker::finishResult(std::vector<uint16_t>& resultRgb,
                                    int width, int height,
                                    std::vector<uint8_t>& mask) {
    const bool usesSkyGroundMask = m_params.skyGroundSepEnabled ||
        (m_params.starTrailMode && m_params.starTrailProtectGround);
    constexpr int kQuickPreviewLongSide = 2400;
    if (!ImageBufferUtils::resizeRgb16ToLongSide(
            resultRgb, width, height, kQuickPreviewLongSide,
            m_quickPreviewSource,
            m_quickPreviewWidth, m_quickPreviewHeight)) {
        qWarning() << "无法建立快速预览 RGB 缓存";
        m_quickPreviewSource.clear();
        m_quickPreviewWidth = 0;
        m_quickPreviewHeight = 0;
    } else if (usesSkyGroundMask &&
               !ImageBufferUtils::resizeMask8(
                   mask, width, height,
                   m_quickPreviewWidth, m_quickPreviewHeight,
                   m_quickPreviewMask)) {
        qWarning() << "无法建立快速预览天地蒙版缓存";
        m_quickPreviewSource.clear();
        m_quickPreviewMask.clear();
        m_quickPreviewWidth = 0;
        m_quickPreviewHeight = 0;
    }

    const bool hasFinishingStage =
        (m_params.noiseReductionEnabled && m_params.noiseReductionStrength > 0)
        || m_params.modifiedCameraColorEnabled
        || m_params.dewarpEnabled
        || m_params.stretchEnabled
        || !m_params.basicAdjustments.isNeutral()
        || (usesSkyGroundMask && m_params.groundDetailStrength > 0)
        || (m_params.starReduceEnabled && m_params.starReduceStrength > 0);
    if (hasFinishingStage) {
        constexpr int kComparisonLongSide = 2400;
        // Before/after comparison must use one fixed transfer function. If each
        // side chooses its own percentile range, contrast and black-point
        // changes introduced by finishing would be visually hidden.
        const PreviewImage8 preview = PreviewToneMapper::mapRgb16WithRange(
            resultRgb, width, height, 0, 65535, kComparisonLongSide);
        if (!preview.rgb.empty()) {
            const QImage borrowed(preview.rgb.data(), preview.width,
                                  preview.height, preview.width * 3,
                                  QImage::Format_RGB888);
            m_beforePreview = borrowed.copy();
            m_beforePreviewBlackPoint = preview.blackPoint;
            m_beforePreviewWhitePoint = preview.whitePoint;
        }
    }

    FinishingOptions finishingOptions;
    finishingOptions.noiseReductionEnabled =
        m_params.noiseReductionEnabled;
    finishingOptions.noiseReductionStrength =
        m_params.noiseReductionStrength;
    finishingOptions.modifiedCameraColorEnabled =
        m_params.modifiedCameraColorEnabled;
    finishingOptions.modifiedCameraColor = m_params.modifiedCameraColor;
    finishingOptions.dehazeEnabled = m_params.dewarpEnabled;
    finishingOptions.dehazeStrength = m_params.dewarpStrength;
    finishingOptions.stretchEnabled = m_params.stretchEnabled;
    finishingOptions.basicAdjustments = m_params.basicAdjustments;
    finishingOptions.skyGroundSeparation = usesSkyGroundMask;
    finishingOptions.groundDetailStrength = m_params.groundDetailStrength;
    finishingOptions.starReductionEnabled = m_params.starReduceEnabled;
    finishingOptions.starReductionStrength = m_params.starReduceStrength;

    FinishingResult finishingResult;
    const bool finished = FinishingPipeline::process(
        resultRgb, width, height,
        usesSkyGroundMask ? &mask : nullptr,
        finishingOptions, finishingResult,
        [this](FinishingStage stage) {
            switch (stage) {
            case FinishingStage::ModifiedCameraColor:
                emit stageMessage("改机色彩还原...");
                emit progress(82);
                break;
            case FinishingStage::NoiseReduction:
                emit stageMessage("多尺度降噪...");
                emit progress(85);
                break;
            case FinishingStage::Dehaze:
            case FinishingStage::Stretch:
                emit stageMessage("自动优化...");
                emit progress(90);
                break;
            case FinishingStage::BasicAdjustments:
                emit stageMessage("基础调色...");
                emit progress(92);
                break;
            case FinishingStage::GroundDetail:
                emit stageMessage("恢复地景细节...");
                break;
            case FinishingStage::Sharpening:
                emit stageMessage("亮度锐化...");
                emit progress(94);
                break;
            case FinishingStage::StarReduction:
                emit stageMessage("缩星处理...");
                emit progress(95);
                break;
            }
        },
        [this]() { return stopIfCancelled(); });
    if (!finished) {
        if (finishingResult.cancelled || m_wasCancelled) return false;
        m_errorString = QString::fromUtf8("收尾处理失败：%1")
            .arg(QString::fromStdString(finishingResult.error));
        return false;
    }
    m_modifiedCameraColorStats = finishingResult.modifiedCameraColorStats;
    m_starReductionStats = finishingResult.starReductionStats;
    if (m_params.modifiedCameraColorEnabled &&
        m_params.modifiedCameraColor.strength > 0) {
        emit stageMessage(QString(
            "改机色彩已还原：%1 · %2% · RGB 增益 %3 / %4 / %5")
            .arg(m_modifiedCameraColorStats.usedManualPoint
                     ? QString::fromUtf8("手动灰点")
                     : QString::fromUtf8("自动灰点"))
            .arg(m_params.modifiedCameraColor.strength)
            .arg(m_modifiedCameraColorStats.gains[0], 0, 'f', 3)
            .arg(m_modifiedCameraColorStats.gains[1], 0, 'f', 3)
            .arg(m_modifiedCameraColorStats.gains[2], 0, 'f', 3));
    }
    if (m_params.starReduceEnabled && m_params.starReduceStrength > 0) {
        emit stageMessage(QString(
            "缩星完成：处理 %1 颗星，其中 %2 颗弱小星被清除或显著缩小，%3 个星缘像素去色边")
            .arg(m_starReductionStats.processedStars)
            .arg(m_starReductionStats.stronglySuppressedStars)
            .arg(m_starReductionStats.defringedPixels));
    }

    m_stackedData = std::move(resultRgb);
    if (stopIfCancelled()) return false;

    emit stageMessage("导出结果...");
    QString outputPath = m_params.outputPath;
    if (outputPath.isEmpty()) outputPath = QDir::homePath() + "/StarProcessor/Output";
    QDir().mkpath(outputPath);
    const bool png = m_params.outputFormat == "png8";
    const QString extension = png ? ".png" : ".tiff";
    const QString outputSuffix = m_params.singleFrameMode
        ? "_single"
        : m_params.starTrailMode ? "_star_trail" : "_stacked";
    m_outputFile = outputPath + "/" +
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") +
        outputSuffix + extension;
    if (!ImageExporter::exportRgb16(m_stackedData, width, height,
                                    m_outputFile,
                                    png ? ImageExporter::Png8 : ImageExporter::Tiff16,
                                    [this]() { return stopIfCancelled(); })) {
        m_outputFile.clear();
        if (m_wasCancelled) return false;
        m_errorString = "导出失败";
        return false;
    }
    emit progress(100);
    emit stageMessage("处理完成");
    return true;
}
