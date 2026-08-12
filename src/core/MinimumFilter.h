#pragma once

#include <vector>

/**
 * @brief Exact square minimum filter for single-channel float images.
 *
 * The implementation is separable: one monotonic sliding window per row and
 * column. It has the same clipped-edge result as scanning every pixel in the
 * square neighborhood, while reducing the work from O(radius^2 * pixels) to
 * O(pixels).
 */
class MinimumFilter {
public:
    static bool applySquare(const std::vector<float>& source,
                            int width, int height, int radius,
                            std::vector<float>& destination);
};
