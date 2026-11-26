#include "ImageExporter.h"
#include <QImage>
#include <QString>
#include <QColorSpace>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#else
#include <cerrno>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

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

#ifdef Q_OS_WIN
static QString windowsPath(const QString& absolutePath) {
    const QString native = QDir::toNativeSeparators(absolutePath);
    if (native.startsWith(QStringLiteral("\\\\?\\")) ||
        native.startsWith(QStringLiteral("\\\\.\\"))) {
        return native;
    }
    if (native.startsWith(QStringLiteral("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    }
    return QStringLiteral("\\\\?\\") + native;
}
#endif

enum class Publication { Renamed, Linked, Occupied, Failed };

static Publication publishWithoutReplacing(const QString& stagedPath,
                                           const QString& outputPath) {
#ifdef Q_OS_WIN
    const auto from = windowsPath(stagedPath).toStdWString();
    const auto to = windowsPath(outputPath).toStdWString();
    if (MoveFileExW(from.c_str(), to.c_str(), 0)) return Publication::Renamed;

    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ||
        ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) &&
         GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES)) {
        return Publication::Occupied;
    }
    return Publication::Failed;
#else
    const QByteArray from = QFile::encodeName(stagedPath);
    const QByteArray to = QFile::encodeName(outputPath);
#if (defined(Q_OS_MACOS) && defined(RENAME_EXCL)) || \
    (defined(Q_OS_LINUX) && defined(SYS_renameat2) && defined(RENAME_NOREPLACE))
#ifdef Q_OS_MACOS
    if (::renamex_np(from.constData(), to.constData(), RENAME_EXCL) == 0) {
#else
    if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(),
                  AT_FDCWD, to.constData(), RENAME_NOREPLACE) == 0) {
#endif
        return Publication::Renamed;
    }
    const int error = errno;
    if (error == EEXIST) return Publication::Occupied;
    if (error != ENOTSUP && error != ENOSYS && error != EINVAL) {
        return Publication::Failed;
    }
#endif
    // Never fall back to plain rename or a copy: either could violate delivery.
    if (::link(from.constData(), to.constData()) == 0) return Publication::Linked;
    return errno == EEXIST ? Publication::Occupied : Publication::Failed;
#endif
}

#ifdef HAS_LIBTIFF
static TIFF* openTiffForRead(const QString& path) {
#ifdef Q_OS_WIN
    const auto nativePath = windowsPath(QFileInfo(path).absoluteFilePath())
                                .toStdWString();
    return TIFFOpenW(nativePath.c_str(), "r");
#else
    const QByteArray nativePath = QFile::encodeName(path);
    return TIFFOpen(nativePath.constData(), "r");
#endif
}

