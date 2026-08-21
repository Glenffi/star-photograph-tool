#pragma once

#include <QColor>
#include <QString>

// Central design tokens for the StarProcessor desktop UI. Components should
// consume these values instead of introducing local colors or dimensions.
namespace StyleTokens {

namespace Colors {
inline constexpr char kBackgroundCanvas[] = "#0A0E13";
inline constexpr char kBackgroundBase[] = "#10161D";
inline constexpr char kBackgroundRaised[] = "#161E27";
inline constexpr char kBackgroundOverlay[] = "#1C2632";
inline constexpr char kBackgroundScrim[] = "#06090CD9";

inline constexpr char kLineSubtle[] = "#223040";
inline constexpr char kLineStrong[] = "#33465A";

inline constexpr char kTextPrimary[] = "#DCE6EE";
// Ratios against kBackgroundBase: 7.23:1 and 4.52:1 respectively.
inline constexpr char kTextSecondary[] = "#91A6B7";
inline constexpr char kTextFaint[] = "#6F8190";

inline constexpr char kAccent[] = "#8FB4D9";
inline constexpr char kAccentStrong[] = "#A9C8E6";
inline constexpr char kAccentDim[] = "#3A5570";

inline constexpr char kAction[] = "#D9C9A8";
inline constexpr char kActionHover[] = "#E3D5B8";
inline constexpr char kActionPressed[] = "#C4B391";
inline constexpr char kActionText[] = "#1A1710";

inline constexpr char kOk[] = "#7FB08A";
inline constexpr char kWarning[] = "#C9A86A";
inline constexpr char kError[] = "#C97B6E";
// Eight-digit values retain the design document's #RRGGBBAA notation.
inline constexpr char kMaskSky[] = "#8FB4D966";
inline constexpr char kMaskGround[] = "#D9C9A866";

// Parses #RRGGBB and the design system's #RRGGBBAA notation into QColor.
QColor fromHex(const char* hex);
}  // namespace Colors

namespace Spacing {
inline constexpr int kMicro = 4;
inline constexpr int kBase = 8;
inline constexpr int kControlGap = 10;
inline constexpr int kPanelPadding = 16;
inline constexpr int kSectionGap = 20;
inline constexpr int kCanvasPadding = 24;
}  // namespace Spacing

namespace Layout {
inline constexpr int kLeftPanelWidth = 280;
inline constexpr int kLeftPanelMinimumWidth = 240;
inline constexpr int kLeftPanelMaximumWidth = 360;
inline constexpr int kRightPanelWidth = 320;
inline constexpr int kRightPanelMinimumWidth = 280;
inline constexpr int kRightPanelMaximumWidth = 420;
inline constexpr int kStatusBarHeight = 28;
}  // namespace Layout

namespace Radius {
inline constexpr int kSmall = 4;
inline constexpr int kMedium = 6;
inline constexpr int kFull = 999;
}  // namespace Radius

namespace Controls {
inline constexpr int kCompactHeight = 26;
inline constexpr int kDialogHeight = 30;
inline constexpr int kIconButtonSize = 26;
inline constexpr int kIconSize = 20;
inline constexpr int kComboItemHeight = 28;
inline constexpr int kTabHeight = 34;
inline constexpr int kMaterialRowHeight = 56;
inline constexpr int kButtonHorizontalPadding = 14;
inline constexpr int kInputHorizontalPadding = 8;
inline constexpr int kCheckIndicatorSize = 14;
inline constexpr int kSliderTrackHeight = 2;
inline constexpr int kSliderHandleSize = 12;
inline constexpr int kSliderValueWidth = 44;
inline constexpr int kToggleWidth = 32;
inline constexpr int kToggleHeight = 18;
inline constexpr int kStatusDotSize = 6;
inline constexpr int kProgressHeight = 2;
}  // namespace Controls

namespace Typography {
inline constexpr int kDisplaySize = 20;
inline constexpr int kDisplayWeight = 600;
inline constexpr int kDisplayLineHeight = 28;
inline constexpr int kTitleSize = 14;
inline constexpr int kTitleWeight = 600;
inline constexpr int kTitleLineHeight = 20;
inline constexpr int kBodySize = 13;
inline constexpr int kBodyWeight = 400;
inline constexpr int kBodyLineHeight = 19;
inline constexpr int kCaptionSize = 11;
inline constexpr int kCaptionWeight = 400;
inline constexpr int kCaptionLineHeight = 16;
inline constexpr int kMonoSize = 12;
inline constexpr int kMonoWeight = 400;
inline constexpr int kMonoLineHeight = 17;

QString uiFontFamily();
QString monoFontFamily();
}  // namespace Typography

namespace Motion {
inline constexpr int kInstantMs = 80;
inline constexpr int kFastMs = 140;
inline constexpr int kBaseMs = 200;
inline constexpr char kEasing[] = "cubic-bezier(0.2, 0, 0, 1)";
}  // namespace Motion

// Shared dynamic-property names and values used by appStyleSheet(). Keeping
// these here prevents similarly named properties from acquiring global scope.
namespace Properties {
inline constexpr char kUiRole[] = "uiRole";
inline constexpr char kCanvas[] = "canvas";
inline constexpr char kPanel[] = "panel";
inline constexpr char kRaised[] = "raised";
inline constexpr char kOverlay[] = "overlay";
inline constexpr char kSeparator[] = "separator";

inline constexpr char kTextRole[] = "textRole";
inline constexpr char kDisplay[] = "display";
inline constexpr char kTitle[] = "title";
inline constexpr char kSecondary[] = "secondary";
inline constexpr char kFaint[] = "faint";
inline constexpr char kCaption[] = "caption";
inline constexpr char kMono[] = "mono";
inline constexpr char kStatus[] = "status";

inline constexpr char kVariant[] = "variant";
inline constexpr char kPrimary[] = "primary";
inline constexpr char kSecondaryButton[] = "secondary";
inline constexpr char kGhost[] = "ghost";
inline constexpr char kDanger[] = "danger";
inline constexpr char kIcon[] = "icon";

inline constexpr char kControlRole[] = "controlRole";
inline constexpr char kToggle[] = "toggle";
inline constexpr char kProgressLine[] = "progressLine";
inline constexpr char kStatusDot[] = "statusDot";

inline constexpr char kStatusRole[] = "statusRole";
inline constexpr char kSuccess[] = "success";
inline constexpr char kWarning[] = "warning";
inline constexpr char kError[] = "error";

inline constexpr char kSizeRole[] = "sizeRole";
inline constexpr char kDialog[] = "dialog";
}  // namespace Properties

// Returns the complete Qt application stylesheet generated from the tokens
// above. Apply it once with QApplication::setStyleSheet().
QString appStyleSheet();

}  // namespace StyleTokens
