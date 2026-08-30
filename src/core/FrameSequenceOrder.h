#pragma once

#include <QStringList>

#include <cstddef>
#include <string>
#include <vector>

namespace FrameSequenceOrder {

/**
 * Return source indices in chronological order.
 *
 * RAW capture time is authoritative only when every frame has one. If a
 * timestamp is missing, or two frames share the same timestamp, camera file
 * names provide a case-insensitive natural-order fallback (IMG_2 before
 * IMG_10). This keeps sequence order independent from file-dialog selection
 * order on macOS and Windows.
 */
std::vector<size_t> chronologicalIndices(
    const QStringList& filePaths,
    const std::vector<std::string>& captureTimes);

} // namespace FrameSequenceOrder
