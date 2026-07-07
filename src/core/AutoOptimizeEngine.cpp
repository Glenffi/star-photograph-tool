#include "AutoOptimizeEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static void normalizeToFloat(const std::vector<uint16_t>& src, std::vector<float>& dst, int w, int h) {
    dst.resize(w * h);
    for (int i = 0; i < w * h; ++i) {
        dst[i] = static_cast<float>(src[i]) / 65535.0f;
    }
}

static void convertToUint16(const std::vector<float>& src, std::vector<uint16_t>& dst, int w, int h) {
    dst.resize(w * h);
    for (int i = 0; i < w * h; ++i) {
        float v = std::max(0.0f, std::min(1.0f, src[i]));
        dst[i] = static_cast<uint16_t>(v * 65535.0f + 0.5f);
    }
}

static void computeDarkChannel(const std::vector<float>& img, int w, int h, std::vector<float>& dark, int patchSize) {
    dark.resize(w * h);
    int half = patchSize / 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float minVal = 1.0f;
            for (int dy = -half; dy <= half; ++dy) {
                int yy = std::max(0, std::min(h - 1, y + dy));
                for (int dx = -half; dx <= half; ++dx) {
                    int xx = std::max(0, std::min(w - 1, x + dx));
                    minVal = std::min(minVal, img[yy * w + xx]);
                }
            }
            dark[y * w + x] = minVal;
        }
    }
}

static float estimateAtmosphericLight(const std::vector<float>& img, const std::vector<float>& dark, int w, int h) {
    int total = w * h;
    int topCount = std::max(1, static_cast<int>(total * 0.001));
    std::vector<std::pair<float, int>> darkIndexed;
    darkIndexed.reserve(total);
    for (int i = 0; i < total; ++i) {
        darkIndexed.emplace_back(dark[i], i);
    }
    std::partial_sort(darkIndexed.begin(), darkIndexed.begin() + topCount, darkIndexed.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    
    double sum = 0.0;
    for (int i = 0; i < topCount; ++i) {
        sum += img[darkIndexed[i].second];
    }
    return static_cast<float>(sum / topCount);
}

static void boxFilter(const std::vector<float>& src, std::vector<float>& dst, int w, int h, int r) {
    dst.resize(w * h);
    std::vector<float> temp(w * h);
    
    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        float sum = 0.0f;
        for (int x = 0; x <= r && x < w; ++x) {
            sum += src[y * w + x];
        }
        for (int x = 0; x < w; ++x) {
            int left = x - r - 1;
            int right = x + r;
            if (left >= 0) sum -= src[y * w + left];
            if (right < w) sum += src[y * w + right];
            int count = std::min(x + r, w - 1) - std::max(x - r, 0) + 1;
            temp[y * w + x] = sum / count;
        }
    }
    
    // Vertical pass
    for (int x = 0; x < w; ++x) {
        float sum = 0.0f;
        for (int y = 0; y <= r && y < h; ++y) {
            sum += temp[y * w + x];
        }
        for (int y = 0; y < h; ++y) {
            int top = y - r - 1;
            int bottom = y + r;
            if (top >= 0) sum -= temp[top * w + x];
            if (bottom < h) sum += temp[bottom * w + x];
            int count = std::min(y + r, h - 1) - std::max(y - r, 0) + 1;
            dst[y * w + x] = sum / count;
        }
    }
}

void AutoOptimizeEngine::guidedFilter(const std::vector<float>& guide, const std::vector<float>& input,
                                      std::vector<float>& output, int w, int h, int r, float eps) {
    output.resize(w * h);
    std::vector<float> mean_I(w * h), mean_p(w * h), mean_Ip(w * h);
    std::vector<float> mean_II(w * h), a(w * h), b(w * h);
    
    boxFilter(guide, mean_I, w, h, r);
    boxFilter(input, mean_p, w, h, r);
    
    std::vector<float> Ip(w * h), II(w * h);
    for (int i = 0; i < w * h; ++i) {
        Ip[i] = guide[i] * input[i];
        II[i] = guide[i] * guide[i];
    }
    boxFilter(Ip, mean_Ip, w, h, r);
    boxFilter(II, mean_II, w, h, r);
    
    for (int i = 0; i < w * h; ++i) {
        float cov = mean_Ip[i] - mean_I[i] * mean_p[i];
        float var = mean_II[i] - mean_I[i] * mean_I[i];
        a[i] = cov / (var + eps);
        b[i] = mean_p[i] - a[i] * mean_I[i];
    }
    
    std::vector<float> mean_a(w * h), mean_b(w * h);
    boxFilter(a, mean_a, w, h, r);
    boxFilter(b, mean_b, w, h, r);
    
    for (int i = 0; i < w * h; ++i) {
        output[i] = mean_a[i] * guide[i] + mean_b[i];
    }
}

