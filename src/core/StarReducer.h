#pragma once

#include <cstdint>
#include <vector>

struct StarPoint;

/**
 * @brief 缩星处理器
 *
 * 基于星点检测 + 形态学腐蚀的纯开源缩星方案。
 * 检测星点后，构建全局软星点遮罩，对星点层执行灰度腐蚀缩小星点尺寸，
 * 再通过亮度比例重建 RGB，保留星云等大面积暗弱细节。
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
    static bool reduce(std::vector<uint16_t>& image, int width, int height, int strength);

private:
    // 从星点列表构建全局软遮罩（逐像素 max 合并）
    // mask 值范围 [0, 1.0]，表示该像素受缩星影响的程度
    static void buildStarMask(const std::vector<StarPoint>& stars,
                              int width, int height,
                              std::vector<float>& mask);

    // 对亮度图执行圆形结构元素的灰度腐蚀
    // radius: 腐蚀半径（1-3 像素）
    static void erodeLuminance(const std::vector<uint16_t>& lum, int w, int h,
                               int radius, std::vector<uint16_t>& out);

    // 使用亮度比例重建 RGB
    // outputL: 处理后的亮度，originalL: 原始亮度
    static void rebuildRgb(const std::vector<uint16_t>& rgb,
                           const std::vector<uint16_t>& originalL,
                           const std::vector<uint16_t>& outputL,
                           int width, int height,
                           std::vector<uint16_t>& outRgb);
};
