#include "core/PrintOrderDocument.h"
#include "ui/InspectorWidgets.h"
#include "ui/ParamsPanel.h"
#include "ui/StyleTokens.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QLabel>
#include <QGroupBox>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

template <typename T>
T* control(ParamsPanel& panel, const char* name) {
    auto* widget = panel.findChild<T*>(QString::fromLatin1(name));
    check(widget, name);
    return widget;
}

QString fixtureFile(const QTemporaryDir& directory, const QString& name) {
    QFile file(directory.filePath(name));
    check(file.open(QIODevice::WriteOnly), "Calibration fixture should be writable");
    return file.fileName();
}

void chooseFiles(QPushButton* button, const QStringList& paths) {
    check(button->isVisible() && button->isEnabled(), "Import action should be usable");
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&paths]() {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        check(dialog, "Pressing the import button should open a file chooser");
        if (paths.size() == 1) {
            dialog->selectFile(paths.first());
        } else {
            auto* fileName = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            check(fileName, "File chooser should provide a filename box");
            fileName->setText(QLatin1Char('"') + paths.join(QStringLiteral("\" \"")) +
                              QLatin1Char('"'));
        }
        static_cast<QDialog*>(dialog)->accept();
        check(dialog->result() == QDialog::Accepted, "Fixture selection should be accepted");
    });
    timer.start(0);
    button->click();
    check(!timer.isActive(), "Import action should run the file chooser");
}

void pressKey(QWidget* widget, Qt::Key key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QApplication::sendEvent(widget, &release);
}

