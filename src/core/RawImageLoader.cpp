#include "RawImageLoader.h"
#include <libraw/libraw.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

bool RawImageLoader::loadRaw(const std::string& filePath, ImageData& out) {
    // 检查文件是否存在
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filePath << std::endl;
        return false;
    }
    file.close();

    LibRaw processor;
    
    int ret = processor.open_file(filePath.c_str());
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw open_file 失败: " << filePath << " (code: " << ret << ")" << std::endl;
        return false;
    }
    
    ret = processor.unpack();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw unpack 失败: " << filePath << " (code: " << ret << ")" << std::endl;
        return false;
    }
    
    out.width = processor.imgdata.sizes.width;
    out.height = processor.imgdata.sizes.height;
    
    // 获取 black level 和最大有效值（用于后续归一化）
    unsigned blackLevel = processor.imgdata.color.black;
    unsigned dataMaximum = processor.imgdata.color.data_maximum;
    if (dataMaximum == 0) dataMaximum = 65535;
    if (blackLevel > dataMaximum) blackLevel = 0;
    
    // 判断是否为 Bayer 模式
    if (processor.imgdata.idata.filters != 0) {
        // Bayer CFA 数据
        out.channels = 1;
        
        // 解析 Bayer 模式
        const char* cdesc = processor.imgdata.idata.cdesc;
        if (cdesc && cdesc[0] != '\0') {
            out.bayerPattern = std::string(cdesc, 4);
        } else {
            // 根据 filters 推断
            unsigned filters = processor.imgdata.idata.filters;
            if (filters == 0x94949494) {
                out.bayerPattern = "RGGB";
            } else if (filters == 0x49494949) {
                out.bayerPattern = "BGGR";
            } else if (filters == 0x61616161) {
                out.bayerPattern = "GRBG";
            } else if (filters == 0x16161616) {
                out.bayerPattern = "GBRG";
            } else {
                out.bayerPattern = "RGGB"; // 默认
            }
        }
        
        // 复制 Bayer 数据
        int rawWidth = processor.imgdata.sizes.raw_width;
        // int rawHeight = processor.imgdata.sizes.raw_height;
        
        // 使用 imgdata.rawdata.raw_image 或从彩色数据中提取
        if (processor.imgdata.rawdata.raw_image) {
            out.data.resize(out.width * out.height);
            // 注意：raw_image 可能包含边框，需要裁剪到有效区域
            int topMargin = processor.imgdata.sizes.top_margin;
            int leftMargin = processor.imgdata.sizes.left_margin;
            
            for (int y = 0; y < out.height; ++y) {
                for (int x = 0; x < out.width; ++x) {
                    int srcIdx = (y + topMargin) * rawWidth + (x + leftMargin);
                    uint16_t val = processor.imgdata.rawdata.raw_image[srcIdx];
                    // 应用 black level 校正并裁剪到有效范围
                    if (val > blackLevel) val -= blackLevel; else val = 0;
                    out.data[y * out.width + x] = val;
                }
            }
        } else {
            std::cerr << "LibRaw raw_image 不可用: " << filePath << std::endl;
            return false;
        }
    } else {
        // 已经是 RGB 数据（如 Foveon 传感器或 DNG）
        out.channels = 3;
        out.bayerPattern = "";
        
        // 复制 RGB 数据
        out.data.resize(out.width * out.height * 3);
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                int idx = y * out.width + x;
                for (int c = 0; c < 3; ++c) {
                    uint16_t val = processor.imgdata.image[idx][c];
                    if (val > blackLevel) val -= blackLevel; else val = 0;
                    out.data[idx * 3 + c] = val;
                }
            }
        }
    }
    
    // 提取 EXIF 元数据
    out.iso = static_cast<int>(processor.imgdata.other.iso_speed);
    out.exposureTime = processor.imgdata.other.shutter;
    out.aperture = processor.imgdata.other.aperture;
    out.focalLength = static_cast<int>(processor.imgdata.other.focal_len);
    out.cameraModel = processor.imgdata.idata.model;
    
    // 时间戳
    auto& other = processor.imgdata.other;
    if (other.timestamp > 0) {
        char buf[64];
        time_t t = static_cast<time_t>(other.timestamp);
        struct tm* tm_info = localtime(&t);
        if (tm_info) {
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
            out.timestamp = buf;
        }
    }
    
    processor.recycle();
    return true;
}

