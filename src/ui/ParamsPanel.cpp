#include "ParamsPanel.h"
#include "ProcessingScene.h"
#include "InspectorWidgets.h"
#include "FileDialogUtils.h"
#include "StyleTokens.h"
#include "UiAssets.h"
#include "core/PresetManager.h"
#include "core/PrintOrderDocument.h"
#include <QCoreApplication>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QTimer>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QDir>
#include <QDateTime>
#include <QSettings>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QInputDialog>
#include <QLocale>
#include <QSet>
#include <QTabWidget>
#include <QTabBar>
#include <QAbstractItemView>
#include <QGridLayout>
#include <QStyle>

#include <QSignalBlocker>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
int formLabelWidth() {
    return QLocale().language() == QLocale::English ? 112 : 88;
}

void setRole(QWidget* widget, const char* role) {
    if (!widget) return;

    const QByteArray semanticRole(role);
    if (semanticRole == "panelTitle") {
        widget->setProperty(StyleTokens::Properties::kTextRole,
                            StyleTokens::Properties::kTitle);
    } else if (semanticRole == "value") {
        widget->setProperty(StyleTokens::Properties::kTextRole,
                            StyleTokens::Properties::kMono);
    } else {
        widget->setProperty(StyleTokens::Properties::kTextRole,
                            StyleTokens::Properties::kCaption);
    }

    // Preserve semantic hooks for status updates and future component-level
    // styling without reintroducing a page-local stylesheet.
    widget->setProperty("inspectorRole", semanticRole);
}

void setButtonVariant(QPushButton* button, const char* variant) {
    if (!button) return;
    button->setProperty(StyleTokens::Properties::kVariant, variant);
    button->setCursor(Qt::PointingHandCursor);
}

void displayOutputPath(QLineEdit* edit, const QString& path) {
    if (!edit) return;
    edit->setText(path);
    edit->setToolTip(path);
    // The directory tail usually distinguishes projects better than the
    // repeated home-directory prefix; the tooltip retains the full path.
    edit->setCursorPosition(path.size());
}

QString signedAdjustmentText(int value) {
    return value > 0 ? QStringLiteral("+%1").arg(value)
                     : QString::number(value);
}

QString exposureText(int tenths) {
    const double ev = static_cast<double>(tenths) / 10.0;
    return QStringLiteral("%1%2 EV")
        .arg(ev > 0.0 ? QStringLiteral("+") : QString())
        .arg(ev, 0, 'f', 1);
}

QString defaultOutputPath() {
    return QDir::homePath() + QStringLiteral("/Nebulens/Output");
}

QString legacyXingLianOutputPath() {
    return QDir::homePath() + QCoreApplication::translate("Nebulens", "/星炼/Output");
}

QString legacyTechnicalOutputPath() {
    return QDir::homePath() + QStringLiteral("/StarProcessor/Output");
}

QString migrateDefaultOutputPath(const QString& path) {
    const QString cleanPath = QDir::cleanPath(path);
    if (cleanPath == QDir::cleanPath(legacyXingLianOutputPath()) ||
        cleanPath == QDir::cleanPath(legacyTechnicalOutputPath())) {
        return defaultOutputPath();
    }
    return path;
}

constexpr qsizetype kMinimumCalibrationRawFrames = 3;

struct CalibrationSource {
    qsizetype rawCount;
    bool master;

    bool present() const { return master || rawCount > 0; }
    bool ready() const {
        return master ? rawCount == 0 : rawCount >= kMinimumCalibrationRawFrames;
    }
};

struct CalibrationSources {
    CalibrationSource dark;
    CalibrationSource flat;
    CalibrationSource bias;
    CalibrationSource darkFlat;

    bool missingFlatOffset() const {
        return !flat.master && !bias.present() && !darkFlat.present();
    }
    bool conflictingFlatOffsets() const {
        // An imported Master Dark may need separate Bias for the lights.
        return !flat.master && !dark.master && bias.present() && darkFlat.present();
    }
    bool unusedBias() const {
        return flat.master && !dark.master && bias.present();
    }
    bool unusedDarkFlat() const {
        return flat.master && darkFlat.present();
    }
    bool hasRaw() const {
        return dark.rawCount > 0 || flat.rawCount > 0 ||
            bias.rawCount > 0 || darkFlat.rawCount > 0;
    }
    bool ready() const {
        // Header/metadata validation belongs to the processing preflight.
        return dark.ready() && flat.ready() &&
            !missingFlatOffset() && !conflictingFlatOffsets() &&
            !unusedBias() && !unusedDarkFlat() &&
            (!bias.present() || bias.ready()) &&
            (!darkFlat.present() || darkFlat.ready());
    }
};

CalibrationSources calibrationSources(const ParamsPanel& panel) {
    return {
        {panel.darkFramePaths().size(), !panel.masterDarkPath().isEmpty()},
        {panel.flatFramePaths().size(), !panel.masterFlatPath().isEmpty()},
        {panel.biasFramePaths().size(), !panel.masterBiasPath().isEmpty()},
        {panel.darkFlatFramePaths().size(), !panel.masterDarkFlatPath().isEmpty()}
    };
}

void updateCalibrationSource(QLabel* label, QPushButton* clearButton,
                             const QStringList& rawPaths,
                             const QString& masterPath) {
    if (!masterPath.isEmpty()) {
        label->setText(QCoreApplication::translate("Nebulens", "已选主帧"));
        label->setToolTip(masterPath);
        label->setAccessibleDescription(masterPath);
    } else if (!rawPaths.isEmpty()) {
        label->setText(QCoreApplication::translate("Nebulens", "RAW %1 张").arg(rawPaths.size()));
        label->setToolTip(rawPaths.join(QLatin1Char('\n')));
        label->setAccessibleDescription(QString());
    } else {
        label->setText(QCoreApplication::translate("Nebulens", "未选择"));
        label->setToolTip(QString());
        label->setAccessibleDescription(QString());
    }
    clearButton->setEnabled(!rawPaths.isEmpty() || !masterPath.isEmpty());
}

Preset scenePreset(ProcessingScene scene) {
    switch (scene) {
    case ProcessingScene::Nightscape:
    case ProcessingScene::SkyGround:
        return PresetManager::builtinPresets().value(0);
    case ProcessingScene::DeepSky:
        return PresetManager::builtinPresets().value(1);
    case ProcessingScene::SingleFrame: {
        Preset preset;
        preset.stackMethod = "median";
        preset.autoRejectLowQualityFrames = false;
        preset.photometricNormalizationEnabled = false;
        preset.noiseReductionEnabled = true;
        preset.noiseReductionStrength = 25;
        preset.stretchEnabled = true;
        return preset;
    }
    case ProcessingScene::StarTrail:
    case ProcessingScene::Timelapse: {
        Preset preset;
        preset.autoRejectLowQualityFrames = false;
        preset.stretchEnabled = scene == ProcessingScene::StarTrail;
        return preset;
    }
    }
    return {};
}

struct OrderSettings {
    Preset preset;
    bool skyGround = false;
    QString skyGroundMode = QStringLiteral("auto");
    int feather = 20;
    QString groundMethod = QStringLiteral("average");
    int groundDetail = 40;
    int timelapseWindow = 5;
    int timelapseStrength = 80;
    int timelapseMotionProtection = 75;
    bool timelapseProtectGround = true;
    int starTrailCometStrength = 0;
    bool starTrailReverse = false;
    bool starTrailProtectGround = true;
    bool saveGeneratedMasters = false;
    QString modifiedCameraColorMode = QStringLiteral("auto");
    double grayPointX = -1.0;
    double grayPointY = -1.0;
};

// 文档只使用固定键和真实单位，不保存翻译后的文字或控件索引。
template <typename Visitor>
void visitOrderSettings(OrderSettings& settings, const Visitor& field) {
    auto& preset = settings.preset;
    auto& basic = preset.basicAdjustments;
    field("stackMethod", preset.stackMethod,
          QStringList{"median", "average", "kappa-sigma", "winsorized"});
    field("kappa", preset.kappaValue, 1.0, 5.0);
    field("autoReject", preset.autoRejectLowQualityFrames);
    field("photometric", preset.photometricNormalizationEnabled);
    field("dewarpEnabled", preset.dewarpEnabled);
    field("dewarpStrength", preset.dewarpStrength, 0, 100);
    field("noiseReductionEnabled", preset.noiseReductionEnabled);
    field("noiseReductionStrength", preset.noiseReductionStrength, 0, 70);
    field("modifiedCameraColorEnabled", preset.modifiedCameraColorEnabled);
    field("modifiedCameraColorStrength", preset.modifiedCameraColorStrength, 0, 100);
    field("modifiedCameraColorMode", settings.modifiedCameraColorMode,
          QStringList{"auto", "manual"});
    field("stretchEnabled", preset.stretchEnabled);
    field("temperature", basic.temperature, -100, 100);
    field("tint", basic.tint, -100, 100);
    field("exposureTenths", basic.exposureTenths, -50, 50);
    field("contrast", basic.contrast, -100, 100);
    field("highlights", basic.highlights, -100, 100);
    field("shadows", basic.shadows, -100, 100);
    field("whites", basic.whites, -100, 100);
    field("blacks", basic.blacks, -100, 100);
    field("vibrance", basic.vibrance, -100, 100);
    field("saturation", basic.saturation, -100, 100);
    field("sharpening", basic.sharpening, 0, 100);
    field("starDefringeEnabled", preset.starDefringeEnabled);
    field("starDefringeStrength", preset.starDefringeStrength, 0, 100);
    field("starReduceEnabled", preset.starReduceEnabled);
    field("starReduceStrength", preset.starReduceStrength, 0, 100);
    field("outputFormat", preset.outputFormat, QStringList{"tiff16", "png8"});
    field("skyGround", settings.skyGround);
    field("skyGroundMode", settings.skyGroundMode, QStringList{"auto", "user-mask"});
    field("feather", settings.feather, 0, 50);
    field("groundMethod", settings.groundMethod, QStringList{"average", "reference", "median"});
    field("groundDetail", settings.groundDetail, 0, 70);
    field("timelapseWindow", settings.timelapseWindow, 3, 5);
    field("timelapseStrength", settings.timelapseStrength, 0, 100);
    field("timelapseMotionProtection", settings.timelapseMotionProtection, 0, 100);
    field("timelapseProtectGround", settings.timelapseProtectGround);
    field("starTrailCometStrength", settings.starTrailCometStrength, 0, 100);
    field("starTrailReverse", settings.starTrailReverse);
    field("starTrailProtectGround", settings.starTrailProtectGround);
    field("saveGeneratedMasters", settings.saveGeneratedMasters);
}

bool readOrderValue(const QJsonValue& value, bool& result) {
    if (!value.isBool()) return false;
    result = value.toBool();
    return true;
}

bool readOrderValue(const QJsonValue& value, double& result, double min, double max) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!(number >= min && number <= max)) return false;
    result = number;
    return true;
}

bool readOrderValue(const QJsonValue& value, int& result, int min, int max) {
    double number;
    if (!readOrderValue(value, number, min, max) || number != std::trunc(number)) {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

bool readOrderValue(const QJsonValue& value, QString& result, const QStringList& choices) {
    if (!value.isString() || !choices.contains(value.toString())) return false;
    result = value.toString();
    return true;
}

bool decodeOrderSettings(const QJsonObject& params, OrderSettings& settings, QString& error) {
    error.clear();
    const auto invalid = [&error](QLatin1String key) {
        error = QCoreApplication::translate("Nebulens", "冲印单参数无效：%1").arg(key);
        return false;
    };
    const auto field = [&params, &error, &invalid](const char* name, auto& result,
                                                 const auto&... constraints) {
        if (!error.isEmpty()) return;
        const QLatin1String key(name);
        const auto value = params.value(key);
        if (!value.isUndefined() && !readOrderValue(value, result, constraints...)) {
            invalid(key);
        }
    };
    // 只校验当前版本认识的键；缺省项沿用场景值，额外键不参与还原。
    visitOrderSettings(settings, field);
    field("modifiedCameraGrayPointX", settings.grayPointX, 0.0, 1.0);
    field("modifiedCameraGrayPointY", settings.grayPointY, 0.0, 1.0);
    if (!error.isEmpty()) return false;

    const double kappaTenths = settings.preset.kappaValue * 10.0;
    if (std::abs(kappaTenths - std::round(kappaTenths)) > 1e-8) {
        return invalid(QLatin1String("kappa"));
    }
    if (settings.timelapseWindow != 3 && settings.timelapseWindow != 5) {
        return invalid(QLatin1String("timelapseWindow"));
    }
    const bool hasX = settings.grayPointX >= 0.0;
    const bool hasY = settings.grayPointY >= 0.0;
    if (hasX != hasY || (!hasX && settings.modifiedCameraColorMode == "manual")) {
        return invalid(QLatin1String("modifiedCameraGrayPointX/Y"));
    }
    return true;
}

} // namespace

ParamsPanel::ParamsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    loadPreset();
    refreshRecentResults();
}