bool AutoOptimizeEngine::dehaze(const std::vector<uint16_t>& src, int w, int h,
                                 int strength, std::vector<uint16_t>& dst) {
    if (src.empty() || w <= 0 || h <= 0 || static_cast<int>(src.size()) != w * h) {
        return false;
    }
    if (strength <= 0) {
        dst = src;
        return true;
    }
    
    std::vector<float> img;
    normalizeToFloat(src, img, w, h);
    
    // Dark Channel Prior
    std::vector<float> dark;
    computeDarkChannel(img, w, h, dark, 15);
    
    // Estimate atmospheric light
    float A = estimateAtmosphericLight(img, dark, w, h);
    A = std::max(A, 0.001f);
    
    // Estimate transmission
    float omega = 0.95f * (static_cast<float>(strength) / 100.0f);
    std::vector<float> t(w * h);
    for (int i = 0; i < w * h; ++i) {
        t[i] = 1.0f - omega * (dark[i] / A);
    }
    
    // Guided Filter refine transmission
    std::vector<float> t_refined;
    guidedFilter(img, t, t_refined, w, h, 40, 0.001f);
    
    // Recover image
    std::vector<float> result(w * h);
    for (int i = 0; i < w * h; ++i) {
        float t_clamped = std::max(t_refined[i], 0.1f);
        result[i] = (img[i] - A) / t_clamped + A;
        result[i] = std::max(0.0f, std::min(1.0f, result[i]));
    }
    
    // Blend based on strength
    float blend = static_cast<float>(strength) / 100.0f;
    for (int i = 0; i < w * h; ++i) {
        result[i] = img[i] * (1.0f - blend) + result[i] * blend;
    }
    
    convertToUint16(result, dst, w, h);
    return true;
}

static float computePercentile(const std::vector<uint16_t>& data, float percentile) {
    std::vector<uint16_t> sorted = data;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(percentile / 100.0f * (sorted.size() - 1));
    return static_cast<float>(sorted[idx]);
}

bool AutoOptimizeEngine::stretchCurve(const std::vector<uint16_t>& src, int w, int h,
                                       std::vector<uint16_t>& dst) {
    if (src.empty() || w <= 0 || h <= 0 || static_cast<int>(src.size()) != w * h) {
        return false;
    }
    
    float p1 = computePercentile(src, 1.0f);
    float p99 = computePercentile(src, 99.0f);
    
    if (p99 <= p1) {
        dst = src;
        return true;
    }
    
    float stretchFactor = 1.0f / (p99 - p1);
    float maxVal = 65535.0f;
    float asinhMax = std::asinh(maxVal * stretchFactor);
    
    dst.resize(w * h);
    float softClipStart = maxVal * 0.95f;
    float softClipRange = maxVal - softClipStart;
    
    for (int i = 0; i < w * h; ++i) {
        float input = static_cast<float>(src[i]);
        float shifted = std::max(0.0f, input - p1);
        float stretched = std::asinh(shifted * stretchFactor) / asinhMax * maxVal;
        
        // Soft-clipping for highlights
        if (stretched > softClipStart && softClipRange > 0.0f) {
            float excess = stretched - softClipStart;
            float compressed = softClipRange * (1.0f - std::exp(-excess / softClipRange));
            stretched = softClipStart + compressed;
        }
        
        dst[i] = static_cast<uint16_t>(std::max(0.0f, std::min(maxVal, stretched)) + 0.5f);
    }
    
    return true;
}
