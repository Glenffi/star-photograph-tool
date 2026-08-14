#pragma once

#include <array>
#include <cstdint>
#include <QString>
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

    struct CfaImageData {
        // Sensor-space Bayer samples before black subtraction, white balance,
        // demosaic or camera-to-sRGB conversion. Calibration must happen here.
        std::vector<uint16_t> data;
        int width = 0;
        int height = 0;
        int rawWidth = 0;
        int rawHeight = 0;
        int topMargin = 0;
        int leftMargin = 0;
        int iso = 0;
        double exposureTime = 0.0;
        std::string cameraModel;
        std::array<uint8_t, 4> cfaPattern = {};
        uint16_t blackLevel = 0;
        uint16_t saturation = 0; // Usable range after calibration removed black.
    };

    // Quality tiers are deliberately separate:
    // - loadMetadata(): RAW header only; never unpacks or demosaics pixels.
    // - loadPreview(): embedded preview first, then a fast half-size fallback.
    // - loadRaw(): full-resolution AHD with conservative clipped-highlight
    //   blending, reserved for the processing pipeline.
    // Keep paths as QString until they reach LibRaw. On Windows this lets the
    // implementation use LibRaw's wchar_t overload for non-ASCII file names.
    bool loadMetadata(const QString& filePath, Metadata& out);
    bool loadPreview(const QString& filePath, int requestedMaxSize,
                     PreviewData& out, Metadata* metadata = nullptr);
    bool loadRaw(const QString& filePath, ImageData& out);

    // Deep-sky calibration path. Only repeating 2x2 Bayer sensors are accepted
    // for now; X-Trans and already-demosaiced RAW variants fail explicitly.
    bool loadRawCfa(const QString& filePath, CfaImageData& out);
    bool processCalibratedCfa(const QString& filePath,
                              const CfaImageData& calibrated,
                              ImageData& out);
};
