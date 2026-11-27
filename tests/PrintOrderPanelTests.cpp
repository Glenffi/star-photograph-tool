#include "ui/FrameReceiptDialog.h"
#include "ui/PrintOrderPanel.h"
#include "ui/RidgeComparisonDialog.h"
#include "ui/StyleTokens.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QRegion>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <cstdlib>
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

void testOrderEntryPoints() {
    PrintOrderPanel panel;
    int opened = 0;
    int saved = 0;
    int compared = 0;
    int processed = 0;
    QObject observer;
    QObject::connect(&panel, &PrintOrderPanel::openOrderRequested, &observer, [&] { ++opened; });
    QObject::connect(&panel, &PrintOrderPanel::saveOrderRequested, &observer, [&] { ++saved; });
    QObject::connect(&panel, &PrintOrderPanel::ridgeComparisonRequested, &observer, [&] { ++compared; });
    QObject::connect(&panel, &PrintOrderPanel::processRequested, &observer, [&] { ++processed; });
    showWindow(panel);

    auto& files = child<QPushButton>(panel, "printOrderFileButton");
    auto& menu = child<QMenu>(panel, "printOrderFileMenu");
    auto& open = child<QAction>(menu, "printOrderOpen");
    auto& save = child<QAction>(menu, "printOrderSave");
    auto& ridge = child<QAbstractButton>(panel, "printOrderRidgeComparison");
    usable(files);
    check(files.menu() == &menu && menu.actions() == QList<QAction*>{&open, &save} &&
              open.text() == QCoreApplication::translate("Nebulens", "打开冲印单") &&
              save.text() == QCoreApplication::translate("Nebulens", "保存冲印单"),
          "The page header must offer opening and saving the order separately from rendering");
    bool popupShown = false;
    QTimer::singleShot(0, &menu, [&] {
        popupShown = menu.isVisible() && menu.actionGeometry(&open).isValid();
        menu.setActiveAction(&open);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&menu, &key);
        menu.close();
    });
    files.showMenu();
    QApplication::processEvents();
    save.trigger();
    ridge.click();
    check(popupShown && opened == 1 && saved == 1 && processed == 0 && compared == 0 && !ridge.isVisible(),
          "Order file actions must emit distinct signals; an order without references cannot compare");

    panel.setScene(ProcessingScene::SkyGround);
    panel.setProject(0, 4, "Reopened capture set", true, {});
    panel.setRidgeComparisonAvailable(true);
    panel.showReopenedOrder();
    QApplication::processEvents();
    expectPage(panel, Page::Assets);
    check(ridge.text() == QCoreApplication::translate("Nebulens", "山脊对照"),
          "Reference comparison must be labeled");
    click(panel, "printOrderRidgeComparison");
    check(compared == 1, "A reopened order with a reference must support comparison");

    panel.setTask(true, 40, {});
    QApplication::processEvents();
    expectPage(panel, Page::Output);
    fits(ridge);
    check(!files.isEnabled() && !open.isEnabled() && !save.isEnabled() && !ridge.isEnabled(),
          "File actions and reference comparison must be locked during processing");
    open.trigger();
    save.trigger();
    ridge.click();
    panel.showReopenedOrder();
    panel.setTask(false, -1, {});
    QApplication::processEvents();
    expectPage(panel, Page::Output);
    check(opened == 1 && saved == 1 && compared == 1 && processed == 0,
          "Locked controls and a reopen request cannot change a running order");

    panel.setReceipt("delivery.tiff", 1);
    QApplication::processEvents();
    expectPage(panel, Page::Receipt);
    usable(files);
    usable(ridge);
    save.trigger();
    check(saved == 2 && processed == 0, "Saving an order from the receipt must not trigger a render");
    panel.showReopenedOrder();
    QApplication::processEvents();
    expectPage(panel, Page::Assets);
    check(child<QLineEdit>(panel, "printOrderResultPath").text().isEmpty() && ridge.isVisible(),
          "Reopening clears the previous receipt while keeping the loaded comparison");
    panel.resetOrder();
    panel.showReopenedOrder();
    QApplication::processEvents();
    expectPage(panel, Page::Product);
    check(!ridge.isVisible(), "A reset or empty order must not keep the previous comparison");
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

