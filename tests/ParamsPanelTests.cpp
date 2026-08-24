#include "ui/ParamsPanel.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QLabel>
#include <QGroupBox>
#include <QSlider>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
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
              panel.outputPath() == QDir::homePath() + "/星炼/Output",
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

    std::cout << "Params panel tests passed\n";
    return 0;
}