bool RawImageLoader::decodeToRgb(const ImageData& bayer, std::vector<uint16_t>& rgb) {
    if (bayer.channels != 1 || bayer.data.empty()) {
        std::cerr << "decodeToRgb: 输入不是有效的 Bayer 数据" << std::endl;
        return false;
    }
    
    rgb.resize(bayer.width * bayer.height * 3);
    
    // 根据 Bayer 模式预计算每个位置的颜色
    // 0=R, 1=G, 2=B
    uint8_t colorMap[4];
    if (bayer.bayerPattern == "RGGB") {
        colorMap[0] = 0; colorMap[1] = 1; colorMap[2] = 1; colorMap[3] = 2;
    } else if (bayer.bayerPattern == "BGGR") {
        colorMap[0] = 2; colorMap[1] = 1; colorMap[2] = 1; colorMap[3] = 0;
    } else if (bayer.bayerPattern == "GRBG") {
        colorMap[0] = 1; colorMap[1] = 0; colorMap[2] = 2; colorMap[3] = 1;
    } else if (bayer.bayerPattern == "GBRG") {
        colorMap[0] = 1; colorMap[1] = 2; colorMap[2] = 0; colorMap[3] = 1;
    } else {
        // 不支持的模式，回退到简单复制
        for (size_t i = 0; i < bayer.data.size(); ++i) {
            rgb[i * 3 + 0] = bayer.data[i];
            rgb[i * 3 + 1] = bayer.data[i];
            rgb[i * 3 + 2] = bayer.data[i];
        }
        return true;
    }
    
    auto getColorAt = [&](int x, int y) -> uint8_t {
        return colorMap[((y & 1) * 2) + (x & 1)];
    };
    
    auto pixelAt = [&](int x, int y) -> uint16_t {
        return bayer.data[y * bayer.width + x];
    };
    
    for (int y = 0; y < bayer.height; ++y) {
        for (int x = 0; x < bayer.width; ++x) {
            int idx = y * bayer.width + x;
            uint8_t color = getColorAt(x, y);
            
            if (color == 0) { // R 位置
                rgb[idx * 3 + 0] = pixelAt(x, y);
                uint32_t gSum = 0, gCount = 0;
                if (x > 0) { gSum += pixelAt(x - 1, y); gCount++; }
                if (x + 1 < bayer.width) { gSum += pixelAt(x + 1, y); gCount++; }
                if (y > 0) { gSum += pixelAt(x, y - 1); gCount++; }
                if (y + 1 < bayer.height) { gSum += pixelAt(x, y + 1); gCount++; }
                rgb[idx * 3 + 1] = gCount > 0 ? gSum / gCount : pixelAt(x, y);
                
                uint32_t bSum = 0, bCount = 0;
                if (x > 0 && y > 0) { bSum += pixelAt(x - 1, y - 1); bCount++; }
                if (x + 1 < bayer.width && y > 0) { bSum += pixelAt(x + 1, y - 1); bCount++; }
                if (x > 0 && y + 1 < bayer.height) { bSum += pixelAt(x - 1, y + 1); bCount++; }
                if (x + 1 < bayer.width && y + 1 < bayer.height) { bSum += pixelAt(x + 1, y + 1); bCount++; }
                rgb[idx * 3 + 2] = bCount > 0 ? bSum / bCount : pixelAt(x, y);
            } else if (color == 2) { // B 位置
                rgb[idx * 3 + 2] = pixelAt(x, y);
                uint32_t gSum = 0, gCount = 0;
                if (x > 0) { gSum += pixelAt(x - 1, y); gCount++; }
                if (x + 1 < bayer.width) { gSum += pixelAt(x + 1, y); gCount++; }
                if (y > 0) { gSum += pixelAt(x, y - 1); gCount++; }
                if (y + 1 < bayer.height) { gSum += pixelAt(x, y + 1); gCount++; }
                rgb[idx * 3 + 1] = gCount > 0 ? gSum / gCount : pixelAt(x, y);
                
                uint32_t rSum = 0, rCount = 0;
                if (x > 0 && y > 0) { rSum += pixelAt(x - 1, y - 1); rCount++; }
                if (x + 1 < bayer.width && y > 0) { rSum += pixelAt(x + 1, y - 1); rCount++; }
                if (x > 0 && y + 1 < bayer.height) { rSum += pixelAt(x - 1, y + 1); rCount++; }
                if (x + 1 < bayer.width && y + 1 < bayer.height) { rSum += pixelAt(x + 1, y + 1); rCount++; }
                rgb[idx * 3 + 0] = rCount > 0 ? rSum / rCount : pixelAt(x, y);
            } else { // G 位置
                rgb[idx * 3 + 1] = pixelAt(x, y);
                uint32_t rSum = 0, rCount = 0;
                uint32_t bSum = 0, bCount = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= bayer.width || ny < 0 || ny >= bayer.height) continue;
                        uint8_t nc = getColorAt(nx, ny);
                        if (nc == 0) { rSum += pixelAt(nx, ny); rCount++; }
                        else if (nc == 2) { bSum += pixelAt(nx, ny); bCount++; }
                    }
                }
                rgb[idx * 3 + 0] = rCount > 0 ? rSum / rCount : pixelAt(x, y);
                rgb[idx * 3 + 2] = bCount > 0 ? bSum / bCount : pixelAt(x, y);
            }
        }
    }
    
    return true;
}

