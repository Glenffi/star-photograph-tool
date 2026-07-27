#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct StarPoint;

struct StarReductionStats {
    size_t detectedStars = 0;
    size_t processedStars = 0;
    size_t affectedPixels = 0;
    double averageInputFwhm = 0.0;
    double radiusScale = 1.0;
};

/**
 * @brief 缩星处理器
 *
 * 基于星点检测、局部背景估计和 PSF 径向收缩的纯开源缩星方案。
 */
class StarReducer {
public:
    /**
     * @brief 对 RGB 图像执行缩星处理
     *
     * @param image     输入/输出 16-bit RGB 图像数据（interleaved R,G,B）
     * @param width     图像宽度
     * @param height    图像高度
     * @param strength  缩星强度 0-100（推荐 30-70）
     * @return true 处理成功；false 参数校验失败或内部错误
     */
    static bool reduce(std::vector<uint16_t>& image, int width, int height,
                       int strength, StarReductionStats* stats = nullptr);

private:
    // 使用亮度比例重建 RGB
    // outputL: 处理后的亮度，originalL: 原始亮度
    static void rebuildRgb(const std::vector<uint16_t>& rgb,
                           const std::vector<uint16_t>& originalL,
                           const std::vector<uint16_t>& outputL,
                           int width, int height,
                           std::vector<uint16_t>& outRgb);
};