static TIFF* openTiffForWrite(QFileDevice& file) {
    // Write through the already-open Qt device, including on Unicode paths.
    return TIFFClientOpen(
        "ImageExporter", "w", &file,
        [](thandle_t handle, void* data, tmsize_t size) {
            return static_cast<tmsize_t>(
                static_cast<QFileDevice*>(handle)->read(
                    static_cast<char*>(data), size));
        },
        [](thandle_t handle, void* data, tmsize_t size) {
            return static_cast<tmsize_t>(
                static_cast<QFileDevice*>(handle)->write(
                    static_cast<const char*>(data), size));
        },
        [](thandle_t handle, toff_t offset, int origin) -> toff_t {
            auto& device = *static_cast<QFileDevice*>(handle);
            constexpr toff_t failed = static_cast<toff_t>(-1);
            qint64 base = 0;
            if (origin == SEEK_CUR) {
                base = device.pos();
            } else if (origin == SEEK_END) {
                base = device.size();
            } else if (origin != SEEK_SET) {
                return failed;
            }
            if (base < 0 || offset > static_cast<toff_t>(
                    std::numeric_limits<qint64>::max() - base)) {
                return failed;
            }
            const qint64 position = base + static_cast<qint64>(offset);
            return device.seek(position) ? static_cast<toff_t>(position) : failed;
        },
        [](thandle_t) { return 0; },
        [](thandle_t handle) {
            return static_cast<toff_t>(static_cast<QFileDevice*>(handle)->size());
        },
        nullptr, nullptr);
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

static bool writeTiff16(QFileDevice& file, const std::vector<uint16_t>& image,
                        int width, int height, int channels,
                        const std::function<bool()>& cancelled) {
#ifdef HAS_LIBTIFF
    const std::unique_ptr<TIFF, decltype(&TIFFClose)> tiff(
        openTiffForWrite(file), TIFFClose);
    if (!tiff) return false;
    const bool rgb = channels == 3;
    if (!setTiffImageFields(tiff.get(), width, height,
                            static_cast<uint16_t>(channels),
                            rgb ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK)) {
        qWarning() << "ImageExporter: Could not set TIFF image fields";
        return false;
    }

    if (rgb) {
        const QByteArray iccProfile =
            QColorSpace(QColorSpace::SRgbLinear).iccProfile();
        if (iccProfile.isEmpty() ||
            TIFFSetField(tiff.get(), TIFFTAG_ICCPROFILE,
                         static_cast<uint32_t>(iccProfile.size()),
                         iccProfile.constData()) != 1) {
            qWarning() << "ImageExporter: Could not embed the linear sRGB profile";
        }
    }

    const size_t samplesPerRow = static_cast<size_t>(width) * channels;
    for (int row = 0; row < height; ++row) {
        if (cancelled && cancelled()) return false;
        uint16_t* scanline = const_cast<uint16_t*>(
            image.data() + static_cast<size_t>(row) * samplesPerRow);
        if (TIFFWriteScanline(tiff.get(), scanline, row, 0) != 1) {
            qWarning() << "ImageExporter: TIFF write failed at row" << row;
            return false;
        }
    }

    // TIFFClose cannot report errors writing the final image directory.
    return TIFFFlush(tiff.get()) == 1;
#else
    Q_UNUSED(file)
    Q_UNUSED(image)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(channels)
    Q_UNUSED(cancelled)
    qWarning() << "ImageExporter: TIFF export requires libtiff";
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

static bool writePng8(QFileDevice& file, const std::vector<uint16_t>& image,
                      int width, int height, int channels,
                      const std::function<bool()>& cancelled) {
    QImage qimg(width, height, channels == 3
                    ? QImage::Format_RGB888 : QImage::Format_Grayscale8);
    if (qimg.isNull()) return false;
    qimg.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const auto& transfer = linearToSrgb8Table();
    const size_t samplesPerRow = static_cast<size_t>(width) * channels;
    for (int y = 0; y < height; ++y) {
        if (cancelled && cancelled()) return false;
        uchar* output = qimg.scanLine(y);
        const uint16_t* input = image.data() +
            static_cast<size_t>(y) * samplesPerRow;
        for (size_t x = 0; x < samplesPerRow; ++x) {
            output[x] = transfer[input[x]];
        }
    }
    if (cancelled && cancelled()) return false;
    return qimg.save(&file, "PNG");
}

static bool validateExport(const std::vector<uint16_t>& image,
                           int width, int height, int channels,
                           ImageExporter::Format format) {
    size_t expected = 0;
    if (!checkedSampleCount(width, height, channels, expected) ||
        image.size() != expected) {
        qWarning() << "ImageExporter: Invalid dimensions or sample count";
        return false;
    }
    return format == ImageExporter::Tiff16 || format == ImageExporter::Png8;
}

static bool writeImage(QFileDevice& file, const std::vector<uint16_t>& image,
                        int width, int height, int channels,
                        ImageExporter::Format format,
                        const std::function<bool()>& cancelled) {
    const bool written = format == ImageExporter::Tiff16
        ? writeTiff16(file, image, width, height, channels, cancelled)
        : writePng8(file, image, width, height, channels, cancelled);
    return written && file.error() == QFileDevice::NoError && file.flush();
}

static bool exportToExplicitPath(const std::vector<uint16_t>& image,
                                 int width, int height, int channels,
                                 const QString& path, ImageExporter::Format format,
                                 const std::function<bool()>& cancelled) {
    if (!validateExport(image, width, height, channels, format) ||
        (cancelled && cancelled())) {
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) &&
           writeImage(file, image, width, height, channels, format, cancelled) &&
           !(cancelled && cancelled()) && file.commit();
}

bool ImageExporter::export16Bit(const std::vector<uint16_t>& image,
                                int width, int height,
                                const QString& path,
                                Format format,
                                const std::function<bool()>& cancelled) {
    return exportToExplicitPath(image, width, height, 1, path, format, cancelled);
}

bool ImageExporter::exportRgb16(const std::vector<uint16_t>& rgb,
                                int width, int height,
                                const QString& path,
                                Format format,
                                const std::function<bool()>& cancelled) {
    return exportToExplicitPath(rgb, width, height, 3, path, format, cancelled);
}

bool ImageExporter::exportNewRgb16(const std::vector<uint16_t>& rgb,
                                   int width, int height,
                                   const QString& preferredPath, Format format,
                                   QString& actualPath,
                                   const std::function<bool()>& cancelled) {
    const QString requested = preferredPath;
    actualPath.clear();
    if (requested.isEmpty() || requested.contains(QChar::Null) ||
        !validateExport(rgb, width, height, 3, format) ||
        (cancelled && cancelled())) {
        return false;
    }

    const QFileInfo preferred(requested);
    const QString name = preferred.fileName();
    if (name.isEmpty() || name == QStringLiteral(".") ||
        name == QStringLiteral("..")) {
        return false;
    }
    const QDir directory = preferred.absoluteDir();
    QString stagedPath;
    const auto cleanup = qScopeGuard([&]() {
        if (!stagedPath.isEmpty() && !QFile::remove(stagedPath)) {
            qWarning().noquote() << "ImageExporter: Could not remove" << stagedPath;
        }
    });
    {
        QTemporaryFile staged(directory.filePath(
            QStringLiteral(".nebulens-export-XXXXXX")));
        if (!staged.open() ||
            !writeImage(staged, rgb, width, height, 3, format, cancelled)) {
            return false;
        }
        const QString path = staged.fileName();
        staged.close();
        if (path.isEmpty() || staged.error() != QFileDevice::NoError) return false;
        stagedPath = path;
        staged.setAutoRemove(false);
        // Destruction closes the native handle so Windows can rename it.
    }

    const QString suffix = preferred.suffix();
    const QString extension =
        suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
    const QString stem = preferred.completeBaseName();
    for (int number = 1; number <= 10'000; ++number) {
        const QString candidate = directory.filePath(number == 1
            ? name : stem + QLatin1Char('_') + QString::number(number) + extension);
        if (cancelled && cancelled()) return false;
        const Publication publication =
            publishWithoutReplacing(stagedPath, candidate);
        if (publication == Publication::Occupied) continue;
        if (publication == Publication::Failed) {
            qWarning().noquote() << "ImageExporter: Could not publish" << candidate;
            return false;
        }
        if (publication == Publication::Renamed) stagedPath.clear();
        actualPath = candidate;
        return true;
    }
    return false;
}

bool ImageExporter::loadTiffRgb16(const QString& path,
                                  std::vector<uint16_t>& rgb,
                                  int& width, int& height,
                                  int maximumLongSide,
                                  const std::function<bool()>& cancelled) {
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
    uint16_t photometric = 0;
    uint16_t sampleFormat = SAMPLEFORMAT_UINT;
    uint16_t compression = COMPRESSION_NONE;
    uint32_t rowsPerStrip = 0;
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
                              &orientation) == 1 &&
        TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT,
                              &sampleFormat) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_COMPRESSION,
                              &compression) == 1 &&
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ROWSPERSTRIP,
                              &rowsPerStrip) == 1;
    if (!fieldsValid || imageWidth == 0 || imageHeight == 0 ||
        imageWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        imageHeight > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        bitsPerSample != 16 ||
        (samplesPerPixel != 1 && samplesPerPixel != 3) ||
        sampleFormat != SAMPLEFORMAT_UINT ||
        compression != COMPRESSION_NONE || rowsPerStrip != 1 ||
        (samplesPerPixel == 1 && photometric != PHOTOMETRIC_MINISBLACK) ||
        (samplesPerPixel == 3 && photometric != PHOTOMETRIC_RGB) ||
        planarConfig != PLANARCONFIG_CONTIG ||
        orientation != ORIENTATION_TOPLEFT || TIFFIsTiled(tiff)) {
        TIFFClose(tiff);
        return false;
    }

    int outputWidth = static_cast<int>(imageWidth);
    int outputHeight = static_cast<int>(imageHeight);
    if (maximumLongSide > 0 &&
        std::max(imageWidth, imageHeight) >
            static_cast<uint32_t>(maximumLongSide)) {
        const double scale = static_cast<double>(maximumLongSide) /
            std::max(imageWidth, imageHeight);
        outputWidth = std::max(1, static_cast<int>(
            std::lround(imageWidth * scale)));
        outputHeight = std::max(1, static_cast<int>(
            std::lround(imageHeight * scale)));
    }

    size_t outputSamples = 0;
    if (!checkedSampleCount(outputWidth, outputHeight, 3,
                            outputSamples)) {
        TIFFClose(tiff);
        return false;
    }
    // Full-resolution loading is only used by small trusted test fixtures.
    // History previews always pass a long-side bound; keep an independent
    // hard ceiling so a forged header cannot request an unbounded allocation.
    constexpr size_t kMaximumDecodedSamples = 150'000'000;
    if (outputSamples > kMaximumDecodedSamples) {
        TIFFClose(tiff);
        return false;
    }
    const tmsize_t scanlineBytes = TIFFScanlineSize(tiff);
    const uint64_t requiredBytes = static_cast<uint64_t>(imageWidth) *
        samplesPerPixel * sizeof(uint16_t);
    constexpr size_t kMaximumScanlineBytes = 256U * 1024U * 1024U;
    if (scanlineBytes <= 0 ||
        static_cast<uint64_t>(scanlineBytes) > kMaximumScanlineBytes ||
        static_cast<uint64_t>(scanlineBytes) < requiredBytes) {
        TIFFClose(tiff);
        return false;
    }

    try {
        rgb.resize(outputSamples);
        std::vector<uint16_t> scanline(
            (static_cast<size_t>(scanlineBytes) + sizeof(uint16_t) - 1) /
            sizeof(uint16_t));
        for (int outputRow = 0; outputRow < outputHeight; ++outputRow) {
            if (cancelled && cancelled()) {
                rgb.clear();
                TIFFClose(tiff);
                return false;
            }
            const uint32_t sourceRow = std::min(
                imageHeight - 1,
                static_cast<uint32_t>(
                    (static_cast<uint64_t>(outputRow) * 2 + 1) *
                    imageHeight / (static_cast<uint64_t>(outputHeight) * 2)));
            if (TIFFReadScanline(tiff, scanline.data(), sourceRow, 0) != 1) {
                rgb.clear();
                TIFFClose(tiff);
                return false;
            }
            uint16_t* output = rgb.data() +
                static_cast<size_t>(outputRow) * outputWidth * 3;
            for (int outputColumn = 0;
                 outputColumn < outputWidth; ++outputColumn) {
                const uint32_t sourceColumn = std::min(
                    imageWidth - 1,
                    static_cast<uint32_t>(
                        (static_cast<uint64_t>(outputColumn) * 2 + 1) *
                        imageWidth /
                        (static_cast<uint64_t>(outputWidth) * 2)));
                if (samplesPerPixel == 3) {
                    const size_t source =
                        static_cast<size_t>(sourceColumn) * 3;
                    output[outputColumn * 3] = scanline[source];
                    output[outputColumn * 3 + 1] = scanline[source + 1];
                    output[outputColumn * 3 + 2] = scanline[source + 2];
                } else {
                    const uint16_t value = scanline[sourceColumn];
                    output[outputColumn * 3] = value;
                    output[outputColumn * 3 + 1] = value;
                    output[outputColumn * 3 + 2] = value;
                }
            }
        }
    } catch (const std::bad_alloc&) {
        rgb.clear();
        TIFFClose(tiff);
        return false;
    } catch (const std::length_error&) {
        rgb.clear();
        TIFFClose(tiff);
        return false;
    }
    TIFFClose(tiff);
    width = outputWidth;
    height = outputHeight;
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}
