#include "app/MainWindow.h"
#include "core/ImageExporter.h"
#include "core/PreviewToneMapper.h"
#include "core/PrintOrderDocument.h"
#include "ui/ParamsPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/PrintOrderPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/StyleTokens.h"
#include "ui/Toolbar.h"
#include "workers/ExportWorker.h"
#include "workers/FrameReceipt.h"
#include "workers/ProcessingWorker.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QTranslator>
#include <QUrl>
#include <QVariant>

#ifdef Q_OS_WIN
#include <QUuid>
#include <qt_windows.h>
#endif

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

#if defined(Q_OS_MACOS) && defined(QT_SHARED)
// Only this executable owns calls from the desktop object files. Route the
// explicit settings constructor to Qt's file backend: setPath(NativeFormat)
// cannot redirect CFPreferences on macOS.
QT_BEGIN_NAMESPACE
QSettings::QSettings(const QString& organization, const QString& application, QObject* parent)
    : QSettings(IniFormat, UserScope, organization, application, parent) {}
QT_END_NAMESPACE
#endif

namespace {

void check(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}

template <typename T>
T& child(QObject& parent, const char* name = nullptr) {
    auto* found = parent.findChild<T*>(QString::fromLatin1(name));
    check(found, QStringLiteral("Missing %1: %2")
                     .arg(T::staticMetaObject.className(), name));
    return *found;
}

bool waitFor(const std::function<bool()>& ready, int timeoutMs = 30000) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready()) loop.quit();
    });
    poll.start(10);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

class DialogCapture {
public:
    DialogCapture() {
        QObject::connect(&m_poll, &QTimer::timeout, &m_poll, [this] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() == QStringLiteral("frameReceiptDialog")) return;
            auto* fileDialog = qobject_cast<QFileDialog*>(dialog);
            if (!m_savePath.isEmpty() && fileDialog && fileDialog->acceptMode() == QFileDialog::AcceptSave) {
                const QFileInfo target(std::exchange(m_savePath, QString()));
                fileDialog->setDirectory(target.absolutePath());
                fileDialog->selectFile(target.fileName());
                dialog->accept();
                return;
            }
            auto* box = qobject_cast<QMessageBox*>(dialog);
            m_messages.append(box ? box->text() : dialog->windowTitle());
            dialog->reject();
        });
        m_poll.start(10);
    }

    void saveTo(const QString& path) { m_savePath = path; }
    const QStringList& messages() const { return m_messages; }

private:
    QTimer m_poll;
    QString m_savePath;
    QStringList m_messages;
};

class FileUrlCapture : public QObject {
    Q_OBJECT
public:
    FileUrlCapture() { QDesktopServices::setUrlHandler("file", this, "recordUrl"); }
    ~FileUrlCapture() override { QDesktopServices::unsetUrlHandler("file"); }

    QList<QUrl> urls;

private slots:
    void recordUrl(const QUrl& url) { urls.append(url); }
};

#ifdef Q_OS_WIN
// The explicit QSettings(organization, application) constructor uses the
// registry on Windows even when the default format is set to IniFormat.
class TestRegistry {
public:
    void install() {
        m_name = (QStringLiteral("Software\\NebulensWindowTests-") +
                  QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString();
        // QSettings creates non-volatile children, even under a temporary root.
        check(RegCreateKeyExW(HKEY_CURRENT_USER, m_name.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                              &m_key, nullptr) == ERROR_SUCCESS,
              "Cannot create an isolated settings key");
        m_user = RegOverridePredefKey(HKEY_CURRENT_USER, m_key) == ERROR_SUCCESS;
        m_system = RegOverridePredefKey(HKEY_LOCAL_MACHINE, m_key) == ERROR_SUCCESS;
        check(m_user && m_system, "Cannot isolate the native settings registry");
    }

    ~TestRegistry() {
        if (m_system) RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
        if (m_user) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (m_key) {
            RegCloseKey(m_key);
            const auto status = RegDeleteTreeW(HKEY_CURRENT_USER, m_name.c_str());
            if (status != ERROR_SUCCESS) {
                std::cerr << "Cannot delete isolated settings key: " << status << '\n';
            }
        }
    }

private:
    std::wstring m_name;
    HKEY m_key = nullptr;
    bool m_user = false;
    bool m_system = false;
};
#endif

void configureSettings(const QDir& root) {
    const QString home = root.filePath("home");
    check(QFileInfo(home).isDir() &&
              QDir::fromNativeSeparators(qEnvironmentVariable("HOME")) == home,
          "Run this binary through WindowFlowTests.py to isolate settings");
    const QString userSettings = root.filePath("user-settings");
    for (auto format : {QSettings::IniFormat, QSettings::NativeFormat}) {
        QSettings::setPath(format, QSettings::UserScope, userSettings);
        QSettings::setPath(format, QSettings::SystemScope, root.filePath("system-settings"));
    }
#ifndef Q_OS_WIN
    check(QSettings("StarProcessor", "App").fileName().startsWith(userSettings + '/'),
          "The explicit app settings constructor did not select the test directory");
#endif
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCoreApplication::setOrganizationName("StarProcessor");
    QCoreApplication::setApplicationName("StarProcessor");
    QSettings settings;
    settings.setValue("updates/lastCheckUtc", QDateTime::currentDateTimeUtc());
    settings.sync();
    check(settings.status() == QSettings::NoError, "Cannot seed the update check timestamp");
}

void prepareOutput(const QDir& output) {
    check(QDir().mkpath(output.absolutePath()), "Cannot create the test output directory");
    QSettings settings("StarProcessor", "App");
    settings.clear();
    settings.setValue("outputPath", output.absolutePath());
    settings.setValue("outputFormat", "tiff16");
    settings.sync();
    check(settings.status() == QSettings::NoError, "Cannot seed the isolated app settings");
}

void click(QAbstractButton& button) {
    check(button.isVisible() && button.isEnabled(),
          "Control is not usable: " + button.objectName());
    button.click();
}

void choose(QComboBox& combo, const QVariant& data) {
    const int target = combo.findData(data);
    check(target >= 0 && combo.isVisible() && combo.isEnabled(),
          "Choice is not available: " + data.toString());
    while (combo.currentIndex() != target) {
        const int before = combo.currentIndex();
        QKeyEvent key(QEvent::KeyPress, before < target ? Qt::Key_Down : Qt::Key_Up,
                      Qt::NoModifier);
        QApplication::sendEvent(&combo, &key);
        check(combo.currentIndex() != before, "Combo box ignored the selection");
    }
}

QAction& menuAction(MainWindow& window, const std::function<bool(QAction&)>& matches) {
    for (auto* heading : window.menuBar()->actions()) {
        if (auto* menu = heading->menu()) {
            for (auto* action : menu->actions()) {
                if (matches(*action)) return *action;
            }
        }
    }
    throw std::runtime_error("Required MainWindow menu action was not found");
}

QAction& shortcut(MainWindow& window, const char* key) {
    const QKeySequence sequence(QString::fromLatin1(key));
    return menuAction(window, [&](QAction& action) { return action.shortcut() == sequence; });
}

QAction& menuText(MainWindow& window, const char* source) {
    const QString text = QCoreApplication::translate("Nebulens", source);
    return menuAction(window, [&](QAction& action) { return action.text() == text; });
}

FileCard& selectAsset(ProjectPanel& project, const QString& path) {
    for (auto* card : project.findChildren<FileCard*>()) {
        if (!card->isVisible() || !card->accessibleName().startsWith(QFileInfo(path).fileName())) continue;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(6, 6),
                          card->mapToGlobal(QPoint(6, 6)), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(card, &press);
        check(project.currentFilePath() == path, "Filmstrip click did not select the source");
        return *card;
    }
    throw std::runtime_error("Required filmstrip card was not found");
}

QAbstractButton& exportButton(MainWindow& window) {
    auto& toolbar = child<Toolbar>(window, "mainToolbar");
    for (auto* button : toolbar.findChildren<QAbstractButton*>()) {
        if (button->accessibleName() == Toolbar::tr(u8"\u5bfc\u51fa\u7ed3\u679c")) {
            return *button;
        }
    }
    throw std::runtime_error("Required toolbar export button was not found");
}

using Outcome = FrameReceipt::Outcome;

struct ReceiptRow {
    Outcome outcome;
    bool reference;
    int outputNumber;