void ParamsPanel::setupUI() {
    setObjectName("paramsPanel");
    setProperty(StyleTokens::Properties::kUiRole,
                StyleTokens::Properties::kPanel);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("paramsTitleBar"));
    titleBar->setFixedHeight(74);
    auto* titleLayout = new QVBoxLayout(titleBar);
    titleLayout->setContentsMargins(
        StyleTokens::Spacing::kPanelPadding, 12,
        StyleTokens::Spacing::kPanelPadding, 10);
    titleLayout->setSpacing(3);
    m_stageKicker = new QLabel(
        QCoreApplication::translate("Nebulens", "05 / 调整"), titleBar);
    m_stageKicker->setObjectName(QStringLiteral("inspectorKicker"));
    m_stageKicker->setStyleSheet(QStringLiteral("font-size: %1px; letter-spacing: 0px;")
        .arg(StyleTokens::Typography::kCaptionSize));
    titleLayout->addWidget(m_stageKicker);
    m_titleLabel = new QLabel(QCoreApplication::translate("Nebulens", "成片调整"), titleBar);
    setRole(m_titleLabel, "panelTitle");
    m_titleLabel->setObjectName(QStringLiteral("inspectorTitle"));
    titleLayout->addWidget(m_titleLabel);
    layout->addWidget(titleBar);

    m_presetBar = new QWidget(this);
    m_presetBar->setObjectName("paramsPresetBar");
    m_presetBar->setFixedHeight(44);
    auto* presetRow = new QHBoxLayout(m_presetBar);
    presetRow->setContentsMargins(StyleTokens::Spacing::kPanelPadding,
                                  StyleTokens::Spacing::kBase,
                                  StyleTokens::Spacing::kPanelPadding,
                                  StyleTokens::Spacing::kBase);
    presetRow->setSpacing(StyleTokens::Spacing::kControlGap);
    auto* presetLabel = new QLabel(QCoreApplication::translate("Nebulens", "预设"), m_presetBar);
    presetLabel->setFixedWidth(44);
    setRole(presetLabel, "muted");
    presetRow->addWidget(presetLabel);
    m_presetCombo = new QComboBox(m_presetBar);
    m_presetCombo->addItem(QCoreApplication::translate("Nebulens", "自定义"));
    for (const Preset& preset : PresetManager::builtinPresets()) {
        m_presetCombo->addItem(preset.name);
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onPresetChanged);
    presetRow->addWidget(m_presetCombo, 1);
    layout->addWidget(m_presetBar);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("paramsTabs");
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setExpanding(true);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->tabBar()->hide();

    auto createPage = [this](const QString& name) -> QVBoxLayout* {
        auto* page = new QWidget(m_tabs);
        page->setObjectName("paramsTabPage");
        page->setProperty(StyleTokens::Properties::kUiRole,
                          StyleTokens::Properties::kPanel);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(page);
        scroll->setObjectName("paramsScrollArea");
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget(scroll);
        content->setObjectName("paramsPageContent");
        content->setProperty(StyleTokens::Properties::kUiRole,
                             StyleTokens::Properties::kPanel);
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(
            StyleTokens::Spacing::kPanelPadding,
            StyleTokens::Spacing::kBase,
            StyleTokens::Spacing::kPanelPadding,
            StyleTokens::Spacing::kSectionGap);
        contentLayout->setSpacing(0);
        scroll->setWidget(content);
        pageLayout->addWidget(scroll);
        m_tabs->addTab(page, name);
        return contentLayout;
    };

    QVBoxLayout* stackPageLayout = createPage(QCoreApplication::translate("Nebulens", "堆栈"));
    QVBoxLayout* adjustPageLayout = createPage(QCoreApplication::translate("Nebulens", "调整"));
    QVBoxLayout* outputPageLayout = createPage(QCoreApplication::translate("Nebulens", "输出"));
    layout->addWidget(m_tabs, 1);

    m_assetsGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "素材与序列"), true);
    auto* assetsLayout = new QVBoxLayout(m_assetsGroup);
    auto* assetsSummary = new QLabel(
        QCoreApplication::translate("Nebulens",
            "底部胶片带管理 Light 素材。点击缩略图查看源文件，"
            "空格排除或恢复，右键可设为参考帧。"),
        m_assetsGroup);
    assetsSummary->setWordWrap(true);
    setRole(assetsSummary, "note");
    assetsLayout->addWidget(assetsSummary);
    auto* assetMetrics = new QGridLayout();
    assetMetrics->setContentsMargins(0, 6, 0, 0);
    assetMetrics->setHorizontalSpacing(12);
    assetMetrics->setVerticalSpacing(10);
    const auto addAssetMetric = [this, assetMetrics](
                                    int row, const QString& name,
                                    QLabel*& value) {
        auto* label = new QLabel(name, m_assetsGroup);
        setRole(label, "muted");
        assetMetrics->addWidget(label, row, 0);
        value = new QLabel(QStringLiteral("--"), m_assetsGroup);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setRole(value, "value");
        value->setProperty("inspectorRole", "assetSummary");
        value->setMinimumWidth(120);
        assetMetrics->addWidget(value, row, 1);
    };
    addAssetMetric(0, QCoreApplication::translate("Nebulens", "可用素材"), m_assetCountValue);
    addAssetMetric(1, QCoreApplication::translate("Nebulens", "场景门槛"), m_assetThresholdValue);
    addAssetMetric(2, QCoreApplication::translate("Nebulens", "参考帧"), m_assetReferenceValue);
    assetsLayout->addLayout(assetMetrics);
    stackPageLayout->addWidget(m_assetsGroup);

    m_lightCalibrationGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "镜头与传感器"), true);
    auto* lightCalibrationLayout = new QVBoxLayout(m_lightCalibrationGroup);
    auto* linearNote = new QLabel(
        QCoreApplication::translate("Nebulens",
            "当前场景使用 LibRaw 线性 16-bit 解码，并在处理阶段完成"
            "黑电平、白平衡和星点色差校正。深空模式可在此设置"
            " Dark、Flat、Bias 与 Dark Flat。"),
        m_lightCalibrationGroup);
    linearNote->setWordWrap(true);
    setRole(linearNote, "note");
    lightCalibrationLayout->addWidget(linearNote);
    stackPageLayout->addWidget(m_lightCalibrationGroup);

    // Deep-sky calibration runs before alignment and stacking. The paths are
    // intentionally session-only because removable disks and capture folders
    // often move between launches.
    m_calibrationGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "校准帧"), true);
    m_calibrationGroup->setObjectName(QStringLiteral("calibrationGroup"));
    m_calibrationGroup->setVisible(false);
    auto* calibrationLayout = new QVBoxLayout(m_calibrationGroup);

    m_calibrationStatus = new QLabel(m_calibrationGroup);
    m_calibrationStatus->setObjectName(QStringLiteral("calibrationStatus"));
    m_calibrationStatus->setWordWrap(true);
    m_calibrationStatus->setTextFormat(Qt::PlainText);
    m_calibrationStatus->setProperty(StyleTokens::Properties::kTextRole,
                                    StyleTokens::Properties::kSecondary);
    calibrationLayout->addWidget(m_calibrationStatus);

    auto addCalibrationRow = [this, calibrationLayout](
                                 const char* roleKey,
                                 const QString& name,
                                 const QString& purpose,
                                 QStringList& paths,
                                 QString& masterPath,
                                 QLabel*& sourceLabel,
                                 QPushButton*& clearButton) {
        const QString key = QStringLiteral("calibration") + QString::fromLatin1(roleKey);
        auto* rowWidget = new QWidget(m_calibrationGroup);
        rowWidget->setObjectName(key + QStringLiteral("Row"));
        auto* row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(StyleTokens::Spacing::kBase);

        auto* summary = new QVBoxLayout();
        summary->setSpacing(0);
        auto* nameLabel = new QLabel(name, rowWidget);
        summary->addWidget(nameLabel);
        sourceLabel = new QLabel(rowWidget);
        sourceLabel->setObjectName(key + QStringLiteral("Source"));
        sourceLabel->setTextFormat(Qt::PlainText);
        setRole(sourceLabel, "muted");
        summary->addWidget(sourceLabel);
        row->addLayout(summary, 1);

        auto* rawButton = new QPushButton(QStringLiteral("RAW"), rowWidget);
        rawButton->setObjectName(key + QStringLiteral("Raw"));
        rawButton->setProperty(StyleTokens::Properties::kSizeRole,
                               "compactText");
        rawButton->setMinimumWidth(48);
        rawButton->setToolTip(
            QCoreApplication::translate("Nebulens", "导入%1 RAW 原片组：%2").arg(name, purpose));
        rawButton->setAccessibleName(rawButton->toolTip());
        row->addWidget(rawButton);

        auto* masterButton = new QPushButton(
            QCoreApplication::translate("Nebulens", "主帧"), rowWidget);
        masterButton->setObjectName(key + QStringLiteral("Master"));
        masterButton->setProperty(StyleTokens::Properties::kSizeRole,
                                  "compactText");
        masterButton->setMinimumWidth(60);
        masterButton->setToolTip(QCoreApplication::translate("Nebulens",
            "导入 Nebulens %1（仅支持 .spmaster）").arg(name));
        masterButton->setAccessibleName(masterButton->toolTip());
        row->addWidget(masterButton);

        clearButton = new QPushButton(rowWidget);
        clearButton->setObjectName(key + QStringLiteral("Clear"));
        clearButton->setIcon(
            UiAssets::icon(
                UiAssets::Glyph::Trash,
                StyleTokens::Colors::fromHex(
                    StyleTokens::Colors::kTextSecondary)));
        clearButton->setIconSize(QSize(16, 16));
        clearButton->setFixedSize(StyleTokens::Controls::kIconButtonSize,
                                  StyleTokens::Controls::kIconButtonSize);
        clearButton->setToolTip(
            QCoreApplication::translate("Nebulens", "清空已选%1").arg(name));
        clearButton->setAccessibleName(clearButton->toolTip());
        updateCalibrationSource(sourceLabel, clearButton, paths, masterPath);
        QStringList* const pathList = &paths;
        QString* const master = &masterPath;
        connect(rawButton, &QPushButton::clicked, this,
                [this, pathList, sourceLabel, clearButton, name]() {
                    importCalibrationFrames(
                        *pathList, sourceLabel, clearButton,
                        QCoreApplication::translate("Nebulens", "选择%1 RAW").arg(name));
                });
        connect(masterButton, &QPushButton::clicked, this,
                [this, master, pathList, sourceLabel, clearButton, name]() {
                    importCalibrationMaster(
                        *master, *pathList, sourceLabel, clearButton, name);
                });
        connect(clearButton, &QPushButton::clicked, this,
                [this, pathList, master, sourceLabel, clearButton]() {
                    clearCalibrationSource(
                        *pathList, *master, sourceLabel, clearButton);
                });
        row->addWidget(clearButton);

        calibrationLayout->addWidget(rowWidget);
        return rowWidget;
    };

    addCalibrationRow(
        "Dark",
        QCoreApplication::translate("Nebulens", "暗场"),
        QCoreApplication::translate("Nebulens", "校正热噪声、固定图样噪声和热像素"),
        m_darkFramePaths, m_masterDarkPath,
        m_darkFrameCount, m_darkFrameClear);
    addCalibrationRow(
        "Flat",
        QCoreApplication::translate("Nebulens", "平场"),
        QCoreApplication::translate("Nebulens", "校正暗角、灰尘阴影和像场亮度不均"),
        m_flatFramePaths, m_masterFlatPath,
        m_flatFrameCount, m_flatFrameClear);
    m_biasFrameRow = addCalibrationRow(
        "Bias",
        QCoreApplication::translate("Nebulens", "偏置场"),
        QCoreApplication::translate("Nebulens", "校正传感器读出偏置，并为平场标定零点"),
        m_biasFramePaths, m_masterBiasPath,
        m_biasFrameCount, m_biasFrameClear);
    m_darkFlatFrameRow = addCalibrationRow(
        "DarkFlat",
        QCoreApplication::translate("Nebulens", "暗平场"),
        QCoreApplication::translate("Nebulens", "匹配 Flat 曝光，用于替代 Bias 校准平场"),
        m_darkFlatFramePaths, m_masterDarkFlatPath,
        m_darkFlatFrameCount, m_darkFlatFrameClear);

    m_saveGeneratedMastersCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "保存本次生成的 Master"), m_calibrationGroup);
    m_saveGeneratedMastersCheck->setObjectName(QStringLiteral("saveGeneratedMasters"));
    m_saveGeneratedMastersCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "把本次由 RAW 原片组合成的 Master 保存为 .spmaster，便于以后直接复用"));
    m_saveGeneratedMastersCheck->setChecked(false);
    m_saveGeneratedMastersCheck->setVisible(false);
    m_saveGeneratedMastersCheck->setEnabled(false);
    connect(m_saveGeneratedMastersCheck, &QCheckBox::toggled,
            this, [this]() { emitParamsChanged(); });
    calibrationLayout->addWidget(m_saveGeneratedMastersCheck);

    m_calibrationDetailsButton = new QToolButton(m_calibrationGroup);
    m_calibrationDetailsButton->setObjectName(QStringLiteral("calibrationDetailsButton"));
    m_calibrationDetailsButton->setText(
        QCoreApplication::translate("Nebulens", "校准详情"));
    m_calibrationDetailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_calibrationDetailsButton->setArrowType(Qt::RightArrow);
    m_calibrationDetailsButton->setCheckable(true);
    m_calibrationDetailsButton->setFocusPolicy(Qt::StrongFocus);
    m_calibrationDetailsButton->setCursor(Qt::PointingHandCursor);
    m_calibrationDetailsButton->setProperty(StyleTokens::Properties::kVariant,
                                           StyleTokens::Properties::kGhost);
    calibrationLayout->addWidget(m_calibrationDetailsButton, 0, Qt::AlignLeft);
    m_calibrationDetails = new QLabel(
        QCoreApplication::translate("Nebulens",
            "每类校准帧可选一组 RAW（至少 3 张，建议 10–20 张），或一个 Nebulens .spmaster。"
            "Master Flat 必须已完成偏移校准与 CFA 分相归一化；若 Master Dark 已减偏置，亮场仍需偏置场。"
            "处理前会检查相机、尺寸、ISO、曝光、CFA 和 Master 的校准状态。"),
        m_calibrationGroup);
    m_calibrationDetails->setObjectName(QStringLiteral("calibrationDetails"));
    m_calibrationDetails->setWordWrap(true);
    setRole(m_calibrationDetails, "note");
    calibrationLayout->addWidget(m_calibrationDetails);
    connect(m_calibrationDetailsButton, &QToolButton::toggled, this, [this](bool expanded) {
        m_calibrationDetailsButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_calibrationDetails->setVisible(expanded && m_calibrationGroup->isChecked());
    });
    updateCalibrationStatus();

    stackPageLayout->addWidget(m_calibrationGroup);

    // 对齐组（默认展开）
    m_alignGroup = createCollapsibleGroup(QCoreApplication::translate("Nebulens", "对齐"), true);
    auto* alignLayout = new QVBoxLayout(m_alignGroup);
    alignLayout->setSpacing(8);

    auto* refRow = new QHBoxLayout();
    auto* refLabel = new QLabel(QCoreApplication::translate("Nebulens", "参考帧:"), m_alignGroup);
    refLabel->setFixedWidth(formLabelWidth());
    refLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(refLabel, "muted");
    refRow->addWidget(refLabel);
    m_refFrame = new QComboBox(m_alignGroup);
    m_refFrame->setObjectName(QStringLiteral("referenceFrameCombo"));
    m_refFrame->setAccessibleName(refLabel->text());
    m_refFrame->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_refFrame->setMinimumContentsLength(10);
    updateRefFrameList({});
    connect(m_refFrame, &QComboBox::activated, this, [this](int index) {
        const QString choice = m_refFrame->itemData(index).toString();
        if (choice == m_referenceChoice) return;
        m_referenceChoice = choice;
        emit referenceChoiceChanged(choice);
        emitParamsChanged();
    });
    refRow->addWidget(m_refFrame, 1);
    alignLayout->addLayout(refRow);

    stackPageLayout->addWidget(m_alignGroup);

    // 堆栈组（默认展开）
    m_stackGroup = createCollapsibleGroup(QCoreApplication::translate("Nebulens", "堆栈与地景"), true);
    auto* stackLayout = new QVBoxLayout(m_stackGroup);
    stackLayout->setSpacing(8);

    auto* algoRow = new QHBoxLayout();
    auto* algoLabel = new QLabel(QCoreApplication::translate("Nebulens", "算法:"), m_stackGroup);
    algoLabel->setFixedWidth(formLabelWidth());
    algoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(algoLabel, "muted");
    algoRow->addWidget(algoLabel);
    m_stackAlgorithm = new QComboBox(m_stackGroup);
    m_stackAlgorithm->addItems({QCoreApplication::translate("Nebulens", "中位数 Median"),
                                QCoreApplication::translate("Nebulens", "平均值 Mean"),
                                "Kappa-Sigma", "Winsorized"});
    m_stackAlgorithm->setToolTip(
        QCoreApplication::translate("Nebulens", "选择堆栈降噪算法：\n"
        "• Median：取中位数，简单鲁棒，适合 ≤5 帧\n"
        "• Mean：取平均值，信噪比最高但抗异常差\n"
        "• Kappa-Sigma：迭代剔除异常值，适合 6-15 帧\n"
        "• Winsorized：用 MAD 替代标准差，适合 >15 帧")
    );
    connect(m_stackAlgorithm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_userChangedStackMethod = true;
        updateStackMethodDescription();
        onComboChanged(index);
    });
    algoRow->addWidget(m_stackAlgorithm, 1);
    stackLayout->addLayout(algoRow);

    m_stackMethodDescription = new QLabel(m_stackGroup);
    m_stackMethodDescription->setTextFormat(Qt::RichText);
    m_stackMethodDescription->setWordWrap(true);
    setRole(m_stackMethodDescription, "note");
    m_stackMethodDescription->setMinimumHeight(58);
    stackLayout->addWidget(m_stackMethodDescription);

    auto* kappaRow = new QHBoxLayout();
    m_kappaNameLabel = new QLabel(QCoreApplication::translate("Nebulens", "κ值:"), m_stackGroup);
    m_kappaNameLabel->setFixedWidth(formLabelWidth());
    m_kappaNameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(m_kappaNameLabel, "muted");
    m_kappaNameLabel->setToolTip(QCoreApplication::translate("Nebulens", "κ (kappa)：异常值剔除阈值系数\n"
        "• 值越小，剔除越严格，可能误删微弱星点\n"
        "• 值越大，保留越多，可能残留飞机轨迹\n"
        "• 推荐值：2.0~3.0，深空常用 2.5"));
    kappaRow->addWidget(m_kappaNameLabel);
    m_kappaSlider = createSlider(10, 50, 25);
    m_kappaSlider->setMinimumWidth(96);
    m_kappaSlider->setToolTip(QCoreApplication::translate("Nebulens", "拖动调整 κ 值，值越小剔除越严格"));
    connect(m_kappaSlider, &QSlider::valueChanged, this, [this](int v) {
        m_kappaLabel->setText(QString::number(v / 10.0, 'f', 1));
    });
    connect(m_kappaSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    kappaRow->addWidget(m_kappaSlider, 1);
    m_kappaLabel = new QLabel("2.5", m_stackGroup);
    setRole(m_kappaLabel, "value");
    m_kappaLabel->setMinimumWidth(24);
    kappaRow->addWidget(m_kappaLabel);
    stackLayout->addLayout(kappaRow);
    updateStackMethodDescription();

    m_autoRejectQualityCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "自动排除严重差帧"), m_stackGroup);
    m_autoRejectQualityCheck->setChecked(true);
    m_autoRejectQualityCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "通过轻量预览识别明显失焦、拖星或云层遮挡的离群帧；正常差异不会自动删除"));
    connect(m_autoRejectQualityCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    stackLayout->addWidget(m_autoRejectQualityCheck);

    m_photometricCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "帧间光度匹配"), m_stackGroup);
    m_photometricCheck->setChecked(true);
    m_photometricCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "将每帧曝光和背景色偏匹配到参考帧，减少薄云、光污染变化造成的堆栈斑块"));
    connect(m_photometricCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    stackLayout->addWidget(m_photometricCheck);

    // 天地分离
    auto* skyGroundRow = new QHBoxLayout();
    m_skyGroundCheck = new CompactToggle(QCoreApplication::translate("Nebulens", "天地分离"), m_stackGroup);
    m_skyGroundCheck->setToolTip(QCoreApplication::translate("Nebulens", "不带赤道仪时，天空对齐星点，地景保持固定，避免地景拖影"));
    connect(m_skyGroundCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    connect(m_skyGroundCheck, &QCheckBox::toggled, this, [this]() {
        updateSkyGroundControls();
    });
    skyGroundRow->addWidget(m_skyGroundCheck);
    stackLayout->addLayout(skyGroundRow);

    // 模式选择
    auto* modeRow = new QHBoxLayout();
    m_skyGroundMode = new QComboBox(m_stackGroup);
    m_skyGroundMode->setObjectName(QStringLiteral("skyGroundModeCombo"));
    m_skyGroundMode->addItems({"自动检测", "用户蒙版"});
    m_skyGroundMode->setEnabled(false);
    connect(m_skyGroundMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index)
        updateSkyGroundControls();
        markPresetCustom();
        emitParamsChanged();
    });
    m_skyGroundModeLabel = new QLabel(QCoreApplication::translate("Nebulens", "模式:"), m_stackGroup);
    m_skyGroundModeLabel->setFixedWidth(formLabelWidth());
    m_skyGroundModeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(m_skyGroundModeLabel, "muted");
    modeRow->addWidget(m_skyGroundModeLabel);
    modeRow->addWidget(m_skyGroundMode, 1);
    stackLayout->addLayout(modeRow);

    // 按钮行
    auto* btnRow = new QHBoxLayout();
    m_detectMaskBtn = new QPushButton(QCoreApplication::translate("Nebulens", "检测地景"), m_stackGroup);
    m_detectMaskBtn->setVisible(false);
    connect(m_detectMaskBtn, &QPushButton::clicked, this, &ParamsPanel::maskPreviewRequested);
    btnRow->addWidget(m_detectMaskBtn);

    m_importMaskBtn = new QPushButton(QCoreApplication::translate("Nebulens", "导入蒙版..."), m_stackGroup);
    m_importMaskBtn->setObjectName(QStringLiteral("importMaskButton"));
    m_importMaskBtn->setVisible(false);
    connect(m_importMaskBtn, &QPushButton::clicked, this, [this]() {
        QString path = FileDialogUtils::getOpenFileName(
            this, QCoreApplication::translate("Nebulens", "选择蒙版"),
            QString(),
            QCoreApplication::translate(
                "Nebulens", "图像 (*.png *.jpg *.bmp)"));
        if (!path.isEmpty()) {
            m_userMaskPath = path;
            m_maskPathLabel->setText(QFileInfo(path).fileName());
            m_maskPathLabel->setVisible(true);
            markPresetCustom();
            emitParamsChanged();
        }
    });
    btnRow->addWidget(m_importMaskBtn);
    stackLayout->addLayout(btnRow);

    m_maskPathLabel = new QLabel(m_stackGroup);
    m_maskPathLabel->setTextFormat(Qt::PlainText);
    m_maskPathLabel->setVisible(false);
    setRole(m_maskPathLabel, "muted");
    stackLayout->addWidget(m_maskPathLabel);

    // 羽化宽度
    auto* featherRow = new QHBoxLayout();
    m_featherSlider = createSlider(0, 50, 20);
    m_featherSlider->setEnabled(false);
    m_featherSlider->setMinimumWidth(96);
    m_featherLabel = new QLabel("20 px", m_stackGroup);
    setRole(m_featherLabel, "value");
    m_featherLabel->setMinimumWidth(38);
    connect(m_featherSlider, &QSlider::valueChanged, this, [this](int value) {
        m_featherLabel->setText(QString("%1 px").arg(value));
        onSliderValueChanged(value);
    });
    connect(m_featherSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    m_featherNameLabel = new QLabel(QCoreApplication::translate("Nebulens", "羽化:"), m_stackGroup);
    m_featherNameLabel->setFixedWidth(formLabelWidth());
    m_featherNameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(m_featherNameLabel, "muted");
    featherRow->addWidget(m_featherNameLabel);
    featherRow->addWidget(m_featherSlider, 1);
    featherRow->addWidget(m_featherLabel);
    stackLayout->addLayout(featherRow);

    // Ground frames are deliberately not aligned to the sky. Average is the
    // stable default; the other modes are useful when local motion or absolute
    // sharpness matters more than foreground noise.
    auto* groundMethodRow = new QHBoxLayout();
    m_groundStackMethod = new QComboBox(m_stackGroup);
    m_groundStackMethod->addItem(QCoreApplication::translate("Nebulens", "平均降噪（推荐）"), "average");
    m_groundStackMethod->addItem(QCoreApplication::translate("Nebulens", "参考帧（单帧）"), "reference");
    m_groundStackMethod->addItem(QCoreApplication::translate("Nebulens", "中值（运动地景）"), "median");
    m_groundStackMethod->setEnabled(false);
    m_groundStackMethod->setToolTip(QCoreApplication::translate("Nebulens",
        "平均降噪适合静止地景；参考单帧更锐但噪声和光照可能不同；中值可抑制短暂人物或车灯"));
    connect(m_groundStackMethod,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParamsPanel::onComboChanged);
    m_groundStackNameLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "地景合成:"), m_stackGroup);
    m_groundStackNameLabel->setFixedWidth(formLabelWidth());
    m_groundStackNameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(m_groundStackNameLabel, "muted");
    groundMethodRow->addWidget(m_groundStackNameLabel);
    groundMethodRow->addWidget(m_groundStackMethod, 1);
    stackLayout->addLayout(groundMethodRow);

    auto* groundDetailRow = new QHBoxLayout();
    m_groundDetailSlider = createSlider(0, 70, 40);
    m_groundDetailSlider->setEnabled(false);
    m_groundDetailSlider->setMinimumWidth(96);
    m_groundDetailSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "恢复山体和建筑的纹理与中尺度清晰度；值过高会同时增强地景噪声"));
    m_groundDetailLabel = new QLabel("40%", m_stackGroup);
    setRole(m_groundDetailLabel, "value");
    m_groundDetailLabel->setMinimumWidth(32);
    connect(m_groundDetailSlider, &QSlider::valueChanged, this,
            [this](int value) {
                m_groundDetailLabel->setText(QString::number(value) + "%");
                onSliderValueChanged(value);
            });
    connect(m_groundDetailSlider, &QSlider::sliderReleased,
            this, &ParamsPanel::onSliderReleased);
    m_groundDetailNameLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "地景细节:"), m_stackGroup);
    m_groundDetailNameLabel->setFixedWidth(formLabelWidth());
    m_groundDetailNameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(m_groundDetailNameLabel, "muted");
    groundDetailRow->addWidget(m_groundDetailNameLabel);
    groundDetailRow->addWidget(m_groundDetailSlider, 1);
    groundDetailRow->addWidget(m_groundDetailLabel);
    stackLayout->addLayout(groundDetailRow);

    stackPageLayout->addWidget(m_stackGroup);

    m_starTrailGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "星轨合成"), true);
    m_starTrailGroup->setVisible(false);
    auto* starTrailLayout = new QVBoxLayout(m_starTrailGroup);
    starTrailLayout->setSpacing(10);

    auto* starTrailNote = new QLabel(
        QCoreApplication::translate("Nebulens",
            "固定机位序列保持原坐标，不进行星点对齐。天空逐帧取亮形成轨迹；开启地景保护后，山体和建筑使用原坐标均值降噪。"),
        m_starTrailGroup);
    starTrailNote->setWordWrap(true);
    setRole(starTrailNote, "noteWarm");
    starTrailLayout->addWidget(starTrailNote);

    auto* cometRow = new QHBoxLayout();
    auto* cometName = new QLabel(
        QCoreApplication::translate("Nebulens", "彗星拖尾:"), m_starTrailGroup);
    cometName->setFixedWidth(formLabelWidth());
    cometName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(cometName, "muted");
    m_starTrailCometSlider = createSlider(0, 100, 0);
    m_starTrailCometSlider->setMinimumWidth(96);
    m_starTrailCometSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "0% 为每帧等亮的连续星轨；提高后，较早帧逐渐变暗，形成一端明、一端暗的彗星效果"));
    m_starTrailCometLabel = new QLabel("0%", m_starTrailGroup);
    setRole(m_starTrailCometLabel, "value");
    m_starTrailCometLabel->setMinimumWidth(32);
    connect(m_starTrailCometSlider, &QSlider::valueChanged, this,
            [this](int value) {
                m_starTrailCometLabel->setText(QString::number(value) + "%");
                if (m_starTrailReverseCheck) {
                    m_starTrailReverseCheck->setEnabled(value > 0);
                }
                onSliderValueChanged(value);
            });
    connect(m_starTrailCometSlider, &QSlider::sliderReleased,
            this, &ParamsPanel::onSliderReleased);
    cometRow->addWidget(cometName);
    cometRow->addWidget(m_starTrailCometSlider, 1);
    cometRow->addWidget(m_starTrailCometLabel);
    starTrailLayout->addLayout(cometRow);

    m_starTrailReverseCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "反转彗星方向"), m_starTrailGroup);
    m_starTrailReverseCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "交换星轨亮端和暗端；连续星轨（0%）不受影响"));
    m_starTrailReverseCheck->setEnabled(false);
    connect(m_starTrailReverseCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    starTrailLayout->addWidget(m_starTrailReverseCheck);

    m_starTrailProtectGroundCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "保护固定地景"), m_starTrailGroup);
    m_starTrailProtectGroundCheck->setChecked(true);
    m_starTrailProtectGroundCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "自动检测地平线；天空合成星轨，地景在相机坐标中平均降噪，避免灯光取亮和前景发糊"));
    connect(m_starTrailProtectGroundCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    starTrailLayout->addWidget(m_starTrailProtectGroundCheck);
    stackPageLayout->addWidget(m_starTrailGroup);

    m_timelapseGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "滑动窗口降噪"), true);
    m_timelapseGroup->setVisible(false);
    auto* timelapseLayout = new QVBoxLayout(m_timelapseGroup);
    timelapseLayout->setSpacing(10);

    auto* windowRow = new QHBoxLayout();
    auto* windowLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "邻近窗口:"), m_timelapseGroup);
    windowLabel->setFixedWidth(formLabelWidth());
    windowLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(windowLabel, "muted");
    m_timelapseWindow = new QComboBox(m_timelapseGroup);
    m_timelapseWindow->addItem(QCoreApplication::translate("Nebulens", "3 帧（更快）"), 3);
    m_timelapseWindow->addItem(QCoreApplication::translate("Nebulens", "5 帧（更干净）"), 5);
    m_timelapseWindow->setCurrentIndex(1);
    m_timelapseWindow->setToolTip(QCoreApplication::translate("Nebulens",
        "每张输出以自身为中心，使用前后邻近 RAW 对齐降噪；序列两端自动缩短窗口"));
    connect(m_timelapseWindow,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParamsPanel::onComboChanged);
    windowRow->addWidget(windowLabel);
    windowRow->addWidget(m_timelapseWindow, 1);
    timelapseLayout->addLayout(windowRow);

    auto* temporalStrengthRow = new QHBoxLayout();
    auto* temporalStrengthName = new QLabel(
        QCoreApplication::translate("Nebulens", "时域强度:"), m_timelapseGroup);
    temporalStrengthName->setFixedWidth(formLabelWidth());
    temporalStrengthName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(temporalStrengthName, "muted");
    temporalStrengthRow->addWidget(temporalStrengthName);
    m_timelapseStrengthSlider = createSlider(0, 100, 80);
    m_timelapseStrengthSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "控制邻近帧对当前帧的贡献；值越高降噪越强，动态云层应适当降低"));
    m_timelapseStrengthLabel = new QLabel("80%", m_timelapseGroup);
    setRole(m_timelapseStrengthLabel, "value");
    m_timelapseStrengthLabel->setMinimumWidth(32);
    connect(m_timelapseStrengthSlider, &QSlider::valueChanged, this,
            [this](int value) {
                m_timelapseStrengthLabel->setText(QString::number(value) + "%");
                onSliderValueChanged(value);
            });
    connect(m_timelapseStrengthSlider, &QSlider::sliderReleased,
            this, &ParamsPanel::onSliderReleased);
    temporalStrengthRow->addWidget(m_timelapseStrengthSlider, 1);
    temporalStrengthRow->addWidget(m_timelapseStrengthLabel);
    timelapseLayout->addLayout(temporalStrengthRow);

    auto* motionProtectionRow = new QHBoxLayout();
    auto* motionProtectionName = new QLabel(
        QCoreApplication::translate("Nebulens", "动态内容保护:"), m_timelapseGroup);
    motionProtectionName->setFixedWidth(formLabelWidth());
    motionProtectionName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(motionProtectionName, "muted");
    motionProtectionRow->addWidget(motionProtectionName);
    m_timelapseMotionProtectionSlider = createSlider(0, 100, 75);
    m_timelapseMotionProtectionSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "在云层、草木、灯光等变化区域降低邻帧贡献，并偏向保留目标帧；值越高保护越强"));
    m_timelapseMotionProtectionLabel = new QLabel("75%", m_timelapseGroup);
    setRole(m_timelapseMotionProtectionLabel, "value");
    m_timelapseMotionProtectionLabel->setMinimumWidth(32);
    connect(m_timelapseMotionProtectionSlider, &QSlider::valueChanged, this,
            [this](int value) {
                m_timelapseMotionProtectionLabel->setText(
                    QString::number(value) + "%");
                onSliderValueChanged(value);
            });
    connect(m_timelapseMotionProtectionSlider, &QSlider::sliderReleased,
            this, &ParamsPanel::onSliderReleased);
    motionProtectionRow->addWidget(m_timelapseMotionProtectionSlider, 1);
    motionProtectionRow->addWidget(m_timelapseMotionProtectionLabel);
    timelapseLayout->addLayout(motionProtectionRow);

    auto* motionProtectionNote = new QLabel(
        QCoreApplication::translate("Nebulens",
            "变化区域会自动减少邻帧混合，优先保留当前目标帧。"),
        m_timelapseGroup);
    motionProtectionNote->setWordWrap(true);
    setRole(motionProtectionNote, "muted");
    timelapseLayout->addWidget(motionProtectionNote);

    m_timelapseProtectGroundCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "固定地景保持原位"), m_timelapseGroup);
    m_timelapseProtectGroundCheck->setChecked(true);
    m_timelapseProtectGroundCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "天空按星点对齐，山体和建筑在相机坐标中降噪，再沿地平线融合"));
    connect(m_timelapseProtectGroundCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    timelapseLayout->addWidget(m_timelapseProtectGroundCheck);

    auto* outputNote = new QLabel(
        QCoreApplication::translate("Nebulens", "每张输入 RAW 对应一张带原文件名的输出图片。"),
        m_timelapseGroup);
    outputNote->setWordWrap(true);
    setRole(outputNote, "note");
    timelapseLayout->addWidget(outputNote);
    stackPageLayout->addWidget(m_timelapseGroup);
    stackPageLayout->addStretch();

    // 基础调整与快速预览共用同一套参数和收尾处理管线。
    m_basicAdjustGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "基础调色"), true);
    auto* basicAdjustLayout = new QVBoxLayout(m_basicAdjustGroup);
    basicAdjustLayout->setSpacing(7);

    auto addSectionLabel = [this, basicAdjustLayout](const QString& text) {
        auto* label = new QLabel(text, m_basicAdjustGroup);
        setRole(label, "muted");
        label->setContentsMargins(0, 5, 0, 0);
        basicAdjustLayout->addWidget(label);
    };
    auto addAdjustmentRow = [this, basicAdjustLayout](
                                const QString& name, int minimum, int maximum,
                                QSlider*& slider, QLabel*& valueLabel,
                                const QString& tooltip, bool exposure = false) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        auto* nameLabel = new QLabel(name, m_basicAdjustGroup);
        nameLabel->setFixedWidth(formLabelWidth());
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setRole(nameLabel, "muted");
        nameLabel->setToolTip(tooltip);
        row->addWidget(nameLabel);
        slider = createSlider(minimum, maximum, 0);
        slider->setMinimumWidth(96);
        slider->setToolTip(tooltip);
        valueLabel = new QLabel(
            exposure ? exposureText(0) : signedAdjustmentText(0),
            m_basicAdjustGroup);
        setRole(valueLabel, "value");
        valueLabel->setMinimumWidth(exposure ? 48 : 30);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QLabel* const capturedLabel = valueLabel;
        connect(slider, &QSlider::valueChanged, this,
                [this, capturedLabel, exposure](int value) {
                    capturedLabel->setText(
                        exposure ? exposureText(value)
                                 : signedAdjustmentText(value));
                    onSliderValueChanged(value);
                });
        connect(slider, &QSlider::sliderReleased,
                this, &ParamsPanel::onSliderReleased);
        row->addWidget(slider, 1);
        row->addWidget(valueLabel);
        basicAdjustLayout->addLayout(row);
    };

    addSectionLabel(QCoreApplication::translate("Nebulens", "白平衡"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "色温偏移"), -100, 100,
                     m_temperatureSlider, m_temperatureLabel,
                     QCoreApplication::translate("Nebulens", "向左冷却画面，向右增加暖色；这是相对偏移，不代表绝对 Kelvin"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "色调"), -100, 100,
                     m_tintSlider, m_tintLabel,
                     QCoreApplication::translate("Nebulens", "在绿色与洋红之间校正色偏"));

    addSectionLabel(QCoreApplication::translate("Nebulens", "光线"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "曝光"), -50, 50,
                     m_exposureSlider, m_exposureLabel,
                     QCoreApplication::translate("Nebulens", "整体曝光补偿，范围 -5.0 EV 到 +5.0 EV"), true);
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "对比度"), -100, 100,
                     m_contrastSlider, m_contrastLabel,
                     QCoreApplication::translate("Nebulens", "围绕中间调压缩或扩展明暗层次"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "高光"), -100, 100,
                     m_highlightsSlider, m_highlightsLabel,
                     QCoreApplication::translate("Nebulens", "主要调整亮部，并保护中间调和黑位"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "阴影"), -100, 100,
                     m_shadowsSlider, m_shadowsLabel,
                     QCoreApplication::translate("Nebulens", "主要调整暗部，并保护高光"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "白色色阶"), -100, 100,
                     m_whitesSlider, m_whitesLabel,
                     QCoreApplication::translate("Nebulens", "设置最亮区域的视觉强度"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "黑色色阶"), -100, 100,
                     m_blacksSlider, m_blacksLabel,
                     QCoreApplication::translate("Nebulens", "设置最暗区域的视觉深度"));

    addSectionLabel(QCoreApplication::translate("Nebulens", "颜色"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "自然饱和度"), -100, 100,
                     m_vibranceSlider, m_vibranceLabel,
                     QCoreApplication::translate("Nebulens", "优先增强低饱和颜色，降低鲜艳区域溢色风险"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "饱和度"), -100, 100,
                     m_saturationSlider, m_saturationLabel,
                     QCoreApplication::translate("Nebulens", "统一调整全部颜色的饱和程度"));

    addSectionLabel(QCoreApplication::translate("Nebulens", "细节"));
    addAdjustmentRow(QCoreApplication::translate("Nebulens", "锐化"), 0, 100,
                     m_sharpeningSlider, m_sharpeningLabel,
                     QCoreApplication::translate("Nebulens", "亮度通道阈值锐化，抑制暗部噪声和彩色边缘"));
    adjustPageLayout->addWidget(m_basicAdjustGroup);

    // 自动优化组（默认展开）
    m_optimizeGroup = createCollapsibleGroup(QCoreApplication::translate("Nebulens", "降噪与增强"), true);
    auto* optimizeLayout = new QVBoxLayout(m_optimizeGroup);
    optimizeLayout->setSpacing(8);

    auto* noiseReductionRow = new QHBoxLayout();
    m_noiseReductionCheck = new CompactToggle(QCoreApplication::translate("Nebulens", "多尺度降噪"), m_optimizeGroup);
    m_noiseReductionCheck->setMinimumWidth(106);
    m_noiseReductionCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "在线性堆栈结果上抑制亮度和色彩噪声\n"
        "建议在去雾和曲线拉伸之前使用"));
    connect(m_noiseReductionCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    noiseReductionRow->addWidget(m_noiseReductionCheck);
    m_noiseReductionSlider = createSlider(0, 70, 30);
    m_noiseReductionSlider->setEnabled(false);
    m_noiseReductionSlider->setMinimumWidth(84);
    m_noiseReductionLabel = new QLabel("30%", m_optimizeGroup);
    setRole(m_noiseReductionLabel, "value");
    m_noiseReductionLabel->setMinimumWidth(32);
    m_noiseReductionSlider->setToolTip(QCoreApplication::translate("Nebulens", "降噪强度：0 为保留全部细节，70 为最强"));
    connect(m_noiseReductionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_noiseReductionLabel->setText(QString("%1%").arg(value));
        onSliderValueChanged(value);
    });
    connect(m_noiseReductionSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    noiseReductionRow->addWidget(m_noiseReductionSlider, 1);
    noiseReductionRow->addWidget(m_noiseReductionLabel);
    optimizeLayout->addLayout(noiseReductionRow);

    m_modifiedCameraColorCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "BCF 改机色彩还原"), m_optimizeGroup);
    m_modifiedCameraColorCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "适用于 BCF 或天文改机造成的整体红偏\n"
        "自动采样中性天空并校正通道响应；普通相机请关闭"));
    connect(m_modifiedCameraColorCheck, &QCheckBox::toggled,
            this, [this](bool checked) {
                updateModifiedCameraColorControls();
                onCheckChanged(checked ? Qt::Checked : Qt::Unchecked);
            });
    optimizeLayout->addWidget(m_modifiedCameraColorCheck);

    auto* modifiedStrengthRow = new QHBoxLayout();
    auto* modifiedStrengthName = new QLabel(
        QCoreApplication::translate("Nebulens", "校正强度"), m_optimizeGroup);
    modifiedStrengthName->setFixedWidth(formLabelWidth());
    modifiedStrengthName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(modifiedStrengthName, "muted");
    modifiedStrengthRow->addWidget(modifiedStrengthName);
    m_modifiedCameraColorStrengthSlider = createSlider(0, 100, 100);
    m_modifiedCameraColorStrengthSlider->setMinimumWidth(84);
    m_modifiedCameraColorStrengthSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "0 为保留原始改机色偏，100 为完整回归中性色彩"));
    connect(m_modifiedCameraColorStrengthSlider, &QSlider::valueChanged,
            this, [this](int value) {
                m_modifiedCameraColorStrengthLabel->setText(
                    QString::number(value) + "%");
                onSliderValueChanged(value);
            });
    connect(m_modifiedCameraColorStrengthSlider, &QSlider::sliderReleased,
            this, &ParamsPanel::onSliderReleased);
    modifiedStrengthRow->addWidget(
        m_modifiedCameraColorStrengthSlider, 1);
    m_modifiedCameraColorStrengthLabel = new QLabel("100%", m_optimizeGroup);
    setRole(m_modifiedCameraColorStrengthLabel, "value");
    m_modifiedCameraColorStrengthLabel->setMinimumWidth(32);
    modifiedStrengthRow->addWidget(m_modifiedCameraColorStrengthLabel);
    optimizeLayout->addLayout(modifiedStrengthRow);

    auto* modifiedSampleRow = new QHBoxLayout();
    m_modifiedCameraColorMode = new QComboBox(m_optimizeGroup);
    m_modifiedCameraColorMode->addItem(QCoreApplication::translate("Nebulens", "自动灰点"));
    m_modifiedCameraColorMode->addItem(QCoreApplication::translate("Nebulens", "手动灰点"));
    m_modifiedCameraColorMode->setToolTip(QCoreApplication::translate("Nebulens",
        "自动模式从中性天空估计；手动模式从预览中选择应为灰色的区域"));
    connect(m_modifiedCameraColorMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                updateModifiedCameraColorControls();
                if (index == 1 && !hasModifiedCameraGrayPoint()) {
                    {
                        const QSignalBlocker blocker(
                            m_modifiedCameraColorMode);
                        m_modifiedCameraColorMode->setCurrentIndex(0);
                    }
                    updateModifiedCameraColorControls();
                    emit modifiedCameraGrayPointRequested();
                    return;
                }
                onComboChanged(index);
            });
    modifiedSampleRow->addWidget(m_modifiedCameraColorMode, 1);
    m_modifiedCameraGrayPointButton = new QPushButton(m_optimizeGroup);
    m_modifiedCameraGrayPointButton->setIcon(
        UiAssets::icon(
            UiAssets::Glyph::Eyedropper,
            StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kTextSecondary)));
    m_modifiedCameraGrayPointButton->setIconSize(QSize(16, 16));
    m_modifiedCameraGrayPointButton->setFixedSize(30, 28);
    m_modifiedCameraGrayPointButton->setToolTip(
        QCoreApplication::translate("Nebulens", "在结果预览中重新采样手动灰点"));
    m_modifiedCameraGrayPointButton->setAccessibleName(
        m_modifiedCameraGrayPointButton->toolTip());
    connect(m_modifiedCameraGrayPointButton, &QPushButton::clicked,
            this, &ParamsPanel::modifiedCameraGrayPointRequested);
    modifiedSampleRow->addWidget(m_modifiedCameraGrayPointButton);
    optimizeLayout->addLayout(modifiedSampleRow);

    m_modifiedCameraGrayPointStatus = new QLabel(m_optimizeGroup);
    m_modifiedCameraGrayPointStatus->setWordWrap(true);
    setRole(m_modifiedCameraGrayPointStatus, "muted");
    optimizeLayout->addWidget(m_modifiedCameraGrayPointStatus);
    updateModifiedCameraColorControls();

    auto* dewarpRow = new QHBoxLayout();
    m_dewarpCheck = new CompactToggle(QCoreApplication::translate("Nebulens", "去雾"), m_optimizeGroup);
    m_dewarpCheck->setMinimumWidth(106);
    m_dewarpCheck->setEnabled(true);
    m_dewarpCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "亮度引导的 Dark Channel Prior 去雾\n"
        "适合明显薄雾；银河暗尘丰富时建议关闭"));
    connect(m_dewarpCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    dewarpRow->addWidget(m_dewarpCheck);
    m_dewarpSlider = createSlider(0, 100, 30);
    m_dewarpSlider->setEnabled(true);
    m_dewarpSlider->setMinimumWidth(84);
    m_dewarpLabel = new QLabel("30%", m_optimizeGroup);
    setRole(m_dewarpLabel, "value");
    m_dewarpLabel->setMinimumWidth(32);
    connect(m_dewarpSlider, &QSlider::valueChanged, this, [this](int value) {
        m_dewarpLabel->setText(QString("%1%").arg(value));
        onSliderValueChanged(value);
    });
    connect(m_dewarpSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    dewarpRow->addWidget(m_dewarpSlider, 1);
    dewarpRow->addWidget(m_dewarpLabel);
    optimizeLayout->addLayout(dewarpRow);

    m_stretchCheck = new CompactToggle(QCoreApplication::translate("Nebulens", "曲线拉伸"), m_optimizeGroup);
    m_stretchCheck->setEnabled(true);
    m_stretchCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "背景色偏中和 + RGB 联动 Arcsinh 拉伸\n"
        "增强暗部并保留星点颜色"));
    connect(m_stretchCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    optimizeLayout->addWidget(m_stretchCheck);

    adjustPageLayout->addWidget(m_optimizeGroup);

    // 星点修饰沿用普通无框分区，不再额外绘制标题卡片。
    m_starReduceGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "星点修饰"), true);
    auto* starLayout = new QVBoxLayout(m_starReduceGroup);
    starLayout->setSpacing(10);

    auto* defringeRow = new QHBoxLayout();
    m_starDefringeCheck = new CompactToggle(
        QCoreApplication::translate("Nebulens", "去除星点紫边"), m_starReduceGroup);
    m_starDefringeCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "比较星核与星翼色度，只抑制向边缘突增的紫、蓝或绿色边\n"
        "不会改变星点尺寸，也不会全局降低饱和度"));
    connect(m_starDefringeCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    defringeRow->addWidget(m_starDefringeCheck);
    starLayout->addLayout(defringeRow);

    auto* defringeStrengthRow = new QHBoxLayout();
    auto* defringeStrengthLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "去边强度:"), m_starReduceGroup);
    defringeStrengthLabel->setFixedWidth(formLabelWidth());
    defringeStrengthLabel->setAlignment(
        Qt::AlignRight | Qt::AlignVCenter);
    setRole(defringeStrengthLabel, "muted");
    defringeStrengthRow->addWidget(defringeStrengthLabel);
    m_starDefringeSlider = createSlider(0, 100, 55);
    m_starDefringeSlider->setEnabled(false);
    m_starDefringeSlider->setMinimumWidth(96);
    m_starDefringeLabel = new QLabel("55%", m_starReduceGroup);
    setRole(m_starDefringeLabel, "value");
    m_starDefringeLabel->setMinimumWidth(32);
    m_starDefringeLabel->setEnabled(false);
    m_starDefringeSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "40-60 适合普通镜头色差；更高强度用于明显的蓝紫星翼\n"
        "建议在 100% 预览下确认真实蓝星仍保留颜色"));
    connect(m_starDefringeSlider, &QSlider::valueChanged,
            this, [this](int value) {
                m_starDefringeLabel->setText(QString("%1%").arg(value));
                onSliderValueChanged(value);
            });
    connect(m_starDefringeSlider, &QSlider::sliderReleased,
            this, &ParamsPanel::onSliderReleased);
    defringeStrengthRow->addWidget(m_starDefringeSlider, 1);
    defringeStrengthRow->addWidget(m_starDefringeLabel);
    starLayout->addLayout(defringeStrengthRow);

    auto* starRow = new QHBoxLayout();
    m_starReduceCheck = new CompactToggle(QCoreApplication::translate("Nebulens", "启用缩星"), m_starReduceGroup);
    m_starReduceCheck->setEnabled(true);
    m_starReduceCheck->setToolTip(QCoreApplication::translate("Nebulens",
        "自动建立无星层，仅对星层应用亚像素圆形 Minimum\n"
        "饱和大星保持原样，避免宽光晕形成暗环"));
    connect(m_starReduceCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    starRow->addWidget(m_starReduceCheck);
    starLayout->addLayout(starRow);

    auto* strengthRow = new QHBoxLayout();
    auto* strengthLabel = new QLabel(QCoreApplication::translate("Nebulens", "强度:"), m_starReduceGroup);
    strengthLabel->setFixedWidth(formLabelWidth());
    strengthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(strengthLabel, "muted");
    strengthLabel->setToolTip(QCoreApplication::translate("Nebulens",
        "缩星强度：40 温和，70 强烈，90 接近清星\n"
        "请在 100% 预览下判断，过高会损失星点细节"));
    strengthRow->addWidget(strengthLabel);
    m_starReduceSlider = createSlider(0, 100, 70);
    m_starReduceSlider->setEnabled(false);
    m_starReduceSlider->setMinimumWidth(96);
    m_starReduceLabel = new QLabel("70%", m_starReduceGroup);
    setRole(m_starReduceLabel, "value");
    m_starReduceLabel->setMinimumWidth(32);
    m_starReduceSlider->setToolTip(QCoreApplication::translate("Nebulens",
        "40 温和收紧；70 强烈缩星；90 会清除多数暗弱小星\n"
        "推荐在 100% 预览下调整"));
    connect(m_starReduceSlider, &QSlider::valueChanged, this, [this](int value) {
        m_starReduceLabel->setText(QString("%1%").arg(value));
        onSliderValueChanged(value);
    });
    connect(m_starReduceSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    strengthRow->addWidget(m_starReduceSlider, 1);
    strengthRow->addWidget(m_starReduceLabel);
    starLayout->addLayout(strengthRow);

    adjustPageLayout->addWidget(m_starReduceGroup);
    adjustPageLayout->addStretch();

    // 输出组与其他检查器分区共用同一套无框标题和细分隔线。
    m_outputGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "文件设置"), true);
    auto* outputLayout = new QVBoxLayout(m_outputGroup);
    outputLayout->setSpacing(14);

    auto* outputForm = new QGridLayout();
    outputForm->setContentsMargins(0, 0, 0, 0);
    outputForm->setHorizontalSpacing(12);
    outputForm->setVerticalSpacing(11);
    outputForm->setColumnStretch(1, 1);

    auto* formatLabel = new QLabel(QCoreApplication::translate("Nebulens", "文件格式"), m_outputGroup);
    formatLabel->setFixedWidth(formLabelWidth());
    formatLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(formatLabel, "muted");
    formatLabel->setToolTip(QCoreApplication::translate("Nebulens", "TIFF 16-bit：最高质量，保留完整动态范围（推荐）\nPNG 8-bit：无损压缩，预览/分享首选"));
    outputForm->addWidget(formatLabel, 0, 0);
    m_outputFormat = new QComboBox(m_outputGroup);
    m_outputFormat->setAccessibleName(formatLabel->text());
    m_outputFormat->addItem(QStringLiteral("TIFF 16-bit"), QStringLiteral("tiff16"));
    m_outputFormat->addItem(
        QCoreApplication::translate("Nebulens", "PNG 8-bit (预览)"),
        QStringLiteral("png8"));
    m_outputFormat->setToolTip(QCoreApplication::translate("Nebulens", "输出图像格式，TIFF 16-bit 为推荐默认"));
    connect(m_outputFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    outputForm->addWidget(m_outputFormat, 0, 1, 1, 2);

    // 色彩空间：当前固定输出线性 sRGB，暂不提供选择控件
    // 后续完整实现色彩空间转换后再恢复

    // 输出路径选择
    auto* pathLabel = new QLabel(QCoreApplication::translate("Nebulens", "输出目录"), m_outputGroup);
    pathLabel->setFixedWidth(formLabelWidth());
    pathLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setRole(pathLabel, "muted");
    outputForm->addWidget(pathLabel, 1, 0);
    auto* pathEdit = new QLineEdit(m_outputGroup);
    m_outputPath = pathEdit;
    pathEdit->setReadOnly(true);
    displayOutputPath(
        pathEdit, defaultOutputPath());
    outputForm->addWidget(pathEdit, 1, 1);
    auto* pathBtn = new QPushButton(m_outputGroup);
    pathBtn->setIcon(
        UiAssets::icon(
            UiAssets::Glyph::Folder,
            StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kTextSecondary)));
    pathBtn->setToolTip(QCoreApplication::translate("Nebulens", "选择输出目录"));
    pathBtn->setAccessibleName(pathBtn->toolTip());
    setButtonVariant(pathBtn, "icon");
    connect(pathBtn, &QPushButton::clicked, this, [this]() {
        QString dir = FileDialogUtils::getExistingDirectory(
            this, QCoreApplication::translate("Nebulens", "选择输出目录"),
            outputPath());
        if (!dir.isEmpty()) {
            displayOutputPath(m_outputPath, dir);
            refreshRecentResults();
            markPresetCustom();
            emitParamsChanged();
        }
    });
    outputForm->addWidget(pathBtn, 1, 2);
    outputLayout->addLayout(outputForm);

    outputPageLayout->addWidget(m_outputGroup, 0, Qt::AlignTop);

    m_processingReportGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "本机处理报告"), true);
    auto* reportLayout = new QVBoxLayout(m_processingReportGroup);
    m_processingReportLabel = new QLabel(m_processingReportGroup);
    m_processingReportLabel->setTextFormat(Qt::PlainText);
    m_processingReportLabel->setToolTip(QCoreApplication::translate("Nebulens", "含本机输出路径；对外反馈可使用冲印回执中的匿名诊断。"));
    m_processingReportLabel->setWordWrap(true);
    m_processingReportLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setRole(m_processingReportLabel, "muted");
    reportLayout->addWidget(m_processingReportLabel);
    m_processingReportGroup->hide();
    outputPageLayout->addWidget(m_processingReportGroup, 0, Qt::AlignTop);

    m_recentResultsGroup = createCollapsibleGroup(
        QCoreApplication::translate("Nebulens", "最近结果"), true);
    auto* recentLayout = new QVBoxLayout(m_recentResultsGroup);
    m_recentResultsList = new QListWidget(m_recentResultsGroup);
    m_recentResultsList->setMaximumHeight(148);
    m_recentResultsList->setAccessibleName(QCoreApplication::translate("Nebulens", "最近处理结果"));
    recentLayout->addWidget(m_recentResultsList);
    auto* recentButtons = new QHBoxLayout();
    m_loadRecentResultBtn = new QPushButton(
        QCoreApplication::translate("Nebulens", "只读预览"), m_recentResultsGroup);
    setButtonVariant(m_loadRecentResultBtn, "secondary");
    m_revealRecentResultBtn = new QPushButton(
        QCoreApplication::translate("Nebulens", "在文件夹中显示"), m_recentResultsGroup);
    setButtonVariant(m_revealRecentResultBtn, "ghost");
    recentButtons->addWidget(m_loadRecentResultBtn);
    recentButtons->addWidget(m_revealRecentResultBtn);
    recentLayout->addLayout(recentButtons);
    outputPageLayout->addWidget(m_recentResultsGroup, 0, Qt::AlignTop);

    const auto selectedRecentPath = [this]() {
        QListWidgetItem* item = m_recentResultsList
            ? m_recentResultsList->currentItem() : nullptr;
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    connect(m_loadRecentResultBtn, &QPushButton::clicked, this,
            [this, selectedRecentPath]() {
                const QString path = selectedRecentPath();
                if (!path.isEmpty()) emit recentResultRequested(path);
            });
    connect(m_revealRecentResultBtn, &QPushButton::clicked, this,
            [this, selectedRecentPath]() {
                const QString path = selectedRecentPath();
                if (!path.isEmpty()) emit revealResultRequested(path);
            });
    connect(m_recentResultsList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item) emit recentResultRequested(
                    item->data(Qt::UserRole).toString());
            });

    // 预设操作只属于输出页，避免在堆栈和调整阶段形成持续抢眼的操作栏。
    m_footerBar = new QWidget(this);
    m_footerBar->setObjectName("paramsFooter");
    m_footerBar->setFixedHeight(52);
    auto* btnLayout = new QHBoxLayout(m_footerBar);
    btnLayout->setContentsMargins(0, StyleTokens::Spacing::kBase, 0, 0);
    btnLayout->setSpacing(8);

    m_restoreBtn = new QPushButton(QCoreApplication::translate("Nebulens", "恢复默认"), m_footerBar);
    setButtonVariant(m_restoreBtn, "ghost");
    connect(m_restoreBtn, &QPushButton::clicked, this, &ParamsPanel::onRestoreDefaults);
    btnLayout->addWidget(m_restoreBtn);

    btnLayout->addStretch();

    m_savePresetBtn = new QPushButton(QCoreApplication::translate("Nebulens", "保存预设"), m_footerBar);
    setButtonVariant(m_savePresetBtn, "secondary");
    connect(m_savePresetBtn, &QPushButton::clicked, this, &ParamsPanel::onSavePreset);
    btnLayout->addWidget(m_savePresetBtn);

    outputPageLayout->addStretch(1);
    layout->addWidget(m_footerBar);

    // Debounce 定时器
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(StyleTokens::Motion::kPreviewDebounceMs);
    connect(m_debounceTimer, &QTimer::timeout, this, &ParamsPanel::emitParamsChanged);

    // 连接复选框和滑块的启用/禁用关系
    connect(m_dewarpCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_dewarpSlider->setEnabled(checked);
        m_dewarpLabel->setEnabled(checked);
    });
    connect(m_noiseReductionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_noiseReductionSlider->setEnabled(checked);
        m_noiseReductionLabel->setEnabled(checked);
    });
    connect(m_starDefringeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_starDefringeSlider->setEnabled(checked);
        m_starDefringeLabel->setEnabled(checked);
    });
    connect(m_starReduceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_starReduceSlider->setEnabled(checked);
        m_starReduceLabel->setEnabled(checked);
    });

    // Keep layout metrics and styling centralized so newly added controls
    // inherit the same visual system without another inline style block.
    const auto groups = findChildren<QGroupBox*>();
    for (QGroupBox* group : groups) {
        group->setFlat(true);
        group->setProperty("inspectorSection", true);
        if (group->layout()) {
            group->layout()->setContentsMargins(
                0, StyleTokens::Spacing::kControlGap, 0,
                StyleTokens::Spacing::kMicro);
            group->layout()->setSpacing(StyleTokens::Spacing::kControlGap);
        }
    }
    const auto buttons = findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (!button->property("variant").isValid() &&
            button->text().isEmpty() && !button->icon().isNull()) {
            button->setProperty("variant", "icon");
        }
        button->setCursor(Qt::PointingHandCursor);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        combo->setCursor(Qt::PointingHandCursor);
    }
    for (QSlider* slider : findChildren<QSlider*>()) {
        slider->setCursor(Qt::PointingHandCursor);
    }
}

