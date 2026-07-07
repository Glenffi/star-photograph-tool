#pragma once

#include <vector>
#include <cstdint>

/**
 * @brief 自动优化引擎
 *
 * 提供去雾（Dark Channel Prior + Guided Filter）和曲线拉伸（Arcsinh）功能。
 */
class AutoOptimizeEngine {
public:
    // 去雾：Dark Channel Prior + Guided Filter
    // 输入/输出：16-bit 单通道图像
    static bool dehaze(const std::vector<uint16_t>& src, int w, int h,
                       int strength,  // 0-100，控制去雾强度
                       std::vector<uint16_t>& dst);

    // 曲线拉伸：Arcsinh 拉伸
    // 输入/输出：16-bit 单通道图像
    static bool stretchCurve(const std::vector<uint16_t>& src, int w, int h,
                             std::vector<uint16_t>& dst);

private:
    // Guided Filter 辅助函数
    static void guidedFilter(const std::vector<float>& guide,
                             const std::vector<float>& src,
                             std::vector<float>& dst,
                             int w, int h, int r, float eps);

    // 计算百分位数
    static float percentile(const std::vector<float>& data, float p);
};
