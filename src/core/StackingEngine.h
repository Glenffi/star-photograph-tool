#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 堆栈引擎
 *
 * 支持 Average 和 Median 两种堆栈算法。
 */
class StackingEngine {
public:
    enum Method {
        Average,    // 平均值（信噪比最高）
        Median      // 中位数（抗异常值，适合 ≤5 帧）
    };

    /**
     * @brief 堆栈多张图像
     *
     * @param images  已对齐的图像序列（16-bit 单通道，尺寸相同）
     * @param width   图像宽度
     * @param height  图像高度
     * @param method  堆栈算法
     * @param result  输出堆栈结果
     * @return true 成功
     */
    bool stack(const std::vector<std::vector<uint16_t>>& images,
               int width, int height,
               Method method,
               std::vector<uint16_t>& result);

private:
    void stackAverage(const std::vector<std::vector<uint16_t>>& images,
                      int w, int h, std::vector<uint16_t>& result);
    void stackMedian(const std::vector<std::vector<uint16_t>>& images,
                     int w, int h, std::vector<uint16_t>& result);
};