void ParamsPanel::updateSkyGroundControls() {
    const bool enabled = m_skyGroundCheck && m_skyGroundCheck->isChecked();
    const bool visible = enabled && m_stackGroup->isChecked();
    const int mode = m_skyGroundMode ? m_skyGroundMode->currentIndex() : 0;
    if (m_skyGroundMode) {
        m_skyGroundMode->setEnabled(enabled);
        m_skyGroundMode->setVisible(visible);
    }
    if (m_skyGroundModeLabel) m_skyGroundModeLabel->setVisible(visible);
    if (m_detectMaskBtn) {
        m_detectMaskBtn->setEnabled(enabled);
        m_detectMaskBtn->setVisible(visible && mode == 0);
    }
    if (m_importMaskBtn) {
        m_importMaskBtn->setEnabled(enabled);
        m_importMaskBtn->setVisible(visible && mode == 1);
    }
    if (m_maskPathLabel) {
        m_maskPathLabel->setVisible(visible && mode == 1 && !m_userMaskPath.isEmpty());
    }
    if (m_featherSlider) {
        m_featherSlider->setEnabled(enabled);
        m_featherSlider->setVisible(visible);
    }
    if (m_featherLabel) m_featherLabel->setVisible(visible);
    if (m_featherNameLabel) m_featherNameLabel->setVisible(visible);
    if (m_groundStackMethod) {
        m_groundStackMethod->setEnabled(enabled);
        m_groundStackMethod->setVisible(visible);
    }
    if (m_groundStackNameLabel) m_groundStackNameLabel->setVisible(visible);
    if (m_groundDetailSlider) {
        m_groundDetailSlider->setEnabled(enabled);
        m_groundDetailSlider->setVisible(visible);
    }
    if (m_groundDetailLabel) m_groundDetailLabel->setVisible(visible);
    if (m_groundDetailNameLabel) m_groundDetailNameLabel->setVisible(visible);
}

