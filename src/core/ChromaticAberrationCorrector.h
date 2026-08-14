#pragma once

#include <cstdint>
#include <vector>

struct ChromaticAberrationChannelStats {
    bool reliable = false;
    int measuredStars = 0;
    int inlierStars = 0;
    int coveredQuadrants = 0;
    int coveredAngularSectors = 0;
    int outerInlierStars = 0;
    double inlierFraction = 0.0;
    double nuisanceOffsetX = 0.0;
    double nuisanceOffsetY = 0.0;
    double sourceScale = 1.0;
    double edgeShiftPixels = 0.0;
    double uncorrectedRadialRms = 0.0;
    double radialResidualRms = 0.0;
    double tangentialResidualRms = 0.0;
};

struct ChromaticAberrationModel {
    // Output pixels sample each source channel at:
    // center + sourceScale * (output - center). A value above one pulls an
    // outward-displaced channel back toward the green reference geometry.
    double centerX = 0.0;
    double centerY = 0.0;
    double redSourceScale = 1.0;
    double blueSourceScale = 1.0;
    bool correctRed = false;
    bool correctBlue = false;

    bool active() const { return correctRed || correctBlue; }
};

struct ChromaticAberrationStats {
    int detectedStars = 0;
    int eligibleStars = 0;
    ChromaticAberrationChannelStats red;
    ChromaticAberrationChannelStats blue;
    bool applied = false;
};

/**
 * Estimates and corrects lateral lens chromatic aberration in linear RGB16.
 *
 * Green is the geometric reference. Unsaturated stars across the outer field
 * provide red/blue centroid offsets, which are fitted to a radial scale model.
 * The estimator deliberately refuses weak, central, one-sided, or tangentially
 * inconsistent evidence; axial color halos remain the defringe stage's job.
 */
class ChromaticAberrationCorrector {
public:
    static bool estimate(const std::vector<uint16_t>& rgb, int width, int height,
                         ChromaticAberrationModel& model,
                         ChromaticAberrationStats* stats = nullptr);

    static bool apply(const std::vector<uint16_t>& rgb, int width, int height,
                      const ChromaticAberrationModel& model,
                      std::vector<uint16_t>& corrected);

    static bool correctInPlace(std::vector<uint16_t>& rgb, int width, int height,
                               const ChromaticAberrationModel& model);
};
