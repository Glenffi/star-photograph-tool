#pragma once

#include <QColor>
#include <QString>

// Central design tokens for the Nebulens desktop UI. Components should
// consume these values instead of introducing local colors or dimensions.
namespace StyleTokens {

namespace Colors {
inline constexpr char kBackgroundBase[] = "#E8DFCB";
inline constexpr char kBackgroundRaised[] = "#F4EDDE";
inline constexpr char kBackgroundOverlay[] = "#DCDAC7";

inline constexpr char kLineSubtle[] = "#AB9D82";
inline constexpr char kLineStrong[] = "#7A705C";

inline constexpr char kTextPrimary[] = "#302F28";
// Both secondary roles retain WCAG AA contrast against kBackgroundBase.
inline constexpr char kTextSecondary[] = "#474438";
inline constexpr char kTextFaint[] = "#625B4D";
inline constexpr char kTextDisabled[] = "#756D5D";

inline constexpr char kAccent[] = "#304E42";
inline constexpr char kAccentStrong[] = "#233D33";
inline constexpr char kAccentDim[] = "#D2DBC9";
inline constexpr char kFocus[] = "#246148";
inline constexpr char kFocusOnAction[] = "#D5E5D3";

inline constexpr char kAction[] = "#304E42";
inline constexpr char kActionHover[] = "#263F35";
inline constexpr char kActionPressed[] = "#192D25";
inline constexpr char kActionText[] = "#FAF5E8";

inline constexpr char kOk[] = "#365C43";
inline constexpr char kWarning[] = "#774D24";
inline constexpr char kError[] = "#873C34";

// Image surround and image annotations are independent of the paper palette.
inline constexpr char kBackgroundCanvas[] = "#343434";
inline constexpr char kPhotoChrome[] = "#EEEEEE";
inline constexpr char kPhotoLine[] = "#808080";
inline constexpr char kPhotoInk[] = "#F5F5F5";
inline constexpr char kPhotoBadge[] = "#242424";

// Eight-digit values retain the design document's #RRGGBBAA notation.
inline constexpr char kMaskSky[] = "#6FA8A366";
inline constexpr char kMaskGround[] = "#C99E5D66";

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
inline constexpr int kWorkflowIndexHeight = 36;
inline constexpr int kFilmstripHeight = 102;
inline constexpr int kLeftPanelWidth = 112;
inline constexpr int kLeftPanelMinimumWidth = 96;
inline constexpr int kLeftPanelMaximumWidth = 140;
inline constexpr int kRightPanelWidth = 368;
inline constexpr int kRightPanelMinimumWidth = 330;
inline constexpr int kRightPanelMaximumWidth = 430;
inline constexpr int kStatusBarHeight = 28;
}  // namespace Layout

namespace Radius {
inline constexpr int kSmall = 4;
inline constexpr int kMedium = 6;
inline constexpr int kFull = 999;
}  // namespace Radius

namespace Controls {
inline constexpr int kCompactHeight = 30;
inline constexpr int kDialogHeight = 34;
inline constexpr int kPreviewToolHeight = 26;
inline constexpr int kIconButtonSize = 30;
inline constexpr int kIconSize = 18;
inline constexpr int kComboItemHeight = 32;
inline constexpr int kTabHeight = 38;
inline constexpr int kMaterialRowHeight = 76;
inline constexpr int kButtonHorizontalPadding = 14;
inline constexpr int kInputHorizontalPadding = 8;
inline constexpr int kCheckIndicatorSize = 14;
inline constexpr int kSliderTrackHeight = 2;
inline constexpr int kSliderHandleSize = 12;
inline constexpr int kSliderValueWidth = 62;
inline constexpr int kSliderMinimumWidth = 80;
inline constexpr int kFormLabelWidth = 124;
inline constexpr int kFormRowGap = 10;
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
QString serifFontFamily();
}  // namespace Typography

namespace Motion {
inline constexpr int kInstantMs = 80;
inline constexpr int kFastMs = 140;
inline constexpr int kBaseMs = 200;
inline constexpr int kImageMs = 280;
inline constexpr int kPreviewDebounceMs = 400;
inline constexpr char kEasing[] = "cubic-bezier(0.2, 0.75, 0.25, 1)";
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
inline constexpr char kSection[] = "section";
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
