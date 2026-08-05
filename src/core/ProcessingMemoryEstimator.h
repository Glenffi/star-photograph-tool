#pragma once

#include <cstdint>

class ProcessingMemoryEstimator {
public:
    struct SystemMemoryInfo {
        uint64_t totalBytes = 0;
        uint64_t availableBytes = 0;
        uint64_t safeBudgetBytes = 0;
    };

    struct EstimateOptions {
        int frameCount = 0;
        int chunkRows = 32;
        bool skyGroundSeparation = false;
        bool noiseReduction = false;
        bool dehaze = false;
        bool stretch = false;
        bool starReduction = false;
    };

    // Estimates the peak resident image buffers used by the current in-memory
    // pipeline. Returns 0 when dimensions or arithmetic are invalid.
    static uint64_t estimatePeakBytes(int width, int height, int frameCount,
                                      bool skyGroundSeparation);
    static uint64_t estimatePeakBytes(int width, int height,
                                      const EstimateOptions& options);

    // A temporal target owns the decoded target, aligned sky samples and,
    // when requested, independent unaligned ground samples for one window.
    static uint64_t estimateTimelapsePeakBytes(int width, int height,
                                               int windowSize,
                                               bool protectGround);

    // Disk-backed alignment stores one RGB16 frame per accepted input, or two
    // when sky/ground separation also preserves the unaligned originals.
    static uint64_t estimateScratchDiskBytes(int width, int height, int frameCount,
                                             bool skyGroundSeparation);

    // Caps work at 65% of physical RAM. When current availability is known,
    // it first reserves max(1 GiB, 10% of RAM), then uses 85% of the remainder.
    // Falls back to the physical-RAM cap when availability cannot be queried.
    static uint64_t recommendedBudgetBytes();
    static uint64_t totalPhysicalMemoryBytes();
    static uint64_t availablePhysicalMemoryBytes();
    static SystemMemoryInfo systemMemoryInfo();

    // Pure helper keeps the headroom policy directly testable.
    static uint64_t calculateSafeBudgetBytes(uint64_t totalBytes,
                                             uint64_t availableBytes);
    static uint64_t calculateEffectiveBudgetBytes(uint64_t platformBudgetBytes,
                                                  uint64_t userLimitBytes);
};
