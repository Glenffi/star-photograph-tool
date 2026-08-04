#pragma once

#include <cstdint>
#include <vector>

struct PreviewImage8 {
    std::vector<uint8_t> rgb;
    int width = 0;
    int height = 0;
    uint16_t blackPoint = 0;
    uint16_t whitePoint = 65535;
};

// Converts linear 16-bit processing data into a bounded 8-bit display preview.
// The shared RGB curve preserves channel ratios and keeps preview memory bounded.
class PreviewToneMapper {
public:
    static PreviewImage8 mapMono16(const std::vector<uint16_t>& data,
                                   int width,
                                   int height,
                                   int maxLongSide = 4096);

    static PreviewImage8 mapRgb16(const std::vector<uint16_t>& rgb,
                                  int width,
                                  int height,
                                  int maxLongSide = 4096);

    // Uses an explicit display range so related images can be compared under
    // the same transfer curve instead of receiving independent auto-stretches.
    static PreviewImage8 mapRgb16WithRange(const std::vector<uint16_t>& rgb,
                                           int width,
                                           int height,
                                           uint16_t blackPoint,
                                           uint16_t whitePoint,
                                           int maxLongSide = 4096);
};