    bool operator==(const ReceiptRow& other) const {
        return outcome == other.outcome && reference == other.reference && outputNumber == other.outputNumber;
    }
};

using ReceiptRows = QHash<QString, ReceiptRow>;

struct Flow {
    MainWindow window;
    PrintOrderPanel& order = child<PrintOrderPanel>(window, "printOrderPanel");
    ProjectPanel& project = child<ProjectPanel>(window, "projectPanel");
    ParamsPanel& params = child<ParamsPanel>(window, "paramsPanel");
    PreviewPanel& preview = child<PreviewPanel>(window);
    QAbstractButton& next = child<QAbstractButton>(order, "printOrderNext");
    QLineEdit& receipt = child<QLineEdit>(order, "printOrderResultPath");
    DialogCapture dialogs;

    Flow() {
        window.show();
        QCoreApplication::processEvents();
        check(order.isVisible() && !child<Toolbar>(window, "mainToolbar").isVisible() &&
                  child<QWidget>(order, "printOrderProductPage").isVisible(),
              "Startup must show the print order above the advanced workspace");
    }

    void goToOutput() {
        click(next);
        check(child<QWidget>(order, "printOrderAssetsPage").isVisible(),
              "Product step must lead to the real assets step");
        click(next);
        check(child<QWidget>(order, "printOrderOutputPage").isVisible(),
              "Assets step must lead to the real output step");
    }

    QString process(int expectedCount, int timeoutMs = 30000) {
        const auto before = dialogs.messages().size();
        click(next);
        const bool finished = waitFor([&] {
            return params.isEnabled() || dialogs.messages().size() != before;
        }, timeoutMs);
        check(finished && receipt.isVisible() && dialogs.messages().size() == before,
              "Processing did not show a successful receipt. Page: " +
                  child<QStackedWidget>(order, "printOrderPages").currentWidget()->objectName() +
                  ". Receipt: " + receipt.text() + ". Task: " +
                  child<QLabel>(order, "printOrderTaskStatus").toolTip() + ". Status: " +
                  window.statusBar()->currentMessage() + ". Dialogs: " +
                  dialogs.messages().join("; "));
        const auto count = QRegularExpression(QStringLiteral("\\d+")).match(
            child<QLabel>(order, "printOrderResultCount").text());
        check(count.hasMatch() && count.captured().toInt() == expectedCount,
              "Receipt must count the actual number of deliverables");
        const QString path = QDir::fromNativeSeparators(receipt.text());
        check(!path.isEmpty() && QFileInfo(path).exists(),
              "Receipt must point to a deliverable that exists: " + path);
        return path;
    }

    void checkExportEligibility(bool allowed) {
        check(shortcut(window, "Ctrl+E").isEnabled() == allowed,
              allowed ? "Menu must allow a current manual export"
                      : "Menu must not offer a stale or inapplicable export");
        check(exportButton(window).isEnabled() == allowed,
              allowed ? "Toolbar must allow a current manual export"
                      : "Toolbar must not offer a stale or inapplicable export");
    }

    QWidget* visibleFrameReceipt() const {
        for (auto* dialog : window.findChildren<QWidget*>(QStringLiteral("frameReceiptDialog"))) {
            if (dialog->isVisible()) return dialog;
        }
        return nullptr;
    }

    QWidget& openFrameReceipt() {
        const auto& summary = child<QLabel>(order, "printOrderFrameSummary");
        check(summary.isVisible() && !summary.text().trimmed().isEmpty(),
              "A formal delivery must show a frame summary");
        click(child<QAbstractButton>(order, "printOrderFrameDetails"));
        auto* dialog = visibleFrameReceipt();
        check(dialog && dialog->isWindow(), "Frame details must open in a separate window");
        return *dialog;
    }

    void checkNoFrameReceipt() {
        check(order.isVisible() &&
                  !child<QWidget>(order, "printOrderFrameSummary").isVisible() &&
                  !child<QWidget>(order, "printOrderFrameDetails").isVisible() && !visibleFrameReceipt(),
              "An order without a current delivery must not show frame details");
    }
};

ReceiptRows readReceiptRows(QWidget& dialog) {
    const auto& table = child<QAbstractItemView>(dialog, "receiptFrameTable");
    const auto* model = table.model();
    check(table.isVisible() && model && model->rowCount() > 0, "Frame details must show the receipt rows");
    ReceiptRows rows;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        const QString source = index.data(Qt::UserRole).toString();
        bool outcomeOk = false, outputOk = false;
        const int outcome = index.data(Qt::UserRole + 1).toInt(&outcomeOk);
        const auto reference = index.data(Qt::UserRole + 2);
        const int output = index.data(Qt::UserRole + 3).toInt(&outputOk);
        check(QDir::isAbsolutePath(source) && !rows.contains(source) && outcomeOk &&
                  reference.typeId() == QMetaType::Bool && outputOk,
              QString("Missing or duplicate frame identity, outcome, reference or output number at row %1")
                  .arg(row + 1));
        rows.insert(source, {static_cast<Outcome>(outcome), reference.toBool(), output});
    }
    return rows;
}

ReceiptRows inspectReceiptRows(Flow& flow) {
    auto& dialog = flow.openFrameReceipt();
    const auto rows = readReceiptRows(dialog);
    check(dialog.close(), "Frame details must be dismissible");
    return rows;
}

void selectReceiptRow(QWidget& dialog, const QString& source) {
    auto& table = child<QAbstractItemView>(dialog, "receiptFrameTable");
    const auto* model = table.model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data(Qt::UserRole).toString() != source) continue;
        table.scrollTo(index);
        const QPoint pos = table.visualRect(index).center();
        auto* viewport = table.viewport();
        check(viewport->rect().contains(pos), "Frame details must make the source row reachable");
        for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            const auto down = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
            QMouseEvent mouse(type, QPointF(pos), viewport->mapToGlobal(pos), Qt::LeftButton,
                              down, Qt::NoModifier);
            QApplication::sendEvent(viewport, &mouse);
        }
        check(table.currentIndex().siblingAtColumn(0) == index, "Click must select the receipt row");
        return;
    }
    throw std::runtime_error("Source was not found in the frame receipt");
}

void checkReveal(Flow& flow, const QString& receiptPath, const QString& expectedDirectory) {
    FileUrlCapture urls;
    QStringList requests;
    QObject::connect(&flow.order, &PrintOrderPanel::revealRequested, &urls,
                     [&](const QString& path) { requests.append(path); });
    click(child<QAbstractButton>(flow.order, "printOrderReveal"));
    check(requests == QStringList{receiptPath}, "Reveal must use the path on this receipt");
    check(urls.urls == QList<QUrl>{QUrl::fromLocalFile(expectedDirectory)},
          "MainWindow must reveal the actual delivery directory: " + expectedDirectory);
}

QStringList files(const QDir& directory) {
    return directory.entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

QByteArray bytes(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path);
    return file.readAll();
}

