#include "app/MainWindow.h"
#include "i18n/AppLocalization.h"
#include "ui/StyleTokens.h"
#include "ui/UiAssets.h"

#include <QApplication>
#include <QFileInfo>
#include <QNetworkProxyFactory>
#include <QSet>
#include <QTimer>
#include <cstring>
#include <iostream>

int main(int argc, char* argv[]) {
    // This path deliberately avoids QApplication. The packaging job uses it
    // to make dyld load the completed bundle without requiring a GUI session.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--runtime-check") == 0) {
            std::cout << "StarProcessor " << STARPROCESSOR_VERSION << '\n';
            return 0;
        }
    }

    bool platformRuntimeCheck = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--platform-runtime-check") == 0) {
            platformRuntimeCheck = true;
            break;
        }
    }

    if (platformRuntimeCheck) {
#ifdef Q_OS_MACOS
        // QImage can load the bundled codecs without starting Cocoa in CI.
        QCoreApplication app(argc, argv);
#else
        // On Windows this also loads the deployed QPA plugin and its DLLs.
        QApplication app(argc, argv);
#endif
        const auto decode = [](const char* path, const char* format) {
            if (!QImage(QString::fromLatin1(path), format).isNull()) return true;
            std::cerr << "Cannot decode packaged " << format << ": " << path << '\n';
            return false;
        };
        if (!decode(":/print-order/vellum.png", "PNG") ||
            !decode(":/print-order/lake-complete.jpg", "JPEG")) {
            return 1;
        }
        std::cout << "StarProcessor " << STARPROCESSOR_VERSION
                  << " print-order PNG and JPEG decoded\n";
        return 0;
    }

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication app(argc, argv);
    // Keep the technical application identity for settings/update migration;
    // all user-facing surfaces use the new product name.
    app.setApplicationName("StarProcessor");
    app.setApplicationDisplayName(QStringLiteral("Nebulens"));
    app.setApplicationVersion(STARPROCESSOR_VERSION);
    app.setOrganizationName("StarProcessor");
    app.setWindowIcon(UiAssets::appIcon());
    AppLocalization::install(app);

    // QNetworkAccessManager otherwise defaults to NoProxy. Respect the
    // operating system proxy/PAC configuration for update checks and package
    // downloads without storing proxy credentials in the application.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    app.setStyleSheet(StyleTokens::appStyleSheet());

    MainWindow window;
    window.show();

    for (const QString& argument : app.arguments()) {
        if (argument == "--scene=single") {
            window.selectStartupScene(ProcessingScene::SingleFrame);
        } else if (argument == "--scene=nightscape") {
            window.selectStartupScene(ProcessingScene::Nightscape);
        } else if (argument == "--scene=deep-sky") {
            window.selectStartupScene(ProcessingScene::DeepSky);
        } else if (argument == "--scene=sky-ground") {
            window.selectStartupScene(ProcessingScene::SkyGround);
        } else if (argument == "--scene=star-trail") {
            window.selectStartupScene(ProcessingScene::StarTrail);
        } else if (argument == "--scene=timelapse") {
            window.selectStartupScene(ProcessingScene::Timelapse);
        } else if (argument == "--inspector=output") {
            window.selectStartupInspectorOutput();
        } else if (argument == "--workflow=assets") {
            window.selectStartupWorkflowStage(WorkflowStage::Assets);
        } else if (argument == "--workflow=calibration") {
            window.selectStartupWorkflowStage(WorkflowStage::Calibration);
        } else if (argument == "--workflow=align") {
            window.selectStartupWorkflowStage(WorkflowStage::Alignment);
        } else if (argument == "--workflow=stack") {
            window.selectStartupWorkflowStage(WorkflowStage::Stacking);
        } else if (argument == "--workflow=adjust") {
            window.selectStartupWorkflowStage(WorkflowStage::Adjustment);
        } else if (argument == "--workflow=output") {
            window.selectStartupWorkflowStage(WorkflowStage::Output);
        } else if (argument.startsWith("--history=")) {
            window.loadStartupHistory(
                argument.mid(QStringLiteral("--history=").size()));
        } else if (argument.startsWith("--window-size=")) {
            const QStringList dimensions = argument.mid(
                QStringLiteral("--window-size=").size()).split('x');
            if (dimensions.size() == 2) {
                bool widthOk = false;
                bool heightOk = false;
                const int width = dimensions[0].toInt(&widthOk);
                const int height = dimensions[1].toInt(&heightOk);
                if (widthOk && heightOk && width > 0 && height > 0) {
                    window.resize(width, height);
                }
            }
        }
    }

    const QSet<QString> rawExtensions = {
        "nef", "cr2", "arw", "dng", "raw", "orf", "raf", "pef", "cr3"
    };
    QStringList startupFiles;
    for (const QString& argument : app.arguments().mid(1)) {
        if (argument.startsWith("--")) continue;
        const QFileInfo info(argument);
        if (info.isFile() && rawExtensions.contains(info.suffix().toLower())) {
            startupFiles.append(info.absoluteFilePath());
        }
    }
    window.importFiles(startupFiles);

    // Offscreen screenshot mode keeps visual regression checks reproducible
    // without requiring macOS screen-recording permission.
    for (const QString& argument : app.arguments()) {
        constexpr auto prefix = "--screenshot=";
        if (!argument.startsWith(prefix)) continue;
        const QString outputPath = argument.mid(QString(prefix).size());
        QTimer::singleShot(1500, &window, [&app, &window, outputPath]() {
            if (!window.grab().save(outputPath)) {
                qWarning() << "无法保存 UI 截图:" << outputPath;
                app.exit(2);
                return;
            }
            app.quit();
        });
        break;
    }

    return app.exec();
}
