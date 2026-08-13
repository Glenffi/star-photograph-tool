#include "SkyGroundMask.h"
#include <QImage>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

// ---------------------------------------------------------------------------
// 图像缩放（双线性插值）
// ---------------------------------------------------------------------------
static void resizeImage(const std::vector<uint16_t>& src, int srcW, int srcH,
                        std::vector<uint16_t>& dst, int dstW, int dstH)
{
    dst.resize(dstW * dstH);
    float scaleX = static_cast<float>(srcW) / dstW;
    float scaleY = static_cast<float>(srcH) / dstH;

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            float sx = (x + 0.5f) * scaleX - 0.5f;
            float sy = (y + 0.5f) * scaleY - 0.5f;
            int x0 = static_cast<int>(std::floor(sx));
            int y0 = static_cast<int>(std::floor(sy));
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            float fx = sx - x0;
            float fy = sy - y0;

            x0 = std::clamp(x0, 0, srcW - 1);
            x1 = std::clamp(x1, 0, srcW - 1);
            y0 = std::clamp(y0, 0, srcH - 1);
            y1 = std::clamp(y1, 0, srcH - 1);

            float v00 = static_cast<float>(src[y0 * srcW + x0]);
            float v10 = static_cast<float>(src[y0 * srcW + x1]);
            float v01 = static_cast<float>(src[y1 * srcW + x0]);
            float v11 = static_cast<float>(src[y1 * srcW + x1]);

            float v0 = v00 + fx * (v10 - v00);
            float v1 = v01 + fx * (v11 - v01);
            float val = v0 + fy * (v1 - v0);

            dst[y * dstW + x] = static_cast<uint16_t>(std::clamp(
                static_cast<int>(val + 0.5f), 0, 65535));
        }
    }
}

static void resizeMask(const std::vector<uint8_t>& src, int srcW, int srcH,
                       std::vector<uint8_t>& dst, int dstW, int dstH)
{
    dst.resize(dstW * dstH);
    float scaleX = static_cast<float>(srcW) / dstW;
    float scaleY = static_cast<float>(srcH) / dstH;

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            float sx = (x + 0.5f) * scaleX - 0.5f;
            float sy = (y + 0.5f) * scaleY - 0.5f;
            int x0 = static_cast<int>(std::floor(sx));
            int y0 = static_cast<int>(std::floor(sy));
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            float fx = sx - x0;
            float fy = sy - y0;

            x0 = std::clamp(x0, 0, srcW - 1);
            x1 = std::clamp(x1, 0, srcW - 1);
            y0 = std::clamp(y0, 0, srcH - 1);
            y1 = std::clamp(y1, 0, srcH - 1);

            float v00 = static_cast<float>(src[y0 * srcW + x0]);
            float v10 = static_cast<float>(src[y0 * srcW + x1]);
            float v01 = static_cast<float>(src[y1 * srcW + x0]);
            float v11 = static_cast<float>(src[y1 * srcW + x1]);

            float v0 = v00 + fx * (v10 - v00);
            float v1 = v01 + fx * (v11 - v01);
            float val = v0 + fy * (v1 - v0);

            dst[y * dstW + x] = static_cast<uint8_t>(std::clamp(
                static_cast<int>(val + 0.5f), 0, 255));
        }
    }
}

static void resizeMaskNearest(const std::vector<uint8_t>& src,
                              int srcW, int srcH,
                              std::vector<uint8_t>& dst,
                              int dstW, int dstH) {
    dst.resize(static_cast<size_t>(dstW) * dstH);
    for (int y = 0; y < dstH; ++y) {
        const int sourceY = std::min(
            srcH - 1, static_cast<int>(static_cast<int64_t>(y) * srcH / dstH));
        for (int x = 0; x < dstW; ++x) {
            const int sourceX = std::min(
                srcW - 1,
                static_cast<int>(static_cast<int64_t>(x) * srcW / dstW));
            dst[static_cast<size_t>(y) * dstW + x] =
                src[static_cast<size_t>(sourceY) * srcW + sourceX];
        }
    }
}

