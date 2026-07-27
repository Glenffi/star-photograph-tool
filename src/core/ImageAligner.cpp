#include "ImageAligner.h"
#include <array>
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
    t.g = 0.0; t.h = 0.0;
    t.model = AlignmentModel::Affine;
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
    out.g = 0.0; out.h = 0.0;
    out.model = AlignmentModel::Affine;
    return true;
}

static bool mapPoint(const AlignmentTransform& transform, double x, double y,
                     double& mappedX, double& mappedY) {
    const double denominator = transform.g * x + transform.h * y + 1.0;
    if (!std::isfinite(denominator) || std::abs(denominator) < 1e-10) return false;
    mappedX = (transform.a * x + transform.b * y + transform.c) / denominator;
    mappedY = (transform.d * x + transform.e * y + transform.f) / denominator;
    return std::isfinite(mappedX) && std::isfinite(mappedY);
}

static bool invertTransform(const AlignmentTransform& transform,
                            AlignmentTransform& inverse);

struct PointNormalization {
    double centerX = 0.0;
    double centerY = 0.0;
    double scale = 1.0;
};

static PointNormalization normalizationForMatches(
    const std::vector<StarPoint>& stars,
    const std::vector<std::pair<int, int>>& matches,
    bool referencePoints) {
    PointNormalization normalization;
    for (const auto& match : matches) {
        const StarPoint& point = stars[referencePoints ? match.first : match.second];
        normalization.centerX += point.x;
        normalization.centerY += point.y;
    }
    normalization.centerX /= matches.size();
    normalization.centerY /= matches.size();
    double squaredDistance = 0.0;
    for (const auto& match : matches) {
        const StarPoint& point = stars[referencePoints ? match.first : match.second];
        const double dx = point.x - normalization.centerX;
        const double dy = point.y - normalization.centerY;
        squaredDistance += dx * dx + dy * dy;
    }
    const double rmsDistance = std::sqrt(squaredDistance / matches.size());
    if (rmsDistance > 1e-9) normalization.scale = std::sqrt(2.0) / rmsDistance;
    return normalization;
}

static void multiplyMatrix3(const double left[3][3], const double right[3][3],
                            double output[3][3]) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            output[row][column] = 0.0;
            for (int index = 0; index < 3; ++index) {
                output[row][column] += left[row][index] * right[index][column];
            }
        }
    }
}

static bool solveHomographyLeastSquares(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    const std::vector<std::pair<int, int>>& matches,
    AlignmentTransform& output) {
    if (matches.size() < 4) return false;

    const PointNormalization sourceNormalization =
        normalizationForMatches(srcStars, matches, false);
    const PointNormalization referenceNormalization =
        normalizationForMatches(refStars, matches, true);
    std::vector<std::array<double, 8>> equations;
    std::vector<double> targets;
    equations.reserve(matches.size() * 2);
    targets.reserve(matches.size() * 2);

    for (const auto& match : matches) {
        const StarPoint& source = srcStars[match.second];
        const StarPoint& reference = refStars[match.first];
        const double x = (source.x - sourceNormalization.centerX) *
            sourceNormalization.scale;
        const double y = (source.y - sourceNormalization.centerY) *
            sourceNormalization.scale;
        const double u = (reference.x - referenceNormalization.centerX) *
            referenceNormalization.scale;
        const double v = (reference.y - referenceNormalization.centerY) *
            referenceNormalization.scale;
        equations.push_back({x, y, 1.0, 0.0, 0.0, 0.0, -u * x, -u * y});
        targets.push_back(u);
        equations.push_back({0.0, 0.0, 0.0, x, y, 1.0, -v * x, -v * y});
        targets.push_back(v);
    }

    // Householder QR solves the normalized least-squares system directly.
    // Avoiding A^T A matters because normal equations square the condition number.
    const size_t rowCount = equations.size();
    for (size_t column = 0; column < 8; ++column) {
        double norm = 0.0;
        for (size_t row = column; row < rowCount; ++row) {
            norm = std::hypot(norm, equations[row][column]);
        }
        if (norm < 1e-10) return false;
        const double alpha = equations[column][column] >= 0.0 ? -norm : norm;
        std::vector<double> householder(rowCount - column);
        householder[0] = equations[column][column] - alpha;
        for (size_t row = column + 1; row < rowCount; ++row) {
            householder[row - column] = equations[row][column];
        }
        double squaredNorm = 0.0;
        for (double value : householder) squaredNorm += value * value;
        if (squaredNorm < 1e-20) return false;
        const double beta = 2.0 / squaredNorm;
        for (size_t targetColumn = column; targetColumn < 8; ++targetColumn) {
            double projection = 0.0;
            for (size_t row = column; row < rowCount; ++row) {
                projection += householder[row - column] *
                              equations[row][targetColumn];
            }
            projection *= beta;
            for (size_t row = column; row < rowCount; ++row) {
                equations[row][targetColumn] -=
                    projection * householder[row - column];
            }
        }
        double targetProjection = 0.0;
        for (size_t row = column; row < rowCount; ++row) {
            targetProjection += householder[row - column] * targets[row];
        }
        targetProjection *= beta;
        for (size_t row = column; row < rowCount; ++row) {
            targets[row] -= targetProjection * householder[row - column];
        }
    }
    double parameters[8] = {};
    for (int row = 7; row >= 0; --row) {
        double value = targets[static_cast<size_t>(row)];
        for (int column = row + 1; column < 8; ++column) {
            value -= equations[static_cast<size_t>(row)]
                              [static_cast<size_t>(column)] * parameters[column];
        }
        const double diagonal =
            equations[static_cast<size_t>(row)][static_cast<size_t>(row)];
        if (std::abs(diagonal) < 1e-10) return false;
        parameters[row] = value / diagonal;
    }
    const double normalizedHomography[3][3] = {
        {parameters[0], parameters[1], parameters[2]},
        {parameters[3], parameters[4], parameters[5]},
        {parameters[6], parameters[7], 1.0}
    };
    const double sourceTransform[3][3] = {
        {sourceNormalization.scale, 0.0,
         -sourceNormalization.scale * sourceNormalization.centerX},
        {0.0, sourceNormalization.scale,
         -sourceNormalization.scale * sourceNormalization.centerY},
        {0.0, 0.0, 1.0}
    };
    const double referenceInverse[3][3] = {
        {1.0 / referenceNormalization.scale, 0.0,
         referenceNormalization.centerX},
        {0.0, 1.0 / referenceNormalization.scale,
         referenceNormalization.centerY},
        {0.0, 0.0, 1.0}
    };
    double intermediate[3][3] = {};
    double homography[3][3] = {};
    multiplyMatrix3(normalizedHomography, sourceTransform, intermediate);
    multiplyMatrix3(referenceInverse, intermediate, homography);
    if (!std::isfinite(homography[2][2]) || std::abs(homography[2][2]) < 1e-12) {
        return false;
    }
    const double scale = homography[2][2];
    output.a = homography[0][0] / scale;
    output.b = homography[0][1] / scale;
    output.c = homography[0][2] / scale;
    output.d = homography[1][0] / scale;
    output.e = homography[1][1] / scale;
    output.f = homography[1][2] / scale;
    output.g = homography[2][0] / scale;
    output.h = homography[2][1] / scale;
    output.model = AlignmentModel::Homography;
    return true;
}

