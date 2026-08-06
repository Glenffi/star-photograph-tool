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
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QTabWidget>

#include <QSignalBlocker>

#include <algorithm>

ParamsPanel::ParamsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    loadPreset();
}

void ParamsPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标题栏与预设保持固定，长参数放入各自可滚动的工作阶段页。
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(44);
    titleBar->setStyleSheet("background-color: #171F21; border-bottom: 1px solid #2B393B;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 12, 0);
    m_titleLabel = new QLabel(QString::fromUtf8("处理参数"), titleBar);
    m_titleLabel->setStyleSheet("font-size: 13px; font-weight: 700; color: #F3F7F6; background-color: transparent;");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();
    layout->addWidget(titleBar);

    m_presetBar = new QWidget(this);
    m_presetBar->setFixedHeight(50);
    m_presetBar->setStyleSheet("background-color: #151C1E; border-bottom: 1px solid #263234;");
    auto* presetRow = new QHBoxLayout(m_presetBar);
    presetRow->setContentsMargins(12, 8, 12, 8);
    presetRow->setSpacing(8);
    auto* presetLabel = new QLabel(QString::fromUtf8("预设"), m_presetBar);
    presetLabel->setStyleSheet("font-size: 11px; color: #91A39F; background-color: transparent;");
    presetRow->addWidget(presetLabel);
    m_presetCombo = new QComboBox(m_presetBar);
    m_presetCombo->addItem(QString::fromUtf8("自定义"));
    for (const Preset& preset : PresetManager::builtinPresets()) {
        m_presetCombo->addItem(preset.name);
    }
    m_presetCombo->setStyleSheet(
        "QComboBox { background-color: #202A2D; color: #D2DDDA; border: 1px solid #344548; "
        "border-radius: 5px; padding: 5px 8px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #202A2D; color: #D2DDDA; "
        "border: 1px solid #344548; selection-background-color: #25463F; }"
    );
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onPresetChanged);
    presetRow->addWidget(m_presetCombo, 1);
    layout->addWidget(m_presetBar);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setStyleSheet(
        "QTabWidget::pane { border: none; background-color: #111719; }"
        "QTabBar { background-color: #171F21; }"
        "QTabBar::tab { background-color: #171F21; color: #7B8E8A; border: none; "
        "  border-bottom: 2px solid transparent; padding: 9px 18px; font-size: 11px; }"
        "QTabBar::tab:hover { color: #D2DDDA; background-color: #202A2D; }"
        "QTabBar::tab:selected { color: #F3F7F6; border-bottom-color: #4ED7AE; font-weight: 700; }"
    );

    auto createPage = [this](const QString& name) -> QVBoxLayout* {
        auto* page = new QWidget(m_tabs);
        page->setStyleSheet("background-color: #111719;");
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet("QScrollArea { background-color: #111719; border: none; }");
        auto* content = new QWidget(scroll);
        content->setStyleSheet("background-color: #111719;");
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(10, 10, 10, 12);
        contentLayout->setSpacing(8);
        scroll->setWidget(content);
        pageLayout->addWidget(scroll);
        m_tabs->addTab(page, name);
        return contentLayout;
    };

    QVBoxLayout* stackPageLayout = createPage(QString::fromUtf8("堆栈"));
    QVBoxLayout* adjustPageLayout = createPage(QString::fromUtf8("调整"));
    QVBoxLayout* outputPageLayout = createPage(QString::fromUtf8("输出"));
    layout->addWidget(m_tabs, 1);

    // 对齐组（默认展开）
    m_alignGroup = createCollapsibleGroup(QString::fromUtf8("对齐"), true);
    auto* alignLayout = new QVBoxLayout(m_alignGroup);
    alignLayout->setSpacing(8);

    auto* methodRow = new QHBoxLayout();
    auto* methodLabel = new QLabel(QString::fromUtf8("方法:"), m_alignGroup);
    methodLabel->setStyleSheet("font-size: 12px; color: #D2DDDA; background-color: transparent;");
    methodRow->addWidget(methodLabel);
    m_alignMethod = new QComboBox(m_alignGroup);
    m_alignMethod->addItem(QString::fromUtf8("星点对齐"));
    m_alignMethod->setStyleSheet(
        "QComboBox { background-color: #202A2D; color: #F3F7F6; border: 1px solid #344548; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #202A2D; color: #F3F7F6; "
        "border: 1px solid #344548; selection-background-color: #344548; }"
    );
    connect(m_alignMethod, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index > 0) {
            m_alignMethod->setCurrentIndex(0);
            QMessageBox::information(this, "提示", "该对齐方式将在后续版本实现");
        }
    });
    methodRow->addWidget(m_alignMethod, 1);
    alignLayout->addLayout(methodRow);

    auto* refRow = new QHBoxLayout();
    auto* refLabel = new QLabel(QString::fromUtf8("参考帧:"), m_alignGroup);
    refLabel->setStyleSheet("font-size: 12px; color: #D2DDDA; background-color: transparent;");
    refRow->addWidget(refLabel);
    m_refFrame = new QComboBox(m_alignGroup);
    m_refFrame->addItem(QString::fromUtf8("自动选择"));
    m_refFrame->setStyleSheet(m_alignMethod->styleSheet());
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
    algoLabel->setStyleSheet("font-size: 12px; color: #D2DDDA; background-color: transparent;");
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
    m_stackAlgorithm->setStyleSheet(m_alignMethod->styleSheet());
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
    m_stackMethodDescription->setMinimumHeight(58);
    m_stackMethodDescription->setStyleSheet(
        "QLabel { color: #91A39F; background-color: #1C302C; "
        "border-left: 3px solid #4ED7AE; border-radius: 4px; "
        "padding: 7px 9px; font-size: 11px; }"
    );
    stackLayout->addWidget(m_stackMethodDescription);

    auto* kappaRow = new QHBoxLayout();
    m_kappaNameLabel = new QLabel(QString::fromUtf8("κ值:"), m_stackGroup);
    m_kappaNameLabel->setStyleSheet(
        "QLabel { font-size: 12px; color: #D2DDDA; background-color: transparent; }"
        "QLabel:disabled { color: #536763; }");
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
    m_kappaLabel->setStyleSheet(
        "QLabel { font-size: 12px; color: #91A39F; background-color: transparent; min-width: 24px; }"
        "QLabel:disabled { color: #536763; }");
    kappaRow->addWidget(m_kappaLabel);
    stackLayout->addLayout(kappaRow);
    updateStackMethodDescription();

    m_autoRejectQualityCheck = new QCheckBox(
        QString::fromUtf8("自动排除严重差帧"), m_stackGroup);
    m_autoRejectQualityCheck->setChecked(true);
    m_autoRejectQualityCheck->setToolTip(QString::fromUtf8(
        "通过轻量预览识别明显失焦、拖星或云层遮挡的离群帧；正常差异不会自动删除"));
    m_autoRejectQualityCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #D2DDDA; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }");
    connect(m_autoRejectQualityCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    stackLayout->addWidget(m_autoRejectQualityCheck);

    m_photometricCheck = new QCheckBox(
        QString::fromUtf8("帧间光度匹配"), m_stackGroup);
    m_photometricCheck->setChecked(true);
    m_photometricCheck->setToolTip(QString::fromUtf8(
        "将每帧曝光和背景色偏匹配到参考帧，减少薄云、光污染变化造成的堆栈斑块"));
    m_photometricCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #D2DDDA; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }");
    connect(m_photometricCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    stackLayout->addWidget(m_photometricCheck);

    // 天地分离
    auto* skyGroundRow = new QHBoxLayout();
    m_skyGroundCheck = new QCheckBox(QString::fromUtf8("天地分离"), m_stackGroup);
    m_skyGroundCheck->setToolTip(QString::fromUtf8("不带赤道仪时，天空对齐星点，地景保持固定，避免地景拖影"));
    // Do not copy styles from controls created later in setupUI(). Keeping this
    // style self-contained also prevents construction order from becoming a
    // hidden dependency between otherwise unrelated parameter groups.
    m_skyGroundCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #D2DDDA; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
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
    m_skyGroundMode->setStyleSheet(m_alignMethod->styleSheet());
    connect(m_skyGroundMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index)
        updateSkyGroundControls();
        markPresetCustom();
        emitParamsChanged();
    });
    m_skyGroundModeLabel = new QLabel(QString::fromUtf8("模式:"), m_stackGroup);
    modeRow->addWidget(m_skyGroundModeLabel);
    modeRow->addWidget(m_skyGroundMode, 1);
    stackLayout->addLayout(modeRow);

    // 按钮行
    auto* btnRow = new QHBoxLayout();
    m_detectMaskBtn = new QPushButton(QString::fromUtf8("检测地景"), m_stackGroup);
    m_detectMaskBtn->setVisible(false);
    m_detectMaskBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #202A2D;"
        "  color: #F3F7F6;"
        "  border: 1px solid #344548;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #344548;"
        "  border: 1px solid #4D6265;"
        "}"
    );
    connect(m_detectMaskBtn, &QPushButton::clicked, this, &ParamsPanel::maskPreviewRequested);
    btnRow->addWidget(m_detectMaskBtn);

    m_importMaskBtn = new QPushButton(QString::fromUtf8("导入蒙版..."), m_stackGroup);
    m_importMaskBtn->setVisible(false);
    m_importMaskBtn->setStyleSheet(m_detectMaskBtn->styleSheet());
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
    m_maskPathLabel->setStyleSheet("font-size: 10px; color: #91A39F;");
    stackLayout->addWidget(m_maskPathLabel);

    // 羽化宽度
    auto* featherRow = new QHBoxLayout();
    m_featherSlider = createSlider(0, 50, 20);
    m_featherSlider->setEnabled(false);
    m_featherSlider->setMinimumWidth(96);
    m_featherLabel = new QLabel("20 px", m_stackGroup);
    m_featherLabel->setMinimumWidth(38);
    connect(m_featherSlider, &QSlider::valueChanged, this, [this](int value) {
        m_featherLabel->setText(QString("%1 px").arg(value));
        onSliderValueChanged(value);
    });
    connect(m_featherSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    m_featherNameLabel = new QLabel(QString::fromUtf8("羽化:"), m_stackGroup);
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
    m_groundStackMethod->setStyleSheet(m_alignMethod->styleSheet());
    m_groundStackMethod->setToolTip(QString::fromUtf8(
        "平均降噪适合静止地景；参考单帧更锐但噪声和光照可能不同；中值可抑制短暂人物或车灯"));
    connect(m_groundStackMethod,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ParamsPanel::onComboChanged);
    m_groundStackNameLabel = new QLabel(
        QString::fromUtf8("地景合成:"), m_stackGroup);
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
    groundDetailRow->addWidget(m_groundDetailNameLabel);
    groundDetailRow->addWidget(m_groundDetailSlider, 1);
    groundDetailRow->addWidget(m_groundDetailLabel);
    stackLayout->addLayout(groundDetailRow);

    stackPageLayout->addWidget(m_stackGroup);

    m_timelapseGroup = createCollapsibleGroup(
        QString::fromUtf8("滑动窗口降噪"), true);
    m_timelapseGroup->setVisible(false);
    auto* timelapseLayout = new QVBoxLayout(m_timelapseGroup);
    timelapseLayout->setSpacing(10);

    auto* windowRow = new QHBoxLayout();
    auto* windowLabel = new QLabel(
        QString::fromUtf8("邻近窗口:"), m_timelapseGroup);
    m_timelapseWindow = new QComboBox(m_timelapseGroup);
    m_timelapseWindow->addItem(QString::fromUtf8("3 帧（更快）"), 3);
    m_timelapseWindow->addItem(QString::fromUtf8("5 帧（更干净）"), 5);
    m_timelapseWindow->setCurrentIndex(1);
    m_timelapseWindow->setStyleSheet(m_alignMethod->styleSheet());
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
    motionProtectionNote->setStyleSheet(
        "color: #91A39F; font-size: 11px; font-weight: 400;");
    timelapseLayout->addWidget(motionProtectionNote);

    m_timelapseProtectGroundCheck = new QCheckBox(
        QString::fromUtf8("固定地景保持原位"), m_timelapseGroup);
    m_timelapseProtectGroundCheck->setChecked(true);
    m_timelapseProtectGroundCheck->setToolTip(QString::fromUtf8(
        "天空按星点对齐，山体和建筑在相机坐标中降噪，再沿地平线融合"));
    m_timelapseProtectGroundCheck->setStyleSheet(
        m_autoRejectQualityCheck->styleSheet());
    connect(m_timelapseProtectGroundCheck, &QCheckBox::toggled,
            this, &ParamsPanel::onCheckChanged);
    timelapseLayout->addWidget(m_timelapseProtectGroundCheck);

    auto* outputNote = new QLabel(
        QString::fromUtf8("每张输入 RAW 对应一张带原文件名的输出图片。"),
        m_timelapseGroup);
    outputNote->setWordWrap(true);
    outputNote->setStyleSheet(
        "color: #91A39F; background-color: #152522; border-left: 3px solid #59C9E8; "
        "border-radius: 4px; padding: 7px 9px; font-size: 11px;");
    timelapseLayout->addWidget(outputNote);
    stackPageLayout->addWidget(m_timelapseGroup);
    stackPageLayout->addStretch();

    // 自动优化组（默认展开）
    m_optimizeGroup = createCollapsibleGroup(QString::fromUtf8("降噪与增强"), true);
    auto* optimizeLayout = new QVBoxLayout(m_optimizeGroup);
    optimizeLayout->setSpacing(8);

    auto* noiseReductionRow = new QHBoxLayout();
    m_noiseReductionCheck = new QCheckBox(QString::fromUtf8("多尺度降噪"), m_optimizeGroup);
    m_noiseReductionCheck->setToolTip(QString::fromUtf8(
        "在线性堆栈结果上抑制亮度和色彩噪声\n"
        "建议在去雾和曲线拉伸之前使用"));
    m_noiseReductionCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #D2DDDA; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    connect(m_noiseReductionCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    noiseReductionRow->addWidget(m_noiseReductionCheck);
    m_noiseReductionSlider = createSlider(0, 70, 30);
    m_noiseReductionSlider->setEnabled(false);
    m_noiseReductionSlider->setMinimumWidth(84);
    m_noiseReductionLabel = new QLabel("30%", m_optimizeGroup);
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

    auto* dewarpRow = new QHBoxLayout();
    m_dewarpCheck = new QCheckBox(QString::fromUtf8("去雾"), m_optimizeGroup);
    m_dewarpCheck->setEnabled(true);
    m_dewarpCheck->setToolTip(QString::fromUtf8(
        "亮度引导的 Dark Channel Prior 去雾\n"
        "适合明显薄雾；银河暗尘丰富时建议关闭"));
    m_dewarpCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #D2DDDA; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    connect(m_dewarpCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    dewarpRow->addWidget(m_dewarpCheck);
    m_dewarpSlider = createSlider(0, 100, 30);
    m_dewarpSlider->setEnabled(true);
    m_dewarpSlider->setMinimumWidth(84);
    m_dewarpLabel = new QLabel("30%", m_optimizeGroup);
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
    m_stretchCheck->setStyleSheet(m_dewarpCheck->styleSheet());
    connect(m_stretchCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    optimizeLayout->addWidget(m_stretchCheck);

    adjustPageLayout->addWidget(m_optimizeGroup);

    // 缩星组（始终展开，不可折叠）
    m_starReduceGroup = new QWidget(this);
    m_starReduceGroup->setStyleSheet(
        "QWidget { background-color: #171F21; border: 1px solid #344548; border-radius: 6px; }"
    );
    auto* starLayout = new QVBoxLayout(m_starReduceGroup);
    starLayout->setContentsMargins(12, 12, 12, 12);
    starLayout->setSpacing(8);

    auto* starTitle = new QLabel(QString::fromUtf8("缩星"), m_starReduceGroup);
    starTitle->setStyleSheet("font-size: 12px; font-weight: 700; color: #F3F7F6; background-color: transparent;");
    starLayout->addWidget(starTitle);

    auto* starRow = new QHBoxLayout();
    m_starReduceCheck = new QCheckBox(QString::fromUtf8("启用缩星"), m_starReduceGroup);
    m_starReduceCheck->setEnabled(true);
    m_starReduceCheck->setToolTip(QString::fromUtf8(
        "自动建立无星层，仅对星层应用圆形 Minimum\n"
        "关闭时保留原始星点"));
    m_starReduceCheck->setStyleSheet(m_dewarpCheck->styleSheet());
    connect(m_starReduceCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    starRow->addWidget(m_starReduceCheck);
    starLayout->addLayout(starRow);

    auto* strengthRow = new QHBoxLayout();
    auto* strengthLabel = new QLabel(QString::fromUtf8("强度:"), m_starReduceGroup);
    strengthLabel->setStyleSheet("font-size: 12px; color: #D2DDDA; background-color: transparent;");
    strengthLabel->setToolTip(QString::fromUtf8(
        "缩星强度：40 温和，70 强烈，90 接近清星\n"
        "请在 100% 预览下判断，过高会损失星点细节"));
    strengthRow->addWidget(strengthLabel);
    m_starReduceSlider = createSlider(0, 100, 70);
    m_starReduceSlider->setEnabled(false);
    m_starReduceSlider->setMinimumWidth(96);
    m_starReduceLabel = new QLabel("70%", m_starReduceGroup);
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
    m_outputGroup->setStyleSheet(
        "QWidget { background-color: #171F21; border: 1px solid #344548; border-radius: 6px; }"
    );
    auto* outputLayout = new QVBoxLayout(m_outputGroup);
    outputLayout->setContentsMargins(12, 12, 12, 12);
    outputLayout->setSpacing(8);

    auto* outTitle = new QLabel(QString::fromUtf8("文件设置"), m_outputGroup);
    outTitle->setStyleSheet("font-size: 12px; font-weight: 700; color: #F3F7F6; background-color: transparent;");
    outputLayout->addWidget(outTitle);

    auto* formatRow = new QHBoxLayout();
    auto* formatLabel = new QLabel(QString::fromUtf8("格式:"), m_outputGroup);
    formatLabel->setStyleSheet("font-size: 12px; color: #D2DDDA; background-color: transparent;");
    formatLabel->setToolTip(QString::fromUtf8("TIFF 16-bit：最高质量，保留完整动态范围（推荐）\nPNG 8-bit：无损压缩，预览/分享首选"));
    formatRow->addWidget(formatLabel);
    m_outputFormat = new QComboBox(m_outputGroup);
    m_outputFormat->addItems({"TIFF 16-bit", "PNG 8-bit (预览)"});
    m_outputFormat->setStyleSheet(m_alignMethod->styleSheet());
    m_outputFormat->setToolTip(QString::fromUtf8("输出图像格式，TIFF 16-bit 为推荐默认"));
    connect(m_outputFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    formatRow->addWidget(m_outputFormat, 1);
    outputLayout->addLayout(formatRow);

    // 色彩空间：当前固定输出线性 sRGB，暂不提供选择控件
    // 后续完整实现色彩空间转换后再恢复

    // 输出路径选择
    auto* pathRow = new QHBoxLayout();
    auto* pathLabel = new QLabel(QString::fromUtf8("输出到:"), m_outputGroup);
    pathLabel->setStyleSheet("font-size: 12px; color: #D2DDDA; background-color: transparent;");
    pathRow->addWidget(pathLabel);
    auto* pathEdit = new QLineEdit(QDir::homePath() + "/StarProcessor/Output", m_outputGroup);
    m_outputPath = pathEdit;
    pathEdit->setStyleSheet(
        "QLineEdit { background-color: #202A2D; color: #F3F7F6; "
        "  border: 1px solid #344548; border-radius: 4px; padding: 4px 8px; font-size: 11px; }"
    );
    pathEdit->setReadOnly(true);
    pathRow->addWidget(pathEdit, 1);
    auto* pathBtn = new QPushButton(m_outputGroup);
    pathBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Folder, QColor("#A7B8B4")));
    pathBtn->setToolTip(QString::fromUtf8("选择输出目录"));
    pathBtn->setAccessibleName(pathBtn->toolTip());
    pathBtn->setFixedSize(28, 28);
    pathBtn->setStyleSheet(
        "QPushButton { background-color: #202A2D; color: #F3F7F6; "
        "  border: 1px solid #344548; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background-color: #344548; }"
    );
    connect(pathBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(nullptr, QString::fromUtf8("选择输出目录"));
        if (!dir.isEmpty()) {
            m_outputPath->setText(dir);
            markPresetCustom();
            emitParamsChanged();
        }
    });
    pathRow->addWidget(pathBtn);
    outputLayout->addLayout(pathRow);

    outputPageLayout->addWidget(m_outputGroup);
    outputPageLayout->addStretch();

    // 底部按钮栏
    auto* btnBar = new QWidget(this);
    btnBar->setFixedHeight(44);
    btnBar->setStyleSheet("background-color: #171F21; border-top: 1px solid #344548;");
    auto* btnLayout = new QHBoxLayout(btnBar);
    btnLayout->setContentsMargins(12, 0, 12, 0);
    btnLayout->setSpacing(8);

    m_restoreBtn = new QPushButton(QString::fromUtf8("恢复默认"), btnBar);
    m_restoreBtn->setFixedHeight(28);
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #91A39F;"
        "  border: 1px solid #344548;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #202A2D;"
        "  color: #F3F7F6;"
        "}"
    );
    connect(m_restoreBtn, &QPushButton::clicked, this, &ParamsPanel::onRestoreDefaults);
    btnLayout->addWidget(m_restoreBtn);

    btnLayout->addStretch();

    m_savePresetBtn = new QPushButton(QString::fromUtf8("保存预设"), btnBar);
    m_savePresetBtn->setFixedHeight(28);
    m_savePresetBtn->setCursor(Qt::PointingHandCursor);
    m_savePresetBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #202A2D;"
        "  color: #F3F7F6;"
        "  border: 1px solid #344548;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #344548;"
        "  border: 1px solid #4D6265;"
        "}"
    );
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
    connect(m_starReduceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_starReduceSlider->setEnabled(checked);
        m_starReduceLabel->setEnabled(checked);
    });
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