QJsonObject readJson(const QString& path) {
    const auto document = QJsonDocument::fromJson(bytes(path));
    check(document.isObject(), "Expected a JSON object");
    return document.object();
}

QString freshArtifactPath(const QString& path) {
    const QFileInfo target(path);
    check(!target.exists() && !target.isSymLink() && QDir(target.absolutePath()).exists(),
          "Artifacts need a fresh local filename in an existing directory");
    return target.absoluteFilePath();
}

QJsonObject saveReceiptDiagnostics(Flow& flow, QWidget& dialog, const QString& path) {
    const QString target = freshArtifactPath(path);
    const auto messagesBefore = flow.dialogs.messages().size();
    flow.dialogs.saveTo(target);
    click(child<QAbstractButton>(dialog, "receiptSaveDiagnostics"));
    check(waitFor([&] {
        return QFileInfo(target).isFile() || flow.dialogs.messages().size() != messagesBefore;
    }, 3000) && QFileInfo(target).isFile(), "Save diagnostics must write to the chosen JSON file");
    return readJson(target);
}

void testEnglishPreferences(const QDir& root) {
    prepareOutput(QDir(root.filePath("preferences-output")));
    MainWindow window;
    QTranslator english;
    check(english.load(":/i18n/nebulens_en.qm"), "Missing bundled English translation");
    qApp->installTranslator(&english);
    bool fits = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>("paperPreferences");
        if (!dialog) return;
        auto* combo = dialog->findChild<QComboBox*>();
        auto* edit = dialog->findChild<QLineEdit*>();
        int checked = 0;
        for (auto* label : dialog->findChildren<QLabel*>())
            if (label->buddy() && label->width() >= label->fontMetrics().horizontalAdvance(label->text()))
                ++checked;
        fits = checked == 2 && combo && edit && combo->x() == edit->x();
        dialog->reject();
    });
    const bool invoked = QMetaObject::invokeMethod(&window, "onSettingsClicked", Qt::DirectConnection);
    qApp->removeTranslator(&english);
    check(invoked && fits, "Translated preference labels must fit and both fields must align");
}

void testOneSheet(const QDir& root) {
    prepareOutput(QDir(root.filePath("one-sheet-output")));
    Flow flow;
    auto* masthead = flow.order.masthead();
    check(flow.window.centralWidget() == &flow.order && masthead->isVisible(),
          "One paper surface and one permanent masthead must own the entire order");
    const QString raw = root.filePath("source.dng");
    flow.window.importFiles({raw});
    check(waitFor([&] { return !flow.order.proofImage().isNull() &&
                              flow.order.proofCaption().contains(QFileInfo(raw).fileName()); }),
          "Import must replace stock imagery with the selected source proof");
    const QImage input = flow.order.proofImage();
    click(child<QAbstractButton>(flow.order, "printOrderProductStarTrail"));
    check(flow.order.proofImage() != input && flow.project.allFilePaths() == QStringList{raw},
          "Changing the product should identify its example without substituting another RAW");
    click(child<QAbstractButton>(flow.order, "printOrderSampleToggle"));
    check(flow.order.proofImage() == input, "The current RAW must be reachable from the product example");
    click(child<QAbstractButton>(flow.order, "printOrderProductStill"));
    flow.goToOutput();
    check(flow.order.proofImage() == input, "The delivery page must not present a demo as this order's result");
    click(child<QAbstractButton>(flow.order, "printOrderQuickToggle"));
    auto& ev = child<QDoubleSpinBox>(flow.order, "printOrderQuickExposure");
    ev.setValue(1.2);
    check(flow.params.basicAdjustmentOptions().exposureTenths == 12 &&
          flow.order.proofImage() != input && flow.order.proofCaption().contains("EV") &&
          flow.receipt.text().isEmpty() && !shortcut(flow.window, "Ctrl+E").isEnabled(),
          "Inline exposure must change real parameters and a labeled preview, never an exportable result");
    auto& appendix = child<QAbstractButton>(flow.order, "printOrderAdvanced");
    click(appendix);
    check(flow.order.showingDetailPage() && masthead == flow.order.masthead() && masthead->isVisible() &&
          flow.order.isAncestorOf(&flow.params) && flow.params.isVisible(),
          "Detailed adjustments must be a leaf of this same visible sheet");
    auto& image = child<QLabel>(flow.preview, "previewImageLabel");
    auto& draftLabel = child<QLabel>(flow.preview, "previewResultState");
    check(waitFor([&]{ return draftLabel.isVisible() && draftLabel.text().contains("EV") &&
                              !image.pixmap().isNull(); }), "The adjustment canvas must identify the source exposure proof");
    const QImage positive = image.pixmap().toImage();
    flow.params.setExposureTenths(-3);
    check(waitFor([&]{ return image.pixmap().toImage() != positive; }) &&
          flow.preview.currentImage() == input && !flow.preview.isShowingResult(),
          "The visible canvas must react to exposure without modifying the source or becoming a result");
    menuText(flow.window, u8"\u51b2\u5370\u5355").trigger();
    check(!flow.order.showingDetailPage() && child<QWidget>(flow.order,"printOrderOutputPage").isVisible() &&
          qAbs(ev.value()+0.3) < 0.001 && flow.project.currentFilePath() == raw &&
          flow.order.proofImage() != input && flow.order.proofCaption().contains("EV"),
          "Return must preserve the step, actual editing values, selection and draft");
    ev.setValue(0);
    check(flow.order.proofImage() == input && flow.receipt.text().isEmpty(),
          "Resetting exposure must restore the identical camera preview without writing a file");
    const QString damaged = root.filePath("unreadable-source.dng");
    QFile invalid(damaged);
    check(invalid.open(QIODevice::WriteOnly) && invalid.write("not a RAW") == 9, "Cannot create broken RAW fixture");
    invalid.close();
    flow.window.importFiles({damaged});
    flow.window.selectStartupWorkflowStage(WorkflowStage::Assets);
    selectAsset(flow.project, damaged);
    menuText(flow.window, u8"\u51b2\u5370\u5355").trigger();
    check(waitFor([&] {
        return flow.order.proofImage().isNull() && flow.order.proofCaption() ==
            QCoreApplication::translate("Nebulens", "原片预览不可用");
    }) && flow.receipt.text().isEmpty(), "A broken RAW must not borrow the previous photo or a sample proof");
}

void testExplicitSceneAndReference(const QDir& root) {
    prepareOutput(QDir(root.filePath("explicit-output")));
    Flow flow;
    const QString first = root.filePath("source.dng");
    const QString second = root.filePath("frame2.dng");
    flow.window.selectStartupScene(ProcessingScene::StarTrail);
    flow.window.importFiles({first});
    check(flow.project.currentScene() == ProcessingScene::StarTrail &&
              child<QAbstractButton>(flow.order, "printOrderProductStarTrail").isChecked(),
          "A one-RAW import must not override an explicit star-trail scene");
    flow.goToOutput();
    check(!flow.next.isEnabled(), "One RAW must not be enough to submit a star trail");

    flow.window.selectStartupScene(ProcessingScene::Nightscape);
    flow.window.importFiles({second});
    flow.window.selectStartupWorkflowStage(WorkflowStage::Alignment);
    auto& combo = child<QComboBox>(flow.params, "referenceFrameCombo");
    flow.project.setReferenceFrame(second);
    check(combo.currentData().toString() == second && flow.params.selectedReferenceFrame() == second,
          "A filmstrip reference choice must update the inspector");
    choose(combo, first);
    check(flow.project.referenceFramePath() == first,
          "An inspector reference choice must update the filmstrip");
    choose(combo, QString());
    check(flow.project.referenceFramePath().isEmpty() && flow.params.selectedReferenceFrame().isEmpty(),
          "Automatic reference must clear the manual choice in both panels");
}

