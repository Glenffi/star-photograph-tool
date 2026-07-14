#include "SkyGroundMask.h"
#include "StarDetector.h"
#include <QImage>
#include <algorithm>
#include <cmath>
#include <numeric>

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

// ---------------------------------------------------------------------------
// 1. Sobel 梯度强度
// ---------------------------------------------------------------------------
void SkyGroundMask::computeGradient(const std::vector<uint16_t>& image, int w, int h,
                                      std::vector<float>& gradient)
{
    gradient.resize(w * h, 0.0f);

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            int idx = y * w + x;

            float gx = -1.0f * image[(y - 1) * w + (x - 1)]
                       + 1.0f * image[(y - 1) * w + (x + 1)]
                       - 2.0f * image[y * w + (x - 1)]
                       + 2.0f * image[y * w + (x + 1)]
                       - 1.0f * image[(y + 1) * w + (x - 1)]
                       + 1.0f * image[(y + 1) * w + (x + 1)];

            float gy = -1.0f * image[(y - 1) * w + (x - 1)]
                       - 2.0f * image[(y - 1) * w + x]
                       - 1.0f * image[(y - 1) * w + (x + 1)]
                       + 1.0f * image[(y + 1) * w + (x - 1)]
                       + 2.0f * image[(y + 1) * w + x]
                       + 1.0f * image[(y + 1) * w + (x + 1)];

            gradient[idx] = std::sqrt(gx * gx + gy * gy) / 8.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// 2. 局部亮度方差（50×50 窗口）— 使用积分图加速
// ---------------------------------------------------------------------------
void SkyGroundMask::computeVariance(const std::vector<uint16_t>& image, int w, int h,
                                    std::vector<float>& variance)
{
    variance.resize(w * h, 0.0f);
    const int win = 50;
    const int half = win / 2;

    // Build integral images: sum and sum of squares
    std::vector<double> integral(w * h);
    std::vector<double> integralSq(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double v = static_cast<double>(image[y * w + x]);
            double vSq = v * v;
            double sum = v;
            double sumSq = vSq;
            if (x > 0) { sum += integral[y * w + (x - 1)]; sumSq += integralSq[y * w + (x - 1)]; }
            if (y > 0) { sum += integral[(y - 1) * w + x]; sumSq += integralSq[(y - 1) * w + x]; }
            if (x > 0 && y > 0) { sum -= integral[(y - 1) * w + (x - 1)]; sumSq -= integralSq[(y - 1) * w + (x - 1)]; }
            integral[y * w + x] = sum;
            integralSq[y * w + x] = sumSq;
        }
    }

    // Query window sums using integral image
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int y0 = std::max(0, y - half);
            int y1 = std::min(h - 1, y + half);
            int x0 = std::max(0, x - half);
            int x1 = std::min(w - 1, x + half);

            double sum = integral[y1 * w + x1];
            double sumSq = integralSq[y1 * w + x1];
            if (x0 > 0) { sum -= integral[y1 * w + (x0 - 1)]; sumSq -= integralSq[y1 * w + (x0 - 1)]; }
            if (y0 > 0) { sum -= integral[(y0 - 1) * w + x1]; sumSq -= integralSq[(y0 - 1) * w + x1]; }
            if (x0 > 0 && y0 > 0) { sum += integral[(y0 - 1) * w + (x0 - 1)]; sumSq += integralSq[(y0 - 1) * w + (x0 - 1)]; }

            int count = (y1 - y0 + 1) * (x1 - x0 + 1);
            if (count > 1) {
                double mean = sum / count;
                double meanSq = sumSq / count;
                variance[y * w + x] = static_cast<float>(meanSq - mean * mean);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 3. 星点密度（复用 StarDetector 阈值逻辑，统计 50×50 块）
// ---------------------------------------------------------------------------
void SkyGroundMask::computeStarDensity(const std::vector<uint16_t>& image, int w, int h,
                                       std::vector<float>& density)
{
    density.resize(w * h, 0.0f);

    StarDetector detector;
    std::vector<StarPoint> stars;
    detector.detect(image, w, h, stars, 5.0);

    const int blockSize = 50;
    int blockW = (w + blockSize - 1) / blockSize;
    int blockH = (h + blockSize - 1) / blockSize;
    std::vector<int> blockCount(blockW * blockH, 0);

    for (const auto& star : stars) {
        int bx = static_cast<int>(star.x) / blockSize;
        int by = static_cast<int>(star.y) / blockSize;
        bx = std::clamp(bx, 0, blockW - 1);
        by = std::clamp(by, 0, blockH - 1);
        blockCount[by * blockW + bx]++;
    }

    int maxCount = 0;
    for (int c : blockCount) {
        if (c > maxCount) maxCount = c;
    }
    if (maxCount == 0) maxCount = 1;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int bx = std::min(x / blockSize, blockW - 1);
            int by = std::min(y / blockSize, blockH - 1);
            density[y * w + x] = static_cast<float>(blockCount[by * blockW + bx]) / maxCount;
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Otsu 自动阈值
// ---------------------------------------------------------------------------
uint8_t SkyGroundMask::otsuThreshold(const std::vector<float>& scores)
{
    std::vector<int> hist(256, 0);
    for (float s : scores) {
        int idx = std::clamp(static_cast<int>(s * 255.0f + 0.5f), 0, 255);
        hist[idx]++;
    }

    int total = static_cast<int>(scores.size());
    double sum = 0.0;
    for (int i = 0; i < 256; ++i) sum += i * hist[i];

    double sumB = 0.0;
    int wB = 0;
    double maxVar = 0.0;
    int threshold = 0;

    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0) continue;
        int wF = total - wB;
        if (wF == 0) break;
        sumB += t * hist[t];
        double mB = sumB / wB;
        double mF = (sum - sumB) / wF;
        double var = wB * wF * (mB - mF) * (mB - mF);
        if (var > maxVar) {
            maxVar = var;
            threshold = t;
        }
    }
    return static_cast<uint8_t>(threshold);
}

// ---------------------------------------------------------------------------
// 5. 形态学闭运算（3×3 结构元素）
// ---------------------------------------------------------------------------
static void dilate3x3(const std::vector<uint8_t>& src, int w, int h,
                        std::vector<uint8_t>& dst)
{
    dst.resize(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t maxVal = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int py = y + dy, px = x + dx;
                    if (py >= 0 && py < h && px >= 0 && px < w) {
                        maxVal = std::max(maxVal, src[py * w + px]);
                    }
                }
            }
            dst[y * w + x] = maxVal;
        }
    }
}

