#include "StarReducer.h"
#include "StarDetector.h"
#include <cmath>
#include <algorithm>
#include <limits>

bool StarReducer::reduce(std::vector<uint16_t>& image, int width, int height, int strength) {
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536 ||
        width > INT_MAX / height) {
        return false;
    }

    const int pixelCount = width * height;
    const size_t expectedSize = static_cast<size_t>(pixelCount) * 3;
    if (image.size() != expectedSize) {
        return false;
    }

    // Zero strength is a valid no-op. Negative input is outside the documented
    // range and is rejected instead of silently hiding a caller bug.
    if (strength < 0) return false;
    if (strength == 0) return true;
    strength = std::min(strength, 100);

    // 1. 从 RGB 提取亮度通道
    std::vector<uint16_t> originalLum(pixelCount);
    for (int i = 0; i < pixelCount; ++i) {
        uint32_t r = image[i * 3 + 0];
        uint32_t g = image[i * 3 + 1];
        uint32_t b = image[i * 3 + 2];
        originalLum[i] = static_cast<uint16_t>((r * 299 + g * 587 + b * 114) / 1000);
    }

    // 2. 检测星点（缩星模式：最多 5000 个高质量星点）
    StarDetector detector;
    std::vector<StarPoint> stars;
    DetectionOptions reduceOptions;
    reduceOptions.maxCandidates = 5000;
    reduceOptions.maxStars = 5000;
    if (!detector.detect(originalLum, width, height, stars, reduceOptions)) {
        return true;
    }

    // StarReducer 中已按 flux 排序后截断至 5000，此处检测器内也限制候选数量
    // 双重保护：检测器限制候选拟合数量，Reducer 限制最终星点数量
    const size_t maxStars = 5000;
    if (stars.size() > maxStars) {
        stars.resize(maxStars);
    }

    // 过滤星点：排除过大、过扁、过暗的星点
    std::vector<StarPoint> filteredStars;
    filteredStars.reserve(stars.size());
    for (const auto& star : stars) {
        if (star.fwhm > 20.0) continue;
        if (star.ellipticity > 0.7) continue;
        if (star.flux < 100.0) continue;
        filteredStars.push_back(star);
    }

    if (filteredStars.empty()) {
        return true;
    }

    // 3. 构建全局软星点遮罩
    std::vector<float> starMask(pixelCount, 0.0f);
    buildStarMask(filteredStars, width, height, starMask);

    // 4. 预计算 radius=1/2/3 的三种腐蚀结果
    std::vector<uint16_t> eroded1(pixelCount);
    std::vector<uint16_t> eroded2(pixelCount);
    std::vector<uint16_t> eroded3(pixelCount);
    erodeLuminance(originalLum, width, height, 1, eroded1);
    erodeLuminance(originalLum, width, height, 2, eroded2);
    erodeLuminance(originalLum, width, height, 3, eroded3);

    // 5. 强度映射：连续插值到腐蚀结果
    // strength 0-100 映射到连续的 radius 和 mixRatio
    // 0: 不处理
    // 1-100: 连续从 radius=1/mix=0.025 过渡到 radius=3/mix=1.0
    float normalized = strength / 100.0f;
    // mixRatio: 0.025 ~ 1.0，低强度时仍有轻微效果
    float mixRatio = 0.025f + normalized * 0.975f;

    // radius 连续值: 1.0 ~ 3.0
    float radiusF = 1.0f + normalized * 2.0f;
    int rLow = static_cast<int>(radiusF);      // 1 或 2
    int rHigh = rLow + 1;                       // 2 或 3
    float rFrac = radiusF - rLow;               // 0.0 ~ 1.0 插值因子
    if (rLow > 3) rLow = 3;
    if (rHigh > 3) rHigh = 3;

    // 选择对应的腐蚀结果指针
    const std::vector<uint16_t>* erodedLow = &eroded1;
    const std::vector<uint16_t>* erodedHigh = &eroded2;
    if (rLow == 2) erodedLow = &eroded2;
    if (rLow == 3) erodedLow = &eroded3;
    if (rHigh == 2) erodedHigh = &eroded2;
    if (rHigh == 3) erodedHigh = &eroded3;

    // 6. 使用软遮罩混合，并在相邻 radius 之间连续插值
    std::vector<uint16_t> outputLum(pixelCount);
    for (int i = 0; i < pixelCount; ++i) {
        float weight = starMask[i] * mixRatio;
        if (weight > 1.0f) weight = 1.0f;

        float orig = static_cast<float>(originalLum[i]);
        // 在相邻 radius 腐蚀结果之间插值
        float erodedLowVal = static_cast<float>((*erodedLow)[i]);
        float erodedHighVal = static_cast<float>((*erodedHigh)[i]);
        float eroded = erodedLowVal * (1.0f - rFrac) + erodedHighVal * rFrac;

        float blended = orig * (1.0f - weight) + eroded * weight;
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

        // 循环半径直接使用 maskRadius（smoothstep 在 maskRadius 处已精确降到 0）
        int r = static_cast<int>(std::ceil(maskRadius));

        int cx = static_cast<int>(star.x);
        int cy = static_cast<int>(star.y);
        int x0 = std::max(0, cx - r);
        int y0 = std::max(0, cy - r);
        int x1 = std::min(width - 1, cx + r);
        int y1 = std::min(height - 1, cy + r);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                double dx = static_cast<double>(x) - star.x;
                double dy = static_cast<double>(y) - star.y;
                double dist = std::sqrt(dx * dx + dy * dy);

                // smoothstep 紧支撑遮罩：在 maskRadius 处精确降到 0
                // 中心=1.0，maskRadius 处=0.0，使用 smoothstep 避免硬边
                double weight = 0.0;
                if (dist < maskRadius) {
                    // 归一化到 [0, 1]，0 在中心，1 在边界
                    double t = dist / maskRadius;
                    // smoothstep: 3t^2 - 2t^3
                    weight = 1.0 - (t * t * (3.0 - 2.0 * t));
                    if (weight < 0.0) weight = 0.0;
                    if (weight > 1.0) weight = 1.0;
                }
                // dist >= maskRadius 时 weight 保持 0.0

                if (weight < 0.001) continue;

                int idx = y * width + x;
                if (idx >= 0 && idx < pixelCount) {
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

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint16_t minVal = std::numeric_limits<uint16_t>::max();

            int y0 = std::max(0, y - radius);
            int y1 = std::min(h - 1, y + radius);
            int x0 = std::max(0, x - radius);
            int x1 = std::min(w - 1, x + radius);

            for (int py = y0; py <= y1; ++py) {
                for (int px = x0; px <= x1; ++px) {
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

    const float minRatio = 0.1f;
    const float maxRatio = 10.0f;
    const float epsilon = 1.0f;

    for (int i = 0; i < pixelCount; ++i) {
        float origL = static_cast<float>(originalL[i]);
        float outL = static_cast<float>(outputL[i]);

        float ratio = outL / std::max(origL, epsilon);
        ratio = std::clamp(ratio, minRatio, maxRatio);

        float r = static_cast<float>(rgb[i * 3 + 0]) * ratio;
        float g = static_cast<float>(rgb[i * 3 + 1]) * ratio;
        float b = static_cast<float>(rgb[i * 3 + 2]) * ratio;

        outRgb[i * 3 + 0] = static_cast<uint16_t>(std::clamp(static_cast<int>(r + 0.5f), 0, 65535));
        outRgb[i * 3 + 1] = static_cast<uint16_t>(std::clamp(static_cast<int>(g + 0.5f), 0, 65535));
        outRgb[i * 3 + 2] = static_cast<uint16_t>(std::clamp(static_cast<int>(b + 0.5f), 0, 65535));
    }
}
