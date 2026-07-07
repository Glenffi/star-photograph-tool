#pragma once

#include <cstdint>
#include <vector>
#include "StarDetector.h"

/**
 * @brief 图像对齐变换矩阵
 *
 * 仿射变换：
 *   x' = a*x + b*y + c
 *   y' = d*x + e*y + f
 */
struct AlignmentTransform {
    double a = 1.0, b = 0.0, c = 0.0;
    double d = 0.0, e = 1.0, f = 0.0;
};

/**
 * @brief 图像对齐器
 *
 * 基于星点三角形匹配 + RANSAC 的图像对齐。
 */
class ImageAligner {
public:
    /**
     * @brief 计算两幅图像的星点匹配和对齐变换
     *
     * @param refStars  参考帧的星点列表
     * @param srcStars  源帧的星点列表
     * @param out       输出变换矩阵
     * @return true 对齐成功
     */
    bool align(const std::vector<StarPoint>& refStars,
               const std::vector<StarPoint>& srcStars,
               AlignmentTransform& out);

    /**
     * @brief 应用仿射变换到图像
     *
     * 使用双线性插值重采样。
     *
     * @param src   源图像 (16-bit 单通道)
     * @param w     宽度
     * @param h     高度
     * @param t     变换矩阵
     * @param dst   输出图像 (与源相同尺寸)
     * @return true 成功
     */
    bool applyTransform(const std::vector<uint16_t>& src, int w, int h,
                        const AlignmentTransform& t,
                        std::vector<uint16_t>& dst);

private:
    bool triangleMatch(const std::vector<StarPoint>& refStars,
                       const std::vector<StarPoint>& srcStars,
                       std::vector<std::pair<int, int>>& matches);
    bool ransacAffine(const std::vector<StarPoint>& refStars,
                        const std::vector<StarPoint>& srcStars,
                        const std::vector<std::pair<int, int>>& matches,
                        AlignmentTransform& out);
};