void testFrameSummary() {
    using Outcome = FrameReceipt::Outcome;
    FrameReceipt receipt;
    receipt.deliverables = 1;
    receipt.rows = {{"used.dng", Outcome::Used, true, 0, 24, 1.2, {}},
                    {"more.dng", Outcome::Used, false, 0, 32, 2.6, {}},
                    {"skipped.dng", Outcome::NoStars, false, 0, 0, -1.0, {}},
                    {"unknown.dng", Outcome::Unconfirmed, false, 0, -1, -1.0, {}},
                    {"other.dng", Outcome::NotSubmitted, false, 0, -1, -1.0, {}}};
    PrintOrderPanel panel;
    showWindow(panel);
    auto& summary = child<QLabel>(panel, "printOrderFrameSummary");
    auto& details = child<QAbstractButton>(panel, "printOrderFrameDetails");
    int requested = 0;
    QObject::connect(&panel, &PrintOrderPanel::frameReceiptRequested, &details, [&] { ++requested; });
    const auto expectNoDetails = [&] {
        QApplication::processEvents();
        check(summary.text().isEmpty() && summary.toolTip().isEmpty() && !summary.isVisible() &&
                  !details.isVisible() && !details.isEnabled(),
              "Inactive or legacy receipts must not leave an old summary or an actionable details button");
    };

    expectNoDetails();
    panel.setReceipt("delivery.tiff", 1, receipt);
    QApplication::processEvents();
    expectPage(panel, Page::Receipt);
    const auto noManual = QCoreApplication::translate("Nebulens", "入选 %1 张，采用 %2 张 · 跳过 %3 张")
                              .arg(4).arg(2).arg(1);
    expectNote(panel, "printOrderFrameSummary", noManual);
    expectNote(panel, "printOrderResultCount", QCoreApplication::translate("Nebulens", "%1 张成品").arg(1));
    check(summary.toolTip() == noManual.toHtmlEscaped() && !summary.wordWrap(),
          "Summary must remain a single line with the complete text on hover");
    click(panel, "printOrderFrameDetails");
    check(requested == 1, "The receipt entry must emit a details request");

    receipt.rows.append({"manual.dng", Outcome::ManualExcluded, false, 0, -1, -1.0, {}});
    panel.setTask(true, 20, {});
    expectNoDetails();
    details.click();
    check(requested == 1, "Busy orders cannot request the previous details");
    panel.setTask(false, -1, {});
    expectNoDetails();
    panel.setTask(true, 20, {});
    panel.setReceipt("next.tiff", 1, receipt);
    expectNoDetails();
    panel.setTask(false, 100, {});
    QApplication::processEvents();
    const auto manual = QCoreApplication::translate("Nebulens", "入选 %1 张，采用 %2 张 · 跳过 %3 张 · 手动排除 %4 张")
                             .arg(4).arg(2).arg(1).arg(1);
    expectNote(panel, "printOrderFrameSummary", manual);
    check(summary.toolTip() == manual.toHtmlEscaped(), "The manual exclusion suffix must not lose the tooltip");

    panel.setReceipt("next.tiff", 1);
    expectNoDetails();
    expectPage(panel, Page::Receipt);
    panel.setReceipt("next.tiff", 1, receipt);
    panel.clearReceipt();
    expectNoDetails();
    panel.setReceipt("next.tiff", 1, receipt);
    panel.showReopenedOrder();
    expectNoDetails();
    panel.setReceipt("next.tiff", 1, receipt);
    panel.resetOrder();
    expectNoDetails();
    panel.setReceipt("next.tiff", 1, receipt);
    panel.setScene(ProcessingScene::StarTrail);
    expectNoDetails();
}

template <typename Predicate>
bool waitForReceipt(Predicate ready) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    timer.start(12);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

