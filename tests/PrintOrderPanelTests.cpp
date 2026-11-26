#include "ui/PrintOrderPanel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QProxyStyle>
#include <QRegion>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {

const QSize kWindowSize(920, 540);
constexpr auto kLight = "printOrderLight";
constexpr auto kNext = "printOrderNext";
constexpr auto kSummary = "printOrderSummary";
constexpr auto kProof = "printOrderProof";
constexpr auto kPaper = "printOrderPaper";
constexpr auto kAmbient = "printOrderAmbientTimer";
constexpr auto kPreference = "printOrderMotionPreferenceTimer";
constexpr auto kReducedTooltip = "Follows your system's Reduce Motion setting";

enum class Page { Product, Assets, Output, Receipt };
enum class Motion { Auto, Moving, Reduced };

void check(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}

template <typename T>
T& child(QObject& parent, const char* name) {
    auto* widget = parent.findChild<T*>(QString::fromLatin1(name));
    check(widget, QStringLiteral("Missing %1: %2").arg(T::staticMetaObject.className(), name));
    return *widget;
}

QString copy(bool english, const char* en, const char* zh) {
    return QString::fromUtf8(english ? en : zh);
}

void showWindow(QWidget& window) {
    window.resize(kWindowSize);
    window.show();
    QApplication::processEvents();
    check(window.size() == kWindowSize, "The test window must fit at 920x540 logical pixels");
}

void fits(const QWidget& widget) {
    const QWidget* window = widget.window();
    const QRect bounds(widget.mapTo(window, QPoint()), widget.size());
    check(widget.isVisible() && !bounds.isEmpty() && window->rect().contains(bounds) &&
              widget.visibleRegion().boundingRect() == widget.rect(),
          "Clipped or hidden at 920x540: " + widget.objectName());
}

void usable(QWidget& widget) {
    fits(widget);
    const QWidget* window = widget.window();
    const QWidget* hit = window->childAt(widget.mapTo(window, widget.rect().center()));
    check(widget.isEnabled() && hit && (hit == &widget || widget.isAncestorOf(hit)),
          "Control is not usable: " + widget.objectName());
}

void click(PrintOrderPanel& panel, const char* name) {
    auto& button = child<QAbstractButton>(panel, name);
    usable(button);
    button.click();
    QApplication::processEvents();
}

void choose(QComboBox& combo, int target) {
    usable(combo);
    check(target >= 0 && target < combo.count(), "Missing choice in " + combo.objectName());
    for (int attempt = 0; combo.currentIndex() != target && attempt < combo.count(); ++attempt) {
        const int before = combo.currentIndex();
        QKeyEvent key(QEvent::KeyPress, before < target ? Qt::Key_Down : Qt::Key_Up,
                      Qt::NoModifier);
        QApplication::sendEvent(&combo, &key);
        check(combo.currentIndex() != before, "Choice ignored keyboard input: " + combo.objectName());
    }
    QApplication::processEvents();
    check(combo.currentIndex() == target, "Wrong selection in " + combo.objectName());
}

void expectPage(PrintOrderPanel& panel, Page page) {
    constexpr std::array pages{"printOrderProductPage", "printOrderAssetsPage",
                               "printOrderOutputPage", "printOrderReceiptPage"};
    constexpr std::array stages{"printOrderStepProduct", "printOrderStepAssets",
                                "printOrderStepOutput"};
    const int index = static_cast<int>(page);
    auto& current = child<QWidget>(panel, pages[index]);
    check(panel.window()->size() == kWindowSize &&
              child<QStackedWidget>(panel, "printOrderPages").currentWidget() == &current,
          "Wrong step or window size: " + current.objectName());
    fits(current);
    for (int i = 0; i < static_cast<int>(stages.size()); ++i) {
        auto& stage = child<QAbstractButton>(panel, stages[i]);
        fits(stage);
        check(stage.isChecked() == (i == std::min(index, 2)),
              "Wrong active step: " + stage.objectName());
    }
    fits(child<QAbstractButton>(panel, kProof));
    fits(child<QLabel>(panel, kSummary));
}

void expectNote(PrintOrderPanel& panel, const char* name, const QString& expected) {
    auto& note = child<QLabel>(panel, name);
    fits(note);
    check(note.text().simplified() == expected.simplified(),
          QStringLiteral("Wrong %1: %2").arg(note.objectName(), note.text()));
    check(!note.wordWrap() || note.heightForWidth(note.width()) <= note.height(),
          "The wrapped message is clipped: " + note.objectName());
}

