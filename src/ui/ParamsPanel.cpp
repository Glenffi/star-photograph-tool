#include "ParamsPanel.h"
#include "SceneLauncher.h"
#include "UiAssets.h"
#include "core/PresetManager.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
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

const char* kPanelStyle = R"QSS(
QWidget#paramsPanel {
    background-color: #14191A;
    color: #DCE5E2;
    font-size: 12px;
}
QWidget#paramsTitleBar {
    background-color: #181E1F;
    border-bottom: 1px solid #283132;
}
QWidget#paramsPresetBar {
    background-color: #151B1C;
    border-bottom: 1px solid #252E2F;
}
QWidget#paramsFooter {
    background-color: #171D1E;
    border-top: 1px solid #293334;
}
QLabel {
    color: #CBD6D3;
    background-color: transparent;
    border: none;
    letter-spacing: 0;
}
QLabel[role="panelTitle"] {
    color: #F2F6F5;
    font-size: 15px;
    font-weight: 700;
}
QLabel[role="sectionTitle"] {
    color: #EEF4F2;
    font-size: 13px;
    font-weight: 700;
}
QLabel[role="muted"] {
    color: #81938F;
    font-size: 11px;
}
QLabel[role="value"] {
    color: #8FDCC7;
    font-size: 11px;
    font-weight: 600;
}
QLabel[role="calibrationStatus"] {
    border: 1px solid #554B2C;
    border-radius: 4px;
    padding: 7px 9px;
    color: #C4B99A;
    background-color: #272316;
    font-size: 11px;
    font-weight: 400;
}
QLabel[role="calibrationStatus"][status="ready"] {
    color: #9FD9C8;
    background-color: #142821;
    border-color: #285746;
}
QLabel[role="note"] {
    color: #9FB0AC;
    background-color: #182321;
    border: none;
    border-left: 2px solid #4DCFA9;
    border-radius: 3px;
    padding: 8px 10px;
    font-size: 11px;
    font-weight: 400;
}
QLabel[role="noteWarm"] {
    color: #B7ACA7;
    background-color: #211D1C;
    border: none;
    border-left: 2px solid #E99A75;
    border-radius: 3px;
    padding: 8px 10px;
    font-size: 11px;
    font-weight: 400;
}
QTabWidget#paramsTabs::pane {
    background-color: #14191A;
    border: none;
    border-top: 1px solid #293334;
}
QTabWidget#paramsTabs > QTabBar {
    background-color: #171D1E;
}
QTabWidget#paramsTabs QTabBar::tab {
    min-height: 38px;
    padding: 0 12px;
    color: #859692;
    background-color: #171D1E;
    border: none;
    border-bottom: 2px solid transparent;
    font-size: 12px;
    font-weight: 500;
}
QTabWidget#paramsTabs QTabBar::tab:hover {
    color: #D7E0DE;
    background-color: #1C2425;
}
QTabWidget#paramsTabs QTabBar::tab:selected {
    color: #F2F6F5;
    background-color: #1A2222;
    border-bottom-color: #54D5B0;
    font-weight: 700;
}
QScrollArea#paramsScrollArea,
QScrollArea#paramsScrollArea > QWidget > QWidget,
QWidget#paramsTabPage,
QWidget#paramsPageContent {
    background-color: #14191A;
    border: none;
}
QGroupBox {
    color: #EDF3F1;
    background-color: transparent;
    border: none;
    border-top: 1px solid #293334;
    border-radius: 0;
    margin-top: 21px;
    padding-top: 8px;
    font-size: 13px;
    font-weight: 700;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 0;
    padding: 0 12px 0 0;
    color: #EDF3F1;
    background-color: #14191A;
}
QWidget[role="plainSection"] {
    background-color: transparent;
    border: none;
    border-top: 1px solid #293334;
}
QComboBox, QLineEdit {
    min-height: 32px;
    color: #E5ECEA;
    background-color: #1B2324;
    border: 1px solid #344142;
    border-radius: 5px;
    padding: 0 10px;
    selection-background-color: #2A5B50;
}
QComboBox:hover, QLineEdit:hover {
    background-color: #20292A;
    border-color: #475756;
}
QComboBox:focus, QLineEdit:focus {
    border-color: #54D5B0;
}
QComboBox:disabled, QLineEdit:disabled {
    color: #62716E;
    background-color: #171D1E;
    border-color: #283132;
}
QComboBox::drop-down {
    width: 28px;
    border: none;
}
QComboBox QAbstractItemView {
    color: #E5ECEA;
    background-color: #1C2425;
    border: 1px solid #3A4848;
    outline: 0;
    padding: 4px;
    selection-color: #F5F8F7;
    selection-background-color: #286253;
}
QLineEdit[readOnly="true"] {
    color: #AAB8B4;
    background-color: #182021;
}
QCheckBox {
    min-height: 24px;
    spacing: 8px;
    color: #CDD7D4;
    background-color: transparent;
    font-size: 12px;
}
QCheckBox:hover {
    color: #F1F5F4;
}
QCheckBox:disabled {
    color: #62716E;
}
QCheckBox::indicator {
    width: 15px;
    height: 15px;
    background-color: #182021;
    border: 1px solid #53605E;
    border-radius: 4px;
}
QCheckBox::indicator:hover {
    border-color: #72DDC0;
}
QCheckBox::indicator:checked {
    background-color: #54D5B0;
    border-color: #54D5B0;
}
QCheckBox::indicator:disabled {
    background-color: #181E1F;
    border-color: #303A3A;
}
QSlider {
    min-height: 20px;
    background-color: transparent;
}
QSlider::groove:horizontal {
    height: 3px;
    background-color: #33403F;
    border-radius: 1px;
}
QSlider::sub-page:horizontal {
    height: 3px;
    background-color: #54D5B0;
    border-radius: 1px;
}
QSlider::handle:horizontal {
    width: 13px;
    height: 13px;
    margin: -5px 0;
    background-color: #E6F6F1;
    border: 2px solid #54D5B0;
    border-radius: 7px;
}
QSlider::handle:horizontal:hover {
    background-color: #FFFFFF;
    border-color: #78E1C4;
}
QSlider::sub-page:horizontal:disabled,
QSlider::handle:horizontal:disabled {
    background-color: #465250;
    border-color: #465250;
}
QPushButton {
    min-height: 30px;
    color: #D9E3E0;
    background-color: #1D2627;
    border: 1px solid #384646;
    border-radius: 5px;
    padding: 0 12px;
    font-size: 11px;
    font-weight: 600;
}
QPushButton:hover {
    color: #F4F8F7;
    background-color: #253031;
    border-color: #526261;
}
QPushButton:pressed {
    background-color: #1B4B40;
    border-color: #54D5B0;
}
QPushButton:disabled {
    color: #5D6B68;
    background-color: #181E1F;
    border-color: #2B3435;
}
QPushButton[variant="primary"] {
    color: #10201C;
    background-color: #59D7B2;
    border-color: #59D7B2;
}
QPushButton[variant="primary"]:hover {
    background-color: #73E2C3;
    border-color: #73E2C3;
}
QPushButton[variant="ghost"] {
    color: #91A19D;
    background-color: transparent;
    border-color: transparent;
}
QPushButton[variant="ghost"]:hover {
    color: #E2E9E7;
    background-color: #202829;
}
QPushButton[variant="icon"] {
    min-width: 31px;
    max-width: 31px;
    min-height: 31px;
    max-height: 31px;
    padding: 0;
}
QScrollBar:vertical {
    width: 7px;
    margin: 4px 1px;
    background-color: transparent;
}
QScrollBar::handle:vertical {
    min-height: 28px;
    background-color: #3A4746;
    border-radius: 3px;
}
QScrollBar::handle:vertical:hover {
    background-color: #53625F;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    height: 0;
    background-color: transparent;
}
QToolTip {
    color: #EAF0EE;
    background-color: #202829;
    border: 1px solid #42504F;
    padding: 5px 7px;
}
)QSS";

void setRole(QWidget* widget, const char* role) {
    if (widget) widget->setProperty("role", role);
}

void setButtonVariant(QPushButton* button, const char* variant) {
    if (!button) return;
    button->setProperty("variant", variant);
    button->setCursor(Qt::PointingHandCursor);
}