void checkInlineExposure(ParamsPanel& panel) {
    auto* slider = control<QSlider>(panel, "advancedExposureSlider");
    auto* value = control<QLabel>(panel, "advancedExposureValue");
    panel.setExposureTenths(100);
    check(slider->value() == 50 && value->text() == QStringLiteral("+5.0 EV") &&
              panel.basicAdjustmentOptions().exposureTenths == 50,
          "Inline exposure must update the advanced slider and clamp to +5.0 EV");
    panel.setExposureTenths(-100);
    check(slider->value() == -50 && value->text() == QStringLiteral("-5.0 EV"),
          "Inline exposure must clamp to -5.0 EV");

    int notifications = 0;
    const auto connection = QObject::connect(&panel, &ParamsPanel::paramsChanged,
        &panel, [&notifications]() { ++notifications; });
    panel.setExposureTenths(12);
    pressKey(slider, Qt::Key_Right);
    QEventLoop eventLoop;
    QTimer::singleShot(StyleTokens::Motion::kPreviewDebounceMs + 200,
                       &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    check(notifications == 1 && panel.basicAdjustmentOptions().exposureTenths == 13 &&
              value->text() == QStringLiteral("+1.3 EV"),
          "The inline API and keyboard edits should share the debounced parameter notification");
    QObject::disconnect(connection);
}

void checkReferenceSelection(ParamsPanel& panel) {
    panel.showWorkflowStage(WorkflowStage::Alignment);
    auto* combo = control<QComboBox>(panel, "referenceFrameCombo");
    const QString light = QStringLiteral("/tmp/light.dng");
    QStringList choices;
    const auto connection = QObject::connect(
        &panel, &ParamsPanel::referenceChoiceChanged, &panel,
        [&choices](const QString& choice) { choices.append(choice); });

    panel.updateRefFrameList({light});
    panel.setSelectedReferenceFrame(light);
    check(panel.selectedReferenceFrame() == light && combo->currentIndex() == 1 &&
              choices.isEmpty(),
          "Synchronizing a manual reference must not emit a user choice");
    panel.setSelectedReferenceFrame({});
    check(panel.selectedReferenceFrame().isEmpty() && combo->currentIndex() == 0 &&
              choices.isEmpty(),
          "Synchronizing automatic reference must clear the selected frame silently");
    pressKey(combo, Qt::Key_Down);
    check(choices == QStringList{light} && panel.selectedReferenceFrame() == light,
          "Changing the reference in the combo should notify with the full path");
    pressKey(combo, Qt::Key_Down);
    check(choices.size() == 1, "Reselecting the current reference should not notify");
    pressKey(combo, Qt::Key_Up);
    check(choices == QStringList{light, QString()} && panel.selectedReferenceFrame().isEmpty(),
          "Choosing automatic in the combo should notify with an empty path");
    panel.setSelectedReferenceFrame(light);
    panel.updateRefFrameList({QStringLiteral("/tmp/another-light.dng")});
    check(choices.size() == 2 && panel.selectedReferenceFrame().isEmpty(),
          "Removing a combo candidate must fall back to automatic silently");
    QObject::disconnect(connection);
}

void checkCalibrationAndProjectClear(ParamsPanel& panel, const QTemporaryDir& directory) {
    panel.applySceneProfile(ProcessingScene::DeepSky, false);
    panel.showCalibrationSettings();
    auto* group = control<QGroupBox>(panel, "calibrationGroup");
    auto* biasRow = control<QWidget>(panel, "calibrationBiasRow");
    auto* darkFlatRow = control<QWidget>(panel, "calibrationDarkFlatRow");
    auto* status = control<QLabel>(panel, "calibrationStatus");
    auto* details = control<QLabel>(panel, "calibrationDetails");
    auto* saveMasters = control<CompactToggle>(panel, "saveGeneratedMasters");
    const QString missing = panel.calibrationHint();
    check(status->text() == missing && missing.contains(QString::fromUtf8("暗场")) &&
              missing.contains(QString::fromUtf8("平场")) &&
              !biasRow->isVisible() && !darkFlatRow->isVisible() && !details->isVisible(),
          "An empty calibration should start with Dark and Flat, with details collapsed");
    control<QToolButton>(panel, "calibrationDetailsButton")->click();
    check(details->isVisible() && !biasRow->isVisible(),
          "Calibration details should be accessible without exposing unrelated sources");
    control<QToolButton>(panel, "calibrationDetailsButton")->click();
    group->setChecked(false);
    panel.showCalibrationSettings();
    check(group->isChecked() && !biasRow->isVisible() && !details->isVisible(),
          "Opening calibration from a shortcut should expand only applicable rows");

    const auto rawGroup = [&directory](const QString& role) {
        QStringList paths;
        for (int i = 1; i <= 3; ++i) {
            paths.append(fixtureFile(directory, QStringLiteral("%1-%2.dng").arg(role).arg(i)));
        }
        return paths;
    };
    const QStringList darks = rawGroup(QStringLiteral("dark"));
    const QStringList flats = rawGroup(QStringLiteral("flat"));
    const QStringList darkFlats = rawGroup(QStringLiteral("dark-flat"));
    const QString darkMaster = fixtureFile(directory, QStringLiteral("dark.spmaster"));
    const QString flatMaster = fixtureFile(directory, QStringLiteral("flat.spmaster"));
    const QString biasMaster = fixtureFile(directory, QStringLiteral("bias.spmaster"));

    chooseFiles(control<QPushButton>(panel, "calibrationDarkRaw"), darks.mid(0, 1));
    check(panel.calibrationHint().contains(QString::fromUtf8("2 张 RAW")),
          "An incomplete RAW group should report the remaining count");
    chooseFiles(control<QPushButton>(panel, "calibrationDarkRaw"), darks.mid(1));
    check(panel.darkFramePaths().size() == 3, "Importing additional RAWs should extend the group");
    chooseFiles(control<QPushButton>(panel, "calibrationFlatRaw"), flats);
    check(!panel.deepSkyCalibrationInputsComplete() && biasRow->isVisible() &&
              darkFlatRow->isVisible() && panel.calibrationHint().contains(QString::fromUtf8("同曝光")),
          "RAW Flat should expose the choice of Bias or exposure-matched Dark Flat");

    chooseFiles(control<QPushButton>(panel, "calibrationBiasMaster"), {biasMaster});
    check(panel.deepSkyCalibrationInputsComplete(), "RAW Flat should accept a Master Bias source");
    chooseFiles(control<QPushButton>(panel, "calibrationDarkFlatRaw"), darkFlats);
    check(!panel.deepSkyCalibrationInputsComplete() &&
              panel.calibrationHint().contains(QString::fromUtf8("移除一组")),
          "RAW Dark must not use two offset sources for RAW Flat");
    chooseFiles(control<QPushButton>(panel, "calibrationDarkMaster"), {darkMaster});
    check(panel.darkFramePaths().isEmpty() && !panel.masterDarkPath().isEmpty() &&
              panel.deepSkyCalibrationInputsComplete(),
          "Master replaces same-role RAW and defers its offset metadata to preflight");

    chooseFiles(control<QPushButton>(panel, "calibrationFlatMaster"), {flatMaster});
    check(panel.flatFramePaths().isEmpty() && !panel.masterFlatPath().isEmpty() &&
              !panel.deepSkyCalibrationInputsComplete() && darkFlatRow->isVisible() &&
              panel.calibrationHint().contains(QString::fromUtf8("移除暗平场")),
          "An already calibrated Master Flat must not use Dark Flat again");
    control<QPushButton>(panel, "calibrationDarkFlatClear")->click();
    check(!darkFlatRow->isVisible() && panel.deepSkyCalibrationInputsComplete(),
          "Removing the unused Dark Flat should make the Master combination ready");
    chooseFiles(control<QPushButton>(panel, "calibrationFlatRaw"), flats);
    check(panel.masterFlatPath().isEmpty() && panel.flatFramePaths().size() == 3,
          "RAW replaces the Master when switching back to a RAW source");
    chooseFiles(control<QPushButton>(panel, "calibrationDarkFlatRaw"), darkFlats);
    saveMasters->click();
    group->setChecked(false);
    check(panel.saveGeneratedMasters(), "Collapsing calibration must preserve the save checkbox");

    panel.showWorkflowStage(WorkflowStage::Stacking);
    control<QComboBox>(panel, "skyGroundModeCombo")->setCurrentIndex(1);
    chooseFiles(control<QPushButton>(panel, "importMaskButton"),
                {fixtureFile(directory, QStringLiteral("project-mask.png"))});
    panel.setModifiedCameraGrayPoint(0.2, 0.8);
    const QString light = QStringLiteral("/tmp/selected-light.dng");
    panel.updateRefFrameList({light});
    panel.setSelectedReferenceFrame(light);
    panel.setProcessingReport(QStringLiteral("Old project report"));
    check(!panel.userMaskPath().isEmpty() && panel.hasModifiedCameraGrayPoint() &&
              panel.skyGroundMode() == SkyGroundMask::UserMask,
          "Project fixture should hold a mask and manual gray point before clearing");

    const QString priorSignature = panel.processingSignature();
    const QString output = directory.filePath(QStringLiteral("output"));
    panel.setOutputFormat(QStringLiteral("png8"));
    panel.setOutputFormat(QStringLiteral("invalid"));
    panel.setOutputPath(output);
    check(panel.outputFormat() == QStringLiteral("png8") &&
              panel.processingSignature() == priorSignature,
          "Output format should round-trip without changing the pixel signature");

    int changes = 0;
    int referenceChanges = 0;
    const auto paramsConnection = QObject::connect(
        &panel, &ParamsPanel::paramsChanged, &panel, [&] { ++changes; });
    const auto referenceConnection = QObject::connect(
        &panel, &ParamsPanel::referenceChoiceChanged, &panel, [&] { ++referenceChanges; });
    panel.clearProjectSources();
    panel.showCalibrationSettings();
    check(changes == 1 && referenceChanges == 0 && panel.calibrationHint() == missing &&
              status->text() == missing && !panel.deepSkyCalibrationInputsComplete() &&
              !saveMasters->isChecked() && !saveMasters->isVisible() &&
              !control<QPushButton>(panel, "calibrationDarkClear")->isEnabled(),
          "Clear should publish one complete reset and restore the initial calibration UI");
    check(panel.darkFramePaths().isEmpty() && panel.flatFramePaths().isEmpty() &&
              panel.biasFramePaths().isEmpty() && panel.darkFlatFramePaths().isEmpty() &&
              panel.masterDarkPath().isEmpty() && panel.masterFlatPath().isEmpty() &&
              panel.masterBiasPath().isEmpty() && panel.masterDarkFlatPath().isEmpty(),
          "Clear must remove all calibration RAW and Master sources");
    check(panel.userMaskPath().isEmpty() && panel.skyGroundMode() == SkyGroundMask::AutoDetect &&
              !panel.hasModifiedCameraGrayPoint() && panel.selectedReferenceFrame().isEmpty() &&
              control<QComboBox>(panel, "referenceFrameCombo")->count() == 1,
          "Clear must remove the project's mask, gray point, and reference choices");
    check(panel.skyGroundSeparationEnabled() && panel.modifiedCameraColorEnabled() &&
              panel.noiseReductionStrength() == 47 &&
              panel.basicAdjustmentOptions().exposureTenths == 13 &&
              panel.outputFormat() == QStringLiteral("png8") && panel.outputPath() == output,
          "Clear must preserve general processing adjustments and output preferences");
    QObject::disconnect(paramsConnection);
    QObject::disconnect(referenceConnection);
}

void checkOrderRoundTrip(const QTemporaryDir& directory) {
    PrintOrderDocument source;
    source.scene = QStringLiteral("deep-sky");
    source.reference = directory.filePath(QStringLiteral("light2.dng"));
    source.selected = directory.filePath(QStringLiteral("light1.dng"));
    source.lights = {source.reference, source.selected};
    source.excluded = {source.selected};
    source.flat = {directory.filePath(QStringLiteral("flat-3.dng")),
                   directory.filePath(QStringLiteral("flat-1.dng")),
                   directory.filePath(QStringLiteral("flat-2.dng"))};
    source.masterDark = directory.filePath(QStringLiteral("dark.spmaster"));
    source.masterBias = directory.filePath(QStringLiteral("bias.spmaster"));
    source.masterDarkFlat = directory.filePath(QStringLiteral("dark-flat.spmaster"));
    source.userMask = directory.filePath(QStringLiteral("order-mask.png"));
    source.outputDirectory = directory.filePath(QStringLiteral("order-output"));
    source.params = {
        {"stackMethod", "kappa-sigma"}, {"kappa", 3.7},
        {"autoReject", false}, {"photometric", false},
        {"dewarpEnabled", true}, {"dewarpStrength", 52},
        {"noiseReductionEnabled", false}, {"noiseReductionStrength", 42},
        {"modifiedCameraColorEnabled", true}, {"modifiedCameraColorStrength", 73},
        {"modifiedCameraColorMode", "manual"},
        {"modifiedCameraGrayPointX", 0.23}, {"modifiedCameraGrayPointY", 0.71},
        {"stretchEnabled", false},
        {"temperature", 12}, {"tint", -6}, {"exposureTenths", -23}, {"contrast", 17},
        {"highlights", -31}, {"shadows", 30}, {"whites", 22}, {"blacks", -3},
        {"vibrance", 25}, {"saturation", -14}, {"sharpening", 26},
        {"starDefringeEnabled", true}, {"starDefringeStrength", 64},
        {"starReduceEnabled", true}, {"starReduceStrength", 45},
        {"outputFormat", "png8"}, {"skyGround", true}, {"skyGroundMode", "user-mask"},
        {"feather", 34}, {"groundMethod", "reference"}, {"groundDetail", 19},
        {"timelapseWindow", 3}, {"timelapseStrength", 52},
        {"timelapseMotionProtection", 23}, {"timelapseProtectGround", false},
        {"starTrailCometStrength", 31}, {"starTrailReverse", true},
        {"starTrailProtectGround", false}, {"saveGeneratedMasters", true}
    };

    QString error;
    check(ParamsPanel::validateOrderSettings(source, error), "All supported order values should validate");
    ParamsPanel original;
    original.applySceneProfile(ProcessingScene::DeepSky);
    original.updateRefFrameList({source.reference});
    original.restoreOrderSettings(source);
    PrintOrderDocument saved;
    saved.scene = source.scene;
    saved.lights = source.lights;
    saved.excluded = source.excluded;
    saved.reference = source.reference;
    saved.selected = source.selected;
    original.captureOrderSettings(saved);
    check(saved.params == source.params && saved.flat == source.flat &&
              saved.masterDark == source.masterDark && saved.masterBias == source.masterBias &&
              saved.masterDarkFlat == source.masterDarkFlat && saved.userMask == source.userMask &&
              saved.outputDirectory == source.outputDirectory,
          "Capture must encode every control and use only dedicated fields for paths");

    const auto path = directory.filePath(QStringLiteral("params-roundtrip.nebprint"));
    check(saved.save(path, error), "Captured parameters should save to an order file");
    PrintOrderDocument loaded;
    check(PrintOrderDocument::load(path, loaded, error), "Captured parameters should reopen from disk");

    ParamsPanel reopened;
    reopened.applySceneProfile(ProcessingScene::DeepSky);
    reopened.updateRefFrameList({source.reference});
    QSettings settings("StarProcessor", "App");
    const auto readPreferences = [&settings] {
        settings.sync();
        QVariantMap snapshot;
        for (const QString& key : settings.allKeys()) snapshot.insert(key, settings.value(key));
        return snapshot;
    };
    const auto preferences = readPreferences();
    int changes = 0;
    int referenceChanges = 0;
    int grayPointRequests = 0;
    QObject::connect(&reopened, &ParamsPanel::paramsChanged, &reopened,
                     [&] { ++changes; });
    QObject::connect(&reopened, &ParamsPanel::referenceChoiceChanged, &reopened,
                     [&] { ++referenceChanges; });
    QObject::connect(&reopened, &ParamsPanel::modifiedCameraGrayPointRequested, &reopened,
                     [&] { ++grayPointRequests; });
    reopened.restoreOrderSettings(loaded);
    reopened.recommendStackMethod(25);
    reopened.saveCurrentSettings();
    check(reopened.processingSignature() == original.processingSignature() &&
              reopened.outputFormat() == source.params.value("outputFormat").toString() &&
              reopened.outputPath() == source.outputDirectory &&
              reopened.deepSkyCalibrationInputsComplete() && reopened.saveGeneratedMasters() &&
              reopened.selectedReferenceFrame() == source.reference &&
              control<QLabel>(reopened, "calibrationFlatSource")->toolTip() == source.flat.join('\n') &&
              control<QLabel>(reopened, "calibrationDarkSource")->toolTip() == source.masterDark,
          "The reopened order must restore effective processing, output and calibration controls");
    check(readPreferences() == preferences,
          "Restore and automatic settings persistence must leave global preferences unchanged");

    PrintOrderDocument recaptured;
    reopened.captureOrderSettings(recaptured);
    check(recaptured.params == saved.params,
          "The recreated controls must also preserve inactive slider values and exact gray point coordinates");
    QEventLoop debounce;
    QTimer::singleShot(StyleTokens::Motion::kPreviewDebounceMs + 20,
                       &debounce, &QEventLoop::quit);
    debounce.exec();
    check(changes == 1 && referenceChanges == 0 && grayPointRequests == 0,
          "Restoring should publish one completed change without user events or deferred emissions");
}

void checkPartialAndInvalidOrders() {
    ParamsPanel panel;
    panel.applySceneProfile(ProcessingScene::SkyGround, false);
    panel.setModifiedCameraGrayPoint(0.2, 0.9);
    PrintOrderDocument empty;
    empty.scene = QStringLiteral("sky-ground");
    QString error = QStringLiteral("stale error");
    check(ParamsPanel::validateOrderSettings(empty, error) && error.isEmpty(),
          "An empty parameter object must be valid");
    panel.restoreOrderSettings(empty);
    panel.recommendStackMethod(21);
    check(panel.stackMethod() == QStringLiteral("median") && panel.kappaValue() == 2.5 &&
              panel.skyGroundSeparationEnabled() && panel.skyGroundMode() == SkyGroundMask::AutoDetect &&
              panel.featherRadius() == 20 && panel.groundStackMethod() == QStringLiteral("average") &&
              panel.groundDetailStrength() == 40 && panel.outputFormat() == QStringLiteral("tiff16") &&
              panel.outputPath() == QDir::homePath() + "/Nebulens/Output" && panel.userMaskPath().isEmpty() &&
              panel.noiseReductionEnabled() && panel.noiseReductionStrength() == 30 && panel.stretchEnabled() &&
              !panel.modifiedCameraColorEnabled() && !panel.hasModifiedCameraGrayPoint() &&
              panel.basicAdjustmentOptions().exposureTenths == 0 &&
              !panel.starReduceEnabled() && panel.starReduceStrength() == 70,
          "Empty params must use scene defaults instead of machine or prior panel settings");

    PrintOrderDocument partial;
    partial.scene = QStringLiteral("timelapse");
    partial.params = {{"timelapseWindow", 3}, {"photometric", false}, {"futureParam", true}};
    panel.applySceneProfile(ProcessingScene::Timelapse, false);
    check(ParamsPanel::validateOrderSettings(partial, error), "Known partial parameters must be valid");
    panel.restoreOrderSettings(partial);
    check(panel.stackMethod() == QStringLiteral("average") && !panel.autoRejectLowQualityFrames() &&
              !panel.photometricNormalizationEnabled() && !panel.stretchEnabled() &&
              !panel.skyGroundSeparationEnabled() && panel.timelapseWindowSize() == 3 &&
              panel.timelapseStrength() == 80 && panel.timelapseMotionProtection() == 75 &&
              panel.timelapseProtectGround(),
          "Partial params must override only their own scene defaults");

    const auto unchanged = panel.processingSignature();
    const std::pair<const char*, QJsonValue> invalidValues[] = {
        {"stackMethod", 2}, {"autoReject", "false"}, {"photometric", QJsonValue::Null},
        {"kappa", 5.1}, {"kappa", 2.55}, {"timelapseWindow", 4}, {"groundDetail", 71},
        {"skyGroundMode", "unsupported"}, {"outputFormat", "jpeg"}, {"exposureTenths", 0.5},
        {"modifiedCameraGrayPointX", 0.2}, {"modifiedCameraGrayPointY", -0.1},
        {"modifiedCameraColorMode", "manual"}
    };
    for (const auto& [key, value] : invalidValues) {
        partial.params = {{"timelapseStrength", 12}, {QLatin1String(key), value}};
        check(!ParamsPanel::validateOrderSettings(partial, error) && !error.isEmpty(),
              "Invalid known fields must produce a validation error");
        panel.restoreOrderSettings(partial);
        check(panel.processingSignature() == unchanged,
              "An invalid order must not apply any controls");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet(StyleTokens::appStyleSheet());
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(),
          "Temporary settings directory should be available");
    // macOS 原生偏好不会采用测试传入的路径。
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       settingsDirectory.filePath(QStringLiteral("system-settings")));

    {
        QSettings settings("StarProcessor", "App");
        settings.clear();
        settings.setValue("stackMethod", 3);
        settings.setValue("noiseReductionEnabled", true);
        settings.setValue("noiseReductionStrength", 47);
        settings.setValue("exposureTenths", 13);
        settings.setValue("starReduceEnabled", true);
        settings.setValue("starReduceStrength", 63);
        settings.setValue("skyGroundSepEnabled", true);
        settings.setValue("outputPath",
                          QDir::homePath() + "/StarProcessor/Output");
        settings.sync();
    }

    ParamsPanel panel;
    check(panel.stackMethod() == QStringLiteral("winsorized") &&
              panel.noiseReductionEnabled() &&
              panel.noiseReductionStrength() == 47 &&
              panel.basicAdjustmentOptions().exposureTenths == 13 &&
              panel.starReduceEnabled() &&
              panel.starReduceStrength() == 63 &&
              panel.outputPath() == QDir::homePath() + "/Nebulens/Output",
          "ParamsPanel should load persisted processing settings");

    panel.applySceneProfile(ProcessingScene::Nightscape, false);
    check(panel.stackMethod() == QStringLiteral("winsorized") &&
              panel.noiseReductionStrength() == 47 &&
              panel.basicAdjustmentOptions().exposureTenths == 13 &&
              panel.starReduceStrength() == 63,
          "Startup scene visibility must not overwrite persisted settings");

    panel.recommendStackMethod(10);
    check(panel.stackMethod() == QStringLiteral("winsorized"),
          "Automatic recommendation must preserve a restored user choice");

    panel.showWorkflowStage(WorkflowStage::Output);
    auto* inspectorTitle = panel.findChild<QLabel*>(
        QStringLiteral("inspectorTitle"));
    check(inspectorTitle &&
              inspectorTitle->text() == QString::fromUtf8("输出与历史"),
          "Workflow stage selection should update the stage inspector");

    panel.showWorkflowStage(WorkflowStage::Adjustment);
    QGroupBox* basicGroup = nullptr;
    for (QGroupBox* group : panel.findChildren<QGroupBox*>()) {
        if (group->title() == QString::fromUtf8("基础调色")) {
            basicGroup = group;
            break;
        }
    }
    check(basicGroup && basicGroup->isCheckable(),
          "Inspector sections should be genuinely collapsible");
    panel.resize(330, 540);
    panel.show();
    QApplication::processEvents();
    auto* exposure = control<QSlider>(panel, "advancedExposureSlider");
    const int expandedHeight = basicGroup->sizeHint().height();
    const auto adjustmentSliders = basicGroup->findChildren<QSlider*>();
    for (auto* slider : adjustmentSliders) {
        check(slider->geometry().left() == exposure->geometry().left() &&
                  slider->geometry().right() == exposure->geometry().right() &&
                  !slider->accessibleName().isEmpty(),
              "All adjustment rows should align their tracks and label the key controls");
    }
    check(!adjustmentSliders.isEmpty(), "The adjustment form should expose editable sliders");
    checkInlineExposure(panel);
    basicGroup->setFocus(Qt::TabFocusReason);
    pressKey(basicGroup, Qt::Key_Space);
    QApplication::processEvents();
    check(!basicGroup->isChecked(), "Space should collapse the focused section");
    check(basicGroup->isVisible() && !exposure->isVisible(),
          "Keyboard collapse should leave the heading and hide its controls");
    check(basicGroup->sizeHint().height() < expandedHeight,
          "The collapsed section should relinquish its form height");
    pressKey(basicGroup, Qt::Key_Return);
    QApplication::processEvents();
    check(basicGroup->isChecked() && exposure->isVisible() &&
              basicGroup->sizeHint().height() == expandedHeight,
          "The same heading should restore the form with the keyboard");

    QGroupBox* optimizeGroup = nullptr;
    for (QGroupBox* group : panel.findChildren<QGroupBox*>()) {
        if (group->title() == QString::fromUtf8("降噪与增强")) {
            optimizeGroup = group;
            break;
        }
    }
    QSlider* disabledDependentSlider = nullptr;
    if (optimizeGroup) {
        for (QSlider* slider : optimizeGroup->findChildren<QSlider*>()) {
            if (!slider->isEnabled()) {
                disabledDependentSlider = slider;
                break;
            }
        }
    }
    check(disabledDependentSlider,
          "Adjustment fixture should contain a disabled dependent slider");
    optimizeGroup->setChecked(false);
    optimizeGroup->setChecked(true);
    QApplication::processEvents();
    check(!disabledDependentSlider->isEnabled(),
          "Collapsing a section must preserve dependent control state");
    const auto optimizeToggles = optimizeGroup->findChildren<CompactToggle*>();
    auto* noiseToggle = optimizeToggles.first();
    check(noiseToggle->isChecked() && noiseToggle->property("thumbPosition").toReal() == 1.0,
          "A toggle restored from settings with blocked signals should draw as enabled");

    checkReferenceSelection(panel);
    checkCalibrationAndProjectClear(panel, settingsDirectory);
    checkOrderRoundTrip(settingsDirectory);
    checkPartialAndInvalidOrders();

    panel.showWorkflowStage(WorkflowStage::Adjustment);
    panel.applySceneProfile(ProcessingScene::StarTrail);
    check(!panel.noiseReductionEnabled() && noiseToggle->property("thumbPosition").toReal() == 0.0,
          "The preset should draw the toggle as disabled when it turns noise reduction off");
    panel.applySceneProfile(ProcessingScene::DeepSky);
    check(panel.noiseReductionEnabled() && noiseToggle->property("thumbPosition").toReal() == 1.0,
          "The preset should draw the toggle as enabled when it turns noise reduction on");

    {
        QSettings settings("StarProcessor", "App");
        settings.setValue("outputPath",
                          QDir::homePath() + QString::fromUtf8("/星炼/Output"));
        settings.sync();
    }
    ParamsPanel previousBrandPanel;
    check(previousBrandPanel.outputPath() ==
              QDir::homePath() + "/Nebulens/Output",
          "Previous display-brand output path should migrate to Nebulens");

    std::cout << "Params panel tests passed\n";
    return 0;
}
