#include "ImageExporter.h"
#include <QImage>
#include <QString>
#include <QColorSpace>
#include <QDebug>
#include <QFile>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

#ifdef HAS_LIBTIFF
#include <tiffio.h>
#endif

static bool checkedSampleCount(int width, int height, size_t channels,
                               size_t& count) {
    if (width <= 0 || height <= 0 || channels == 0) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h) return false;
    const size_t pixels = w * h;
    if (pixels > std::numeric_limits<size_t>::max() / channels) return false;
    count = pixels * channels;
    return true;
}

#ifdef HAS_LIBTIFF
static TIFF* openTiffForWrite(const QString& path) {
#ifdef _WIN32
    const std::wstring nativePath = path.toStdWString();
    return TIFFOpenW(nativePath.c_str(), "w");
#else
    const QByteArray nativePath = QFile::encodeName(path);
    return TIFFOpen(nativePath.constData(), "w");
#endif
}

static TIFF* openTiffForRead(const QString& path) {
#ifdef _WIN32
    const std::wstring nativePath = path.toStdWString();
    return TIFFOpenW(nativePath.c_str(), "r");
#else
    const QByteArray nativePath = QFile::encodeName(path);
    return TIFFOpen(nativePath.constData(), "r");
#endif
}

static bool setTiffImageFields(TIFF* tiff, int width, int height,
                               uint16_t samplesPerPixel,
                               uint16_t photometric) {
    return TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, width) == 1 &&
           TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, height) == 1 &&
           TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) == 1 &&
           TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL,
                        samplesPerPixel) == 1 &&
           TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE) == 1 &&
           TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, photometric) == 1 &&
           TIFFSetField(tiff, TIFFTAG_PLANARCONFIG,
                        PLANARCONFIG_CONTIG) == 1 &&
           TIFFSetField(tiff, TIFFTAG_ORIENTATION,
                        ORIENTATION_TOPLEFT) == 1 &&
           TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1) == 1;
}
#endif

static bool exportTiff16Bit(
    const std::vector<uint16_t>& image, int width, int height,
    const QString& path, const std::function<bool()>& cancelled) {
#ifdef HAS_LIBTIFF
    TIFF* tiff = openTiffForWrite(path);
    if (!tiff) {
        qWarning().noquote() << "ImageExporter: 无法打开 TIFF 文件:" << path;
        return false;
    }

    if (!setTiffImageFields(
            tiff, width, height, 1, PHOTOMETRIC_MINISBLACK)) {
        qWarning() << "ImageExporter: 无法设置灰度 TIFF 必需标签";
        TIFFClose(tiff);
        return false;
    }

    for (int row = 0; row < height; ++row) {
        if (cancelled && cancelled()) {
            TIFFClose(tiff);
            QFile::remove(path);
            return false;
        }
        uint16_t* scanline = const_cast<uint16_t*>(
            image.data() + static_cast<size_t>(row) * width);
        if (TIFFWriteScanline(tiff, scanline, row, 0) < 0) {
            std::cerr << "ImageExporter: TIFF 写入失败于行 " << row << std::endl;
            TIFFClose(tiff);
            QFile::remove(path);
            return false;
        }
    }

    TIFFClose(tiff);
    if (cancelled && cancelled()) {
        QFile::remove(path);
        return false;
    }
    return true;
#else
    std::cerr << "ImageExporter: TIFF 导出不可用，libtiff 未找到" << std::endl;
    return false;
#endif
}

