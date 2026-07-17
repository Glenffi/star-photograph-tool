#include "ImageAligner.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>

// 三角形结构
struct Triangle {
    int idx[3];  // 按对边长度排序后的顶点索引，保证相似三角形顶点一一对应
    double sides[3]; // 边长，排序后
    double ratio[2]; // 短边/长边，中边/长边
};

static std::vector<Triangle> buildTriangles(const std::vector<StarPoint>& stars, double maxDist) {
    std::vector<Triangle> triangles;
    int n = static_cast<int>(stars.size());
    if (n < 3) return triangles;

    // Triangle-to-triangle comparison grows as O(n^6). Keep geometric hashing
    // at 30 stars; the translation fallback can still use the complete list.
    if (n > 30) n = 30;

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                double dx1 = stars[j].x - stars[i].x;
                double dy1 = stars[j].y - stars[i].y;
                double dx2 = stars[k].x - stars[i].x;
                double dy2 = stars[k].y - stars[i].y;
                double dx3 = stars[k].x - stars[j].x;
                double dy3 = stars[k].y - stars[j].y;

                double s1 = std::sqrt(dx1*dx1 + dy1*dy1);
                double s2 = std::sqrt(dx2*dx2 + dy2*dy2);
                double s3 = std::sqrt(dx3*dx3 + dy3*dy3);

                if (s1 > maxDist || s2 > maxDist || s3 > maxDist) continue;
                if (s1 < 1.0 || s2 < 1.0 || s3 < 1.0) continue;

                double sorted[3] = {s1, s2, s3};
                std::sort(sorted, sorted + 3);
                const double areaTwice = std::abs(dx1 * dy2 - dy1 * dx2);
                if (areaTwice < 0.02 * sorted[2] * sorted[2]) continue;
                // Near-isosceles triangles do not provide a stable canonical
                // vertex ordering when measurement noise swaps equal sides.
                if ((sorted[1] - sorted[0]) / sorted[2] < 0.015 ||
                    (sorted[2] - sorted[1]) / sorted[2] < 0.015) continue;

                Triangle tri;
                // Opposite-edge lengths identify corresponding vertices in
                // similar scalene triangles: i<->|jk|, j<->|ik|, k<->|ij|.
                std::pair<double, int> vertices[3] = {{s3, i}, {s2, j}, {s1, k}};
                std::sort(vertices, vertices + 3,
                          [](const auto& left, const auto& right) {
                              return left.first < right.first;
                          });
                for (int vertex = 0; vertex < 3; ++vertex) {
                    tri.idx[vertex] = vertices[vertex].second;
                }
                tri.sides[0] = sorted[0];
                tri.sides[1] = sorted[1];
                tri.sides[2] = sorted[2];
                tri.ratio[0] = sorted[0] / sorted[2];
                tri.ratio[1] = sorted[1] / sorted[2];

                triangles.push_back(tri);
            }
        }
    }

    return triangles;
}