void testFrameDetails() {
    using Outcome = FrameReceipt::Outcome;
    QTemporaryDir directory;
    check(directory.isValid(), "Cannot create temporary preview sources");
    const QDir root(directory.path());
    check(root.mkdir("camera-a") && root.mkdir("camera-b"), "Cannot create two same-name fixture directories");
    const QString reference = root.filePath("camera-a/frame.png");
    const QString manual = root.filePath("camera-b/frame.png");
    const QString last = root.filePath("latest.png");
    const QString missing = root.filePath("removed.dng");
    const QString damaged = root.filePath("damaged.dng");
    QImage swatch(48, 36, QImage::Format_RGB32);
    swatch.fill(Qt::red);
    check(swatch.save(reference), "Cannot write red reference fixture");
    swatch.fill(Qt::blue);
    check(swatch.save(manual), "Cannot write blue comparison fixture");
    swatch.fill(Qt::green);
    check(swatch.save(last), "Cannot write green selection fixture");
    {
        QFile file(damaged);
        check(file.open(QIODevice::WriteOnly) && file.write("not a RAW") == 9,
              "Cannot make a broken RAW fixture");
    }

    FrameReceipt receipt;
    receipt.sequence = true;
    receipt.deliverables = 2;
    receipt.rows = {{reference, Outcome::Used, true, 2, 18, 0.5, {}},
                    {manual, Outcome::ManualExcluded, false, 0, -1, -1.0, {}},
                    {missing, Outcome::AlignmentFailed, false, 0, 15, -2.5,
                     {"alignment-quality-gate", "rms-above-2px", "p95-above-3.5px",
                      "low-grid-coverage", "low-match-coverage", "private-code"}},
                    {damaged, Outcome::RawLoadFailed, false, 0, -1, -1.0, {}},
                    {{}, Outcome::NotSubmitted, false, 0, -1, -1.0, {}},
                    {missing, Outcome::AlignmentFailed, false, 0, -1, -1.0, {"unknown-code"}},
                    {last, Outcome::Used, false, 1, 12, 0.2, {}}};
    FrameReceiptDialog dialog(receipt);
    dialog.setStyleSheet(StyleTokens::appStyleSheet());
    showWindow(dialog);
    check(dialog.objectName() == "frameReceiptDialog", "Incorrect details window identity");
    auto& table = child<QTableView>(dialog, "receiptFrameTable");
    auto& filter = child<QComboBox>(dialog, "receiptFrameFilter");
    auto& referenceView = child<QLabel>(dialog, "receiptReferencePreview");
    auto& selectedView = child<QLabel>(dialog, "receiptSelectedPreview");
    auto& save = child<QPushButton>(dialog, "receiptSaveDiagnostics");
    usable(save);
    usable(table);
    fits(referenceView);
    fits(selectedView);
    check(referenceView.geometry().right() < selectedView.geometry().left() &&
              selectedView.geometry().bottom() < table.geometry().top() && table.horizontalScrollBar()->maximum() == 0,
          "Both previews and all table columns must fit without overlap at 920x540");

    const auto tr = [](const char* source) { return QCoreApplication::translate("Nebulens", source); };
    auto* model = table.model();
    const auto first = model->index(0, 0);
    const auto second = model->index(1, 0);
    check(first.data(Qt::UserRole).toString() == reference &&
              first.data(Qt::UserRole + 1).toInt() == static_cast<int>(Outcome::Used) &&
              first.data(Qt::UserRole + 2).toBool() && first.data(Qt::UserRole + 3).toInt() == 2 &&
              first.data().toString().contains("#1") && second.data().toString().contains("#2") &&
              second.data(Qt::UserRole).toString() == manual && table.currentIndex().row() == 1,
          "Same-name paths must have distinct original numbers and the first unused frame must be current");
    const QString reason = model->index(2, 2).data(Qt::ToolTipRole).toString();
    check(reason.contains(tr("星点对齐后的偏差过大")) && reason.contains(tr("匹配星点未覆盖足够画面")) &&
              reason.contains(tr("匹配星点太少")) && !reason.contains("private-code") &&
              model->index(5, 2).data().toString() == tr("无法与参考帧对齐"),
          "Known alignment thresholds must be explained without leaking opaque reason codes");

    const auto center = [](QLabel& view) { return render(view).pixel(view.rect().center()); };
    check(waitForReceipt([&] { return center(referenceView) == qRgb(255, 0, 0) &&
                                     center(selectedView) == qRgb(0, 0, 255); }),
          "The two same-name sources must actually render independently");
    table.setCurrentIndex(model->index(2, 0));
    table.setCurrentIndex(model->index(3, 0));
    table.setCurrentIndex(model->index(6, 0));
    check(selectedView.text() == tr("正在读取预览…"), "Old pixels must be replaced immediately on selection");
    check(waitForReceipt([&] { return center(selectedView) == qRgb(0, 255, 0); }) &&
              center(referenceView) == qRgb(255, 0, 0),
          "Rapid selection must show only the latest source and leave the reference unchanged");

    choose(filter, filter.findText(tr("处理跳过")));
    check(model->rowCount() == 3 && model->index(0, 0).data().toString().contains("#3") &&
              model->index(1, 1).data().toString() == tr("处理跳过"),
          "Processing skips must include decode errors and retain input frame numbers");
    check(waitForReceipt([&] { return selectedView.text() == tr("找不到原片文件"); }),
          "A removed file must not appear as a decode failure");
    table.setCurrentIndex(model->index(1, 0));
    check(waitForReceipt([&] { return selectedView.text() == tr("无法解码原片预览"); }),
          "A damaged RAW must not appear missing");
    choose(filter, filter.findText(tr("手动排除")));
    check(model->rowCount() == 1 && table.currentIndex().data(Qt::UserRole).toString() == manual &&
              model->index(0, 1).data().toString() == tr("手动排除"),
          "Manual exclusions must have a distinct filter from processing skips");
    check(!table.isColumnHidden(3) && !table.isColumnHidden(4) && !table.isColumnHidden(5),
          "Measurements and output numbers must stay available even if the current filter has none");
    choose(filter, filter.findText(tr("未确认")));
    check(model->rowCount() == 0 && selectedView.text() == tr("未选择原片") &&
              center(referenceView) == qRgb(255, 0, 0),
          "An empty filter must clear only the selected pane");
    choose(filter, filter.findText(tr("未提交")));
    check(selectedView.text() == tr("未记录原片路径"), "A pathless record needs a distinct placeholder");

    bool correctSaveSuggestion = false;
    const QString canceled = root.filePath("canceled.json");
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QDir suggestedDirectory(QFileInfo(documents).isDir() ? documents : QDir::homePath());
    QTimer::singleShot(0, &dialog, [&] {
        auto* picker = dialog.findChild<QFileDialog*>();
        if (!picker) return;
        correctSaveSuggestion = picker->selectedFiles().value(0) == suggestedDirectory.filePath("frame-receipt.json");
        picker->setDirectory(root);
        picker->selectFile("canceled.json");
        picker->reject();
    });
    save.click();
    check(correctSaveSuggestion && !QFile::exists(canceled) && save.isEnabled(),
          "The save picker needs a neutral filename in Documents or home, and cancellation must not write");

    choose(filter, filter.findText(tr("已采用")));
    dialog.close();

    FrameReceipt unmeasured;
    unmeasured.deliverables = 1;
    unmeasured.rows.append({{}, Outcome::Used, false, 0, -1, -1.0, {}});
    FrameReceiptDialog noMetrics(unmeasured);
    const auto& noMetricsTable = child<QTableView>(noMetrics, "receiptFrameTable");
    check(noMetricsTable.isColumnHidden(3) && noMetricsTable.isColumnHidden(4) && noMetricsTable.isColumnHidden(5),
          "Unmeasured columns and non-sequence numbering should not take space in the table");

    FrameReceiptDialog empty(FrameReceipt{});
    showWindow(empty);
    check(child<QLabel>(empty, "receiptReferencePreview").text() == tr("本次没有参考帧") &&
              !child<QPushButton>(empty, "receiptSaveDiagnostics").isEnabled(),
          "An empty receipt must not manufacture a reference or an exportable file");
}

