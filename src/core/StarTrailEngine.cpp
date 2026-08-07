#include "StarTrailEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace {

constexpr size_t kRgbChannels = 3;
constexpr size_t kHistogramBins = 65536;

bool checkedRgbSize(int width, int height, size_t& valueCount) {
    if (width <= 0 || height <= 0) return false;

    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h) return false;

    const size_t pixelCount = w * h;
    if (pixelCount > std::numeric_limits<size_t>::max() / kRgbChannels) {
        return false;
    }
    valueCount = pixelCount * kRgbChannels;
    return true;
}

bool validOptions(const StarTrailEngine::Options& options) {
    const bool validMode = options.mode == StarTrailEngine::Mode::Lighten ||
        options.mode == StarTrailEngine::Mode::Comet;
    const bool validDirection =
        options.direction == StarTrailEngine::TailDirection::Forward ||
        options.direction == StarTrailEngine::TailDirection::Reverse;
    return validMode && validDirection &&
        std::isfinite(options.cometStrength) &&
        options.cometStrength >= 0.0 && options.cometStrength <= 100.0;
}

float cometRetention(double strength) {
    // Map the UI control to a temporal half-life instead of a raw multiplier.
    // A linear multiplier made most of the useful range collapse into one or
    // two visible frames. Here low strength keeps a long gentle taper, while
    // 100% still halves the previous signal on every frame.
    const double normalized = strength / 100.0;
    const double halfLifeFrames = 1.0 + 23.0 * (1.0 - normalized);
    return static_cast<float>(
        std::pow(0.5, 1.0 / halfLifeFrames));
}

uint16_t clampRgb16(double value) {
    return static_cast<uint16_t>(
        std::clamp(std::lround(value), 0L, 65535L));
}

} // namespace

bool StarTrailEngine::initialize(int width, int height,
                                 const Options& options) {
    reset();

    size_t valueCount = 0;
    if (width <= 0 || height <= 0) {
        m_lastError = Error::InvalidDimensions;
        return false;
    }
    if (!checkedRgbSize(width, height, valueCount) ||
        valueCount > std::vector<uint16_t>().max_size() ||
        valueCount > std::vector<float>().max_size()) {
        m_lastError = Error::SizeOverflow;
        return false;
    }
    if (!validOptions(options)) {
        m_lastError = Error::InvalidOptions;
        return false;
    }

    try {
        m_histogram.resize(kHistogramBins);
        if (options.mode == Mode::Lighten || options.cometStrength == 0.0) {
            m_lightenComposite.resize(valueCount);
        } else {
            m_backgroundBase.resize(valueCount);
            m_cometSignal.resize(valueCount);
        }
    } catch (const std::bad_alloc&) {
        reset();
        m_lastError = Error::AllocationFailure;
        return false;
    } catch (const std::length_error&) {
        reset();
        m_lastError = Error::SizeOverflow;
        return false;
    }

    m_valueCount = valueCount;
    m_options = options;
    m_initialized = true;
    m_lastError = Error::None;
    return true;
}

bool StarTrailEngine::initialize(int width, int height) {
    return initialize(width, height, Options{});
}

bool StarTrailEngine::addFrame(const std::vector<uint16_t>& rgb) {
    if (!m_initialized) {
        m_lastError = Error::NotInitialized;
        return false;
    }
    if (rgb.size() != m_valueCount) {
        m_lastError = Error::InvalidFrameSize;
        return false;
    }
    if (m_statistics.processedFrames == std::numeric_limits<size_t>::max()) {
        m_lastError = Error::FrameCountOverflow;
        return false;
    }

    const bool firstFrame = m_statistics.processedFrames == 0;
    if (firstFrame) {
        m_statistics.referenceBackground = estimateBackground(rgb);
    }

    if (usesLightenPath()) {
        if (firstFrame) {
            std::copy(rgb.begin(), rgb.end(), m_lightenComposite.begin());
        } else {
            for (size_t index = 0; index < m_valueCount; ++index) {
                m_lightenComposite[index] =
                    std::max(m_lightenComposite[index], rgb[index]);
            }
        }
    } else {
        const std::array<uint16_t, 3> frameBackground = firstFrame
            ? m_statistics.referenceBackground
            : estimateBackground(rgb);
        const float retention = cometRetention(m_options.cometStrength);

        if (!firstFrame && m_options.direction == TailDirection::Forward) {
            for (float& signal : m_cometSignal) signal *= retention;
        }

        float frameWeight = 1.0f;
        if (!firstFrame && m_options.direction == TailDirection::Reverse) {
            frameWeight = std::pow(
                retention, static_cast<float>(m_statistics.processedFrames));
        }

        for (size_t index = 0; index < m_valueCount; ++index) {
            const size_t channel = index % kRgbChannels;
            if (firstFrame) {
                // Preserve first-frame low-frequency texture below the robust
                // channel background while separating bright trail signal.
                m_backgroundBase[index] = std::min(
                    rgb[index], m_statistics.referenceBackground[channel]);
            }
            const float signal = static_cast<float>(
                std::max<int>(0, static_cast<int>(rgb[index]) -
                                     static_cast<int>(frameBackground[channel]))) *
                frameWeight;
            m_cometSignal[index] = std::max(m_cometSignal[index], signal);
        }
    }

    ++m_statistics.processedFrames;
    m_lastError = Error::None;
    return true;
}

