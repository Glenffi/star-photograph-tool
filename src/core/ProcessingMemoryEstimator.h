#pragma once

#include <cstdint>

class ProcessingMemoryEstimator {
public:
    // Estimates the peak resident image buffers used by the current in-memory
    // pipeline. Returns 0 when dimensions or arithmetic are invalid.
    static uint64_t estimatePeakBytes(int width, int height, int frameCount,
                                      bool skyGroundSeparation);

    // Uses 65% of physical RAM so the OS, Qt and LibRaw retain working space.
    // Falls back to 8 GiB when the platform query is unavailable.
    static uint64_t recommendedBudgetBytes();
    static uint64_t totalPhysicalMemoryBytes();
};
