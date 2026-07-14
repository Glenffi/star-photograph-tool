#include "StarReducer.h"
#include "StarDetector.h"
#include <cmath>
#include <algorithm>
#include <limits>

bool StarReducer::reduce(std::vector<uint16_t>& image, int width, int height, int strength) {
    // 参数校验
    if (image.empty() || width <= 0 || height <= 0) {
        return true; // 无数据，视为成功（无需处理）
    }
    if (strength <= 0) {
        return true; // 无缩星需求
    }
    if (strength > 100) {
        strength = 100;
    }

    // 验证数据长度
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    if (image.size() != expectedSize) {
        return false; // 数据长度不匹配
    }

    // 检查整数溢出（width * height 是否超出合理范围）
    if (width > 65536 || height > 65536) {
        return false;
    }

    const int pixelCount = width * height;

    // 1. 从 RGB 提取亮度通道
    std::vector<uint16_t> originalLum(pixelCount);
    for (int i = 0; i < pixelCount; ++i) {
        uint32_t r = image[i * 3 + 0];
        uint32_t g = image[i * 3 + 1];
        uint32_t b = image[i * 3 + 2];
        originalLum[i] = static_cast<uint16_t>((r * 299 + g * 587 + b * 114) / 1000);
    }

    // 2. 检测星点
    StarDetector detector;
    std::vector<StarPoint> stars;
    if (!detector.detect(originalLum, width, height, stars, 5.0)) {
        return true; // 未检测到星点，无需处理
    }

    // 过滤星点：排除过大、过扁、过暗的星点
    std::vector<StarPoint> filteredStars;
    filteredStars.reserve(stars.size());

    for (const auto& star : stars) {
        // 排除 FWHM 过大（可能是星云/银河亮斑）
        if (star.fwhm > 20.0) continue;
        // 排除 ellipticity 过高（可能是飞机轨迹/星轨）
        if (star.ellipticity > 0.7) continue;
        // 排除 flux 过低（可能是热像素/噪点）
        if (star.flux < 100.0) continue;

        filteredStars.push_back(star);
    }

    if (filteredStars.empty()) {
        return true; // 过滤后无有效星点
    }

    // 3. 构建全局软星点遮罩
    std::vector<float> starMask(pixelCount, 0.0f);
    buildStarMask(filteredStars, width, height, starMask);

    // 4. 强度映射到腐蚀半径和混合权重
    // 0: 不处理
    // 1-40: radius=1，混合比例逐渐增加
    // 41-75: radius=1 到 2
    // 76-100: radius=2 到 3，限制最大混合比例
    int radius;
    float mixRatio;
    if (strength <= 40) {
        radius = 1;
        mixRatio = strength / 40.0f; // 0.0 ~ 1.0
    } else if (strength <= 75) {
        radius = 1;
        float t = (strength - 40) / 35.0f; // 0.0 ~ 1.0
        mixRatio = 1.0f;
        // 逐渐过渡到 radius=2
        if (t > 0.5f) {
            radius = 2;
        }
    } else {
        radius = 2;
        float t = (strength - 75) / 25.0f; // 0.0 ~ 1.0
        mixRatio = 1.0f;
        if (t > 0.5f) {
            radius = 3;
        }
    }

    // 5. 对亮度图执行灰度腐蚀
    std::vector<uint16_t> erodedLum(pixelCount);
    erodeLuminance(originalLum, width, height, radius, erodedLum);

    // 6. 使用软遮罩混合原始亮度与腐蚀亮度
    std::vector<uint16_t> outputLum(pixelCount);
    for (int i = 0; i < pixelCount; ++i) {
        float weight = starMask[i] * mixRatio;
        // 限制权重
        if (weight > 1.0f) weight = 1.0f;

        float orig = static_cast<float>(originalLum[i]);
        float erod = static_cast<float>(erodedLum[i]);
        float blended = orig * (1.0f - weight) + erod * weight;

        outputLum[i] = static_cast<uint16_t>(std::clamp(static_cast<int>(blended + 0.5f), 0, 65535));
    }

    // 7. 使用亮度比例重建 RGB
    std::vector<uint16_t> outputRgb;
    rebuildRgb(image, originalLum, outputLum, width, height, outputRgb);

    image = std::move(outputRgb);
    return true;
}

