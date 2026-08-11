#pragma once

#include <QString>
#include <cstdint>
#include <vector>

class QImage;

/**
 * @brief 天空/地景蒙版生成器
 *
 * 支持自动地平线检测和用户上传蒙版。
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
     * 先在低分辨率亮度图上寻找连续地平线，再将地平线上方标记
     * 为天空。连续性约束可以避免星点、草叶和灯光等局部纹理把
     * 蒙版切成零散区域。
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
     * @brief 从经过相机显色的预览图检测，并缩放到处理图尺寸
     *
     * 线性 RAW 在地平线附近可能缺少肉眼可见的亮度反差。相机内嵌
     * 预览已经应用白平衡和显示曲线，更适合做结构分割。若输入和目标
     * 长宽比不一致则返回 false，调用方应回退到线性图检测。
     */
    static bool autoDetectPreview(const QImage& preview, int targetWidth,
                                  int targetHeight, std::vector<uint8_t>& mask,
                                  int featherRadius = 0);

    /**
     * @brief 加载用户蒙版
     *
     * 支持格式：
     * - 白/黑 PNG/JPG：白色=天空，黑色=地景
     * - PS alpha蒙版：白色=天空，黑色或完全透明=地景
     *
     * @param path  蒙版文件路径
     * @param width  目标宽度（缩放匹配）
     * @param height 目标高度（缩放匹配）
     * @param mask   输出 8-bit 灰度蒙版
     * @return true 成功
     */
    static bool loadUserMask(const QString& path, int width, int height,
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

};