void verifyReceipt(PrintOrderPanel& panel, const QDir& folder, int written, bool sequence,
                   bool english) {
    auto& next = child<QAbstractButton>(panel, kNext);
    int requests = 0;
    QString revealed;
    QObject observer;
    QObject::connect(&panel, &PrintOrderPanel::processRequested, &observer, [&] { ++requests; });
    QObject::connect(&panel, &PrintOrderPanel::revealRequested, &observer,
                     [&](const QString& path) { revealed = path; });
    click(panel, kNext);
    check(requests == 1, "A ready order must submit exactly once");
    panel.setTask(true, 37, {});
    QApplication::processEvents();
    auto& progress = child<QProgressBar>(panel, "printOrderTaskProgress");
    fits(progress);
    check(progress.value() == 37 && next.isEnabled(), "Running order must show progress and allow cancel");

    QImage image(12, 8, QImage::Format_RGB32);
    image.fill(Qt::blue);
    for (int i = 0; i < written; ++i) {
        check(image.save(folder.filePath(QStringLiteral("frame_%1.png").arg(i))),
              "Cannot write a temporary delivery image");
    }
    const auto files = folder.entryList({QStringLiteral("*.png")}, QDir::Files);
    check(files.size() == written, "Wrong number of files in the temporary delivery");
    for (const auto& name : files) {
        check(QImage(folder.filePath(name)).size() == image.size(), "Delivery image cannot be decoded");
    }
    const QString path = sequence ? folder.absolutePath() : folder.filePath(files.front());
    // This is the panel's delivery boundary; the full RAW/worker path is covered by print_order_flow.
    panel.setReceipt(path, static_cast<int>(files.size()));
    expectPage(panel, Page::Output);
    panel.setTask(false, 100, {});
    QApplication::processEvents();
    expectPage(panel, Page::Receipt);
    const QString actual = copy(english, "%1 saved", u8"%1 \u5f20\u6210\u54c1").arg(files.size());
    expectNote(panel, "printOrderResultCount", actual);
    expectNote(panel, kSummary, actual);
    auto& resultPath = child<QLineEdit>(panel, "printOrderResultPath");
    usable(resultPath);
    check(!next.isVisible() && resultPath.isReadOnly() &&
              resultPath.text() == QDir::toNativeSeparators(path),
          "The receipt must expose the actual delivery path, not the requested folder");
    click(panel, "printOrderReveal");
    check(revealed == path, "Reveal must pass the current file or sequence directory unchanged");
}