void checkQuickPreview(Flow& flow, const QDir& output) {
    QSlider* exposure = nullptr;
    for (auto* candidate : flow.params.findChildren<QSlider*>()) {
        if (candidate->toolTip().contains(QStringLiteral(" EV"))) {
            exposure = candidate;
            break;
        }
    }
    check(exposure && exposure->isVisible() && exposure->isEnabled(),
          "The advanced workspace must expose the finishing exposure control");
    const int previousExposure = exposure->value();
    const auto formalKey = flow.preview.currentContentKey();
    const auto formalImage = flow.preview.currentImage();
    const auto formalProof = flow.order.proofImage();
    auto& proof = child<QAbstractButton>(flow.order, "printOrderProof");
    auto& showOrder = menuText(flow.window, u8"\u51b2\u5370\u5355");
    auto& showAdvanced = menuText(flow.window, u8"\u51b2\u5370\u53c2\u6570");
    const auto before = files(output);
    auto& comparison = shortcut(flow.window, "Shift+B");
    check(comparison.isEnabled(), "The formal result must offer before/after comparison");
    comparison.setChecked(true);
    flow.preview.setZoom(3.0);
    auto& canvas = child<QScrollArea>(flow.preview);
    auto* horizontal = canvas.horizontalScrollBar();
    auto* vertical = canvas.verticalScrollBar();
    horizontal->setValue(97);
    vertical->setValue(63);
    const int x = horizontal->value();
    const int y = vertical->value();
    check(x > 0 && y > 0 && qFuzzyCompare(flow.preview.zoom(), 3.0),
          "Manual zoom must give the test a scrollable view");
    const auto sameView = [&] {
        return flow.preview.currentContentKey() == formalKey &&
               qFuzzyCompare(flow.preview.zoom(), 3.0) && comparison.isChecked() &&
               horizontal->value() == x && vertical->value() == y;
    };
    exposure->setValue(previousExposure + 20);
    showOrder.trigger();
    check(flow.order.isVisible() && flow.receipt.text().isEmpty() &&
              flow.order.proofImage() == formalProof &&
              proof.accessibleDescription().contains(u8"\u4e0a\u6b21\u5904\u7406\u7684\u6210\u7247"),
          "Returning before the debounce must label the previous proof and remove its current receipt");
    click(proof);
    auto& largeProof = child<QDialog>(flow.window, "neutralProofDialog");
    check(largeProof.isVisible() &&
              largeProof.windowTitle().contains(u8"\u4e0a\u6b21\u5904\u7406\u7684\u6210\u7247"),
          "An enlarged previous proof must keep its previous-result label");
    largeProof.reject();
    showAdvanced.trigger();
    check(waitFor([&] {
        const auto image = flow.preview.currentImage();
        return flow.preview.isShowingResult() && !image.isNull() && image != formalImage;
    }), "Finishing changes should refresh the real quick-preview canvas");
    check(waitFor(sameView), "Quick preview must preserve zoom, pan, and split comparison");
    const QImage quickImage = flow.preview.currentImage();
    flow.checkExportEligibility(false);
    check(files(output) == before && flow.receipt.text().isEmpty(),
          "Quick preview must neither publish nor retain a current formal receipt");
    showOrder.trigger();
    const QImage quickProof = flow.order.proofImage();
    check(!quickProof.isNull() && quickProof != formalProof &&
              proof.accessibleDescription().contains(u8"\u5feb\u901f\u9884\u89c8"),
          "A current quick result must update and label the order proof");
    flow.checkNoFrameReceipt();
    showAdvanced.trigger();

    exposure->setValue(previousExposure);
    const auto formalRestored = [&] {
        return flow.preview.currentImage() == formalImage && sameView();
    };
    check(waitFor(formalRestored),
          "Reverting the parameter should restore the current formal result");
    check(flow.order.proofImage() == formalProof &&
              proof.accessibleDescription().trimmed() == u8"\u5904\u7406\u7ed3\u679c",
          "Reverting to the formal settings must restore the saved proof and its label");
    flow.checkExportEligibility(true);

    exposure->setValue(previousExposure + 20);
    check(waitFor([&] { return flow.preview.currentImage() == quickImage && sameView(); }) &&
              flow.order.proofImage() == quickProof,
          "A cache hit must restore the same quick proof and darkroom result");
    exposure->setValue(previousExposure);
    check(waitFor(formalRestored) && flow.order.proofImage() == formalProof,
          "Restoring the formal settings again must choose the formal image");
    flow.checkExportEligibility(true);
}

void testSingleAndClear(const QDir& root) {
    const QDir output(root.filePath("single-output"));
    prepareOutput(output);
    Flow flow;
    const QString source = root.filePath("source.dng");
    flow.window.importFiles({source});
    check(flow.project.currentScene() == ProcessingScene::SingleFrame &&
              flow.project.includedFilePaths() == QStringList{source},
          "One RAW on a default order must automatically select single-frame finishing");
    check(waitFor([&] { return !flow.preview.currentImage().isNull(); }),
          "The original RAW must finish loading before it can be replaced");
    const QImage originalRawPreview = flow.preview.currentImage();

    flow.goToOutput();
    auto& format = child<QComboBox>(flow.order, "printOrderFormat");
    choose(format, QStringLiteral("png8"));
    check(flow.params.outputFormat() == QStringLiteral("png8"),
          "Print order format must update the real processing settings");
    const QString path = flow.process(1);
    const QFileInfo formal(path);
    const QImage formalImage(path);
    check(formal.absolutePath() == output.absolutePath() && formal.suffix() == "png" &&
              formalImage.size() == QSize(512, 384) &&
              files(output) == QStringList{formal.fileName()},
          "Single-frame delivery must contain one full-size PNG in the chosen directory");
    check(flow.order.proofImage() == formalImage.convertToFormat(QImage::Format_RGB888),
          "The formal proof must match the delivered PNG, not the darkroom tone map");
    const auto frameRows = inspectReceiptRows(flow);
    check(frameRows == ReceiptRows{{source, {Outcome::Used, true, 0}}},
          "The only RAW must be the frame used by the single delivery and its actual reference");
    checkReveal(flow, path, output.absolutePath());

    menuText(flow.window, u8"\u51b2\u5370\u53c2\u6570").trigger();
    check(flow.order.showingDetailPage() && flow.order.masthead()->isVisible() && flow.params.isEnabled() && flow.receipt.text() == path,
          "The Advanced menu must resume editing the delivered project");
    flow.window.selectStartupWorkflowStage(WorkflowStage::Adjustment);
    check(exportButton(flow.window).isVisible(), "Advanced toolbar must be reachable from the same window");
    flow.checkExportEligibility(true);
    checkQuickPreview(flow, output);

    auto& exportAction = shortcut(flow.window, "Ctrl+E");
    auto& exportTool = exportButton(flow.window);
    exportAction.trigger();
    const bool exportedCurrent = waitFor([&] {
        return files(output).size() == 2 && flow.params.isEnabled();
    });
    check(exportedCurrent,
          "The real export menu did not publish a second file: " +
              flow.dialogs.messages().join("; "));
    flow.checkExportEligibility(true);
    const QStringList deliveries = files(output);
    const QString exported = deliveries.at(deliveries.first() == formal.fileName() ? 1 : 0);
    check(QImage(output.filePath(exported)) == formalImage &&
              QDir::fromNativeSeparators(flow.receipt.text()) == output.filePath(exported),
          "Manual export must publish the current full-resolution result and its own receipt");
    menuText(flow.window, u8"\u51b2\u5370\u5355").trigger();
    check(inspectReceiptRows(flow) == frameRows,
          "Re-export must keep the current formal delivery's original frame rows");
    menuText(flow.window, u8"\u51b2\u5370\u53c2\u6570").trigger();
    const auto formalBytes = bytes(path);
    const auto exportBytes = bytes(output.filePath(exported));

    auto& clear = menuAction(flow.window, [](QAction& action) {
        return action.text() == QCoreApplication::translate("Nebulens", u8"\u6e05\u7a7a\u9879\u76ee");
    });
    check(clear.isEnabled(), "Clear-project menu must be available after delivery");
    clear.trigger();
    check(flow.project.filePaths().isEmpty() && flow.receipt.text().isEmpty() &&
              flow.preview.currentImage().isNull(),
          "Clearing the project must remove the source, receipt, and result canvas");
    flow.checkNoFrameReceipt();
    flow.checkExportEligibility(false);

    const QFileInfo replacement(root.filePath("replacement.dng"));
    check(QFileInfo(source).size() == replacement.size() &&
              QFileInfo(source).lastModified() == replacement.lastModified() &&
              bytes(source) != bytes(replacement.filePath()),
          "Replacement fixture must match path metadata but contain different RAW data");
    check(QFile::remove(source) && QFile::rename(replacement.filePath(), source),
          "Cannot replace the fixture at its original path");
    flow.window.importFiles({source});
    flow.goToOutput();
    flow.checkNoFrameReceipt();
    flow.window.selectStartupWorkflowStage(WorkflowStage::Adjustment);
    check(waitFor([&] { return !flow.preview.currentImage().isNull(); }) &&
              flow.preview.currentImage() != originalRawPreview &&
              flow.project.currentScene() == ProcessingScene::SingleFrame,
          "Reimport must show the replacement RAW as a new single-frame project");
    flow.checkExportEligibility(false);
    check(exportTool.isVisible(), "Stale toolbar eligibility must be checked in the advanced workspace");
    exportAction.trigger();
    exportTool.click();
    // Also exercise the guard shared by both controls, even when Qt blocks disabled actions.
    check(QMetaObject::invokeMethod(&flow.window, "onExportClicked", Qt::DirectConnection),
          "MainWindow must expose the real export slot");
    check(flow.params.isEnabled() && files(output) == deliveries &&
              bytes(path) == formalBytes && bytes(output.filePath(exported)) == exportBytes,
          "No stale export may start or overwrite either existing delivery after replacement");
    const QString replacementKey = flow.preview.currentContentKey();
    shortcut(flow.window, "R").trigger();
    check(flow.preview.currentContentKey() == replacementKey && !flow.preview.isShowingResult(),
          "Returning to a result after clear must not revive the previous cache");
}

