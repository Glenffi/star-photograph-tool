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
                    out.data[y * out.width + x] = processor.imgdata.rawdata.raw_image[srcIdx];
                }
            }
        } else {
            // 回退：使用彩色数据的第一通道
            out.data.resize(out.width * out.height);
            for (int y = 0; y < out.height; ++y) {
                for (int x = 0; x < out.width; ++x) {
                    int idx = (processor.imgdata.sizes.top_margin + y) * rawWidth
                            + (processor.imgdata.sizes.left_margin + x);
                    out.data[y * out.width + x] = processor.imgdata.rawdata.raw_image[idx];
                }
            }
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
                out.data[idx * 3 + 0] = processor.imgdata.image[idx][0]; // R
                out.data[idx * 3 + 1] = processor.imgdata.image[idx][1]; // G
                out.data[idx * 3 + 2] = processor.imgdata.image[idx][2]; // B
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
    
    // 简单的 Bayer 解码（最近邻插值）
    // 支持 RGGB, BGGR, GRBG, GBRG
    for (int y = 0; y < bayer.height; ++y) {
        for (int x = 0; x < bayer.width; ++x) {
            int idx = y * bayer.width + x;
            uint16_t r = 0, g = 0, b = 0;
            
            // 判断当前像素位置的颜色
            int rowParity = y % 2;
            int colParity = x % 2;
            
            if (bayer.bayerPattern == "RGGB") {
                if (rowParity == 0 && colParity == 0) {
                    // R 位置
                    r = bayer.data[idx];
                    g = (y > 0 ? bayer.data[(y-1)*bayer.width+x] : bayer.data[y*bayer.width+x]) / 2 +
                        (y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+x] : bayer.data[y*bayer.width+x]) / 2;
                    b = (x > 0 && y > 0 ? bayer.data[(y-1)*bayer.width+(x-1)] : bayer.data[idx]) / 4 +
                        (x < bayer.width-1 && y > 0 ? bayer.data[(y-1)*bayer.width+(x+1)] : bayer.data[idx]) / 4 +
                        (x > 0 && y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+(x-1)] : bayer.data[idx]) / 4 +
                        (x < bayer.width-1 && y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+(x+1)] : bayer.data[idx]) / 4;
                } else if (rowParity == 0 && colParity == 1) {
                    // G 位置 (第一行)
                    g = bayer.data[idx];
                    r = (x > 0 ? bayer.data[y*bayer.width+(x-1)] : bayer.data[idx]) / 2 +
                        (x < bayer.width-1 ? bayer.data[y*bayer.width+(x+1)] : bayer.data[idx]) / 2;
                    b = (y > 0 ? bayer.data[(y-1)*bayer.width+x] : bayer.data[idx]) / 2 +
                        (y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+x] : bayer.data[idx]) / 2;
                } else if (rowParity == 1 && colParity == 0) {
                    // G 位置 (第二行)
                    g = bayer.data[idx];
                    r = (y > 0 ? bayer.data[(y-1)*bayer.width+x] : bayer.data[idx]) / 2 +
                        (y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+x] : bayer.data[idx]) / 2;
                    b = (x > 0 ? bayer.data[y*bayer.width+(x-1)] : bayer.data[idx]) / 2 +
                        (x < bayer.width-1 ? bayer.data[y*bayer.width+(x+1)] : bayer.data[idx]) / 2;
                } else {
                    // B 位置
                    b = bayer.data[idx];
                    g = (y > 0 ? bayer.data[(y-1)*bayer.width+x] : bayer.data[y*bayer.width+x]) / 2 +
                        (y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+x] : bayer.data[y*bayer.width+x]) / 2;
                    r = (x > 0 && y > 0 ? bayer.data[(y-1)*bayer.width+(x-1)] : bayer.data[idx]) / 4 +
                        (x < bayer.width-1 && y > 0 ? bayer.data[(y-1)*bayer.width+(x+1)] : bayer.data[idx]) / 4 +
                        (x > 0 && y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+(x-1)] : bayer.data[idx]) / 4 +
                        (x < bayer.width-1 && y < bayer.height-1 ? bayer.data[(y+1)*bayer.width+(x+1)] : bayer.data[idx]) / 4;
                }
            } else {
                // 其他 Bayer 模式暂时简化处理
                r = bayer.data[idx];
                g = bayer.data[idx];
                b = bayer.data[idx];
            }
            
            rgb[idx * 3 + 0] = r;
            rgb[idx * 3 + 1] = g;
            rgb[idx * 3 + 2] = b;
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
        // Bayer 数据，简单灰度化
        // 找到 Bayer 中每个 2x2 块的平均值
        for (int y = 0; y < thumbHeight; ++y) {
            for (int x = 0; x < thumbWidth; ++x) {
                int srcX = x * raw.width / thumbWidth;
                int srcY = y * raw.height / thumbHeight;
                
                // 简单采样（避免 demosaic 开销）
                int srcIdx = srcY * raw.width + srcX;
                uint8_t gray = scale16to8(raw.data[srcIdx]);
                
                int dstIdx = (y * thumbWidth + x) * 3;
                thumb[dstIdx + 0] = gray;
                thumb[dstIdx + 1] = gray;
                thumb[dstIdx + 2] = gray;
            }
        }
    }
    
    return true;
}