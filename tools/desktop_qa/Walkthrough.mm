#include "WindowCapture.h"

#include "app/MainWindow.h"
#include "i18n/AppLocalization.h"
#include "ui/ParamsPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/PrintOrderPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/StyleTokens.h"
#include "ui/Toolbar.h"
#include "ui/UiAssets.h"
#include "ui/WorkflowRail.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <sys/stat.h>

namespace {
using namespace DesktopQA;

constexpr auto kStill = "printOrderProductStill";
constexpr auto kTrail = "printOrderProductStarTrail";
constexpr auto kTimelapse = "printOrderProductTimelapse";
constexpr auto kUpdateKey = "updates/lastCheckUtc";
constexpr auto kIsolationRoot = "NEBULENS_DESKTOP_QA_RUN_ROOT";

QJsonArray rectJson(NSRect rect) {
    return {rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
}

template<class T>
T& child(QWidget& root, const QString& name) {
    auto* widget = root.findChild<T*>(name);
    require(widget != nullptr, "Missing control: " + name);
    return *widget;
}

class InputObserver final : public QObject {
public:
    explicit InputObserver(QWidget& target) : target_(&target) {
        qApp->installEventFilter(this);
    }

    bool down = false;
    bool up = false;
    bool key = false;

private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && target_ && (widget == target_ || target_->isAncestorOf(widget))) {
            if (event->type() == QEvent::MouseButtonPress) down = event->spontaneous();
            if (event->type() == QEvent::MouseButtonRelease) up = event->spontaneous();
            if (event->type() == QEvent::KeyPress) key = event->spontaneous();
        }
        return false;
    }

    QPointer<QWidget> target_;
};

class NativeInput {
public:
    explicit NativeInput(QWidget& window) : window_(window), view_(nativeView(window)),
                                            native_(view_.window) {}

    NSWindow* window() const { return native_; }
    NSView* view() const { return view_; }

    void requireForeground() const {
        const auto isKey = [&] { return NSApp.isActive && native_.isKeyWindow; };
        require(native_.isVisible && !native_.isMiniaturized, "QA MainWindow is not on screen");
        if (!isKey()) {
            [NSApp activate];
            [native_ makeKeyAndOrderFront:nil];
            require(waitFor(isKey, 2500), "macOS did not restore focus to the QA MainWindow");
            pump(120);
        }
        require(isKey(), "QA MainWindow is not the active native key window");
    }

    void click(QWidget& widget, const QPoint& at) const {
        requireForeground();
        const QPoint inWindow = widget.mapTo(&window_, at);
        auto* hit = window_.childAt(inWindow);
        require(widget.isVisible() && widget.isEnabled() && widget.visibleRegion().contains(at) &&
                    hit && (hit == &widget || widget.isAncestorOf(hit)),
                "Control is clipped, disabled, or not hit-testable: " + widget.objectName());
        InputObserver observer(widget);
        const NSPoint point = viewPoint(view_, inWindow);
        static NSInteger eventNumber = 100;
        const NSInteger clickNumber = ++eventNumber;
        for (const auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
            NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:0
                timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:native_.windowNumber
                context:nil eventNumber:clickNumber clickCount:1
                pressure:type == NSEventTypeLeftMouseDown ? 1.0 : 0.0];
            require(event != nil, "Could not construct AppKit mouse event");
            [NSApp postEvent:event atStart:NO];
            pump(65);
        }
        require(observer.down && observer.up,
                "Cocoa did not deliver a spontaneous mouse press/release to " + widget.objectName());
    }

    void clickButton(QAbstractButton& button) const {
        bool clicked = false;
        const auto connection = QObject::connect(&button, &QAbstractButton::clicked, thisContext(),
                                                  [&] { clicked = true; });
        try {
            click(button, button.rect().center());
        } catch (...) {
            QObject::disconnect(connection);
            throw;
        }
        QObject::disconnect(connection);
        require(clicked, "No clicked signal from " + button.objectName());
    }

    void rightArrow(QWidget& focused) const {
        requireForeground();
        require(focused.hasFocus(), "The adjustment control did not receive keyboard focus");
        InputObserver observer(focused);
        postKey(NSRightArrowFunctionKey, 124, NSEventModifierFlagFunction);
        pump(80);
        require(observer.key, "Cocoa did not deliver a spontaneous key press to the slider");
    }

    void typeAscii(QLineEdit& edit, const QString& value) const {
        requireForeground();
        require(edit.hasFocus(), "The real file name field did not receive keyboard focus");
        InputObserver observer(edit);
        // ANSI virtual key codes for the isolated fixture's ASCII path.
        constexpr std::array<unsigned short, 26> letters{
            0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46, 45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6
        };
        constexpr std::array<unsigned short, 10> digits{29, 18, 19, 20, 21, 23, 22, 26, 28, 25};
        for (QChar letter : value) {
            const auto ch = letter.toLatin1();
            if (ch >= 'a' && ch <= 'z') postKey(ch, letters[ch - 'a']);
            else if (ch >= 'A' && ch <= 'Z') postKey(ch, letters[ch - 'A'], NSEventModifierFlagShift);
            else if (ch >= '0' && ch <= '9') postKey(ch, digits[ch - '0']);
            else if (ch == '/') postKey(ch, 44);
            else if (ch == '-') postKey(ch, 27);
            else if (ch == '.') postKey(ch, 47);
            else throw std::runtime_error("Unexpected character in isolated fixture path");
        }
        require(waitFor([&] { return edit.text() == value && observer.key; }, 3000),
                "Native typing did not fill the actual file name field");
    }

    void pressReturn() const {
        requireForeground();
        postKey(NSCarriageReturnCharacter, 36);
        pump(100);
    }

