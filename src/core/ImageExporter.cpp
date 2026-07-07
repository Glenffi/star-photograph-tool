#include "ImageExporter.h"
#include <QImage>
#include <QString>
#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef HAS_LIBTIFF
#include <tiffio.h>
#endif

static bool exportTiff16Bit(const std::vector<uint16_t>& image, int width, int height, const std::string& path) {
#ifdef HAS_LIBTIFF
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

    for (int row = 0; row < height; ++row) {
        if (TIFFWriteScanline(tiff, const_cast<uint16_t*>(&image[row * width]), row, 0) < 0) {
            std::cerr << "ImageExporter: TIFF 写入失败于行 " << row << std::endl;
            TIFFClose(tiff);
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
#else
    std::cerr << "ImageExporter: TIFF 导出不可用，libtiff 未找到" << std::endl;
    return false;
#endif
}

static bool exportTiffRgb16(const std::vector<uint16_t>& rgb, int width, int height, const std::string& path) {
#ifdef HAS_LIBTIFF
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

    for (int row = 0; row < height; ++row) {
        if (TIFFWriteScanline(tiff, const_cast<uint16_t*>(&rgb[row * width * 3]), row, 0) < 0) {
            std::cerr << "ImageExporter: TIFF 写入失败于行 " << row << std::endl;
            TIFFClose(tiff);
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
#else
    std::cerr << "ImageExporter: TIFF 导出不可用，libtiff 未找到" << std::endl;
    return false;
#endif
}

static bool exportPng8Bit(const std::vector<uint16_t>& image, int width, int height, const std::string& path) {
    QImage qimg(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint16_t val = image[y * width + x];
            uint8_t v = static_cast<uint8_t>(val >> 8);
            qimg.setPixel(x, y, v);
        }
    }
    return qimg.save(QString::fromStdString(path), "PNG");
}

static bool exportPngRgb16(const std::vector<uint16_t>& rgb, int width, int height, const std::string& path) {
    QImage qimg(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 3;
            uint8_t r = static_cast<uint8_t>(rgb[idx + 0] >> 8);
            uint8_t g = static_cast<uint8_t>(rgb[idx + 1] >> 8);
            uint8_t b = static_cast<uint8_t>(rgb[idx + 2] >> 8);
            qimg.setPixelColor(x, y, QColor(r, g, b));
        }
    }
    return qimg.save(QString::fromStdString(path), "PNG");
}

bool ImageExporter::export16Bit(const std::vector<uint16_t>& image,
                                int width, int height,
                                const std::string& path,
                                Format format) {
    if (image.empty() || width <= 0 || height <= 0 || static_cast<int>(image.size()) != width * height) {
        std::cerr << "ImageExporter: 无效的图像数据" << std::endl;
        return false;
    }

    if (format == Tiff16) {
        return exportTiff16Bit(image, width, height, path);
    } else {
        return exportPng8Bit(image, width, height, path);
    }
}

bool ImageExporter::exportRgb16(const std::vector<uint16_t>& rgb,
                                int width, int height,
                                const std::string& path,
                                Format format) {
    if (rgb.empty() || width <= 0 || height <= 0 || static_cast<int>(rgb.size()) != width * height * 3) {
        std::cerr << "ImageExporter: 无效的 RGB 图像数据" << std::endl;
        return false;
    }

    if (format == Tiff16) {
        return exportTiffRgb16(rgb, width, height, path);
    } else {
        return exportPngRgb16(rgb, width, height, path);
    }
}
