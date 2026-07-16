#include "ProcessingWorker.h"

#include "core/AutoOptimizeEngine.h"
#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/RawImageLoader.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarReducer.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <utility>

namespace {

struct LoadedFrame {
    QString path;
    int width = 0;
    int height = 0;
    std::vector<uint16_t> rgb;
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

bool stackRgb(StackingEngine& stacker,
              const std::vector<std::vector<uint16_t>>& rgbFrames,
              int width, int height, StackingEngine::Method method, double kappa,
              std::vector<uint16_t>& output) {
    if (rgbFrames.empty()) return false;
    std::vector<std::vector<uint16_t>> red;
    std::vector<std::vector<uint16_t>> green;
    std::vector<std::vector<uint16_t>> blue;
    if (!splitFrames(rgbFrames, width, height, red, green, blue)) return false;

    std::vector<uint16_t> redResult;
    std::vector<uint16_t> greenResult;
    std::vector<uint16_t> blueResult;
    // ImageAligner pads out-of-frame pixels with zero. Only this aligned path
    // treats zero as invalid; ordinary stack calls preserve real black pixels.
    if (!stacker.stack(red, width, height, method, kappa, redResult, true) ||
        !stacker.stack(green, width, height, method, kappa, greenResult, true) ||
        !stacker.stack(blue, width, height, method, kappa, blueResult, true)) {
        return false;
    }
    return mergeChannels(std::move(redResult), std::move(greenResult),
                         std::move(blueResult), width, height, output);
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
    m_wasCancelled = false;
    emit progress(0);

    emit stageMessage("加载 RAW 文件...");
    std::vector<LoadedFrame> frames;
    frames.reserve(static_cast<size_t>(m_files.size()));
    RawImageLoader loader;

    for (int i = 0; i < m_files.size(); ++i) {
        if (stopIfCancelled()) return;
        RawImageLoader::ImageData image;
        if (!loader.loadRaw(m_files[i].toStdString(), image)) {
            m_errorString = QString("无法加载: %1").arg(QFileInfo(m_files[i]).fileName());
            return;
        }
        LoadedFrame frame;
        frame.path = m_files[i];
        frame.width = image.width;
        frame.height = image.height;
        frame.rgb = std::move(image.data);
        frames.push_back(std::move(frame));
        emit progress(static_cast<int>((i + 1) * 10.0 / m_files.size()));
    }

    if (frames.empty()) {
        m_errorString = "没有成功加载任何图像";
        return;
    }

    size_t referenceIndex = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].path == m_referenceFrame) {
            referenceIndex = i;
            break;
        }
    }

    const int width = frames[referenceIndex].width;
    const int height = frames[referenceIndex].height;
    m_width = width;
    m_height = height;

    std::vector<uint16_t> referenceLuminance;
    if (!ImageBufferUtils::extractLuminance(frames[referenceIndex].rgb, width, height,
                                            referenceLuminance)) {
        m_errorString = "参考帧 RGB 数据无效";
        return;
    }

    emit stageMessage("参考帧星点检测...");
    StarDetector detector;
    DetectionOptions alignmentOptions;
    alignmentOptions.spatiallyBalanced = true;
    alignmentOptions.maxCandidates = 30;
    alignmentOptions.maxStars = 30;
    std::vector<StarPoint> referenceStars;
    if (!detector.detect(referenceLuminance, width, height,
                         referenceStars, alignmentOptions)) {
        m_errorString = "参考帧星点检测失败";
        return;
    }
    emit progress(20);

    emit stageMessage("对齐图像...");
    ImageAligner aligner;
    std::vector<std::vector<uint16_t>> alignedRgb;
    std::vector<std::vector<uint16_t>> originalRgb;
    alignedRgb.reserve(frames.size());
    if (m_params.skyGroundSepEnabled) originalRgb.reserve(frames.size());

    if (m_params.skyGroundSepEnabled) {
        originalRgb.push_back(std::move(frames[referenceIndex].rgb));
        alignedRgb.push_back(originalRgb.back());
    } else {
        alignedRgb.push_back(std::move(frames[referenceIndex].rgb));
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        if (stopIfCancelled()) return;
        if (i == referenceIndex) continue;
        if (frames[i].width != width || frames[i].height != height) {
            m_errorString = QString("图像尺寸不匹配: %1")
                                .arg(QFileInfo(frames[i].path).fileName());
            return;
        }

        std::vector<uint16_t> sourceLuminance;
        if (!ImageBufferUtils::extractLuminance(frames[i].rgb, width, height,
                                                sourceLuminance)) {
            m_errorString = QString("RGB 数据无效: %1")
                                .arg(QFileInfo(frames[i].path).fileName());
            return;
        }
        std::vector<StarPoint> sourceStars;
        if (!detector.detect(sourceLuminance, width, height,
                             sourceStars, alignmentOptions)) {
            qWarning() << "星点检测失败，跳过:" << frames[i].path;
            continue;
        }

        AlignmentTransform transform;
        if (!aligner.align(referenceStars, sourceStars, transform)) {
            qWarning() << "对齐失败，跳过:" << frames[i].path;
            continue;
        }

        ImageBufferUtils::RgbChannels sourceChannels;
        if (!ImageBufferUtils::splitRgb(frames[i].rgb, width, height, sourceChannels)) {
            m_errorString = QString("RGB 数据无效: %1")
                                .arg(QFileInfo(frames[i].path).fileName());
            return;
        }
        ImageBufferUtils::RgbChannels alignedChannels;
        if (!aligner.applyTransform(sourceChannels.red, width, height, transform,
                                    alignedChannels.red) ||
            !aligner.applyTransform(sourceChannels.green, width, height, transform,
                                    alignedChannels.green) ||
            !aligner.applyTransform(sourceChannels.blue, width, height, transform,
                                    alignedChannels.blue)) {
            qWarning() << "变换应用失败，跳过:" << frames[i].path;
            continue;
        }

        std::vector<uint16_t> alignedFrame;
        if (!ImageBufferUtils::mergeRgb(alignedChannels, width, height, alignedFrame)) {
            m_errorString = "对齐后 RGB 数据无效";
            return;
        }
        alignedRgb.push_back(std::move(alignedFrame));
        if (m_params.skyGroundSepEnabled) {
            originalRgb.push_back(std::move(frames[i].rgb));
        } else {
            frames[i].rgb.clear();
            frames[i].rgb.shrink_to_fit();
        }
        emit progress(20 + static_cast<int>((i + 1) * 30.0 / frames.size()));
    }

    if (alignedRgb.size() < 2) {
        m_errorString = "对齐后可用帧数不足（<2），无法堆栈";
        return;
    }
    emit progress(55);
    if (stopIfCancelled()) return;

    emit stageMessage("堆栈中...");
    StackingEngine stacker;
    const StackingEngine::Method method = stackMethodFromName(m_params.stackMethod);
    std::vector<uint16_t> resultRgb;
    if (m_params.skyGroundSepEnabled) {
        emit stageMessage("生成天地蒙版...");
        std::vector<uint8_t> mask;
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
        if (!stackRgbWithMask(stacker, alignedRgb, originalRgb, width, height,
                              method, m_params.kappaValue, mask, resultRgb)) {
            m_errorString = "天地分离堆栈失败";
            return;
        }
    } else if (!stackRgb(stacker, alignedRgb, width, height, method,
                         m_params.kappaValue, resultRgb)) {
        m_errorString = "堆栈失败";
        return;
    }
    m_frameCount = static_cast<int>(alignedRgb.size());
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
    const QString outputFile = outputPath + "/" +
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_stacked" + extension;
    if (!ImageExporter::exportRgb16(m_stackedData, width, height,
                                    outputFile.toStdString(),
                                    png ? ImageExporter::Png8 : ImageExporter::Tiff16)) {
        m_errorString = "导出失败";
        return;
    }
    emit progress(100);
    emit stageMessage("处理完成");
}