bool ImageAligner::triangleMatch(const std::vector<StarPoint>& refStars,
                                   const std::vector<StarPoint>& srcStars,
                                   std::vector<std::pair<int, int>>& matches) {
    matches.clear();
    if (refStars.size() < 3 || srcStars.size() < 3) return false;

    auto coveredDiagonal = [](const std::vector<StarPoint>& stars) {
        double minX = stars.front().x;
        double maxX = stars.front().x;
        double minY = stars.front().y;
        double maxY = stars.front().y;
        for (const StarPoint& star : stars) {
            minX = std::min(minX, star.x);
            maxX = std::max(maxX, star.x);
            minY = std::min(minY, star.y);
            maxY = std::max(maxY, star.y);
        }
        return std::hypot(maxX - minX, maxY - minY);
    };
    // A fixed 1000 px limit discards almost every useful triangle on 36 MP
    // frames. Derive the limit from the actual spatial star coverage.
    const double maxDist = std::max(1000.0,
        0.75 * std::max(coveredDiagonal(refStars), coveredDiagonal(srcStars)));

    auto refTris = buildTriangles(refStars, maxDist);
    auto srcTris = buildTriangles(srcStars, maxDist);

    if (refTris.empty() || srcTris.empty()) return false;

    // 投票匹配
    std::vector<std::vector<int>> vote(refStars.size(), std::vector<int>(srcStars.size(), 0));

    for (const auto& rt : refTris) {
        for (const auto& st : srcTris) {
            // 三角形相似度判断
            if (std::abs(rt.ratio[0] - st.ratio[0]) > 0.025) continue;
            if (std::abs(rt.ratio[1] - st.ratio[1]) > 0.025) continue;
            if (std::abs(rt.sides[2] - st.sides[2]) / rt.sides[2] > 0.1) continue;

            // Canonical vertex ordering makes the correspondence explicit.
            for (int a = 0; a < 3; ++a) {
                vote[rt.idx[a]][st.idx[a]]++;
            }
        }
    }

    // 取票数最高的匹配对
    std::vector<bool> refUsed(refStars.size(), false);
    std::vector<bool> srcUsed(srcStars.size(), false);

    for (int iter = 0; iter < static_cast<int>(std::min(refStars.size(), srcStars.size())); ++iter) {
        int bestVote = 0;
        int bestRef = -1, bestSrc = -1;

        for (size_t i = 0; i < refStars.size(); ++i) {
            if (refUsed[i]) continue;
            for (size_t j = 0; j < srcStars.size(); ++j) {
                if (srcUsed[j]) continue;
                if (vote[i][j] > bestVote) {
                    bestVote = vote[i][j];
                    bestRef = static_cast<int>(i);
                    bestSrc = static_cast<int>(j);
                }
            }
        }

        if (bestVote < 2) break;

        matches.emplace_back(bestRef, bestSrc);
        refUsed[bestRef] = true;
        srcUsed[bestSrc] = true;
    }

    return matches.size() >= 3;
}

bool ImageAligner::translationSeedMatch(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    std::vector<std::pair<int, int>>& matches) {
    matches.clear();
    if (refStars.size() < 3 || srcStars.size() < 3) return false;

    constexpr double matchRadius = 96.0;
    const double radiusSquared = matchRadius * matchRadius;
    double bestError = std::numeric_limits<double>::max();

    // Every ref/src pair supplies a coarse translation hypothesis. For a real
    // fixed-tripod sequence, many other stars then land close to unique peers;
    // random hypotheses normally explain only one or two points.
    for (size_t refSeed = 0; refSeed < refStars.size(); ++refSeed) {
        for (size_t srcSeed = 0; srcSeed < srcStars.size(); ++srcSeed) {
            const double offsetX = refStars[refSeed].x - srcStars[srcSeed].x;
            const double offsetY = refStars[refSeed].y - srcStars[srcSeed].y;
            std::vector<bool> refUsed(refStars.size(), false);
            std::vector<std::pair<int, int>> candidate;
            double error = 0.0;

            for (size_t srcIndex = 0; srcIndex < srcStars.size(); ++srcIndex) {
                int nearestRef = -1;
                double nearestDistance = radiusSquared;
                const double x = srcStars[srcIndex].x + offsetX;
                const double y = srcStars[srcIndex].y + offsetY;
                for (size_t refIndex = 0; refIndex < refStars.size(); ++refIndex) {
                    if (refUsed[refIndex]) continue;
                    const double dx = refStars[refIndex].x - x;
                    const double dy = refStars[refIndex].y - y;
                    const double distance = dx * dx + dy * dy;
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearestRef = static_cast<int>(refIndex);
                    }
                }
                if (nearestRef >= 0) {
                    refUsed[static_cast<size_t>(nearestRef)] = true;
                    candidate.emplace_back(nearestRef, static_cast<int>(srcIndex));
                    error += nearestDistance;
                }
            }

            if (candidate.size() > matches.size() ||
                (candidate.size() == matches.size() && error < bestError)) {
                matches = std::move(candidate);
                bestError = error;
            }
        }
    }
    return matches.size() >= 4;
}