// ---------------------------------------------------------------------------
// 高斯模糊（1D 可分离，用于 8-bit 蒙版）
// ---------------------------------------------------------------------------
static void gaussianBlurMask(const std::vector<uint8_t>& src, int w, int h,
                             std::vector<uint8_t>& dst, float sigma)
{
    int kernelSize = static_cast<int>(std::ceil(sigma * 6.0f));
    if (kernelSize % 2 == 0) kernelSize++;
    int half = kernelSize / 2;

    std::vector<float> kernel(kernelSize);
    float sum = 0.0f;
    for (int i = 0; i < kernelSize; ++i) {
        float x = static_cast<float>(i - half);
        kernel[i] = std::exp(-(x * x) / (2.0f * sigma * sigma));
        sum += kernel[i];
    }
    for (float& k : kernel) k /= sum;

    std::vector<float> temp(w * h);
    dst.resize(w * h);

    // 行方向
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int k = 0; k < kernelSize; ++k) {
                int px = x + k - half;
                px = std::clamp(px, 0, w - 1);
                acc += static_cast<float>(src[y * w + px]) * kernel[k];
            }
            temp[y * w + x] = acc;
        }
    }

    // 列方向
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int k = 0; k < kernelSize; ++k) {
                int py = y + k - half;
                py = std::clamp(py, 0, h - 1);
                acc += temp[py * w + x] * kernel[k];
            }
            dst[y * w + x] = static_cast<uint8_t>(std::clamp(
                static_cast<int>(acc + 0.5f), 0, 255));
        }
    }
}

static float percentile(std::vector<float> values, float fraction)
{
    if (values.empty()) return 0.0f;
    const size_t index = std::min(
        values.size() - 1,
        static_cast<size_t>(std::round(fraction * (values.size() - 1))));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

static void gaussianBlurLuminance(const std::vector<uint16_t>& src, int w, int h,
                                  std::vector<float>& dst, float sigma)
{
    int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
    const int kernelSize = radius * 2 + 1;
    std::vector<float> kernel(kernelSize);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float value = std::exp(-(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = value;
        sum += value;
    }
    for (float& value : kernel) value /= sum;

    std::vector<float> temp(static_cast<size_t>(w) * h);
    dst.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float value = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int px = std::clamp(x + k, 0, w - 1);
                value += src[static_cast<size_t>(y) * w + px] * kernel[k + radius];
            }
            temp[static_cast<size_t>(y) * w + x] = value;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float value = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int py = std::clamp(y + k, 0, h - 1);
                value += temp[static_cast<size_t>(py) * w + x] * kernel[k + radius];
            }
            dst[static_cast<size_t>(y) * w + x] = value;
        }
    }
}

