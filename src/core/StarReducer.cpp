#include "StarReducer.h"
#include "StarDetector.h"
#include <cmath>
#include <algorithm>

bool StarReducer::reduce(std::vector<uint16_t>& image, int width, int height, int strength) {
    if (image.empty() || width <= 0 || height <= 0 || strength <= 0) {
        return true; // 无缩星需求，视为成功
    }

    // 1. 从 RGB 提取亮度通道用于星点检测
    std::vector<uint16_t> lum(width * height);
    for (int i = 0; i < width * height; ++i) {
        uint32_t r = image[i * 3 + 0];
        uint32_t g = image[i * 3 + 1];
        uint32_t b = image[i * 3 + 2];
        lum[i] = static_cast<uint16_t>((r * 299 + g * 587 + b * 114) / 1000);
    }

    // 2. 检测星点
    StarDetector detector;
    std::vector<StarPoint> stars;
    if (!detector.detect(lum, width, height, stars, 5.0)) {
        return true; // 未检测到星点，无需处理
    }

    // 3. 对每个星点应用局部模糊缩小
    // 限制处理星点数量，避免过长的处理时间
    const size_t maxStars = 5000;
    size_t processCount = std::min(stars.size(), maxStars);

    for (size_t i = 0; i < processCount; ++i) {
        const auto& star = stars[i];
        blurStarRegion(image, width, height, star.x, star.y, star.fwhm, strength);
    }

    return true;
}

void StarReducer::blurStarRegion(std::vector<uint16_t>& image, int w, int h,
                                 double cx, double cy, double fwhm,
                                 int strength) {
    // 计算模糊窗口大小：基于 FWHM 和强度
    // strength 0-100 映射到 sigma 缩放因子 1.0 - 3.0
    float strengthFactor = 1.0f + (strength / 100.0f) * 2.0f;
    float sigma = static_cast<float>(fwhm * 0.5 * strengthFactor);
    if (sigma < 0.8f) sigma = 0.8f;

    // 窗口大小：覆盖 3-sigma 范围
    int windowSize = static_cast<int>(std::ceil(sigma * 6.0f));
    if (windowSize % 2 == 0) windowSize++;
    if (windowSize < 3) windowSize = 3;

    // 限制窗口不超过合理范围
    int half = windowSize / 2;
    int x0 = static_cast<int>(cx) - half;
    int y0 = static_cast<int>(cy) - half;
    int x1 = static_cast<int>(cx) + half;
    int y1 = static_cast<int>(cy) + half;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    int regionW = x1 - x0 + 1;
    int regionH = y1 - y0 + 1;
    if (regionW < 3 || regionH < 3) return;

    // 提取局部区域（3 通道）
    std::vector<uint16_t> region(regionW * regionH * 3);
    for (int ry = 0; ry < regionH; ++ry) {
        for (int rx = 0; rx < regionW; ++rx) {
            int srcIdx = ((y0 + ry) * w + (x0 + rx)) * 3;
            int dstIdx = (ry * regionW + rx) * 3;
            region[dstIdx + 0] = image[srcIdx + 0];
            region[dstIdx + 1] = image[srcIdx + 1];
            region[dstIdx + 2] = image[srcIdx + 2];
        }
    }

    // 生成 1D 高斯核
    int kernelSize = static_cast<int>(std::ceil(sigma * 6.0f));
    if (kernelSize % 2 == 0) kernelSize++;
    int kHalf = kernelSize / 2;

    std::vector<float> kernel(kernelSize);
    float kSum = 0.0f;
    for (int i = 0; i < kernelSize; ++i) {
        float x = static_cast<float>(i - kHalf);
        kernel[i] = std::exp(-(x * x) / (2.0f * sigma * sigma));
        kSum += kernel[i];
    }
    for (float& k : kernel) k /= kSum;

    // 临时缓冲区
    std::vector<float> temp(regionW * regionH * 3);
    std::vector<uint16_t> blurred(regionW * regionH * 3);

    // 行方向模糊（每通道分别处理）
    for (int c = 0; c < 3; ++c) {
        for (int ry = 0; ry < regionH; ++ry) {
            for (int rx = 0; rx < regionW; ++rx) {
                float sum = 0.0f;
                for (int k = 0; k < kernelSize; ++k) {
                    int px = rx + k - kHalf;
                    px = std::clamp(px, 0, regionW - 1);
                    sum += static_cast<float>(region[(ry * regionW + px) * 3 + c]) * kernel[k];
                }
                temp[(ry * regionW + rx) * 3 + c] = sum;
            }
        }
    }

    // 列方向模糊
    for (int c = 0; c < 3; ++c) {
        for (int ry = 0; ry < regionH; ++ry) {
            for (int rx = 0; rx < regionW; ++rx) {
                float sum = 0.0f;
                for (int k = 0; k < kernelSize; ++k) {
                    int py = ry + k - kHalf;
                    py = std::clamp(py, 0, regionH - 1);
                    sum += temp[(py * regionW + rx) * 3 + c] * kernel[k];
                }
                float val = sum;
                uint16_t outVal = static_cast<uint16_t>(std::clamp(static_cast<int>(val + 0.5f), 0, 65535));
                blurred[(ry * regionW + rx) * 3 + c] = outVal;
            }
        }
    }

    // 混合：中心区域用模糊结果，边缘用原图（避免硬边）
    // 使用距离中心的权重进行羽化混合
    double centerX = regionW / 2.0;
    double centerY = regionH / 2.0;
    double maxDist = std::sqrt(centerX * centerX + centerY * centerY);
    if (maxDist < 1.0) maxDist = 1.0;

    for (int ry = 0; ry < regionH; ++ry) {
        for (int rx = 0; rx < regionW; ++rx) {
            double dx = rx - centerX;
            double dy = ry - centerY;
            double dist = std::sqrt(dx * dx + dy * dy);

            // 权重：中心 1.0（完全模糊），边缘 0.0（完全原图）
            float blurWeight = static_cast<float>(std::max(0.0, 1.0 - dist / (maxDist * 0.7)));
            // 增强权重曲线，使中心更模糊
            blurWeight = blurWeight * blurWeight;

            int srcIdx = ((y0 + ry) * w + (x0 + rx)) * 3;
            int dstIdx = (ry * regionW + rx) * 3;

            for (int c = 0; c < 3; ++c) {
                float orig = static_cast<float>(image[srcIdx + c]);
                float blur = static_cast<float>(blurred[dstIdx + c]);
                float mixed = orig * (1.0f - blurWeight) + blur * blurWeight;
                image[srcIdx + c] = static_cast<uint16_t>(std::clamp(static_cast<int>(mixed + 0.5f), 0, 65535));
            }
        }
    }
}
