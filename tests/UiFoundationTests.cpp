#include "ui/InspectorWidgets.h"
#include "ui/StyleTokens.h"
#include "ui/ViewStateStore.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QString>

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

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    bool success = true;

    const QString styleSheet = StyleTokens::appStyleSheet();
    success &= require(!styleSheet.isEmpty(),
                       "Application stylesheet is empty");
    success &= require(!styleSheet.contains(QStringLiteral("{{")),
                       "Application stylesheet contains an unresolved token");

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
    first.maskOverlayVisible = true;
    states.save(QStringLiteral("first"), first);
    states.save(QStringLiteral("second"), ViewStateStore::ViewState{});
    const auto restored = states.stateFor(QStringLiteral("first"));
    success &= require(restored.has_value() && restored->zoom == 2.0
                           && restored->horizontalScroll == 140
                           && restored->comparisonMode
                               == ViewStateStore::ComparisonMode::Split
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

    return success ? 0 : 1;
}