private:
    void postKey(unichar character, unsigned short code, NSEventModifierFlags flags = 0) const {
        NSString* key = [NSString stringWithCharacters:&character length:1];
        for (const auto type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
            NSEvent* event = [NSEvent keyEventWithType:type location:NSZeroPoint
                modifierFlags:flags timestamp:NSProcessInfo.processInfo.systemUptime
                windowNumber:native_.windowNumber context:nil characters:key
                charactersIgnoringModifiers:key isARepeat:NO keyCode:code];
            require(event != nil, "Could not construct AppKit key");
            [NSApp postEvent:event atStart:NO];
        }
    }

    QObject* thisContext() const { return &window_; }
    QWidget& window_;
    NSView* view_;
    NSWindow* native_;
};

struct NativeButton {
    NSWindowButton kind;
    const char* name;
};

constexpr std::array kNativeButtons{
    NativeButton{NSWindowCloseButton, "close"},
    NativeButton{NSWindowMiniaturizeButton, "minimize"},
    NativeButton{NSWindowZoomButton, "zoom"},
};

QJsonObject nativeButtons(NSWindow* window) {
    require(window.styleMask & NSWindowStyleMaskTitled, "The NSWindow is not titled");
    QJsonObject result;
    for (const auto& item : kNativeButtons) {
        NSButton* control = [window standardWindowButton:item.kind];
        require(control && control.window == window && !control.hiddenOrHasHiddenAncestor &&
                    control.enabled && !NSIsEmptyRect(control.visibleRect),
                QString("AppKit standard window button is not visible/enabled: ") + item.name);
        const auto bounds = control.bounds;
        const auto center = framePoint(window,
            [control convertPoint:NSMakePoint(NSMidX(bounds), NSMidY(bounds)) toView:nil]);
        result.insert(item.name, QJsonArray{center.x(), center.y()});
    }
    return result;
}

int colorPixels(const QImage& image, const QJsonArray& logicalCenter, qreal scale, size_t kind) {
    const QPoint center(qRound(logicalCenter[0].toDouble() * scale),
                        qRound(logicalCenter[1].toDouble() * scale));
    const int radius = qRound(4 * scale);
    const QRect box = QRect(center - QPoint(radius, radius), QSize(2 * radius + 1, 2 * radius + 1))
                          .intersected(image.rect());
    int count = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const int r = color.red(), g = color.green(), b = color.blue();
            if (color.alpha() > 200 &&
                ((kind == 0 && r > 165 && r > g + 35 && r > b + 35 && g < 175) ||
                 (kind == 1 && r > 170 && g > 135 && b < 135 && g > b + 45) ||
                 (kind == 2 && g > 135 && g > r + 30 && g > b + 25))) {
                ++count;
            }
        }
    }
    return count;
}

double meanImageDelta(const QImage& first, const QImage& second) {
    require(!first.isNull() && first.size() == second.size(), "Incomparable proof regions");
    double difference = 0;
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            const auto a = first.pixel(x, y);
            const auto b = second.pixel(x, y);
            difference += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
                          std::abs(qBlue(a) - qBlue(b));
        }
    }
    return difference / (3.0 * first.width() * first.height());
}

double meanLuma(const QImage& image) {
    require(!image.isNull(), "Cannot measure an empty preview");
    double total = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            total += qGray(image.pixel(x, y));
    return total / (static_cast<double>(image.width()) * image.height());
}

class API_AVAILABLE(macos(15.0)) Evidence {
public:
    Evidence(MainWindow& window, NativeInput& input, WindowCapture& capture, const QDir& output)
        : window_(window), input_(input), capture_(capture), output_(output) {
        clock_.start();
    }

    void mark(const QString& action, const QJsonObject& details = {}) {
        log.append(QJsonObject{{"ms", static_cast<double>(clock_.elapsed())},
                               {"action", action}, {"details", details}});
        std::cout << qPrintable(action) << std::endl;
    }