bool RawImageLoader::generateThumbnail(const ImageData& raw, int maxSize, std::vector<uint8_t>& thumb) {
    if (raw.data.empty() || raw.width == 0 || raw.height == 0) {
        std::cerr << "generateThumbnail: 无效的图像数据" << std::endl;
        return false;
    }
    
    // 计算缩略图尺寸
    int thumbWidth, thumbHeight;
    if (raw.width > raw.height) {
        thumbWidth = maxSize;
        thumbHeight = raw.height * maxSize / raw.width;
    } else {
        thumbHeight = maxSize;
        thumbWidth = raw.width * maxSize / raw.height;
    }
    if (thumbWidth < 1) thumbWidth = 1;
    if (thumbHeight < 1) thumbHeight = 1;
    
    thumb.resize(thumbWidth * thumbHeight * 3);
    
    // 将 16-bit 数据缩放到 8-bit
    auto scale16to8 = [](uint16_t v) -> uint8_t {
        uint32_t scaled = (static_cast<uint32_t>(v) * 255) / 65535;
        if (scaled > 255) scaled = 255;
        return static_cast<uint8_t>(scaled);
    };
    
    if (raw.channels == 3) {
        // 直接采样 RGB 数据
        for (int y = 0; y < thumbHeight; ++y) {
            for (int x = 0; x < thumbWidth; ++x) {
                int srcX = x * raw.width / thumbWidth;
                int srcY = y * raw.height / thumbHeight;
                int srcIdx = (srcY * raw.width + srcX) * 3;
                int dstIdx = (y * thumbWidth + x) * 3;
                
                thumb[dstIdx + 0] = scale16to8(raw.data[srcIdx + 0]);
                thumb[dstIdx + 1] = scale16to8(raw.data[srcIdx + 1]);
                thumb[dstIdx + 2] = scale16to8(raw.data[srcIdx + 2]);
            }
        }
    } else {
        // Bayer 数据，使用 2x2 块平均灰度化以减少摩尔纹
        for (int y = 0; y < thumbHeight; ++y) {
            for (int x = 0; x < thumbWidth; ++x) {
                int srcX = x * raw.width / thumbWidth;
                int srcY = y * raw.height / thumbHeight;
                
                // 对齐到 2x2 Bayer 块边界，确保颜色覆盖均匀
                srcX = (srcX / 2) * 2;
                srcY = (srcY / 2) * 2;
                
                // 取 2x2 块的平均值（覆盖 R, G, G, B）
                uint32_t sum = 0;
                int count = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int px = srcX + dx;
                        int py = srcY + dy;
                        if (px < raw.width && py < raw.height) {
                            sum += raw.data[py * raw.width + px];
                            count++;
                        }
                    }
                }
                uint8_t gray = count > 0 ? scale16to8(static_cast<uint16_t>(sum / count)) : 0;
                
                int dstIdx = (y * thumbWidth + x) * 3;
                thumb[dstIdx + 0] = gray;
                thumb[dstIdx + 1] = gray;
                thumb[dstIdx + 2] = gray;
            }
        }
    }
    
    return true;
}
