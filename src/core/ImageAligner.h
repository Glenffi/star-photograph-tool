#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "StarDetector.h"

/**
 * @brief 图像对齐变换矩阵
 *
 * Source-to-reference projective transform:
 *   x' = (a*x + b*y + c) / (g*x + h*y + 1)
 *   y' = (d*x + e*y + f) / (g*x + h*y + 1)
 *
 * Affine transforms are represented by g = h = 0.
 */
enum class AlignmentModel {
    Affine,
    Homography
};

struct AlignmentTransform {
    double a = 1.0, b = 0.0, c = 0.0;
    double d = 0.0, e = 1.0, f = 0.0;
    double g = 0.0, h = 0.0;
    AlignmentModel model = AlignmentModel::Affine;
};

struct AlignmentGridCell {
    int referenceStars = 0;
    int matchedStars = 0;
    double matchCoverage = 0.0;
    double rmsError = 0.0;
    double medianError = 0.0;
    double p95Error = 0.0;
    // Local cells often contain fewer than 20 stars, where a conventional P95
    // is simply the maximum. Ignore one worst match for the acceptance metric
    // so one false detection cannot veto an otherwise precise full-frame fit.
    double trimmedP95Error = 0.0;
    double maxError = 0.0;
    bool eligible = false;
    bool covered = false;
};

struct AlignmentMetrics {
    int evaluationReferenceStars = 0;
    int matchedStars = 0;
    double matchCoverage = 0.0;
    double rmsError = 0.0;
    double medianError = 0.0;
    double p95Error = 0.0;
    double outerGridP95Error = 0.0;
    int gridColumns = 3;
    int gridRows = 3;
    int eligibleCells = 0;
    int coveredCells = 0;
    int outerEligibleCells = 0;
    int outerCoveredCells = 0;
    double gridCoverage = 0.0;
    bool qualityPassed = false;
    std::vector<std::string> failureReasons;
    std::vector<AlignmentGridCell> gridCells;
};

struct AlignmentQuality : AlignmentMetrics {
    bool selected = false;
    AlignmentModel model = AlignmentModel::Affine;
    std::string selectionReason;
    bool affineEvaluated = false;
    bool homographyEvaluated = false;
    AlignmentMetrics affineCandidate;
    AlignmentMetrics homographyCandidate;
};

struct AlignmentOptions {
    int imageWidth = 0;
    int imageHeight = 0;
    bool allowHomography = true;
    const std::vector<StarPoint>* evaluationReferenceStars = nullptr;
    const std::vector<StarPoint>* evaluationSourceStars = nullptr;
};

struct AlignmentBounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/**
 * @brief 图像对齐器
 *
 * 基于星点三角形匹配 + RANSAC 的图像对齐。
 */
class ImageAligner {
public:
    /**
     * @brief 计算两幅图像的星点匹配和对齐变换
     *
     * @param refStars  参考帧的星点列表
     * @param srcStars  源帧的星点列表
     * @param out       输出变换矩阵
     * @param quality   可选的独立星点与全画幅网格质量指标
     * @param options   图像尺寸、候选模型及独立评估星点；启用
     *                  Homography 时必须提供有效图像尺寸
     * @return true 对齐成功
     */
    bool align(const std::vector<StarPoint>& refStars,
               const std::vector<StarPoint>& srcStars,
               AlignmentTransform& out,
               AlignmentQuality* quality = nullptr,
               const AlignmentOptions& options = {});

    /**
     * @brief 应用 Affine 或 Homography 变换到图像
     *
     * 使用双线性插值重采样。
     *
     * @param src   源图像 (16-bit 单通道)
     * @param w     宽度
     * @param h     高度
     * @param t     变换矩阵
     * @param dst   输出图像 (与源相同尺寸)
     * @return true 成功
     */
    bool applyTransform(const std::vector<uint16_t>& src, int w, int h,
                        const AlignmentTransform& t,
                        std::vector<uint16_t>& dst);

    /**
     * @brief Apply one transform directly to an interleaved RGB image.
     *
     * This avoids three full-size input copies and three output allocations.
     * All channels use the same inverse-mapped sample position.
     */
    bool applyTransformRgb(const std::vector<uint16_t>& src, int w, int h,
                           const AlignmentTransform& t,
                           std::vector<uint16_t>& dst);

    /**
     * @brief Apply the same inverse-mapped transform to an 8-bit mask.
     *
     * This keeps per-frame sky validity in the same coordinates as an aligned
     * RGB frame, which is required to keep shifted terrain out of a sky stack.
     */
    bool applyTransformMask(const std::vector<uint8_t>& src, int w, int h,
                            const AlignmentTransform& t,
                            std::vector<uint8_t>& dst);

    /**
     * Finds a conservative axis-aligned rectangle covered by every accepted
     * source-to-reference transform. The result excludes resampling borders.
     */
    bool commonValidBounds(const std::vector<AlignmentTransform>& transforms,
                           int width, int height,
                           AlignmentBounds& bounds) const;

private:
    bool triangleMatch(const std::vector<StarPoint>& refStars,
                       const std::vector<StarPoint>& srcStars,
                       std::vector<std::pair<int, int>>& matches);
    bool translationSeedMatch(const std::vector<StarPoint>& refStars,
                              const std::vector<StarPoint>& srcStars,
                              std::vector<std::pair<int, int>>& matches);
    bool ransacAffine(const std::vector<StarPoint>& refStars,
                      const std::vector<StarPoint>& srcStars,
                      const std::vector<std::pair<int, int>>& matches,
                      AlignmentTransform& out);
    bool ransacHomography(const std::vector<StarPoint>& refStars,
                          const std::vector<StarPoint>& srcStars,
                          const std::vector<std::pair<int, int>>& matches,
                          int imageWidth, int imageHeight,
                          AlignmentTransform& out);
    AlignmentMetrics evaluateTransform(const std::vector<StarPoint>& refStars,
                                       const std::vector<StarPoint>& srcStars,
                                       const AlignmentTransform& transform,
                                       double matchRadius,
                                       int imageWidth,
                                       int imageHeight) const;
};