    QImage shot(const QString& name, QWidget* proof = nullptr, QRect* nativeRegion = nullptr) {
        pump(340);
        input_.requireForeground();
        auto* native = input_.window();
        const QJsonObject buttons = nativeButtons(native);
        const auto image = capture_.still(output_.filePath(name));
        const qreal scale = capture_.scale();
        const auto frame = native.frame;
        const QSize framePixels(qRound(NSWidth(frame) * scale), qRound(NSHeight(frame) * scale));
        QJsonObject colors;
        int visibleColors = 0;
        for (size_t i = 0; i < kNativeButtons.size(); ++i) {
            const auto name = kNativeButtons[i].name;
            const int count = colorPixels(image, buttons.value(name).toArray(), scale, i);
            colors.insert(name, count);
            visibleColors += count >= 4;
        }
        mark("snapshot", {{"file", name}, {"client_points", QJsonArray{window_.width(), window_.height()}},
                          {"native_frame_points", rectJson(frame)},
                          {"pixels", QJsonArray{image.width(), image.height()}},
                          {"native_button_centers_points", buttons}, {"native_button_color_pixels", colors}});
        require(image.size() == framePixels, "Screenshot dimensions do not match the whole native window: " + name);
        require(visibleColors == 3, "Native red/yellow/green were not visible at AppKit coordinates: " + name);
        if (!proof) return {};

        require(proof->isVisible(), "The proof control is not visible: " + proof->objectName());
        const QRect visible = proof->visibleRegion().boundingRect();
        require(!visible.isEmpty(), "The comparison control has no exposed area: " + proof->objectName());
        const QPoint origin = proof->mapTo(&window_, visible.topLeft());
        // Compare only an inner proof region. Do not crop or modify saved evidence.
        const QPoint first = origin + QPoint(visible.width() / 5, visible.height() / 5);
        const QPoint last = origin + QPoint(visible.width() * 4 / 5, visible.height() * 3 / 4);
        const auto toPixel = [&](const QPoint& point) {
            return (framePoint(native, viewPoint(input_.view(), point)) * scale).toPoint();
        };
        const QRect region(toPixel(first), toPixel(last));
        require(image.rect().contains(region) && !region.isEmpty(), "Proof comparison region is clipped");
        mark("proof-region", {{"file", name}, {"object", proof->objectName()},
                              {"source_pixels", QJsonArray{region.x(), region.y(), region.width(), region.height()}}});
        if (nativeRegion) {
            *nativeRegion = region;
            return image.copy(region).convertToFormat(QImage::Format_RGB32);
        }
        return image.copy(region).scaled(96, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGB32);
    }

    QJsonArray log;

private:
    MainWindow& window_;
    NativeInput& input_;
    WindowCapture& capture_;
    QDir output_;
    QElapsedTimer clock_;
};

void selectProduct(MainWindow& window, NativeInput& input, const QString& name, ProcessingScene expected) {
    auto& button = child<QAbstractButton>(window, name);
    input.clickButton(button);
    require(button.isChecked() && child<ProjectPanel>(window, "projectPanel").currentScene() == expected,
            "Native product input did not change MainWindow's scene: " + name);
}

int API_AVAILABLE(macos(15.0))
adjustExposure(MainWindow& window, NativeInput& input, Evidence& evidence, const QString& control, int steps) {
    auto& params = child<ParamsPanel>(window, "paramsPanel");
    QSlider* exposure = control.isEmpty() ? nullptr : &child<QSlider>(params, control);
    if (!exposure) {
        for (auto* slider : params.findChildren<QSlider*>()) {
            if (slider->isVisible() && slider->minimum() == -50 && slider->maximum() == 50) {
                require(!exposure, "Exposure slider is ambiguous; specify --exposure-control");
                exposure = slider;
            }
        }
    }
    require(exposure && exposure->isVisible() && exposure->isEnabled(),
            "No usable exposure slider in the adjustment inspector");
    const int before = params.basicAdjustmentOptions().exposureTenths;
    require(exposure->value() == before, "Selected slider is not the exposure parameter");
    QStyleOptionSlider option;
    option.initFrom(exposure);
    option.orientation = exposure->orientation();
    option.minimum = exposure->minimum();
    option.maximum = exposure->maximum();
    option.sliderPosition = exposure->sliderPosition();
    option.sliderValue = exposure->value();
    option.upsideDown = exposure->invertedAppearance();
    const QRect handle = exposure->style()->subControlRect(QStyle::CC_Slider, &option,
                                                            QStyle::SC_SliderHandle, exposure);
    require(handle.isValid(), "Could not locate the real exposure slider handle");
    input.click(*exposure, handle.center());
    for (int i = 0; i < steps; ++i) input.rightArrow(*exposure);
    const int after = params.basicAdjustmentOptions().exposureTenths;
    require(after > before && after == exposure->value(), "Native input did not change exposure");
    evidence.mark("adjust-exposure-with-right-arrow", {{"before_tenths_ev", before}, {"after_tenths_ev", after}});
    return after;
}

