#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @brief 对已对齐、已完成光度匹配的 RGB16 序列做时域降噪。
 *
 * TimelapseEngine 不负责 RAW 解码、几何对齐或曝光匹配。调用方应按时间顺序
 * 提供帧，并把每帧到目标帧的相对时间写入 FrameView::timeDistance。
 */
class TimelapseEngine {
public:
    /**
     * @brief 一帧只读、交错排列的 RGB16 图像视图。
     *
     * rgb 的排列必须是 R,G,B,R,G,B...；valueCount 必须严格等于
     * width * height * 3。指针只需在 denoise() 调用期间保持有效。
     */
    struct FrameView {
        const uint16_t* rgb = nullptr;
        size_t valueCount = 0;
        double timeDistance = 0.0;
    };

    struct Options {
        // 以目标帧为中心的最大窗口，必须是 >= 3 的奇数。序列边缘允许少帧。
        size_t windowSize = 5;

        // 高斯时间权重 exp(-0.5 * (distance / sigma)^2) 中的 sigma。
        // 单位与 FrameView::timeDistance 相同，例如以帧为单位时可使用 1.5。
        double temporalSigma = 1.5;

        // 接受区间为 median +/- madThreshold * robustSigma。
        double madThreshold = 3.0;

        // MAD 为零或样本很少时的噪声下限，单位为 RGB16 DN。
        // 它避免轻微量化波动被当成异常值，同时仍能排除热像素等强离群点。
        double minimumDeviation = 8.0;

        // 0 完全保留目标帧，100 完全使用时域估计，中间值连续线性混合。
        double strength = 50.0;

        // 0 使用纯时间权重；100 会在与目标帧结构明显不同的区域强烈降低
        // 邻帧贡献。运动判断基于共享亮度引导图，因此不会让 RGB 通道分离。
        double motionProtection = 75.0;
    };

    enum class Error {
        None,
        EmptyInput,
        InvalidDimensions,
        SizeOverflow,
        InvalidTargetIndex,
        InvalidWindowSize,
        InvalidOptions,
        InvalidFrame,
        InvalidTimeDistance,
        InvalidTimeOrder
    };

    struct Result {
        Error error = Error::None;
        std::vector<uint16_t> rgb;

        // 实际使用的连续窗口，表示为 [firstFrameIndex, endFrameIndex)。
        size_t firstFrameIndex = 0;
        size_t frameCount = 0;
        size_t motionProtectedPixels = 0;

        bool ok() const noexcept { return error == Error::None; }
        explicit operator bool() const noexcept { return ok(); }
    };

    /**
     * @brief 生成指定目标帧的时域降噪结果。
     *
     * frames 必须按时间升序排列，targetFrameIndex 对应帧的 timeDistance
     * 必须为 0。非目标帧中，重采样产生的全 RGB 零像素不会参与统计；如果
     * 某个像素没有可用时域估计，则无条件回退到目标帧原值。
     *
     * 返回值拥有输出数据，因此输入帧可以来自 vector、映射文件或其他只读
     * 缓冲区。校验失败时 rgb 为空，errorMessage() 可用于日志或 UI 提示。
     */
    static Result denoise(const std::vector<FrameView>& frames,
                          int width,
                          int height,
                          size_t targetFrameIndex,
                          const Options& options);

    /** @brief 使用默认 Options 处理目标帧。 */
    static Result denoise(const std::vector<FrameView>& frames,
                          int width,
                          int height,
                          size_t targetFrameIndex);

    static const char* errorMessage(Error error) noexcept;
};