static void erode3x3(const std::vector<uint8_t>& src, int w, int h,
                     std::vector<uint8_t>& dst)
{
    dst.resize(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t minVal = 255;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int py = y + dy, px = x + dx;
                    if (py >= 0 && py < h && px >= 0 && px < w) {
                        minVal = std::min(minVal, src[py * w + px]);
                    } else {
                        minVal = 0; // 边界外视为 0
                    }
                }
            }
            dst[y * w + x] = minVal;
        }
    }
}

void SkyGroundMask::morphologicalClose(std::vector<uint8_t>& mask, int w, int h)
{
    std::vector<uint8_t> temp;
    dilate3x3(mask, w, h, temp);
    erode3x3(temp, w, h, mask);
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
    if (static_cast<int>(image.size()) != width * height)
        return false;

    // 1. 缩放至 1/4
    int smallW = width / 4;
    int smallH = height / 4;
    if (smallW < 1) smallW = 1;
    if (smallH < 1) smallH = 1;

    std::vector<uint16_t> smallImage;
    resizeImage(image, width, height, smallImage, smallW, smallH);

    // 2. 计算特征
    std::vector<float> gradient, variance, density;
    computeGradient(smallImage, smallW, smallH, gradient);
    computeVariance(smallImage, smallW, smallH, variance);
    computeStarDensity(smallImage, smallW, smallH, density);

    // 3. 归一化并综合打分
    float maxGrad = 0.0f, maxVar = 0.0f;
    for (float g : gradient) if (g > maxGrad) maxGrad = g;
    for (float v : variance) if (v > maxVar) maxVar = v;
    if (maxGrad < 1e-6f) maxGrad = 1.0f;
    if (maxVar < 1e-6f) maxVar = 1.0f;

    std::vector<float> scores(smallW * smallH);
    for (int i = 0; i < smallW * smallH; ++i) {
        scores[i] = 0.4f * density[i]
                  + 0.3f * (1.0f - gradient[i] / maxGrad)
                  + 0.3f * (1.0f - variance[i] / maxVar);
    }

    // 4. Otsu 阈值
    uint8_t thresh = otsuThreshold(scores);

    // 5. 二值化
    std::vector<uint8_t> smallMask(smallW * smallH);
    for (int i = 0; i < smallW * smallH; ++i) {
        int val = static_cast<int>(scores[i] * 255.0f + 0.5f);
        smallMask[i] = (val >= thresh) ? 255 : 0;
    }

    // 6. 形态学闭运算
    morphologicalClose(smallMask, smallW, smallH);

    // 7. 羽化（在小图上做，避免全图高斯模糊的性能开销）
    if (featherRadius > 0) {
        int smallFeather = std::max(1, featherRadius / 4);
        std::vector<uint8_t> blurredMask;
        gaussianBlurMask(smallMask, smallW, smallH, blurredMask, static_cast<float>(smallFeather));
        smallMask = std::move(blurredMask);
    }
    // featherRadius == 0 表示完全不羽化，与 UI 语义一致

    // 8. 缩放回原始尺寸（双线性插值）
    std::vector<uint8_t> fullMask;
    resizeMask(smallMask, smallW, smallH, fullMask, width, height);

    mask = std::move(fullMask);
    return true;
}

// ---------------------------------------------------------------------------
// 8. 加载用户蒙版
// ---------------------------------------------------------------------------
bool SkyGroundMask::loadUserMask(const std::string& path, int width, int height,
                                 std::vector<uint8_t>& mask)
{
    QImage img(QString::fromStdString(path));
    if (img.isNull())
        return false;

    int srcW = img.width();
    int srcH = img.height();
    std::vector<uint8_t> srcMask(srcW * srcH);

    bool hasAlpha = img.hasAlphaChannel();
    for (int y = 0; y < srcH; ++y) {
        for (int x = 0; x < srcW; ++x) {
            QRgb pixel = img.pixel(x, y);
            if (hasAlpha) {
                int alpha = qAlpha(pixel);
                srcMask[y * srcW + x] = (alpha == 0) ? 255 : 0;
            } else {
                int gray = qGray(pixel);
                srcMask[y * srcW + x] = (gray > 128) ? 255 : 0;
            }
        }
    }

    if (srcW == width && srcH == height) {
        mask = std::move(srcMask);
    } else {
        resizeMask(srcMask, srcW, srcH, mask, width, height);
    }
    return true;
}