// 解仿射矩阵：从3对点
static bool solveAffine(const std::vector<StarPoint>& refStars,
                        const std::vector<StarPoint>& srcStars,
                        const std::vector<std::pair<int, int>>& sampleIdx,
                        AlignmentTransform& t) {
    if (sampleIdx.size() < 3) return false;

    // 使用3对点解线性方程组
    double A[6][6] = {};
    double B[6] = {};

    for (int i = 0; i < 3; ++i) {
        const auto& ref = refStars[sampleIdx[i].first];
        const auto& src = srcStars[sampleIdx[i].second];

        A[i][0] = src.x; A[i][1] = src.y; A[i][2] = 1.0;
        B[i] = ref.x;

        A[i + 3][3] = src.x; A[i + 3][4] = src.y; A[i + 3][5] = 1.0;
        B[i + 3] = ref.y;
    }

    // 高斯消元
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 6; ++row) {
            if (std::abs(A[row][col]) > std::abs(A[pivot][col])) {
                pivot = row;
            }
        }
        if (std::abs(A[pivot][col]) < 1e-12) return false;

        if (pivot != col) {
            for (int j = col; j < 6; ++j) std::swap(A[col][j], A[pivot][j]);
            std::swap(B[col], B[pivot]);
        }

        for (int row = col + 1; row < 6; ++row) {
            double factor = A[row][col] / A[col][col];
            for (int j = col; j < 6; ++j) A[row][j] -= factor * A[col][j];
            B[row] -= factor * B[col];
        }
    }

    double sol[6];
    for (int i = 5; i >= 0; --i) {
        double sum = B[i];
        for (int j = i + 1; j < 6; ++j) sum -= A[i][j] * sol[j];
        sol[i] = sum / A[i][i];
    }

    t.a = sol[0]; t.b = sol[1]; t.c = sol[2];
    t.d = sol[3]; t.e = sol[4]; t.f = sol[5];
    return true;
}

static bool solveAffineLeastSquares(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    const std::vector<std::pair<int, int>>& matches,
    AlignmentTransform& out) {
    if (matches.size() < 3) return false;

    // 最小二乘求解 Ax = b
    // 对每个匹配对 (ref_i, src_i):
    //   ref.x = a * src.x + b * src.y + c
    //   ref.y = d * src.x + e * src.y + f
    // 构建正规方程 A^T A x = A^T b
    // 其中 x = [a, b, c, d, e, f]^T
    double AtA[6][6] = {};
    double Atb[6] = {};

    for (const auto& m : matches) {
        const auto& ref = refStars[m.first];
        const auto& src = srcStars[m.second];

        double x = src.x;
        double y = src.y;

        // 第一行: ref.x = a*x + b*y + c
        AtA[0][0] += x * x; AtA[0][1] += x * y; AtA[0][2] += x;
        AtA[1][0] += x * y; AtA[1][1] += y * y; AtA[1][2] += y;
        AtA[2][0] += x;     AtA[2][1] += y;     AtA[2][2] += 1.0;
        Atb[0] += x * ref.x;
        Atb[1] += y * ref.x;
        Atb[2] += ref.x;

        // 第二行: ref.y = d*x + e*y + f
        AtA[3][3] += x * x; AtA[3][4] += x * y; AtA[3][5] += x;
        AtA[4][3] += x * y; AtA[4][4] += y * y; AtA[4][5] += y;
        AtA[5][3] += x;     AtA[5][4] += y;     AtA[5][5] += 1.0;
        Atb[3] += x * ref.y;
        Atb[4] += y * ref.y;
        Atb[5] += ref.y;
    }

    // 高斯消元求解
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 6; ++row) {
            if (std::abs(AtA[row][col]) > std::abs(AtA[pivot][col])) {
                pivot = row;
            }
        }
        if (std::abs(AtA[pivot][col]) < 1e-12) {
            // A^T A 接近奇异或条件数差，回退到 3 点法
            return solveAffine(refStars, srcStars, matches, out);
        }

        if (pivot != col) {
            for (int j = col; j < 6; ++j) std::swap(AtA[col][j], AtA[pivot][j]);
            std::swap(Atb[col], Atb[pivot]);
        }

        for (int row = col + 1; row < 6; ++row) {
            double factor = AtA[row][col] / AtA[col][col];
            for (int j = col; j < 6; ++j) AtA[row][j] -= factor * AtA[col][j];
            Atb[row] -= factor * Atb[col];
        }
    }

    double sol[6];
    for (int i = 5; i >= 0; --i) {
        double sum = Atb[i];
        for (int j = i + 1; j < 6; ++j) sum -= AtA[i][j] * sol[j];
        sol[i] = sum / AtA[i][i];
    }

    out.a = sol[0]; out.b = sol[1]; out.c = sol[2];
    out.d = sol[3]; out.e = sol[4]; out.f = sol[5];
    return true;
}