static bool exportTiffRgb16(
    const std::vector<uint16_t>& rgb, int width, int height,
    const QString& path, const std::function<bool()>& cancelled) {
#ifdef HAS_LIBTIFF
    TIFF* tiff = openTiffForWrite(path);
    if (!tiff) {
        qWarning().noquote() << "ImageExporter: 无法打开 TIFF 文件:" << path;
        return false;
    }

    if (!setTiffImageFields(tiff, width, height, 3, PHOTOMETRIC_RGB)) {
        qWarning() << "ImageExporter: 无法设置 RGB TIFF 必需标签";
        TIFFClose(tiff);
        return false;
    }

    // 使用 Qt 内置的线性 sRGB ICC profile
    const QByteArray iccProfile = QColorSpace(QColorSpace::SRgbLinear).iccProfile();
    if (!iccProfile.isEmpty()) {
        if (TIFFSetField(tiff, TIFFTAG_ICCPROFILE,
                         static_cast<uint32_t>(iccProfile.size()),
                         iccProfile.constData()) != 1) {
            std::cerr << "ImageExporter: 警告：TIFFSetField ICC profile 失败，"
                         "导出将不嵌入色彩空间标记" << std::endl;
        }
    } else {
        std::cerr << "ImageExporter: 警告：无法获取线性 sRGB ICC profile，"
                     "TIFF 将不嵌入色彩空间标记" << std::endl;
    }

    for (int row = 0; row < height; ++row) {
        if (cancelled && cancelled()) {
            TIFFClose(tiff);
            QFile::remove(path);
            return false;
        }
        uint16_t* scanline = const_cast<uint16_t*>(
            rgb.data() + static_cast<size_t>(row) * width * 3);
        if (TIFFWriteScanline(tiff, scanline, row, 0) < 0) {
            std::cerr << "ImageExporter: TIFF 写入失败于行 " << row << std::endl;
            TIFFClose(tiff);
            QFile::remove(path);
            return false;
        }
    }

    TIFFClose(tiff);
    if (cancelled && cancelled()) {
        QFile::remove(path);
        return false;
    }
    return true;
#else
    std::cerr << "ImageExporter: TIFF 导出不可用，libtiff 未找到" << std::endl;
    return false;
#endif
}

// sRGB OETF: 线性值 -> sRGB 编码值
// 输入: 0-65535 线性值, 输出: 0-255 sRGB 编码值
static uint8_t computeLinearToSrgb8(uint16_t linear16) {
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

static const std::array<uint8_t, 65536>& linearToSrgb8Table() {
    static const std::array<uint8_t, 65536> table = []() {
        std::array<uint8_t, 65536> values{};
        for (size_t value = 0; value < values.size(); ++value) {
            values[value] = computeLinearToSrgb8(
                static_cast<uint16_t>(value));
        }
        return values;
    }();
    return table;
}

static bool exportPng8Bit(
    const std::vector<uint16_t>& image, int width, int height,
    const QString& path, const std::function<bool()>& cancelled) {
    QImage qimg(width, height, QImage::Format_Grayscale8);
    if (qimg.isNull()) return false;
    // 设置 sRGB 色彩空间，与 RGB PNG 保持一致
    qimg.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const auto& transfer = linearToSrgb8Table();
    for (int y = 0; y < height; ++y) {
        if (cancelled && cancelled()) return false;
        uchar* output = qimg.scanLine(y);
        const uint16_t* input = image.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            output[x] = transfer[input[x]];
        }
    }
    if (cancelled && cancelled()) return false;
    const bool saved = qimg.save(path, "PNG");
    if (cancelled && cancelled()) {
        if (saved) QFile::remove(path);
        return false;
    }
    return saved;
}

static bool exportPngRgb16(
    const std::vector<uint16_t>& rgb, int width, int height,
    const QString& path, const std::function<bool()>& cancelled) {
    QImage qimg(width, height, QImage::Format_RGB888);
    if (qimg.isNull()) return false;
    // 设置 sRGB 色彩空间，确保查看器正确解释
    qimg.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const auto& transfer = linearToSrgb8Table();
    const size_t samplesPerRow = static_cast<size_t>(width) * 3;
    for (int y = 0; y < height; ++y) {
        if (cancelled && cancelled()) return false;
        uchar* output = qimg.scanLine(y);
        const uint16_t* input = rgb.data() +
            static_cast<size_t>(y) * samplesPerRow;
        for (size_t x = 0; x < samplesPerRow; ++x) {
            output[x] = transfer[input[x]];
        }
    }
    if (cancelled && cancelled()) return false;
    const bool saved = qimg.save(path, "PNG");
    if (cancelled && cancelled()) {
        if (saved) QFile::remove(path);
        return false;
    }
    return saved;
}