constexpr std::array kRidgeFiles{"ridge-single.png", "ridge-stack.png", "ridge-separated.png"};
constexpr std::array kRidgeLabels{"ridgeComparisonSingleTitle", "ridgeComparisonStackTitle",
                                  "ridgeComparisonSeparatedTitle"};
constexpr std::array kRidgeChannels{41, 119, 207};

QRgb ridgePixel(int x, int y, int channel) {
    return qRgb(x % 256, y % 256, channel);
}

void writeRidgeCrops(const QDir& directory, const QSize& size) {
    QImage crop(size, QImage::Format_RGB32);
    for (std::size_t i = 0; i < kRidgeFiles.size(); ++i) {
        for (int y = 0; y < size.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(crop.scanLine(y));
            for (int x = 0; x < size.width(); ++x) row[x] = ridgePixel(x, y, kRidgeChannels[i]);
        }
        check(crop.save(directory.filePath(QString::fromLatin1(kRidgeFiles[i]))),
              "Cannot write a reference crop");
    }
}

void expectRidgePixels(QAbstractScrollArea& view, const QSize& crop, bool actualSize) {
    const auto& horizontal = *view.horizontalScrollBar();
    const auto& vertical = *view.verticalScrollBar();
    const int width = horizontal.pageStep();
    const int height = vertical.pageStep();
    check(width > 0 && height > 0 &&
              horizontal.maximum() == (actualSize ? std::max(0, crop.width() - width) : 0) &&
              vertical.maximum() == (actualSize ? std::max(0, crop.height() - height) : 0),
          "Each shared scroll range must match the visible span at the chosen zoom");
    const QImage image = render(*view.viewport());
    const int x = width / 2;
    const int y = height / 2;
    const int top = image.height() - height;
    int lastLeft = -1;
    for (std::size_t i = 0; i < kRidgeLabels.size(); ++i) {
        auto& heading = child<QLabel>(view, kRidgeLabels[i]);
        fits(heading);
        const int left = heading.geometry().left();
        check(left > lastLeft && heading.width() == width &&
                  heading.heightForWidth(heading.width()) <= heading.height(),
              "The three comparison headings must fit above equal-width columns");
        lastLeft = left;
        const QRgb pixel = image.pixel(left + x, top + y);
        if (actualSize) {
            const int sourceX = horizontal.value() + x - std::max(0, (width - crop.width()) / 2);
            const int sourceY = vertical.value() + y - std::max(0, (height - crop.height()) / 2);
            check(pixel == ridgePixel(sourceX, sourceY, kRidgeChannels[i]),
                  "The actual-size panes must paint the same source coordinate without tonal changes");
        } else {
            check(std::abs(qBlue(pixel) - kRidgeChannels[i]) <= 1,
                  "Fit must paint all three distinct crops");
        }
    }
}