void testWorkflow(bool english) {
    QTemporaryDir output;
    check(output.isValid(), "Cannot create a temporary output folder");
    const QDir folder(output.path());
    PrintOrderPanel panel;
    QList<ProcessingScene> products;
    QList<WorkflowStage> advanced;
    int imports = 0;
    int directories = 0;
    QString formatRequested;
    QObject observer;
    QObject::connect(&panel, &PrintOrderPanel::productRequested, &observer,
                     [&](ProcessingScene scene) { products.append(scene); });
    QObject::connect(&panel, &PrintOrderPanel::advancedRequested, &observer,
                     [&](WorkflowStage stage) { advanced.append(stage); });
    QObject::connect(&panel, &PrintOrderPanel::importFilesRequested, &observer, [&] { ++imports; });
    QObject::connect(&panel, &PrintOrderPanel::directoryRequested, &observer, [&] { ++directories; });
    QObject::connect(&panel, &PrintOrderPanel::formatRequested, &observer,
                     [&](const QString& format) { formatRequested = format; });
    QObject::connect(&panel, &PrintOrderPanel::newOrderRequested, &panel, &PrintOrderPanel::resetOrder);

    showWindow(panel);
    expectPage(panel, Page::Product);
    expectNote(panel, "printOrderTitle", copy(english, "Print Order", u8"\u51b2\u5370\u5355"));
    auto& still = child<QAbstractButton>(panel, "printOrderProductStill");
    auto& trail = child<QAbstractButton>(panel, "printOrderProductStarTrail");
    auto& sequence = child<QAbstractButton>(panel, "printOrderProductTimelapse");
    auto& next = child<QAbstractButton>(panel, kNext);
    const auto& proof = child<QAbstractButton>(panel, kProof);
    check(still.isChecked() && !trail.isChecked() && !sequence.isChecked() &&
              !panel.proofImage().isNull() && proof.accessibleDescription().contains(
                  copy(english, "Sample Proof", u8"\u793a\u4f8b\u5370\u6837")),
          "Startup must offer the default still product and an identified sample proof");
    click(panel, "printOrderProductStarTrail");
    check(trail.isChecked() && !still.isChecked(), "Star trail must replace the still selection");
    click(panel, "printOrderProductTimelapse");
    check(sequence.isChecked() && !trail.isChecked(), "Timelapse must replace the star trail selection");
    click(panel, "printOrderProductStill");
    check(still.isChecked() && !sequence.isChecked() &&
              products == QList<ProcessingScene>{ProcessingScene::StarTrail, ProcessingScene::Timelapse,
                                                 ProcessingScene::Nightscape},
          "Product controls must select mutually exclusive real scenes");

    click(panel, kNext);
    expectPage(panel, Page::Assets);
    const QString noFrames = copy(english, "No RAW frames imported yet",
                                  u8"\u5c1a\u672a\u5bfc\u5165 RAW \u7d20\u6750");
    expectNote(panel, "printOrderAssetNote", noFrames);
    click(panel, "printOrderImportFiles");
    click(panel, kNext);
    expectPage(panel, Page::Output);
    expectNote(panel, "printOrderOutputNote", noFrames);
    click(panel, "printOrderOutputImportFiles");
    check(imports == 2 && !next.isEnabled(), "Both steps must offer import, but not save without RAWs");
    click(panel, "printOrderBack");
    expectPage(panel, Page::Assets);

    auto& subject = child<QComboBox>(panel, "printOrderSubject");
    const int deepSky = subject.findData(static_cast<int>(ProcessingScene::DeepSky));
    choose(subject, deepSky);
    check(products.size() > 3 && products.back() == ProcessingScene::DeepSky,
          "The Assets subject menu must select the Deep Sky scene");
    panel.setProject(4, 6, "Capture set", false, {});
    QApplication::processEvents();
    const QString calibration = copy(english,
        "Add Dark, Flat, and either Bias or exposure-matched Dark Flats; Masters are also supported",
        u8"\u8bf7\u8865\u9f50 Dark\u3001Flat\uff0c\u4ee5\u53ca Bias \u6216\u540c\u66dd\u5149 Dark Flat\uff1b"
        u8"\u4e5f\u53ef\u5bfc\u5165 Master");
    const QString addCalibration = copy(english, "Add Calibration Frames", u8"\u8865\u9f50\u6821\u51c6");
    expectNote(panel, "printOrderCalibrationNote", calibration);
    check(child<QAbstractButton>(panel, "printOrderCalibration").text() == addCalibration,
          "The Assets action must identify how to fix missing calibration");
    click(panel, "printOrderCalibration");
    click(panel, "printOrderStepOutput");
    expectPage(panel, Page::Output);
    panel.setOutput("tiff16", output.path());
    QApplication::processEvents();
    expectNote(panel, "printOrderOutputNote", calibration);
    check(child<QAbstractButton>(panel, "printOrderOutputCalibration").text() == addCalibration,
          "The Save step must offer the same actionable calibration fix");
    click(panel, "printOrderOutputCalibration");
    check(advanced == QList<WorkflowStage>{WorkflowStage::Calibration, WorkflowStage::Calibration},
          "Both calibration shortcuts must open the calibration stage");
    int blockedRequests = 0;
    const auto blocked = QObject::connect(
        &panel, &PrintOrderPanel::processRequested, &observer, [&] { ++blockedRequests; });
    next.click();
    check(!next.isEnabled() && blockedRequests == 0,
          "Missing calibration must block saving even with sufficient lights and a destination");
    QObject::disconnect(blocked);
    panel.setProject(4, 6, "Capture set", true, {});
    QApplication::processEvents();
    check(next.isEnabled() && !child<QWidget>(panel, "printOrderOutputNote").isVisible() &&
              !child<QWidget>(panel, "printOrderOutputCalibration").isVisible(),
          "Completing calibration must clear the save blocker");
    click(panel, "printOrderChooseDirectory");
    auto& format = child<QComboBox>(panel, "printOrderFormat");
    choose(format, format.findData(QStringLiteral("png8")));
    check(directories == 1 && formatRequested == "png8", "Delivery settings must be actionable");
    verifyReceipt(panel, folder, 1, false, english);

    click(panel, "printOrderNewOrder");
    expectPage(panel, Page::Product);
    check(still.isChecked() && child<QLineEdit>(panel, "printOrderResultPath").text().isEmpty(),
          "New order must remove the previous receipt and restore the default product");
    click(panel, "printOrderProductTimelapse");
    click(panel, kNext);
    expectPage(panel, Page::Assets);
    check(!subject.isVisible(), "Timelapse must not offer a still-image subject selection");
    panel.setProject(4, 6, "Capture set", false, {});
    panel.setOutput("png8", output.path());
    click(panel, kNext);
    expectPage(panel, Page::Output);
    expectNote(panel, kSummary, copy(english, "Timelapse frames: 4", u8"4 \u5f20\u5ef6\u65f6\u7167\u7247"));
    check(folder.mkdir("sequence"), "Cannot create the job's sequence subdirectory");
    verifyReceipt(panel, QDir(folder.filePath("sequence")), 2, true, english);
}

