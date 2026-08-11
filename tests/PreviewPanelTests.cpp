#include "ui/PreviewPanel.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QPixmap>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void processLayout() {
    QApplication::processEvents();
    QApplication::processEvents();
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    PreviewPanel panel;
    panel.resize(1000, 640);
    panel.show();
    processLayout();

    constexpr int width = 400;
    constexpr int height = 200;
    QImage before(width, height, QImage::Format_RGB888);
    QImage after(width, height, QImage::Format_RGB888);
    before.fill(QColor(210, 35, 25));
    after.fill(QColor(20, 185, 90));

    panel.setComparisonImages(before, after);
    panel.setBeforeAfterMode(true);
    panel.fitToView();
    processLayout();

    auto* imageLabel = panel.findChild<QLabel*>("previewImageLabel");
    check(imageLabel != nullptr, "Preview image label should be discoverable");
    const QPixmap splitPixmap = imageLabel->pixmap();
    check(!splitPixmap.isNull(), "Split preview should render a pixmap");
    check(splitPixmap.width() > splitPixmap.height() * 3,
          "Split preview should place two complete wide images side by side");

    const QImage splitImage = splitPixmap.toImage();
    const QColor left = splitImage.pixelColor(
        splitImage.width() / 8, splitImage.height() / 2);
    const QColor right = splitImage.pixelColor(
        splitImage.width() * 7 / 8, splitImage.height() / 2);
    check(left.red() > 180 && left.green() < 70,
          "Left pane should retain the complete before image");
    check(right.green() > 150 && right.red() < 70,
          "Right pane should retain the complete after image");

    std::vector<uint16_t> firstResult(
        static_cast<size_t>(width) * height * 3, 18000);
    std::vector<uint16_t> secondResult = firstResult;
    for (size_t index = 1; index < secondResult.size(); index += 3) {
        secondResult[index] = 26000;
    }

    panel.loadRgb16BitComparison(
        before, firstResult, width, height, 0, 65535);
    panel.resetZoom();
    panel.setBeforeAfterMode(true);
    processLayout();
    check(std::abs(panel.zoom() - 1.0) < 0.0001,
          "100% zoom should be active before a quick-preview refresh");

    panel.loadRgb16BitComparison(
        before, secondResult, width, height, 0, 65535);
    processLayout();
    check(std::abs(panel.zoom() - 1.0) < 0.0001,
          "Quick-preview refresh should preserve the user's 100% zoom");

    const QPixmap refreshedPixmap = imageLabel->pixmap();
    check(refreshedPixmap.width() >= width * 2,
          "Quick-preview refresh should preserve split comparison mode");

    const QByteArray screenshotPath = qgetenv(
        "STARPROCESSOR_PREVIEW_TEST_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        check(panel.grab().save(QString::fromUtf8(screenshotPath)),
              "Preview test screenshot should be writable");
    }

    std::cout << "Preview panel tests passed\n";
    return 0;
}
