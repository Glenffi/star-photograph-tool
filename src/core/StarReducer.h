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
 * 先用局部 RGB 背景建立无星层，再从原图分离出有符号星层。圆形
 * Minimum（形态学腐蚀）只作用于星层，随后与无星层重新合成。
 * 这让小于腐蚀核的暗弱星点真正消失，同时保护银河和地景纹理。
 */
class StarReducer {
public:
    /**
     * @brief 对 RGB 图像执行缩星处理
     *
     * @param image     输入/输出 16-bit RGB 图像数据（interleaved R,G,B）
     * @param width     图像宽度
     * @param height    图像高度
     * @param strength  缩星强度 0-100，对应约 0-2 px 圆形腐蚀半径；
     *                  70 以上会逐步清除腐蚀后残留的弱小星点
     * @return true 处理成功；false 参数校验失败或内部错误
     */
    static bool reduce(std::vector<uint16_t>& image, int width, int height,
                       int strength, StarReductionStats* stats = nullptr,
                       const std::vector<uint8_t>* processingMask = nullptr);
};
