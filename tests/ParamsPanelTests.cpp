#include "ui/ParamsPanel.h"

#include <QApplication>
#include <QSettings>
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
        settings.sync();
    }

    ParamsPanel panel;
    check(panel.stackMethod() == QStringLiteral("winsorized") &&
              panel.noiseReductionEnabled() &&
              panel.noiseReductionStrength() == 47 &&
              panel.basicAdjustmentOptions().exposureTenths == 13 &&
              panel.starReduceEnabled() &&
              panel.starReduceStrength() == 63,
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

    std::cout << "Params panel tests passed\n";
    return 0;
}
