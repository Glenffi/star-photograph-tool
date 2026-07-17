#include "ProcessingMemoryEstimator.h"

#include <limits>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

namespace {

constexpr uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

bool checkedMultiply(uint64_t left, uint64_t right, uint64_t& result) {
    if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left) return false;
    result = left * right;
    return true;
}

} // namespace

uint64_t ProcessingMemoryEstimator::estimatePeakBytes(int width, int height,
                                                       int frameCount,
                                                       bool skyGroundSeparation) {
    if (width <= 0 || height <= 0 || frameCount <= 0) return 0;

    uint64_t pixels = 0;
    uint64_t frameBytes = 0;
    if (!checkedMultiply(static_cast<uint64_t>(width), static_cast<uint64_t>(height), pixels) ||
        !checkedMultiply(pixels, 3ULL * sizeof(uint16_t), frameBytes)) {
        return 0;
    }

    // Frames are disk-backed and stacked in row chunks, so peak image memory no
    // longer grows with sequence length. These conservative equivalents cover
    // LibRaw decode, source/reference luminance, resampling and final output.
    const uint64_t frameEquivalents = skyGroundSeparation
        ? 10ULL : 8ULL;
    uint64_t estimate = 0;
    return checkedMultiply(frameBytes, frameEquivalents, estimate) ? estimate : 0;
}

uint64_t ProcessingMemoryEstimator::estimateScratchDiskBytes(
    int width, int height, int frameCount, bool skyGroundSeparation) {
    if (width <= 0 || height <= 0 || frameCount <= 0) return 0;
    uint64_t pixels = 0;
    uint64_t frameBytes = 0;
    uint64_t allFrames = 0;
    if (!checkedMultiply(static_cast<uint64_t>(width), static_cast<uint64_t>(height), pixels) ||
        !checkedMultiply(pixels, 3ULL * sizeof(uint16_t), frameBytes) ||
        !checkedMultiply(frameBytes, static_cast<uint64_t>(frameCount), allFrames)) {
        return 0;
    }
    if (!skyGroundSeparation) return allFrames;
    uint64_t doubled = 0;
    return checkedMultiply(allFrames, 2, doubled) ? doubled : 0;
}

uint64_t ProcessingMemoryEstimator::totalPhysicalMemoryBytes() {
#if defined(__APPLE__)
    uint64_t memory = 0;
    size_t size = sizeof(memory);
    return sysctlbyname("hw.memsize", &memory, &size, nullptr, 0) == 0 ? memory : 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#elif defined(__linux__)
    struct sysinfo info {};
    if (sysinfo(&info) != 0) return 0;
    uint64_t memory = 0;
    return checkedMultiply(static_cast<uint64_t>(info.totalram),
                           static_cast<uint64_t>(info.mem_unit), memory) ? memory : 0;
#else
    return 0;
#endif
}

uint64_t ProcessingMemoryEstimator::recommendedBudgetBytes() {
    const uint64_t physical = totalPhysicalMemoryBytes();
    if (physical == 0) return 8 * kGiB;
    return physical / 100 * 65;
}
