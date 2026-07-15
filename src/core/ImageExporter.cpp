#include "ImageExporter.h"
#include <QImage>
#include <QString>
#include <QColorSpace>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <cmath>

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

    // 使用 Qt 内置的线性 sRGB ICC profile
    const QByteArray iccProfile = QColorSpace(QColorSpace::SRgbLinear).iccProfile();
    if (!iccProfile.isEmpty()) {
        TIFFSetField(tiff, TIFFTAG_ICCPROFILE,
                     static_cast<uint32_t>(iccProfile.size()),
                     iccProfile.constData());
    } else {
        std::cerr << "ImageExporter: 警告：无法获取线性 sRGB ICC profile，"
                     "TIFF 将不嵌入色彩空间标记" << std::endl;
    }

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

// sRGB OETF: 线性值 -> sRGB 编码值
// 输入: 0-65535 线性值, 输出: 0-255 sRGB 编码值
static uint8_t linearToSrgb8(uint16_t linear16) {
    // 归一化到 0-1
    float linear = static_cast<float>(linear16) / 65535.0f;
    // sRGB OETF
    float srgb;
    if (linear <= 0.0031308f) {
        srgb = linear * 12.92f;
    } else {
        srgb = 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }
    // 量化到 8-bit
    int val = static_cast<int>(srgb * 255.0f + 0.5f);
    return static_cast<uint8_t>(std::clamp(val, 0, 255));
}

static bool exportPng8Bit(const std::vector<uint16_t>& image, int width, int height, const std::string& path) {
    QImage qimg(width, height, QImage::Format_Grayscale8);
    // 设置 sRGB 色彩空间，与 RGB PNG 保持一致
    qimg.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint16_t val = image[y * width + x];
            uint8_t v = linearToSrgb8(val);
            qimg.setPixel(x, y, v);
        }
    }
    return qimg.save(QString::fromStdString(path), "PNG");
}

static bool exportPngRgb16(const std::vector<uint16_t>& rgb, int width, int height, const std::string& path) {
    QImage qimg(width, height, QImage::Format_RGB888);
    // 设置 sRGB 色彩空间，确保查看器正确解释
    qimg.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 3;
            uint8_t r = linearToSrgb8(rgb[idx + 0]);
            uint8_t g = linearToSrgb8(rgb[idx + 1]);
            uint8_t b = linearToSrgb8(rgb[idx + 2]);
            qimg.setPixelColor(x, y, QColor(r, g, b));
        }
    }
    return qimg.save(QString::fromStdString(path), "PNG");
}

bool ImageExporter::export16Bit(const std::vector<uint16_t>& image,
                                int width, int height,
                                const std::string& path,
                                Format format) {
    if (image.empty() || width <= 0 || height <= 0) {
        std::cerr << "ImageExporter: 无效的图像数据" << std::endl;
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (image.size() != expectedSize) {
        std::cerr << "ImageExporter: 图像尺寸不匹配" << std::endl;
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
    if (rgb.empty() || width <= 0 || height <= 0) {
        std::cerr << "ImageExporter: 无效的 RGB 图像数据" << std::endl;
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    if (rgb.size() != expectedSize) {
        std::cerr << "ImageExporter: RGB 图像尺寸不匹配" << std::endl;
        return false;
    }

    if (format == Tiff16) {
        return exportTiffRgb16(rgb, width, height, path);
    } else {
        return exportPngRgb16(rgb, width, height, path);
    }
}
