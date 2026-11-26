#include "ui/InspectorWidgets.h"
#include "ui/ParamsPanel.h"
#include "ui/StyleTokens.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
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
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
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

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet(StyleTokens::appStyleSheet());
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(),
          "Temporary settings directory should be available");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());

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
    basicGroup->setChecked(false);
    QApplication::processEvents();
    check(basicGroup->minimumHeight() == 34,
          "Collapsed inspector sections should keep only their heading");

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

    panel.resize(330, 620);
    panel.show();
    QApplication::processEvents();
    checkReferenceSelection(panel);
    checkCalibrationAndProjectClear(panel, settingsDirectory);

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