int API_AVAILABLE(macos(15.0))
verifyRenderedExposure(MainWindow& window, QWidget& detail, NativeInput& input, Evidence& evidence,
                       const QString& control) {
    auto& state = child<QLabel>(detail, "previewResultState");
    auto& rendered = child<QLabel>(detail, "previewImageLabel");
    require(rendered.isVisible() && !rendered.pixmap().isNull(), "No rendered source photo in the attachment");
    const auto waitForPreview = [&](int exposure, qint64 previousPixmap) {
        const QString ev = QLocale().toString(exposure / 10.0, 'f', 1) + " EV";
        require(waitFor([&] {
            const QPixmap pixmap = rendered.pixmap();
            return state.isVisible() && !state.visibleRegion().isEmpty() && state.text().contains(ev) &&
                   !pixmap.isNull() && pixmap.cacheKey() != previousPixmap;
        }, 12000), "The preview never displayed the requested exposure trial: " + ev);
        evidence.mark("exposure-preview-ready", {{"ev", exposure / 10.0}, {"label", state.text()}});
    };

    // The product does not label the unadjusted source as a trial at 0 EV.
    const auto originalPixmap = rendered.pixmap().cacheKey();
    const int lower = adjustExposure(window, input, evidence, control, 2);
    waitForPreview(lower, originalPixmap);
    QRect beforeRect;
    const QImage before = evidence.shot("09-exposure-before.png", &rendered, &beforeRect);
    pump(650);

    const auto beforePixmap = rendered.pixmap().cacheKey();
    const int higher = adjustExposure(window, input, evidence, control, 8);
    waitForPreview(higher, beforePixmap);
    QRect afterRect;
    const QImage after = evidence.shot("09-exposure-after.png", &rendered, &afterRect);
    require(beforeRect == afterRect, "The actual photo view moved during the exposure comparison");
    const double beforeLuma = meanLuma(before);
    const double afterLuma = meanLuma(after);
    const double delta = meanImageDelta(before, after);
    evidence.mark("rendered-exposure-pixel-check", {{"crop_pixels", QJsonArray{beforeRect.x(), beforeRect.y(),
                       beforeRect.width(), beforeRect.height()}}, {"before_ev", lower / 10.0},
                       {"after_ev", higher / 10.0}, {"before_luma_0_255", beforeLuma},
                       {"after_luma_0_255", afterLuma}, {"mean_rgb_delta", delta}});
    require(delta > 0.75 && afterLuma > beforeLuma + 0.75,
            "The captured photo did not brighten after increasing exposure");
    return higher;
}

void API_AVAILABLE(macos(15.0))
importPrivateFixture(MainWindow& window, NativeInput& input, Evidence& evidence, const QString& fixture) {
    auto& order = child<PrintOrderPanel>(window, "printOrderPanel");
    auto& project = child<ProjectPanel>(window, "projectPanel");
    auto* preview = window.findChild<PreviewPanel*>();
    require(preview != nullptr, "No production source preview");
    const auto emptyProof = order.proofImage().cacheKey();
    bool sourceReady = false;
    QSize sourceSize;
    QObject observer;
    QObject::connect(preview, &PreviewPanel::sourcePreviewReady, &observer,
        [&](const QString& source, const QImage& image) {
            if (!image.isNull() && QFileInfo(source) == QFileInfo(fixture)) {
                sourceReady = true;
                sourceSize = image.size();
            }
        });
    input.clickButton(child<QAbstractButton>(order, "printOrderStepAssets"));
    auto& import = child<QAbstractButton>(order, "printOrderImportFiles");
    bool dialogAcceptedByInput = false;
    QString dialogFailure;
    QTimer driver;
    driver.setInterval(75);
    QObject::connect(&driver, &QTimer::timeout, &driver, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        driver.stop();
        try {
            QObject::connect(dialog, &QDialog::accepted, &driver, [&] { dialogAcceptedByInput = true; });
            NativeInput dialogInput(*dialog);
            auto& edit = child<QLineEdit>(*dialog, "fileNameEdit");
            dialogInput.click(edit, edit.rect().center());
            dialogInput.typeAscii(edit, fixture);
            auto* box = dialog->findChild<QDialogButtonBox*>();
            auto* open = box ? box->button(QDialogButtonBox::Open) : nullptr;
            require(open != nullptr, "The production file chooser did not expose an Open button");
            require(waitFor([&] { return open->isEnabled(); }, 3000), "The file chooser did not accept the fixture path");
            // Qt's filename completion popup may first consume Return to
            // confirm the path. The next Return then reaches the dialog.
            for (int attempt = 0; dialog->isVisible() && attempt < 2; ++attempt)
                dialogInput.pressReturn();
            dialogAcceptedByInput = dialogAcceptedByInput && dialog->result() == QDialog::Accepted;
            if (!dialogAcceptedByInput) {
                dialogFailure = "The real file chooser did not accept Return";
                dialog->reject();
            }
        } catch (const std::exception& e) {
            dialogFailure = QString::fromUtf8(e.what());
            dialog->reject();
        }
    });
    driver.start();
    evidence.mark("click-import-files", {{"object", import.objectName()}});
    input.clickButton(import);
    driver.stop();
    require(dialogAcceptedByInput, "Production RAW chooser was not accepted through AppKit input: " + dialogFailure);
    evidence.mark("raw-import-via-production-file-dialog", {{"confirm", "AppKit Return key"},
                                                            {"dialog_in_video", false},
                                                            {"source_path_logged", false}});
    require(waitFor([&] {
        const auto proof = order.proofImage();
        return sourceReady && project.allFilePaths().size() == 1 && !proof.isNull() && proof.cacheKey() != emptyProof;
    }, 25000), "The imported RAW never produced a nonempty source preview");
    evidence.mark("source-preview-ready", {{"count", 1}, {"source_pixels", QJsonArray{sourceSize.width(), sourceSize.height()}}});
}

