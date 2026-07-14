#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 天空/地景蒙版生成器
 *
 * 支持自动检测（基于星点密度+纹理+亮度方差）和用户上传蒙版。
 * 输出 8-bit 灰度蒙版：255=天空（需要对齐），0=地景（不移动），中间值=羽化过渡。
 */
class SkyGroundMask {
public:
    enum Mode {
        AutoDetect,   // 自动检测（传统CV，无AI）
        UserMask      // 用户上传蒙版
    };

    /**
     * @brief 自动检测天空/地景蒙版
     *
     * 基于星点密度+纹理强度+亮度方差的Patch分类法。
     *
     * @param image   16-bit 单通道图像（Bayer CFA或灰度）
     * @param width   图像宽度
     * @param height  图像高度
     * @param mask    输出 8-bit 灰度蒙版（0=地景, 255=天空）
     * @return true 成功
     */
    static bool autoDetect(const std::vector<uint16_t>& image, int width, int height,
                           std::vector<uint8_t>& mask, int featherRadius = 0);

    /**
     * @brief 加载用户蒙版
     *
     * 支持格式：
     * - 白/黑 PNG/JPG：白色=天空，黑色=地景
     * - PS alpha蒙版：透明/白色=天空，黑色=地景
     *
     * @param path  蒙版文件路径
     * @param width  目标宽度（缩放匹配）
     * @param height 目标高度（缩放匹配）
     * @param mask   输出 8-bit 灰度蒙版
     * @return true 成功
     */
    static bool loadUserMask(const std::string& path, int width, int height,
                             std::vector<uint8_t>& mask, int featherRadius = 0);

    /**
     * @brief 应用羽化到蒙版边缘
     *
     * 对蒙版边缘 ±featherRadius 内做高斯模糊平滑。
     *
     * @param mask   输入/输出蒙版
     * @param width  宽度
     * @param height 高度
     * @param featherRadius 羽化半径（像素）
     */
    static void feather(std::vector<uint8_t>& mask, int width, int height,
                        int featherRadius);

private:
    // 计算图像梯度强度（Sobel算子）
    static void computeGradient(const std::vector<uint16_t>& image, int w, int h,
                                std::vector<float>& gradient);

    // 计算局部亮度方差
    static void computeVariance(const std::vector<uint16_t>& image, int w, int h,
                                std::vector<float>& variance);

    // 星点密度检测（复用 StarDetector 的阈值逻辑）
    static void computeStarDensity(const std::vector<uint16_t>& image, int w, int h,
                                    std::vector<float>& density);

    // Otsu 自动阈值
    static uint8_t otsuThreshold(const std::vector<float>& scores);

    // 形态学闭运算（填充小洞）
    static void morphologicalClose(std::vector<uint8_t>& mask, int w, int h);
};
