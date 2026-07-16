#include "RawImageLoader.h"

#include <libraw/libraw.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>

namespace {

void extractMetadata(const LibRaw& processor, RawImageLoader::Metadata& out) {
    const auto& data = processor.imgdata;
    out.width = static_cast<int>(data.sizes.width);
    out.height = static_cast<int>(data.sizes.height);
    out.iso = static_cast<int>(data.other.iso_speed);
    out.exposureTime = data.other.shutter;
    out.aperture = data.other.aperture;
    out.focalLength = static_cast<int>(data.other.focal_len);
    out.cameraModel = data.idata.model;

    if (data.other.timestamp <= 0) return;

    const std::time_t timestamp = static_cast<std::time_t>(data.other.timestamp);
    std::tm localTime{};
#ifdef _WIN32
    if (localtime_s(&localTime, &timestamp) != 0) return;
#else
    if (!localtime_r(&timestamp, &localTime)) return;
#endif
    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) > 0) {
        out.timestamp = buffer;
    }
}

void copyMetadata(const RawImageLoader::Metadata& metadata,
                  RawImageLoader::ImageData& image) {
    image.iso = metadata.iso;
    image.exposureTime = metadata.exposureTime;
    image.aperture = metadata.aperture;
    image.focalLength = metadata.focalLength;
    image.cameraModel = metadata.cameraModel;
    image.timestamp = metadata.timestamp;
}

bool copyBitmap8(const libraw_processed_image_t& image,
                 RawImageLoader::PreviewData& out) {
    if (image.type != LIBRAW_IMAGE_BITMAP || image.bits != 8 ||
        image.width == 0 || image.height == 0 ||
        (image.colors != 1 && image.colors < 3)) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(image.width) * image.height;
    if (pixelCount > std::numeric_limits<size_t>::max() / 3) return false;
    const size_t sourceSize = pixelCount * image.colors;
    if (image.data_size < sourceSize) return false;

    out.width = static_cast<int>(image.width);
    out.height = static_cast<int>(image.height);
    out.encoding = RawImageLoader::PreviewData::Encoding::Rgb8;
    out.bytes.resize(pixelCount * 3);

    if (image.colors == 3) {
        std::memcpy(out.bytes.data(), image.data, out.bytes.size());
        return true;
    }

    for (size_t i = 0; i < pixelCount; ++i) {
        if (image.colors == 1) {
            out.bytes[i * 3] = image.data[i];
            out.bytes[i * 3 + 1] = image.data[i];
            out.bytes[i * 3 + 2] = image.data[i];
        } else {
            out.bytes[i * 3] = image.data[i * image.colors];
            out.bytes[i * 3 + 1] = image.data[i * image.colors + 1];
            out.bytes[i * 3 + 2] = image.data[i * image.colors + 2];
        }
    }
    return true;
}

bool loadFastHalfSize(const std::string& filePath,
                      RawImageLoader::PreviewData& out) {
    LibRaw processor;
    int result = processor.open_file(filePath.c_str());
    if (result != LIBRAW_SUCCESS) return false;
    result = processor.unpack();
    if (result != LIBRAW_SUCCESS) return false;

    // This fallback is display-oriented. half_size and bilinear demosaic keep
    // browsing responsive; the processing pipeline still uses 16-bit AHD.
    auto& params = processor.imgdata.params;
    params.half_size = 1;
    params.user_qual = 0;
    params.use_camera_wb = 1;
    params.use_camera_matrix = 1;
    params.output_bps = 8;
    params.output_color = 1;
    params.no_auto_bright = 0;

    result = processor.dcraw_process();
    if (result != LIBRAW_SUCCESS) return false;

    libraw_processed_image_t* image = processor.dcraw_make_mem_image(&result);
    if (!image || result != LIBRAW_SUCCESS) {
        if (image) LibRaw::dcraw_clear_mem(image);
        return false;
    }
    const bool copied = copyBitmap8(*image, out);
    LibRaw::dcraw_clear_mem(image);
    return copied;
}

} // namespace

bool RawImageLoader::loadMetadata(const std::string& filePath, Metadata& out) {
    out = {};
    LibRaw processor;
    const int result = processor.open_file(filePath.c_str());
    if (result != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw open_file failed: " << filePath
                  << " (code: " << result << ")" << std::endl;
        return false;
    }
    extractMetadata(processor, out);
    return true;
}