void ParamsPanel::updateModifiedCameraColorControls() {
    const bool enabled = modifiedCameraColorEnabled();
    const bool manual = m_modifiedCameraColorMode &&
        m_modifiedCameraColorMode->currentIndex() == 1;
    if (m_modifiedCameraColorStrengthSlider) {
        m_modifiedCameraColorStrengthSlider->setEnabled(enabled);
    }
    if (m_modifiedCameraColorStrengthLabel) {
        m_modifiedCameraColorStrengthLabel->setEnabled(enabled);
    }
    if (m_modifiedCameraColorMode) {
        m_modifiedCameraColorMode->setEnabled(enabled);
    }
    if (m_modifiedCameraGrayPointButton) {
        m_modifiedCameraGrayPointButton->setEnabled(enabled);
    }
    if (!m_modifiedCameraGrayPointStatus) return;
    m_modifiedCameraGrayPointStatus->setEnabled(enabled);
    if (!enabled) {
        m_modifiedCameraGrayPointStatus->setText(
            QCoreApplication::translate("Nebulens", "普通相机请保持关闭"));
    } else if (!manual) {
        m_modifiedCameraGrayPointStatus->setText(
            QCoreApplication::translate("Nebulens", "自动从中等亮度天空估计中性色"));
    } else if (hasModifiedCameraGrayPoint()) {
        m_modifiedCameraGrayPointStatus->setText(
            QCoreApplication::translate("Nebulens", "手动灰点已设置，可用吸管重新采样"));
    } else {
        m_modifiedCameraGrayPointStatus->setText(
            QCoreApplication::translate("Nebulens", "请点击吸管，再在结果预览中选择中性天空"));
    }
}

QGroupBox* ParamsPanel::createCollapsibleGroup(const QString& title, bool expanded) {
    auto* group = new QGroupBox(title, this);
    group->setCheckable(true);
    group->setChecked(expanded);
    group->setProperty("collapsible", true);
    connect(group, &QGroupBox::toggled,
            this, &ParamsPanel::onGroupToggled);
    return group;
}

QSlider* ParamsPanel::createSlider(int min, int max, int value, const QString& suffix) {
    Q_UNUSED(suffix)
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setValue(value);
    return slider;
}

void ParamsPanel::onGroupToggled(bool checked) {
    auto* group = qobject_cast<QGroupBox*>(sender());
    if (!group) return;

    // The 3px title indicator is styled globally; collapsing only changes the
    // content visibility and never turns the section into a disabled form.
    for (auto* child : group->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        child->setVisible(checked);
    }
    if (checked && group == m_stackGroup) updateSkyGroundControls();
    if (checked && group == m_calibrationGroup) updateCalibrationStatus();
    group->setMinimumHeight(checked ? 0 : 34);
}

void ParamsPanel::onSliderValueChanged(int value) {
    Q_UNUSED(value)
    markPresetCustom();
    m_debounceTimer->start();
}

void ParamsPanel::onSliderReleased() {
    markPresetCustom();
    m_debounceTimer->stop();
    emitParamsChanged();
}

