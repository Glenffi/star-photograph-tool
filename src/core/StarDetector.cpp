#include "StarDetector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

// 高斯核生成
static std::vector<float> generateGaussianKernel(int size, float sigma) {
    std::vector<float> kernel(size);
    int half = size / 2;
    float sum = 0.0f;
    for (int i = 0; i < size; ++i) {
        float x = static_cast<float>(i - half);
        kernel[i] = std::exp(-(x * x) / (2.0f * sigma * sigma));
        sum += kernel[i];
    }
    for (float& v : kernel) v /= sum;
    return kernel;
}

void StarDetector::gaussianBlur(const std::vector<uint16_t>& src, int w, int h,
                                std::vector<float>& dst, float sigma) {
    dst.resize(w * h);
    int kernelSize = static_cast<int>(std::ceil(sigma * 6.0f));
    if (kernelSize % 2 == 0) kernelSize++;
    auto kernel = generateGaussianKernel(kernelSize, sigma);
    int half = kernelSize / 2;

    std::vector<float> temp(w * h);

    // 1D 行方向
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < kernelSize; ++k) {
                int px = x + k - half;
                px = std::clamp(px, 0, w - 1);
                sum += static_cast<float>(src[y * w + px]) * kernel[k];
            }
            temp[y * w + x] = sum;
        }
    }

    // 1D 列方向
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < kernelSize; ++k) {
                int py = y + k - half;
                py = std::clamp(py, 0, h - 1);
                sum += temp[py * w + x] * kernel[k];
            }
            dst[y * w + x] = sum;
        }
    }
}

std::pair<float, float> StarDetector::estimateBackground(const std::vector<uint16_t>& image, int /*w*/, int /*h*/) {
    // 采样图像中的一部分像素来估计背景
    size_t sampleCount = std::min<size_t>(image.size(), 65536);
    size_t step = image.size() / sampleCount;
    if (step == 0) step = 1;

    std::vector<float> samples;
    samples.reserve(sampleCount);
    for (size_t i = 0; i < image.size(); i += step) {
        samples.push_back(static_cast<float>(image[i]));
    }

    // 计算中值作为背景水平
    size_t n = samples.size();
    if (n == 0) return {0.0f, 0.0f};

    std::nth_element(samples.begin(), samples.begin() + n / 2, samples.end());
    float median = samples[n / 2];

    // 计算 MAD (Median Absolute Deviation) 作为噪声估计
    std::vector<float> absDev;
    absDev.reserve(n);
    for (float v : samples) {
        absDev.push_back(std::abs(v - median));
    }
    std::nth_element(absDev.begin(), absDev.begin() + n / 2, absDev.end());
    float mad = absDev[n / 2];

    // 返回背景中值和噪声水平（MAD * 1.4826 近似标准差）
    return {median, mad * 1.4826f};
}

