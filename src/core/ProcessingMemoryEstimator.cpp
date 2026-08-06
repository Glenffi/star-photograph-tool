#include "ProcessingMemoryEstimator.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
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
constexpr uint64_t kComparisonPreviewWorkingBytes = 32ULL * 1024ULL * 1024ULL;

bool checkedMultiply(uint64_t left, uint64_t right, uint64_t& result) {
    if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left) return false;
    result = left * right;
    return true;
}

uint64_t percentageOf(uint64_t value, uint64_t percentage) {
    return value / 100 * percentage + (value % 100) * percentage / 100;
}

} // namespace

uint64_t ProcessingMemoryEstimator::estimatePeakBytes(int width, int height,
                                                       int frameCount,
                                                       bool skyGroundSeparation) {
    EstimateOptions options;
    options.frameCount = frameCount;
    options.skyGroundSeparation = skyGroundSeparation;
    return estimatePeakBytes(width, height, options);
}

uint64_t ProcessingMemoryEstimator::estimatePeakBytes(
    int width, int height, const EstimateOptions& options) {
    if (width <= 0 || height <= 0 || options.frameCount <= 0 ||
        options.chunkRows <= 0) {
        return 0;
    }

    uint64_t pixels = 0;
    uint64_t frameBytes = 0;
    if (!checkedMultiply(static_cast<uint64_t>(width), static_cast<uint64_t>(height), pixels) ||
        !checkedMultiply(pixels, 3ULL * sizeof(uint16_t), frameBytes)) {
        return 0;
    }

    // Decode/alignment keeps only a reference and current source frame resident.
    uint64_t alignmentPeak = 0;
    if (!checkedMultiply(frameBytes,
                         options.skyGroundSeparation ? 10ULL : 8ULL,
                         alignmentPeak)) {
        return 0;
    }

    // Disk-backed stacking is frame-count dependent only within one row chunk.
    const uint64_t rows = static_cast<uint64_t>(
        std::min(options.chunkRows, height));
    uint64_t chunkValues = 0;
    uint64_t chunkBytesPerFrame = 0;
    uint64_t cachedChunkBytes = 0;
    if (!checkedMultiply(static_cast<uint64_t>(width), rows, chunkValues) ||
        !checkedMultiply(chunkValues, 3ULL * sizeof(uint16_t),
                         chunkBytesPerFrame) ||
        !checkedMultiply(chunkBytesPerFrame,
                         static_cast<uint64_t>(options.frameCount),
                         cachedChunkBytes)) {
        return 0;
    }
    uint64_t stackingPeak = frameBytes;
    const uint64_t chunkCopies = options.skyGroundSeparation ? 4ULL : 1ULL;
    uint64_t allChunkCopies = 0;
    if (!checkedMultiply(cachedChunkBytes, chunkCopies, allChunkCopies) ||
        stackingPeak > std::numeric_limits<uint64_t>::max() - allChunkCopies) {
        return 0;
    }
    stackingPeak += allChunkCopies;

    uint64_t postProcessPeak = 0;
    if (!checkedMultiply(frameBytes, 2, postProcessPeak)) return 0;
    const bool createsComparisonPreview = options.skyGroundSeparation ||
        options.noiseReduction || options.dehaze || options.stretch ||
        options.starReduction;
    if (createsComparisonPreview) {
        if (frameBytes > std::numeric_limits<uint64_t>::max() -
                             kComparisonPreviewWorkingBytes) {
            return 0;
        }
        // The bounded RGB8 vector and its owned QImage briefly coexist while
        // the full-resolution stack remains resident.
        postProcessPeak = std::max(
            postProcessPeak, frameBytes + kComparisonPreviewWorkingBytes);
    }
    auto includeFrameEquivalents = [&](bool enabled, uint64_t equivalents) {
        if (!enabled) return true;
        uint64_t stage = 0;
        if (!checkedMultiply(frameBytes, equivalents, stage)) return false;
        postProcessPeak = std::max(postProcessPeak, stage);
        return true;
    };
    // Guided-filter dehaze owns many full-resolution float planes. The
    // equivalent counts include caller RGB/channel buffers and library margin.
    if (!includeFrameEquivalents(options.noiseReduction, 6ULL) ||
        !includeFrameEquivalents(options.dehaze, 15ULL) ||
        !includeFrameEquivalents(options.stretch, 4ULL) ||
        // Starless RGB, signed RGB star layer and its eroded working buffer
        // coexist with the caller-owned image and luminance planes.
        !includeFrameEquivalents(options.starReduction, 8ULL)) {
        return 0;
    }
    return std::max({alignmentPeak, stackingPeak, postProcessPeak});
}

