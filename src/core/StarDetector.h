#pragma once

#include <cstdint>
#include <vector>
#include <utility>

struct StarPoint {
    double x = 0.0;        // 亚像素中心坐标 X
    double y = 0.0;        // 亚像素中心坐标 Y
    double flux = 0.0;     // 亮度总流量
    double fwhm = 0.0;     // 半高全宽 (pixels)
    double ellipticity = 0.0; // 椭率 (0=圆形, 1=线)
};

/**
 * @brief 星点检测器
 *
 * 基于高斯模糊 + 局部最大值检测 + 2D 高斯拟合的星点检测。
 * 输入为单通道 16-bit 图像（Bayer 或灰度）。
 */
class StarDetector {
public:
    /**
     * @brief 检测图像中的星点
     *
     * @param image  16-bit 单通道图像数据
     * @param width  图像宽度
     * @param height 图像高度
     * @param stars  输出检测到的星点列表
     * @param thresholdSigma  检测阈值（背景噪声的倍数，默认 5.0）
     * @return true 检测成功
     */
    bool detect(const std::vector<uint16_t>& image, int width, int height,
                std::vector<StarPoint>& stars,
                double thresholdSigma = 5.0);

private:
    void gaussianBlur(const std::vector<uint16_t>& src, int w, int h,
                      std::vector<float>& dst, float sigma);
    std::pair<float, float> estimateBackground(const std::vector<uint16_t>& image, int w, int h);
    bool fit2DGaussian(const std::vector<uint16_t>& image, int w, int h,
                       int cx, int cy, int windowSize,
                       StarPoint& star);
};
