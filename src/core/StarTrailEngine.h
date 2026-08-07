#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @brief Streaming RGB16 star-trail compositor for fixed-camera sequences.
 *
 * Frames must be supplied in capture order. The engine does no alignment and
 * never retains input frames; memory use is independent of frame count.
 */
class StarTrailEngine {
public:
    enum class Mode {
        Lighten,
        Comet
    };

    enum class TailDirection {
        // The newest position is brightest; older positions form the tail.
        Forward,

        // The first position is brightest; later positions form the tail.
        Reverse
    };

    struct Options {
        Mode mode = Mode::Lighten;
        TailDirection direction = TailDirection::Forward;

        // 0 is bit-exact Lighten. Larger values shorten the temporal tail.
        double cometStrength = 50.0;
    };

    enum class Error {
        None,
        NotInitialized,
        InvalidDimensions,
        SizeOverflow,
        InvalidOptions,
        InvalidFrameSize,
        NoFrames,
        FrameCountOverflow,
        AllocationFailure
    };

    struct Statistics {
        size_t processedFrames = 0;
        std::array<uint16_t, 3> referenceBackground{{0, 0, 0}};
    };

    /**
     * @brief Reset and prepare the engine for a sequence.
     *
     * Width and height describe interleaved R,G,B uint16_t frames. A failed
     * call leaves the engine reset and reports the reason through lastError().
     */
    bool initialize(int width, int height, const Options& options);
    bool initialize(int width, int height);

    /**
     * @brief Add one frame in capture order.
     *
     * The input is read only during this call and is not retained.
     */
    bool addFrame(const std::vector<uint16_t>& rgb);

    /** @brief Copy the current composite to rgb. */
    bool render(std::vector<uint16_t>& rgb);

    void reset() noexcept;

    const Statistics& statistics() const noexcept { return m_statistics; }
    Error lastError() const noexcept { return m_lastError; }
    bool isInitialized() const noexcept { return m_initialized; }

    static const char* errorMessage(Error error) noexcept;

private:
    bool usesLightenPath() const noexcept;
    std::array<uint16_t, 3> estimateBackground(
        const std::vector<uint16_t>& rgb);
    uint16_t channelMedian(const std::vector<uint16_t>& rgb, size_t channel);

    size_t m_valueCount = 0;
    Options m_options;
    Statistics m_statistics;
    Error m_lastError = Error::NotInitialized;
    bool m_initialized = false;

    std::vector<uint16_t> m_lightenComposite;
    std::vector<uint16_t> m_backgroundBase;
    std::vector<float> m_cometSignal;
    std::vector<size_t> m_histogram;
};