uint64_t ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
    int width, int height, int windowSize, bool protectGround,
    bool motionProtection) {
    if (width <= 0 || height <= 0 || windowSize < 3 ||
        windowSize % 2 == 0) {
        return 0;
    }
    uint64_t pixels = 0;
    uint64_t frameBytes = 0;
    if (!checkedMultiply(static_cast<uint64_t>(width),
                         static_cast<uint64_t>(height), pixels) ||
        !checkedMultiply(pixels, 3ULL * sizeof(uint16_t), frameBytes)) {
        return 0;
    }
    uint64_t residentFrames = protectGround
        ? static_cast<uint64_t>(windowSize) * 2ULL + 4ULL
        : static_cast<uint64_t>(windowSize) + 4ULL;
    // The half-resolution uint16 motion guides for one active window consume
    // less than one RGB16 frame; reserve a full equivalent for allocator and
    // edge-window overhead.
    if (motionProtection) ++residentFrames;
    uint64_t peakBytes = 0;
    return checkedMultiply(frameBytes, residentFrames, peakBytes)
        ? peakBytes : 0;
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

uint64_t ProcessingMemoryEstimator::availablePhysicalMemoryBytes() {
#if defined(__APPLE__)
    mach_port_t host = mach_host_self();
    vm_size_t pageSize = 0;
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const bool queried = host_page_size(host, &pageSize) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&statistics),
                          &count) == KERN_SUCCESS;
    mach_port_deallocate(mach_task_self(), host);
    if (!queried) return 0;
    const uint64_t reclaimablePages =
        static_cast<uint64_t>(statistics.free_count) +
        static_cast<uint64_t>(statistics.inactive_count);
    uint64_t available = 0;
    return checkedMultiply(reclaimablePages, static_cast<uint64_t>(pageSize),
                           available) ? available : 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys : 0;
#elif defined(__linux__)
    std::ifstream memoryInfo("/proc/meminfo");
    std::string key;
    uint64_t valueKiB = 0;
    std::string unit;
    while (memoryInfo >> key >> valueKiB >> unit) {
        if (key != "MemAvailable:") continue;
        uint64_t available = 0;
        return checkedMultiply(valueKiB, 1024, available) ? available : 0;
    }
    struct sysinfo info {};
    if (sysinfo(&info) != 0) return 0;
    const uint64_t freeUnits = static_cast<uint64_t>(info.freeram);
    const uint64_t bufferUnits = static_cast<uint64_t>(info.bufferram);
    if (freeUnits > std::numeric_limits<uint64_t>::max() - bufferUnits) return 0;
    uint64_t available = 0;
    return checkedMultiply(freeUnits + bufferUnits,
                           static_cast<uint64_t>(info.mem_unit),
                           available) ? available : 0;
#else
    return 0;
#endif
}

uint64_t ProcessingMemoryEstimator::calculateSafeBudgetBytes(
    uint64_t totalBytes, uint64_t availableBytes) {
    const uint64_t totalLimit = totalBytes > 0
        ? percentageOf(totalBytes, 65) : 8 * kGiB;
    if (availableBytes == 0) return totalLimit;
    const uint64_t systemReserve = std::max(
        kGiB, totalBytes > 0 ? percentageOf(totalBytes, 10) : kGiB);
    const uint64_t dynamicAvailable =
        availableBytes > systemReserve ? availableBytes - systemReserve : 0;
    return std::min(totalLimit, percentageOf(dynamicAvailable, 85));
}

uint64_t ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
    uint64_t platformBudgetBytes, uint64_t userLimitBytes) {
    return userLimitBytes > 0
        ? std::min(platformBudgetBytes, userLimitBytes)
        : platformBudgetBytes;
}

ProcessingMemoryEstimator::SystemMemoryInfo
ProcessingMemoryEstimator::systemMemoryInfo() {
    SystemMemoryInfo info;
    info.totalBytes = totalPhysicalMemoryBytes();
    info.availableBytes = availablePhysicalMemoryBytes();
    info.safeBudgetBytes =
        calculateSafeBudgetBytes(info.totalBytes, info.availableBytes);
    return info;
}

uint64_t ProcessingMemoryEstimator::recommendedBudgetBytes() {
    return systemMemoryInfo().safeBudgetBytes;
}
