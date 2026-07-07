#include "ImageAligner.h"
#include <cmath>
#include <algorithm>
#include <random>

// 三角形结构
struct Triangle {
    int idx[3];  // 星点索引
    double sides[3]; // 边长，排序后
    double ratio[2]; // 短边/长边，中边/长边
};

static std::vector<Triangle> buildTriangles(const std::vector<StarPoint>& stars, double maxDist) {
    std::vector<Triangle> triangles;
    int n = static_cast<int>(stars.size());
    if (n < 3) return triangles;

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

                Triangle tri;
                tri.idx[0] = i; tri.idx[1] = j; tri.idx[2] = k;

                double sorted[3] = {s1, s2, s3};
                std::sort(sorted, sorted + 3);
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

    // 估计最大距离（图像对角线的50%）
    double maxDist = 1000.0; // 保守估计

    auto refTris = buildTriangles(refStars, maxDist);
    auto srcTris = buildTriangles(srcStars, maxDist);

    if (refTris.empty() || srcTris.empty()) return false;

    // 投票匹配
    std::vector<std::vector<int>> vote(refStars.size(), std::vector<int>(srcStars.size(), 0));

    for (const auto& rt : refTris) {
        for (const auto& st : srcTris) {
            // 三角形相似度判断
            if (std::abs(rt.ratio[0] - st.ratio[0]) > 0.05) continue;
            if (std::abs(rt.ratio[1] - st.ratio[1]) > 0.05) continue;
            if (std::abs(rt.sides[2] - st.sides[2]) / rt.sides[2] > 0.3) continue;

            // 投票
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    vote[rt.idx[a]][st.idx[b]]++;
                }
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

static bool isInlier(const StarPoint& ref, const StarPoint& src, const AlignmentTransform& t, double threshold) {
    double dx = t.a * src.x + t.b * src.y + t.c - ref.x;
    double dy = t.d * src.x + t.e * src.y + t.f - ref.y;
    return (dx * dx + dy * dy) < (threshold * threshold);
}

bool ImageAligner::ransacAffine(const std::vector<StarPoint>& refStars,
                                  const std::vector<StarPoint>& srcStars,
                                  const std::vector<std::pair<int, int>>& matches,
                                  AlignmentTransform& out) {
    if (matches.size() < 3) return false;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(matches.size()) - 1);

    int bestInliers = 0;
    AlignmentTransform bestT;
    double threshold = 2.0; // 像素误差阈值

    for (int iter = 0; iter < 100; ++iter) {
        // 随机采样3对
        std::vector<std::pair<int, int>> sample;
        std::vector<int> idx(matches.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::shuffle(idx.begin(), idx.end(), rng);

        for (int i = 0; i < 3 && i < static_cast<int>(idx.size()); ++i) {
            sample.push_back(matches[idx[i]]);
        }

        AlignmentTransform t;
        if (!solveAffine(refStars, srcStars, sample, t)) continue;

        // 验证内点率
        int inliers = 0;
        for (const auto& m : matches) {
            if (isInlier(refStars[m.first], srcStars[m.second], t, threshold)) {
                inliers++;
            }
        }

        if (inliers > bestInliers) {
            bestInliers = inliers;
            bestT = t;
        }
    }

    if (bestInliers < 3) return false;

    // 用所有内点重新拟合
    std::vector<std::pair<int, int>> inlierMatches;
    for (const auto& m : matches) {
        if (isInlier(refStars[m.first], srcStars[m.second], bestT, threshold)) {
            inlierMatches.push_back(m);
        }
    }

    if (inlierMatches.size() >= 3) {
        solveAffine(refStars, srcStars, inlierMatches, out);
    } else {
        out = bestT;
    }

    return true;
}

bool ImageAligner::align(const std::vector<StarPoint>& refStars,
                           const std::vector<StarPoint>& srcStars,
                           AlignmentTransform& out) {
    std::vector<std::pair<int, int>> matches;
    if (!triangleMatch(refStars, srcStars, matches)) return false;
    return ransacAffine(refStars, srcStars, matches, out);
}

bool ImageAligner::applyTransform(const std::vector<uint16_t>& src, int w, int h,
                                    const AlignmentTransform& t,
                                    std::vector<uint16_t>& dst) {
    dst.resize(w * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 目标坐标 (x,y) 对应源坐标：使用逆变换
            double det = t.a * t.e - t.b * t.d;
            if (std::abs(det) < 1e-12) {
                dst[y * w + x] = 0;
                continue;
            }
            double sx = (t.e * x - t.b * y + t.b * t.f - t.e * t.c) / det;
            double sy = (-t.d * x + t.a * y + t.d * t.c - t.a * t.f) / det;

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
