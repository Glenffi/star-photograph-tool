#include "ImageExporter.h"
#include <tiffio.h>
#include <cstring>
#include <iostream>

bool ImageExporter::export16Bit(const std::vector<uint16_t>& image,
                                  int width, int height,
                                  const std::string& path,
                                  Format /*format*/) {
    if (image.empty() || width <= 0 || height <= 0 || static_cast<int>(image.size()) != width * height) {
        std::cerr << "ImageExporter: 无效的图像数据" << std::endl;
        return false;
    }

    TIFF* tiff = TIFFOpen(path.c_str(), "w");
    if (!tiff) {
        std::cerr << "ImageExporter: 无法打开 TIFF 文件: " << path << std::endl;
        return false;
    }

    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);

    // 按行写入
    for (int row = 0; row < height; ++row) {
        if (TIFFWriteScanline(tiff, const_cast<uint16_t*>(&image[row * width]), row, 0) < 0) {
            std::cerr << "ImageExporter: TIFF 写入失败于行 " << row << std::endl;
            TIFFClose(tiff);
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
}

bool ImageExporter::exportRgb16(const std::vector<uint16_t>& rgb,
                                  int width, int height,
                                  const std::string& path,
                                  Format /*format*/) {
    if (rgb.empty() || width <= 0 || height <= 0 || static_cast<int>(rgb.size()) != width * height * 3) {
        std::cerr << "ImageExporter: 无效的 RGB 图像数据" << std::endl;
        return false;
    }

    TIFF* tiff = TIFFOpen(path.c_str(), "w");
    if (!tiff) {
        std::cerr << "ImageExporter: 无法打开 TIFF 文件: " << path << std::endl;
        return false;
    }

    uint16_t samplesPerPixel = 3;
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);

    // 按行写入（RGB交错）
    for (int row = 0; row < height; ++row) {
        if (TIFFWriteScanline(tiff, const_cast<uint16_t*>(&rgb[row * width * 3]), row, 0) < 0) {
            std::cerr << "ImageExporter: TIFF 写入失败于行 " << row << std::endl;
            TIFFClose(tiff);
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
}
