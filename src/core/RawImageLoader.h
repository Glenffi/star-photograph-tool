#pragma once

#include <cstdint>
#include <string>
#include <vector>

class RawImageLoader {
public:
    struct ImageData {
        std::vector<uint16_t> data;     // Bayer CFA 或 RGB 数据
        int width = 0;
        int height = 0;
        int channels = 0;              // 1=Bayer, 3=RGB
        std::string bayerPattern;        // RGGB/BGGR/GRBG/GBRG
        // EXIF 元数据
        int iso = 0;
        double exposureTime = 0.0;
        double aperture = 0.0;
        int focalLength = 0;
        std::string cameraModel;
        std::string timestamp;
    };
    
    bool loadRaw(const std::string& filePath, ImageData& out);
    bool decodeToRgb(const ImageData& bayer, std::vector<uint16_t>& rgb);
    bool generateThumbnail(const ImageData& raw, int maxSize, std::vector<uint8_t>& thumb);
};