constexpr std::array<QRgb, 4> kSwatches{
    qRgb(209, 15, 92), qRgb(12, 148, 218), qRgb(11, 193, 66), qRgb(230, 104, 18)
};

QImage swatchProof() {
    QImage proof(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < proof.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(proof.scanLine(y));
        for (int x = 0; x < proof.width(); ++x) {
            row[x] = kSwatches[2 * (y >= proof.height() / 2) + (x >= proof.width() / 2)];
        }
    }
    return proof;
}

QImage render(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image;
}

void checkIdlePixels(PrintOrderPanel& panel) {
    auto& paper = child<QWidget>(panel, kPaper);
    auto& proof = child<QAbstractButton>(panel, kProof);
    panel.setProof(swatchProof(), "Motion test print");
    QApplication::processEvents();
    const QImage baseline = render(paper);
    const QRect proofBounds(proof.mapTo(&paper, QPoint()), proof.size());
    check(baseline.rect().contains(proofBounds), "The proof must fit on the paper");

    // Locate the actual painted photograph, without assuming mount dimensions or DPR.
    QRect photo;
    unsigned seen = 0;
    for (int y = proofBounds.top(); y <= proofBounds.bottom(); ++y) {
        for (int x = proofBounds.left(); x <= proofBounds.right(); ++x) {
            const auto color = std::find(kSwatches.begin(), kSwatches.end(), baseline.pixel(x, y));
            if (color == kSwatches.end()) continue;
            seen |= 1u << (color - kSwatches.begin());
            photo |= QRect(x, y, 1, 1);
        }
    }
    check(seen == 0b1111 && photo.width() > 4 && photo.height() > 4,
          "All four generated colors must be visible in the painted proof");
    const QRect sample = photo.adjusted(photo.width() / 4, photo.height() / 4,
                                        -photo.width() / 4, -photo.height() / 4);
    const QImage fixed = baseline.copy(sample);

    // Exclude child widgets so hover, focus, and page transitions cannot masquerade as lighting.
    QRegion barePaper(paper.rect());
    for (auto* widget : paper.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (widget->isVisible()) barePaper -= widget->geometry();
    }
    check(!barePaper.isEmpty(), "No exposed paper to sample");

    bool paperMoved = false;
    bool proofMoved = false;
    int frames = 0;
    QEventLoop loop;
    QObject::connect(&child<QTimer>(panel, kAmbient), &QTimer::timeout, &loop, [&] {
        const QImage frame = render(paper);
        ++frames;
        proofMoved = frame.copy(sample) != fixed;
        if (!paperMoved) {
            for (const QRect& rect : barePaper) {
                if (frame.copy(rect) != baseline.copy(rect)) {
                    paperMoved = true;
                    break;
                }
            }
        }
        if (proofMoved || (paperMoved && frames >= 3)) loop.quit();
    });
    QTimer::singleShot(750, &loop, &QEventLoop::quit);
    loop.exec();
    check(!proofMoved, "Ambient lighting changed the rendered proof pixels or their position");
    check(paperMoved && frames >= 3, "Allowed idle lighting did not change visible paper pixels in 750 ms");
}

bool appSuspended() {
    const auto state = QGuiApplication::applicationState();
    return state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended;
}

void expectReduced(PrintOrderPanel& panel) {
    const auto& light = child<QComboBox>(panel, kLight);
    check(!light.isEnabled() && light.currentText() == "Still" &&
              light.toolTip() == kReducedTooltip && !child<QTimer>(panel, kAmbient).isActive(),
          "Reduce Motion must lock the control to Still, explain why, and stop the ambient timer");
}