bool StarDetector::fit2DGaussian(const std::vector<uint16_t>& image, int w, int h,
                                 int cx, int cy, int windowSize,
                                 StarPoint& star) {
    int half = windowSize / 2;
    if (cx < half || cx >= w - half || cy < half || cy >= h - half) {
        return false;
    }

    // 收集窗口数据
    std::vector<double> xvals, yvals, zvals;
    for (int y = cy - half; y <= cy + half; ++y) {
        for (int x = cx - half; x <= cx + half; ++x) {
            xvals.push_back(static_cast<double>(x - cx));
            yvals.push_back(static_cast<double>(y - cy));
            zvals.push_back(static_cast<double>(image[y * w + x]));
        }
    }

    // 估计背景
    double zmin = *std::min_element(zvals.begin(), zvals.end());
    double zmax = *std::max_element(zvals.begin(), zvals.end());
    if (zmax <= zmin) return false;

    // 简单 2D 高斯拟合：ln(I - bg) = ln(A) - (x^2)/(2*sx^2) - (y^2)/(2*sy^2)
    // 线性化后最小二乘拟合
    std::vector<double> lnVals;
    std::vector<double> x2, y2;
    double sumX = 0.0, sumY = 0.0, sumW = 0.0;

    for (size_t i = 0; i < zvals.size(); ++i) {
        double z = zvals[i] - zmin;
        if (z > 0.1) {
            double lnZ = std::log(z);
            lnVals.push_back(lnZ);
            x2.push_back(xvals[i] * xvals[i]);
            y2.push_back(yvals[i] * yvals[i]);
            sumW += z;
            sumX += xvals[i] * z;
            sumY += yvals[i] * z;
        }
    }

    if (lnVals.size() < 10) return false;

    // 加权质心
    star.x = cx + sumX / sumW;
    star.y = cy + sumY / sumW;

    // 最小二乘拟合
    double n = static_cast<double>(lnVals.size());
    double sumL = 0.0, sumX2 = 0.0, sumY2 = 0.0, sumX2L = 0.0, sumY2L = 0.0;
    double sumX4 = 0.0, sumY4 = 0.0;

    for (size_t i = 0; i < lnVals.size(); ++i) {
        sumL += lnVals[i];
        sumX2 += x2[i];
        sumY2 += y2[i];
        sumX2L += x2[i] * lnVals[i];
        sumY2L += y2[i] * lnVals[i];
        sumX4 += x2[i] * x2[i];
        sumY4 += y2[i] * y2[i];
    }

    double detX = n * sumX4 - sumX2 * sumX2;
    double detY = n * sumY4 - sumY2 * sumY2;

    if (std::abs(detX) < 1e-10 || std::abs(detY) < 1e-10) {
        return false;
    }

    double aX = (sumX2L * n - sumL * sumX2) / detX;
    double aY = (sumY2L * n - sumL * sumY2) / detY;

    if (aX >= 0 || aY >= 0) return false;

    double sigmaX = std::sqrt(-0.5 / aX);
    double sigmaY = std::sqrt(-0.5 / aY);

    // FWHM = 2 * sqrt(2 * ln(2)) * sigma ≈ 2.355 * sigma
    star.fwhm = 2.355 * std::sqrt(sigmaX * sigmaY);
    star.ellipticity = std::abs(sigmaX - sigmaY) / std::max(sigmaX, sigmaY);

    // 计算 flux（总流量）
    double flux = 0.0;
    for (double z : zvals) {
        flux += (z - zmin);
    }
    star.flux = flux;

    return true;
}

bool StarDetector::detect(const std::vector<uint16_t>& image, int width, int height,
                          std::vector<StarPoint>& stars,
                          double thresholdSigma) {
    stars.clear();

    if (image.empty() || width <= 0 || height <= 0) {
        return false;
    }

    // 1. 高斯模糊
    std::vector<float> blurred;
    gaussianBlur(image, width, height, blurred, 1.5f);

    // 2. 估计背景中值和噪声（基于模糊图像，因为检测在 blurred 上进行）
    float bgNoise = 0.0f;
    float backgroundMedian = 0.0f;
    {
        size_t sampleCount = std::min<size_t>(blurred.size(), 65536);
        size_t step = blurred.size() / sampleCount;
        if (step == 0) step = 1;
        std::vector<float> samples;
        samples.reserve(sampleCount);
        for (size_t i = 0; i < blurred.size(); i += step) {
            samples.push_back(blurred[i]);
        }
        size_t n = samples.size();
        if (n > 0) {
            std::nth_element(samples.begin(), samples.begin() + n / 2, samples.end());
            float median = samples[n / 2];
            backgroundMedian = median;
            std::vector<float> absDev;
            absDev.reserve(n);
            for (float v : samples) {
                absDev.push_back(std::abs(v - median));
            }
            std::nth_element(absDev.begin(), absDev.begin() + n / 2, absDev.end());
            bgNoise = absDev[n / 2] * 1.4826f;
        }
    }
    if (bgNoise < 0.5f) bgNoise = 0.5f;
    float threshold = backgroundMedian + static_cast<float>(thresholdSigma) * bgNoise;

    // 3. 找局部最大值（3×3窗口）
    std::vector<std::pair<int, int>> candidates;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            float val = blurred[y * width + x];
            if (val < threshold) continue;

            bool isLocalMax = true;
            for (int dy = -1; dy <= 1 && isLocalMax; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (blurred[(y + dy) * width + (x + dx)] >= val) {
                        isLocalMax = false;
                        break;
                    }
                }
            }

            if (isLocalMax) {
                candidates.emplace_back(x, y);
            }
        }
    }

    // 4. 2D 高斯拟合
    for (const auto& [cx, cy] : candidates) {
        StarPoint star;
        if (fit2DGaussian(image, width, height, cx, cy, 7, star)) {
            if (star.fwhm > 0.5 && star.fwhm < 50.0 && star.flux > 0) {
                stars.push_back(star);
            }
        }
    }

    // 按亮度排序
    std::sort(stars.begin(), stars.end(), [](const StarPoint& a, const StarPoint& b) {
        return a.flux > b.flux;
    });

    return !stars.empty();
}
