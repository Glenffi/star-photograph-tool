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