static bool findHorizon(const std::vector<uint16_t>& image, int w, int h,
                        std::vector<int>& horizon)
{
    if (w < 32 || h < 32) return false;

    std::vector<float> smooth;
    const float sigma = std::max(1.0f, 2.5f * std::max(w, h) / 640.0f);
    gaussianBlurLuminance(image, w, h, smooth, sigma);

    const int span = std::max(2, h / 140);
    const int band = std::max(6, h / 36);
    const int firstY = std::max(band + span,
                                static_cast<int>(std::round(h * 0.25)));
    const int lastY = std::min(h - band - span - 1,
                              static_cast<int>(std::round(h * 0.92)));
    if (firstY >= lastY) return false;

    // Column integrals make the above/below band contrast inexpensive. Point
    // stars survive as isolated edges, while a true horizon stays coherent
    // across many neighboring columns.
    std::vector<double> columnIntegral(static_cast<size_t>(h + 1) * w, 0.0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            columnIntegral[static_cast<size_t>(y + 1) * w + x] =
                columnIntegral[static_cast<size_t>(y) * w + x] +
                smooth[static_cast<size_t>(y) * w + x];
        }
    }

    std::vector<float> evidence(static_cast<size_t>(h) * w, 0.0f);
    std::vector<float> candidateEvidence;
    candidateEvidence.reserve(static_cast<size_t>(lastY - firstY + 1) * w);
    for (int y = firstY; y <= lastY; ++y) {
        for (int x = 0; x < w; ++x) {
            const float verticalEdge = std::abs(
                smooth[static_cast<size_t>(y + span) * w + x] -
                smooth[static_cast<size_t>(y - span) * w + x]);
            const double above =
                (columnIntegral[static_cast<size_t>(y) * w + x] -
                 columnIntegral[static_cast<size_t>(y - band) * w + x]) / band;
            const double below =
                (columnIntegral[static_cast<size_t>(y + band) * w + x] -
                 columnIntegral[static_cast<size_t>(y) * w + x]) / band;
            const float value = 0.7f * verticalEdge +
                                0.3f * static_cast<float>(std::abs(below - above));
            evidence[static_cast<size_t>(y) * w + x] = value;
            candidateEvidence.push_back(value);
        }
    }

    std::vector<float> luminanceSamples(smooth.begin(), smooth.end());
    const float luminanceP95 = percentile(luminanceSamples, 0.95f);
    const float luminanceP05 = percentile(luminanceSamples, 0.05f);
    const float luminanceRange = luminanceP95 - luminanceP05;
    const float evidenceScale = percentile(candidateEvidence, 0.95f);
    if (luminanceRange < 32.0f ||
        evidenceScale < std::max(8.0f, luminanceRange * 0.005f)) {
        return false;
    }

    const int candidateRows = lastY - firstY + 1;
    std::vector<float> previous(candidateRows);
    std::vector<float> current(candidateRows);
    std::vector<uint16_t> parent(static_cast<size_t>(candidateRows) * w);
    for (int row = 0; row < candidateRows; ++row) {
        const int y = firstY + row;
        const float normalized = std::min(
            3.0f, evidence[static_cast<size_t>(y) * w] / evidenceScale);
        previous[row] = -normalized +
            0.15f * std::abs(y - h * 0.65f) / h;
    }

    const int maxStep = std::clamp(static_cast<int>(std::round(w / 213.0)), 2, 4);
    for (int x = 1; x < w; ++x) {
        for (int row = 0; row < candidateRows; ++row) {
            float bestCost = std::numeric_limits<float>::max();
            int bestParent = row;
            const int parentStart = std::max(0, row - maxStep);
            const int parentEnd = std::min(candidateRows - 1, row + maxStep);
            for (int candidate = parentStart; candidate <= parentEnd; ++candidate) {
                const float delta = static_cast<float>(std::abs(candidate - row));
                const float cost = previous[candidate] +
                                   0.04f * delta + 0.012f * delta * delta;
                if (cost < bestCost) {
                    bestCost = cost;
                    bestParent = candidate;
                }
            }
            const int y = firstY + row;
            const float normalized = std::min(
                3.0f, evidence[static_cast<size_t>(y) * w + x] / evidenceScale);
            current[row] = bestCost - normalized;
            parent[static_cast<size_t>(x) * candidateRows + row] =
                static_cast<uint16_t>(bestParent);
        }
        previous.swap(current);
    }

    int row = static_cast<int>(std::min_element(previous.begin(), previous.end()) -
                               previous.begin());
    horizon.assign(w, firstY + row);
    for (int x = w - 1; x > 0; --x) {
        row = parent[static_cast<size_t>(x) * candidateRows + row];
        horizon[x - 1] = firstY + row;
    }

    const int medianRadius = std::max(2, w / 100);
    std::vector<int> smoothedHorizon(w);
    std::vector<int> window;
    window.reserve(medianRadius * 2 + 1);
    for (int x = 0; x < w; ++x) {
        window.clear();
        for (int sampleX = std::max(0, x - medianRadius);
             sampleX <= std::min(w - 1, x + medianRadius); ++sampleX) {
            window.push_back(horizon[sampleX]);
        }
        const size_t middle = window.size() / 2;
        std::nth_element(window.begin(), window.begin() + middle, window.end());
        smoothedHorizon[x] = window[middle];
    }
    horizon = std::move(smoothedHorizon);
    return true;
}

// ---------------------------------------------------------------------------
// 6. 羽化
// ---------------------------------------------------------------------------
void SkyGroundMask::feather(std::vector<uint8_t>& mask, int width, int height,
                            int featherRadius)
{
    if (featherRadius <= 0) return;
    std::vector<uint8_t> blurred;
    gaussianBlurMask(mask, width, height, blurred, static_cast<float>(featherRadius));
    mask = std::move(blurred);
}