class NoAnimationStyle final : public QProxyStyle {
public:
    NoAnimationStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* data) const override {
        if (hint == SH_Widget_Animation_Duration) return 0;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

void testMotion(Motion expectation) {
    QStackedWidget window;
    PrintOrderPanel panel;
    QWidget alternate;
    window.addWidget(&panel);
    window.addWidget(&alternate);
    showWindow(window);
    auto& light = child<QComboBox>(panel, kLight);
    auto& ambient = child<QTimer>(panel, kAmbient);
    auto& poll = child<QTimer>(panel, kPreference);
    fits(light);
    if (appSuspended()) {
        check(!ambient.isActive() && !poll.isActive(), "A hidden application must stop both timers");
        check(expectation == Motion::Auto, "A visible desktop session is required for strict motion checks");
        std::cout << "[SKIP] Idle render: the platform reports a hidden or suspended application\n";
        return;
    }
    if (expectation == Motion::Reduced || !light.isEnabled()) {
        check(expectation != Motion::Moving, "This run requires motion but the platform disabled it");
        expectReduced(panel);
        std::cout << "[PASS] Platform Reduce Motion; moving pixels not applicable in this run\n";
        return;
    }

    check(light.currentText() == "Soft Light" && ambient.isActive() && poll.isActive(),
          "Visible Soft Light must start the ambient and preference timers");
    const int flow = light.findText(QStringLiteral("Light Sweep"));
    choose(light, flow);
    check(ambient.isActive(), "Light Sweep must keep the ambient timer running");
    checkIdlePixels(panel);

    QObject::connect(&panel, &PrintOrderPanel::proofRequested, &panel,
                     [&] { panel.setProofInspectionActive(true); });
    click(panel, kProof);
    check(!ambient.isActive() && !poll.isActive(), "Inspecting the proof must suspend both timers");
    panel.setProofInspectionActive(false);
    check(ambient.isActive() && poll.isActive(), "Returning from proof inspection must resume lighting");

    QObject::connect(&panel, &PrintOrderPanel::advancedRequested, &window,
                     [&](WorkflowStage) { window.setCurrentWidget(&alternate); });
    click(panel, "printOrderAdvanced");
    check(window.isVisible() && !panel.isVisible() && !ambient.isActive() && !poll.isActive(),
          "Switching to the other workspace must pause the order without hiding the window");
    window.setCurrentWidget(&panel);
    QApplication::processEvents();
    check(ambient.isActive() && poll.isActive() && light.currentIndex() == flow,
          "Returning to the order must restore its selected lighting");

    window.hide();
    check(!ambient.isActive() && !poll.isActive(), "Hiding the top-level window must stop both timers");
    showWindow(window);
    check(ambient.isActive() && poll.isActive(), "Showing the window again must resume lighting");

    choose(light, light.findText(QStringLiteral("Still")));
    check(light.isEnabled() && light.toolTip().isEmpty() && !ambient.isActive() && poll.isActive(),
          "User-selected Still must stop lighting without locking the control or preference monitor");
    window.hide();
    showWindow(window);
    check(light.currentText() == "Still" && !ambient.isActive(), "Showing a Still order must not restart lighting");
    choose(light, flow);
    check(ambient.isActive(), "Leaving Still must resume the ambient timer");
    std::cout << "[PASS] Idle paper pixels, fixed proof pixels, and lighting pause/resume\n";

    // Exercise a live Qt policy change without modifying the desktop accessibility setting.
    QApplication::setStyle(new NoAnimationStyle);
    QApplication::processEvents();
    expectReduced(panel);
    check(poll.isActive(), "A visible reduced-motion order must still watch for preference changes");
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::processEvents();
    check(light.isEnabled() && light.currentIndex() == flow && ambient.isActive(),
          "Removing reduced motion must restore the user's chosen light and restart the timer");
    std::cout << "[PASS] Live Qt reduced-animation policy change\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        check(!QGuiApplication::screens().isEmpty(),
              "No Qt screen available: use offscreen for CTest or a desktop session for native checks");
        const auto args = app.arguments();
        const bool moving = args.contains(QStringLiteral("--expect-motion"));
        const bool reduced = args.contains(QStringLiteral("--expect-reduced-motion"));
        check(!(moving && reduced), "Choose only one strict motion expectation");

        testWorkflow(false);
        QTranslator english;
        check(english.load(QStringLiteral(":/i18n/nebulens_en.qm")) && app.installTranslator(&english),
              "The test target must link the real English translation resource");
        testWorkflow(true);
        std::cout << "[PASS] Chinese and English workflow, 920x540 layout, and file/sequence receipts\n";

        testMotion(moving ? Motion::Moving : reduced ? Motion::Reduced : Motion::Auto);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