bool RawImageLoader::loadPreview(const std::string& filePath, int requestedMaxSize,
                                 PreviewData& out, Metadata* metadata) {
    out = {};
    if (requestedMaxSize <= 0) return false;

    LibRaw processor;
    int result = processor.open_file(filePath.c_str());
    if (result != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw open_file failed: " << filePath
                  << " (code: " << result << ")" << std::endl;
        return false;
    }

    Metadata localMetadata;
    extractMetadata(processor, localMetadata);
    if (metadata) *metadata = localMetadata;

    PreviewData embeddedFallback;
    bool hasEmbeddedFallback = false;
    result = processor.unpack_thumb();
    if (result == LIBRAW_SUCCESS) {
        libraw_processed_image_t* image = processor.dcraw_make_mem_thumb(&result);
        if (image && result == LIBRAW_SUCCESS) {
            PreviewData embedded;
            bool usable = false;
            if (image->type == LIBRAW_IMAGE_JPEG && image->data_size > 0) {
                embedded.encoding = PreviewData::Encoding::Jpeg;
                embedded.width = static_cast<int>(image->width);
                embedded.height = static_cast<int>(image->height);
                embedded.bytes.assign(image->data, image->data + image->data_size);
                usable = true;
            } else {
                usable = copyBitmap8(*image, embedded);
            }
            LibRaw::dcraw_clear_mem(image);

            // Tiny camera thumbnails are insufficient for the central preview.
            // A 75% threshold avoids an expensive fallback for previews that are
            // already close to the requested display size.
            const int embeddedLongSide = std::max(embedded.width, embedded.height);
            const int minimumUsefulSide = std::min(requestedMaxSize, 800) * 3 / 4;
            if (usable && embeddedLongSide >= minimumUsefulSide) {
                out = std::move(embedded);
                return true;
            }
            if (usable) {
                embeddedFallback = std::move(embedded);
                hasEmbeddedFallback = true;
            }
        } else if (image) {
            LibRaw::dcraw_clear_mem(image);
        }
    }

    if (loadFastHalfSize(filePath, out)) return true;

    // A small preview is still preferable to no image for RAW variants whose
    // full pixel payload is not supported by the installed LibRaw version.
    if (hasEmbeddedFallback) {
        out = std::move(embeddedFallback);
        return true;
    }

    std::cerr << "LibRaw preview decoding failed: " << filePath << std::endl;
    return false;
}

bool RawImageLoader::loadRaw(const std::string& filePath, ImageData& out) {
    out = {};
    LibRaw processor;

    int result = processor.open_file(filePath.c_str());
    if (result != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw open_file failed: " << filePath
                  << " (code: " << result << ")" << std::endl;
        return false;
    }

    Metadata metadata;
    extractMetadata(processor, metadata);
    copyMetadata(metadata, out);

    result = processor.unpack();
    if (result != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw unpack failed: " << filePath
                  << " (code: " << result << ")" << std::endl;
        return false;
    }

    auto& params = processor.imgdata.params;
    if (processor.imgdata.idata.filters != 0) {
        params.user_qual = 3; // AHD demosaic for the full processing path.
    }
    params.use_camera_wb = 1;
    params.use_camera_matrix = 1;
    params.output_bps = 16;
    params.no_auto_bright = 1;
    params.output_color = 1; // sRGB primaries
    params.gamm[0] = 1.0f;   // linear transfer function
    params.gamm[1] = 1.0f;

    result = processor.dcraw_process();
    if (result != LIBRAW_SUCCESS) {
        std::cerr << "LibRaw dcraw_process failed: " << filePath
                  << " (code: " << result << ")" << std::endl;
        return false;
    }

    libraw_processed_image_t* image = processor.dcraw_make_mem_image(&result);
    if (!image || result != LIBRAW_SUCCESS || image->type != LIBRAW_IMAGE_BITMAP) {
        std::cerr << "LibRaw dcraw_make_mem_image failed: " << filePath << std::endl;
        if (image) LibRaw::dcraw_clear_mem(image);
        return false;
    }

    if (image->bits != 16 || image->colors != 3) {
        std::cerr << "Unsupported LibRaw output (bits=" << image->bits
                  << ", colors=" << image->colors << "): " << filePath << std::endl;
        LibRaw::dcraw_clear_mem(image);
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(image->width) * image->height;
    if (pixelCount > std::numeric_limits<size_t>::max() / (3 * sizeof(uint16_t))) {
        LibRaw::dcraw_clear_mem(image);
        return false;
    }
    const size_t valueCount = pixelCount * 3;
    const size_t expectedSize = valueCount * sizeof(uint16_t);
    if (image->data_size < expectedSize) {
        std::cerr << "LibRaw buffer is smaller than expected: " << filePath << std::endl;
        LibRaw::dcraw_clear_mem(image);
        return false;
    }

    out.width = static_cast<int>(image->width);
    out.height = static_cast<int>(image->height);
    out.channels = 3;
    out.data.resize(valueCount);
    std::memcpy(out.data.data(), image->data, expectedSize);
    LibRaw::dcraw_clear_mem(image);
    return true;
}
