#include "FrameSequenceOrder.h"

#include <QCollator>
#include <QFileInfo>

#include <algorithm>
#include <numeric>

namespace FrameSequenceOrder {

std::vector<size_t> chronologicalIndices(
    const QStringList& filePaths,
    const std::vector<std::string>& captureTimes) {
    if (static_cast<size_t>(filePaths.size()) != captureTimes.size()) {
        return {};
    }

    std::vector<size_t> order(captureTimes.size());
    std::iota(order.begin(), order.end(), size_t{0});

    const bool allHaveCaptureTimes = std::all_of(
        captureTimes.begin(), captureTimes.end(),
        [](const std::string& value) { return !value.empty(); });

    QCollator fileNameCollator;
    fileNameCollator.setCaseSensitivity(Qt::CaseInsensitive);
    fileNameCollator.setNumericMode(true);

    std::stable_sort(
        order.begin(), order.end(),
        [&filePaths, &captureTimes, allHaveCaptureTimes,
         &fileNameCollator](size_t left, size_t right) {
            if (allHaveCaptureTimes &&
                captureTimes[left] != captureTimes[right]) {
                return captureTimes[left] < captureTimes[right];
            }

            const QString leftName = QFileInfo(
                filePaths.at(static_cast<qsizetype>(left))).fileName();
            const QString rightName = QFileInfo(
                filePaths.at(static_cast<qsizetype>(right))).fileName();
            return fileNameCollator.compare(leftName, rightName) < 0;
        });
    return order;
}

} // namespace FrameSequenceOrder