QGroupBox* ParamsPanel::createCollapsibleGroup(const QString& title, bool expanded) {
    Q_UNUSED(expanded)
    auto* group = new QGroupBox(title, this);
    group->setCheckable(false);
    group->setStyleSheet(
        "QGroupBox {"
        "  background-color: #171F21;"
        "  color: #F3F7F6;"
        "  border: 1px solid #344548;"
        "  border-radius: 6px;"
        "  margin-top: 12px;"
        "  padding: 10px 12px 12px 12px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 8px;"
        "  padding: 0 4px;"
        "  background-color: #111719;"
        "}"
    );
    return group;
}

QSlider* ParamsPanel::createSlider(int min, int max, int value, const QString& suffix) {
    Q_UNUSED(suffix)
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setValue(value);
    slider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  background-color: #344548;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background-color: #4ED7AE;"
        "  width: 14px;"
        "  height: 14px;"
        "  border-radius: 7px;"
        "  margin: -5px 0;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background-color: #4ED7AE;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider:disabled {"
        "  background-color: transparent;"
        "}"
        "QSlider::handle:horizontal:disabled {"
        "  background-color: #4D6265;"
        "}"
        "QSlider::sub-page:horizontal:disabled {"
        "  background-color: #4D6265;"
        "}"
    );
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
        m_alignMethod->setCurrentIndex(0);
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
    m_stretchCheck->setChecked(false);
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
    settings.setValue("alignMethod", m_alignMethod->currentIndex());
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
    settings.setValue("stretchEnabled", m_stretchCheck->isChecked());
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
    settings.setValue("alignMethod", m_alignMethod->currentIndex());
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
    settings.setValue("stretchEnabled", m_stretchCheck->isChecked());
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
    int alignIndex = settings.value("alignMethod", 0).toInt();
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
    bool stretch = settings.value("stretchEnabled", false).toBool();
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

    // 使用信号阻塞避免触发 paramsChanged
    QSignalBlocker blocker1(m_alignMethod);
    QSignalBlocker blocker2(m_stackAlgorithm);
    QSignalBlocker blocker3(m_kappaSlider);
    QSignalBlocker blocker4(m_dewarpCheck);
    QSignalBlocker blocker5(m_dewarpSlider);
    QSignalBlocker blocker6(m_noiseReductionCheck);
    QSignalBlocker blocker7(m_noiseReductionSlider);
    QSignalBlocker blocker8(m_stretchCheck);
    QSignalBlocker blocker9(m_starReduceCheck);
    QSignalBlocker blocker10(m_starReduceSlider);
    QSignalBlocker blocker11(m_outputFormat);
    QSignalBlocker blocker12(m_presetCombo);
    QSignalBlocker blocker13(m_skyGroundCheck);
    QSignalBlocker blocker14(m_skyGroundMode);
    QSignalBlocker blocker15(m_featherSlider);
    QSignalBlocker blocker16(m_photometricCheck);
    QSignalBlocker blocker17(m_autoRejectQualityCheck);
    QSignalBlocker blocker18(m_groundStackMethod);
    QSignalBlocker blocker19(m_groundDetailSlider);
    QSignalBlocker blocker20(m_timelapseWindow);
    QSignalBlocker blocker21(m_timelapseStrengthSlider);
    QSignalBlocker blocker22(m_timelapseMotionProtectionSlider);
    QSignalBlocker blocker23(m_timelapseProtectGroundCheck);

    Q_UNUSED(alignIndex)
    m_alignMethod->setCurrentIndex(0);
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
    m_stretchCheck->setChecked(stretch);
    m_starReduceCheck->setChecked(starReduce);
    m_starReduceSlider->setValue(starReduceStrength);
    m_starReduceSlider->setEnabled(starReduce);
    m_starReduceLabel->setText(QString("%1%").arg(starReduceStrength));
    m_starReduceLabel->setEnabled(starReduce);
    m_outputFormat->setCurrentIndex(outputFormat);
    m_outputPath->setText(outputPath);

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
            int alignIdx = settings.value("alignMethod", 0).toInt();
            preset.alignMethod = alignIdx == 0 ? "star" : alignIdx == 1 ? "feature" : "manual";
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
            preset.stretchEnabled = settings.value("stretchEnabled", false).toBool();
            preset.starReduceEnabled = settings.value("starReduceEnabled", false).toBool();
            preset.starReduceStrength = settings.value("starReduceStrength", 70).toInt();
            preset.outputFormat = settings.value("outputFormat", 0).toInt() == 0 ? "tiff16" : "png8";
            applyPreset(preset);

            // 恢复自定义预设中保存的其他参数
            QString op = settings.value("outputPath", QDir::homePath() + "/StarProcessor/Output").toString();
            m_outputPath->setText(op);

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
    QSignalBlocker blocker1(m_alignMethod);
    QSignalBlocker blocker2(m_stackAlgorithm);
    QSignalBlocker blocker3(m_kappaSlider);
    QSignalBlocker blocker4(m_dewarpCheck);
    QSignalBlocker blocker5(m_dewarpSlider);
    QSignalBlocker blocker6(m_noiseReductionCheck);
    QSignalBlocker blocker7(m_noiseReductionSlider);
    QSignalBlocker blocker8(m_stretchCheck);
    QSignalBlocker blocker9(m_starReduceCheck);
    QSignalBlocker blocker10(m_starReduceSlider);
    QSignalBlocker blocker11(m_outputFormat);
    QSignalBlocker blocker12(m_photometricCheck);
    QSignalBlocker blocker13(m_autoRejectQualityCheck);

    // Align method
    // 当前产品只提供已经实现并验证过的星点对齐。
    m_alignMethod->setCurrentIndex(0);

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

    // Stretch
    m_stretchCheck->setChecked(preset.stretchEnabled);

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
    if (m_alignGroup) m_alignGroup->setVisible(!timelapse);
    if (m_stackGroup) m_stackGroup->setVisible(!timelapse);
    if (m_timelapseGroup) m_timelapseGroup->setVisible(timelapse);
    // General stack presets change controls that do not participate in the
    // temporal pipeline, so keep this task-specific workspace focused.
    if (m_presetBar) m_presetBar->setVisible(!timelapse);
    if (m_savePresetBtn) m_savePresetBtn->setVisible(!timelapse);
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

