#include "i18n/AppLocalization.h"

#include <QByteArray>
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

void checkTranslation(const QString& actual, const QString& expected,
                      const QString& context, const QString& source) {
    if (actual == expected) return;
    std::cerr << "FAIL: " << context.toStdString() << " / "
              << source.toStdString() << "\n  Expected: "
              << expected.toStdString() << "\n  Actual: "
              << actual.toStdString() << '\n';
    std::exit(1);
}

QStringList placeholders(const QString& text) {
    static const QRegularExpression expression(
        QStringLiteral("%L?(?:\\d+|n)"));
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
    QSettings::setDefaultFormat(QSettings::IniFormat);
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
    QTranslator translator;
    check(translator.load(sourceRoot + QStringLiteral(
              "/resources/i18n/nebulens_en.qm")),
          "Compiled English QM catalog should load");

    QFile catalog(sourceRoot + QStringLiteral(
        "/translations/nebulens_en.ts"));
    check(catalog.open(QIODevice::ReadOnly | QIODevice::Text),
          "English TS catalog should be readable");

    QXmlStreamReader xml(&catalog);
    QString context;
    int messageCount = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == QStringLiteral("name")) {
            context = xml.readElementText();
            continue;
        }
        if (xml.name() != QStringLiteral("message")) continue;

        QString source;
        QString translation;
        QByteArray disambiguation;
        QString translationType;
        while (!(xml.isEndElement() &&
                 xml.name() == QStringLiteral("message")) &&
               !xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QStringLiteral("source")) {
                source = xml.readElementText();
            } else if (xml.name() == QStringLiteral("comment")) {
                disambiguation = xml.readElementText().toUtf8();
            } else if (xml.name() == QStringLiteral("translation")) {
                translationType =
                    xml.attributes().value(QStringLiteral("type")).toString();
                translation = xml.readElementText();
            }
        }

        ++messageCount;
        check(translationType.isEmpty() && !translation.trimmed().isEmpty(),
              "Every English translation should be finished");
        check(placeholders(source) == placeholders(translation),
              "Translation placeholders should match the source");
        static const QRegularExpression xmlIndent(QStringLiteral("\\n[ \\t]{8,}"));
        check(source.contains('\n') || !translation.contains(xmlIndent),
              "XML formatting must not indent wrapped single-paragraph UI text");
        const bool intentionalLegacyPath =
            source == QString::fromUtf8("/星炼/Output");
        check(intentionalLegacyPath ||
                  !translation.contains(QRegularExpression(
                      QStringLiteral("[\\x{4e00}-\\x{9fff}]"))),
              "English translations should not contain Chinese text");
        checkTranslation(
            translator.translate(context.toUtf8().constData(),
                                 source.toUtf8().constData(),
                                 disambiguation.isEmpty() ? nullptr
                                                          : disambiguation.constData()),
            translation, context, source);
    }
    check(!xml.hasError(), "English TS catalog should be valid XML");
    check(messageCount >= 650,
          "English catalog should cover the complete UI surface");

    app.installTranslator(&translator);
    struct Sample {
        const char* source;
        const char* english;
        const char* context = "Nebulens";
    };
    constexpr Sample samples[] = {
        {"素材", "Assets"},
        {"界面语言:", "Interface language:"},
        {"视图", "View"},
        {"循环对比", "Cycle Comparison"},
        {"处理前后分屏", "Before/After Split"},
        {"正在下载 Nebulens %1...", "Downloading Nebulens %1..."},
        {"选择", "Select", "Toolbar"},
        {"选择", "Select"},
        {"冲印单", "Print Order"},
        {"高级暗房", "Advanced Darkroom"},
        {"成品", "Product"},
        {"保存", "Save"},
        {"核对素材", "Review Assets"},
        {"交付设置", "Delivery Settings"},
        {"一组延时照片", "A Timelapse Sequence"},
        {"冲印回执", "Print Receipt"},
        {"实际保存位置", "Saved To"},
        {"%1 张成品", "%1 saved"},
        {"返回冲印单", "Back to Order"},
        {"导入文件夹", "Import Folder"},
        {"选择目录…", "Choose Folder…"},
        {"打开目录", "Open Folder"},
        {"无法打开成品位置：%1", "Unable to open the saved output location: %1"},
        {"纸面光影", "Paper Lighting"},
        {"柔光", "Soft Light"},
        {"流光", "Light Sweep"},
        {"静止", "Still"},
        {"已遵循系统的减弱动态效果设置", "Follows your system's Reduce Motion setting"},
        {"校准详情", "Calibration Details"},
        {"PNG 8-bit (预览)", "PNG 8-bit (Preview)"},
        {"无法读取延时序列的磁盘可用空间：%1",
         "Could not check available disk space for the timelapse sequence: %1"},
        {"临时卷和输出卷相同，磁盘空间不足：RAW 缓存与 %1 序列共需约 %2，当前可用 %3。",
         "Not enough space on the shared temporary and output volume: the RAW cache and %1 sequence need about %2 in total; %3 available."},
        {"临时磁盘空间不足：延时 RAW 缓存约需 %1，当前可用 %2。",
         "Not enough temporary disk space: the timelapse RAW cache needs about %1; %2 available."},
        {"输出磁盘空间不足：%1 序列约需 %2，当前可用 %3。",
         "Not enough space on the output volume: the %1 sequence needs about %2; %3 available."},
        {"对齐后共同有效区域不足，无法堆栈。请移除偏移过大的帧后重试。",
         "The common valid region after alignment is too small to stack. Remove frames with large shifts and try again."},
        {"\n• ", "\n• "},
        {"%1\n%2 · %3 MB", "%1\n%2 · %3 MB"},
    };
    for (const auto& sample : samples) {
        checkTranslation(QCoreApplication::translate(sample.context, sample.source),
                         QString::fromUtf8(sample.english),
                         QString::fromUtf8(sample.context),
                         QString::fromUtf8(sample.source));
    }
    check(translator.translate("Nebulens", "/星炼/Output") ==
              QString::fromUtf8("/星炼/Output"),
          "The legacy output path must remain unchanged in the English QM");
    app.removeTranslator(&translator);
    check(QCoreApplication::translate("Nebulens", "冲印回执") ==
              QString::fromUtf8("冲印回执"),
          "Print order copy should remain Chinese without the English translator");

    std::cout << "Localization catalog tests passed\n";
    return 0;
}