static bool isInlier(const StarPoint& ref, const StarPoint& src,
                     const AlignmentTransform& transform, double threshold) {
    double x = 0.0;
    double y = 0.0;
    if (!mapPoint(transform, src.x, src.y, x, y)) return false;
    const double dx = x - ref.x;
    const double dy = y - ref.y;
    return (dx * dx + dy * dy) < (threshold * threshold);
}

static bool isPlausibleAffine(const AlignmentTransform& t) {
    if (!std::isfinite(t.a) || !std::isfinite(t.b) || !std::isfinite(t.c) ||
        !std::isfinite(t.d) || !std::isfinite(t.e) || !std::isfinite(t.f) ||
        std::abs(t.g) > 1e-12 || std::abs(t.h) > 1e-12) {
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

static bool isPlausibleHomography(const AlignmentTransform& transform,
                                  int width, int height) {
    if (width <= 1 || height <= 1) return false;
    for (double value : {transform.a, transform.b, transform.c, transform.d,
                         transform.e, transform.f, transform.g, transform.h}) {
        if (!std::isfinite(value)) return false;
    }
    const double input[5][2] = {
        {0.0, 0.0}, {static_cast<double>(width - 1), 0.0},
        {static_cast<double>(width - 1), static_cast<double>(height - 1)},
        {0.0, static_cast<double>(height - 1)},
        {0.5 * (width - 1), 0.5 * (height - 1)}
    };
    double mapped[5][2] = {};
    const double diagonal = std::hypot(static_cast<double>(width),
                                       static_cast<double>(height));
    for (int point = 0; point < 5; ++point) {
        if (!mapPoint(transform, input[point][0], input[point][1],
                      mapped[point][0], mapped[point][1])) {
            return false;
        }
        if (std::hypot(mapped[point][0] - input[point][0],
                       mapped[point][1] - input[point][1]) > diagonal * 0.4) {
            return false;
        }
    }
    double orientation = 0.0;
    double cornerCrossProducts[4] = {};
    for (int corner = 0; corner < 4; ++corner) {
        const int next = (corner + 1) % 4;
        const int nextNext = (corner + 2) % 4;
        orientation += mapped[corner][0] * mapped[next][1] -
                       mapped[next][0] * mapped[corner][1];
        const double edgeX = mapped[next][0] - mapped[corner][0];
        const double edgeY = mapped[next][1] - mapped[corner][1];
        const double nextEdgeX = mapped[nextNext][0] - mapped[next][0];
        const double nextEdgeY = mapped[nextNext][1] - mapped[next][1];
        cornerCrossProducts[corner] =
            edgeX * nextEdgeY - edgeY * nextEdgeX;
        const double sourceEdge = std::hypot(
            input[next][0] - input[corner][0],
            input[next][1] - input[corner][1]);
        const double mappedEdge = std::hypot(
            mapped[next][0] - mapped[corner][0],
            mapped[next][1] - mapped[corner][1]);
        const double ratio = mappedEdge / sourceEdge;
        if (ratio < 0.7 || ratio > 1.3) return false;
    }
    if (orientation <= 0.0 ||
        std::any_of(std::begin(cornerCrossProducts),
                    std::end(cornerCrossProducts),
                    [](double cross) { return cross <= 0.0; })) {
        return false;
    }

    double denominatorSign = 0.0;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            const double x = (width - 1) * column / 4.0;
            const double y = (height - 1) * row / 4.0;
            const double denominator = transform.g * x + transform.h * y + 1.0;
            if (!std::isfinite(denominator) || std::abs(denominator) < 0.5) {
                return false;
            }
            const double sign = denominator > 0.0 ? 1.0 : -1.0;
            if (denominatorSign == 0.0) denominatorSign = sign;
            if (sign != denominatorSign) return false;

            double centerX = 0.0;
            double centerY = 0.0;
            double xStepX = 0.0;
            double xStepY = 0.0;
            double yStepX = 0.0;
            double yStepY = 0.0;
            if (!mapPoint(transform, x, y, centerX, centerY) ||
                !mapPoint(transform, x + 1.0, y, xStepX, xStepY) ||
                !mapPoint(transform, x, y + 1.0, yStepX, yStepY)) {
                return false;
            }
            const double axisXX = xStepX - centerX;
            const double axisXY = xStepY - centerY;
            const double axisYX = yStepX - centerX;
            const double axisYY = yStepY - centerY;
            const double scaleX = std::hypot(axisXX, axisXY);
            const double scaleY = std::hypot(axisYX, axisYY);
            const double jacobian = axisXX * axisYY - axisXY * axisYX;
            if (scaleX < 0.8 || scaleX > 1.2 ||
                scaleY < 0.8 || scaleY > 1.2 || jacobian <= 0.0) {
                return false;
            }
            const double normalizedDot =
                (axisXX * axisYX + axisXY * axisYY) / (scaleX * scaleY);
            if (std::abs(normalizedDot) > 0.2) return false;
        }
    }
    return true;
}