bool ImageExporter::export16Bit(const std::vector<uint16_t>& image,
                                int width, int height,
                                const QString& path,
                                Format format,
                                const std::function<bool()>& cancelled) {
    if (image.empty() || width <= 0 || height <= 0) {
        std::cerr << "ImageExporter: 无效的图像数据" << std::endl;
        return false;
    }
    size_t expectedSize = 0;
    if (!checkedSampleCount(width, height, 1, expectedSize) ||
        image.size() != expectedSize) {
        std::cerr << "ImageExporter: 图像尺寸不匹配" << std::endl;
        return false;
    }

    if (format == Tiff16) {
        return exportTiff16Bit(image, width, height, path, cancelled);
    } else {
        return exportPng8Bit(image, width, height, path, cancelled);
    }
}

bool ImageExporter::exportRgb16(const std::vector<uint16_t>& rgb,
                                int width, int height,
                                const QString& path,
                                Format format,
                                const std::function<bool()>& cancelled) {
    if (rgb.empty() || width <= 0 || height <= 0) {
        std::cerr << "ImageExporter: 无效的 RGB 图像数据" << std::endl;
        return false;
    }
    size_t expectedSize = 0;
    if (!checkedSampleCount(width, height, 3, expectedSize) ||
        rgb.size() != expectedSize) {
        std::cerr << "ImageExporter: RGB 图像尺寸不匹配" << std::endl;
        return false;
    }

    if (format == Tiff16) {
        return exportTiffRgb16(rgb, width, height, path, cancelled);
    } else {
        return exportPngRgb16(rgb, width, height, path, cancelled);
    }
}

bool ImageExporter::loadTiffRgb16(const QString& path,
                                  std::vector<uint16_t>& rgb,
                                  int& width, int& height) {
    rgb.clear();
    width = 0;
    height = 0;
#ifdef HAS_LIBTIFF
    TIFF* tiff = openTiffForRead(path);
    if (!tiff) return false;

    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
    uint16_t bitsPerSample = 0;
    uint16_t samplesPerPixel = 0;
    uint16_t planarConfig = 0;
    uint16_t orientation = ORIENTATION_TOPLEFT;
    const bool fieldsValid =
        TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &imageWidth) == 1 &&
        TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &imageHeight) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE,
                              &bitsPerSample) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL,
                              &samplesPerPixel) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG,
                              &planarConfig) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION,
                              &orientation) == 1;
    if (!fieldsValid || imageWidth == 0 || imageHeight == 0 ||
        imageWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        imageHeight > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        bitsPerSample != 16 ||
        (samplesPerPixel != 1 && samplesPerPixel != 3) ||
        planarConfig != PLANARCONFIG_CONTIG ||
        orientation != ORIENTATION_TOPLEFT) {
        TIFFClose(tiff);
        return false;
    }

    size_t outputSamples = 0;
    if (!checkedSampleCount(static_cast<int>(imageWidth),
                            static_cast<int>(imageHeight), 3,
                            outputSamples)) {
        TIFFClose(tiff);
        return false;
    }
    const tmsize_t scanlineBytes = TIFFScanlineSize(tiff);
    const size_t requiredBytes = static_cast<size_t>(imageWidth) *
        samplesPerPixel * sizeof(uint16_t);
    if (scanlineBytes <= 0 ||
        static_cast<size_t>(scanlineBytes) < requiredBytes) {
        TIFFClose(tiff);
        return false;
    }

    rgb.resize(outputSamples);
    std::vector<uint8_t> scanline(static_cast<size_t>(scanlineBytes));
    for (uint32_t row = 0; row < imageHeight; ++row) {
        if (TIFFReadScanline(tiff, scanline.data(), row, 0) < 0) {
            rgb.clear();
            TIFFClose(tiff);
            return false;
        }
        const auto* input = reinterpret_cast<const uint16_t*>(scanline.data());
        uint16_t* output = rgb.data() +
            static_cast<size_t>(row) * imageWidth * 3;
        if (samplesPerPixel == 3) {
            std::copy_n(input, static_cast<size_t>(imageWidth) * 3, output);
        } else {
            for (uint32_t column = 0; column < imageWidth; ++column) {
                output[column * 3] = input[column];
                output[column * 3 + 1] = input[column];
                output[column * 3 + 2] = input[column];
            }
        }
    }
    TIFFClose(tiff);
    width = static_cast<int>(imageWidth);
    height = static_cast<int>(imageHeight);
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}