void testSingleFrameReceiptFromMultipleRaws(const QDir& root) {
    const QDir output(root.filePath("multi-raw-receipt-output"));
    prepareOutput(output);
    Flow flow;
    const QString unsubmitted = root.filePath("frame2.dng");
    const QString selected = root.filePath("frame10.dng");
    const QString excluded = root.filePath("late.dng");
    flow.window.importFiles({unsubmitted, selected, excluded});
    flow.window.selectStartupWorkflowStage(WorkflowStage::Alignment);
    auto& excludedCard = selectAsset(flow.project, excluded);
    excludedCard.setFocus(Qt::MouseFocusReason);
    QKeyEvent exclude(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&excludedCard, &exclude);
    check(flow.project.excludedFilePaths() == QStringList{excluded},
          "The filmstrip shortcut must manually exclude exactly one RAW");
    choose(child<QComboBox>(flow.params, "referenceFrameCombo"), unsubmitted);
    choose(child<QComboBox>(flow.window, "toolbarSceneCombo"),
           static_cast<int>(ProcessingScene::SingleFrame));
    selectAsset(flow.project, selected);
    auto& showOrder = menuText(flow.window, u8"\u51b2\u5370\u5355");
    showOrder.trigger();
    flow.goToOutput();
    flow.checkNoFrameReceipt();
    const QString delivery = flow.process(1);
    const QStringList deliveries{QFileInfo(delivery).fileName()};
    check(files(output) == deliveries, "A formal delivery must not automatically write a diagnostics report");
    const auto contentKey = flow.preview.currentContentKey();
    const auto checkEditor = [&] {
        check(flow.project.currentFilePath() == selected && flow.project.referenceFramePath() == unsubmitted &&
                  flow.params.selectedReferenceFrame() == unsubmitted &&
                  flow.project.excludedFilePaths() == QStringList{excluded} &&
                  flow.preview.isShowingResult() && flow.preview.currentContentKey() == contentKey &&
                  flow.receipt.text() == QDir::toNativeSeparators(delivery),
              "Viewing the receipt must not change the editor's source, reference, result or exclusion");
        flow.checkExportEligibility(true);
    };
    checkEditor();
    auto& dialog = flow.openFrameReceipt();
    const auto rows = readReceiptRows(dialog);
    check(rows == ReceiptRows{{selected, {Outcome::Used, true, 0}},
                              {unsubmitted, {Outcome::NotSubmitted, false, 0}},
                              {excluded, {Outcome::ManualExcluded, false, 0}}},
          "Single-frame details must mark the real reference and distinguish unused from manually excluded RAWs");
    selectReceiptRow(dialog, unsubmitted);
    checkEditor();
    selectReceiptRow(dialog, excluded);
    const auto report = saveReceiptDiagnostics(flow, dialog, root.filePath("anonymous-frame-receipt.json"));
    const auto anonymousRows = report.value("rows").toArray();
    // An allowlist of values catches leaks even when the original path or text is JSON-escaped.
    check(anonymousRows.size() == 3 &&
              report == QJsonObject{{"schemaVersion", 1}, {"sequence", false}, {"deliverables", 1},
                                    {"submittedCount", 1}, {"usedCount", 1}, {"skippedCount", 0},
                                    {"manualCount", 1}, {"rows", anonymousRows}},
          "Saved report must contain only the anonymous schema and correct counts");
    QSet<QString> outcomes{"used", "not-submitted", "manual-excluded"};
    for (int row = 0; row < anonymousRows.size(); ++row) {
        const auto entry = anonymousRows.at(row).toObject();
        const QString outcome = entry.value("outcome").toString();
        check(outcomes.remove(outcome) &&
                  entry == QJsonObject{{"frameNumber", row + 1}, {"outcome", outcome},
                                       {"reference", outcome == "used"}},
              "Saved rows must contain only consecutive anonymous numbers, known outcomes and reference flags");
    }
    check(dialog.close() && files(output) == deliveries,
          "Saving diagnostics elsewhere must not modify the delivered files");
    checkEditor();

    menuText(flow.window, u8"\u51b2\u5370\u53c2\u6570").trigger();
    shortcut(flow.window, "Ctrl+E").trigger();
    check(waitFor([&] { return files(output).size() == 2 && flow.params.isEnabled(); }),
          "The formal result must remain exportable after viewing frame details");
    showOrder.trigger();
    check(inspectReceiptRows(flow) == rows,
          "A re-export must retain the same used, unsubmitted and manually excluded frames");
}