void ParamsPanel::onComboChanged(int index) {
    Q_UNUSED(index)
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::onCheckChanged(int state) {
    Q_UNUSED(state)
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::onRestoreDefaults() {
    QSignalBlocker panelBlocker(this);
    m_userChangedStackMethod = false;
    {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->setCurrentIndex(0);
    }
    {
        QSignalBlocker blocker(m_stackAlgorithm); // 阻塞信号，避免触发 m_userChangedStackMethod = true
        m_stackAlgorithm->setCurrentIndex(0);
        m_kappaSlider->setValue(25);
        m_kappaLabel->setText("2.5");
    }
    updateStackMethodDescription();
    m_dewarpCheck->setChecked(false);
    m_dewarpSlider->setValue(30);
    m_noiseReductionCheck->setChecked(false);
    m_noiseReductionSlider->setValue(30);
    m_modifiedCameraColorCheck->setChecked(false);
    m_modifiedCameraColorStrengthSlider->setValue(100);
    clearModifiedCameraGrayPoint();
    m_stretchCheck->setChecked(false);
    m_temperatureSlider->setValue(0);
    m_tintSlider->setValue(0);
    m_exposureSlider->setValue(0);
    m_contrastSlider->setValue(0);
    m_highlightsSlider->setValue(0);
    m_shadowsSlider->setValue(0);
    m_whitesSlider->setValue(0);
    m_blacksSlider->setValue(0);
    m_vibranceSlider->setValue(0);
    m_saturationSlider->setValue(0);
    m_sharpeningSlider->setValue(0);
    m_starDefringeCheck->setChecked(false);
    m_starDefringeSlider->setValue(55);
    m_starReduceCheck->setChecked(false);
    m_starReduceSlider->setValue(70);
    m_outputFormat->setCurrentIndex(0);
    m_autoRejectQualityCheck->setChecked(true);
    m_photometricCheck->setChecked(true);
    // 重置天地分离
    m_skyGroundCheck->setChecked(false);
    m_skyGroundMode->setCurrentIndex(0);
    m_featherSlider->setValue(20);
    m_groundStackMethod->setCurrentIndex(0);
    m_groundDetailSlider->setValue(40);
    m_timelapseWindow->setCurrentIndex(1);
    m_timelapseStrengthSlider->setValue(80);
    m_timelapseMotionProtectionSlider->setValue(75);
    m_timelapseProtectGroundCheck->setChecked(true);
    m_starTrailCometSlider->setValue(0);
    m_starTrailReverseCheck->setChecked(false);
    m_starTrailProtectGroundCheck->setChecked(true);
    clearCalibrationSources();
    clearUserMask();
    m_debounceTimer->stop();
    panelBlocker.unblock();
    emitParamsChanged();
}

void ParamsPanel::onSavePreset() {
    bool ok;
    QString name = QInputDialog::getText(this, QCoreApplication::translate("Nebulens", "保存预设"),
                                          QCoreApplication::translate("Nebulens", "预设名称:"),
                                          QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;

    QSettings settings("StarProcessor", "App");
    int count = settings.beginReadArray("customPresets");
    settings.endArray();

    settings.beginWriteArray("customPresets", count + 1);
    settings.setArrayIndex(count);
    settings.setValue("name", name);
    settings.setValue("stackMethod", m_stackAlgorithm->currentIndex());
    settings.setValue("kappaValue", m_kappaSlider->value());
    settings.setValue("autoRejectLowQualityFrames",
                      m_autoRejectQualityCheck->isChecked());
    settings.setValue("photometricNormalizationEnabled",
                      m_photometricCheck->isChecked());
    settings.setValue("dewarpEnabled", m_dewarpCheck->isChecked());
    settings.setValue("dewarpStrength", m_dewarpSlider->value());
    settings.setValue("noiseReductionEnabled", m_noiseReductionCheck->isChecked());
    settings.setValue("noiseReductionStrength", m_noiseReductionSlider->value());
    settings.setValue("modifiedCameraColorEnabled",
                      m_modifiedCameraColorCheck->isChecked());
    settings.setValue("modifiedCameraColorStrength",
                      m_modifiedCameraColorStrengthSlider->value());
    settings.setValue("stretchEnabled", m_stretchCheck->isChecked());
    const BasicAdjustmentOptions basic = basicAdjustmentOptions();
    settings.setValue("temperature", basic.temperature);
    settings.setValue("tint", basic.tint);
    settings.setValue("exposureTenths", basic.exposureTenths);
    settings.setValue("contrast", basic.contrast);
    settings.setValue("highlights", basic.highlights);
    settings.setValue("shadows", basic.shadows);
    settings.setValue("whites", basic.whites);
    settings.setValue("blacks", basic.blacks);
    settings.setValue("vibrance", basic.vibrance);
    settings.setValue("saturation", basic.saturation);
    settings.setValue("sharpening", basic.sharpening);
    settings.setValue("starDefringeEnabled", m_starDefringeCheck->isChecked());
    settings.setValue("starDefringeStrength", m_starDefringeSlider->value());
    settings.setValue("starReduceEnabled", m_starReduceCheck->isChecked());
    settings.setValue("starReduceStrength", m_starReduceSlider->value());
    settings.setValue("outputFormat", m_outputFormat->currentIndex());
    settings.setValue("outputPath", m_outputPath->text());
    settings.setValue("skyGroundSepEnabled", m_skyGroundCheck->isChecked());
    settings.setValue("skyGroundMode", m_skyGroundMode->currentIndex());
    settings.setValue("userMaskPath", m_userMaskPath);
    settings.setValue("featherRadius", m_featherSlider->value());
    settings.setValue("groundStackMethod", m_groundStackMethod->currentIndex());
    settings.setValue("groundDetailStrength", m_groundDetailSlider->value());
    settings.endArray();

    m_presetCombo->addItem(name);
    m_presetCombo->setCurrentIndex(m_presetCombo->count() - 1);

    QMessageBox::information(this, QCoreApplication::translate("Nebulens", "保存预设"),
                             QCoreApplication::translate("Nebulens", "预设 \"%1\" 已保存").arg(name));
}

void ParamsPanel::recommendStackMethod(int frameCount) {
    if (m_userChangedStackMethod) return;

    int recommendedIndex = 0;
    if (frameCount <= 5) {
        recommendedIndex = 0; // Median
    } else if (frameCount <= 15) {
        recommendedIndex = 2; // Kappa-Sigma
    } else {
        recommendedIndex = 3; // Winsorized
    }

    if (m_stackAlgorithm->currentIndex() != recommendedIndex) {
        QSignalBlocker blocker(m_stackAlgorithm);
        m_stackAlgorithm->setCurrentIndex(recommendedIndex);
    }
    updateStackMethodDescription();
}

void ParamsPanel::saveCurrentSettings() {
    // 处理和关闭窗口仍会自动保存偏好，但不能用复开的文档覆盖它们。
    if (m_orderSettingsRestored) return;
    QSettings settings("StarProcessor", "App");
    settings.setValue("stackMethod", m_stackAlgorithm->currentIndex());
    settings.setValue("kappaValue", m_kappaSlider->value());
    settings.setValue("autoRejectLowQualityFrames",
                      m_autoRejectQualityCheck->isChecked());
    settings.setValue("photometricNormalizationEnabled",
                      m_photometricCheck->isChecked());
    settings.setValue("dewarpEnabled", m_dewarpCheck->isChecked());
    settings.setValue("dewarpStrength", m_dewarpSlider->value());
    settings.setValue("noiseReductionEnabled", m_noiseReductionCheck->isChecked());
    settings.setValue("noiseReductionStrength", m_noiseReductionSlider->value());
    settings.setValue("modifiedCameraColorEnabled",
                      m_modifiedCameraColorCheck->isChecked());
    settings.setValue("modifiedCameraColorStrength",
                      m_modifiedCameraColorStrengthSlider->value());
    settings.setValue("stretchEnabled", m_stretchCheck->isChecked());
    const BasicAdjustmentOptions basic = basicAdjustmentOptions();
    settings.setValue("temperature", basic.temperature);
    settings.setValue("tint", basic.tint);
    settings.setValue("exposureTenths", basic.exposureTenths);
    settings.setValue("contrast", basic.contrast);
    settings.setValue("highlights", basic.highlights);
    settings.setValue("shadows", basic.shadows);
    settings.setValue("whites", basic.whites);
    settings.setValue("blacks", basic.blacks);
    settings.setValue("vibrance", basic.vibrance);
    settings.setValue("saturation", basic.saturation);
    settings.setValue("sharpening", basic.sharpening);
    settings.setValue("starDefringeEnabled", m_starDefringeCheck->isChecked());
    settings.setValue("starDefringeStrength", m_starDefringeSlider->value());
    settings.setValue("starReduceEnabled", m_starReduceCheck->isChecked());
    settings.setValue("starReduceStrength", m_starReduceSlider->value());
    settings.setValue("outputFormat", m_outputFormat->currentIndex());
    settings.setValue("outputPath", m_outputPath->text());
    settings.setValue("lastPresetIndex", m_presetCombo->currentIndex());
    // 天地分离参数
    settings.setValue("skyGroundSepEnabled", m_skyGroundCheck->isChecked());
    settings.setValue("skyGroundMode", m_skyGroundMode->currentIndex());
    settings.setValue("userMaskPath", m_userMaskPath);
    settings.setValue("featherRadius", m_featherSlider->value());
    settings.setValue("groundStackMethod", m_groundStackMethod->currentIndex());
    settings.setValue("groundDetailStrength", m_groundDetailSlider->value());
    settings.setValue("timelapseWindow", timelapseWindowSize());
    settings.setValue("timelapseStrength", timelapseStrength());
    settings.setValue("timelapseMotionProtection", timelapseMotionProtection());
    settings.setValue("timelapseProtectGround", timelapseProtectGround());
    settings.setValue("starTrailCometStrength", starTrailCometStrength());
    settings.setValue("starTrailReverse", starTrailReverse());
    settings.setValue("starTrailProtectGround", starTrailProtectGround());
}

void ParamsPanel::captureOrderSettings(PrintOrderDocument& document) const {
    OrderSettings settings;
    auto& preset = settings.preset;
    preset.stackMethod = stackMethod();
    preset.kappaValue = kappaValue();
    preset.autoRejectLowQualityFrames = autoRejectLowQualityFrames();
    preset.photometricNormalizationEnabled = photometricNormalizationEnabled();
    preset.dewarpEnabled = dewarpEnabled();
    preset.dewarpStrength = dewarpStrength();
    preset.noiseReductionEnabled = noiseReductionEnabled();
    preset.noiseReductionStrength = noiseReductionStrength();
    preset.modifiedCameraColorEnabled = modifiedCameraColorEnabled();
    preset.modifiedCameraColorStrength = modifiedCameraColorStrength();
    preset.stretchEnabled = stretchEnabled();
    preset.basicAdjustments = basicAdjustmentOptions();
    preset.starDefringeEnabled = starDefringeEnabled();
    preset.starDefringeStrength = starDefringeStrength();
    preset.starReduceEnabled = starReduceEnabled();
    preset.starReduceStrength = starReduceStrength();
    preset.outputFormat = outputFormat();

    settings.skyGround = skyGroundSeparationEnabled();
    settings.skyGroundMode = skyGroundMode() == SkyGroundMask::UserMask
        ? QStringLiteral("user-mask") : QStringLiteral("auto");
    settings.feather = featherRadius();
    settings.groundMethod = groundStackMethod();
    settings.groundDetail = groundDetailStrength();
    settings.timelapseWindow = timelapseWindowSize();
    settings.timelapseStrength = timelapseStrength();
    settings.timelapseMotionProtection = timelapseMotionProtection();
    settings.timelapseProtectGround = timelapseProtectGround();
    settings.starTrailCometStrength = starTrailCometStrength();
    settings.starTrailReverse = starTrailReverse();
    settings.starTrailProtectGround = starTrailProtectGround();
    settings.saveGeneratedMasters = saveGeneratedMasters();
    settings.modifiedCameraColorMode = modifiedCameraColorMode() ==
            ModifiedCameraNeutralMode::ManualPoint
        ? QStringLiteral("manual") : QStringLiteral("auto");

    QJsonObject params;
    visitOrderSettings(settings, [&params](const char* key, const auto& value,
                                         const auto&...) {
        params.insert(QLatin1String(key), QJsonValue(value));
    });
    if (hasModifiedCameraGrayPoint()) {
        params.insert(QStringLiteral("modifiedCameraGrayPointX"), modifiedCameraGrayPointX());
        params.insert(QStringLiteral("modifiedCameraGrayPointY"), modifiedCameraGrayPointY());
    }
    document.params = params;
    document.dark = m_darkFramePaths;
    document.flat = m_flatFramePaths;
    document.bias = m_biasFramePaths;
    document.darkFlat = m_darkFlatFramePaths;
    document.masterDark = m_masterDarkPath;
    document.masterFlat = m_masterFlatPath;
    document.masterBias = m_masterBiasPath;
    document.masterDarkFlat = m_masterDarkFlatPath;
    document.userMask = m_userMaskPath;
    document.outputDirectory = outputPath();
}

bool ParamsPanel::validateOrderSettings(const PrintOrderDocument& document,
                                        QString& error) {
    OrderSettings settings;
    return decodeOrderSettings(document.params, settings, error);
}

void ParamsPanel::restoreOrderSettings(const PrintOrderDocument& document) {
    // 场景由主窗口先切换。即使没有切换到不同场景，缺省值也不继承旧控件。
    OrderSettings settings;
    settings.preset = scenePreset(m_scene);
    settings.skyGround = m_scene == ProcessingScene::SkyGround;
    QString error;
    if (!decodeOrderSettings(document.params, settings, error)) return;

    {
        const QSignalBlocker panelBlocker(this);
        m_orderSettingsRestored = true;
        applyPreset(settings.preset);
        m_userChangedStackMethod = true;

        m_skyGroundCheck->setChecked(settings.skyGround);
        m_skyGroundMode->setCurrentIndex(settings.skyGroundMode == "user-mask" ? 1 : 0);
        m_userMaskPath = document.userMask;
        m_maskPathLabel->setText(QFileInfo(m_userMaskPath).fileName());
        m_featherSlider->setValue(settings.feather);
        m_groundStackMethod->setCurrentIndex(m_groundStackMethod->findData(settings.groundMethod));
        m_groundDetailSlider->setValue(settings.groundDetail);
        updateSkyGroundControls();

        m_timelapseWindow->setCurrentIndex(m_timelapseWindow->findData(settings.timelapseWindow));
        m_timelapseStrengthSlider->setValue(settings.timelapseStrength);
        m_timelapseMotionProtectionSlider->setValue(settings.timelapseMotionProtection);
        m_timelapseProtectGroundCheck->setChecked(settings.timelapseProtectGround);
        m_starTrailCometSlider->setValue(settings.starTrailCometStrength);
        m_starTrailReverseCheck->setChecked(settings.starTrailReverse);
        m_starTrailProtectGroundCheck->setChecked(settings.starTrailProtectGround);

        m_modifiedCameraGrayPointX = settings.grayPointX;
        m_modifiedCameraGrayPointY = settings.grayPointY;
        m_modifiedCameraColorMode->setCurrentIndex(settings.modifiedCameraColorMode == "manual" ? 1 : 0);
        updateModifiedCameraColorControls();

        m_darkFramePaths = document.dark;
        m_flatFramePaths = document.flat;
        m_biasFramePaths = document.bias;
        m_darkFlatFramePaths = document.darkFlat;
        m_masterDarkPath = document.masterDark;
        m_masterFlatPath = document.masterFlat;
        m_masterBiasPath = document.masterBias;
        m_masterDarkFlatPath = document.masterDarkFlat;
        updateCalibrationSource(m_darkFrameCount, m_darkFrameClear, m_darkFramePaths, m_masterDarkPath);
        updateCalibrationSource(m_flatFrameCount, m_flatFrameClear, m_flatFramePaths, m_masterFlatPath);
        updateCalibrationSource(m_biasFrameCount, m_biasFrameClear, m_biasFramePaths, m_masterBiasPath);
        updateCalibrationSource(m_darkFlatFrameCount, m_darkFlatFrameClear, m_darkFlatFramePaths, m_masterDarkFlatPath);
        m_saveGeneratedMastersCheck->setChecked(settings.saveGeneratedMasters);
        updateCalibrationStatus();

        QString reference = document.reference;
#ifdef Q_OS_WIN
        // 文档可用不同大小写引用同一张片，控件应保留片列里的路径拼写。
        const auto light = std::find_if(m_referenceFiles.cbegin(), m_referenceFiles.cend(),
            [&reference](const QString& path) { return path.compare(reference, Qt::CaseInsensitive) == 0; });
        if (light != m_referenceFiles.cend()) reference = *light;
#endif
        setSelectedReferenceFrame(reference);
        setOutputPath(document.outputDirectory.isEmpty()
                          ? defaultOutputPath() : document.outputDirectory);
        m_debounceTimer->stop();
    }
    emitParamsChanged();
}

void ParamsPanel::loadCustomPresets() {
    QSettings settings("StarProcessor", "App");
    int count = settings.beginReadArray("customPresets");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        QString name = settings.value("name").toString();
        if (!name.isEmpty()) {
            m_presetCombo->addItem(name);
        }
    }
    settings.endArray();
}

void ParamsPanel::loadPreset() {
    QSettings settings("StarProcessor", "App");
    const bool hasPersistedStackMethod = settings.contains("stackMethod");

    // 加载自定义预设列表
    loadCustomPresets();

    // 加载上次使用的参数
    int stackIndex = settings.value("stackMethod", 0).toInt();
    int kappa = settings.value("kappaValue", 25).toInt();
    bool autoRejectQuality =
        settings.value("autoRejectLowQualityFrames", true).toBool();
    bool photometricNormalization =
        settings.value("photometricNormalizationEnabled", true).toBool();
    bool dewarp = settings.value("dewarpEnabled", false).toBool();
    int dewarpStrength = settings.value("dewarpStrength", 30).toInt();
    bool noiseReduction = settings.value("noiseReductionEnabled", false).toBool();
    int noiseReductionStrength = settings.value("noiseReductionStrength", 30).toInt();
    bool modifiedCameraColor =
        settings.value("modifiedCameraColorEnabled", false).toBool();
    int modifiedCameraColorStrength =
        settings.value("modifiedCameraColorStrength", 100).toInt();
    bool stretch = settings.value("stretchEnabled", false).toBool();
    BasicAdjustmentOptions basic;
    basic.temperature = settings.value("temperature", 0).toInt();
    basic.tint = settings.value("tint", 0).toInt();
    basic.exposureTenths = settings.value("exposureTenths", 0).toInt();
    basic.contrast = settings.value("contrast", 0).toInt();
    basic.highlights = settings.value("highlights", 0).toInt();
    basic.shadows = settings.value("shadows", 0).toInt();
    basic.whites = settings.value("whites", 0).toInt();
    basic.blacks = settings.value("blacks", 0).toInt();
    basic.vibrance = settings.value("vibrance", 0).toInt();
    basic.saturation = settings.value("saturation", 0).toInt();
    basic.sharpening = settings.value("sharpening", 0).toInt();
    bool starDefringe = settings.value("starDefringeEnabled", false).toBool();
    int starDefringeStrength =
        settings.value("starDefringeStrength", 55).toInt();
    bool starReduce = settings.value("starReduceEnabled", false).toBool();
    int starReduceStrength = settings.value("starReduceStrength", 70).toInt();
    int outputFormat = settings.value("outputFormat", 0).toInt();
    QString outputPath = settings.value(
        "outputPath", defaultOutputPath()).toString();
    const QString migratedOutputPath = migrateDefaultOutputPath(outputPath);
    if (migratedOutputPath != outputPath) {
        outputPath = migratedOutputPath;
        settings.setValue("outputPath", outputPath);
    }
    int lastPresetIndex = settings.value("lastPresetIndex", 0).toInt();

    // 天地分离参数
    bool skyGroundSep = settings.value("skyGroundSepEnabled", false).toBool();
    int skyGroundModeIdx = settings.value("skyGroundMode", 0).toInt();
    QString userMaskPath = settings.value("userMaskPath", QString()).toString();
    int featherRadius = settings.value("featherRadius", 20).toInt();
    int groundStackMethod = settings.value("groundStackMethod", 0).toInt();
    int groundDetailStrength = settings.value("groundDetailStrength", 40).toInt();
    int timelapseWindow = settings.value("timelapseWindow", 5).toInt();
    int timelapseStrength = settings.value("timelapseStrength", 80).toInt();
    int timelapseMotionProtection =
        settings.value("timelapseMotionProtection", 75).toInt();
    bool timelapseProtectGround =
        settings.value("timelapseProtectGround", true).toBool();
    int starTrailCometStrength =
        settings.value("starTrailCometStrength", 0).toInt();
    bool starTrailReverse =
        settings.value("starTrailReverse", false).toBool();
    bool starTrailProtectGround =
        settings.value("starTrailProtectGround", true).toBool();

    // 使用信号阻塞避免触发 paramsChanged
    QSignalBlocker blocker2(m_stackAlgorithm);
    QSignalBlocker blocker3(m_kappaSlider);
    QSignalBlocker blocker4(m_dewarpCheck);
    QSignalBlocker blocker5(m_dewarpSlider);
    QSignalBlocker blocker6(m_noiseReductionCheck);
    QSignalBlocker blocker7(m_noiseReductionSlider);
    QSignalBlocker blocker8(m_modifiedCameraColorCheck);
    QSignalBlocker blocker9(m_stretchCheck);
    QSignalBlocker blocker10(m_starReduceCheck);
    QSignalBlocker blocker11(m_starReduceSlider);
    QSignalBlocker blocker12(m_outputFormat);
    QSignalBlocker blocker13(m_presetCombo);
    QSignalBlocker blocker14(m_skyGroundCheck);
    QSignalBlocker blocker15(m_skyGroundMode);
    QSignalBlocker blocker16(m_featherSlider);
    QSignalBlocker blocker17(m_photometricCheck);
    QSignalBlocker blocker18(m_autoRejectQualityCheck);
    QSignalBlocker blocker19(m_groundStackMethod);
    QSignalBlocker blocker20(m_groundDetailSlider);
    QSignalBlocker blocker21(m_timelapseWindow);
    QSignalBlocker blocker22(m_timelapseStrengthSlider);
    QSignalBlocker blocker23(m_timelapseMotionProtectionSlider);
    QSignalBlocker blocker24(m_timelapseProtectGroundCheck);
    QSignalBlocker blocker25(m_modifiedCameraColorStrengthSlider);
    QSignalBlocker blocker26(m_modifiedCameraColorMode);
    QSignalBlocker blocker27(m_starTrailCometSlider);
    QSignalBlocker blocker28(m_starTrailReverseCheck);
    QSignalBlocker blocker29(m_starTrailProtectGroundCheck);
    QSignalBlocker blocker30(m_temperatureSlider);
    QSignalBlocker blocker31(m_tintSlider);
    QSignalBlocker blocker32(m_exposureSlider);
    QSignalBlocker blocker33(m_contrastSlider);
    QSignalBlocker blocker34(m_highlightsSlider);
    QSignalBlocker blocker35(m_shadowsSlider);
    QSignalBlocker blocker36(m_whitesSlider);
    QSignalBlocker blocker37(m_blacksSlider);
    QSignalBlocker blocker38(m_vibranceSlider);
    QSignalBlocker blocker39(m_saturationSlider);
    QSignalBlocker blocker40(m_sharpeningSlider);
    QSignalBlocker blocker41(m_starDefringeCheck);
    QSignalBlocker blocker42(m_starDefringeSlider);

    m_stackAlgorithm->setCurrentIndex(stackIndex);
    m_kappaSlider->setValue(kappa);
    m_kappaLabel->setText(QString::number(kappa / 10.0, 'f', 1));
    m_autoRejectQualityCheck->setChecked(autoRejectQuality);
    m_photometricCheck->setChecked(photometricNormalization);
    m_dewarpCheck->setChecked(dewarp);
    m_dewarpSlider->setValue(dewarpStrength);
    m_dewarpSlider->setEnabled(dewarp);
    m_dewarpLabel->setText(QString("%1%").arg(dewarpStrength));
    m_dewarpLabel->setEnabled(dewarp);
    m_noiseReductionCheck->setChecked(noiseReduction);
    m_noiseReductionSlider->setValue(noiseReductionStrength);
    m_noiseReductionSlider->setEnabled(noiseReduction);
    m_noiseReductionLabel->setText(QString("%1%").arg(noiseReductionStrength));
    m_noiseReductionLabel->setEnabled(noiseReduction);
    m_modifiedCameraColorCheck->setChecked(modifiedCameraColor);
    m_modifiedCameraColorStrengthSlider->setValue(
        std::clamp(modifiedCameraColorStrength, 0, 100));
    m_modifiedCameraColorStrengthLabel->setText(
        QString::number(m_modifiedCameraColorStrengthSlider->value()) + "%");
    m_modifiedCameraColorMode->setCurrentIndex(0);
    m_modifiedCameraGrayPointX = -1.0;
    m_modifiedCameraGrayPointY = -1.0;
    updateModifiedCameraColorControls();
    m_stretchCheck->setChecked(stretch);
    auto applyBasicValue = [](QSlider* slider, QLabel* label, int value) {
        slider->setValue(std::clamp(value, slider->minimum(), slider->maximum()));
        label->setText(signedAdjustmentText(slider->value()));
    };
    applyBasicValue(m_temperatureSlider, m_temperatureLabel, basic.temperature);
    applyBasicValue(m_tintSlider, m_tintLabel, basic.tint);
    m_exposureSlider->setValue(std::clamp(
        basic.exposureTenths, m_exposureSlider->minimum(),
        m_exposureSlider->maximum()));
    m_exposureLabel->setText(exposureText(m_exposureSlider->value()));
    applyBasicValue(m_contrastSlider, m_contrastLabel, basic.contrast);
    applyBasicValue(m_highlightsSlider, m_highlightsLabel, basic.highlights);
    applyBasicValue(m_shadowsSlider, m_shadowsLabel, basic.shadows);
    applyBasicValue(m_whitesSlider, m_whitesLabel, basic.whites);
    applyBasicValue(m_blacksSlider, m_blacksLabel, basic.blacks);
    applyBasicValue(m_vibranceSlider, m_vibranceLabel, basic.vibrance);
    applyBasicValue(m_saturationSlider, m_saturationLabel, basic.saturation);
    applyBasicValue(m_sharpeningSlider, m_sharpeningLabel, basic.sharpening);
    m_starDefringeCheck->setChecked(starDefringe);
    m_starDefringeSlider->setValue(std::clamp(starDefringeStrength, 0, 100));
    m_starDefringeSlider->setEnabled(starDefringe);
    m_starDefringeLabel->setText(
        QString("%1%").arg(m_starDefringeSlider->value()));
    m_starDefringeLabel->setEnabled(starDefringe);
    m_starReduceCheck->setChecked(starReduce);
    m_starReduceSlider->setValue(starReduceStrength);
    m_starReduceSlider->setEnabled(starReduce);
    m_starReduceLabel->setText(QString("%1%").arg(starReduceStrength));
    m_starReduceLabel->setEnabled(starReduce);
    m_outputFormat->setCurrentIndex(outputFormat);
    displayOutputPath(m_outputPath, outputPath);

    // 天地分离
    m_skyGroundCheck->setChecked(skyGroundSep);
    m_skyGroundMode->setCurrentIndex(skyGroundModeIdx);
    m_userMaskPath = userMaskPath;
    if (!m_userMaskPath.isEmpty()) {
        m_maskPathLabel->setText(QFileInfo(m_userMaskPath).fileName());
    }
    m_featherSlider->setValue(featherRadius);
    m_featherLabel->setText(QString("%1 px").arg(featherRadius));
    m_groundStackMethod->setCurrentIndex(
        std::clamp(groundStackMethod, 0, m_groundStackMethod->count() - 1));
    m_groundDetailSlider->setValue(std::clamp(groundDetailStrength, 0, 70));
    m_groundDetailLabel->setText(
        QString::number(m_groundDetailSlider->value()) + "%");
    m_timelapseWindow->setCurrentIndex(timelapseWindow == 3 ? 0 : 1);
    m_timelapseStrengthSlider->setValue(
        std::clamp(timelapseStrength, 0, 100));
    m_timelapseStrengthLabel->setText(
        QString::number(m_timelapseStrengthSlider->value()) + "%");
    m_timelapseMotionProtectionSlider->setValue(
        std::clamp(timelapseMotionProtection, 0, 100));
    m_timelapseMotionProtectionLabel->setText(
        QString::number(m_timelapseMotionProtectionSlider->value()) + "%");
    m_timelapseProtectGroundCheck->setChecked(timelapseProtectGround);
    m_starTrailCometSlider->setValue(
        std::clamp(starTrailCometStrength, 0, 100));
    m_starTrailCometLabel->setText(
        QString::number(m_starTrailCometSlider->value()) + "%");
    m_starTrailReverseCheck->setChecked(starTrailReverse);
    m_starTrailReverseCheck->setEnabled(
        m_starTrailCometSlider->value() > 0);
    m_starTrailProtectGroundCheck->setChecked(starTrailProtectGround);
    updateSkyGroundControls();
    updateStackMethodDescription();

    if (lastPresetIndex >= 0 && lastPresetIndex < m_presetCombo->count()) {
        m_presetCombo->setCurrentIndex(lastPresetIndex);
    }
    // A restored algorithm is an explicit user choice. Do not replace it with
    // the frame-count recommendation immediately after the next import.
    m_userChangedStackMethod = hasPersistedStackMethod;
}

void ParamsPanel::onPresetChanged(int index) {
    if (index <= 0) return;

    m_userChangedStackMethod = false; // 选择预设时重置，允许后续智能推荐

    int builtinCount = PresetManager::builtinPresets().size();
    if (index - 1 < builtinCount) {
        auto presets = PresetManager::builtinPresets();
        applyPreset(presets[index - 1]);
    } else {
        // 自定义预设：从 QSettings 加载
        int customIndex = index - 1 - builtinCount;
        QSettings settings("StarProcessor", "App");
        int count = settings.beginReadArray("customPresets");
        if (customIndex < count) {
            settings.setArrayIndex(customIndex);
            Preset preset;
            preset.name = settings.value("name").toString();
            preset.stackMethod = settings.value("stackMethod", 0).toInt() == 0 ? "median" :
                                 settings.value("stackMethod", 0).toInt() == 1 ? "average" :
                                 settings.value("stackMethod", 0).toInt() == 2 ? "kappa-sigma" : "winsorized";
            preset.kappaValue = settings.value("kappaValue", 25).toInt() / 10.0;
            preset.autoRejectLowQualityFrames =
                settings.value("autoRejectLowQualityFrames", true).toBool();
            preset.photometricNormalizationEnabled =
                settings.value("photometricNormalizationEnabled", true).toBool();
            preset.dewarpEnabled = settings.value("dewarpEnabled", false).toBool();
            preset.dewarpStrength = settings.value("dewarpStrength", 30).toInt();
            preset.noiseReductionEnabled = settings.value("noiseReductionEnabled", false).toBool();
            preset.noiseReductionStrength = settings.value("noiseReductionStrength", 30).toInt();
            preset.modifiedCameraColorEnabled =
                settings.value("modifiedCameraColorEnabled", false).toBool();
            preset.modifiedCameraColorStrength =
                settings.value("modifiedCameraColorStrength", 100).toInt();
            preset.stretchEnabled = settings.value("stretchEnabled", false).toBool();
            preset.basicAdjustments.temperature =
                settings.value("temperature", 0).toInt();
            preset.basicAdjustments.tint =
                settings.value("tint", 0).toInt();
            preset.basicAdjustments.exposureTenths =
                settings.value("exposureTenths", 0).toInt();
            preset.basicAdjustments.contrast =
                settings.value("contrast", 0).toInt();
            preset.basicAdjustments.highlights =
                settings.value("highlights", 0).toInt();
            preset.basicAdjustments.shadows =
                settings.value("shadows", 0).toInt();
            preset.basicAdjustments.whites =
                settings.value("whites", 0).toInt();
            preset.basicAdjustments.blacks =
                settings.value("blacks", 0).toInt();
            preset.basicAdjustments.vibrance =
                settings.value("vibrance", 0).toInt();
            preset.basicAdjustments.saturation =
                settings.value("saturation", 0).toInt();
            preset.basicAdjustments.sharpening =
                settings.value("sharpening", 0).toInt();
            preset.starDefringeEnabled =
                settings.value("starDefringeEnabled", false).toBool();
            preset.starDefringeStrength =
                settings.value("starDefringeStrength", 55).toInt();
            preset.starReduceEnabled = settings.value("starReduceEnabled", false).toBool();
            preset.starReduceStrength = settings.value("starReduceStrength", 70).toInt();
            preset.outputFormat = settings.value("outputFormat", 0).toInt() == 0 ? "tiff16" : "png8";
            applyPreset(preset);

            // 恢复自定义预设中保存的其他参数
            QString op = settings.value(
                "outputPath", defaultOutputPath()).toString();
            op = migrateDefaultOutputPath(op);
            displayOutputPath(m_outputPath, op);

            // 天地分离参数
            bool sgs = settings.value("skyGroundSepEnabled", false).toBool();
            int sgm = settings.value("skyGroundMode", 0).toInt();
            QString ump = settings.value("userMaskPath", QString()).toString();
            int fr = settings.value("featherRadius", 20).toInt();
            int gsm = settings.value("groundStackMethod", 0).toInt();
            int gds = settings.value("groundDetailStrength", 40).toInt();
            QSignalBlocker sgBlocker1(m_skyGroundCheck);
            QSignalBlocker sgBlocker2(m_skyGroundMode);
            QSignalBlocker sgBlocker3(m_featherSlider);
            QSignalBlocker sgBlocker4(m_groundStackMethod);
            QSignalBlocker sgBlocker5(m_groundDetailSlider);
            m_skyGroundCheck->setChecked(sgs);
            m_skyGroundMode->setCurrentIndex(sgm);
            m_userMaskPath = ump;
            if (!m_userMaskPath.isEmpty()) {
                m_maskPathLabel->setText(QFileInfo(m_userMaskPath).fileName());
            }
            m_featherSlider->setValue(fr);
            m_featherLabel->setText(QString("%1 px").arg(fr));
            m_groundStackMethod->setCurrentIndex(
                std::clamp(gsm, 0, m_groundStackMethod->count() - 1));
            m_groundDetailSlider->setValue(std::clamp(gds, 0, 70));
            m_groundDetailLabel->setText(
                QString::number(m_groundDetailSlider->value()) + "%");
            updateSkyGroundControls();
            emitParamsChanged();
        }
        settings.endArray();
    }
}

void ParamsPanel::applyPreset(const Preset& preset) {
    // Block signals to avoid recursive emits
    QSignalBlocker blocker2(m_stackAlgorithm);
    QSignalBlocker blocker3(m_kappaSlider);
    QSignalBlocker blocker4(m_dewarpCheck);
    QSignalBlocker blocker5(m_dewarpSlider);
    QSignalBlocker blocker6(m_noiseReductionCheck);
    QSignalBlocker blocker7(m_noiseReductionSlider);
    QSignalBlocker blocker8(m_modifiedCameraColorCheck);
    QSignalBlocker blocker9(m_stretchCheck);
    QSignalBlocker blocker10(m_starReduceCheck);
    QSignalBlocker blocker11(m_starReduceSlider);
    QSignalBlocker blocker12(m_outputFormat);
    QSignalBlocker blocker13(m_photometricCheck);
    QSignalBlocker blocker14(m_autoRejectQualityCheck);
    QSignalBlocker blocker15(m_modifiedCameraColorStrengthSlider);
    QSignalBlocker blocker16(m_modifiedCameraColorMode);
    QSignalBlocker blocker17(m_temperatureSlider);
    QSignalBlocker blocker18(m_tintSlider);
    QSignalBlocker blocker19(m_exposureSlider);
    QSignalBlocker blocker20(m_contrastSlider);
    QSignalBlocker blocker21(m_highlightsSlider);
    QSignalBlocker blocker22(m_shadowsSlider);
    QSignalBlocker blocker23(m_whitesSlider);
    QSignalBlocker blocker24(m_blacksSlider);
    QSignalBlocker blocker25(m_vibranceSlider);
    QSignalBlocker blocker26(m_saturationSlider);
    QSignalBlocker blocker27(m_sharpeningSlider);
    QSignalBlocker blocker28(m_starDefringeCheck);
    QSignalBlocker blocker29(m_starDefringeSlider);

    // Stack method
    if (preset.stackMethod == "median") m_stackAlgorithm->setCurrentIndex(0);
    else if (preset.stackMethod == "average") m_stackAlgorithm->setCurrentIndex(1);
    else if (preset.stackMethod == "kappa-sigma") m_stackAlgorithm->setCurrentIndex(2);
    else if (preset.stackMethod == "winsorized") m_stackAlgorithm->setCurrentIndex(3);

    // Kappa
    int kappaInt = static_cast<int>(preset.kappaValue * 10 + 0.5);
    m_kappaSlider->setValue(kappaInt);
    m_kappaLabel->setText(QString::number(preset.kappaValue, 'f', 1));
    m_autoRejectQualityCheck->setChecked(
        preset.autoRejectLowQualityFrames);
    m_photometricCheck->setChecked(
        preset.photometricNormalizationEnabled);

    // Dewarp
    m_dewarpCheck->setChecked(preset.dewarpEnabled);
    m_dewarpSlider->setValue(preset.dewarpStrength);
    m_dewarpSlider->setEnabled(preset.dewarpEnabled);
    m_dewarpLabel->setText(QString("%1%").arg(preset.dewarpStrength));
    m_dewarpLabel->setEnabled(preset.dewarpEnabled);

    // Denoise is applied to the linear stacked image before dehaze/stretch.
    m_noiseReductionCheck->setChecked(preset.noiseReductionEnabled);
    m_noiseReductionSlider->setValue(preset.noiseReductionStrength);
    m_noiseReductionSlider->setEnabled(preset.noiseReductionEnabled);
    m_noiseReductionLabel->setText(
        QString("%1%").arg(preset.noiseReductionStrength));
    m_noiseReductionLabel->setEnabled(preset.noiseReductionEnabled);

    m_modifiedCameraColorCheck->setChecked(
        preset.modifiedCameraColorEnabled);
    m_modifiedCameraColorStrengthSlider->setValue(
        std::clamp(preset.modifiedCameraColorStrength, 0, 100));
    m_modifiedCameraColorStrengthLabel->setText(
        QString::number(m_modifiedCameraColorStrengthSlider->value()) + "%");
    m_modifiedCameraColorMode->setCurrentIndex(0);
    m_modifiedCameraGrayPointX = -1.0;
    m_modifiedCameraGrayPointY = -1.0;
    updateModifiedCameraColorControls();

    // Stretch
    m_stretchCheck->setChecked(preset.stretchEnabled);

    auto applyBasicValue = [](QSlider* slider, QLabel* label, int value) {
        slider->setValue(std::clamp(value, slider->minimum(), slider->maximum()));
        label->setText(signedAdjustmentText(slider->value()));
    };
    applyBasicValue(m_temperatureSlider, m_temperatureLabel,
                    preset.basicAdjustments.temperature);
    applyBasicValue(m_tintSlider, m_tintLabel,
                    preset.basicAdjustments.tint);
    m_exposureSlider->setValue(std::clamp(
        preset.basicAdjustments.exposureTenths,
        m_exposureSlider->minimum(), m_exposureSlider->maximum()));
    m_exposureLabel->setText(exposureText(m_exposureSlider->value()));
    applyBasicValue(m_contrastSlider, m_contrastLabel,
                    preset.basicAdjustments.contrast);
    applyBasicValue(m_highlightsSlider, m_highlightsLabel,
                    preset.basicAdjustments.highlights);
    applyBasicValue(m_shadowsSlider, m_shadowsLabel,
                    preset.basicAdjustments.shadows);
    applyBasicValue(m_whitesSlider, m_whitesLabel,
                    preset.basicAdjustments.whites);
    applyBasicValue(m_blacksSlider, m_blacksLabel,
                    preset.basicAdjustments.blacks);
    applyBasicValue(m_vibranceSlider, m_vibranceLabel,
                    preset.basicAdjustments.vibrance);
    applyBasicValue(m_saturationSlider, m_saturationLabel,
                    preset.basicAdjustments.saturation);
    applyBasicValue(m_sharpeningSlider, m_sharpeningLabel,
                    preset.basicAdjustments.sharpening);

    m_starDefringeCheck->setChecked(preset.starDefringeEnabled);
    m_starDefringeSlider->setValue(
        std::clamp(preset.starDefringeStrength, 0, 100));
    m_starDefringeSlider->setEnabled(preset.starDefringeEnabled);
    m_starDefringeLabel->setText(
        QString("%1%").arg(m_starDefringeSlider->value()));
    m_starDefringeLabel->setEnabled(preset.starDefringeEnabled);

    // Star reduction is part of the preset contract. Apply both values here so
    // built-in and custom presets behave the same after being selected.
    m_starReduceCheck->setChecked(preset.starReduceEnabled);
    m_starReduceSlider->setValue(preset.starReduceStrength);
    m_starReduceSlider->setEnabled(preset.starReduceEnabled);
    m_starReduceLabel->setText(QString("%1%").arg(preset.starReduceStrength));
    m_starReduceLabel->setEnabled(preset.starReduceEnabled);

    // Output format
    setOutputFormat(preset.outputFormat);

    updateStackMethodDescription();
    emitParamsChanged();
}

void ParamsPanel::applySceneProfile(ProcessingScene scene,
                                    bool resetParameters) {
    m_scene = scene;
    int presetIndex = 0;
    int activeTab = 0;
    QString title;

    switch (scene) {
    case ProcessingScene::SingleFrame:
        title = QCoreApplication::translate("Nebulens", "单张精修参数");
        activeTab = 1;
        break;
    case ProcessingScene::Nightscape:
        presetIndex = 1;
        title = QCoreApplication::translate("Nebulens", "银河星景参数");
        break;
    case ProcessingScene::DeepSky:
        presetIndex = 2;
        title = QCoreApplication::translate("Nebulens", "深空堆栈参数");
        break;
    case ProcessingScene::SkyGround:
        title = QCoreApplication::translate("Nebulens", "天地分离参数");
        break;
    case ProcessingScene::StarTrail:
        title = QCoreApplication::translate("Nebulens", "星轨合成参数");
        break;
    case ProcessingScene::Timelapse:
        title = QCoreApplication::translate("Nebulens", "延时序列参数");
        break;
    }

    if (resetParameters) {
        m_userChangedStackMethod = false;
        applyPreset(scenePreset(scene));

        const QSignalBlocker presetBlocker(m_presetCombo);
        const QSignalBlocker skyGroundBlocker(m_skyGroundCheck);
        m_presetCombo->setCurrentIndex(
            std::clamp(presetIndex, 0, m_presetCombo->count() - 1));
        m_skyGroundCheck->setChecked(scene == ProcessingScene::SkyGround);
    }
    updateSkyGroundControls();

    if (m_titleLabel) m_titleLabel->setText(title);
    const bool timelapse = scene == ProcessingScene::Timelapse;
    const bool starTrail = scene == ProcessingScene::StarTrail;
    const bool sequenceTool = timelapse || starTrail;
    if (m_alignGroup) m_alignGroup->setVisible(!sequenceTool);
    if (m_stackGroup) m_stackGroup->setVisible(!sequenceTool);
    if (m_starTrailGroup) m_starTrailGroup->setVisible(starTrail);
    if (m_timelapseGroup) m_timelapseGroup->setVisible(timelapse);
    if (m_calibrationGroup) {
        m_calibrationGroup->setVisible(scene == ProcessingScene::DeepSky);
    }
    // General stack presets change controls that do not participate in the
    // temporal pipeline, so keep this task-specific workspace focused.
    if (m_presetBar) m_presetBar->setVisible(!sequenceTool);
    if (m_savePresetBtn) m_savePresetBtn->setVisible(!sequenceTool);
    if (m_starReduceGroup) m_starReduceGroup->setVisible(!starTrail);
    if (m_tabs) {
        // A single frame has no alignment or stack phase, so that page is
        // removed instead of leaving irrelevant disabled controls in view.
        m_tabs->setTabVisible(0, scene != ProcessingScene::SingleFrame);
        m_tabs->setTabVisible(1, !timelapse);
        m_tabs->setCurrentIndex(activeTab);
    }
    showWorkflowStage(m_activeWorkflowStage);
    emitParamsChanged();
}

void ParamsPanel::showWorkflowStage(WorkflowStage stage) {
    if (!m_tabs) return;
    m_activeWorkflowStage = stage;

    int tabIndex = 0;
    QString kicker;
    QString title;
    switch (stage) {
    case WorkflowStage::Assets:
        kicker = QCoreApplication::translate("Nebulens", "01 / 素材");
        title = QCoreApplication::translate("Nebulens", "素材与序列");
        break;
    case WorkflowStage::Calibration:
        kicker = QCoreApplication::translate("Nebulens", "02 / 校准");
        title = QCoreApplication::translate("Nebulens", "镜头与传感器");
        break;
    case WorkflowStage::Alignment:
        kicker = QCoreApplication::translate("Nebulens", "03 / 对齐");
        title = QCoreApplication::translate("Nebulens", "星点与地景对齐");
        break;
    case WorkflowStage::Stacking:
        kicker = QCoreApplication::translate("Nebulens", "04 / 合成");
        title = QCoreApplication::translate("Nebulens", "帧合成");
        break;
    case WorkflowStage::Adjustment:
        kicker = QCoreApplication::translate("Nebulens", "05 / 调整");
        title = QCoreApplication::translate("Nebulens", "成片调整");
        tabIndex = 1;
        break;
    case WorkflowStage::Output:
        kicker = QCoreApplication::translate("Nebulens", "06 / 输出");
        title = QCoreApplication::translate("Nebulens", "输出与历史");
        tabIndex = 2;
        refreshRecentResults();
        break;
    }

    if (m_stageKicker) m_stageKicker->setText(kicker);
    if (m_titleLabel) m_titleLabel->setText(title);
    m_tabs->setCurrentIndex(tabIndex);

    const bool stackPage = tabIndex == 0;
    const bool sequence = m_scene == ProcessingScene::StarTrail ||
        m_scene == ProcessingScene::Timelapse;
    if (m_assetsGroup) {
        m_assetsGroup->setVisible(
            stackPage && stage == WorkflowStage::Assets);
    }
    if (m_lightCalibrationGroup) {
        m_lightCalibrationGroup->setVisible(
            stackPage && stage == WorkflowStage::Calibration &&
            m_scene != ProcessingScene::DeepSky);
    }
    if (m_calibrationGroup) {
        m_calibrationGroup->setVisible(
            stackPage && stage == WorkflowStage::Calibration &&
            m_scene == ProcessingScene::DeepSky);
    }
    if (m_alignGroup) {
        m_alignGroup->setVisible(
            stackPage && stage == WorkflowStage::Alignment && !sequence);
    }
    if (m_stackGroup) {
        m_stackGroup->setVisible(
            stackPage && stage == WorkflowStage::Stacking && !sequence);
    }
    if (m_starTrailGroup) {
        m_starTrailGroup->setVisible(
            stackPage && stage == WorkflowStage::Stacking &&
            m_scene == ProcessingScene::StarTrail);
    }
    if (m_timelapseGroup) {
        m_timelapseGroup->setVisible(
            stackPage && stage == WorkflowStage::Stacking &&
            m_scene == ProcessingScene::Timelapse);
    }
    if (m_presetBar) {
        m_presetBar->setVisible(stage == WorkflowStage::Adjustment &&
                                m_scene != ProcessingScene::Timelapse);
    }
    if (m_footerBar) {
        m_footerBar->setVisible(stage == WorkflowStage::Adjustment &&
                                m_scene != ProcessingScene::Timelapse);
    }
    if (m_starReduceGroup) {
        m_starReduceGroup->setVisible(
            stage == WorkflowStage::Adjustment &&
            m_scene != ProcessingScene::StarTrail);
    }

    QWidget* page = m_tabs->currentWidget();
    if (!page || !isVisible()) return;
    if (m_stageAnimation) {
        m_stageAnimation->stop();
        m_stageAnimation->deleteLater();
        m_stageAnimation = nullptr;
    }
    if (m_animatedPage) {
        m_animatedPage->setGraphicsEffect(nullptr);
        m_animatedPage = nullptr;
    }
    auto* effect = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(effect);
    auto* animation = new QPropertyAnimation(effect, "opacity", page);
    m_stageAnimation = animation;
    m_animatedPage = page;
    animation->setDuration(StyleTokens::Motion::kBaseMs);
    animation->setStartValue(0.35);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, page,
            [this, page, animation]() {
                if (m_stageAnimation != animation) return;
                page->setGraphicsEffect(nullptr);
                m_stageAnimation = nullptr;
                m_animatedPage = nullptr;
                animation->deleteLater();
            });
    animation->start();
}

void ParamsPanel::setAssetSummary(int total, int available,
                                  const QString& reference, bool ready) {
    if (m_assetCountValue) {
        m_assetCountValue->setText(
            QStringLiteral("%1 / %2").arg(available).arg(total));
    }
    if (m_assetThresholdValue) {
        m_assetThresholdValue->setText(
            ready ? QCoreApplication::translate("Nebulens", "已满足")
                  : QCoreApplication::translate("Nebulens", "素材不足"));
        m_assetThresholdValue->setProperty(
            StyleTokens::Properties::kStatusRole,
            QString::fromLatin1(
                ready ? StyleTokens::Properties::kSuccess
                      : StyleTokens::Properties::kWarning));
        m_assetThresholdValue->style()->unpolish(m_assetThresholdValue);
        m_assetThresholdValue->style()->polish(m_assetThresholdValue);
    }
    if (m_assetReferenceValue) {
        m_assetReferenceValue->setText(
            reference.isEmpty() ? QCoreApplication::translate("Nebulens", "自动") : reference);
        m_assetReferenceValue->setToolTip(reference);
    }
}

void ParamsPanel::showCalibrationSettings() {
    if (!m_tabs || !m_calibrationGroup) {
        return;
    }
    showWorkflowStage(WorkflowStage::Calibration);
    if (m_scene == ProcessingScene::DeepSky) m_calibrationGroup->setChecked(true);
    QWidget* ancestor = m_calibrationGroup->parentWidget();
    while (ancestor) {
        if (auto* scrollArea = qobject_cast<QScrollArea*>(ancestor)) {
            scrollArea->ensureWidgetVisible(m_calibrationGroup, 0, 12);
            break;
        }
        ancestor = ancestor->parentWidget();
    }
}

void ParamsPanel::showOutputSettings() {
    if (!m_tabs) return;
    showWorkflowStage(WorkflowStage::Output);
}

void ParamsPanel::setProcessingReport(const QString& report) {
    if (!m_processingReportGroup || !m_processingReportLabel) return;
    m_processingReportLabel->setText(report);
    m_processingReportGroup->setVisible(!report.isEmpty());
}

void ParamsPanel::refreshRecentResults() {
    if (!m_recentResultsList || !m_recentResultsGroup) return;
    m_recentResultsList->clear();
    const QStringList filters = {
        QStringLiteral("*_stacked*.tif"),
        QStringLiteral("*_stacked*.tiff"),
        QStringLiteral("*_single*.tif"),
        QStringLiteral("*_single*.tiff"),
        QStringLiteral("*_star_trail*.tif"),
        QStringLiteral("*_star_trail*.tiff")
    };
    QFileInfoList files;
    QSet<QString> seenPaths;
    const QStringList directories = {
        outputPath(), legacyXingLianOutputPath(),
        legacyTechnicalOutputPath()
    };
    for (const QString& path : directories) {
        QDir directory(path);
        if (!directory.exists()) continue;
        const QFileInfoList candidates = directory.entryInfoList(
            filters, QDir::Files | QDir::Readable, QDir::Time);
        for (const QFileInfo& candidate : candidates) {
            const QString identity = candidate.absoluteFilePath();
            if (seenPaths.contains(identity)) continue;
            seenPaths.insert(identity);
            files.append(candidate);
        }
    }
    std::sort(files.begin(), files.end(),
              [](const QFileInfo& left, const QFileInfo& right) {
                  return left.lastModified() > right.lastModified();
              });
    const int count = std::min(5, static_cast<int>(files.size()));
    for (int index = 0; index < count; ++index) {
        const QFileInfo& file = files[index];
        auto* item = new QListWidgetItem(file.fileName(), m_recentResultsList);
        item->setData(Qt::UserRole, file.absoluteFilePath());
        item->setToolTip(QCoreApplication::translate("Nebulens", "%1\n%2 · %3 MB")
            .arg(file.absoluteFilePath())
            .arg(file.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
            .arg(file.size() / (1024.0 * 1024.0), 0, 'f', 1));
    }
    const bool available = count > 0;
    m_recentResultsGroup->setVisible(available);
    m_loadRecentResultBtn->setEnabled(available);
    m_revealRecentResultBtn->setEnabled(available);
    if (available) m_recentResultsList->setCurrentRow(0);
}

void ParamsPanel::emitParamsChanged() {
    emit paramsChanged();
}

void ParamsPanel::markPresetCustom() {
    if (!m_presetCombo || m_presetCombo->currentIndex() == 0) return;
    const QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->setCurrentIndex(0);
}

void ParamsPanel::updateStackMethodDescription() {
    if (!m_stackAlgorithm || !m_stackMethodDescription) return;

    QString description;
    switch (m_stackAlgorithm->currentIndex()) {
        case 0:
            description = QCoreApplication::translate("Nebulens",
                "<b>少帧稳健</b><br>逐像素取中位数，能压制飞机轨迹和热像素；信噪比提升低于平均值。");
            break;
        case 1:
            description = QCoreApplication::translate("Nebulens",
                "<b>细节优先</b><br>逐像素平均，信噪比利用率最高；异常轨迹、坏点和薄云更容易残留。");
            break;
        case 2:
            description = QCoreApplication::translate("Nebulens",
                "<b>均衡推荐</b><br>迭代剔除超出 κ 阈值的异常像素，适合 6–15 帧；κ 越小剔除越严格。");
            break;
        case 3:
            description = QCoreApplication::translate("Nebulens",
                "<b>长序列稳健</b><br>限制极端值后求均值，兼顾信噪比与异常值抑制；帧数越多越稳定。");
            break;
        default:
            break;
    }
    m_stackMethodDescription->setText(description);

    // κ only affects Kappa-Sigma. Disabling it for the other algorithms makes
    // the relationship visible and prevents users from tuning a no-op value.
    const bool usesKappa = m_stackAlgorithm->currentIndex() == 2;
    if (m_kappaNameLabel) m_kappaNameLabel->setEnabled(usesKappa);
    if (m_kappaSlider) m_kappaSlider->setEnabled(usesKappa);
    if (m_kappaLabel) m_kappaLabel->setEnabled(usesKappa);
}

QString ParamsPanel::stackMethod() const {
    if (!m_stackAlgorithm) return "average";
    int index = m_stackAlgorithm->currentIndex();
    // 0: Median, 1: Mean, 2: Kappa-Sigma, 3: Winsorized
    if (index == 0) return "median";
    if (index == 1) return "average";
    if (index == 2) return "kappa-sigma";
    if (index == 3) return "winsorized";
    return "average";
}

double ParamsPanel::kappaValue() const {
    if (!m_kappaSlider) return 2.5;
    return m_kappaSlider->value() / 10.0;
}

bool ParamsPanel::autoRejectLowQualityFrames() const {
    return m_autoRejectQualityCheck
        ? m_autoRejectQualityCheck->isChecked() : true;
}

bool ParamsPanel::photometricNormalizationEnabled() const {
    return m_photometricCheck ? m_photometricCheck->isChecked() : true;
}

bool ParamsPanel::dewarpEnabled() const {
    return m_dewarpCheck ? m_dewarpCheck->isChecked() : false;
}

int ParamsPanel::dewarpStrength() const {
    return m_dewarpSlider ? m_dewarpSlider->value() : 0;
}

bool ParamsPanel::noiseReductionEnabled() const {
    return m_noiseReductionCheck ? m_noiseReductionCheck->isChecked() : false;
}

bool ParamsPanel::modifiedCameraColorEnabled() const {
    return m_modifiedCameraColorCheck &&
        m_modifiedCameraColorCheck->isChecked();
}

int ParamsPanel::modifiedCameraColorStrength() const {
    return m_modifiedCameraColorStrengthSlider
        ? m_modifiedCameraColorStrengthSlider->value() : 100;
}

ModifiedCameraNeutralMode ParamsPanel::modifiedCameraColorMode() const {
    if (m_modifiedCameraColorMode &&
        m_modifiedCameraColorMode->currentIndex() == 1 &&
        hasModifiedCameraGrayPoint()) {
        return ModifiedCameraNeutralMode::ManualPoint;
    }
    return ModifiedCameraNeutralMode::Automatic;
}

bool ParamsPanel::hasModifiedCameraGrayPoint() const {
    return std::isfinite(m_modifiedCameraGrayPointX) &&
        std::isfinite(m_modifiedCameraGrayPointY) &&
        m_modifiedCameraGrayPointX >= 0.0 &&
        m_modifiedCameraGrayPointX <= 1.0 &&
        m_modifiedCameraGrayPointY >= 0.0 &&
        m_modifiedCameraGrayPointY <= 1.0;
}

double ParamsPanel::modifiedCameraGrayPointX() const {
    return m_modifiedCameraGrayPointX;
}

double ParamsPanel::modifiedCameraGrayPointY() const {
    return m_modifiedCameraGrayPointY;
}

void ParamsPanel::setModifiedCameraGrayPoint(double normalizedX,
                                              double normalizedY) {
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY)) return;
    m_modifiedCameraGrayPointX = std::clamp(normalizedX, 0.0, 1.0);
    m_modifiedCameraGrayPointY = std::clamp(normalizedY, 0.0, 1.0);
    const QSignalBlocker checkBlocker(m_modifiedCameraColorCheck);
    const QSignalBlocker modeBlocker(m_modifiedCameraColorMode);
    m_modifiedCameraColorCheck->setChecked(true);
    m_modifiedCameraColorMode->setCurrentIndex(1);
    updateModifiedCameraColorControls();
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::clearModifiedCameraGrayPoint() {
    m_modifiedCameraGrayPointX = -1.0;
    m_modifiedCameraGrayPointY = -1.0;
    if (m_modifiedCameraColorMode) {
        const QSignalBlocker blocker(m_modifiedCameraColorMode);
        m_modifiedCameraColorMode->setCurrentIndex(0);
    }
    updateModifiedCameraColorControls();
}

int ParamsPanel::noiseReductionStrength() const {
    return m_noiseReductionSlider ? m_noiseReductionSlider->value() : 30;
}

bool ParamsPanel::stretchEnabled() const {
    return m_stretchCheck ? m_stretchCheck->isChecked() : false;
}

BasicAdjustmentOptions ParamsPanel::basicAdjustmentOptions() const {
    BasicAdjustmentOptions options;
    if (!m_temperatureSlider) return options;
    options.temperature = m_temperatureSlider->value();
    options.tint = m_tintSlider->value();
    options.exposureTenths = m_exposureSlider->value();
    options.contrast = m_contrastSlider->value();
    options.highlights = m_highlightsSlider->value();
    options.shadows = m_shadowsSlider->value();
    options.whites = m_whitesSlider->value();
    options.blacks = m_blacksSlider->value();
    options.vibrance = m_vibranceSlider->value();
    options.saturation = m_saturationSlider->value();
    options.sharpening = m_sharpeningSlider->value();
    return options;
}

bool ParamsPanel::starDefringeEnabled() const {
    return m_starDefringeCheck ? m_starDefringeCheck->isChecked() : false;
}

int ParamsPanel::starDefringeStrength() const {
    return m_starDefringeSlider ? m_starDefringeSlider->value() : 0;
}

bool ParamsPanel::starReduceEnabled() const {
    return m_starReduceCheck ? m_starReduceCheck->isChecked() : false;
}

int ParamsPanel::starReduceStrength() const {
    return m_starReduceSlider ? m_starReduceSlider->value() : 0;
}

QString ParamsPanel::outputFormat() const {
    const QString format = m_outputFormat->currentData().toString();
    return format.isEmpty() ? QStringLiteral("tiff16") : format;
}

void ParamsPanel::setOutputFormat(const QString& format) {
    const int index = m_outputFormat->findData(format);
    if (index >= 0) m_outputFormat->setCurrentIndex(index);
}

QString ParamsPanel::outputPath() const {
    return m_outputPath ? m_outputPath->text()
                        : defaultOutputPath();
}

void ParamsPanel::setOutputPath(const QString& path) {
    if (m_outputPath) {
        displayOutputPath(m_outputPath, path);
        refreshRecentResults();
        markPresetCustom();
        emitParamsChanged();
    }
}

void ParamsPanel::clearProjectSources() {
    m_debounceTimer->stop();
    m_orderSettingsRestored = false;
    clearCalibrationSources();
    clearModifiedCameraGrayPoint();
    clearUserMask();
    updateRefFrameList({});
    setAssetSummary(0, 0, QString(), false);
    setProcessingReport(QString());
    emitParamsChanged();
}

void ParamsPanel::clearUserMask() {
    m_userMaskPath.clear();
    m_maskPathLabel->clear();
    const QSignalBlocker blocker(m_skyGroundMode);
    m_skyGroundMode->setCurrentIndex(0);
    updateSkyGroundControls();
}

void ParamsPanel::updateRefFrameList(const QStringList& fileNames) {
    if (m_refFrame->count() > 0 && fileNames == m_referenceFiles) return;
    m_referenceFiles = fileNames;
    const QSignalBlocker blocker(m_refFrame);
    m_refFrame->clear();
    m_refFrame->addItem(QCoreApplication::translate("Nebulens", "自动选择"), QString());
    for (const QString& path : fileNames) {
        m_refFrame->addItem(QFileInfo(path).fileName(), path);
    }
    const int index = m_refFrame->findData(m_referenceChoice);
    m_refFrame->setCurrentIndex(std::max(index, 0));
    m_referenceChoice = m_refFrame->currentData().toString();
}

void ParamsPanel::setSelectedReferenceFrame(const QString& filePath) {
    const int index = filePath.isEmpty() ? 0 : m_refFrame->findData(filePath);
    if (index < 0) return;
    const QSignalBlocker blocker(m_refFrame);
    m_refFrame->setCurrentIndex(index);
    m_referenceChoice = filePath;
}

QString ParamsPanel::selectedReferenceFrame() const {
    return m_referenceChoice;
}

bool ParamsPanel::skyGroundSeparationEnabled() const {
    return m_skyGroundCheck ? m_skyGroundCheck->isChecked() : false;
}

SkyGroundMask::Mode ParamsPanel::skyGroundMode() const {
    if (!m_skyGroundMode) return SkyGroundMask::AutoDetect;
    return m_skyGroundMode->currentIndex() == 0 ? SkyGroundMask::AutoDetect : SkyGroundMask::UserMask;
}

QString ParamsPanel::userMaskPath() const {
    return m_userMaskPath;
}

int ParamsPanel::featherRadius() const {
    return m_featherSlider ? m_featherSlider->value() : 20;
}

QString ParamsPanel::groundStackMethod() const {
    return m_groundStackMethod
        ? m_groundStackMethod->currentData().toString() : QString("average");
}

int ParamsPanel::groundDetailStrength() const {
    return m_groundDetailSlider ? m_groundDetailSlider->value() : 40;
}

int ParamsPanel::timelapseWindowSize() const {
    return m_timelapseWindow
        ? m_timelapseWindow->currentData().toInt() : 3;
}

int ParamsPanel::timelapseStrength() const {
    return m_timelapseStrengthSlider
        ? m_timelapseStrengthSlider->value() : 80;
}

int ParamsPanel::timelapseMotionProtection() const {
    return m_timelapseMotionProtectionSlider
        ? m_timelapseMotionProtectionSlider->value() : 75;
}

bool ParamsPanel::timelapseProtectGround() const {
    return m_timelapseProtectGroundCheck &&
        m_timelapseProtectGroundCheck->isChecked();
}

int ParamsPanel::starTrailCometStrength() const {
    return m_starTrailCometSlider ? m_starTrailCometSlider->value() : 0;
}

bool ParamsPanel::starTrailReverse() const {
    return m_starTrailReverseCheck && m_starTrailReverseCheck->isChecked();
}

bool ParamsPanel::starTrailProtectGround() const {
    return m_starTrailProtectGroundCheck &&
        m_starTrailProtectGroundCheck->isChecked();
}

QStringList ParamsPanel::darkFramePaths() const {
    return m_darkFramePaths;
}

QStringList ParamsPanel::flatFramePaths() const {
    return m_flatFramePaths;
}

QStringList ParamsPanel::biasFramePaths() const {
    return m_biasFramePaths;
}

QStringList ParamsPanel::darkFlatFramePaths() const {
    return m_darkFlatFramePaths;
}

QString ParamsPanel::masterDarkPath() const {
    return m_masterDarkPath;
}

QString ParamsPanel::masterFlatPath() const {
    return m_masterFlatPath;
}

QString ParamsPanel::masterBiasPath() const {
    return m_masterBiasPath;
}

QString ParamsPanel::masterDarkFlatPath() const {
    return m_masterDarkFlatPath;
}

bool ParamsPanel::saveGeneratedMasters() const {
    return m_saveGeneratedMastersCheck->isChecked() &&
        calibrationSources(*this).hasRaw();
}

bool ParamsPanel::deepSkyCalibrationInputsComplete() const {
    return calibrationSources(*this).ready();
}

QString ParamsPanel::calibrationHint() const {
    const CalibrationSources sources = calibrationSources(*this);
    if (sources.ready()) {
        return QCoreApplication::translate("Nebulens", "校准来源已齐，处理前将检查兼容性");
    }

    QStringList issues;
    const auto addMissing = [&issues](CalibrationSource source, const QString& role) {
        if (source.ready()) return;
        if (!source.present()) {
            issues.append(QCoreApplication::translate("Nebulens", "缺少%1").arg(role));
        } else {
            issues.append(QCoreApplication::translate("Nebulens", "%1还差 %2 张 RAW")
                .arg(role).arg(kMinimumCalibrationRawFrames - source.rawCount));
        }
    };

    addMissing(sources.dark, QCoreApplication::translate("Nebulens", "暗场"));
    addMissing(sources.flat, QCoreApplication::translate("Nebulens", "平场"));
    if (sources.missingFlatOffset() && sources.flat.rawCount > 0) {
        issues.append(QCoreApplication::translate("Nebulens",
            "RAW 平场需偏置场或同曝光的暗平场"));
    } else if (sources.conflictingFlatOffsets() && sources.flat.rawCount > 0) {
        issues.append(QCoreApplication::translate("Nebulens",
            "请在偏置场和暗平场中移除一组"));
    } else {
        if (sources.unusedBias()) {
            issues.append(sources.dark.present()
                ? QCoreApplication::translate("Nebulens",
                    "Master Flat 搭配 RAW 暗场不需要偏置场，请移除偏置场")
                : QCoreApplication::translate("Nebulens",
                    "保留偏置场需搭配已减偏置的 Master Dark"));
        } else if (sources.bias.present()) {
            addMissing(sources.bias, QCoreApplication::translate("Nebulens", "偏置场"));
        }
        if (sources.unusedDarkFlat()) {
            issues.append(QCoreApplication::translate("Nebulens",
                "Master Flat 不需要暗平场，请移除暗平场"));
        } else if (sources.darkFlat.present()) {
            addMissing(sources.darkFlat, QCoreApplication::translate("Nebulens", "暗平场"));
        }
    }
    return issues.join(QCoreApplication::translate("Nebulens", "；"));
}

void ParamsPanel::importCalibrationFrames(QStringList& paths,
                                          QLabel* countLabel,
                                          QPushButton* clearButton,
                                          const QString& dialogTitle) {
    const QStringList selected = FileDialogUtils::getOpenFileNames(
        this, dialogTitle, QString(),
        QCoreApplication::translate("Nebulens",
            "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef *.cr3);;"
            "所有文件 (*)"));
    if (selected.isEmpty()) return;

    QSet<QString> otherCalibrationPaths;
    const std::array<const QStringList*, 4> lists = {
        &m_darkFramePaths, &m_flatFramePaths, &m_biasFramePaths,
        &m_darkFlatFramePaths};
    for (const QStringList* list : lists) {
        if (list == &paths) continue;
        for (const QString& path : *list) {
            otherCalibrationPaths.insert(calibrationPathIdentity(path));
        }
    }
    const std::array<const QString*, 4> masters = {
        &m_masterDarkPath, &m_masterFlatPath, &m_masterBiasPath,
        &m_masterDarkFlatPath};
    QString* const ownMaster = masterPathForRawGroup(paths);
    for (const QString* master : masters) {
        if (master == ownMaster || master->isEmpty()) continue;
        otherCalibrationPaths.insert(calibrationPathIdentity(*master));
    }

    QStringList merged = paths;
    QSet<QString> currentCalibrationPaths;
    for (const QString& path : paths) {
        currentCalibrationPaths.insert(calibrationPathIdentity(path));
    }
    int categoryConflicts = 0;
    for (const QString& path : selected) {
        const QString absolutePath =
            QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        const QString identity = calibrationPathIdentity(path);
        if (otherCalibrationPaths.contains(identity)) {
            ++categoryConflicts;
        } else if (!absolutePath.isEmpty() &&
                   !currentCalibrationPaths.contains(identity)) {
            merged.append(absolutePath);
            currentCalibrationPaths.insert(identity);
        }
    }
    if (categoryConflicts > 0) {
        QMessageBox::information(
            this, QCoreApplication::translate("Nebulens", "已跳过重复校准帧"),
            QCoreApplication::translate("Nebulens",
                "%1 张 RAW 已属于另一类校准帧，未重复导入。")
                .arg(categoryConflicts));
    }
    if (merged == paths) return;

    paths = merged;
    if (ownMaster) ownMaster->clear();
    updateCalibrationSource(countLabel, clearButton, paths,
                            ownMaster ? *ownMaster : QString());
    updateCalibrationStatus();
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::importCalibrationMaster(QString& masterPath,
                                          QStringList& rawPaths,
                                          QLabel* sourceLabel,
                                          QPushButton* clearButton,
                                          const QString& roleName) {
    const QString selected = FileDialogUtils::getOpenFileName(
        this,
        QCoreApplication::translate("Nebulens", "选择 Master %1")
            .arg(roleName),
        QString(),
        QCoreApplication::translate(
            "Nebulens", "Nebulens Master (*.spmaster)"));
    if (selected.isEmpty()) return;

    const QFileInfo selectedInfo(selected);
    if (selectedInfo.suffix().compare(
            QStringLiteral("spmaster"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(
            this, QCoreApplication::translate("Nebulens", "不支持的 Master 文件"),
            QCoreApplication::translate("Nebulens",
                "Master 只能导入由 Nebulens 生成的 .spmaster 文件。"));
        return;
    }
    const QString absolutePath = QDir::cleanPath(selectedInfo.absoluteFilePath());
    if (calibrationPathUsedElsewhere(
            absolutePath, &rawPaths, &masterPath)) {
        QMessageBox::warning(
            this, QCoreApplication::translate("Nebulens", "Master 已被使用"),
            QCoreApplication::translate("Nebulens",
                "同一个文件不能同时分配给多个校准角色。"));
        return;
    }

    rawPaths.clear();
    masterPath = absolutePath;
    updateCalibrationSource(sourceLabel, clearButton, rawPaths, masterPath);
    updateCalibrationStatus();
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::clearCalibrationSource(QStringList& paths,
                                         QString& masterPath,
                                         QLabel* sourceLabel,
                                         QPushButton* clearButton) {
    if (paths.isEmpty() && masterPath.isEmpty()) return;
    paths.clear();
    masterPath.clear();
    updateCalibrationSource(sourceLabel, clearButton, paths, masterPath);
    updateCalibrationStatus();
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::clearCalibrationSources() {
    m_darkFramePaths.clear();
    m_flatFramePaths.clear();
    m_biasFramePaths.clear();
    m_darkFlatFramePaths.clear();
    m_masterDarkPath.clear();
    m_masterFlatPath.clear();
    m_masterBiasPath.clear();
    m_masterDarkFlatPath.clear();
    updateCalibrationSource(m_darkFrameCount, m_darkFrameClear, {}, {});
    updateCalibrationSource(m_flatFrameCount, m_flatFrameClear, {}, {});
    updateCalibrationSource(m_biasFrameCount, m_biasFrameClear, {}, {});
    updateCalibrationSource(m_darkFlatFrameCount, m_darkFlatFrameClear, {}, {});
    updateCalibrationStatus();
}

QString* ParamsPanel::masterPathForRawGroup(const QStringList& paths) {
    if (&paths == &m_darkFramePaths) return &m_masterDarkPath;
    if (&paths == &m_flatFramePaths) return &m_masterFlatPath;
    if (&paths == &m_biasFramePaths) return &m_masterBiasPath;
    if (&paths == &m_darkFlatFramePaths) return &m_masterDarkFlatPath;
    return nullptr;
}

QString ParamsPanel::calibrationPathIdentity(const QString& path) const {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(
        canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool ParamsPanel::calibrationPathUsedElsewhere(
    const QString& path, const QStringList* ignoredRaw,
    const QString* ignoredMaster) const {
    const QString identity = calibrationPathIdentity(path);
    const std::array<const QStringList*, 4> lists = {
        &m_darkFramePaths, &m_flatFramePaths, &m_biasFramePaths,
        &m_darkFlatFramePaths};
    for (const QStringList* list : lists) {
        if (list == ignoredRaw) continue;
        for (const QString& existing : *list) {
            if (calibrationPathIdentity(existing) == identity) return true;
        }
    }
    const std::array<const QString*, 4> masters = {
        &m_masterDarkPath, &m_masterFlatPath, &m_masterBiasPath,
        &m_masterDarkFlatPath};
    for (const QString* master : masters) {
        if (master == ignoredMaster || master->isEmpty()) continue;
        if (calibrationPathIdentity(*master) == identity) return true;
    }
    return false;
}

void ParamsPanel::updateCalibrationStatus() {
    const CalibrationSources sources = calibrationSources(*this);
    const bool ready = sources.ready();
    m_calibrationStatus->setText(calibrationHint());
    m_calibrationStatus->setProperty(
        "status", ready ? "ready" : "waiting");
    m_calibrationStatus->setProperty(
        StyleTokens::Properties::kStatusRole,
        ready ? StyleTokens::Properties::kSuccess
              : StyleTokens::Properties::kWarning);
    if (m_calibrationStatus->style()) {
        m_calibrationStatus->style()->unpolish(m_calibrationStatus);
        m_calibrationStatus->style()->polish(m_calibrationStatus);
    }
    m_calibrationStatus->update();

    const bool expanded = m_calibrationGroup->isChecked();
    const bool rawFlat = sources.flat.rawCount > 0 && !sources.flat.master;
    m_biasFrameRow->setVisible(
        expanded && (rawFlat || sources.dark.master || sources.bias.present()));
    m_darkFlatFrameRow->setVisible(expanded && (rawFlat || sources.darkFlat.present()));
    m_calibrationDetails->setVisible(expanded && m_calibrationDetailsButton->isChecked());

    const bool hasRawSources = sources.hasRaw();
    if (!hasRawSources && m_saveGeneratedMastersCheck->isChecked()) {
        const QSignalBlocker blocker(m_saveGeneratedMastersCheck);
        m_saveGeneratedMastersCheck->setChecked(false);
    }
    m_saveGeneratedMastersCheck->setVisible(expanded && hasRawSources);
    m_saveGeneratedMastersCheck->setEnabled(hasRawSources);
}

QString ParamsPanel::upstreamSignature() const {
    return QStringList{
        QStringLiteral("star"), selectedReferenceFrame(), stackMethod(),
        QString::number(kappaValue(), 'f', 1),
        QString::number(autoRejectLowQualityFrames()),
        QString::number(photometricNormalizationEnabled()),
        QString::number(skyGroundSeparationEnabled()),
        QString::number(static_cast<int>(skyGroundMode())), userMaskPath(),
        QString::number(featherRadius()), groundStackMethod(),
        QString::number(timelapseWindowSize()),
        QString::number(timelapseStrength()),
        QString::number(timelapseMotionProtection()),
        QString::number(timelapseProtectGround()),
        QString::number(starTrailCometStrength()),
        QString::number(starTrailReverse()),
        QString::number(starTrailProtectGround()),
        QStringLiteral("dark=") + m_darkFramePaths.join(QChar(0x1e)),
        QStringLiteral("flat=") + m_flatFramePaths.join(QChar(0x1e)),
        QStringLiteral("bias=") + m_biasFramePaths.join(QChar(0x1e)),
        QStringLiteral("darkFlat=") + m_darkFlatFramePaths.join(QChar(0x1e)),
        QStringLiteral("masterDark=") + m_masterDarkPath,
        QStringLiteral("masterFlat=") + m_masterFlatPath,
        QStringLiteral("masterBias=") + m_masterBiasPath,
        QStringLiteral("masterDarkFlat=") + m_masterDarkFlatPath,
        QStringLiteral("saveGeneratedMasters=") +
            QString::number(saveGeneratedMasters())
    }.join('|');
}

QString ParamsPanel::finishingSignature() const {
    const BasicAdjustmentOptions basic = basicAdjustmentOptions();
    return QStringList{
        QString::number(noiseReductionEnabled()),
        QString::number(noiseReductionStrength()),
        QString::number(modifiedCameraColorEnabled()),
        QString::number(modifiedCameraColorStrength()),
        QString::number(static_cast<int>(modifiedCameraColorMode())),
        QString::number(modifiedCameraGrayPointX(), 'f', 6),
        QString::number(modifiedCameraGrayPointY(), 'f', 6),
        QString::number(dewarpEnabled()), QString::number(dewarpStrength()),
        QString::number(stretchEnabled()),
        QString::number(basic.temperature), QString::number(basic.tint),
        QString::number(basic.exposureTenths), QString::number(basic.contrast),
        QString::number(basic.highlights), QString::number(basic.shadows),
        QString::number(basic.whites), QString::number(basic.blacks),
        QString::number(basic.vibrance), QString::number(basic.saturation),
        QString::number(basic.sharpening),
        QString::number(starDefringeEnabled()),
        QString::number(starDefringeStrength()),
        QString::number(starReduceEnabled()),
        QString::number(starReduceStrength()),
        QString::number(groundDetailStrength())
    }.join('|');
}

QString ParamsPanel::processingSignature() const {
    // Output path/format are intentionally excluded: changing only where or
    // how a cached result is written does not make its pixels stale.
    return upstreamSignature() + QStringLiteral("||") + finishingSignature();
}

void ParamsPanel::setMaskPreview(const std::vector<uint8_t>& mask, int w, int h) {
    Q_UNUSED(mask) Q_UNUSED(w) Q_UNUSED(h)
    // 蒙版预览由 PreviewPanel 处理，此处仅作接口预留
}

void ParamsPanel::setDetectMaskEnabled(bool enabled) {
    if (m_detectMaskBtn) m_detectMaskBtn->setEnabled(enabled);
}