static bool homographySampleHasCoverage(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    const std::vector<std::pair<int, int>>& sample,
    int width, int height) {
    if (sample.size() != 4 || width <= 0 || height <= 0) return false;
    const double imageArea = static_cast<double>(width) * height;
    auto covered = [&](const std::vector<StarPoint>& stars, bool reference) {
        double minimumX = std::numeric_limits<double>::max();
        double minimumY = std::numeric_limits<double>::max();
        double maximumX = std::numeric_limits<double>::lowest();
        double maximumY = std::numeric_limits<double>::lowest();
        for (const auto& match : sample) {
            const StarPoint& point = stars[reference ? match.first : match.second];
            minimumX = std::min(minimumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumX = std::max(maximumX, point.x);
            maximumY = std::max(maximumY, point.y);
        }
        if ((maximumX - minimumX) * (maximumY - minimumY) < imageArea * 0.01) {
            return false;
        }
        for (int first = 0; first < 4; ++first) {
            for (int second = first + 1; second < 4; ++second) {
                for (int third = second + 1; third < 4; ++third) {
                    const StarPoint& a = stars[reference
                        ? sample[first].first : sample[first].second];
                    const StarPoint& b = stars[reference
                        ? sample[second].first : sample[second].second];
                    const StarPoint& c = stars[reference
                        ? sample[third].first : sample[third].second];
                    const double areaTwice = std::abs(
                        (b.x - a.x) * (c.y - a.y) -
                        (b.y - a.y) * (c.x - a.x));
                    if (areaTwice < imageArea * 0.0001) return false;
                }
            }
        }
        return true;
    };
    return covered(refStars, true) && covered(srcStars, false);
}

static double symmetricSquaredError(const StarPoint& reference,
                                    const StarPoint& source,
                                    const AlignmentTransform& transform,
                                    const AlignmentTransform& inverse) {
    double mappedReferenceX = 0.0;
    double mappedReferenceY = 0.0;
    double mappedSourceX = 0.0;
    double mappedSourceY = 0.0;
    if (!mapPoint(transform, source.x, source.y,
                  mappedReferenceX, mappedReferenceY) ||
        !mapPoint(inverse, reference.x, reference.y,
                  mappedSourceX, mappedSourceY)) {
        return std::numeric_limits<double>::max();
    }
    const double referenceDx = mappedReferenceX - reference.x;
    const double referenceDy = mappedReferenceY - reference.y;
    const double sourceDx = mappedSourceX - source.x;
    const double sourceDy = mappedSourceY - source.y;
    return 0.5 * (referenceDx * referenceDx + referenceDy * referenceDy +
                  sourceDx * sourceDx + sourceDy * sourceDy);
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
        if (!solveAffine(refStars, srcStars, sample, t) || !isPlausibleAffine(t)) continue;

        // 验证内点率
        int inliers = 0;
        double squaredError = 0.0;
        for (const auto& m : matches) {
            if (isInlier(refStars[m.first], srcStars[m.second], t, threshold)) {
                inliers++;
                const auto& ref = refStars[m.first];
                const auto& src = srcStars[m.second];
                double mappedX = 0.0;
                double mappedY = 0.0;
                mapPoint(t, src.x, src.y, mappedX, mappedY);
                const double dx = mappedX - ref.x;
                const double dy = mappedY - ref.y;
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
    if (!isPlausibleAffine(out)) return false;

    int finalInliers = 0;
    double finalSquaredError = 0.0;
    for (const auto& match : matches) {
        const auto& ref = refStars[match.first];
        const auto& src = srcStars[match.second];
        double mappedX = 0.0;
        double mappedY = 0.0;
        if (!mapPoint(out, src.x, src.y, mappedX, mappedY)) continue;
        const double dx = mappedX - ref.x;
        const double dy = mappedY - ref.y;
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

bool ImageAligner::ransacHomography(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    const std::vector<std::pair<int, int>>& matches,
    int imageWidth, int imageHeight,
    AlignmentTransform& out) {
    if (matches.size() < 6 || imageWidth <= 1 || imageHeight <= 1) return false;

    std::mt19937 rng(0x484f4d4fU);
    std::uniform_int_distribution<int> distribution(
        0, static_cast<int>(matches.size()) - 1);
    constexpr double threshold = 2.5;
    int bestInliers = 0;
    double bestSquaredError = std::numeric_limits<double>::max();
    AlignmentTransform bestTransform;

    for (int iteration = 0; iteration < 1000; ++iteration) {
        std::vector<std::pair<int, int>> sample;
        std::vector<int> used;
        sample.reserve(4);
        used.reserve(4);
        while (sample.size() < 4) {
            const int index = distribution(rng);
            if (std::find(used.begin(), used.end(), index) != used.end()) continue;
            used.push_back(index);
            sample.push_back(matches[static_cast<size_t>(index)]);
        }
        if (!homographySampleHasCoverage(
                refStars, srcStars, sample, imageWidth, imageHeight)) {
            continue;
        }

        AlignmentTransform candidate;
        if (!solveHomographyLeastSquares(refStars, srcStars, sample, candidate) ||
            !isPlausibleHomography(candidate, imageWidth, imageHeight)) {
            continue;
        }
        AlignmentTransform candidateInverse;
        if (!invertTransform(candidate, candidateInverse)) continue;

        int inliers = 0;
        double squaredError = 0.0;
        for (const auto& match : matches) {
            const StarPoint& reference = refStars[match.first];
            const StarPoint& source = srcStars[match.second];
            const double error = symmetricSquaredError(
                reference, source, candidate, candidateInverse);
            if (error < threshold * threshold) {
                ++inliers;
                squaredError += error;
            }
        }
        if (inliers > bestInliers ||
            (inliers == bestInliers && squaredError < bestSquaredError)) {
            bestInliers = inliers;
            bestSquaredError = squaredError;
            bestTransform = candidate;
        }
    }

    const int minimumInliers = std::max(
        6, std::min(10, static_cast<int>(std::ceil(matches.size() * 0.35))));
    if (bestInliers < minimumInliers) return false;
    AlignmentTransform bestInverse;
    if (!invertTransform(bestTransform, bestInverse)) return false;
    std::vector<std::pair<int, int>> inliers;
    inliers.reserve(matches.size());
    for (const auto& match : matches) {
        const double error = symmetricSquaredError(
            refStars[match.first], srcStars[match.second],
            bestTransform, bestInverse);
        if (error < threshold * threshold) {
            inliers.push_back(match);
        }
    }
    if (!solveHomographyLeastSquares(refStars, srcStars, inliers, out)) {
        out = bestTransform;
    }
    if (!isPlausibleHomography(out, imageWidth, imageHeight)) return false;
    AlignmentTransform outputInverse;
    if (!invertTransform(out, outputInverse)) return false;

    int finalInliers = 0;
    double finalSquaredError = 0.0;
    for (const auto& match : matches) {
        const StarPoint& reference = refStars[match.first];
        const StarPoint& source = srcStars[match.second];
        const double error = symmetricSquaredError(
            reference, source, out, outputInverse);
        if (error < threshold * threshold) {
            ++finalInliers;
            finalSquaredError += error;
        }
    }
    const double rms = finalInliers > 0
        ? std::sqrt(finalSquaredError / finalInliers)
        : std::numeric_limits<double>::max();
    return finalInliers >= minimumInliers && rms <= threshold;
}

AlignmentMetrics ImageAligner::evaluateTransform(
    const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    const AlignmentTransform& transform,
    double matchRadius,
    int imageWidth,
    int imageHeight) const {
    AlignmentMetrics metrics;
    metrics.evaluationReferenceStars = static_cast<int>(refStars.size());
    metrics.gridCells.resize(
        static_cast<size_t>(metrics.gridColumns * metrics.gridRows));
    if (matchRadius <= 0.0) return metrics;
    if (imageWidth <= 0 || imageHeight <= 0) {
        for (const StarPoint& star : refStars) {
            imageWidth = std::max(imageWidth, static_cast<int>(std::ceil(star.x)) + 1);
            imageHeight = std::max(imageHeight, static_cast<int>(std::ceil(star.y)) + 1);
        }
    }
    if (imageWidth <= 0 || imageHeight <= 0) return metrics;

    auto cellIndex = [&](const StarPoint& reference) {
        const int column = std::clamp(
            static_cast<int>(reference.x * metrics.gridColumns / imageWidth),
            0, metrics.gridColumns - 1);
        const int row = std::clamp(
            static_cast<int>(reference.y * metrics.gridRows / imageHeight),
            0, metrics.gridRows - 1);
        return static_cast<size_t>(row * metrics.gridColumns + column);
    };
    for (const StarPoint& reference : refStars) {
        ++metrics.gridCells[cellIndex(reference)].referenceStars;
    }

    struct CandidateEdge {
        size_t source = 0;
        size_t reference = 0;
        double squaredError = 0.0;
    };
    std::vector<CandidateEdge> edges;
    const double radiusSquared = matchRadius * matchRadius;
    for (size_t sourceIndex = 0; sourceIndex < srcStars.size(); ++sourceIndex) {
        double x = 0.0;
        double y = 0.0;
        if (!mapPoint(transform, srcStars[sourceIndex].x,
                      srcStars[sourceIndex].y, x, y)) {
            continue;
        }
        for (size_t referenceIndex = 0;
             referenceIndex < refStars.size(); ++referenceIndex) {
            const double dx = refStars[referenceIndex].x - x;
            const double dy = refStars[referenceIndex].y - y;
            const double error = dx * dx + dy * dy;
            if (error <= radiusSquared) {
                edges.push_back({sourceIndex, referenceIndex, error});
            }
        }
    }
    std::sort(edges.begin(), edges.end(),
              [](const CandidateEdge& left, const CandidateEdge& right) {
                  return left.squaredError < right.squaredError;
              });

    std::vector<bool> sourceUsed(srcStars.size(), false);
    std::vector<bool> referenceUsed(refStars.size(), false);
    double squaredError = 0.0;
    std::vector<double> allErrors;
    std::vector<std::vector<double>> cellErrors(metrics.gridCells.size());
    for (const CandidateEdge& edge : edges) {
        if (sourceUsed[edge.source] || referenceUsed[edge.reference]) continue;
        sourceUsed[edge.source] = true;
        referenceUsed[edge.reference] = true;
        ++metrics.matchedStars;
        squaredError += edge.squaredError;
        const double error = std::sqrt(edge.squaredError);
        allErrors.push_back(error);
        cellErrors[cellIndex(refStars[edge.reference])].push_back(error);
    }
    metrics.matchCoverage = refStars.empty()
        ? 0.0 : static_cast<double>(metrics.matchedStars) / refStars.size();
    metrics.rmsError = metrics.matchedStars > 0
        ? std::sqrt(squaredError / metrics.matchedStars)
        : std::numeric_limits<double>::max();
    auto percentile = [](std::vector<double> errors, double fraction) {
        if (errors.empty()) return 0.0;
        std::sort(errors.begin(), errors.end());
        const size_t index = std::min(
            errors.size() - 1,
            static_cast<size_t>(std::ceil(errors.size() * fraction)) - 1);
        return errors[index];
    };
    metrics.medianError = percentile(allErrors, 0.5);
    metrics.p95Error = percentile(allErrors, 0.95);
    std::vector<double> outerErrors;
    for (size_t cell = 0; cell < cellErrors.size(); ++cell) {
        AlignmentGridCell& outputCell = metrics.gridCells[cell];
        outputCell.matchedStars = static_cast<int>(cellErrors[cell].size());
        outputCell.matchCoverage = outputCell.referenceStars > 0
            ? static_cast<double>(outputCell.matchedStars) /
                  outputCell.referenceStars
            : 0.0;
        outputCell.eligible = outputCell.referenceStars >= 3;
        outputCell.covered = outputCell.eligible &&
            outputCell.matchedStars >= 3 && outputCell.matchCoverage >= 0.5;
        if (outputCell.eligible) ++metrics.eligibleCells;
        if (outputCell.covered) ++metrics.coveredCells;
        if (!cellErrors[cell].empty()) {
            double cellSquaredError = 0.0;
            for (double error : cellErrors[cell]) cellSquaredError += error * error;
            outputCell.rmsError =
                std::sqrt(cellSquaredError / cellErrors[cell].size());
            outputCell.medianError = percentile(cellErrors[cell], 0.5);
            outputCell.p95Error = percentile(cellErrors[cell], 0.95);
            outputCell.maxError =
                *std::max_element(cellErrors[cell].begin(), cellErrors[cell].end());
        }
        const int row = static_cast<int>(cell) / metrics.gridColumns;
        const int column = static_cast<int>(cell) % metrics.gridColumns;
        if (!(row == 1 && column == 1)) {
            if (outputCell.eligible) ++metrics.outerEligibleCells;
            if (outputCell.covered) ++metrics.outerCoveredCells;
            outerErrors.insert(outerErrors.end(),
                               cellErrors[cell].begin(), cellErrors[cell].end());
        }
    }
    metrics.gridCoverage = metrics.eligibleCells > 0
        ? static_cast<double>(metrics.coveredCells) / metrics.eligibleCells : 0.0;
    metrics.outerGridP95Error = percentile(outerErrors, 0.95);

    const int requiredMatches = std::min(
        12, static_cast<int>(std::min(refStars.size(), srcStars.size())));
    auto fail = [&](bool condition, const char* reason) {
        if (condition) metrics.failureReasons.emplace_back(reason);
    };
    fail(metrics.matchedStars < requiredMatches, "insufficient-matches");
    fail(metrics.matchCoverage < 0.35, "low-match-coverage");
    fail(metrics.rmsError > 2.0, "rms-above-2px");
    fail(metrics.p95Error > 3.5, "p95-above-3.5px");
    if (refStars.size() >= 18) {
        fail(metrics.eligibleCells < 4, "insufficient-field-coverage");
        // Landscapes can produce star-like detections in the fixed foreground.
        // Six well-covered cells still constrain the complete sky region even
        // when those foreground cells correctly do not follow the sky model.
        fail(metrics.gridCoverage < 0.75 && metrics.coveredCells < 6,
             "low-grid-coverage");
        fail(metrics.outerEligibleCells < 3 || metrics.outerCoveredCells < 3,
             "insufficient-outer-grid-coverage");
    }
    fail(metrics.outerGridP95Error > 4.0, "outer-p95-above-4px");
    for (const AlignmentGridCell& cell : metrics.gridCells) {
        if (!cell.covered) continue;
        fail(cell.rmsError > 3.0, "cell-rms-above-3px");
        fail(cell.p95Error > 4.5, "cell-p95-above-4.5px");
    }
    metrics.qualityPassed = metrics.failureReasons.empty();
    return metrics;
}

bool ImageAligner::align(const std::vector<StarPoint>& refStars,
    const std::vector<StarPoint>& srcStars,
    AlignmentTransform& out,
    AlignmentQuality* quality,
    const AlignmentOptions& options) {
    std::vector<std::pair<int, int>> matches;
    const std::vector<StarPoint>& evaluationReference =
        options.evaluationReferenceStars
            ? *options.evaluationReferenceStars : refStars;
    const std::vector<StarPoint>& evaluationSource =
        options.evaluationSourceStars
            ? *options.evaluationSourceStars : srcStars;
    auto fitBestModel = [&](AlignmentTransform& selected,
                            AlignmentQuality& selectedQuality) {
        AlignmentTransform affine;
        AlignmentMetrics affineMetrics;
        const bool affineFitted = ransacAffine(refStars, srcStars, matches, affine);
        if (affineFitted) {
            affineMetrics = evaluateTransform(
                evaluationReference, evaluationSource, affine, 6.0,
                options.imageWidth, options.imageHeight);
        }

        AlignmentTransform homography;
        AlignmentMetrics homographyMetrics;
        const bool homographyFitted = options.allowHomography &&
            ransacHomography(refStars, srcStars, matches,
                             options.imageWidth, options.imageHeight,
                             homography);
        if (homographyFitted) {
            homographyMetrics = evaluateTransform(
                evaluationReference, evaluationSource, homography, 6.0,
                options.imageWidth, options.imageHeight);
        }
        selectedQuality.affineEvaluated = affineFitted;
        selectedQuality.homographyEvaluated = homographyFitted;
        selectedQuality.affineCandidate = affineMetrics;
        selectedQuality.homographyCandidate = homographyMetrics;
        const bool affineAccepted = affineFitted && affineMetrics.qualityPassed;
        const bool homographyAccepted =
            homographyFitted && homographyMetrics.qualityPassed;
        if (!affineAccepted && !homographyAccepted) return false;

        bool selectHomography = false;
        if (homographyAccepted && !affineAccepted) {
            selectHomography = true;
            selectedQuality.selectionReason = "affine-failed-quality-gate";
        } else if (homographyAccepted && affineAccepted) {
            const bool preservesMatches =
                homographyMetrics.matchedStars >=
                    static_cast<int>(std::ceil(affineMetrics.matchedStars * 0.95));
            const bool preservesGrid =
                homographyMetrics.gridCoverage + 1e-9 >= affineMetrics.gridCoverage;
            const bool preservesRms =
                homographyMetrics.rmsError <= affineMetrics.rmsError * 1.05;
            const bool globalTailImproved =
                affineMetrics.p95Error - homographyMetrics.p95Error >= 0.3 &&
                homographyMetrics.p95Error <= affineMetrics.p95Error * 0.85;
            const bool outerTailImproved =
                affineMetrics.outerGridP95Error -
                    homographyMetrics.outerGridP95Error >= 0.5 &&
                homographyMetrics.outerGridP95Error <=
                    affineMetrics.outerGridP95Error * 0.8;
            selectHomography = preservesMatches && preservesGrid && preservesRms &&
                (globalTailImproved || outerTailImproved);
            selectedQuality.selectionReason = selectHomography
                ? (outerTailImproved ? "outer-p95-improved"
                                     : "global-p95-improved")
                : "affine-preferred-no-significant-gain";
        } else {
            selectedQuality.selectionReason = "affine-only-quality-pass";
        }

        if (selectHomography) {
            selected = homography;
            static_cast<AlignmentMetrics&>(selectedQuality) = homographyMetrics;
            selectedQuality.selected = true;
            selectedQuality.model = AlignmentModel::Homography;
        } else {
            selected = affine;
            static_cast<AlignmentMetrics&>(selectedQuality) = affineMetrics;
            selectedQuality.selected = true;
            selectedQuality.model = AlignmentModel::Affine;
        }
        return true;
    };

    AlignmentQuality verified;
    bool aligned = triangleMatch(refStars, srcStars, matches) &&
                   fitBestModel(out, verified);
    if (!aligned) {
        aligned = translationSeedMatch(refStars, srcStars, matches) &&
                  fitBestModel(out, verified);
    }
    if (quality) *quality = verified;
    return aligned;
}

static bool invertTransform(const AlignmentTransform& transform,
                            AlignmentTransform& inverse) {
    const double matrix[3][3] = {
        {transform.a, transform.b, transform.c},
        {transform.d, transform.e, transform.f},
        {transform.g, transform.h, 1.0}
    };
    const double determinant =
        matrix[0][0] * (matrix[1][1] * matrix[2][2] -
                        matrix[1][2] * matrix[2][1]) -
        matrix[0][1] * (matrix[1][0] * matrix[2][2] -
                        matrix[1][2] * matrix[2][0]) +
        matrix[0][2] * (matrix[1][0] * matrix[2][1] -
                        matrix[1][1] * matrix[2][0]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12) return false;

    double output[3][3] = {
        {
            matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1],
            matrix[0][2] * matrix[2][1] - matrix[0][1] * matrix[2][2],
            matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]
        },
        {
            matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2],
            matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0],
            matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]
        },
        {
            matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0],
            matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1],
            matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]
        }
    };
    const double normalization = output[2][2];
    if (!std::isfinite(normalization) || std::abs(normalization) < 1e-12) {
        return false;
    }
    for (auto& row : output) {
        for (double& value : row) value /= normalization;
    }
    inverse.a = output[0][0];
    inverse.b = output[0][1];
    inverse.c = output[0][2];
    inverse.d = output[1][0];
    inverse.e = output[1][1];
    inverse.f = output[1][2];
    inverse.g = output[2][0];
    inverse.h = output[2][1];
    inverse.model = transform.model;
    return true;
}

