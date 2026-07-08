#include "StackingEngine.h"
#include <algorithm>
#include <numeric>
#include <cmath>

bool StackingEngine::stack(const std::vector<std::vector<uint16_t>>& images,
                             int width, int height,
                             Method method,
                             double kappa,
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
        case KappaSigma:
            stackKappaSigma(images, width, height, kappa, result);
            return true;
        case Winsorized:
            stackWinsorized(images, width, height, kappa, result);
            return true;
    }

    return false;
}

StackingEngine::Method StackingEngine::recommendMethod(int frameCount) {
    if (frameCount <= 5) return Median;
    if (frameCount <= 15) return KappaSigma;
    return Winsorized;
}

void StackingEngine::stackAverage(const std::vector<std::vector<uint16_t>>& images,
                                    int w, int h, std::vector<uint16_t>& result) {
    result.resize(w * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint64_t sum = 0;
            int count = 0;
            for (const auto& img : images) {
                uint16_t val = img[y * w + x];
                if (val != 0) {
                    sum += val;
                    ++count;
                }
            }
            if (count > 0) {
                result[y * w + x] = static_cast<uint16_t>(sum / count);
            } else {
                result[y * w + x] = 0;
            }
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
                uint16_t val = img[y * w + x];
                if (val != 0) {
                    pixels.push_back(val);
                }
            }
            if (pixels.empty()) {
                result[y * w + x] = 0;
            } else {
                size_t mid = pixels.size() / 2;
                std::nth_element(pixels.begin(), pixels.begin() + mid, pixels.end());
                result[y * w + x] = pixels[mid];
            }
        }
    }
}

static double computeMedian(std::vector<uint16_t>& v) {
    size_t n = v.size();
    if (n == 0) return 0.0;
    size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    double med = static_cast<double>(v[mid]);
    if (n % 2 == 0) {
        double med2 = static_cast<double>(*std::max_element(v.begin(), v.begin() + mid));
        med = (med + med2) / 2.0;
    }
    return med;
}

static double computeStdDev(const std::vector<uint16_t>& v, double mean) {
    double sum = 0.0;
    for (uint16_t val : v) {
        double d = static_cast<double>(val) - mean;
        sum += d * d;
    }
    return std::sqrt(sum / v.size());
}

static double computeMAD(std::vector<uint16_t>& v, double med) {
    std::vector<double> absDevs;
    absDevs.reserve(v.size());
    for (uint16_t val : v) {
        absDevs.push_back(std::abs(static_cast<double>(val) - med));
    }
    size_t mid = absDevs.size() / 2;
    std::nth_element(absDevs.begin(), absDevs.begin() + mid, absDevs.end());
    return absDevs[mid];
}

void StackingEngine::stackKappaSigma(const std::vector<std::vector<uint16_t>>& images,
                                      int w, int h, double kappa,
                                      std::vector<uint16_t>& result) {
    result.resize(w * h);
    int n = static_cast<int>(images.size());
    std::vector<uint16_t> pixels;
    pixels.reserve(n);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            pixels.clear();
            for (const auto& img : images) {
                uint16_t val = img[y * w + x];
                if (val != 0) pixels.push_back(val);
            }
            if (pixels.empty()) {
                result[y * w + x] = 0;
                continue;
            }

            std::vector<uint16_t> working = pixels;
            double med = computeMedian(working);
            double std = computeStdDev(pixels, med);

            for (int iter = 0; iter < 3; ++iter) {
                if (std == 0.0) break;
                double threshold = kappa * std;
                std::vector<uint16_t> filtered;
                filtered.reserve(pixels.size());
                for (uint16_t val : pixels) {
                    if (std::abs(static_cast<double>(val) - med) <= threshold) {
                        filtered.push_back(val);
                    }
                }
                if (filtered.empty()) break;
                pixels = std::move(filtered);
                working = pixels;
                med = computeMedian(working);
                std = computeStdDev(pixels, med);
            }

            if (pixels.empty()) {
                pixels = working;
                size_t mid = pixels.size() / 2;
                std::nth_element(pixels.begin(), pixels.begin() + mid, pixels.end());
                result[y * w + x] = pixels[mid];
            } else {
                uint64_t sum = 0;
                for (uint16_t val : pixels) {
                    sum += val;
                }
                result[y * w + x] = static_cast<uint16_t>(sum / pixels.size());
            }
        }
    }
}