QString ParamsPanel::alignMethod() const {
    if (!m_alignMethod) return "star";
    switch (m_alignMethod->currentIndex()) {
        case 0: return "star";
        case 1: return "feature";
        case 2: return "manual";
        default: return "star";
    }
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

int ParamsPanel::noiseReductionStrength() const {
    return m_noiseReductionSlider ? m_noiseReductionSlider->value() : 30;
}

bool ParamsPanel::stretchEnabled() const {
    return m_stretchCheck ? m_stretchCheck->isChecked() : false;
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
        m_outputPath->setText(path);
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

QString ParamsPanel::processingSignature() const {
    // Output path/format are intentionally excluded: changing only where or
    // how a cached result is written does not make its pixels stale.
    return QStringList{
        alignMethod(), selectedReferenceFrame(), stackMethod(),
        QString::number(kappaValue(), 'f', 1),
        QString::number(autoRejectLowQualityFrames()),
        QString::number(photometricNormalizationEnabled()),
        QString::number(noiseReductionEnabled()),
        QString::number(noiseReductionStrength()),
        QString::number(dewarpEnabled()), QString::number(dewarpStrength()),
        QString::number(stretchEnabled()), QString::number(starReduceEnabled()),
        QString::number(starReduceStrength()),
        QString::number(skyGroundSeparationEnabled()),
        QString::number(static_cast<int>(skyGroundMode())), userMaskPath(),
        QString::number(featherRadius()), groundStackMethod(),
        QString::number(groundDetailStrength()),
        QString::number(timelapseWindowSize()),
        QString::number(timelapseStrength()),
        QString::number(timelapseMotionProtection()),
        QString::number(timelapseProtectGround())
    }.join('|');
}

void ParamsPanel::setMaskPreview(const std::vector<uint8_t>& mask, int w, int h) {
    Q_UNUSED(mask) Q_UNUSED(w) Q_UNUSED(h)
    // 蒙版预览由 PreviewPanel 处理，此处仅作接口预留
}

void ParamsPanel::setDetectMaskEnabled(bool enabled) {
    if (m_detectMaskBtn) m_detectMaskBtn->setEnabled(enabled);
}