void API_AVAILABLE(macos(15.0))
walkthrough(MainWindow& window, NativeInput& input, Evidence& evidence, const QString& proofName,
            const QString& originStep, const QString& exposureControl, const QString& fixture) {
    auto& order = child<QWidget>(window, "printOrderPanel");
    auto& detail = child<QWidget>(order, "advancedDarkroom");
    auto& toolbar = child<Toolbar>(detail, "mainToolbar");
    auto mastheads = window.findChildren<QWidget*>("paperMasthead");
    require(mastheads.size() == 1 && mastheads.first()->isVisible(),
            "Exactly one visible, shared paperMasthead is required");
    const QPointer<QWidget> masthead = mastheads.first();
    const auto checkLayer = [&](bool advanced) {
        const auto current = window.findChildren<QWidget*>("paperMasthead");
        require(order.isVisible() && current.size() == 1 && masthead &&
                    current.first() == masthead && masthead->isVisible() &&
                    detail.isVisible() == advanced && toolbar.isVisible() == advanced,
                advanced ? "Advanced layer did not open under the shared masthead"
                         : "Product sheet is not visible with the advanced layer closed");
    };
    checkLayer(false);
    auto& proof = child<QWidget>(order, proofName);
    const auto hold = [] { pump(650); };
    const auto choose = [&](const QString& name, ProcessingScene scene) {
        selectProduct(window, input, name, scene);
        evidence.mark("click-product", {{"object", name}, {"scene", static_cast<int>(scene)}});
    };

    choose(kStill, ProcessingScene::Nightscape);
    const QImage still = evidence.shot("01-still.png", &proof);
    hold();
    choose(kTrail, ProcessingScene::StarTrail);
    const QImage trail = evidence.shot("02-star-trail.png", &proof);
    hold();
    choose(kTimelapse, ProcessingScene::Timelapse);
    const QImage timelapse = evidence.shot("03-timelapse.png", &proof);
    hold();

    QMap<int, QPointer<QAbstractButton>> frames;
    const QRegularExpression frameName(QStringLiteral("^printOrderSampleFrame([1-9][0-9]*)$"));
    for (auto* button : order.findChildren<QAbstractButton*>()) {
        const auto match = frameName.match(button->objectName());
        if (!match.hasMatch()) continue;
        const int index = match.captured(1).toInt();
        require(index > 0 && !frames.contains(index), "Invalid or duplicate sample frame: " + button->objectName());
        frames.insert(index, button);
    }
    evidence.mark("timelapse-frame-controls", {{"count", frames.size()}});
    require(frames.size() >= 2 && frames.contains(1) && frames.contains(2), "No manual frame 1/2 controls");
    QImage previous;
    double largestFrameDelta = 0;
    for (auto it = frames.cbegin(); it != frames.cend(); ++it) {
        auto* button = it.value().data();
        require(button, "Sample frame control disappeared while browsing");
        const QString name = button->objectName();
        input.clickButton(*button);
        require(!button->isCheckable() || button->isChecked(), "Sample frame is not selected: " + name);
        evidence.mark("click-sample-frame", {{"object", name}, {"index", it.key()},
                                             {"checked", button->isCheckable() ? QJsonValue(button->isChecked())
                                                                             : QJsonValue::Null}});
        QImage current = evidence.shot(QString("04-timelapse-frame-%1.png").arg(it.key(), 2, 10, QChar('0')),
                                       &proof);
        if (!previous.isNull()) {
            const double delta = meanImageDelta(previous, current);
            largestFrameDelta = std::max(largestFrameDelta, delta);
            evidence.mark("proof-delta-adjacent-frames", {{"ending_at", it.key()}, {"mean_rgb_delta", delta}});
        }
        previous = current;
        hold();
    }

    const double stillTrail = meanImageDelta(still, trail);
    const double stillTimelapse = meanImageDelta(still, timelapse);
    const double trailTimelapse = meanImageDelta(trail, timelapse);
    evidence.mark("proof-deltas-products", {{"still_star_trail", stillTrail}, {"still_timelapse", stillTimelapse},
                                             {"star_trail_timelapse", trailTimelapse}});
    constexpr double minimumVisibleDelta = 0.8;
    require(largestFrameDelta >= minimumVisibleDelta, "No visible sample material change across manual frames");
    require(std::min({stillTrail, stillTimelapse, trailTimelapse}) >= minimumVisibleDelta,
            "At least two products have indistinguishable proof imagery");

    choose(kStill, ProcessingScene::Nightscape);
    evidence.shot("05-back-to-still.png", &proof);
    hold();
    if (!fixture.isEmpty()) {
        importPrivateFixture(window, input, evidence, fixture);
        evidence.shot("06-private-raw-source.png", &proof);
        hold();
    }
    auto& step = child<QAbstractButton>(order, originStep);
    input.clickButton(step);
    require(step.isChecked(), "The requested origin step did not become current: " + originStep);
    evidence.mark("click-origin-step", {{"object", originStep}, {"text", step.text()}});
    evidence.shot("07-origin-step.png");
    hold();
    auto& advanced = child<QAbstractButton>(order, "printOrderAdvanced");
    input.clickButton(advanced);
    checkLayer(true);
    evidence.mark("open-advanced", {{"object", advanced.objectName()}, {"masthead_shared", true}});
    if (!fixture.isEmpty())
        require(!child<QWidget>(detail, "previewEmptyState").isVisible(),
                "The advanced attachment is still showing an empty canvas");
    evidence.shot("08-advanced.png");
    hold();

    // A build may route this entry to Assets or Calibration instead of Adjustment.
    auto* rail = detail.findChild<WorkflowRail*>();
    require(rail != nullptr, "No workflow rail in the advanced layer");
    if (rail->currentStage() != WorkflowStage::Adjustment) {
        const auto adjustment = QCoreApplication::translate("Nebulens", u8"\u8c03\u6574");
        auto buttons = rail->findChildren<QAbstractButton*>();
        const auto found = std::find_if(buttons.cbegin(), buttons.cend(), [&](const auto* button) {
            return button->isVisible() && (button->text() == adjustment || button->accessibleName() == adjustment);
        });
        require(found != buttons.cend(), "No visible Adjustment step in the advanced workflow");
        input.clickButton(**found);
        require(rail->currentStage() == WorkflowStage::Adjustment, "Could not enter the Adjustment inspector");
    }
    const int adjusted = fixture.isEmpty() ? adjustExposure(window, input, evidence, exposureControl, 5)
                                           : verifyRenderedExposure(window, detail, input, evidence, exposureControl);
    if (fixture.isEmpty()) evidence.shot("09-advanced-adjusted.png");
    hold();

    auto& returnButton = child<QAbstractButton>(toolbar, "returnToPrintOrder");
    const QString returnName = returnButton.objectName();
    input.clickButton(returnButton);
    checkLayer(false);
    require(step.isVisible() && step.isChecked() &&
                child<QAbstractButton>(order, kStill).isChecked() &&
                child<ProjectPanel>(window, "projectPanel").allFilePaths().size() == (fixture.isEmpty() ? 0 : 1),
            "Advanced return did not restore the same step, still product and source count");
    evidence.mark("return-to-origin-step", {{"return_button", returnName}, {"restored_step", originStep},
                                            {"masthead_shared", true}});
    evidence.shot("10-returned-to-origin.png");
    hold();
    if (!fixture.isEmpty()) {
        if (originStep != "printOrderStepOutput")
            input.clickButton(child<QAbstractButton>(order, "printOrderStepOutput"));
        auto& quick = child<QAbstractButton>(order, "printOrderQuickToggle");
        if (!quick.isChecked()) input.clickButton(quick);
        auto& ev = child<QDoubleSpinBox>(order, "printOrderQuickExposure");
        require(ev.isVisible() && ev.isEnabled() && qRound(ev.value() * 10) == adjusted,
                "Quick exposure did not reflect the real advanced adjustment");
        evidence.mark("verify-quick-exposure-value", {{"object", ev.objectName()}, {"ev", ev.value()}});
        evidence.shot("11-quick-exposure.png");
        hold();
    }
}

