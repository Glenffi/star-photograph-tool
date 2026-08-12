#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class ModifiedCameraNeutralMode {
    Automatic,
    ManualPoint
};

struct ModifiedCameraColorOptions {
    int strength = 100;
    ModifiedCameraNeutralMode neutralMode =
        ModifiedCameraNeutralMode::Automatic;
    // Normalized image coordinates keep a picked point stable between the
    // bounded quick preview and the full-resolution finishing pass.
    double manualPointX = 0.5;
    double manualPointY = 0.5;
};

struct ModifiedCameraColorStats {
    std::array<uint16_t, 3> neutralSample = {};
    std::array<double, 3> gains = {1.0, 1.0, 1.0};
    size_t sampleCount = 0;
    size_t clippedChannelValues = 0;
    bool usedManualPoint = false;
    double samplePointX = 0.5;
    double samplePointY = 0.5;
    bool applied = false;
};

/**
 * @brief 自动优化引擎
 *
 * 提供去雾（Dark Channel Prior + Guided Filter）和曲线拉伸（Arcsinh）功能。
 */
class AutoOptimizeEngine {
public:
    // RGB-aware path used by the production pipeline. Dehaze derives one
    // transmission map from luminance and applies it to all channels, avoiding
    // the color shifts caused by independent per-channel processing.
    static bool dehazeRgb(const std::vector<uint16_t>& src, int w, int h,
                          int strength, std::vector<uint16_t>& dst);

    // Removes a robust additive background cast, then applies one linked
    // luminance curve to RGB so star colors and white balance remain coherent.
    // In a nightscape, skyMask keeps dark terrain out of the coarse background
    // model and smoothly feathers the correction away from protected land.
    static bool stretchRgb(const std::vector<uint16_t>& src, int w, int h,
                           std::vector<uint16_t>& dst,
                           const std::vector<uint8_t>* skyMask = nullptr);

    static bool neutralizeBackgroundRgb(const std::vector<uint16_t>& src,
                                        int w, int h,
                                        std::vector<uint16_t>& dst,
                                        const std::vector<uint8_t>* skyMask = nullptr);

    // Restores a conventional color balance for BCF/astronomy-modified
    // cameras. A robust neutral sky sample is measured on linear RGB before
    // stretch; bounded per-channel gains approximate a Camera Raw gray-point
    // correction while leaving local H-alpha contrast present.
    static bool restoreModifiedCameraColorRgb(
        const std::vector<uint16_t>& src, int w, int h,
        std::vector<uint16_t>& dst,
        ModifiedCameraColorStats* stats = nullptr,
        const std::vector<uint8_t>* skyMask = nullptr,
        const ModifiedCameraColorOptions& options = {});

    // Restores fine texture and medium-scale clarity only where skyMask
    // approaches zero. The clarity layer favors distant ground near the
    // horizon; RGB uses one luminance ratio so ground color remains stable.
    static bool enhanceGroundDetail(std::vector<uint16_t>& image,
                                    int w, int h,
                                    const std::vector<uint8_t>& skyMask,
                                    int strength);

    // 去雾：Dark Channel Prior + Guided Filter
    // 输入/输出：16-bit 单通道图像
    static bool dehaze(const std::vector<uint16_t>& src, int w, int h,
                       int strength,  // 0-100，控制去雾强度
                       std::vector<uint16_t>& dst);

private:
    // Guided Filter 辅助函数
    static void guidedFilter(const std::vector<float>& guide,
                             const std::vector<float>& src,
                             std::vector<float>& dst,
                             int w, int h, int r, float eps);

};
