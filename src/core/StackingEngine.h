#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 堆栈引擎
 *
 * 支持 Average、Median、Kappa-Sigma、Winsorized 四种堆栈算法。
 */
class StackingEngine {
public:
    enum Method {
        Average,      // 平均值（信噪比最高）
        Median,       // 中位数（抗异常值，适合 ≤5 帧）
        KappaSigma,   // Kappa-Sigma Clipping（剔除异常值，适合 6-15 帧）
        Winsorized    // Winsorized Sigma Clipping（MAD 替代标准差，适合 >15 帧）
    };

    /**
     * @brief 堆栈多张图像
     */
    bool stack(const std::vector<std::vector<uint16_t>>& images,
               int width, int height,
               Method method,
               double kappa,       // Kappa-Sigma 的 κ 值
               std::vector<uint16_t>& result);

    /**
     * @brief 天地分离堆栈
     *
     * 天空区域用对齐后图像堆栈，地景区域用原始图像直接堆栈（不移动），边界羽化融合。
     *
     * @param images         已对齐的图像序列（用于天空堆栈）
     * @param originalImages 原始图像序列（用于地景堆栈，不移动）
     * @param width          图像宽度
     * @param height         图像高度
     * @param method         天空堆栈算法
     * @param kappa          κ 值
     * @param mask           8-bit 灰度蒙版（255=天空，0=地景）
     * @param result         输出堆栈结果
     * @return true 成功
     */
    bool stackWithMask(const std::vector<std::vector<uint16_t>>& images,
                       const std::vector<std::vector<uint16_t>>& originalImages,
                       int width, int height,
                       Method method, double kappa,
                       const std::vector<uint8_t>& mask,
                       std::vector<uint16_t>& result);

    /**
     * @brief 根据帧数推荐最佳堆栈算法
     */
    static Method recommendMethod(int frameCount);

private:
    void stackAverage(const std::vector<std::vector<uint16_t>>& images,
                      int w, int h, std::vector<uint16_t>& result);
    void stackMedian(const std::vector<std::vector<uint16_t>>& images,
                     int w, int h, std::vector<uint16_t>& result);
    void stackKappaSigma(const std::vector<std::vector<uint16_t>>& images,
                          int w, int h, double kappa,
                          std::vector<uint16_t>& result);
    void stackWinsorized(const std::vector<std::vector<uint16_t>>& images,
                          int w, int h, double kappa,
                          std::vector<uint16_t>& result);
};