void writeReport(const QDir& output, const QJsonObject& report) {
    QSaveFile json(output.filePath("run.json"));
    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    require(json.open(QIODevice::WriteOnly) && json.write(payload) == payload.size() && json.commit(),
            "Cannot write run.json");
}

int API_AVAILABLE(macos(15.0)) run(int argc, char* argv[]) {
    umask(S_IRWXG | S_IRWXO);
    const QString root = qEnvironmentVariable(kIsolationRoot);
    const QDir base(root);
    const QString home = base.filePath("home");
    require(root.startsWith("/private/tmp/nebulens-qa-home-") && QFileInfo(home).isDir() &&
                qEnvironmentVariable("HOME") == home && qEnvironmentVariable("CFFIXED_USER_HOME") == home,
            "The GUI child did not start with an isolated home");
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    require(app.platformName() == "cocoa", "QA must use the actual Cocoa window");
    QCommandLineParser parser;
    parser.setApplicationDescription("Nebulens MainWindow native desktop evidence (macOS 15+)");
    parser.addHelpOption();
    parser.addOptions({
        {"evidence-dir", "Fresh directory for original screenshots, recording and input log.", "path"},
        {"language", "Application language, zh_CN or en.", "language", "zh_CN"},
        {"size", "Qt client size in logical points.", "WIDTHxHEIGHT", "1280x740"},
        {"still-only", "Capture the initial state only (no video)."},
        {"probe", "Test native capture and product clicks on a pre-unification build."},
        {"proof", "Object name of the visual proof area.", "objectName", "printOrderProof"},
        {"origin-step", "Optional step before Advanced (Assets without RAW, Save with RAW).", "objectName"},
        {"exposure-control", "Optional object name of the advanced exposure slider.", "objectName"},
        {"raw", "Optional local RAW alias for private review evidence.", "path"},
    });
    parser.process(app);
    require(parser.isSet("evidence-dir"), "--evidence-dir is required");
    require(!(parser.isSet("probe") && parser.isSet("still-only")), "Choose either --probe or --still-only");
    const QString language = parser.value("language");
    require(language == "zh_CN" || language == "en", "Use --language=zh_CN or --language=en");
    const auto sizeMatch = QRegularExpression(QStringLiteral("^([0-9]{3,4})x([0-9]{3,4})$"))
                               .match(parser.value("size"));
    require(sizeMatch.hasMatch(), "Use --size=WIDTHxHEIGHT");
    const QSize size(sizeMatch.captured(1).toInt(), sizeMatch.captured(2).toInt());
    require(size.width() >= 920 && size.height() >= 540, "Size is below Nebulens's 920x540 minimum");
    const QString destination = parser.value("evidence-dir");
    require(!destination.trimmed().isEmpty() && QDir().mkpath(destination), "Cannot create evidence directory");
    const QDir output(destination);
    require(output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty(),
            "Evidence directory must be empty");
    QString fixture;
    if (parser.isSet("raw")) {
        require(!parser.isSet("probe") && !parser.isSet("still-only"), "--raw requires a full walkthrough");
        require(output.canonicalPath().contains("/star-photograph-tool-review/"),
                "RAW captures must remain under the private star-photograph-tool-review directory");
        const QString source = parser.value("raw");
        require(QFileInfo(source).isFile(), "The private RAW alias is not readable");
        const QString extension = QFileInfo(source).suffix().toLower();
        require(QRegularExpression(QStringLiteral("^[a-z0-9]{2,4}$")).match(extension).hasMatch(),
                "The private RAW alias must have a file extension");
        fixture = QDir(home).filePath("field-exposure." + extension);
        // Give the production chooser a local copy; do not show/log the alias target.
        require(QFile::copy(source, fixture) && !QFileInfo(fixture).isSymLink(),
                "Could not stage the private RAW fixture in the isolated home");
    }
    const QString originStep = parser.isSet("origin-step") ? parser.value("origin-step")
        : fixture.isEmpty() ? QStringLiteral("printOrderStepAssets") : QStringLiteral("printOrderStepOutput");

    app.setApplicationName("StarProcessor");
    app.setOrganizationName("StarProcessor");
    app.setApplicationVersion(STARPROCESSOR_VERSION);
    app.setApplicationDisplayName("Nebulens");
    app.setWindowIcon(UiAssets::appIcon());
    const QString userSettings = base.filePath("user-settings");
    for (auto format : {QSettings::IniFormat, QSettings::NativeFormat}) {
        QSettings::setPath(format, QSettings::UserScope, userSettings);
        QSettings::setPath(format, QSettings::SystemScope, base.filePath("system-settings"));
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    QSettings featureSettings("StarProcessor", "App");
    require(QDir(QDir::homePath()).canonicalPath() == QDir(home).canonicalPath(),
            "Unexpected Qt home directory: " + QDir::homePath());
    require(settings.fileName().startsWith(userSettings + '/') &&
                (featureSettings.fileName().startsWith(userSettings + '/') ||
                 featureSettings.fileName().startsWith(home + "/Library/Preferences/")),
            "Unexpected settings paths: " + settings.fileName() + "; " + featureSettings.fileName());
    featureSettings.setValue("outputPath", base.filePath("output"));
    featureSettings.sync();
    require(featureSettings.status() == QSettings::NoError, "Cannot initialize isolated feature settings");
    const auto updateTimestamp = QDateTime::currentDateTimeUtc();
    settings.setValue(kUpdateKey, updateTimestamp);
    settings.sync();
    require(settings.status() == QSettings::NoError && QSettings().value(kUpdateKey).toDateTime() == updateTimestamp,
            "Could not inhibit automatic update scheduling");
    AppLocalization::setPreferredLanguage(language);
    AppLocalization::install(app);
    app.setStyleSheet(StyleTokens::appStyleSheet());

    int finishedNetworkReplies = 0;
    MainWindow window;
    for (auto* manager : window.findChildren<QNetworkAccessManager*>()) {
        QObject::connect(manager, &QNetworkAccessManager::finished, &window,
                         [&] { ++finishedNetworkReplies; });
    }
    window.resize(size);
    window.show();
    NativeInput input(window);
    [input.window() center];
    const bool probe = parser.isSet("probe");
    const bool stillOnly = parser.isSet("still-only");
    QJsonObject report{{"status", "fail"}, {"mode", probe ? "old-window-probe" : stillOnly ? "still-only" : "walkthrough"},
                       {"language", language}, {"application_version", app.applicationVersion()},
                       {"binary", app.applicationFilePath()}, {"pid", static_cast<qint64>(app.applicationPid())},
                       {"native_window_id", static_cast<qint64>(input.window().windowNumber)},
                       {"platform", app.platformName()}, {"isolated_home", home},
                       {"capture", "ScreenCaptureKit currentProcess + exact desktopIndependentWindow ID; no display/child windows/audio"},
                       {"recording", "Live SCScreenshotManager CMSampleBuffers, host-clock timestamps, one realtime AVAssetWriter session, no PNG assembly or editing"},
                       {"private_raw_fixture", !fixture.isEmpty()},
                       {"input", "synthetic NSEvents through own NSApplication; checked spontaneous Cocoa delivery; no global event tap"},
                       {"native_controls", "AppKit standardWindowButton identity and visibility plus captured colors; not clicked"}};
    std::unique_ptr<WindowCapture> capture;
    std::unique_ptr<Evidence> evidence;
    QString error;
    try {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activate];
        [input.window() makeKeyAndOrderFront:nil];
        require(waitFor([&] { return NSApp.isActive && input.window().isKeyWindow; }),
                "No active GUI session or macOS did not make the QA window key");
        pump(180);
        if (window.size() != size) {
            window.resize(size);
            pump(180);
        }
        require(window.size() == size, QString("Real client size %1x%2 differs from requested %3x%4")
                    .arg(window.width()).arg(window.height()).arg(size.width()).arg(size.height()));
        require(child<ProjectPanel>(window, "projectPanel").allFilePaths().isEmpty(),
                "Initial order is not empty");
        const bool translated = !window.windowTitle().contains(QChar(0x661f));
        require(translated == (language == "en"), "The application title did not use the requested localization");
        report.insert("window_title", window.windowTitle());
        capture = std::make_unique<WindowCapture>(input.window());
        evidence = std::make_unique<Evidence>(window, input, *capture, output);
        if (!stillOnly) {
            if (probe) evidence->shot("probe-00-before-recording.png");
            capture->start(output.filePath(probe ? "probe-original.mp4" : "walkthrough-original.mp4"));
            evidence->mark("recording-started");
            pump(600);
        }
        if (probe) {
            evidence->shot("probe-01-still.png");
            pump(650);
            selectProduct(window, input, kTrail, ProcessingScene::StarTrail);
            evidence->mark("probe-click-star-trail");
            evidence->shot("probe-02-star-trail.png");
            pump(650);
            selectProduct(window, input, kStill, ProcessingScene::Nightscape);
            evidence->mark("probe-click-still");
            evidence->shot("probe-03-back-to-still.png");
            pump(500);
        } else if (stillOnly) {
            auto& masthead = child<QWidget>(window, "paperMasthead");
            require(masthead.isVisible() && !child<QWidget>(window, "advancedDarkroom").isVisible(),
                    "Initial print order or shared masthead is not visible");
            evidence->shot("initial.png");
        } else {
            walkthrough(window, input, *evidence, parser.value("proof"), originStep,
                        parser.value("exposure-control"), fixture);
        }
    } catch (const std::exception& failure) {
        error = QString::fromUtf8(failure.what());
    }
    if (capture && !stillOnly) {
        try {
            capture->stop();
            const qint64 bytes =
                QFileInfo(output.filePath(probe ? "probe-original.mp4" : "walkthrough-original.mp4")).size();
            require(bytes > 0 && capture->recordedSeconds() > 0 && capture->frameCount() > 0,
                    "Recording is empty");
            report.insert("video_bytes", bytes);
            report.insert("video_seconds", capture->recordedSeconds());
            report.insert("native_video_frames", capture->frameCount());
            report.insert("encoder_backpressure_drops", capture->droppedFrames());
            report.insert("max_sample_gap_ms", capture->longestGap() * 1000);
            evidence->mark("recording-finished");
        } catch (const std::exception& failure) {
            if (!error.isEmpty()) error += "\n";
            error += QString::fromUtf8(failure.what());
        }
    }
    if (evidence) report.insert("events", evidence->log);
    const bool updateUnchanged = QSettings().value(kUpdateKey).toDateTime() == updateTimestamp;
    int liveNetworkReplies = 0;
    for (auto* manager : window.findChildren<QNetworkAccessManager*>())
        liveNetworkReplies += manager->findChildren<QNetworkReply*>().size();
    report.insert("update_timestamp_unchanged", updateUnchanged);
    report.insert("finished_network_replies", finishedNetworkReplies);
    report.insert("live_network_replies", liveNetworkReplies);
    if (finishedNetworkReplies || liveNetworkReplies || !updateUnchanged) {
        if (!error.isEmpty()) error += "\n";
        error += "An update or Qt network reply was detected";
    }
    report.insert("status", error.isEmpty() ? "pass" : "fail");
    if (!error.isEmpty()) report.insert("failure", error);
    writeReport(output, report);
    if (!error.isEmpty()) std::cerr << qPrintable(error) << std::endl;
    return error.isEmpty() ? 0 : 1;
}

