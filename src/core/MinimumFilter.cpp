#include "MinimumFilter.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <utility>

namespace {

void slidingMinimum(const float* source, int length, int radius,
                    float* destination) {
    std::deque<int> candidates;
    int next = 0;
    for (int position = 0; position < length; ++position) {
        const int distanceToRight = length - 1 - position;
        const int right = radius >= distanceToRight
            ? length - 1 : position + radius;
        while (next <= right) {
            while (!candidates.empty() &&
                   source[candidates.back()] >= source[next]) {
                candidates.pop_back();
            }
            candidates.push_back(next++);
        }
        const int left = radius >= position ? 0 : position - radius;
        while (!candidates.empty() && candidates.front() < left) {
            candidates.pop_front();
        }
        destination[position] = source[candidates.front()];
    }
}

} // namespace

bool MinimumFilter::applySquare(const std::vector<float>& source,
                                int width, int height, int radius,
                                std::vector<float>& destination) {
    if (width <= 0 || height <= 0 || radius < 0 ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() /
                static_cast<size_t>(height)) {
        return false;
    }
    const size_t pixelCount =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    if (source.size() != pixelCount) return false;

    std::vector<float> horizontal(pixelCount);
    std::vector<float> output(pixelCount);
    for (int y = 0; y < height; ++y) {
        const size_t offset = static_cast<size_t>(y) * width;
        slidingMinimum(source.data() + offset, width, radius,
                       horizontal.data() + offset);
    }

    std::vector<float> column(static_cast<size_t>(height));
    std::vector<float> minimumColumn(static_cast<size_t>(height));
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            column[static_cast<size_t>(y)] =
                horizontal[static_cast<size_t>(y) * width + x];
        }
        slidingMinimum(column.data(), height, radius, minimumColumn.data());
        for (int y = 0; y < height; ++y) {
            output[static_cast<size_t>(y) * width + x] =
                minimumColumn[static_cast<size_t>(y)];
        }
    }
    destination = std::move(output);
    return true;
}
