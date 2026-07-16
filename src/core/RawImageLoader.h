#pragma once

#include <cstdint>
#include <string>
#include <vector>

class RawImageLoader {
public:
    struct Metadata {
        int width = 0;
        int height = 0;
        int iso = 0;
        double exposureTime = 0.0;
        double aperture = 0.0;
        int focalLength = 0;
        std::string cameraModel;
        std::string timestamp;
    };

    struct PreviewData {
        enum class Encoding {
            Jpeg,  // bytes contains a complete encoded JPEG image
            Rgb8   // bytes contains tightly packed width * height * 3 RGB data
        };

        std::vector<uint8_t> bytes;
        int width = 0;
        int height = 0;
        Encoding encoding = Encoding::Rgb8;
    };

    struct ImageData {
        // Full-quality processing buffer: linear sRGB primaries, 16-bit RGB.
        std::vector<uint16_t> data;
        int width = 0;
        int height = 0;
        int channels = 0;              // Always 3 (RGB).
        std::string bayerPattern;       // Deprecated; always empty.
        int iso = 0;
        double exposureTime = 0.0;
        double aperture = 0.0;
        int focalLength = 0;
        std::string cameraModel;
        std::string timestamp;
    };

    // Quality tiers are deliberately separate:
    // - loadMetadata(): RAW header only; never unpacks or demosaics pixels.
    // - loadPreview(): embedded preview first, then a fast half-size fallback.
    // - loadRaw(): full-resolution AHD, reserved for the processing pipeline.
    bool loadMetadata(const std::string& filePath, Metadata& out);
    bool loadPreview(const std::string& filePath, int requestedMaxSize,
                     PreviewData& out, Metadata* metadata = nullptr);
    bool loadRaw(const std::string& filePath, ImageData& out);
};
