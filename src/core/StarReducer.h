#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct StarPoint;

struct StarReductionStats {
    size_t detectedStars = 0;
    size_t processedStars = 0;
    size_t stronglySuppressedStars = 0;
    size_t affectedPixels = 0;
    double averageInputFwhm = 0.0;
    double radiusScale = 1.0;
};

/**
 * @brief 缩星处理器
 *
 * 基于低阈值星点检测、局部 RGB 背景估计和 PSF 径向收缩的方案。
 * 亮星收紧轮廓，暗弱小星随强度提高逐渐融入周围天空颜色。
 */
class StarReducer {
public:
    /**
     * @brief 对 RGB 图像执行缩星处理
     *
     * @param image     输入/输出 16-bit RGB 图像数据（interleaved R,G,B）
     * @param width     图像宽度
     * @param height    图像高度
     * @param strength  缩星强度 0-100（40 温和，70 强烈，90 接近清星）
     * @return true 处理成功；false 参数校验失败或内部错误
     */
    static bool reduce(std::vector<uint16_t>& image, int width, int height,
                       int strength, StarReductionStats* stats = nullptr);
};
