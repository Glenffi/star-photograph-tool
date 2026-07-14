#pragma once

#include <cstdint>
#include <vector>

struct StarPoint;

/**
 * @brief 缩星处理器
 *
 * 基于星点检测 + 局部高斯模糊的纯开源缩星方案。
 * 检测星点后，在每个星点周围应用自适应高斯模糊，缩小星点尺寸，
 * 同时保留星云等大面积暗弱细节。
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
     * @return true 处理成功（即使未检测到星点也返回 true）
     */
    static bool reduce(std::vector<uint16_t>& image, int width, int height, int strength);

private:
    // 对每个星点应用局部高斯模糊缩小星点
    static void blurStarRegion(std::vector<uint16_t>& image, int w, int h,
                               double cx, double cy, double fwhm,
                               int strength);

    // 可分离高斯模糊（1D 核，分别作用于行和列）
    static void separableGaussianBlur(std::vector<uint16_t>& src, int w, int h,
                                      int cx, int cy, int windowSize, float sigma);
};
