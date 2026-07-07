#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 图像导出器
 *
 * 支持 TIFF 16-bit 和 PNG 16-bit 导出。
 */
class ImageExporter {
public:
    enum Format {
        Tiff16,
        Png16
    };

    /**
     * @brief 导出 16-bit 单通道图像
     *
     * @param image  16-bit 图像数据
     * @param width  图像宽度
     * @param height 图像高度
     * @param path   输出文件路径
     * @param format 输出格式
     * @return true 导出成功
     */
    static bool export16Bit(const std::vector<uint16_t>& image,
                            int width, int height,
                            const std::string& path,
                            Format format = Tiff16);

    /**
     * @brief 导出 16-bit RGB 图像（3 通道）
     *
     * @param rgb    RGB 数据，按 R,G,B 顺序排列，总长度 = width * height * 3
     * @param width  图像宽度
     * @param height 图像高度
     * @param path   输出文件路径
     * @param format 输出格式
     * @return true 导出成功
     */
    static bool exportRgb16(const std::vector<uint16_t>& rgb,
                              int width, int height,
                              const std::string& path,
                              Format format = Tiff16);
};
