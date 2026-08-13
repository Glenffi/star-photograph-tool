#include "ui/PreviewPanel.h"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QThread>

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

    double selectedX = -1.0;
    double selectedY = -1.0;
    QObject::connect(&panel, &PreviewPanel::imagePointSelected,
                     [&](double x, double y) {
                         selectedX = x;
                         selectedY = y;
                     });
    panel.setPointSelectionActive(true);
    panel.setBeforeAfterMode(true);
    processLayout();
    const QPixmap selectionPixmap = imageLabel->pixmap();
    const int paneWidth = (selectionPixmap.width() - 12) / 2;
    const QPoint localPoint(paneWidth + 12 + paneWidth / 4,
                            selectionPixmap.height() / 2);
    const QPointF globalPoint = imageLabel->mapToGlobal(localPoint);
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(localPoint),
                      QPointF(localPoint), globalPoint, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(imageLabel, &click);
    check(std::abs(selectedX - 0.25) < 0.01 &&
              std::abs(selectedY - 0.5) < 0.01,
          "Manual gray-point selection should use pane-local split coordinates");

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

    PreviewPanel maskPanel;
    maskPanel.resize(1000, 640);
    maskPanel.show();
    QImage groundScene(width, height, QImage::Format_RGB888);
    std::vector<uint8_t> automaticMask(
        static_cast<size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        uchar* row = groundScene.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int ridge = 96 + qRound(8.0 * std::sin(x * 0.03));
            const bool sky = y < ridge;
            row[x * 3] = static_cast<uchar>(sky ? 115 : 24);
            row[x * 3 + 1] = static_cast<uchar>(sky ? 132 : 38);
            row[x * 3 + 2] = static_cast<uchar>(sky ? 154 : 29);
            automaticMask[static_cast<size_t>(y) * width + x] =
                y < ridge + 26 ? 255 : 0;
        }
    }
    maskPanel.loadImage(groundScene);
    maskPanel.setMaskOverlay(automaticMask, width, height);
    processLayout();
    check(!maskPanel.hasEditedMask(),
          "Automatic detection alone should not be treated as a user edit");
    auto* maskImageLabel =
        maskPanel.findChild<QLabel*>("previewImageLabel");
    auto* undoButton =
        maskPanel.findChild<QPushButton*>("maskGroundHintUndo");
    check(maskImageLabel && undoButton && undoButton->isVisible(),
          "Ground-hint editing controls should appear with a detected mask");

    bool refinementFinished = false;
    QObject::connect(&maskPanel, &PreviewPanel::maskRefinementFinished,
                     [&](bool success) { refinementFinished = success; });
    const QPixmap maskPixmap = maskImageLabel->pixmap();
    const QPoint hintPoint(maskPixmap.width() / 2,
                           qRound(maskPixmap.height() * 0.60));
    const QPointF hintGlobal = maskImageLabel->mapToGlobal(hintPoint);
    QMouseEvent hintPress(
        QEvent::MouseButtonPress, QPointF(hintPoint), QPointF(hintPoint),
        hintGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(maskImageLabel, &hintPress);
    QMouseEvent hintRelease(
        QEvent::MouseButtonRelease, QPointF(hintPoint), QPointF(hintPoint),
        hintGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(maskImageLabel, &hintRelease);

    QElapsedTimer waitForRefinement;
    waitForRefinement.start();
    while (!refinementFinished && waitForRefinement.elapsed() < 3000) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    check(refinementFinished && maskPanel.hasEditedMask(),
          "Releasing a rough ground stroke should asynchronously refine the mask");
    const std::vector<uint8_t>& editedMask = maskPanel.editedMask();
    check(editedMask[120U * width + width / 2] == 0,
          "The painted missed-ground region should be classified as ground");

    const QByteArray maskScreenshotPath = qgetenv(
        "STARPROCESSOR_MASK_EDITOR_SCREENSHOT");
    if (!maskScreenshotPath.isEmpty()) {
        check(maskPanel.grab().save(QString::fromUtf8(maskScreenshotPath)),
              "Mask editor test screenshot should be writable");
    }

    undoButton->click();
    processLayout();
    check(!maskPanel.hasEditedMask() &&
              maskPanel.editedMask() == automaticMask,
          "Undoing the only ground hint should restore automatic detection");

    const QByteArray screenshotPath = qgetenv(
        "STARPROCESSOR_PREVIEW_TEST_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        check(panel.grab().save(QString::fromUtf8(screenshotPath)),
              "Preview test screenshot should be writable");
    }

    std::cout << "Preview panel tests passed\n";
    return 0;
}