void testRidgeViewport(QWidget& parent, const QString& manifest, const QSize& crop) {
    QString error = QStringLiteral("Old error");
    check(RidgeComparisonDialog::openForManifest(manifest, &parent, error) && error.isEmpty(),
          "A complete fixed-file reference bundle must open: " + error);
    auto& dialog = child<QDialog>(parent, "ridgeComparisonDialog");
    showWindow(dialog);
    auto& view = child<QAbstractScrollArea>(dialog, "ridgeComparisonView");
    auto& fit = child<QAbstractButton>(dialog, "ridgeComparisonFit");
    auto& actual = child<QAbstractButton>(dialog, "ridgeComparisonActualSize");
    usable(fit);
    usable(actual);
    check(fit.isChecked() && dialog.isModal(), "The modal comparison must initially show the whole crop");
    expectRidgePixels(view, crop, false);

    actual.click();
    QApplication::processEvents();
    view.horizontalScrollBar()->setValue(41);
    view.verticalScrollBar()->setValue(73);
    expectRidgePixels(view, crop, true);
    dialog.resize(360, 260);
    QApplication::processEvents();
    check(dialog.size() == QSize(360, 260), "The comparison must remain usable in a smaller window");
    usable(fit);
    usable(actual);
    expectRidgePixels(view, crop, true);
    view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->maximum());
    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());
    expectRidgePixels(view, crop, true);
    const QPoint offset(view.horizontalScrollBar()->value(), view.verticalScrollBar()->value());
    fit.click();
    QApplication::processEvents();
    expectRidgePixels(view, crop, false);
    actual.click();
    QApplication::processEvents();
    expectRidgePixels(view, crop, true);
    check(offset == QPoint(view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()),
          "Switching back from Fit must return to the previous shared inspection location");
    const QPointer<QDialog> guard(&dialog);
    dialog.close();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(!guard, "Closing comparison must release its decoded crops");
}