bool ImageAligner::applyTransform(const std::vector<uint16_t>& src, int w, int h,
                                    const AlignmentTransform& t,
                                    std::vector<uint16_t>& dst) {
    if (w <= 1 || h <= 1 ||
        static_cast<size_t>(w) > std::numeric_limits<size_t>::max() / static_cast<size_t>(h) ||
        src.size() != static_cast<size_t>(w) * static_cast<size_t>(h)) {
        return false;
    }

    AlignmentTransform inverse;
    if (!invertTransform(t, inverse)) return false;

    dst.resize(static_cast<size_t>(w) * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // The transform maps source to destination, so resampling uses its inverse.
            double sx = 0.0;
            double sy = 0.0;
            if (!mapPoint(inverse, x, y, sx, sy)) {
                dst[static_cast<size_t>(y) * w + x] = 0;
                continue;
            }

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

    AlignmentTransform inverse;
    if (!invertTransform(t, inverse)) return false;

    dst.resize(pixelCount * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sx = 0.0;
            double sy = 0.0;
            const bool mapped = mapPoint(inverse, x, y, sx, sy);
            const size_t destination = (static_cast<size_t>(y) * w + x) * 3;
            if (!mapped || sx < 0.0 || sx >= static_cast<double>(w - 1) ||
                sy < 0.0 || sy >= static_cast<double>(h - 1)) {
                dst[destination] = 0;
                dst[destination + 1] = 0;
                dst[destination + 2] = 0;
                continue;
            }
            const int ix = static_cast<int>(std::floor(sx));
            const int iy = static_cast<int>(std::floor(sy));

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

bool ImageAligner::commonValidBounds(
    const std::vector<AlignmentTransform>& transforms,
    int width, int height, AlignmentBounds& bounds) const {
    if (transforms.empty() || width <= 2 || height <= 2) return false;

    std::vector<AlignmentTransform> inverses;
    inverses.reserve(transforms.size());
    for (const AlignmentTransform& transform : transforms) {
        AlignmentTransform inverse;
        if (!invertTransform(transform, inverse)) return false;
        inverses.push_back(inverse);
    }

    auto validPoint = [&](double x, double y) {
        for (const AlignmentTransform& inverse : inverses) {
            double sourceX = 0.0;
            double sourceY = 0.0;
            if (!mapPoint(inverse, x, y, sourceX, sourceY) ||
                sourceX < 0.0 || sourceX >= static_cast<double>(width - 1) ||
                sourceY < 0.0 || sourceY >= static_cast<double>(height - 1)) {
                return false;
            }
        }
        return true;
    };

    constexpr int kGridLimit = 256;
    const int columns = std::min(kGridLimit, width);
    const int rows = std::min(kGridLimit, height);
    std::vector<int> heights(static_cast<size_t>(columns), 0);
    int bestArea = 0;
    int bestLeft = 0;
    int bestRight = -1;
    int bestTop = 0;
    int bestBottom = -1;

    for (int row = 0; row < rows; ++row) {
        const double y = static_cast<double>(row) * (height - 1) / (rows - 1);
        for (int column = 0; column < columns; ++column) {
            const double x =
                static_cast<double>(column) * (width - 1) / (columns - 1);
            heights[static_cast<size_t>(column)] =
                validPoint(x, y)
                    ? heights[static_cast<size_t>(column)] + 1 : 0;
        }

        std::vector<int> stack;
        stack.reserve(static_cast<size_t>(columns) + 1);
        for (int column = 0; column <= columns; ++column) {
            const int currentHeight =
                column < columns ? heights[static_cast<size_t>(column)] : 0;
            while (!stack.empty() &&
                   heights[static_cast<size_t>(stack.back())] > currentHeight) {
                const int bar = stack.back();
                stack.pop_back();
                const int rectangleHeight =
                    heights[static_cast<size_t>(bar)];
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const int right = column - 1;
                const int area = rectangleHeight * (right - left + 1);
                if (area > bestArea) {
                    bestArea = area;
                    bestLeft = left;
                    bestRight = right;
                    bestBottom = row;
                    bestTop = row - rectangleHeight + 1;
                }
            }
            if (column < columns) stack.push_back(column);
        }
    }
    if (bestArea == 0) return false;

    int left = static_cast<int>(std::ceil(
        static_cast<double>(bestLeft) * (width - 1) / (columns - 1)));
    int right = static_cast<int>(std::floor(
        static_cast<double>(bestRight) * (width - 1) / (columns - 1)));
    int top = static_cast<int>(std::ceil(
        static_cast<double>(bestTop) * (height - 1) / (rows - 1)));
    int bottom = static_cast<int>(std::floor(
        static_cast<double>(bestBottom) * (height - 1) / (rows - 1)));

    auto validCorners = [&](int candidateLeft, int candidateTop,
                            int candidateRight, int candidateBottom) {
        return validPoint(candidateLeft, candidateTop) &&
               validPoint(candidateRight, candidateTop) &&
               validPoint(candidateLeft, candidateBottom) &&
               validPoint(candidateRight, candidateBottom);
    };
    bool expanded = true;
    while (expanded) {
        expanded = false;
        if (left > 0 &&
            validCorners(left - 1, top, right, bottom)) {
            --left;
            expanded = true;
        }
        if (right < width - 1 &&
            validCorners(left, top, right + 1, bottom)) {
            ++right;
            expanded = true;
        }
        if (top > 0 &&
            validCorners(left, top - 1, right, bottom)) {
            --top;
            expanded = true;
        }
        if (bottom < height - 1 &&
            validCorners(left, top, right, bottom + 1)) {
            ++bottom;
            expanded = true;
        }
    }

    // Keep one pixel inside the bilinear validity boundary.
    ++left;
    ++top;
    --right;
    --bottom;
    if (right < left || bottom < top ||
        right - left + 1 < width / 2 ||
        bottom - top + 1 < height / 2) {
        return false;
    }
    bounds = {left, top, right - left + 1, bottom - top + 1};
    return true;
}
