#pragma once

#include <QString>
#include <functional>
#include <cstdint>
#include <vector>

/**
 * @brief 图像导出器
 *
 * 支持 TIFF 16-bit 和 PNG 8-bit 预览导出。
 */
class ImageExporter {
public:
    enum Format {
        Tiff16,
        Png8
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
                            const QString& path,
                            Format format = Tiff16,
                            const std::function<bool()>& cancelled = {});

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
                            const QString& path,
                            Format format = Tiff16,
                            const std::function<bool()>& cancelled = {});

    // 读取本应用导出的 16-bit 连续平面 TIFF。灰度输入会复制为 RGB。
    static bool loadTiffRgb16(const QString& path,
                              std::vector<uint16_t>& rgb,
                              int& width, int& height);
};