void StarReducer::buildStarMask(const std::vector<StarPoint>& stars,
                                int width, int height,
                                std::vector<float>& mask) {
    const int pixelCount = width * height;

    for (const auto& star : stars) {
        // 遮罩半径 = FWHM * 1.5，限制在 [2, 20] 像素
        double maskRadius = star.fwhm * 1.5;
        if (maskRadius < 2.0) maskRadius = 2.0;
        if (maskRadius > 20.0) maskRadius = 20.0;

        int cx = static_cast<int>(star.x);
        int cy = static_cast<int>(star.y);
        int r = static_cast<int>(std::ceil(maskRadius));

        int x0 = std::max(0, cx - r);
        int y0 = std::max(0, cy - r);
        int x1 = std::min(width - 1, cx + r);
        int y1 = std::min(height - 1, cy + r);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                double dx = static_cast<double>(x) - star.x;
                double dy = static_cast<double>(y) - star.y;
                double dist = std::sqrt(dx * dx + dy * dy);

                // 高斯衰减的软遮罩
                double sigma = maskRadius / 2.0;
                double weight = std::exp(-(dist * dist) / (2.0 * sigma * sigma));
                if (weight < 0.01) continue;

                int idx = y * width + x;
                if (idx >= 0 && idx < pixelCount) {
                    // 逐像素 max 合并
                    float w = static_cast<float>(weight);
                    if (w > mask[idx]) {
                        mask[idx] = w;
                    }
                }
            }
        }
    }
}

void StarReducer::erodeLuminance(const std::vector<uint16_t>& lum, int w, int h,
                                 int radius, std::vector<uint16_t>& out) {
    out.resize(lum.size());

    // 圆形结构元素的灰度腐蚀
    // 对每个像素，取圆形邻域内的最小值
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint16_t minVal = std::numeric_limits<uint16_t>::max();

            int y0 = std::max(0, y - radius);
            int y1 = std::min(h - 1, y + radius);
            int x0 = std::max(0, x - radius);
            int x1 = std::min(w - 1, x + radius);

            for (int py = y0; py <= y1; ++py) {
                for (int px = x0; px <= x1; ++px) {
                    // 圆形掩码：只处理圆形区域内的像素
                    int dy = py - y;
                    int dx = px - x;
                    if (dx * dx + dy * dy > radius * radius) continue;

                    uint16_t val = lum[py * w + px];
                    if (val < minVal) {
                        minVal = val;
                    }
                }
            }

            out[y * w + x] = minVal;
        }
    }
}

void StarReducer::rebuildRgb(const std::vector<uint16_t>& rgb,
                               const std::vector<uint16_t>& originalL,
                               const std::vector<uint16_t>& outputL,
                               int width, int height,
                               std::vector<uint16_t>& outRgb) {
    const int pixelCount = width * height;
    outRgb.resize(pixelCount * 3);

    const float minRatio = 0.1f;   // 最小比例，避免过度压缩
    const float maxRatio = 10.0f;  // 最大比例，避免过度放大
    const float epsilon = 1.0f;    // 避免除零

    for (int i = 0; i < pixelCount; ++i) {
        float origL = static_cast<float>(originalL[i]);
        float outL = static_cast<float>(outputL[i]);

        // 计算亮度比例
        float ratio = outL / std::max(origL, epsilon);
        ratio = std::clamp(ratio, minRatio, maxRatio);

        // 同步调整 RGB
        float r = static_cast<float>(rgb[i * 3 + 0]) * ratio;
        float g = static_cast<float>(rgb[i * 3 + 1]) * ratio;
        float b = static_cast<float>(rgb[i * 3 + 2]) * ratio;

        outRgb[i * 3 + 0] = static_cast<uint16_t>(std::clamp(static_cast<int>(r + 0.5f), 0, 65535));
        outRgb[i * 3 + 1] = static_cast<uint16_t>(std::clamp(static_cast<int>(g + 0.5f), 0, 65535));
        outRgb[i * 3 + 2] = static_cast<uint16_t>(std::clamp(static_cast<int>(b + 0.5f), 0, 65535));
    }
}