void testQueuedDeliverySnapshots(const QDir& root) {
    for (const bool manual : {false, true}) {
        const QDir output(root.filePath(manual ? "queued-export-output" : "queued-formal-output"));
        prepareOutput(output);
        Flow flow;
        const QImage sample = flow.order.proofImage();
        auto& showOrder = menuText(flow.window, u8"\u51b2\u5370\u5355");
        auto& showAdvanced = menuText(flow.window, u8"\u51b2\u5370\u53c2\u6570");
        showAdvanced.trigger();
        choose(child<QComboBox>(flow.window, "toolbarSceneCombo"),
               static_cast<int>(ProcessingScene::SingleFrame));
        const QString first = root.filePath("frame2.dng");
        const QString second = root.filePath("late.dng");
        flow.window.importFiles({first, second});
        check(flow.project.currentScene() == ProcessingScene::SingleFrame &&
                  waitFor([&] { return !flow.preview.currentImage().isNull(); }),
              "The single-frame setup must expose two browsable sources");
        showOrder.trigger();
        flow.goToOutput();
        const QString previousPath = flow.process(1);
        const QImage previousProof = flow.order.proofImage();
        check(inspectReceiptRows(flow).value(first).reference,
              "The previous delivery must describe the original source");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        showAdvanced.trigger();

        // Wait for actual disk publication without dispatching Qt's queued finished signal.
        QString savedPath;
        if (manual) {
            shortcut(flow.window, "Ctrl+E").trigger();
            auto& worker = child<ExportWorker>(flow.window);
            check(worker.wait(30000) && worker.succeeded(), "Manual export did not write a file");
            savedPath = worker.outputPath();
        } else {
            shortcut(flow.window, "Ctrl+Return").trigger();
            auto& worker = child<ProcessingWorker>(flow.window);
            check(worker.wait(30000) && worker.errorString().isEmpty() &&
                      worker.selectedReferenceFrame() == first,
                  "Formal processing did not finish with the captured source");
            savedPath = worker.outputFile();
        }
        check(!flow.params.isEnabled() && flow.receipt.text().isEmpty() &&
                  savedPath != previousPath && QFileInfo::exists(savedPath),
              "The completion callback must remain queued after the file is saved");
        const QByteArray savedBytes = bytes(savedPath);
        menuText(flow.window, u8"\u6e05\u7a7a\u9879\u76ee").trigger();
        check(flow.project.filePaths() == QStringList{first, second},
              "Reset must remain blocked until the queued delivery has been consumed");

        selectAsset(flow.project, second);
        QCoreApplication::sendPostedEvents(&flow.window, QEvent::MetaCall);
        showOrder.trigger();
        check(flow.params.isEnabled() && !flow.receipt.isVisible() && flow.receipt.text().isEmpty() &&
                  flow.next.isEnabled() && child<QWidget>(flow.order, "printOrderOutputPage").isVisible() &&
                  !child<QProgressBar>(flow.order, "printOrderTaskProgress").isVisible(),
              "The stale delivery must unlock a retryable order without publishing a receipt");
        flow.checkNoFrameReceipt();
        check(waitFor([&] { return !flow.preview.currentImage().isNull(); }) &&
                  !flow.preview.isShowingResult() && flow.order.proofImage() == flow.preview.currentImage() &&
                  flow.order.proofImage() != previousProof,
              "The stale callback must not replace the newly selected RAW or its proof");
        flow.checkExportEligibility(false);

        showAdvanced.trigger();
        selectAsset(flow.project, first);
        flow.checkExportEligibility(false); // Even the original signature cannot revive rejected pixels.
        selectAsset(flow.project, second);
        showOrder.trigger();
        const QString recoveredPath = flow.process(1);
        check(recoveredPath != savedPath && QFileInfo::exists(previousPath) && bytes(savedPath) == savedBytes,
              "A new valid delivery must leave both earlier files untouched");
        const auto rows = inspectReceiptRows(flow);
        check(rows == ReceiptRows{{second, {Outcome::Used, true, 0}},
                                  {first, {Outcome::NotSubmitted, false, 0}}},
              "A valid retry must describe the newly delivered source, not the stale worker's source");

        click(child<QAbstractButton>(flow.order, "printOrderNewOrder"));
        check(flow.project.filePaths().isEmpty() && flow.preview.currentImage().isNull() &&
                  flow.order.proofImage() == sample && flow.receipt.text().isEmpty() && files(output).size() == 3,
              "A new order must remove session images and receipts without removing files");
        flow.checkNoFrameReceipt();
    }
}