static bool isInlier(const StarPoint& ref, const StarPoint& src, const AlignmentTransform& t, double threshold) {
    double dx = t.a * src.x + t.b * src.y + t.c - ref.x;
    double dy = t.d * src.x + t.e * src.y + t.f - ref.y;
    return (dx * dx + dy * dy) < (threshold * threshold);
}

static bool isPlausibleTransform(const AlignmentTransform& t) {
    if (!std::isfinite(t.a) || !std::isfinite(t.b) || !std::isfinite(t.c) ||
        !std::isfinite(t.d) || !std::isfinite(t.e) || !std::isfinite(t.f)) {
        return false;
    }
    const double scaleX = std::hypot(t.a, t.d);
    const double scaleY = std::hypot(t.b, t.e);
    const double determinant = t.a * t.e - t.b * t.d;
    if (scaleX < 0.85 || scaleX > 1.15 || scaleY < 0.85 || scaleY > 1.15 ||
        determinant <= 0.0) {
        return false;
    }
    const double normalizedDot = (t.a * t.b + t.d * t.e) / (scaleX * scaleY);
    return std::abs(normalizedDot) < 0.15;
}

bool ImageAligner::ransacAffine(const std::vector<StarPoint>& refStars,
                                  const std::vector<StarPoint>& srcStars,
                                  const std::vector<std::pair<int, int>>& matches,
                                  AlignmentTransform& out) {
    if (matches.size() < 3) return false;

    // A fixed seed makes regression results reproducible.
    std::mt19937 rng(0x53544152U);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(matches.size()) - 1);

    int bestInliers = 0;
    double bestSquaredError = std::numeric_limits<double>::max();
    AlignmentTransform bestT;
    double threshold = 2.0; // 像素误差阈值

    for (int iter = 0; iter < 500; ++iter) {
        // 随机采样3对（无放回）
        std::vector<std::pair<int, int>> sample;
        std::vector<int> used;
        for (int s = 0; s < 3; ++s) {
            int idx;
            do {
                idx = dist(rng);
            } while (std::find(used.begin(), used.end(), idx) != used.end());
            used.push_back(idx);
            sample.push_back(matches[idx]);
        }

        AlignmentTransform t;
        if (!solveAffine(refStars, srcStars, sample, t) || !isPlausibleTransform(t)) continue;

        // 验证内点率
        int inliers = 0;
        double squaredError = 0.0;
        for (const auto& m : matches) {
            if (isInlier(refStars[m.first], srcStars[m.second], t, threshold)) {
                inliers++;
                const auto& ref = refStars[m.first];
                const auto& src = srcStars[m.second];
                const double dx = t.a * src.x + t.b * src.y + t.c - ref.x;
                const double dy = t.d * src.x + t.e * src.y + t.f - ref.y;
                squaredError += dx * dx + dy * dy;
            }
        }

        if (inliers > bestInliers ||
            (inliers == bestInliers && squaredError < bestSquaredError)) {
            bestInliers = inliers;
            bestSquaredError = squaredError;
            bestT = t;
        }
    }

    const int minimumInliers = matches.size() == 3
        ? 3 : std::max(4, std::min(8, static_cast<int>(std::ceil(matches.size() * 0.3))));
    if (bestInliers < minimumInliers) return false;

    // 用所有内点重新拟合
    std::vector<std::pair<int, int>> inlierMatches;
    for (const auto& m : matches) {
        if (isInlier(refStars[m.first], srcStars[m.second], bestT, threshold)) {
            inlierMatches.push_back(m);
        }
    }

    if (inlierMatches.size() >= 3) {
        if (!solveAffineLeastSquares(refStars, srcStars, inlierMatches, out)) {
            // least squares 失败（接近奇异），回退到 bestT
            out = bestT;
        }
    } else {
        out = bestT;
    }
    if (!isPlausibleTransform(out)) return false;

    int finalInliers = 0;
    double finalSquaredError = 0.0;
    for (const auto& match : matches) {
        const auto& ref = refStars[match.first];
        const auto& src = srcStars[match.second];
        const double dx = out.a * src.x + out.b * src.y + out.c - ref.x;
        const double dy = out.d * src.x + out.e * src.y + out.f - ref.y;
        if (dx * dx + dy * dy < threshold * threshold) {
            ++finalInliers;
            finalSquaredError += dx * dx + dy * dy;
        }
    }
    const double rms = finalInliers > 0
        ? std::sqrt(finalSquaredError / finalInliers)
        : std::numeric_limits<double>::max();
    return finalInliers >= minimumInliers && rms <= threshold;
}

