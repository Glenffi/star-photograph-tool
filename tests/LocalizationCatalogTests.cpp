#include "i18n/AppLocalization.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

QStringList placeholders(const QString& text) {
    static const QRegularExpression expression(
        QStringLiteral("%(?:L)?\\d+|%n"));
    QStringList result;
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) result.append(matches.next().captured());
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(),
          "Temporary settings directory should be available");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());
    check(AppLocalization::preferredLanguage() ==
              QString::fromLatin1(AppLocalization::kSimplifiedChinese),
          "Existing users should default to Simplified Chinese");
    AppLocalization::setPreferredLanguage(
        QString::fromLatin1(AppLocalization::kEnglish));
    check(AppLocalization::isEnglish(),
          "English language preference should persist");

    const QString sourceRoot = QStringLiteral(NEBULENS_SOURCE_DIR);
    QFile catalog(sourceRoot + QStringLiteral(
        "/translations/nebulens_en.ts"));
    check(catalog.open(QIODevice::ReadOnly | QIODevice::Text),
          "English TS catalog should be readable");

    QXmlStreamReader xml(&catalog);
    int messageCount = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() ||
            xml.name() != QStringLiteral("message")) {
            continue;
        }

        QString source;
        QString translation;
        bool unfinished = false;
        while (!(xml.isEndElement() &&
                 xml.name() == QStringLiteral("message")) &&
               !xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QStringLiteral("source")) {
                source = xml.readElementText();
            } else if (xml.name() == QStringLiteral("translation")) {
                unfinished =
                    xml.attributes().value(QStringLiteral("type")) ==
                    QStringLiteral("unfinished");
                translation = xml.readElementText();
            }
        }

        ++messageCount;
        check(!unfinished && !translation.isEmpty(),
              "Every English translation should be finished");
        check(placeholders(source) == placeholders(translation),
              "Translation placeholders should match the source");
        const bool intentionalLegacyPath =
            source == QString::fromUtf8("/星炼/Output");
        check(intentionalLegacyPath ||
                  !translation.contains(QRegularExpression(
                      QStringLiteral("[\\x{4e00}-\\x{9fff}]"))),
              "English translations should not contain Chinese text");
    }
    check(!xml.hasError(), "English TS catalog should be valid XML");
    check(messageCount >= 650,
          "English catalog should cover the complete UI surface");

    QTranslator translator;
    check(translator.load(sourceRoot + QStringLiteral(
              "/resources/i18n/nebulens_en.qm")),
          "Compiled English QM catalog should load");
    app.installTranslator(&translator);
    check(QCoreApplication::translate("Nebulens", "素材") ==
              QStringLiteral("Assets") &&
          QCoreApplication::translate("Nebulens", "界面语言:") ==
              QStringLiteral("Interface language:") &&
          QCoreApplication::translate("Nebulens", "视图") ==
              QStringLiteral("View") &&
          QCoreApplication::translate("Nebulens", "循环对比") ==
              QStringLiteral("Cycle Comparison") &&
          QCoreApplication::translate("Nebulens", "处理前后分屏") ==
              QStringLiteral("Before/After Split") &&
          QCoreApplication::translate(
              "Nebulens", "正在下载 Nebulens %1...") ==
              QStringLiteral("Downloading Nebulens %1..."),
          "Representative UI and worker strings should translate");

    std::cout << "Localization catalog tests passed\n";
    return 0;
}