// ---------------------------------------------------------------------------
// 7. 自动检测
// ---------------------------------------------------------------------------
bool SkyGroundMask::autoDetect(const std::vector<uint16_t>& image, int width, int height,
                               std::vector<uint8_t>& mask, int featherRadius)
{
    if (image.empty() || width <= 0 || height <= 0)
        return false;
    if (static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() / static_cast<size_t>(height) ||
        image.size() != static_cast<size_t>(width) * height)
        return false;

    constexpr int kAnalysisLongSide = 640;
    const double analysisScale = std::min(
        1.0, static_cast<double>(kAnalysisLongSide) / std::max(width, height));
    const int smallW = std::max(1, static_cast<int>(std::round(width * analysisScale)));
    const int smallH = std::max(1, static_cast<int>(std::round(height * analysisScale)));

    std::vector<uint16_t> smallImage;
    resizeImage(image, width, height, smallImage, smallW, smallH);

    std::vector<int> horizon;
    if (!findHorizon(smallImage, smallW, smallH, horizon)) return false;
    std::vector<uint8_t> smallMask(static_cast<size_t>(smallW) * smallH, 0);
    for (int x = 0; x < smallW; ++x) {
        for (int y = 0; y < horizon[x]; ++y) {
            smallMask[static_cast<size_t>(y) * smallW + x] = 255;
        }
    }

    // 羽化仍在分析尺寸完成，避免在 40MP 蒙版上做大核卷积。
    if (featherRadius > 0) {
        const int smallFeather = std::max(
            1, static_cast<int>(std::round(featherRadius * analysisScale)));
        std::vector<uint8_t> blurredMask;
        gaussianBlurMask(smallMask, smallW, smallH, blurredMask, static_cast<float>(smallFeather));
        smallMask = std::move(blurredMask);
    }

    std::vector<uint8_t> fullMask;
    resizeMask(smallMask, smallW, smallH, fullMask, width, height);

    mask = std::move(fullMask);
    return true;
}

bool SkyGroundMask::autoDetectPreview(const QImage& preview, int targetWidth,
                                      int targetHeight, std::vector<uint8_t>& mask,
                                      int featherRadius)
{
    if (preview.isNull() || targetWidth <= 0 || targetHeight <= 0) return false;
    const double sourceAspect = static_cast<double>(preview.width()) / preview.height();
    const double targetAspect = static_cast<double>(targetWidth) / targetHeight;
    if (std::abs(sourceAspect - targetAspect) / targetAspect > 0.03) return false;

    const QImage rgb = preview.convertToFormat(QImage::Format_RGB888);
    const int width = rgb.width();
    const int height = rgb.height();
    std::vector<uint16_t> luminance(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const uchar* row = rgb.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const uint32_t red = row[x * 3];
            const uint32_t green = row[x * 3 + 1];
            const uint32_t blue = row[x * 3 + 2];
            luminance[static_cast<size_t>(y) * width + x] =
                static_cast<uint16_t>(
                    ((red * 299 + green * 587 + blue * 114) / 1000) * 257);
        }
    }

    const double scale = static_cast<double>(std::max(width, height)) /
                         std::max(targetWidth, targetHeight);
    const int previewFeather = featherRadius > 0
        ? std::max(1, static_cast<int>(std::round(featherRadius * scale))) : 0;
    std::vector<uint8_t> previewMask;
    if (!autoDetect(luminance, width, height, previewMask, previewFeather)) {
        return false;
    }
    if (width == targetWidth && height == targetHeight) {
        mask = std::move(previewMask);
    } else {
        resizeMask(previewMask, width, height, mask, targetWidth, targetHeight);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 8. 加载用户蒙版（带 downscale 羽化优化）
// ---------------------------------------------------------------------------
bool SkyGroundMask::loadUserMask(const QString& path, int width, int height,
                                 std::vector<uint8_t>& mask, int featherRadius)
{
    QImage img(path);
    if (img.isNull())
        return false;

    int srcW = img.width();
    int srcH = img.height();
    std::vector<uint8_t> srcMask(srcW * srcH);

    // 统一处理：完全透明像素视为地景(0)，其余由灰度决定天空(255)/地景(0)
    for (int y = 0; y < srcH; ++y) {
        for (int x = 0; x < srcW; ++x) {
            QRgb pixel = img.pixel(x, y);
            int gray = qGray(pixel);
            int alpha = qAlpha(pixel);
            // 契约：完全透明像素视为地景(0)，其余由灰度决定天空(255)/地景(0)
            if (alpha == 0) {
                srcMask[y * srcW + x] = 0;
            } else {
                srcMask[y * srcW + x] = (gray > 128) ? 255 : 0;
            }
        }
    }

    std::vector<uint8_t> resizedMask;
    if (srcW == width && srcH == height) {
        resizedMask = std::move(srcMask);
    } else {
        resizeMask(srcMask, srcW, srcH, resizedMask, width, height);
    }

    // 羽化：在小图上做，避免全分辨率高斯模糊的性能开销
    if (featherRadius > 0) {
        int smallW = width / 4;
        int smallH = height / 4;
        if (smallW < 1) smallW = 1;
        if (smallH < 1) smallH = 1;

        std::vector<uint8_t> smallMask;
        resizeMask(resizedMask, width, height, smallMask, smallW, smallH);

        int smallFeather = std::max(1, featherRadius / 4);
        std::vector<uint8_t> blurredMask;
        gaussianBlurMask(smallMask, smallW, smallH, blurredMask, static_cast<float>(smallFeather));
        smallMask = std::move(blurredMask);

        resizeMask(smallMask, smallW, smallH, mask, width, height);
    } else {
        mask = std::move(resizedMask);
    }
    return true;
}

bool SkyGroundMask::prepareEditedMask(
    const std::vector<uint8_t>& source, int sourceWidth, int sourceHeight,
    int targetWidth, int targetHeight, std::vector<uint8_t>& mask,
    int featherRadius) {
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        targetWidth <= 0 || targetHeight <= 0 || featherRadius < 0 ||
        static_cast<size_t>(sourceWidth) >
            std::numeric_limits<size_t>::max() /
                static_cast<size_t>(sourceHeight) ||
        sourceWidth > std::numeric_limits<int>::max() / sourceHeight ||
        static_cast<size_t>(targetWidth) >
            std::numeric_limits<size_t>::max() /
                static_cast<size_t>(targetHeight) ||
        targetWidth > std::numeric_limits<int>::max() / targetHeight ||
        source.size() !=
            static_cast<size_t>(sourceWidth) * sourceHeight) {
        return false;
    }

    if (featherRadius <= 0) {
        if (sourceWidth == targetWidth && sourceHeight == targetHeight) {
            mask = source;
        } else {
            resizeMaskNearest(source, sourceWidth, sourceHeight, mask,
                              targetWidth, targetHeight);
        }
        return true;
    }

    // Feather at one-quarter output dimensions, matching the existing user
    // mask path while avoiding a large-kernel blur over a 40-60 MP mask.
    const int smallWidth = std::max(1, targetWidth / 4);
    const int smallHeight = std::max(1, targetHeight / 4);
    std::vector<uint8_t> smallMask;
    resizeMaskNearest(source, sourceWidth, sourceHeight, smallMask,
                      smallWidth, smallHeight);
    std::vector<uint8_t> blurred;
    gaussianBlurMask(smallMask, smallWidth, smallHeight, blurred,
                     static_cast<float>(std::max(1, featherRadius / 4)));
    resizeMask(blurred, smallWidth, smallHeight, mask,
               targetWidth, targetHeight);
    return true;
}