AlignmentQuality ImageAligner::evaluateTransform(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    const AlignmentTransform& transform,
    double matchRadius) const {
    AlignmentQuality quality;
    if (matchRadius <= 0.0) return quality;
    std::vector<bool> refUsed(refStars.size(), false);
    double squaredError = 0.0;
    const double radiusSquared = matchRadius * matchRadius;

    for (const StarPoint& src : srcStars) {
        const double x = transform.a * src.x + transform.b * src.y + transform.c;
        const double y = transform.d * src.x + transform.e * src.y + transform.f;
        int nearestRef = -1;
        double nearestDistance = radiusSquared;
        for (size_t refIndex = 0; refIndex < refStars.size(); ++refIndex) {
            if (refUsed[refIndex]) continue;
            const double dx = refStars[refIndex].x - x;
            const double dy = refStars[refIndex].y - y;
            const double distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestRef = static_cast<int>(refIndex);
            }
        }
        if (nearestRef >= 0) {
            refUsed[static_cast<size_t>(nearestRef)] = true;
            ++quality.matchedStars;
            squaredError += nearestDistance;
        }
    }
    quality.rmsError = quality.matchedStars > 0
        ? std::sqrt(squaredError / quality.matchedStars)
        : std::numeric_limits<double>::max();
    return quality;
}

bool ImageAligner::align(const std::vector<StarPoint>& refStars,
                           const std::vector<StarPoint>& srcStars,
                           AlignmentTransform& out,
                           AlignmentQuality* quality) {
    std::vector<std::pair<int, int>> matches;
    bool aligned = triangleMatch(refStars, srcStars, matches) &&
                   ransacAffine(refStars, srcStars, matches, out);
    if (!aligned) {
        aligned = translationSeedMatch(refStars, srcStars, matches) &&
                  ransacAffine(refStars, srcStars, matches, out);
    }
    if (!aligned) return false;

    const AlignmentQuality verified = evaluateTransform(refStars, srcStars, out, 4.0);
    if (quality) *quality = verified;
    const int minimumMatches = std::max(4, std::min(8,
        static_cast<int>(std::ceil(std::min(refStars.size(), srcStars.size()) * 0.2))));
    return verified.matchedStars >= minimumMatches && verified.rmsError <= 2.5;
}