int launchWithIsolatedHome(int argc, char* argv[]) {
    QCoreApplication launcher(argc, argv);
    QTemporaryDir isolated("/private/tmp/nebulens-qa-home-XXXXXX");
    require(isolated.isValid(), "Cannot create a fresh QA home");
    const QDir base(isolated.path());
    require(base.mkpath("home") && base.mkpath("output"), "Cannot initialize QA home");
    const QString home = base.filePath("home");

    // Cocoa/CoreFoundation can cache the home before QApplication is created.
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(kIsolationRoot, isolated.path());
    environment.insert("HOME", home);
    environment.insert("CFFIXED_USER_HOME", home);
    environment.insert("XDG_CONFIG_HOME", home + "/config");
    environment.insert("XDG_CACHE_HOME", home + "/cache");
    environment.insert("XDG_DATA_HOME", home + "/data");
    environment.insert("QT_QPA_PLATFORM", "cocoa");
    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.setProcessEnvironment(environment);
    child.start(launcher.applicationFilePath(), launcher.arguments().mid(1));
    require(child.waitForStarted(), "Could not start isolated GUI child: " + child.errorString());
    require(child.waitForFinished(180000) && child.exitStatus() == QProcess::NormalExit,
            "The isolated GUI process crashed or did not finish");
    return child.exitCode();
}

} // namespace

int main(int argc, char* argv[]) {
    @autoreleasepool {
        try {
            if (@available(macOS 15.0, *)) {
                return qEnvironmentVariableIsEmpty(kIsolationRoot) ? launchWithIsolatedHome(argc, argv)
                                                                   : run(argc, argv);
            }
            throw std::runtime_error("Native QA recording requires macOS 15 or later");
        } catch (const std::exception& failure) {
            std::cerr << failure.what() << std::endl;
            return 1;
        }
    }
}