bool SkyGroundMask::refineWithGroundHints(
    const QImage& preview, const std::vector<uint8_t>& initialMask,
    const std::vector<uint8_t>& groundHints, int width, int height,
    std::vector<uint8_t>& refinedMask) {
    if (preview.isNull() || width <= 1 || height <= 1 ||
        preview.width() != width || preview.height() != height ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() /
                static_cast<size_t>(height) ||
        width > std::numeric_limits<int>::max() / height) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (initialMask.size() != pixelCount || groundHints.size() != pixelCount) {
        return false;
    }
    const auto firstHint = std::find_if(
        groundHints.begin(), groundHints.end(),
        [](uint8_t value) { return value > 0; });
    if (firstHint == groundHints.end()) {
        refinedMask = initialMask;
        return true;
    }

    constexpr int kWorkLongSide = 1100;
    const double scale = std::min(
        1.0, static_cast<double>(kWorkLongSide) / std::max(width, height));
    const int workWidth = std::max(2, qRound(width * scale));
    const int workHeight = std::max(2, qRound(height * scale));
    const QImage workImage = preview.scaled(
        workWidth, workHeight, Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    std::vector<uint8_t> workInitial;
    std::vector<uint8_t> workHints;
    resizeMaskNearest(initialMask, width, height, workInitial,
                      workWidth, workHeight);
    resizeMaskNearest(groundHints, width, height, workHints,
                      workWidth, workHeight);

    int minX = workWidth;
    int minY = workHeight;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < workHeight; ++y) {
        for (int x = 0; x < workWidth; ++x) {
            if (workHints[static_cast<size_t>(y) * workWidth + x] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY) return false;

    // Keep refinement local enough to preserve a good automatic horizon, but
    // large enough for a loose stroke inside a mountain to reach its outline.
    const int hintSpan = std::max(maxX - minX + 1, maxY - minY + 1);
    const int margin = std::clamp(hintSpan * 2 + 40, 56,
                                  std::max(workWidth, workHeight));
    minX = std::max(0, minX - margin);
    minY = std::max(0, minY - margin);
    maxX = std::min(workWidth - 1, maxX + margin);
    maxY = std::min(workHeight - 1, maxY + margin);

    const size_t workCount = static_cast<size_t>(workWidth) * workHeight;
    std::vector<uint32_t> best(workCount,
                               std::numeric_limits<uint32_t>::max());
    std::vector<uint8_t> label(workCount, 2); // 0=ground, 1=sky, 2=unknown
    struct Node {
        uint32_t cost;
        int index;
        uint8_t label;
    };
    const auto compare = [](const Node& a, const Node& b) {
        return a.cost > b.cost;
    };
    std::priority_queue<Node, std::vector<Node>, decltype(compare)> queue(compare);
    const auto seed = [&](int x, int y, uint8_t value) {
        const int index = y * workWidth + x;
        if (best[static_cast<size_t>(index)] == 0 &&
            label[static_cast<size_t>(index)] == 0) {
            return;
        }
        best[static_cast<size_t>(index)] = 0;
        label[static_cast<size_t>(index)] = value;
        queue.push({0, index, value});
    };

    // The top and bottom of the ROI preserve the sky-over-ground topology.
    // Do not seed the vertical edges: a missed ridge can legitimately continue
    // through them, and copying the old label there would pin that same error.
    // User strokes are inserted last and always win as ground.
    for (int x = minX; x <= maxX; ++x) {
        seed(x, minY,
             workInitial[static_cast<size_t>(minY) * workWidth + x] >= 128);
        seed(x, maxY,
             workInitial[static_cast<size_t>(maxY) * workWidth + x] >= 128);
    }
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (workHints[static_cast<size_t>(y) * workWidth + x] > 0) {
                seed(x, y, 0);
            }
        }
    }

    const auto edgeCost = [&](int first, int second) {
        const int firstY = first / workWidth;
        const int firstX = first - firstY * workWidth;
        const int secondY = second / workWidth;
        const int secondX = second - secondY * workWidth;
        const uchar* a = workImage.constScanLine(firstY) + firstX * 3;
        const uchar* b = workImage.constScanLine(secondY) + secondX * 3;
        const int dr = static_cast<int>(a[0]) - b[0];
        const int dg = static_cast<int>(a[1]) - b[1];
        const int db = static_cast<int>(a[2]) - b[2];
        const int lumaA = 3 * a[0] + 6 * a[1] + a[2];
        const int lumaB = 3 * b[0] + 6 * b[1] + b[2];
        const int dl = (lumaA - lumaB) / 10;
        return static_cast<uint32_t>(
            1 + dr * dr + dg * dg + db * db + 2 * dl * dl);
    };
    constexpr int dx[4] = {-1, 1, 0, 0};
    constexpr int dy[4] = {0, 0, -1, 1};
    while (!queue.empty()) {
        const Node node = queue.top();
        queue.pop();
        if (node.cost != best[static_cast<size_t>(node.index)] ||
            node.label != label[static_cast<size_t>(node.index)]) {
            continue;
        }
        const int y = node.index / workWidth;
        const int x = node.index - y * workWidth;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction];
            const int ny = y + dy[direction];
            if (nx < minX || nx > maxX || ny < minY || ny > maxY) continue;
            const int next = ny * workWidth + nx;
            if (workHints[static_cast<size_t>(next)] > 0 && node.label != 0) {
                continue;
            }
            const uint32_t candidate = std::max(
                node.cost, edgeCost(node.index, next));
            const size_t nextIndex = static_cast<size_t>(next);
            const bool better = candidate < best[nextIndex];
            const bool tieFollowsInitial = candidate == best[nextIndex] &&
                node.label ==
                    static_cast<uint8_t>(workInitial[nextIndex] >= 128) &&
                label[nextIndex] != node.label;
            if (!better && !tieFollowsInitial) continue;
            best[nextIndex] = candidate;
            label[nextIndex] = node.label;
            queue.push({candidate, next, node.label});
        }
    }

    std::vector<uint8_t> workRefined(workInitial.size());
    std::transform(workInitial.begin(), workInitial.end(),
                   workRefined.begin(),
                   [](uint8_t value) { return value >= 128 ? 255 : 0; });
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const size_t index = static_cast<size_t>(y) * workWidth + x;
            if (label[index] != 2) workRefined[index] = label[index] ? 255 : 0;
        }
    }
    resizeMaskNearest(workRefined, workWidth, workHeight, refinedMask,
                      width, height);
    return true;
}
