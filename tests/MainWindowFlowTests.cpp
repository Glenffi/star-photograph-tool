#include "app/MainWindow.h"
#include "core/ImageExporter.h"
#include "core/PreviewToneMapper.h"
#include "ui/ParamsPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/PrintOrderPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/StyleTokens.h"
#include "ui/Toolbar.h"
#include "workers/ExportWorker.h"
#include "workers/ProcessingWorker.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
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

bool waitFor(const std::function<bool()>& ready) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready()) loop.quit();
    });
    poll.start(10);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

class DialogCapture {
public:
    DialogCapture() {
        QObject::connect(&m_poll, &QTimer::timeout, &m_poll, [this] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* box = qobject_cast<QMessageBox*>(dialog);
            m_messages.append(box ? box->text() : dialog->windowTitle());
            dialog->reject();
        });
        m_poll.start(10);
    }

    const QStringList& messages() const { return m_messages; }

private:
    QTimer m_poll;
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

void selectAsset(ProjectPanel& project, const QString& path) {
    for (auto* card : project.findChildren<FileCard*>()) {
        if (!card->isVisible() || !card->accessibleName().startsWith(QFileInfo(path).fileName())) continue;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(6, 6),
                          card->mapToGlobal(QPoint(6, 6)), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(card, &press);
        check(project.currentFilePath() == path, "Filmstrip click did not select the source");
        return;
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

    QString process(int expectedCount) {
        const auto before = dialogs.messages().size();
        click(next);
        const bool finished = waitFor([&] {
            return params.isEnabled() || dialogs.messages().size() != before;
        });
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
};

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
    auto& showAdvanced = menuText(flow.window, u8"\u9ad8\u7ea7\u6697\u623f");
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
    checkReveal(flow, path, output.absolutePath());

    menuText(flow.window, u8"\u9ad8\u7ea7\u6697\u623f").trigger();
    check(!flow.order.isVisible() && flow.params.isEnabled() && flow.receipt.text() == path,
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
    flow.checkExportEligibility(false);

    const QFileInfo replacement(root.filePath("replacement.dng"));
    check(QFileInfo(source).size() == replacement.size() &&
              QFileInfo(source).lastModified() == replacement.lastModified() &&
              bytes(source) != bytes(replacement.filePath()),
          "Replacement fixture must match path metadata but contain different RAW data");
    check(QFile::remove(source) && QFile::rename(replacement.filePath(), source),
          "Cannot replace the fixture at its original path");
    flow.window.importFiles({source});
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

void testQueuedDeliverySnapshots(const QDir& root) {
    for (const bool manual : {false, true}) {
        const QDir output(root.filePath(manual ? "queued-export-output" : "queued-formal-output"));
        prepareOutput(output);
        Flow flow;
        const QImage sample = flow.order.proofImage();
        auto& showOrder = menuText(flow.window, u8"\u51b2\u5370\u5355");
        auto& showAdvanced = menuText(flow.window, u8"\u9ad8\u7ea7\u6697\u623f");
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

        click(child<QAbstractButton>(flow.order, "printOrderNewOrder"));
        check(flow.project.filePaths().isEmpty() && flow.preview.currentImage().isNull() &&
                  flow.order.proofImage() == sample && flow.receipt.text().isEmpty() && files(output).size() == 3,
              "A new order must remove session images and receipts without removing files");
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
    click(flow.next);
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
                               QDir::NoDotAndDotDot).isEmpty(),
          "Cancellation before delivery must not leave a receipt, file, or sequence directory");
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
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setStyleSheet(StyleTokens::appStyleSheet());
    try {
        const auto arguments = app.arguments();
        check(arguments.size() == 2, "Usage: NebulensWindowTests <fixture-directory>");
        const QDir root(arguments.at(1));
#ifdef Q_OS_WIN
        TestRegistry registry;
        registry.install();
#endif
        configureSettings(root);
        int failures = 0;
        for (const auto& test : {
                 std::make_pair("Explicit scene and reference selection", testExplicitSceneAndReference),
                 std::make_pair("Single-frame delivery, quick preview, and clear", testSingleAndClear),
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
