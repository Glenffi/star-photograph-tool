#include "StackingEngine.h"
#include <algorithm>
#include <numeric>
#include <cmath>

bool StackingEngine::stack(const std::vector<std::vector<uint16_t>>& images,
                             int width, int height,
                             Method method,
                             std::vector<uint16_t>& result) {
    if (images.empty() || width <= 0 || height <= 0) {
        return false;
    }

    for (const auto& img : images) {
        if (static_cast<int>(img.size()) != width * height) {
            return false;
        }
    }

    switch (method) {
        case Average:
            stackAverage(images, width, height, result);
            return true;
        case Median:
            stackMedian(images, width, height, result);
            return true;
    }

    return false;
}

void StackingEngine::stackAverage(const std::vector<std::vector<uint16_t>>& images,
                                    int w, int h, std::vector<uint16_t>& result) {
    int n = static_cast<int>(images.size());
    result.resize(w * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint64_t sum = 0;
            for (const auto& img : images) {
                sum += img[y * w + x];
            }
            result[y * w + x] = static_cast<uint16_t>(sum / n);
        }
    }
}

void StackingEngine::stackMedian(const std::vector<std::vector<uint16_t>>& images,
                                   int w, int h, std::vector<uint16_t>& result) {
    result.resize(w * h);
    std::vector<uint16_t> pixels;
    pixels.reserve(images.size());

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            pixels.clear();
            for (const auto& img : images) {
                pixels.push_back(img[y * w + x]);
            }
            size_t mid = pixels.size() / 2;
            std::nth_element(pixels.begin(), pixels.begin() + mid, pixels.end());
            result[y * w + x] = pixels[mid];
        }
    }
}