void testRidgeComparison() {
    QTemporaryDir temp;
    check(temp.isValid(), "Cannot create a temporary reference folder");
    const QDir directory(temp.path());
    const QString manifest = directory.filePath(QStringLiteral("comparison.json"));
    QString error;
    check(!RidgeComparisonDialog::openForManifest(QStringLiteral("https://example.invalid/comparison.json"),
                                                 nullptr, error) && !error.isEmpty(),
          "Comparison must not open a remote manifest");
    check(!RidgeComparisonDialog::openForManifest(manifest, nullptr, error) && !error.isEmpty(),
          "Comparison must reject a missing manifest");
    const auto writeManifest = [&](const QByteArray& bytes) {
        QFile anchor(manifest);
        check(anchor.open(QIODevice::WriteOnly) && anchor.write(bytes) == bytes.size(),
              "Cannot write a temporary reference manifest");
    };
    constexpr auto schema = R"({"schema":"nebulens.ridge-comparison","version":1})";
    writeManifest(schema);

    const QString first = directory.filePath(QString::fromLatin1(kRidgeFiles.front()));
    const auto failsAt = [&](const char* name) {
        return !RidgeComparisonDialog::openForManifest(manifest, nullptr, error) &&
               error.contains(QString::fromLatin1(name));
    };
    check(failsAt(kRidgeFiles.front()), "A missing crop must be reported by filename");
    for (const QSize size : {QSize(4097, 1), QSize(2300, 2000)}) {
        QImage oversized(size, QImage::Format_Grayscale8);
        oversized.fill(0);
        check(oversized.save(first) && failsAt(kRidgeFiles.front()) &&
                  error == QCoreApplication::translate("Nebulens", "对照图片尺寸过大：%1")
                               .arg(QString::fromLatin1(kRidgeFiles.front())),
              "Crop width and total pixels must be bounded before decoding");
    }

    constexpr int w = 720;
    constexpr int h = 620;
    const QString second = directory.filePath(QString::fromLatin1(kRidgeFiles[1]));
    const QString third = directory.filePath(QString::fromLatin1(kRidgeFiles[2]));
    QImage crop(w, h, QImage::Format_RGB32);
    crop.fill(Qt::black);
    check(crop.save(first, "BMP") && failsAt(kRidgeFiles.front()),
          "Only PNG data is accepted under a fixed crop filename");
    const auto mismatchAt = [&](const char* name) {
        return failsAt(name) && error == QCoreApplication::translate("Nebulens", "对照图片的裁片尺寸不一致：%1")
                                           .arg(QString::fromLatin1(name));
    };
    check(crop.save(first) && crop.copy(0, 0, w - 1, h).save(second) && mismatchAt(kRidgeFiles[1]),
          "Mismatched width must be reported for the second crop: " + error);
    check(crop.save(second) && failsAt(kRidgeFiles[2]), "A missing third crop must be reported by filename");
    check(crop.copy(0, 0, w, h - 1).save(third) && mismatchAt(kRidgeFiles[2]),
          "Mismatched height must be reported for the third crop: " + error);

    // Invalid manifests must still fail when all three correctly named PNGs are present.
    writeRidgeCrops(directory, QSize(w, h));
    for (const auto* invalid : {"{ invalid JSON", "[]", "{}", R"({"schema":"Other","version":1})",
                                R"({"schema":"nebulens.ridge-comparison"})",
                                R"({"schema":"nebulens.ridge-comparison","version":"1"})",
                                R"({"schema":"nebulens.ridge-comparison","version":2})"}) {
        writeManifest(invalid);
        check(!RidgeComparisonDialog::openForManifest(manifest, nullptr, error) && !error.isEmpty(),
              "A wrong JSON schema/version must not authorize adjacent crops");
    }
    writeManifest(QByteArray(schema).leftJustified(1024 * 1024 + 1, ' '));
    check(!RidgeComparisonDialog::openForManifest(manifest, nullptr, error) && !error.isEmpty(),
          "Comparison must not parse a manifest larger than its read limit");
    writeManifest(schema);

    QWidget parent;
    showWindow(parent);
    testRidgeViewport(parent, manifest, QSize(w, h));
    for (const QSize size : {QSize(640, 384), QSize(95, 71)}) {
        writeRidgeCrops(directory, size);
        testRidgeViewport(parent, manifest, size);
    }
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
        testOrderEntryPoints();
        testFrameSummary();
        testFrameDetails();
        QTranslator english;
        check(english.load(QStringLiteral(":/i18n/nebulens_en.qm")) && app.installTranslator(&english),
              "The test target must link the real English translation resource");
        testWorkflow(true);
        testOrderEntryPoints();
        testFrameSummary();
        testFrameDetails();
        std::cout << "[PASS] Chinese and English workflow, 920x540 layout, and file/sequence receipts\n";
        std::cout << "[PASS] Frame receipt counts, filters, layout, and independent previews\n";
        testRidgeComparison();
        std::cout << "[PASS] Order file actions, reopened orders, and synchronized local comparison crops\n";

        testMotion(moving ? Motion::Moving : reduced ? Motion::Reduced : Motion::Auto);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
