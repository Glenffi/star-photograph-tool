#include "AppLocalization.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QTranslator>

#include <memory>

namespace AppLocalization {
namespace {

std::unique_ptr<QTranslator> g_applicationTranslator;
std::unique_ptr<QTranslator> g_qtTranslator;

QString normalizedLanguage(const QString& languageCode) {
    return languageCode.startsWith(QStringLiteral("en"),
                                   Qt::CaseInsensitive)
        ? QString::fromLatin1(kEnglish)
        : QString::fromLatin1(kSimplifiedChinese);
}

bool loadQtChineseTranslation(QTranslator& translator) {
    const QString fileName = QStringLiteral("qtbase_zh_CN.qm");
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDirectory).filePath(
            QStringLiteral("translations/") + fileName),
        QDir(appDirectory).filePath(
            QStringLiteral("../Resources/translations/") + fileName),
        QDir(QLibraryInfo::path(QLibraryInfo::TranslationsPath))
            .filePath(fileName)
    };
    for (const QString& candidate : candidates) {
        if (translator.load(candidate)) return true;
    }
    return false;
}

}  // namespace

QString preferredLanguage() {
    // NativeFormat remains the application default. Tests can choose an isolated
    // INI store instead of reading or modifying the photographer's desktop preferences.
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("StarProcessor"), QStringLiteral("App"));
    return normalizedLanguage(settings.value(
        QStringLiteral("ui/language"),
        QString::fromLatin1(kSimplifiedChinese)).toString());
}

void setPreferredLanguage(const QString& languageCode) {
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("StarProcessor"), QStringLiteral("App"));
    settings.setValue(QStringLiteral("ui/language"),
                      normalizedLanguage(languageCode));
    settings.sync();
}

bool isEnglish() {
    return preferredLanguage() == QString::fromLatin1(kEnglish);
}

void install(QCoreApplication& application) {
    QString language = preferredLanguage();
    const QString prefix = QStringLiteral("--language=");
    for (const QString& argument : application.arguments()) {
        if (argument.startsWith(prefix)) {
            language = normalizedLanguage(argument.mid(prefix.size()));
            break;
        }
    }
    QLocale::setDefault(QLocale(language));

    if (g_applicationTranslator) {
        application.removeTranslator(g_applicationTranslator.get());
        g_applicationTranslator.reset();
    }
    if (g_qtTranslator) {
        application.removeTranslator(g_qtTranslator.get());
        g_qtTranslator.reset();
    }

    if (language == QString::fromLatin1(kEnglish)) {
        auto translator = std::make_unique<QTranslator>();
        if (translator->load(QStringLiteral(":/i18n/nebulens_en.qm"))) {
            application.installTranslator(translator.get());
            g_applicationTranslator = std::move(translator);
        }
        return;
    }

    auto qtTranslator = std::make_unique<QTranslator>();
    if (loadQtChineseTranslation(*qtTranslator)) {
        application.installTranslator(qtTranslator.get());
        g_qtTranslator = std::move(qtTranslator);
    }
}

bool restartApplication() {
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) arguments.removeFirst();
    return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                   arguments);
}

}  // namespace AppLocalization