void StackingEngine::stackWinsorized(const std::vector<std::vector<uint16_t>>& images,
                                      int w, int h, double kappa,
                                      std::vector<uint16_t>& result) {
    result.resize(w * h);
    int n = static_cast<int>(images.size());
    std::vector<uint16_t> pixels;
    pixels.reserve(n);
    const double MAD_TO_STD = 1.4826;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            pixels.clear();
            for (const auto& img : images) {
                uint16_t val = img[y * w + x];
                if (val != 0) pixels.push_back(val);
            }
            if (pixels.empty()) {
                result[y * w + x] = 0;
                continue;
            }

            std::vector<uint16_t> working = pixels;
            double med = computeMedian(working);
            double mad = computeMAD(pixels, med);
            double threshold = kappa * mad * MAD_TO_STD;
            double lo = med - threshold;
            double hi = med + threshold;

            double sum = 0.0;
            for (uint16_t val : pixels) {
                double clipped = std::max(lo, std::min(hi, static_cast<double>(val)));
                sum += clipped;
            }
            result[y * w + x] = static_cast<uint16_t>(sum / pixels.size() + 0.5);
        }
    }
}

bool StackingEngine::stackWithMask(
    const std::vector<std::vector<uint16_t>>& images,
    const std::vector<std::vector<uint16_t>>& originalImages,
    int width, int height,
    Method method, double kappa,
    const std::vector<uint8_t>& mask,
    std::vector<uint16_t>& result)
{
    if (images.empty() || originalImages.empty() || width <= 0 || height <= 0) {
        return false;
    }
    if (mask.size() != static_cast<size_t>(width * height)) {
        return false;
    }
    for (const auto& img : images) {
        if (static_cast<int>(img.size()) != width * height) {
            return false;
        }
    }
    for (const auto& img : originalImages) {
        if (static_cast<int>(img.size()) != width * height) {
            return false;
        }
    }

    // 1. 天空区域：用对齐后的 images 堆栈
    std::vector<std::vector<uint16_t>> skyImages;
    skyImages.reserve(images.size());
    for (const auto& img : images) {
        std::vector<uint16_t> skyImg(img.size());
        for (size_t i = 0; i < img.size(); ++i) {
            skyImg[i] = (mask[i] > 0) ? img[i] : static_cast<uint16_t>(0);
        }
        skyImages.push_back(std::move(skyImg));
    }

    std::vector<uint16_t> skyResult;
    if (!stack(skyImages, width, height, method, kappa, skyResult)) {
        return false;
    }

    // 2. 地景区域：用原始 originalImages 直接堆栈（Average固定）
    std::vector<std::vector<uint16_t>> groundImages;
    groundImages.reserve(originalImages.size());
    for (const auto& img : originalImages) {
        std::vector<uint16_t> groundImg(img.size());
        for (size_t i = 0; i < img.size(); ++i) {
            groundImg[i] = (mask[i] < 255) ? img[i] : static_cast<uint16_t>(0);
        }
        groundImages.push_back(std::move(groundImg));
    }

    std::vector<uint16_t> groundResult;
    stackAverage(groundImages, width, height, groundResult);

    // 3. 融合：result = sky * (mask/255) + ground * (1 - mask/255)
    result.resize(width * height);
    for (size_t i = 0; i < result.size(); ++i) {
        double alpha = mask[i] / 255.0;
        double val = skyResult[i] * alpha + groundResult[i] * (1.0 - alpha);
        result[i] = static_cast<uint16_t>(std::min(65535.0, val));
    }
    return true;
}
