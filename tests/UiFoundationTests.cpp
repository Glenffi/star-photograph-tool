#include "ui/InspectorWidgets.h"
#include "ui/StyleTokens.h"
#include "ui/ViewStateStore.h"
#include "ui/TaskStatusBar.h"
#include "ui/WorkflowRail.h"

#include <QApplication>
#include <QAbstractButton>
#include <QColor>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QString>
#include <QTranslator>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

double relativeLuminance(const QColor& color) {
    const auto linear = [](double component) {
        component /= 255.0;
        return component <= 0.04045
            ? component / 12.92
            : std::pow((component + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.red())
        + 0.7152 * linear(color.green())
        + 0.0722 * linear(color.blue());
}

double contrastRatio(const QColor& first, const QColor& second) {
    const double firstLuminance = relativeLuminance(first);
    const double secondLuminance = relativeLuminance(second);
    const double lighter = std::max(firstLuminance, secondLuminance);
    const double darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

bool require(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

class RailTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* sourceText,
                      const char*, int) const override {
        if (qstrcmp(context, "Nebulens") == 0 && qstrcmp(sourceText, "校准") == 0) {
            return QStringLiteral("Calibration");
        }
        return {};
    }
};

int stageLabelInkWidth(QAbstractButton* stage) {
    const QImage image = stage->grab().toImage().scaled(stage->size());
    int left = image.width();
    int right = -1;
    // Skip the number and the status dot in the upper part of the button.
    for (int y = 22; y < image.height() - 5; ++y) {
        for (int x = 5; x < image.width() - 5; ++x) {
            if (relativeLuminance(image.pixelColor(x, y)) > 0.25) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
    }
    return right - left + 1;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    bool success = true;

    const QString styleSheet = StyleTokens::appStyleSheet();
    success &= require(!styleSheet.isEmpty(),
                       "Application stylesheet is empty");
    success &= require(!styleSheet.contains(QStringLiteral("{{")),
                       "Application stylesheet contains an unresolved token");
    app.setStyleSheet(styleSheet);

    const QColor background = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kBackgroundBase);
    success &= require(
        contrastRatio(StyleTokens::Colors::fromHex(
                          StyleTokens::Colors::kTextSecondary),
                      background) >= 7.0,
        "Secondary text does not meet the planned 7:1 contrast ratio");
    success &= require(
        contrastRatio(StyleTokens::Colors::fromHex(
                          StyleTokens::Colors::kTextFaint),
                      background) >= 4.5,
        "Faint text does not meet the planned 4.5:1 contrast ratio");

    const QColor translucent = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kMaskSky);
    success &= require(translucent.alpha() == 0x66,
                       "RRGGBBAA token parsing produced the wrong alpha");

    ViewStateStore states(2);
    ViewStateStore::ViewState first;
    first.zoomMode = ViewStateStore::ZoomMode::Manual;
    first.zoom = 2.0;
    first.horizontalScroll = 140;
    first.verticalScroll = 80;
    first.comparisonMode = ViewStateStore::ComparisonMode::Split;
    first.splitPosition = 0.31;
    first.maskOverlayVisible = true;
    states.save(QStringLiteral("first"), first);
    states.save(QStringLiteral("second"), ViewStateStore::ViewState{});
    const auto restored = states.stateFor(QStringLiteral("first"));
    success &= require(restored.has_value() && restored->zoom == 2.0
                           && restored->horizontalScroll == 140
                           && restored->comparisonMode
                               == ViewStateStore::ComparisonMode::Split
                           && std::abs(restored->splitPosition - 0.31) < 0.0001
                           && restored->maskOverlayVisible,
                       "View state did not round-trip");
    states.save(QStringLiteral("third"), ViewStateStore::ViewState{});
    success &= require(states.contains(QStringLiteral("first"))
                           && !states.contains(QStringLiteral("second")),
                       "View state LRU eviction order is incorrect");

    InspectorSection section(QString::fromUtf8("基础调整"));
    auto* slider = new ValueSlider(-100, 100, 25, &section);
    slider->setShowPositiveSign(true);
    slider->setSuffix(QStringLiteral(" EV"));
    slider->setAccessibleName(QString::fromUtf8("曝光"));
    section.addWidget(slider);
    auto* toggle = new CompactToggle(QString::fromUtf8("启用降噪"), &section);
    toggle->setChecked(true);
    section.addWidget(toggle);
    success &= require(slider->valueLabel()->text() == QStringLiteral("+25 EV"),
                       "Value slider formatting is incorrect");
    success &= require(toggle->isChecked()
                           && toggle->minimumSizeHint().width()
                               >= StyleTokens::Controls::kToggleWidth,
                       "Compact toggle state or geometry is incorrect");
    section.resize(340, 100);
    section.show();
    QApplication::processEvents();
    toggle->setAnimationEnabled(false);
    const QImage on = toggle->grab().toImage();
    toggle->setChecked(false);
    const QImage off = toggle->grab().toImage();
    int toggles = 0;
    QObject::connect(toggle, &QCheckBox::toggled, toggle, [&](bool) { ++toggles; });
    QCheckBox* baseToggle = toggle;
    {
        const QSignalBlocker blocker(baseToggle);
        baseToggle->setChecked(true);
    }
    success &= require(on != off && toggle->grab().toImage() == on &&
                           toggle->property("state").toString() == QStringLiteral("on") &&
                           toggles == 0,
                       "A blocked setChecked must repaint the correct state without a signal");

    toggle->setChecked(false);
    toggle->setAnimationEnabled(true);
    toggle->setChecked(true);
    {
        const QSignalBlocker blocker(baseToggle);
        baseToggle->setCheckState(Qt::Unchecked);
    }
    success &= require(toggles == 2 && !toggle->isChecked() &&
                           toggle->grab().toImage() == off &&
                           toggle->property("thumbPosition").toReal() == 0.0,
                       "A blocked check state must override an animation still in flight");

    TaskStatusBar statusBar;
    statusBar.resize(640, StyleTokens::Layout::kStatusBarHeight);
    statusBar.show();
    QApplication::processEvents();
    statusBar.setProgressVisible(true);
    success &= require(statusBar.progressBar()->width() == 640 &&
                           statusBar.progressBar()->height() ==
                               StyleTokens::Controls::kProgressHeight,
                       "Task progress line should span the status bar");

    RailTranslator translator;
    app.installTranslator(&translator);
    WorkflowRail rail;
    rail.show();
    QApplication::processEvents();
    success &= require(
        rail.width() == StyleTokens::Layout::kWorkflowRailWidth &&
            rail.currentStage() == WorkflowStage::Adjustment,
        "Workflow rail should use the instrument layout and default stage");
    const auto workflowButtons = rail.findChildren<QAbstractButton*>();
    success &= require(
        std::count_if(workflowButtons.begin(), workflowButtons.end(),
                      [](QAbstractButton* button) {
                          return button->isChecked();
                      }) == 1,
        "Workflow rail should expose exactly one selected stage");
    const auto calibrationButton = std::find_if(
        workflowButtons.cbegin(), workflowButtons.cend(), [](QAbstractButton* button) {
            return button->text() == QStringLiteral("Calibration");
        });
    success &= require(calibrationButton != workflowButtons.cend() &&
                           stageLabelInkWidth(*calibrationButton) >= 50,
                       "English calibration should occupy a readable full-width label");
    if (calibrationButton != workflowButtons.cend()) {
        QFont labelFont = (*calibrationButton)->font();
        labelFont.setPixelSize(StyleTokens::Typography::kBodySize - 1);
        labelFont.setWeight(QFont::DemiBold);
        success &= require(
            QFontMetrics(labelFont).horizontalAdvance((*calibrationButton)->text()) <=
                (*calibrationButton)->width() - 8,
            "The full English calibration label must fit at the intended font size");
    }
    if (!workflowButtons.isEmpty()) {
        auto* firstButton = workflowButtons.first();
        firstButton->setFocus(Qt::ActiveWindowFocusReason);
        QApplication::processEvents();
        success &= require(
            !firstButton->property("keyboardFocusVisible").toBool(),
            "Window activation must not draw a second workflow outline");
        const QImage inactiveFocus = firstButton->grab().toImage();
        firstButton->clearFocus();
        firstButton->setFocus(Qt::TabFocusReason);
        QApplication::processEvents();
        success &= require(
            firstButton->property("keyboardFocusVisible").toBool() &&
                firstButton->grab().toImage() != inactiveFocus,
            "Keyboard navigation should retain an accessible focus outline");
    }
    int stageChanges = 0;
    QObject::connect(&rail, &WorkflowRail::stageChanged,
                     [&](WorkflowStage) { ++stageChanges; });
    rail.setCurrentStage(WorkflowStage::Stacking, true);
    success &= require(
        rail.currentStage() == WorkflowStage::Stacking && stageChanges == 1,
        "Workflow stage selection should notify exactly once");
    rail.setScene(ProcessingScene::SingleFrame);
    success &= require(
        rail.currentStage() == WorkflowStage::Adjustment && stageChanges == 2,
        "Single-frame mode should leave only relevant workflow stages active");
    rail.setScene(ProcessingScene::Nightscape);
    rail.setCurrentStage(WorkflowStage::Adjustment, false);
    rail.setScene(ProcessingScene::Timelapse);
    success &= require(
        rail.currentStage() == WorkflowStage::Stacking && stageChanges == 3,
        "Timelapse should hide stages without user-facing settings");

    return success ? 0 : 1;
}