void testNightscapeGroundDetail(const QDir& root) {
    const QDir output(root.filePath("ground-detail-output"));
    prepareOutput(output);
    Flow flow;
    flow.window.importFiles({root.filePath("ridge0.dng"), root.filePath("ridge1.dng")});
    flow.goToOutput();
    flow.window.selectStartupWorkflowStage(WorkflowStage::Stacking);
    check(flow.project.currentScene() == ProcessingScene::Nightscape &&
              !flow.params.skyGroundSeparationEnabled(),
          "Start in Milky Way mode without automatic sky/ground separation");
    auto* stackGroup = child<QComboBox>(flow.params, "skyGroundModeCombo").parentWidget();
    QCheckBox* separation = nullptr;
    for (auto* candidate : stackGroup->findChildren<QCheckBox*>()) {
        if (candidate->text() == QCoreApplication::translate("Nebulens", u8"\u5929\u5730\u5206\u79bb")) {
            separation = candidate;
            break;
        }
    }
    check(separation, "Missing the sky/ground toggle in the stacking inspector");
    click(*separation);
    check(flow.params.skyGroundSeparationEnabled() &&
              flow.project.currentScene() == ProcessingScene::Nightscape,
          "The manual toggle must enable separation without switching scenes");
    QSlider* detail = nullptr;
    for (auto* candidate : stackGroup->findChildren<QSlider*>()) {
        if (candidate->minimum() == 0 && candidate->maximum() == 70) detail = candidate;
    }
    check(detail && detail->isVisible() && detail->isEnabled(),
          "Manual separation must expose the ground detail slider");
    auto& returnToOrder = child<QAbstractButton>(flow.window, "returnToPrintOrder");
    click(returnToOrder);
    flow.process(1); // Seed the bounded RGB and mask cache at the default strength.

    const auto runAtStrength = [&](Qt::Key key, int strength) {
        flow.window.selectStartupWorkflowStage(WorkflowStage::Stacking);
        const QImage previous = flow.preview.currentImage();
        const auto upstream = flow.params.upstreamSignature();
        const auto finishing = flow.params.finishingSignature();
        const auto before = files(output);
        check(detail->isVisible() && detail->isEnabled(), "Ground detail slider became unusable");
        QKeyEvent change(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(detail, &change);
        check(flow.params.skyGroundSeparationEnabled() &&
                  flow.params.groundDetailStrength() == strength &&
                  flow.params.upstreamSignature() == upstream &&
                  flow.params.finishingSignature() != finishing,
              "Ground detail must update finishing only");
        check(waitFor([&] {
            const auto image = flow.preview.currentImage();
            return flow.preview.isShowingResult() && !image.isNull() && image != previous;
        }), QString("No ground detail quick preview at %1%. Status: %2")
                .arg(strength).arg(flow.window.statusBar()->currentMessage()));
        const QImage quick = flow.preview.currentImage();
        flow.checkExportEligibility(false);
        check(files(output) == before && flow.receipt.text().isEmpty(),
              "Changing ground detail in quick preview must not deliver a file or receipt");

        click(returnToOrder);
        const QImage quickProof = flow.order.proofImage();
        const QString path = flow.process(1);
        int width = 0, height = 0;
        std::vector<uint16_t> rgb;
        check(ImageExporter::loadTiffRgb16(path, rgb, width, height) &&
                  QSize(width, height) == quick.size() && files(output).size() == before.size() + 1,
              "Formal ground detail must deliver a new, readable full-size TIFF");
        // This fixture is smaller than the preview cap, so compare at the same
        // geometry and fixed display range, using the actual delivered pixels.
        const auto mapped = PreviewToneMapper::mapRgb16WithRange(rgb, width, height, 0, 65535);
        const QImage formal(mapped.rgb.data(), width, height, width * 3, QImage::Format_RGB888);
        check(quick == formal, QString("Quick preview and delivered TIFF differ at %1%").arg(strength));
        check(!quickProof.isNull() && quickProof == flow.order.proofImage() &&
                  quickProof == ImageExporter::makeRgb16Proof(rgb, width, height),
              QString("Quick proof and delivered TIFF differ at %1%").arg(strength));
        return quick;
    };
    const QImage low = runAtStrength(Qt::Key_Home, 0);
    const QImage high = runAtStrength(Qt::Key_End, 70);
    const auto groundContrast = [](const QImage& image) {
        int contrast = 0;
        for (int y = image.height() * 5 / 6; y < image.height() * 9 / 10; ++y) {
            for (int x = image.width() / 8; x < image.width() * 7 / 8; ++x) {
                contrast += std::abs(qGray(image.pixel(x + 8, y)) - qGray(image.pixel(x, y)));
            }
        }
        return contrast;
    };
    const int lowContrast = groundContrast(low);
    const int highContrast = groundContrast(high);
    check(highContrast > lowContrast,
          QString("Higher ground detail must increase foreground contrast: %1 -> %2")
              .arg(lowContrast).arg(highContrast));
    const QRect sky(0, 0, low.width(), low.height() / 2);
    check(low.copy(sky) == high.copy(sky), "Ground detail must leave the upper sky unchanged");
    std::cout << "R06 foreground contrast (0% -> 70%): "
              << lowContrast << " -> " << highContrast << '\n';
}

void testCancelOrder(const QDir& root) {
    const QDir output(root.filePath("cancel-output"));
    prepareOutput(output);
    Flow flow;
    click(child<QAbstractButton>(flow.order, "printOrderProductTimelapse"));
    flow.window.importFiles({root.filePath("frame2.dng"), root.filePath("frame10.dng"),
                             root.filePath("late.dng")});
    flow.goToOutput();
    auto& progress = child<QProgressBar>(flow.order, "printOrderTaskProgress");
    const QDir priorDelivery(flow.process(3));
    const auto deliveredFiles = files(priorDelivery);
    check(inspectReceiptRows(flow).size() == 3, "Cancellation must start with a prior frame receipt");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    shortcut(flow.window, "Ctrl+Return").trigger();
    flow.checkNoFrameReceipt();
    auto& worker = child<ProcessingWorker>(flow.window);
    int cancelledAt = 0;
    QObject progressReceiver;
    QObject::connect(&worker, &ProcessingWorker::progress, &progressReceiver, [&](int value) {
        // Wait for actual RAW decode progress, not just the initial busy state.
        if (cancelledAt > 0 || value <= 5 || value >= 30 || flow.params.isEnabled() ||
                !worker.isRunning() || !progress.isVisible() || progress.value() != value ||
                !flow.next.isVisible() || !flow.next.isEnabled()) return;
        flow.next.click();
        if (!flow.next.isEnabled()) cancelledAt = value;
    }, Qt::QueuedConnection);
    const bool finished = waitFor([&] { return flow.params.isEnabled(); });
    check(finished && cancelledAt > 0, "The print order did not cancel a running RAW task");
    const auto& status = child<QLabel>(flow.order, "printOrderTaskStatus");
    check(status.isVisible() &&
              status.text() == QCoreApplication::translate("Nebulens",
                  u8"\u5904\u7406\u5df2\u53d6\u6d88\uff0c\u53ef\u8c03\u6574\u53c2\u6570\u540e\u91cd\u8bd5") &&
              child<QWidget>(flow.order, "printOrderOutputPage").isVisible() && flow.next.isEnabled() &&
              !progress.isVisible() && flow.dialogs.messages().isEmpty(),
          "The cancelled order must return to a retryable output page. Status: " + status.text());
    check(flow.receipt.text().isEmpty() && !flow.receipt.isVisible() &&
              !child<QAbstractButton>(flow.order, "printOrderReveal").isVisible() &&
              output.entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                               QDir::NoDotAndDotDot) == QStringList{priorDelivery.dirName()} &&
              files(priorDelivery) == deliveredFiles,
          "Cancellation must not publish a receipt or leave any new file or sequence directory");
    flow.checkNoFrameReceipt();
    flow.checkExportEligibility(false);
    std::cout << "Print order cancelled at " << cancelledAt << "%\n";
}

void testSequence(const QDir& root) {
    const QDir output(root.filePath("sequence-output"));
    prepareOutput(output);
    Flow flow;
    click(child<QAbstractButton>(flow.order, "printOrderProductTimelapse"));
    flow.window.importFiles({root.filePath("late.dng"), root.filePath("frame10.dng"),
                             root.filePath("frame2.dng")});
    check(flow.project.currentScene() == ProcessingScene::Timelapse,
          "The real product selection must reach MainWindow");
    flow.goToOutput();
    const QString receiptPath = flow.process(3);
    const QFileInfo receipt(receiptPath);
    check(receipt.isDir() && receipt.absolutePath() == output.absolutePath(),
          "Sequence receipt must identify its own run directory");
    const QDir sequence(receiptPath);
    const QStringList expected{"0001_frame2_denoised.tiff", "0002_frame10_denoised.tiff",
                               "0003_late_denoised.tiff"};
    check(files(sequence) == expected,
          "Sequence files must follow capture time, with natural filename tie-breaking");
    const auto rows = inspectReceiptRows(flow);
    const QStringList orderedSources{"frame2.dng", "frame10.dng", "late.dng"};
    check(rows.size() == orderedSources.size(), "Sequence receipt must account for all three frames");
    for (int index = 0; index < orderedSources.size(); ++index) {
        const auto row = rows.value(root.filePath(orderedSources.at(index)));
        check(row.outcome == Outcome::Used && row.outputNumber == index + 1,
              "Receipt output numbers must follow the worker's on-disk sequence, not the import order");
    }
    for (const auto& name : expected) {
        std::vector<uint16_t> rgb;
        int width = 0;
        int height = 0;
        check(ImageExporter::loadTiffRgb16(sequence.filePath(name), rgb, width, height) &&
                  width == 512 && height == 384 && rgb.size() == 512 * 384 * 3,
              "Sequence must contain readable full-resolution TIFFs: " + name);
    }
    checkReveal(flow, receiptPath, receiptPath);
    click(child<QAbstractButton>(flow.order, "printOrderNewOrder"));
    check(flow.project.filePaths().isEmpty() && flow.receipt.text().isEmpty() &&
              child<QWidget>(flow.order, "printOrderProductPage").isVisible() &&
              files(sequence) == expected,
          "Starting a new order must reset the receipt without deleting the delivered sequence");
    flow.checkNoFrameReceipt();
}

struct ExampleFrames {
    QSet<QString> selected;
    QSet<QString> used;
    QString reference;
};

