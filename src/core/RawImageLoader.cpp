#include "RawImageLoader.h"
#include <libraw/libraw.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <cstring>

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

    // 保存元数据（在 dcraw_process 之前提取，因为某些参数会被修改）
    out.iso = static_cast<int>(processor.imgdata.other.iso_speed);
    out.exposureTime = processor.imgdata.other.shutter;
    out.aperture = processor.imgdata.other.aperture;
    out.focalLength = static_cast<int>(processor.imgdata.other.focal_len);
    out.cameraModel = processor.imgdata.idata.model;

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

    // 判断是否为 Bayer 模式
    bool isBayer = (processor.imgdata.idata.filters != 0);

    if (isBayer) {
        // Bayer CFA: 使用 LibRaw 内置高质量 demosaic (AHD) + 相机 WB + 颜色矩阵
        processor.imgdata.params.user_qual = 3;          // AHD demosaic
        processor.imgdata.params.use_camera_wb = 1;      // 使用相机白平衡
        processor.imgdata.params.use_camera_matrix = 1;  // 使用相机色彩矩阵
        processor.imgdata.params.output_bps = 16;        // 16-bit 输出
        processor.imgdata.params.no_auto_bright = 1;     // 关闭自动亮度拉伸，保持线性
        processor.imgdata.params.output_color = 0;       // 输出相机线性空间
        processor.imgdata.params.gamm[0] = 1.0f;           // 关闭 gamma，保持线性
        processor.imgdata.params.gamm[1] = 1.0f;

        ret = processor.dcraw_process();
        if (ret != LIBRAW_SUCCESS) {
            std::cerr << "LibRaw dcraw_process 失败: " << filePath << " (code: " << ret << ")" << std::endl;
            return false;
        }

        libraw_processed_image_t* img = processor.dcraw_make_mem_image(&ret);
        if (!img || img->type != LIBRAW_IMAGE_BITMAP) {
            std::cerr << "LibRaw dcraw_make_mem_image 失败: " << filePath << std::endl;
            if (img) LibRaw::dcraw_clear_mem(img);
            return false;
        }

        // 校验 LibRaw 返回的位深、通道数和缓冲区大小
        if (img->bits != 16) {
            std::cerr << "LibRaw 输出位深不支持: " << img->bits << " bits (需要 16-bit): " << filePath << std::endl;
            LibRaw::dcraw_clear_mem(img);
            return false;
        }
        if (img->colors != 3) {
            std::cerr << "LibRaw 输出通道数不支持: " << img->colors << " colors (需要 3): " << filePath << std::endl;
            LibRaw::dcraw_clear_mem(img);
            return false;
        }
        size_t expectedDataSize = static_cast<size_t>(img->width) * img->height * img->colors * sizeof(uint16_t);
        if (img->data_size < expectedDataSize) {
            std::cerr << "LibRaw 缓冲区大小不足: " << img->data_size << " < " << expectedDataSize << ": " << filePath << std::endl;
            LibRaw::dcraw_clear_mem(img);
            return false;
        }

        out.width = img->width;
        out.height = img->height;
        out.channels = 3;
        out.bayerPattern = "";

        // img->data 是 uint8_t 数组，但 16-bit 模式下每像素占 2 bytes
        // colors=3, bits=16
        size_t totalPixels = img->width * img->height * img->colors;
        out.data.resize(totalPixels);
        std::memcpy(out.data.data(), img->data, totalPixels * sizeof(uint16_t));

        LibRaw::dcraw_clear_mem(img);
    } else {
        // 非 Bayer（如 Foveon 或已解码 RGB），使用 LibRaw 处理
        processor.imgdata.params.use_camera_wb = 1;
        processor.imgdata.params.use_camera_matrix = 1;
        processor.imgdata.params.output_bps = 16;
        processor.imgdata.params.no_auto_bright = 1;
        processor.imgdata.params.output_color = 0;
        processor.imgdata.params.gamm[0] = 1.0f;
        processor.imgdata.params.gamm[1] = 1.0f;

        ret = processor.dcraw_process();
        if (ret != LIBRAW_SUCCESS) {
            std::cerr << "LibRaw dcraw_process 失败: " << filePath << " (code: " << ret << ")" << std::endl;
            return false;
        }

        libraw_processed_image_t* img = processor.dcraw_make_mem_image(&ret);
        if (!img || img->type != LIBRAW_IMAGE_BITMAP) {
            std::cerr << "LibRaw dcraw_make_mem_image 失败: " << filePath << std::endl;
            if (img) LibRaw::dcraw_clear_mem(img);
            return false;
        }

        // 校验 LibRaw 返回的位深、通道数和缓冲区大小
        if (img->bits != 16) {
            std::cerr << "LibRaw 输出位深不支持: " << img->bits << " bits (需要 16-bit): " << filePath << std::endl;
            LibRaw::dcraw_clear_mem(img);
            return false;
        }
        if (img->colors != 3) {
            std::cerr << "LibRaw 输出通道数不支持: " << img->colors << " colors (需要 3): " << filePath << std::endl;
            LibRaw::dcraw_clear_mem(img);
            return false;
        }
        size_t expectedDataSize = static_cast<size_t>(img->width) * img->height * img->colors * sizeof(uint16_t);
        if (img->data_size < expectedDataSize) {
            std::cerr << "LibRaw 缓冲区大小不足: " << img->data_size << " < " << expectedDataSize << ": " << filePath << std::endl;
            LibRaw::dcraw_clear_mem(img);
            return false;
        }

        out.width = img->width;
        out.height = img->height;
        out.channels = 3;
        out.bayerPattern = "";

        size_t totalPixels = img->width * img->height * img->colors;
        out.data.resize(totalPixels);
        std::memcpy(out.data.data(), img->data, totalPixels * sizeof(uint16_t));

        LibRaw::dcraw_clear_mem(img);
    }

    processor.recycle();
    return true;
}

bool RawImageLoader::decodeToRgb(const ImageData& bayer, std::vector<uint16_t>& rgb) {
    // 当前 loadRaw() 已直接输出 RGB，此函数保留为兼容性接口
    if (bayer.channels != 3 || bayer.data.empty()) {
        std::cerr << "decodeToRgb: 输入不是有效的 RGB 数据" << std::endl;
        return false;
    }
    rgb = bayer.data;
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

    // 直接采样 RGB 数据（loadRaw 现在始终输出 RGB）
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

    return true;
}