bool ImageAligner::applyTransform(const std::vector<uint16_t>& src, int w, int h,
                                    const AlignmentTransform& t,
                                    std::vector<uint16_t>& dst) {
    if (w <= 1 || h <= 1 ||
        static_cast<size_t>(w) > std::numeric_limits<size_t>::max() / static_cast<size_t>(h) ||
        src.size() != static_cast<size_t>(w) * static_cast<size_t>(h)) {
        return false;
    }

    const double det = t.a * t.e - t.b * t.d;
    if (!std::isfinite(det) || std::abs(det) < 1e-12) return false;
    const double sourceXFromX = t.e / det;
    const double sourceXFromY = -t.b / det;
    const double sourceXOffset = (t.b * t.f - t.e * t.c) / det;
    const double sourceYFromX = -t.d / det;
    const double sourceYFromY = t.a / det;
    const double sourceYOffset = (t.d * t.c - t.a * t.f) / det;

    dst.resize(static_cast<size_t>(w) * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // The transform maps source to destination, so resampling uses its inverse.
            const double sx = sourceXFromX * x + sourceXFromY * y + sourceXOffset;
            const double sy = sourceYFromX * x + sourceYFromY * y + sourceYOffset;

            int ix = static_cast<int>(std::floor(sx));
            int iy = static_cast<int>(std::floor(sy));
            double fx = sx - ix;
            double fy = sy - iy;

            if (ix < 0 || ix >= w - 1 || iy < 0 || iy >= h - 1) {
                dst[y * w + x] = 0;
                continue;
            }

            // 双线性插值
            uint16_t v00 = src[iy * w + ix];
            uint16_t v10 = src[iy * w + (ix + 1)];
            uint16_t v01 = src[(iy + 1) * w + ix];
            uint16_t v11 = src[(iy + 1) * w + (ix + 1)];

            double val = (1.0 - fx) * (1.0 - fy) * v00 +
                         fx * (1.0 - fy) * v10 +
                         (1.0 - fx) * fy * v01 +
                         fx * fy * v11;

            dst[y * w + x] = static_cast<uint16_t>(std::clamp(val, 0.0, 65535.0));
        }
    }

    return true;
}

bool ImageAligner::applyTransformRgb(const std::vector<uint16_t>& src, int w, int h,
                                     const AlignmentTransform& t,
                                     std::vector<uint16_t>& dst) {
    if (w <= 1 || h <= 1 ||
        static_cast<size_t>(w) > std::numeric_limits<size_t>::max() /
                                     static_cast<size_t>(h)) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (pixelCount > std::numeric_limits<size_t>::max() / 3 ||
        src.size() != pixelCount * 3) {
        return false;
    }

    const double det = t.a * t.e - t.b * t.d;
    if (!std::isfinite(det) || std::abs(det) < 1e-12) return false;
    const double sourceXFromX = t.e / det;
    const double sourceXFromY = -t.b / det;
    const double sourceXOffset = (t.b * t.f - t.e * t.c) / det;
    const double sourceYFromX = -t.d / det;
    const double sourceYFromY = t.a / det;
    const double sourceYOffset = (t.d * t.c - t.a * t.f) / det;

    dst.resize(pixelCount * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double sx = sourceXFromX * x + sourceXFromY * y + sourceXOffset;
            const double sy = sourceYFromX * x + sourceYFromY * y + sourceYOffset;
            const int ix = static_cast<int>(std::floor(sx));
            const int iy = static_cast<int>(std::floor(sy));
            const size_t destination = (static_cast<size_t>(y) * w + x) * 3;
            if (ix < 0 || ix >= w - 1 || iy < 0 || iy >= h - 1) {
                dst[destination] = 0;
                dst[destination + 1] = 0;
                dst[destination + 2] = 0;
                continue;
            }

            const double fx = sx - ix;
            const double fy = sy - iy;
            const double weight00 = (1.0 - fx) * (1.0 - fy);
            const double weight10 = fx * (1.0 - fy);
            const double weight01 = (1.0 - fx) * fy;
            const double weight11 = fx * fy;
            const size_t topLeft = (static_cast<size_t>(iy) * w + ix) * 3;
            const size_t topRight = topLeft + 3;
            const size_t bottomLeft = topLeft + static_cast<size_t>(w) * 3;
            const size_t bottomRight = bottomLeft + 3;
            for (size_t channel = 0; channel < 3; ++channel) {
                const double value = weight00 * src[topLeft + channel] +
                    weight10 * src[topRight + channel] +
                    weight01 * src[bottomLeft + channel] +
                    weight11 * src[bottomRight + channel];
                dst[destination + channel] = static_cast<uint16_t>(
                    std::clamp(value, 0.0, 65535.0));
            }
        }
    }
    return true;
}
