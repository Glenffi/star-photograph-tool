#include "ParamsPanel.h"
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

#include <QSignalBlocker>

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

    // 标题栏
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(36);
    titleBar->setStyleSheet("background-color: #161B22; border-bottom: 1px solid #30363D;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 12, 0);
    auto* titleLabel = new QLabel(QString::fromUtf8("🎛️ 处理参数"), titleBar);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #E6EDF3; background-color: transparent;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    layout->addWidget(titleBar);

    // 滚动区域
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(
        "QScrollArea { background-color: #0D1117; border: none; }"
        "QScrollBar:vertical { background-color: #0D1117; width: 12px; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background-color: #30363D; border-radius: 6px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background-color: #484F58; }"
    );

    auto* container = new QWidget();
    container->setStyleSheet("background-color: #0D1117;");
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(12, 12, 12, 12);
    containerLayout->setSpacing(8);

    // 预设选择
    auto* presetRow = new QHBoxLayout();
    auto* presetLabel = new QLabel(QString::fromUtf8("预设:"), container);
    presetLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    presetRow->addWidget(presetLabel);
    m_presetCombo = new QComboBox(container);
    m_presetCombo->addItem(QString::fromUtf8("自定义"));
    m_presetCombo->addItem(QString::fromUtf8("银河广角"));
    m_presetCombo->addItem(QString::fromUtf8("深空天体"));
    m_presetCombo->addItem(QString::fromUtf8("单帧降噪"));
    m_presetCombo->addItem(QString::fromUtf8("延时序列"));
    m_presetCombo->setStyleSheet(
        "QComboBox { background-color: #21262D; color: #E6EDF3; border: 1px solid #30363D; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #21262D; color: #E6EDF3; "
        "border: 1px solid #30363D; selection-background-color: #30363D; }"
    );
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onPresetChanged);
    presetRow->addWidget(m_presetCombo, 1);
    containerLayout->addLayout(presetRow);

    // 对齐组（默认展开）
    m_alignGroup = createCollapsibleGroup(QString::fromUtf8("▼ 对齐"), true);
    auto* alignLayout = new QVBoxLayout(m_alignGroup);
    alignLayout->setSpacing(8);

    auto* methodRow = new QHBoxLayout();
    auto* methodLabel = new QLabel(QString::fromUtf8("方法:"), m_alignGroup);
    methodLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    methodRow->addWidget(methodLabel);
    m_alignMethod = new QComboBox(m_alignGroup);
    m_alignMethod->addItem("星点对齐 (已实现)");
    m_alignMethod->addItem("特征点匹配 (后续版本)");
    m_alignMethod->addItem("手动对齐 (后续版本)");
    m_alignMethod->setItemData(1, 0, Qt::UserRole - 1); // 禁用未实现项
    m_alignMethod->setItemData(2, 0, Qt::UserRole - 1);
    m_alignMethod->setStyleSheet(
        "QComboBox { background-color: #21262D; color: #E6EDF3; border: 1px solid #30363D; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #21262D; color: #E6EDF3; "
        "border: 1px solid #30363D; selection-background-color: #30363D; }"
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
    refLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    refRow->addWidget(refLabel);
    m_refFrame = new QComboBox(m_alignGroup);
    m_refFrame->addItem(QString::fromUtf8("自动选择"));
    m_refFrame->setStyleSheet(m_alignMethod->styleSheet());
    connect(m_refFrame, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    refRow->addWidget(m_refFrame, 1);
    alignLayout->addLayout(refRow);

    containerLayout->addWidget(m_alignGroup);

    // 堆栈组（默认展开）
    m_stackGroup = createCollapsibleGroup(QString::fromUtf8("▼ 堆栈"), true);
    auto* stackLayout = new QVBoxLayout(m_stackGroup);
    stackLayout->setSpacing(8);

    auto* stackDesc = new QLabel(QString::fromUtf8("多张图像叠加对齐，提升信噪比并剔除异常像素"), m_stackGroup);
    stackDesc->setStyleSheet("font-size: 10px; color: #8B949E; background-color: transparent; padding-bottom: 4px;");
    stackLayout->addWidget(stackDesc);

    auto* algoRow = new QHBoxLayout();
    auto* algoLabel = new QLabel(QString::fromUtf8("算法:"), m_stackGroup);
    algoLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    algoRow->addWidget(algoLabel);
    m_stackAlgorithm = new QComboBox(m_stackGroup);
    m_stackAlgorithm->addItems({"Median", "Mean", "Kappa-Sigma", "Winsorized"});
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
        onComboChanged(index);
    });
    algoRow->addWidget(m_stackAlgorithm, 1);
    stackLayout->addLayout(algoRow);

    auto* kappaRow = new QHBoxLayout();
    auto* kappaLabel = new QLabel(QString::fromUtf8("κ值:"), m_stackGroup);
    kappaLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    kappaLabel->setToolTip(QString::fromUtf8("κ (kappa)：异常值剔除阈值系数\n"
        "• 值越小，剔除越严格，可能误删微弱星点\n"
        "• 值越大，保留越多，可能残留飞机轨迹\n"
        "• 推荐值：2.0~3.0，深空常用 2.5"));
    kappaRow->addWidget(kappaLabel);
    m_kappaSlider = createSlider(10, 50, 25);
    m_kappaSlider->setFixedWidth(120);
    m_kappaSlider->setToolTip(QString::fromUtf8("拖动调整 κ 值，值越小剔除越严格"));
    connect(m_kappaSlider, &QSlider::valueChanged, this, [this](int v) {
        m_kappaLabel->setText(QString::number(v / 10.0, 'f', 1));
    });
    connect(m_kappaSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    kappaRow->addWidget(m_kappaSlider);
    m_kappaLabel = new QLabel("2.5", m_stackGroup);
    m_kappaLabel->setStyleSheet("font-size: 12px; color: #8B949E; background-color: transparent; min-width: 24px;");
    kappaRow->addWidget(m_kappaLabel);
    stackLayout->addLayout(kappaRow);

    auto* stackHint = new QLabel(QString::fromUtf8("ℹ️ Sigma Clipping: 剔除偏离中值过远的像素，适合有飞机/卫星轨迹的情况\nℹ️ κ=2.5: 异常值剔除阈值，值越小剔除越严格"), m_stackGroup);
    stackHint->setStyleSheet("font-size: 10px; color: #58A6FF; background-color: transparent; padding-top: 4px;");
    stackHint->setWordWrap(true);
    stackLayout->addWidget(stackHint);

    containerLayout->addWidget(m_stackGroup);

    // 自动优化组（默认展开）
    m_optimizeGroup = createCollapsibleGroup(QString::fromUtf8("▼ 自动优化"), true);
    auto* optimizeLayout = new QVBoxLayout(m_optimizeGroup);
    optimizeLayout->setSpacing(8);

    auto* dewarpRow = new QHBoxLayout();
    m_dewarpCheck = new QCheckBox(QString::fromUtf8("去雾"), m_optimizeGroup);
    m_dewarpCheck->setEnabled(true);
    m_dewarpCheck->setToolTip(QString::fromUtf8("Dark Channel Prior 去雾，改善大气光污染"));
    m_dewarpCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #C9D1D9; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    connect(m_dewarpCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    dewarpRow->addWidget(m_dewarpCheck);
    m_dewarpSlider = createSlider(0, 100, 30);
    m_dewarpSlider->setEnabled(true);
    m_dewarpSlider->setFixedWidth(100);
    connect(m_dewarpSlider, &QSlider::valueChanged, this, &ParamsPanel::onSliderValueChanged);
    connect(m_dewarpSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    dewarpRow->addWidget(m_dewarpSlider);
    optimizeLayout->addLayout(dewarpRow);

    m_stretchCheck = new QCheckBox(QString::fromUtf8("曲线拉伸"), m_optimizeGroup);
    m_stretchCheck->setEnabled(true);
    m_stretchCheck->setToolTip(QString::fromUtf8("Arcsinh 曲线拉伸，增强暗部细节"));
    m_stretchCheck->setStyleSheet(m_dewarpCheck->styleSheet());
    connect(m_stretchCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    optimizeLayout->addWidget(m_stretchCheck);

    containerLayout->addWidget(m_optimizeGroup);

    // 缩星组（始终展开，不可折叠）
    m_starReduceGroup = new QWidget(this);
    m_starReduceGroup->setStyleSheet(
        "QWidget { background-color: #161B22; border: 1px solid #30363D; border-radius: 6px; }"
    );
    auto* starLayout = new QVBoxLayout(m_starReduceGroup);
    starLayout->setContentsMargins(12, 12, 12, 12);
    starLayout->setSpacing(8);

    auto* starTitle = new QLabel(QString::fromUtf8("✨ 缩星"), m_starReduceGroup);
    starTitle->setStyleSheet("font-size: 12px; font-weight: bold; color: #E6EDF3; background-color: transparent;");
    starLayout->addWidget(starTitle);

    auto* starDesc = new QLabel(QString::fromUtf8("缩小星点尺寸，突出星云细节"), m_starReduceGroup);
    starDesc->setStyleSheet("font-size: 10px; color: #8B949E; background-color: transparent; padding-bottom: 4px;");
    starLayout->addWidget(starDesc);

    auto* starRow = new QHBoxLayout();
    m_starReduceCheck = new QCheckBox(QString::fromUtf8("启用缩星（后续版本）"), m_starReduceGroup);
    m_starReduceCheck->setEnabled(false);
    m_starReduceCheck->setToolTip(QString::fromUtf8("缩星功能将在后续版本实现"));
    m_starReduceCheck->setStyleSheet(m_dewarpCheck->styleSheet());
    m_starReduceCheck->setToolTip(QString::fromUtf8("AI 自动检测星点并缩小尺寸\n关闭时直接输出原始星点"));
    connect(m_starReduceCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    starRow->addWidget(m_starReduceCheck);
    starLayout->addLayout(starRow);

    auto* strengthRow = new QHBoxLayout();
    auto* strengthLabel = new QLabel(QString::fromUtf8("强度:"), m_starReduceGroup);
    strengthLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    strengthLabel->setToolTip(QString::fromUtf8("缩星强度：值越大星点越小\n推荐 30-70，过高会损失星点细节"));
    strengthRow->addWidget(strengthLabel);
    m_starReduceSlider = createSlider(0, 100, 50);
    m_starReduceSlider->setEnabled(false);
    m_starReduceSlider->setFixedWidth(120);
    m_starReduceSlider->setToolTip(QString::fromUtf8("拖动调整缩星强度"));
    connect(m_starReduceSlider, &QSlider::valueChanged, this, &ParamsPanel::onSliderValueChanged);
    connect(m_starReduceSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    strengthRow->addWidget(m_starReduceSlider);
    starLayout->addLayout(strengthRow);

    containerLayout->addWidget(m_starReduceGroup);

    // 输出组（始终展开，不可折叠）
    m_outputGroup = new QWidget(this);
    m_outputGroup->setStyleSheet(
        "QWidget { background-color: #161B22; border: 1px solid #30363D; border-radius: 6px; }"
    );
    auto* outputLayout = new QVBoxLayout(m_outputGroup);
    outputLayout->setContentsMargins(12, 12, 12, 12);
    outputLayout->setSpacing(8);

    auto* outTitle = new QLabel(QString::fromUtf8("💾 输出"), m_outputGroup);
    outTitle->setStyleSheet("font-size: 12px; font-weight: bold; color: #E6EDF3; background-color: transparent;");
    outputLayout->addWidget(outTitle);

    auto* formatRow = new QHBoxLayout();
    auto* formatLabel = new QLabel(QString::fromUtf8("格式:"), m_outputGroup);
    formatLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    formatLabel->setToolTip(QString::fromUtf8("TIFF 16-bit：最高质量，保留完整动态范围（推荐）\nPNG 8-bit：无损压缩，预览/分享首选"));
    formatRow->addWidget(formatLabel);
    m_outputFormat = new QComboBox(m_outputGroup);
    m_outputFormat->addItems({"TIFF 16-bit", "PNG 8-bit (预览)"});
    m_outputFormat->setStyleSheet(m_alignMethod->styleSheet());
    m_outputFormat->setToolTip(QString::fromUtf8("输出图像格式，TIFF 16-bit 为推荐默认"));
    connect(m_outputFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    formatRow->addWidget(m_outputFormat, 1);
    outputLayout->addLayout(formatRow);

    auto* colorRow = new QHBoxLayout();
    auto* colorLabel = new QLabel(QString::fromUtf8("色彩空间:"), m_outputGroup);
    colorLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    colorRow->addWidget(colorLabel);
    m_colorSpace = new QComboBox(m_outputGroup);
    m_colorSpace->addItems({"sRGB", "Adobe RGB", "ProPhoto RGB", "Rec. 2020"});
    m_colorSpace->setStyleSheet(m_alignMethod->styleSheet());
    connect(m_colorSpace, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    colorRow->addWidget(m_colorSpace, 1);
    outputLayout->addLayout(colorRow);

    // 输出路径选择
    auto* pathRow = new QHBoxLayout();
    auto* pathLabel = new QLabel(QString::fromUtf8("输出到:"), m_outputGroup);
    pathLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    pathRow->addWidget(pathLabel);
    auto* pathEdit = new QLineEdit(QDir::homePath() + "/StarProcessor/Output", m_outputGroup);
    m_outputPath = pathEdit;
    pathEdit->setStyleSheet(
        "QLineEdit { background-color: #21262D; color: #E6EDF3; "
        "  border: 1px solid #30363D; border-radius: 4px; padding: 4px 8px; font-size: 11px; }"
    );
    pathEdit->setReadOnly(true);
    pathRow->addWidget(pathEdit, 1);
    auto* pathBtn = new QPushButton("📁", m_outputGroup);
    pathBtn->setFixedSize(24, 24);
    pathBtn->setStyleSheet(
        "QPushButton { background-color: #21262D; color: #E6EDF3; "
        "  border: 1px solid #30363D; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background-color: #30363D; }"
    );
    connect(pathBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(nullptr, QString::fromUtf8("选择输出目录"));
        if (!dir.isEmpty()) m_outputPath->setText(dir);
    });
    pathRow->addWidget(pathBtn);
    outputLayout->addLayout(pathRow);

    containerLayout->addWidget(m_outputGroup);

    containerLayout->addStretch();
    scrollArea->setWidget(container);
    layout->addWidget(scrollArea, 1);

    // 底部按钮栏
    auto* btnBar = new QWidget(this);
    btnBar->setFixedHeight(44);
    btnBar->setStyleSheet("background-color: #161B22; border-top: 1px solid #30363D;");
    auto* btnLayout = new QHBoxLayout(btnBar);
    btnLayout->setContentsMargins(12, 0, 12, 0);
    btnLayout->setSpacing(8);

    m_restoreBtn = new QPushButton(QString::fromUtf8("恢复默认"), btnBar);
    m_restoreBtn->setFixedHeight(28);
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #8B949E;"
        "  border: 1px solid #30363D;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #21262D;"
        "  color: #E6EDF3;"
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
        "  background-color: #21262D;"
        "  color: #E6EDF3;"
        "  border: 1px solid #30363D;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #30363D;"
        "  border: 1px solid #484F58;"
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
    });
    connect(m_starReduceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_starReduceSlider->setEnabled(checked);
    });
}

QGroupBox* ParamsPanel::createCollapsibleGroup(const QString& title, bool expanded) {
    auto* group = new QGroupBox(title, this);
    group->setCheckable(true);
    group->setChecked(expanded);
    group->setStyleSheet(
        "QGroupBox {"
        "  background-color: #161B22;"
        "  color: #E6EDF3;"
        "  border: 1px solid #30363D;"
        "  border-radius: 6px;"
        "  margin-top: 8px;"
        "  padding: 8px 12px 12px 12px;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 8px;"
        "  padding: 0 4px;"
        "  background-color: #0D1117;"
        "}"
    );
    connect(group, &QGroupBox::toggled, this, &ParamsPanel::onGroupToggled);
    return group;
}

QSlider* ParamsPanel::createSlider(int min, int max, int value, const QString& suffix) {
    Q_UNUSED(suffix)
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setValue(value);
    slider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  background-color: #30363D;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background-color: #F0B90B;"
        "  width: 14px;"
        "  height: 14px;"
        "  border-radius: 7px;"
        "  margin: -5px 0;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background-color: #F0B90B;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider:disabled {"
        "  background-color: transparent;"
        "}"
        "QSlider::handle:horizontal:disabled {"
        "  background-color: #484F58;"
        "}"
        "QSlider::sub-page:horizontal:disabled {"
        "  background-color: #484F58;"
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
    group->setMinimumHeight(checked ? 0 : 28);
}

void ParamsPanel::onSliderValueChanged(int value) {
    Q_UNUSED(value)
    m_debounceTimer->start();
}

void ParamsPanel::onSliderReleased() {
    m_debounceTimer->stop();
    emitParamsChanged();
}

void ParamsPanel::onComboChanged(int index) {
    Q_UNUSED(index)
    emitParamsChanged();
}

void ParamsPanel::onCheckChanged(int state) {
    Q_UNUSED(state)
    emitParamsChanged();
}

void ParamsPanel::onRestoreDefaults() {
    m_userChangedStackMethod = false;
    m_alignMethod->setCurrentIndex(0);
    m_refFrame->setCurrentIndex(0);
    m_stackAlgorithm->setCurrentIndex(0);
    m_kappaSlider->setValue(25);
    m_dewarpCheck->setChecked(false);
    m_dewarpSlider->setValue(30);
    m_stretchCheck->setChecked(false);
    m_starReduceCheck->setChecked(false);
    m_starReduceSlider->setValue(50);
    m_outputFormat->setCurrentIndex(0);
    m_colorSpace->setCurrentIndex(0);
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
    settings.setValue("dewarpEnabled", m_dewarpCheck->isChecked());
    settings.setValue("dewarpStrength", m_dewarpSlider->value());
    settings.setValue("stretchEnabled", m_stretchCheck->isChecked());
    settings.setValue("starReduceEnabled", m_starReduceCheck->isChecked());
    settings.setValue("starReduceStrength", m_starReduceSlider->value());
    settings.setValue("outputFormat", m_outputFormat->currentIndex());
    settings.setValue("colorSpace", m_colorSpace->currentIndex());
    settings.setValue("outputPath", m_outputPath->text());
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
}

void ParamsPanel::saveCurrentSettings() {
    QSettings settings("StarProcessor", "App");
    settings.setValue("alignMethod", m_alignMethod->currentIndex());
    settings.setValue("stackMethod", m_stackAlgorithm->currentIndex());
    settings.setValue("kappaValue", m_kappaSlider->value());
    settings.setValue("dewarpEnabled", m_dewarpCheck->isChecked());
    settings.setValue("dewarpStrength", m_dewarpSlider->value());
    settings.setValue("stretchEnabled", m_stretchCheck->isChecked());
    settings.setValue("starReduceEnabled", m_starReduceCheck->isChecked());
    settings.setValue("starReduceStrength", m_starReduceSlider->value());
    settings.setValue("outputFormat", m_outputFormat->currentIndex());
    settings.setValue("colorSpace", m_colorSpace->currentIndex());
    settings.setValue("outputPath", m_outputPath->text());
    settings.setValue("lastPresetIndex", m_presetCombo->currentIndex());
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
    bool dewarp = settings.value("dewarpEnabled", false).toBool();
    int dewarpStrength = settings.value("dewarpStrength", 30).toInt();
    bool stretch = settings.value("stretchEnabled", false).toBool();
    bool starReduce = settings.value("starReduceEnabled", false).toBool();
    int starReduceStrength = settings.value("starReduceStrength", 50).toInt();
    int outputFormat = settings.value("outputFormat", 0).toInt();
    int colorSpace = settings.value("colorSpace", 0).toInt();
    QString outputPath = settings.value("outputPath", QDir::homePath() + "/StarProcessor/Output").toString();
    int lastPresetIndex = settings.value("lastPresetIndex", 0).toInt();

    // 使用信号阻塞避免触发 paramsChanged
    QSignalBlocker blocker1(m_alignMethod);
    QSignalBlocker blocker2(m_stackAlgorithm);
    QSignalBlocker blocker3(m_kappaSlider);
    QSignalBlocker blocker4(m_dewarpCheck);
    QSignalBlocker blocker5(m_dewarpSlider);
    QSignalBlocker blocker6(m_stretchCheck);
    QSignalBlocker blocker7(m_starReduceCheck);
    QSignalBlocker blocker8(m_starReduceSlider);
    QSignalBlocker blocker9(m_outputFormat);
    QSignalBlocker blocker10(m_colorSpace);
    QSignalBlocker blocker11(m_presetCombo);

    m_alignMethod->setCurrentIndex(alignIndex);
    m_stackAlgorithm->setCurrentIndex(stackIndex);
    m_kappaSlider->setValue(kappa);
    m_kappaLabel->setText(QString::number(kappa / 10.0, 'f', 1));
    m_dewarpCheck->setChecked(dewarp);
    m_dewarpSlider->setValue(dewarpStrength);
    m_dewarpSlider->setEnabled(dewarp);
    m_stretchCheck->setChecked(stretch);
    m_starReduceCheck->setChecked(starReduce);
    m_starReduceSlider->setValue(starReduceStrength);
    m_outputFormat->setCurrentIndex(outputFormat);
    m_colorSpace->setCurrentIndex(colorSpace);
    m_outputPath->setText(outputPath);

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
            preset.dewarpEnabled = settings.value("dewarpEnabled", false).toBool();
            preset.dewarpStrength = settings.value("dewarpStrength", 30).toInt();
            preset.stretchEnabled = settings.value("stretchEnabled", false).toBool();
            preset.starReduceEnabled = settings.value("starReduceEnabled", false).toBool();
            preset.starReduceStrength = settings.value("starReduceStrength", 50).toInt();
            preset.outputFormat = settings.value("outputFormat", 0).toInt() == 0 ? "tiff16" : "png8";
            applyPreset(preset);
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
    QSignalBlocker blocker6(m_stretchCheck);
    QSignalBlocker blocker7(m_outputFormat);

    // Align method
    if (preset.alignMethod == "star") m_alignMethod->setCurrentIndex(0);
    else if (preset.alignMethod == "feature") m_alignMethod->setCurrentIndex(1);
    else if (preset.alignMethod == "manual") m_alignMethod->setCurrentIndex(2);

    // Stack method
    if (preset.stackMethod == "median") m_stackAlgorithm->setCurrentIndex(0);
    else if (preset.stackMethod == "average") m_stackAlgorithm->setCurrentIndex(1);
    else if (preset.stackMethod == "kappa-sigma") m_stackAlgorithm->setCurrentIndex(2);
    else if (preset.stackMethod == "winsorized") m_stackAlgorithm->setCurrentIndex(3);

    // Kappa
    int kappaInt = static_cast<int>(preset.kappaValue * 10 + 0.5);
    m_kappaSlider->setValue(kappaInt);
    m_kappaLabel->setText(QString::number(preset.kappaValue, 'f', 1));

    // Dewarp
    m_dewarpCheck->setChecked(preset.dewarpEnabled);
    m_dewarpSlider->setValue(preset.dewarpStrength);
    m_dewarpSlider->setEnabled(preset.dewarpEnabled);

    // Stretch
    m_stretchCheck->setChecked(preset.stretchEnabled);

    // Output format
    if (preset.outputFormat == "tiff16") m_outputFormat->setCurrentIndex(0);
    else if (preset.outputFormat == "png8") m_outputFormat->setCurrentIndex(1);

    emitParamsChanged();
}

void ParamsPanel::emitParamsChanged() {
    emit paramsChanged();
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

bool ParamsPanel::dewarpEnabled() const {
    return m_dewarpCheck ? m_dewarpCheck->isChecked() : false;
}

int ParamsPanel::dewarpStrength() const {
    return m_dewarpSlider ? m_dewarpSlider->value() : 0;
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

void ParamsPanel::updateRefFrameList(const QStringList& fileNames) {
    if (!m_refFrame) return;
    QString current = m_refFrame->currentText();
    m_refFrame->clear();
    m_refFrame->addItem(QString::fromUtf8("自动选择"));
    for (const QString& name : fileNames) {
        m_refFrame->addItem(QFileInfo(name).fileName());
    }
    int idx = m_refFrame->findText(current);
    if (idx >= 0) m_refFrame->setCurrentIndex(idx);
}