void displayOutputPath(QLineEdit* edit, const QString& path) {
    if (!edit) return;
    edit->setText(path);
    edit->setToolTip(path);
    edit->setCursorPosition(0);
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

} // namespace

ParamsPanel::ParamsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    loadPreset();
}

void ParamsPanel::setupUI() {
    setObjectName("paramsPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标题栏与预设保持固定，长参数放入各自可滚动的工作阶段页。
    auto* titleBar = new QWidget(this);
    titleBar->setObjectName("paramsTitleBar");
    titleBar->setFixedHeight(52);
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(18, 0, 18, 0);
    m_titleLabel = new QLabel(QString::fromUtf8("处理参数"), titleBar);
    setRole(m_titleLabel, "panelTitle");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();
    layout->addWidget(titleBar);

    m_presetBar = new QWidget(this);
    m_presetBar->setObjectName("paramsPresetBar");
    m_presetBar->setFixedHeight(56);
    auto* presetRow = new QHBoxLayout(m_presetBar);
    presetRow->setContentsMargins(18, 10, 18, 10);
    presetRow->setSpacing(12);
    auto* presetLabel = new QLabel(QString::fromUtf8("预设"), m_presetBar);
    presetLabel->setFixedWidth(36);
    setRole(presetLabel, "muted");
    presetRow->addWidget(presetLabel);
    m_presetCombo = new QComboBox(m_presetBar);
    m_presetCombo->addItem(QString::fromUtf8("自定义"));
    for (const Preset& preset : PresetManager::builtinPresets()) {
        m_presetCombo->addItem(preset.name);
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onPresetChanged);
    presetRow->addWidget(m_presetCombo, 1);
    layout->addWidget(m_presetBar);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("paramsTabs");
    m_tabs->setDocumentMode(true);
    m_tabs->setIconSize(QSize(15, 15));
    m_tabs->tabBar()->setExpanding(true);
    m_tabs->tabBar()->setDrawBase(false);

    auto createPage = [this](const QString& name) -> QVBoxLayout* {
        auto* page = new QWidget(m_tabs);
        page->setObjectName("paramsTabPage");
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(page);
        scroll->setObjectName("paramsScrollArea");
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget(scroll);
        content->setObjectName("paramsPageContent");
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(18, 14, 18, 20);
        contentLayout->setSpacing(14);
        scroll->setWidget(content);
        pageLayout->addWidget(scroll);
        m_tabs->addTab(page, name);
        return contentLayout;
    };

    QVBoxLayout* stackPageLayout = createPage(QString::fromUtf8("堆栈"));
    QVBoxLayout* adjustPageLayout = createPage(QString::fromUtf8("调整"));
    QVBoxLayout* outputPageLayout = createPage(QString::fromUtf8("输出"));
    m_tabs->setTabIcon(
        0, UiAssets::icon(UiAssets::Glyph::DeepSky, QColor("#83A39B")));
    m_tabs->setTabIcon(
        1, UiAssets::icon(UiAssets::Glyph::Sliders, QColor("#83A39B")));
    m_tabs->setTabIcon(
        2, UiAssets::icon(UiAssets::Glyph::Export, QColor("#83A39B")));
    layout->addWidget(m_tabs, 1);

    // Deep-sky calibration runs before alignment and stacking. The paths are
    // intentionally session-only because removable disks and capture folders
    // often move between launches.
    m_calibrationGroup = createCollapsibleGroup(
        QString::fromUtf8("校准帧"), true);
    m_calibrationGroup->setVisible(false);
    auto* calibrationLayout = new QVBoxLayout(m_calibrationGroup);
    calibrationLayout->setSpacing(7);

    auto* calibrationNote = new QLabel(
        QString::fromUtf8(
            "Light 主帧在左侧素材栏导入。\n"
            "每类可使用一组 RAW 原片，或一个由 StarProcessor 生成的 "
            ".spmaster，二者自动互斥。\n"
            "生成 Master Flat 时，Bias 与 Dark Flat 选择一种；"
            "每组 RAW 至少 3 张，建议 10–20 张。"),
        m_calibrationGroup);
    calibrationNote->setWordWrap(true);
    setRole(calibrationNote, "note");
    calibrationLayout->addWidget(calibrationNote);

    // A raw group and its project Master represent the same calibration
    // source. The two import actions share one status label and selecting one
    // clears the other, so an ambiguous configuration cannot be created here.
    auto addCalibrationRow = [this, calibrationLayout](
                                 const QString& name,
                                 const QString& purpose,
                                 QStringList& paths,
                                 QString& masterPath,
                                 QLabel*& sourceLabel,
                                 QPushButton*& clearButton,
                                 bool confirmNormalizedFlat = false) {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);

        auto* nameLabel = new QLabel(name, m_calibrationGroup);
        nameLabel->setFixedWidth(54);
        row->addWidget(nameLabel);

        sourceLabel = new QLabel(QString::fromUtf8("未选择"), m_calibrationGroup);
        sourceLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sourceLabel->setTextFormat(Qt::PlainText);
        sourceLabel->setMinimumWidth(44);
        sourceLabel->setSizePolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        setRole(sourceLabel, "muted");
        row->addWidget(sourceLabel, 1);

        auto* rawButton = new QPushButton(QStringLiteral("RAW"), m_calibrationGroup);
        rawButton->setFixedSize(48, 28);
        rawButton->setToolTip(
            QString::fromUtf8("导入%1 RAW 原片组：%2").arg(name, purpose));
        rawButton->setAccessibleName(rawButton->toolTip());
        row->addWidget(rawButton);

        auto* masterButton = new QPushButton(
            QString::fromUtf8("主帧"), m_calibrationGroup);
        masterButton->setFixedSize(48, 28);
        masterButton->setToolTip(QString::fromUtf8(
            "导入 StarProcessor %1（仅支持 .spmaster）").arg(name));
        masterButton->setAccessibleName(masterButton->toolTip());
        row->addWidget(masterButton);

        clearButton = new QPushButton(m_calibrationGroup);
        clearButton->setIcon(
            UiAssets::icon(UiAssets::Glyph::Trash, QColor("#A7B8B4")));
        clearButton->setIconSize(QSize(16, 16));
        clearButton->setFixedSize(28, 28);
        clearButton->setToolTip(
            QString::fromUtf8("清空已选%1").arg(name));
        clearButton->setAccessibleName(clearButton->toolTip());
        clearButton->setEnabled(false);
        QStringList* const pathList = &paths;
        QString* const master = &masterPath;
        connect(rawButton, &QPushButton::clicked, this,
                [this, pathList, sourceLabel, clearButton, name]() {
                    importCalibrationFrames(
                        *pathList, sourceLabel, clearButton,
                        QString::fromUtf8("选择%1 RAW").arg(name));
                });
        connect(masterButton, &QPushButton::clicked, this,
                [this, master, pathList, sourceLabel, clearButton, name,
                 confirmNormalizedFlat]() {
                    importCalibrationMaster(
                        *master, *pathList, sourceLabel, clearButton, name,
                        confirmNormalizedFlat);
                });
        connect(clearButton, &QPushButton::clicked, this,
                [this, pathList, master, sourceLabel, clearButton]() {
                    clearCalibrationSource(
                        *pathList, *master, sourceLabel, clearButton);
                });
        row->addWidget(clearButton);

        calibrationLayout->addLayout(row);
    };

    addCalibrationRow(
        QString::fromUtf8("暗场"),
        QString::fromUtf8("校正热噪声、固定图样噪声和热像素"),
        m_darkFramePaths, m_masterDarkPath,
        m_darkFrameCount, m_darkFrameClear);
    addCalibrationRow(
        QString::fromUtf8("平场"),
        QString::fromUtf8("校正暗角、灰尘阴影和像场亮度不均"),
        m_flatFramePaths, m_masterFlatPath,
        m_flatFrameCount, m_flatFrameClear, true);
    addCalibrationRow(
        QString::fromUtf8("偏置场"),
        QString::fromUtf8("校正传感器读出偏置，并为平场标定零点"),
        m_biasFramePaths, m_masterBiasPath,
        m_biasFrameCount, m_biasFrameClear);
    addCalibrationRow(
        QString::fromUtf8("暗平场"),
        QString::fromUtf8("匹配 Flat 曝光，用于替代 Bias 校准平场"),
        m_darkFlatFramePaths, m_masterDarkFlatPath,
        m_darkFlatFrameCount, m_darkFlatFrameClear);

    m_saveGeneratedMastersCheck = new QCheckBox(
        QString::fromUtf8("保存本次生成的 Master"), m_calibrationGroup);
    m_saveGeneratedMastersCheck->setToolTip(QString::fromUtf8(
        "把本次由 RAW 原片组合成的 Master 保存为 .spmaster，便于以后直接复用"));
    m_saveGeneratedMastersCheck->setChecked(false);
    m_saveGeneratedMastersCheck->setVisible(false);
    m_saveGeneratedMastersCheck->setEnabled(false);
    connect(m_saveGeneratedMastersCheck, &QCheckBox::toggled,
            this, [this]() { emitParamsChanged(); });
    calibrationLayout->addWidget(m_saveGeneratedMastersCheck);

    m_calibrationStatus = new QLabel(m_calibrationGroup);
    m_calibrationStatus->setWordWrap(true);
    setRole(m_calibrationStatus, "calibrationStatus");
    calibrationLayout->addWidget(m_calibrationStatus);
    updateCalibrationStatus();

    stackPageLayout->addWidget(m_calibrationGroup);

    // 对齐组（默认展开）
    m_alignGroup = createCollapsibleGroup(QString::fromUtf8("对齐"), true);
    auto* alignLayout = new QVBoxLayout(m_alignGroup);
    alignLayout->setSpacing(8);

    auto* refRow = new QHBoxLayout();
    auto* refLabel = new QLabel(QString::fromUtf8("参考帧:"), m_alignGroup);
    refLabel->setFixedWidth(60);
    refRow->addWidget(refLabel);
    m_refFrame = new QComboBox(m_alignGroup);
    m_refFrame->addItem(QString::fromUtf8("自动选择"));
    connect(m_refFrame, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    refRow->addWidget(m_refFrame, 1);
    alignLayout->addLayout(refRow);

    stackPageLayout->addWidget(m_alignGroup);

    // 堆栈组（默认展开）
    m_stackGroup = createCollapsibleGroup(QString::fromUtf8("堆栈与地景"), true);
    auto* stackLayout = new QVBoxLayout(m_stackGroup);
    stackLayout->setSpacing(8);

    auto* algoRow = new QHBoxLayout();
    auto* algoLabel = new QLabel(QString::fromUtf8("算法:"), m_stackGroup);
    algoLabel->setFixedWidth(60);
    algoRow->addWidget(algoLabel);
    m_stackAlgorithm = new QComboBox(m_stackGroup);
    m_stackAlgorithm->addItems({QString::fromUtf8("中位数 Median"),
                                QString::fromUtf8("平均值 Mean"),
                                "Kappa-Sigma", "Winsorized"});
    m_stackAlgorithm->setToolTip(
        QString::fromUtf8("选择堆栈降噪算法：\n"
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
    m_kappaNameLabel = new QLabel(QString::fromUtf8("κ值:"), m_stackGroup);
    m_kappaNameLabel->setFixedWidth(60);
    m_kappaNameLabel->setToolTip(QString::fromUtf8("κ (kappa)：异常值剔除阈值系数\n"
        "• 值越小，剔除越严格，可能误删微弱星点\n"
        "• 值越大，保留越多，可能残留飞机轨迹\n"
        "• 推荐值：2.0~3.0，深空常用 2.5"));
    kappaRow->addWidget(m_kappaNameLabel);
    m_kappaSlider = createSlider(10, 50, 25);
    m_kappaSlider->setMinimumWidth(96);
    m_kappaSlider->setToolTip(QString::fromUtf8("拖动调整 κ 值，值越小剔除越严格"));
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

    m_autoRejectQualityCheck = new QCheckBox(
        QString::fromUtf8("自动排除严重差帧"), m_stackGroup);
    m_autoRejectQualityCheck->setChecked(true);
    m_autoRejectQualityCheck->setToolTip(QString::fromUtf8(
        "通过轻量预览识别明显失焦、拖星或云层遮挡的离群帧；正常差异不会自动删除"));
    connect(m_autoRejectQualityCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    stackLayout->addWidget(m_autoRejectQualityCheck);

    m_photometricCheck = new QCheckBox(
        QString::fromUtf8("帧间光度匹配"), m_stackGroup);
    m_photometricCheck->setChecked(true);
    m_photometricCheck->setToolTip(QString::fromUtf8(
        "将每帧曝光和背景色偏匹配到参考帧，减少薄云、光污染变化造成的堆栈斑块"));
    connect(m_photometricCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    stackLayout->addWidget(m_photometricCheck);

    // 天地分离
    auto* skyGroundRow = new QHBoxLayout();
    m_skyGroundCheck = new QCheckBox(QString::fromUtf8("天地分离"), m_stackGroup);
    m_skyGroundCheck->setToolTip(QString::fromUtf8("不带赤道仪时，天空对齐星点，地景保持固定，避免地景拖影"));
    connect(m_skyGroundCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    connect(m_skyGroundCheck, &QCheckBox::toggled, this, [this]() {
        updateSkyGroundControls();
    });
    skyGroundRow->addWidget(m_skyGroundCheck);
    stackLayout->addLayout(skyGroundRow);

    // 模式选择
    auto* modeRow = new QHBoxLayout();
    m_skyGroundMode = new QComboBox(m_stackGroup);
    m_skyGroundMode->addItems({"自动检测", "用户蒙版"});
    m_skyGroundMode->setEnabled(false);
    connect(m_skyGroundMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index)
        updateSkyGroundControls();
        markPresetCustom();
        emitParamsChanged();
    });
    m_skyGroundModeLabel = new QLabel(QString::fromUtf8("模式:"), m_stackGroup);
    m_skyGroundModeLabel->setFixedWidth(60);
    modeRow->addWidget(m_skyGroundModeLabel);
    modeRow->addWidget(m_skyGroundMode, 1);
    stackLayout->addLayout(modeRow);

    // 按钮行
    auto* btnRow = new QHBoxLayout();
    m_detectMaskBtn = new QPushButton(QString::fromUtf8("检测地景"), m_stackGroup);
    m_detectMaskBtn->setVisible(false);
    connect(m_detectMaskBtn, &QPushButton::clicked, this, &ParamsPanel::maskPreviewRequested);
    btnRow->addWidget(m_detectMaskBtn);

    m_importMaskBtn = new QPushButton(QString::fromUtf8("导入蒙版..."), m_stackGroup);
    m_importMaskBtn->setVisible(false);
    connect(m_importMaskBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("选择蒙版"),
                                                    QString(), "Images (*.png *.jpg *.bmp)");
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
    m_featherNameLabel = new QLabel(QString::fromUtf8("羽化:"), m_stackGroup);
    m_featherNameLabel->setFixedWidth(60);
    featherRow->addWidget(m_featherNameLabel);
    featherRow->addWidget(m_featherSlider, 1);
    featherRow->addWidget(m_featherLabel);
    stackLayout->addLayout(featherRow);

    // Ground frames are deliberately not aligned to the sky. Average is the
    // stable default; the other modes are useful when local motion or absolute
    // sharpness matters more than foreground noise.
    auto* groundMethodRow = new QHBoxLayout();
    m_groundStackMethod = new QComboBox(m_stackGroup);
    m_groundStackMethod->addItem(QString::fromUtf8("平均降噪（推荐）"), "average");
    m_groundStackMethod->addItem(QString::fromUtf8("参考帧（单帧）"), "reference");
    m_groundStackMethod->addItem(QString::fromUtf8("中值（运动地景）"), "median");
    m_groundStackMethod->setEnabled(false);
    m_groundStackMethod->setToolTip(QString::fromUtf8(
        "平均降噪适合静止地景；参考单帧更锐但噪声和光照可能不同；中值可抑制短暂人物或车灯"));
    connect(m_groundStackMethod,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParamsPanel::onComboChanged);
    m_groundStackNameLabel = new QLabel(
        QString::fromUtf8("地景合成:"), m_stackGroup);
    m_groundStackNameLabel->setFixedWidth(60);
    groundMethodRow->addWidget(m_groundStackNameLabel);
    groundMethodRow->addWidget(m_groundStackMethod, 1);
    stackLayout->addLayout(groundMethodRow);

    auto* groundDetailRow = new QHBoxLayout();
    m_groundDetailSlider = createSlider(0, 70, 40);
    m_groundDetailSlider->setEnabled(false);
    m_groundDetailSlider->setMinimumWidth(96);
    m_groundDetailSlider->setToolTip(QString::fromUtf8(
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
        QString::fromUtf8("地景细节:"), m_stackGroup);
    m_groundDetailNameLabel->setFixedWidth(60);
    groundDetailRow->addWidget(m_groundDetailNameLabel);
    groundDetailRow->addWidget(m_groundDetailSlider, 1);
    groundDetailRow->addWidget(m_groundDetailLabel);
    stackLayout->addLayout(groundDetailRow);

    stackPageLayout->addWidget(m_stackGroup);

    m_starTrailGroup = createCollapsibleGroup(
        QString::fromUtf8("星轨合成"), true);
    m_starTrailGroup->setVisible(false);
    auto* starTrailLayout = new QVBoxLayout(m_starTrailGroup);
    starTrailLayout->setSpacing(10);

    auto* starTrailNote = new QLabel(
        QString::fromUtf8(
            "固定机位序列保持原坐标，不进行星点对齐。天空逐帧取亮形成轨迹；开启地景保护后，山体和建筑使用原坐标均值降噪。"),
        m_starTrailGroup);
    starTrailNote->setWordWrap(true);
    setRole(starTrailNote, "noteWarm");
    starTrailLayout->addWidget(starTrailNote);

    auto* cometRow = new QHBoxLayout();
    auto* cometName = new QLabel(
        QString::fromUtf8("彗星拖尾:"), m_starTrailGroup);
    m_starTrailCometSlider = createSlider(0, 100, 0);
    m_starTrailCometSlider->setMinimumWidth(96);
    m_starTrailCometSlider->setToolTip(QString::fromUtf8(
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

    m_starTrailReverseCheck = new QCheckBox(
        QString::fromUtf8("反转彗星方向"), m_starTrailGroup);
    m_starTrailReverseCheck->setToolTip(QString::fromUtf8(
        "交换星轨亮端和暗端；连续星轨（0%）不受影响"));
    m_starTrailReverseCheck->setEnabled(false);
    connect(m_starTrailReverseCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    starTrailLayout->addWidget(m_starTrailReverseCheck);

    m_starTrailProtectGroundCheck = new QCheckBox(
        QString::fromUtf8("保护固定地景"), m_starTrailGroup);
    m_starTrailProtectGroundCheck->setChecked(true);
    m_starTrailProtectGroundCheck->setToolTip(QString::fromUtf8(
        "自动检测地平线；天空合成星轨，地景在相机坐标中平均降噪，避免灯光取亮和前景发糊"));
    connect(m_starTrailProtectGroundCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    starTrailLayout->addWidget(m_starTrailProtectGroundCheck);
    stackPageLayout->addWidget(m_starTrailGroup);

    m_timelapseGroup = createCollapsibleGroup(
        QString::fromUtf8("滑动窗口降噪"), true);
    m_timelapseGroup->setVisible(false);
    auto* timelapseLayout = new QVBoxLayout(m_timelapseGroup);
    timelapseLayout->setSpacing(10);

    auto* windowRow = new QHBoxLayout();
    auto* windowLabel = new QLabel(
        QString::fromUtf8("邻近窗口:"), m_timelapseGroup);
    windowLabel->setFixedWidth(76);
    m_timelapseWindow = new QComboBox(m_timelapseGroup);
    m_timelapseWindow->addItem(QString::fromUtf8("3 帧（更快）"), 3);
    m_timelapseWindow->addItem(QString::fromUtf8("5 帧（更干净）"), 5);
    m_timelapseWindow->setCurrentIndex(1);
    m_timelapseWindow->setToolTip(QString::fromUtf8(
        "每张输出以自身为中心，使用前后邻近 RAW 对齐降噪；序列两端自动缩短窗口"));
    connect(m_timelapseWindow,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParamsPanel::onComboChanged);
    windowRow->addWidget(windowLabel);
    windowRow->addWidget(m_timelapseWindow, 1);
    timelapseLayout->addLayout(windowRow);

    auto* temporalStrengthRow = new QHBoxLayout();
    temporalStrengthRow->addWidget(
        new QLabel(QString::fromUtf8("时域强度:"), m_timelapseGroup));
    m_timelapseStrengthSlider = createSlider(0, 100, 80);
    m_timelapseStrengthSlider->setToolTip(QString::fromUtf8(
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
    motionProtectionRow->addWidget(
        new QLabel(QString::fromUtf8("动态内容保护:"), m_timelapseGroup));
    m_timelapseMotionProtectionSlider = createSlider(0, 100, 75);
    m_timelapseMotionProtectionSlider->setToolTip(QString::fromUtf8(
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
        QString::fromUtf8(
            "变化区域会自动减少邻帧混合，优先保留当前目标帧。"),
        m_timelapseGroup);
    motionProtectionNote->setWordWrap(true);
    setRole(motionProtectionNote, "muted");
    timelapseLayout->addWidget(motionProtectionNote);

    m_timelapseProtectGroundCheck = new QCheckBox(
        QString::fromUtf8("固定地景保持原位"), m_timelapseGroup);
    m_timelapseProtectGroundCheck->setChecked(true);
    m_timelapseProtectGroundCheck->setToolTip(QString::fromUtf8(
        "天空按星点对齐，山体和建筑在相机坐标中降噪，再沿地平线融合"));
    connect(m_timelapseProtectGroundCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    timelapseLayout->addWidget(m_timelapseProtectGroundCheck);

    auto* outputNote = new QLabel(
        QString::fromUtf8("每张输入 RAW 对应一张带原文件名的输出图片。"),
        m_timelapseGroup);
    outputNote->setWordWrap(true);
    setRole(outputNote, "note");
    timelapseLayout->addWidget(outputNote);
    stackPageLayout->addWidget(m_timelapseGroup);
    stackPageLayout->addStretch();

    // 基础调整与快速预览共用同一套参数和收尾处理管线。
    m_basicAdjustGroup = createCollapsibleGroup(
        QString::fromUtf8("基础调色"), true);
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
        nameLabel->setFixedWidth(58);
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

    addSectionLabel(QString::fromUtf8("白平衡"));
    addAdjustmentRow(QString::fromUtf8("色温偏移"), -100, 100,
                     m_temperatureSlider, m_temperatureLabel,
                     QString::fromUtf8("向左冷却画面，向右增加暖色；这是相对偏移，不代表绝对 Kelvin"));
    addAdjustmentRow(QString::fromUtf8("色调"), -100, 100,
                     m_tintSlider, m_tintLabel,
                     QString::fromUtf8("在绿色与洋红之间校正色偏"));

    addSectionLabel(QString::fromUtf8("光线"));
    addAdjustmentRow(QString::fromUtf8("曝光"), -50, 50,
                     m_exposureSlider, m_exposureLabel,
                     QString::fromUtf8("整体曝光补偿，范围 -5.0 EV 到 +5.0 EV"), true);
    addAdjustmentRow(QString::fromUtf8("对比度"), -100, 100,
                     m_contrastSlider, m_contrastLabel,
                     QString::fromUtf8("围绕中间调压缩或扩展明暗层次"));
    addAdjustmentRow(QString::fromUtf8("高光"), -100, 100,
                     m_highlightsSlider, m_highlightsLabel,
                     QString::fromUtf8("主要调整亮部，并保护中间调和黑位"));
    addAdjustmentRow(QString::fromUtf8("阴影"), -100, 100,
                     m_shadowsSlider, m_shadowsLabel,
                     QString::fromUtf8("主要调整暗部，并保护高光"));
    addAdjustmentRow(QString::fromUtf8("白色色阶"), -100, 100,
                     m_whitesSlider, m_whitesLabel,
                     QString::fromUtf8("设置最亮区域的视觉强度"));
    addAdjustmentRow(QString::fromUtf8("黑色色阶"), -100, 100,
                     m_blacksSlider, m_blacksLabel,
                     QString::fromUtf8("设置最暗区域的视觉深度"));

    addSectionLabel(QString::fromUtf8("颜色"));
    addAdjustmentRow(QString::fromUtf8("自然饱和度"), -100, 100,
                     m_vibranceSlider, m_vibranceLabel,
                     QString::fromUtf8("优先增强低饱和颜色，降低鲜艳区域溢色风险"));
    addAdjustmentRow(QString::fromUtf8("饱和度"), -100, 100,
                     m_saturationSlider, m_saturationLabel,
                     QString::fromUtf8("统一调整全部颜色的饱和程度"));

    addSectionLabel(QString::fromUtf8("细节"));
    addAdjustmentRow(QString::fromUtf8("锐化"), 0, 100,
                     m_sharpeningSlider, m_sharpeningLabel,
                     QString::fromUtf8("亮度通道阈值锐化，抑制暗部噪声和彩色边缘"));
    adjustPageLayout->addWidget(m_basicAdjustGroup);

    // 自动优化组（默认展开）
    m_optimizeGroup = createCollapsibleGroup(QString::fromUtf8("降噪与增强"), true);
    auto* optimizeLayout = new QVBoxLayout(m_optimizeGroup);
    optimizeLayout->setSpacing(8);

    auto* noiseReductionRow = new QHBoxLayout();
    m_noiseReductionCheck = new QCheckBox(QString::fromUtf8("多尺度降噪"), m_optimizeGroup);
    m_noiseReductionCheck->setMinimumWidth(106);
    m_noiseReductionCheck->setToolTip(QString::fromUtf8(
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
    m_noiseReductionSlider->setToolTip(QString::fromUtf8("降噪强度：0 为保留全部细节，70 为最强"));
    connect(m_noiseReductionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_noiseReductionLabel->setText(QString("%1%").arg(value));
        onSliderValueChanged(value);
    });
    connect(m_noiseReductionSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    noiseReductionRow->addWidget(m_noiseReductionSlider, 1);
    noiseReductionRow->addWidget(m_noiseReductionLabel);
    optimizeLayout->addLayout(noiseReductionRow);

    m_modifiedCameraColorCheck = new QCheckBox(
        QString::fromUtf8("BCF 改机色彩还原"), m_optimizeGroup);
    m_modifiedCameraColorCheck->setToolTip(QString::fromUtf8(
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
        QString::fromUtf8("校正强度"), m_optimizeGroup);
    setRole(modifiedStrengthName, "muted");
    modifiedStrengthRow->addWidget(modifiedStrengthName);
    m_modifiedCameraColorStrengthSlider = createSlider(0, 100, 100);
    m_modifiedCameraColorStrengthSlider->setMinimumWidth(84);
    m_modifiedCameraColorStrengthSlider->setToolTip(QString::fromUtf8(
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
    m_modifiedCameraColorMode->addItem(QString::fromUtf8("自动灰点"));
    m_modifiedCameraColorMode->addItem(QString::fromUtf8("手动灰点"));
    m_modifiedCameraColorMode->setToolTip(QString::fromUtf8(
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
        UiAssets::icon(UiAssets::Glyph::Eyedropper, QColor("#A7B8B4")));
    m_modifiedCameraGrayPointButton->setIconSize(QSize(16, 16));
    m_modifiedCameraGrayPointButton->setFixedSize(30, 28);
    m_modifiedCameraGrayPointButton->setToolTip(
        QString::fromUtf8("在结果预览中重新采样手动灰点"));
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
    m_dewarpCheck = new QCheckBox(QString::fromUtf8("去雾"), m_optimizeGroup);
    m_dewarpCheck->setMinimumWidth(106);
    m_dewarpCheck->setEnabled(true);
    m_dewarpCheck->setToolTip(QString::fromUtf8(
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

    m_stretchCheck = new QCheckBox(QString::fromUtf8("曲线拉伸"), m_optimizeGroup);
    m_stretchCheck->setEnabled(true);
    m_stretchCheck->setToolTip(QString::fromUtf8(
        "背景色偏中和 + RGB 联动 Arcsinh 拉伸\n"
        "增强暗部并保留星点颜色"));
    connect(m_stretchCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    optimizeLayout->addWidget(m_stretchCheck);

    adjustPageLayout->addWidget(m_optimizeGroup);

    // 星点修饰组（始终展开，不可折叠）
    m_starReduceGroup = new QWidget(this);
    setRole(m_starReduceGroup, "plainSection");
    auto* starLayout = new QVBoxLayout(m_starReduceGroup);
    starLayout->setContentsMargins(0, 15, 0, 6);
    starLayout->setSpacing(10);

    auto* starTitle = new QLabel(QString::fromUtf8("星点修饰"), m_starReduceGroup);
    setRole(starTitle, "sectionTitle");
    starLayout->addWidget(starTitle);

    auto* defringeRow = new QHBoxLayout();
    m_starDefringeCheck = new QCheckBox(
        QString::fromUtf8("去除星点紫边"), m_starReduceGroup);
    m_starDefringeCheck->setToolTip(QString::fromUtf8(
        "比较星核与星翼色度，只抑制向边缘突增的紫、蓝或绿色边\n"
        "不会改变星点尺寸，也不会全局降低饱和度"));
    connect(m_starDefringeCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    defringeRow->addWidget(m_starDefringeCheck);
    starLayout->addLayout(defringeRow);

    auto* defringeStrengthRow = new QHBoxLayout();
    auto* defringeStrengthLabel = new QLabel(
        QString::fromUtf8("去边强度:"), m_starReduceGroup);
    defringeStrengthRow->addWidget(defringeStrengthLabel);
    m_starDefringeSlider = createSlider(0, 100, 55);
    m_starDefringeSlider->setEnabled(false);
    m_starDefringeSlider->setMinimumWidth(96);
    m_starDefringeLabel = new QLabel("55%", m_starReduceGroup);
    setRole(m_starDefringeLabel, "value");
    m_starDefringeLabel->setMinimumWidth(32);
    m_starDefringeLabel->setEnabled(false);
    m_starDefringeSlider->setToolTip(QString::fromUtf8(
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
    m_starReduceCheck = new QCheckBox(QString::fromUtf8("启用缩星"), m_starReduceGroup);
    m_starReduceCheck->setEnabled(true);
    m_starReduceCheck->setToolTip(QString::fromUtf8(
        "自动建立无星层，仅对星层应用亚像素圆形 Minimum\n"
        "饱和大星保持原样，避免宽光晕形成暗环"));
    connect(m_starReduceCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    starRow->addWidget(m_starReduceCheck);
    starLayout->addLayout(starRow);

    auto* strengthRow = new QHBoxLayout();
    auto* strengthLabel = new QLabel(QString::fromUtf8("强度:"), m_starReduceGroup);
    strengthLabel->setToolTip(QString::fromUtf8(
        "缩星强度：40 温和，70 强烈，90 接近清星\n"
        "请在 100% 预览下判断，过高会损失星点细节"));
    strengthRow->addWidget(strengthLabel);
    m_starReduceSlider = createSlider(0, 100, 70);
    m_starReduceSlider->setEnabled(false);
    m_starReduceSlider->setMinimumWidth(96);
    m_starReduceLabel = new QLabel("70%", m_starReduceGroup);
    setRole(m_starReduceLabel, "value");
    m_starReduceLabel->setMinimumWidth(32);
    m_starReduceSlider->setToolTip(QString::fromUtf8(
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

    // 输出组（始终展开，不可折叠）
    m_outputGroup = new QWidget(this);
    setRole(m_outputGroup, "plainSection");
    auto* outputLayout = new QVBoxLayout(m_outputGroup);
    outputLayout->setContentsMargins(0, 15, 0, 8);
    outputLayout->setSpacing(14);

    auto* outTitle = new QLabel(QString::fromUtf8("文件设置"), m_outputGroup);
    setRole(outTitle, "sectionTitle");
    outputLayout->addWidget(outTitle);

    auto* outputForm = new QGridLayout();
    outputForm->setContentsMargins(0, 0, 0, 0);
    outputForm->setHorizontalSpacing(12);
    outputForm->setVerticalSpacing(11);
    outputForm->setColumnStretch(1, 1);

    auto* formatLabel = new QLabel(QString::fromUtf8("文件格式"), m_outputGroup);
    formatLabel->setFixedWidth(56);
    formatLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formatLabel->setToolTip(QString::fromUtf8("TIFF 16-bit：最高质量，保留完整动态范围（推荐）\nPNG 8-bit：无损压缩，预览/分享首选"));
    outputForm->addWidget(formatLabel, 0, 0);
    m_outputFormat = new QComboBox(m_outputGroup);
    m_outputFormat->addItems({"TIFF 16-bit", "PNG 8-bit (预览)"});
    m_outputFormat->setToolTip(QString::fromUtf8("输出图像格式，TIFF 16-bit 为推荐默认"));
    connect(m_outputFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    outputForm->addWidget(m_outputFormat, 0, 1, 1, 2);

    // 色彩空间：当前固定输出线性 sRGB，暂不提供选择控件
    // 后续完整实现色彩空间转换后再恢复

    // 输出路径选择
    auto* pathLabel = new QLabel(QString::fromUtf8("输出目录"), m_outputGroup);
    pathLabel->setFixedWidth(56);
    pathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    outputForm->addWidget(pathLabel, 1, 0);
    auto* pathEdit = new QLineEdit(m_outputGroup);
    m_outputPath = pathEdit;
    pathEdit->setReadOnly(true);
    displayOutputPath(
        pathEdit, QDir::homePath() + "/StarProcessor/Output");
    outputForm->addWidget(pathEdit, 1, 1);
    auto* pathBtn = new QPushButton(m_outputGroup);
    pathBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Folder, QColor("#A7B8B4")));
    pathBtn->setToolTip(QString::fromUtf8("选择输出目录"));
    pathBtn->setAccessibleName(pathBtn->toolTip());
    setButtonVariant(pathBtn, "icon");
    connect(pathBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(nullptr, QString::fromUtf8("选择输出目录"));
        if (!dir.isEmpty()) {
            displayOutputPath(m_outputPath, dir);
            markPresetCustom();
            emitParamsChanged();
        }
    });
    outputForm->addWidget(pathBtn, 1, 2);
    outputLayout->addLayout(outputForm);

    outputPageLayout->addWidget(m_outputGroup, 0, Qt::AlignTop);
    outputPageLayout->addStretch(1);

    // 底部按钮栏
    auto* btnBar = new QWidget(this);
    btnBar->setObjectName("paramsFooter");
    btnBar->setFixedHeight(52);
    auto* btnLayout = new QHBoxLayout(btnBar);
    btnLayout->setContentsMargins(18, 0, 18, 0);
    btnLayout->setSpacing(8);

    m_restoreBtn = new QPushButton(QString::fromUtf8("恢复默认"), btnBar);
    setButtonVariant(m_restoreBtn, "ghost");
    connect(m_restoreBtn, &QPushButton::clicked, this, &ParamsPanel::onRestoreDefaults);
    btnLayout->addWidget(m_restoreBtn);

    btnLayout->addStretch();

    m_savePresetBtn = new QPushButton(QString::fromUtf8("保存预设"), btnBar);
    setButtonVariant(m_savePresetBtn, "primary");
    connect(m_savePresetBtn, &QPushButton::clicked, this, &ParamsPanel::onSavePreset);
    btnLayout->addWidget(m_savePresetBtn);

    layout->addWidget(btnBar);

    // Debounce 定时器
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(200);
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
        if (group->layout()) {
            group->layout()->setContentsMargins(0, 12, 0, 5);
            group->layout()->setSpacing(10);
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
    setStyleSheet(QString::fromLatin1(kPanelStyle));
}

void ParamsPanel::updateSkyGroundControls() {
    bool enabled = m_skyGroundCheck ? m_skyGroundCheck->isChecked() : false;
    int mode = m_skyGroundMode ? m_skyGroundMode->currentIndex() : 0;
    if (m_skyGroundMode) {
        m_skyGroundMode->setEnabled(enabled);
        m_skyGroundMode->setVisible(enabled);
    }
    if (m_skyGroundModeLabel) m_skyGroundModeLabel->setVisible(enabled);
    if (m_detectMaskBtn) {
        m_detectMaskBtn->setEnabled(enabled);
        m_detectMaskBtn->setVisible(enabled && mode == 0);
    }
    if (m_importMaskBtn) {
        m_importMaskBtn->setEnabled(enabled);
        m_importMaskBtn->setVisible(enabled && mode == 1);
    }
    if (m_maskPathLabel) {
        m_maskPathLabel->setVisible(enabled && mode == 1 && !m_userMaskPath.isEmpty());
    }
    if (m_featherSlider) {
        m_featherSlider->setEnabled(enabled);
        m_featherSlider->setVisible(enabled);
    }
    if (m_featherLabel) m_featherLabel->setVisible(enabled);
    if (m_featherNameLabel) m_featherNameLabel->setVisible(enabled);
    if (m_groundStackMethod) {
        m_groundStackMethod->setEnabled(enabled);
        m_groundStackMethod->setVisible(enabled);
    }
    if (m_groundStackNameLabel) m_groundStackNameLabel->setVisible(enabled);
    if (m_groundDetailSlider) {
        m_groundDetailSlider->setEnabled(enabled);
        m_groundDetailSlider->setVisible(enabled);
    }
    if (m_groundDetailLabel) m_groundDetailLabel->setVisible(enabled);
    if (m_groundDetailNameLabel) m_groundDetailNameLabel->setVisible(enabled);
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
            QString::fromUtf8("普通相机请保持关闭"));
    } else if (!manual) {
        m_modifiedCameraGrayPointStatus->setText(
            QString::fromUtf8("自动从中等亮度天空估计中性色"));
    } else if (hasModifiedCameraGrayPoint()) {
        m_modifiedCameraGrayPointStatus->setText(
            QString::fromUtf8("手动灰点已设置，可用吸管重新采样"));
    } else {
        m_modifiedCameraGrayPointStatus->setText(
            QString::fromUtf8("请点击吸管，再在结果预览中选择中性天空"));
    }
}

QGroupBox* ParamsPanel::createCollapsibleGroup(const QString& title, bool expanded) {
    Q_UNUSED(expanded)
    auto* group = new QGroupBox(title, this);
    group->setCheckable(false);
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

    // 切换展开/折叠图标
    QString title = group->title();
    if (checked) {
        title.replace("▶", "▼");
    } else {
        title.replace("▼", "▶");
    }
    group->setTitle(title);

    // 折叠时隐藏内部内容
    for (auto* child : group->findChildren<QWidget*>()) {
        if (child != group) {
            child->setVisible(checked);
        }
    }
    if (checked && group == m_stackGroup) updateSkyGroundControls();
    if (checked && group == m_calibrationGroup) updateCalibrationStatus();
    group->setMinimumHeight(checked ? 0 : 28);
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
    m_userChangedStackMethod = false;
    {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->setCurrentIndex(0);
    }
    {
        QSignalBlocker blocker(m_stackAlgorithm); // 阻塞信号，避免触发 m_userChangedStackMethod = true
        m_refFrame->setCurrentIndex(0);
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
    m_darkFramePaths.clear();
    m_flatFramePaths.clear();
    m_biasFramePaths.clear();
    m_darkFlatFramePaths.clear();
    m_masterDarkPath.clear();
    m_masterFlatPath.clear();
    m_masterBiasPath.clear();
    m_masterDarkFlatPath.clear();
    if (m_saveGeneratedMastersCheck) {
        m_saveGeneratedMastersCheck->setChecked(false);
    }
    updateCalibrationSource(m_darkFrameCount, m_darkFrameClear,
                            m_darkFramePaths, m_masterDarkPath);
    updateCalibrationSource(m_flatFrameCount, m_flatFrameClear,
                            m_flatFramePaths, m_masterFlatPath);
    updateCalibrationSource(m_biasFrameCount, m_biasFrameClear,
                            m_biasFramePaths, m_masterBiasPath);
    updateCalibrationSource(m_darkFlatFrameCount, m_darkFlatFrameClear,
                            m_darkFlatFramePaths, m_masterDarkFlatPath);
    m_userMaskPath.clear();
    m_maskPathLabel->clear();
    m_maskPathLabel->setVisible(false);
    emitParamsChanged();
}

void ParamsPanel::onSavePreset() {
    bool ok;
    QString name = QInputDialog::getText(this, QString::fromUtf8("保存预设"),
                                          QString::fromUtf8("预设名称:"),
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

    QMessageBox::information(this, QString::fromUtf8("保存预设"),
                             QString::fromUtf8("预设 \"%1\" 已保存").arg(name));
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
    QString outputPath = settings.value("outputPath", QDir::homePath() + "/StarProcessor/Output").toString();
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
            QString op = settings.value("outputPath", QDir::homePath() + "/StarProcessor/Output").toString();
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
    if (preset.outputFormat == "tiff16") m_outputFormat->setCurrentIndex(0);
    else if (preset.outputFormat == "png8") m_outputFormat->setCurrentIndex(1);

    updateStackMethodDescription();
    emitParamsChanged();
}

void ParamsPanel::applySceneProfile(ProcessingScene scene) {
    const QList<Preset> builtins = PresetManager::builtinPresets();
    Preset preset;
    int presetIndex = 0;
    int activeTab = 0;
    QString title;
    bool skyGroundEnabled = false;

    switch (scene) {
    case ProcessingScene::SingleFrame:
        preset.name = QString::fromUtf8("单张精修");
        preset.stackMethod = "median";
        preset.autoRejectLowQualityFrames = false;
        preset.photometricNormalizationEnabled = false;
        preset.noiseReductionEnabled = true;
        preset.noiseReductionStrength = 25;
        preset.stretchEnabled = true;
        preset.starReduceEnabled = false;
        title = QString::fromUtf8("单张精修参数");
        activeTab = 1;
        break;
    case ProcessingScene::Nightscape:
        if (!builtins.isEmpty()) preset = builtins[0];
        presetIndex = 1;
        title = QString::fromUtf8("银河星景参数");
        break;
    case ProcessingScene::DeepSky:
        if (builtins.size() > 1) preset = builtins[1];
        presetIndex = 2;
        title = QString::fromUtf8("深空堆栈参数");
        break;
    case ProcessingScene::SkyGround:
        if (!builtins.isEmpty()) preset = builtins[0];
        presetIndex = 0;
        skyGroundEnabled = true;
        title = QString::fromUtf8("天地分离参数");
        break;
    case ProcessingScene::StarTrail:
        preset.name = QString::fromUtf8("星轨合成");
        preset.stackMethod = "average";
        preset.autoRejectLowQualityFrames = false;
        preset.photometricNormalizationEnabled = true;
        preset.noiseReductionEnabled = false;
        preset.dewarpEnabled = false;
        preset.stretchEnabled = true;
        preset.starReduceEnabled = false;
        presetIndex = 0;
        title = QString::fromUtf8("星轨合成参数");
        break;
    case ProcessingScene::Timelapse:
        preset.name = QString::fromUtf8("延时序列");
        preset.stackMethod = "average";
        preset.autoRejectLowQualityFrames = false;
        preset.photometricNormalizationEnabled = true;
        preset.noiseReductionEnabled = false;
        preset.dewarpEnabled = false;
        preset.stretchEnabled = false;
        preset.starReduceEnabled = false;
        presetIndex = 0;
        title = QString::fromUtf8("延时序列参数");
        break;
    }

    m_userChangedStackMethod = false;
    applyPreset(preset);

    {
        const QSignalBlocker presetBlocker(m_presetCombo);
        const QSignalBlocker skyGroundBlocker(m_skyGroundCheck);
        m_presetCombo->setCurrentIndex(
            std::clamp(presetIndex, 0, m_presetCombo->count() - 1));
        m_skyGroundCheck->setChecked(skyGroundEnabled);
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
    emitParamsChanged();
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
            description = QString::fromUtf8(
                "<b>少帧稳健</b><br>逐像素取中位数，能压制飞机轨迹和热像素；信噪比提升低于平均值。");
            break;
        case 1:
            description = QString::fromUtf8(
                "<b>细节优先</b><br>逐像素平均，信噪比利用率最高；异常轨迹、坏点和薄云更容易残留。");
            break;
        case 2:
            description = QString::fromUtf8(
                "<b>均衡推荐</b><br>迭代剔除超出 κ 阈值的异常像素，适合 6–15 帧；κ 越小剔除越严格。");
            break;
        case 3:
            description = QString::fromUtf8(
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
    if (!m_outputFormat) return "tiff16";
    switch (m_outputFormat->currentIndex()) {
        case 0: return "tiff16";
        case 1: return "png8";
        default: return "tiff16";
    }
}

QString ParamsPanel::outputPath() const {
    return m_outputPath ? m_outputPath->text() : (QDir::homePath() + "/StarProcessor/Output");
}

void ParamsPanel::setOutputPath(const QString& path) {
    if (m_outputPath) {
        displayOutputPath(m_outputPath, path);
        markPresetCustom();
        emitParamsChanged();
    }
}

void ParamsPanel::updateRefFrameList(const QStringList& fileNames) {
    if (!m_refFrame) return;
    const QString currentPath = m_refFrame->currentData().toString();
    QSignalBlocker blocker(m_refFrame);
    m_refFrame->clear();
    m_refFrame->addItem(QString::fromUtf8("自动选择"));
    for (const QString& path : fileNames) {
        m_refFrame->addItem(QFileInfo(path).fileName(), path);
    }
    const int idx = m_refFrame->findData(currentPath);
    if (idx >= 0) m_refFrame->setCurrentIndex(idx);
}

void ParamsPanel::setSelectedReferenceFrame(const QString& filePath) {
    if (!m_refFrame || filePath.isEmpty()) return;
    const int index = m_refFrame->findData(filePath);
    if (index > 0 && index != m_refFrame->currentIndex()) {
        m_refFrame->setCurrentIndex(index);
    }
}

QString ParamsPanel::selectedReferenceFrame() const {
    return m_refFrame ? m_refFrame->currentData().toString() : QString();
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
    return m_saveGeneratedMastersCheck &&
        m_saveGeneratedMastersCheck->isEnabled() &&
        m_saveGeneratedMastersCheck->isChecked();
}

bool ParamsPanel::deepSkyCalibrationInputsComplete() const {
    auto sourceReady = [](const QStringList& rawPaths,
                          const QString& masterPath) {
        return (rawPaths.size() >= 3 && masterPath.isEmpty()) ||
            (rawPaths.isEmpty() && !masterPath.isEmpty());
    };

    if (!sourceReady(m_darkFramePaths, m_masterDarkPath) ||
        !sourceReady(m_flatFramePaths, m_masterFlatPath)) {
        return false;
    }

    const bool biasReady = sourceReady(m_biasFramePaths, m_masterBiasPath);
    const bool darkFlatReady = sourceReady(
        m_darkFlatFramePaths, m_masterDarkFlatPath);
    const bool hasAnyBias = !m_biasFramePaths.isEmpty() ||
        !m_masterBiasPath.isEmpty();
    const bool hasAnyDarkFlat = !m_darkFlatFramePaths.isEmpty() ||
        !m_masterDarkFlatPath.isEmpty();

    if (!m_masterFlatPath.isEmpty()) {
        // A serialized Master Flat is already offset-corrected and normalized.
        // Bias is still meaningful only when an imported Master Dark had its
        // Bias pedestal removed and Light therefore needs a separate offset.
        if (hasAnyDarkFlat) return false;
        // Master Dark's internal metadata decides whether Light still needs a
        // Bias source. The panel cannot infer that before the file is parsed,
        // so an optional Bias is accepted and Worker performs the final rule.
        if (m_masterDarkPath.isEmpty()) return !hasAnyBias;
        return !hasAnyBias || biasReady;
    }

    // A raw Flat group needs exactly one offset source. Supplying both would
    // make the calibration path ambiguous.
    // A bias-corrected imported Master Dark may need Bias for Light while
    // Dark Flat calibrates Flat. The panel cannot trust filenames to infer
    // that internal state, so allow this combination and let preflight read
    // the `.spmaster` metadata before processing starts.
    if (hasAnyBias && hasAnyDarkFlat) return !m_masterDarkPath.isEmpty() &&
        biasReady && darkFlatReady;
    if (!hasAnyBias && !hasAnyDarkFlat) return false;
    return hasAnyBias ? biasReady : darkFlatReady;
}

void ParamsPanel::importCalibrationFrames(QStringList& paths,
                                          QLabel* countLabel,
                                          QPushButton* clearButton,
                                          const QString& dialogTitle) {
    const QStringList selected = QFileDialog::getOpenFileNames(
        this, dialogTitle, QString(),
        QString::fromUtf8(
            "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef *.cr3);;"
            "所有文件 (*)"));
    if (selected.isEmpty()) return;

    auto pathIdentity = [](const QString& path) {
        QString identity = QFileInfo(path).canonicalFilePath();
        if (identity.isEmpty()) identity = QFileInfo(path).absoluteFilePath();
        return QDir::cleanPath(identity);
    };
    QSet<QString> otherCalibrationPaths;
    const std::array<const QStringList*, 4> lists = {
        &m_darkFramePaths, &m_flatFramePaths, &m_biasFramePaths,
        &m_darkFlatFramePaths};
    for (const QStringList* list : lists) {
        if (list == &paths) continue;
        for (const QString& path : *list) {
            otherCalibrationPaths.insert(pathIdentity(path));
        }
    }
    const std::array<const QString*, 4> masters = {
        &m_masterDarkPath, &m_masterFlatPath, &m_masterBiasPath,
        &m_masterDarkFlatPath};
    QString* const ownMaster = masterPathForRawGroup(paths);
    for (const QString* master : masters) {
        if (master == ownMaster || master->isEmpty()) continue;
        otherCalibrationPaths.insert(pathIdentity(*master));
    }

    QStringList merged = paths;
    QSet<QString> currentCalibrationPaths;
    for (const QString& path : paths) {
        currentCalibrationPaths.insert(pathIdentity(path));
    }
    int categoryConflicts = 0;
    for (const QString& path : selected) {
        const QString absolutePath =
            QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        const QString identity = pathIdentity(path);
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
            this, QString::fromUtf8("已跳过重复校准帧"),
            QString::fromUtf8(
                "%1 张 RAW 已属于另一类校准帧，未重复导入。")
                .arg(categoryConflicts));
    }
    if (merged == paths) return;

    paths = merged;
    if (ownMaster) ownMaster->clear();
    updateCalibrationSource(countLabel, clearButton, paths,
                            ownMaster ? *ownMaster : QString());
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::importCalibrationMaster(QString& masterPath,
                                          QStringList& rawPaths,
                                          QLabel* sourceLabel,
                                          QPushButton* clearButton,
                                          const QString& roleName,
                                          bool confirmNormalizedFlat) {
    const QString selected = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("选择 Master %1").arg(roleName), QString(),
        QString::fromUtf8("StarProcessor Master (*.spmaster)"));
    if (selected.isEmpty()) return;

    const QFileInfo selectedInfo(selected);
    if (selectedInfo.suffix().compare(
            QStringLiteral("spmaster"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(
            this, QString::fromUtf8("不支持的 Master 文件"),
            QString::fromUtf8(
                "Master 只能导入由 StarProcessor 生成的 .spmaster 文件。"));
        return;
    }
    const QString absolutePath = QDir::cleanPath(selectedInfo.absoluteFilePath());
    if (calibrationPathUsedElsewhere(
            absolutePath, &rawPaths, &masterPath)) {
        QMessageBox::warning(
            this, QString::fromUtf8("Master 已被使用"),
            QString::fromUtf8(
                "同一个文件不能同时分配给多个校准角色。"));
        return;
    }

    if (confirmNormalizedFlat) {
        QMessageBox::information(
            this, QString::fromUtf8("Master Flat 校验"),
            QString::fromUtf8(
                "StarProcessor 会读取 .spmaster 内的处理状态。只有已经"
                "完成偏移校准并按 CFA phase 分相归一化的 Master Flat "
                "才能通过正式预检。"));
    }

    rawPaths.clear();
    masterPath = absolutePath;
    updateCalibrationSource(sourceLabel, clearButton, rawPaths, masterPath);
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
    markPresetCustom();
    emitParamsChanged();
}

void ParamsPanel::updateCalibrationSource(
    QLabel* label, QPushButton* clearButton,
    const QStringList& rawPaths, const QString& masterPath) {
    if (label) {
        if (!masterPath.isEmpty()) {
            label->setText(QFileInfo(masterPath).fileName());
            label->setToolTip(masterPath);
        } else if (!rawPaths.isEmpty()) {
            label->setText(QString::fromUtf8("RAW %1 张").arg(rawPaths.size()));
            label->setToolTip(rawPaths.join(QLatin1Char('\n')));
        } else {
            label->setText(QString::fromUtf8("未选择"));
            label->setToolTip(QString());
        }
    }
    if (clearButton) {
        clearButton->setEnabled(!rawPaths.isEmpty() || !masterPath.isEmpty());
    }
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
    if (!m_calibrationStatus) return;
    const bool ready = deepSkyCalibrationInputsComplete();
    if (ready) {
        m_calibrationStatus->setText(QString::fromUtf8(
            "校准来源已就绪。处理前仍会检查相机、尺寸、ISO、曝光、"
            "CFA 和 Master 元数据。"));
    } else {
        QStringList missing;
        auto sourceReady = [](const QStringList& rawPaths,
                              const QString& masterPath) {
            return (rawPaths.size() >= 3 && masterPath.isEmpty()) ||
                (rawPaths.isEmpty() && !masterPath.isEmpty());
        };
        if (!sourceReady(m_darkFramePaths, m_masterDarkPath)) {
            missing.append(QString::fromUtf8("Dark（≥3 RAW 或 Master）"));
        }
        if (!sourceReady(m_flatFramePaths, m_masterFlatPath)) {
            missing.append(QString::fromUtf8("Flat（≥3 RAW 或 Master）"));
        }
        const bool hasBias = !m_biasFramePaths.isEmpty() ||
            !m_masterBiasPath.isEmpty();
        const bool hasDarkFlat = !m_darkFlatFramePaths.isEmpty() ||
            !m_masterDarkFlatPath.isEmpty();
        if (m_masterFlatPath.isEmpty() && hasBias == hasDarkFlat) {
            missing.append(QString::fromUtf8(
                "生成 Flat 时 Bias / Dark Flat 恰选一种"));
        } else if (!m_masterFlatPath.isEmpty() && hasDarkFlat) {
            missing.append(QString::fromUtf8(
                "Master Flat 不再搭配 Dark Flat"));
        }
        if (!m_masterFlatPath.isEmpty() && m_masterDarkPath.isEmpty() &&
            hasBias) {
            missing.append(QString::fromUtf8(
                "RAW Dark 含偏置基底，Master Flat 不再搭配 Bias"));
        }
        if (hasBias && !sourceReady(m_biasFramePaths, m_masterBiasPath)) {
            missing.append(QString::fromUtf8(
                "Bias（≥3 RAW 或 Master）"));
        }
        if (hasDarkFlat &&
            !sourceReady(m_darkFlatFramePaths, m_masterDarkFlatPath)) {
            missing.append(QString::fromUtf8(
                "Dark Flat（≥3 RAW 或 Master）"));
        }
        m_calibrationStatus->setText(
            missing.isEmpty()
                ? QString::fromUtf8("校准来源尚未就绪，请检查各 RAW 组数量。")
                : QString::fromUtf8("还需要：%1").arg(
                      missing.join(QString::fromUtf8("；"))));
    }
    m_calibrationStatus->setProperty(
        "status", ready ? "ready" : "waiting");
    if (m_calibrationStatus->style()) {
        m_calibrationStatus->style()->unpolish(m_calibrationStatus);
        m_calibrationStatus->style()->polish(m_calibrationStatus);
    }
    m_calibrationStatus->update();

    const bool hasRawSources = !m_darkFramePaths.isEmpty() ||
        !m_flatFramePaths.isEmpty() || !m_biasFramePaths.isEmpty() ||
        !m_darkFlatFramePaths.isEmpty();
    if (m_saveGeneratedMastersCheck) {
        if (!hasRawSources && m_saveGeneratedMastersCheck->isChecked()) {
            const QSignalBlocker blocker(m_saveGeneratedMastersCheck);
            m_saveGeneratedMastersCheck->setChecked(false);
        }
        m_saveGeneratedMastersCheck->setVisible(hasRawSources);
        m_saveGeneratedMastersCheck->setEnabled(hasRawSources);
    }
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