ExampleFrames readExampleFrames(const QString& orderPath) {
    PrintOrderDocument order;
    QString error;
    check(PrintOrderDocument::load(orderPath, order, error), error);
    check(!order.comparisonManifest.isEmpty(), "The example order must link to its comparison manifest");
    const auto manifest = readJson(order.comparisonManifest);
    const auto selected = manifest.value("selectedLights").toArray();
    const auto used = manifest.value("lights").toArray();
    check(!selected.isEmpty() && selected.size() == order.lights.size() && !used.isEmpty(),
          "The comparison manifest must list the submitted and adopted lights");

    const auto relative = [](const QJsonValue& value) {
        const QString path = value.toString();
        check(!path.isEmpty() && path != "." && path != ".." && !path.startsWith("../") &&
                  QDir::isRelativePath(path) && QDir::cleanPath(path) == path,
              "Manifest light paths must be relative to the Runner input root");
        return path;
    };
    // The manifest and order list the same slice under different roots. Bind
    // the whole relative path so same-name RAWs in different folders stay distinct.
    const QString firstRelative = relative(selected.first());
    const QString firstLocal = order.lights.first();
    check(firstLocal.endsWith('/' + firstRelative), "Cannot bind the manifest to the order's local RAW root");
    const QDir rawRoot(firstLocal.left(firstLocal.size() - firstRelative.size()));
    ExampleFrames expected;
    for (int i = 0; i < selected.size(); ++i) {
        const QString path = rawRoot.filePath(relative(selected.at(i)));
        check(path == order.lights.at(i) && !expected.selected.contains(path),
              "Manifest selectedLights must match the full order slice");
        expected.selected.insert(path);
    }
    for (const auto& value : used) {
        const QString path = rawRoot.filePath(relative(value));
        check(expected.selected.contains(path) && !expected.used.contains(path),
              "Manifest lights must be a unique subset of selectedLights");
        expected.used.insert(path);
    }
    expected.reference = rawRoot.filePath(relative(manifest.value("reference")));
    check(expected.used.contains(expected.reference), "Manifest reference must be an adopted light");
    return expected;
}

void replayLocalExample(const QDir& root, const QString& order, const QString& baseline) {
    const auto expectedFrames = readExampleFrames(order);
    const QDir out(root.filePath("replay-output"));
    prepareOutput(out);
    Flow flow;
    QString error;
    check(flow.window.openPrintOrderFrom(order, error), error);
    check(waitFor([&] { return !flow.order.proofImage().isNull(); }),
          "The reopened real RAW must supply a source proof");
    click(child<QAbstractButton>(flow.order, "printOrderRidgeComparison"));
    auto* comparison = flow.window.findChild<QDialog*>(QStringLiteral("ridgeComparisonDialog"));
    check(comparison && comparison->isVisible(), "The generated manifest must open the three-panel viewer");
    const QString snapshot = qEnvironmentVariable("NEBULENS_TEST_RIDGE_SCREENSHOT");
    if (!snapshot.isEmpty()) check(comparison->grab().save(snapshot), "Cannot capture the comparison window");
    comparison->reject();
    flow.params.setOutputPath(out.absolutePath());
    check(child<QWidget>(flow.order, "printOrderAssetsPage").isVisible(), "Reopen must land on assets");
    click(flow.next);
    const QString actual = flow.process(1, 600000);
    int w = 0, h = 0, expectedW = 0, expectedH = 0;
    {
        std::vector<uint16_t> actualPixels, expectedPixels;
        check(ImageExporter::loadTiffRgb16(actual, actualPixels, w, h) &&
              ImageExporter::loadTiffRgb16(baseline, expectedPixels, expectedW, expectedH),
              "Cannot compare formal TIFF pixels");
        check(w == expectedW && h == expectedH && actualPixels == expectedPixels,
              QStringLiteral("Reopened GUI pixels differ from the original TIFF (%1x%2 vs %3x%4)")
                  .arg(w).arg(h).arg(expectedW).arg(expectedH));
    }
    auto& details = flow.openFrameReceipt();
    const auto rows = readReceiptRows(details);
    check(rows.size() == expectedFrames.selected.size(), "Receipt must cover manifest selectedLights");
    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        const auto& row = it.value();
        check(expectedFrames.selected.contains(it.key()) &&
                  (row.outcome == Outcome::Used) == expectedFrames.used.contains(it.key()) &&
                  row.outcome != Outcome::ManualExcluded && row.outcome != Outcome::NotSubmitted &&
                  row.outcome != Outcome::Unconfirmed,
              "Receipt must distinguish adopted lights from those the worker did not adopt");
        check(row.reference == (it.key() == expectedFrames.reference),
              "Receipt reference must match the actual reference in the comparison manifest");
    }
    const QString frameSnapshot = qEnvironmentVariable("NEBULENS_TEST_FRAME_RECEIPT_SCREENSHOT");
    const auto& referencePreview = child<QLabel>(details, "receiptReferencePreview");
    const auto& selectedPreview = child<QLabel>(details, "receiptSelectedPreview");
    const auto ready = [](const QLabel& preview) {
        return preview.isVisible() && preview.text().isEmpty();
    };
    const bool previewsReady = waitFor([&] { return ready(referencePreview) && ready(selectedPreview); });
    check(previewsReady, QStringLiteral("Frame receipt previews did not load. Reference: %1; selected: %2")
              .arg(ready(referencePreview) ? QStringLiteral("ready") : referencePreview.text(),
                   ready(selectedPreview) ? QStringLiteral("ready") : selectedPreview.text()));
    if (!frameSnapshot.isEmpty()) {
        check(details.grab().save(freshArtifactPath(frameSnapshot), "PNG"), "Cannot capture the frame receipt window");
    }
    const QString diagnostics = qEnvironmentVariable("NEBULENS_TEST_FRAME_RECEIPT_JSON");
    if (!diagnostics.isEmpty()) saveReceiptDiagnostics(flow, details, diagnostics);
    check(details.close(), "Frame details must be dismissible");
    std::cout << "Local example: " << flow.project.totalFileCount() << " RAW inputs, "
              << expectedFrames.used.size() << " adopted and "
              << expectedFrames.selected.size() - expectedFrames.used.size() << " not adopted, "
              << w << 'x' << h << " RGB16 pixels identical after GUI replay\n";
}

} // namespace

void testOrderAcrossLaunches(const QDir& root, bool write);

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setStyleSheet(StyleTokens::appStyleSheet());
    try {
        const auto arguments = app.arguments();
        const bool replay = arguments.size() == 5 && arguments[2] == "--replay-example";
        check(arguments.size() == 2 || arguments.size() == 3 || replay,
              "Usage: NebulensWindowTests <fixture-directory> [--order-write|--order-read]");
        const QDir root(arguments.at(1));
#ifdef Q_OS_WIN
        TestRegistry registry;
        registry.install();
#endif
        configureSettings(root);
        if (replay) {
            replayLocalExample(root, arguments[3], arguments[4]);
            return 0;
        }
        if (arguments.size() == 3) {
            check(arguments[2] == "--order-write" || arguments[2] == "--order-read", "Invalid phase");
            testOrderAcrossLaunches(root, arguments[2] == "--order-write");
            return 0;
        }
        int failures = 0;
        for (const auto& test : {
                 std::make_pair("One sheet from product choice to exposure and back", testOneSheet),
                 std::make_pair("English preferences label columns", testEnglishPreferences),
                 std::make_pair("Explicit scene and reference selection", testExplicitSceneAndReference),
                 std::make_pair("Single-frame delivery, quick preview, and clear", testSingleAndClear),
                 std::make_pair("Single-frame receipt of multiple RAWs and private diagnostics",
                                testSingleFrameReceiptFromMultipleRaws),
                 std::make_pair("Queued formal and manual delivery snapshots", testQueuedDeliverySnapshots),
                 std::make_pair("Milky Way manual separation and ground detail", testNightscapeGroundDetail),
                 std::make_pair("Cancel a running print order without a receipt", testCancelOrder),
                 std::make_pair("Timelapse delivery and reveal", testSequence),
             }) {
            try {
                test.second(root);
                std::cout << "PASS: " << test.first << '\n';
            } catch (const std::exception& error) {
                std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
                ++failures;
            }
        }
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

#include "MainWindowFlowTests.moc"
