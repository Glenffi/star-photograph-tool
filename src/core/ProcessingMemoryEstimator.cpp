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

    // Normal stacking peaks while aligned RGB frames and their split channels
    // coexist. Sky/ground stacking additionally keeps original frames and a
    // second split-channel set. Fixed terms cover outputs and temporary frames.
    const uint64_t frameEquivalents = skyGroundSeparation
        ? static_cast<uint64_t>(frameCount) * 4 + 6
        : static_cast<uint64_t>(frameCount) * 2 + 4;
    uint64_t estimate = 0;
    return checkedMultiply(frameBytes, frameEquivalents, estimate) ? estimate : 0;
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