bool StarTrailEngine::render(std::vector<uint16_t>& rgb) {
    rgb.clear();
    if (!m_initialized) {
        m_lastError = Error::NotInitialized;
        return false;
    }
    if (m_statistics.processedFrames == 0) {
        m_lastError = Error::NoFrames;
        return false;
    }

    try {
        if (usesLightenPath()) {
            rgb = m_lightenComposite;
        } else {
            rgb.resize(m_valueCount);
            for (size_t index = 0; index < m_valueCount; ++index) {
                rgb[index] = clampRgb16(
                    static_cast<double>(m_backgroundBase[index]) +
                    m_cometSignal[index]);
            }
        }
    } catch (const std::bad_alloc&) {
        rgb.clear();
        m_lastError = Error::AllocationFailure;
        return false;
    } catch (const std::length_error&) {
        rgb.clear();
        m_lastError = Error::SizeOverflow;
        return false;
    }

    m_lastError = Error::None;
    return true;
}

void StarTrailEngine::reset() noexcept {
    m_valueCount = 0;
    m_options = Options{};
    m_statistics = Statistics{};
    m_initialized = false;
    m_lightenComposite.clear();
    m_backgroundBase.clear();
    m_cometSignal.clear();
    m_histogram.clear();
    m_lastError = Error::NotInitialized;
}

bool StarTrailEngine::usesLightenPath() const noexcept {
    return m_options.mode == Mode::Lighten || m_options.cometStrength == 0.0;
}

std::array<uint16_t, 3> StarTrailEngine::estimateBackground(
    const std::vector<uint16_t>& rgb) {
    return {{channelMedian(rgb, 0), channelMedian(rgb, 1),
             channelMedian(rgb, 2)}};
}

uint16_t StarTrailEngine::channelMedian(const std::vector<uint16_t>& rgb,
                                        size_t channel) {
    std::fill(m_histogram.begin(), m_histogram.end(), size_t{0});
    for (size_t index = channel; index < rgb.size(); index += kRgbChannels) {
        ++m_histogram[rgb[index]];
    }

    const size_t pixelCount = rgb.size() / kRgbChannels;
    const size_t lowerRank = (pixelCount - 1) / 2;
    const size_t upperRank = pixelCount / 2;
    size_t cumulative = 0;
    uint16_t lowerValue = 0;
    uint16_t upperValue = 0;
    bool lowerFound = false;
    for (size_t value = 0; value < m_histogram.size(); ++value) {
        cumulative += m_histogram[value];
        if (!lowerFound && cumulative > lowerRank) {
            lowerValue = static_cast<uint16_t>(value);
            lowerFound = true;
        }
        if (cumulative > upperRank) {
            upperValue = static_cast<uint16_t>(value);
            break;
        }
    }
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(lowerValue) + upperValue) / 2);
}

const char* StarTrailEngine::errorMessage(Error error) noexcept {
    switch (error) {
    case Error::None: return "success";
    case Error::NotInitialized: return "engine is not initialized";
    case Error::InvalidDimensions: return "width and height must be positive";
    case Error::SizeOverflow: return "RGB image size overflows addressable memory";
    case Error::InvalidOptions: return "star-trail options are outside their valid ranges";
    case Error::InvalidFrameSize: return "frame size does not match width * height * 3";
    case Error::NoFrames: return "no frames have been added";
    case Error::FrameCountOverflow: return "processed frame count overflow";
    case Error::AllocationFailure: return "unable to allocate star-trail buffers";
    }
    return "unknown star-trail error";
}
