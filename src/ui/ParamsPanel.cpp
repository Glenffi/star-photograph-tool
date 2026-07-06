#include "ParamsPanel.h"
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

ParamsPanel::ParamsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
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

    // 对齐组（默认展开）
    m_alignGroup = createCollapsibleGroup(QString::fromUtf8("▼ 对齐"), true);
    auto* alignLayout = new QVBoxLayout(m_alignGroup);
    alignLayout->setSpacing(8);

    auto* methodRow = new QHBoxLayout();
    auto* methodLabel = new QLabel(QString::fromUtf8("方法:"), m_alignGroup);
    methodLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    methodRow->addWidget(methodLabel);
    m_alignMethod = new QComboBox(m_alignGroup);
    m_alignMethod->addItems({"星点对齐", "特征点匹配", "手动对齐"});
    m_alignMethod->setStyleSheet(
        "QComboBox { background-color: #21262D; color: #E6EDF3; border: 1px solid #30363D; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #21262D; color: #E6EDF3; "
        "border: 1px solid #30363D; selection-background-color: #30363D; }"
    );
    connect(m_alignMethod, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
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

    auto* algoRow = new QHBoxLayout();
    auto* algoLabel = new QLabel(QString::fromUtf8("算法:"), m_stackGroup);
    algoLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    algoRow->addWidget(algoLabel);
    m_stackAlgorithm = new QComboBox(m_stackGroup);
    m_stackAlgorithm->addItems({"Sigma Clipping", "Median", "Mean", "Kappa-Sigma", "Winsorized"});
    m_stackAlgorithm->setStyleSheet(m_alignMethod->styleSheet());
    connect(m_stackAlgorithm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ParamsPanel::onComboChanged);
    algoRow->addWidget(m_stackAlgorithm, 1);
    stackLayout->addLayout(algoRow);

    auto* kappaRow = new QHBoxLayout();
    auto* kappaLabel = new QLabel(QString::fromUtf8("κ值:"), m_stackGroup);
    kappaLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    kappaRow->addWidget(kappaLabel);
    m_kappaSlider = createSlider(10, 50, 25);
    m_kappaSlider->setFixedWidth(120);
    connect(m_kappaSlider, &QSlider::valueChanged, this, [this](int v) {
        m_kappaLabel->setText(QString::number(v / 10.0, 'f', 1));
    });
    connect(m_kappaSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    kappaRow->addWidget(m_kappaSlider);
    m_kappaLabel = new QLabel("2.5", m_stackGroup);
    m_kappaLabel->setStyleSheet("font-size: 12px; color: #8B949E; background-color: transparent; min-width: 24px;");
    kappaRow->addWidget(m_kappaLabel);
    stackLayout->addLayout(kappaRow);

    containerLayout->addWidget(m_stackGroup);

    // 自动优化组（默认展开）
    m_optimizeGroup = createCollapsibleGroup(QString::fromUtf8("▼ 自动优化"), true);
    auto* optimizeLayout = new QVBoxLayout(m_optimizeGroup);
    optimizeLayout->setSpacing(8);

    auto* dewarpRow = new QHBoxLayout();
    m_dewarpCheck = new QCheckBox(QString::fromUtf8("去雾"), m_optimizeGroup);
    m_dewarpCheck->setStyleSheet(
        "QCheckBox { font-size: 12px; color: #C9D1D9; background-color: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
    );
    connect(m_dewarpCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    dewarpRow->addWidget(m_dewarpCheck);
    m_dewarpSlider = createSlider(0, 100, 30);
    m_dewarpSlider->setEnabled(false);
    m_dewarpSlider->setFixedWidth(100);
    connect(m_dewarpSlider, &QSlider::valueChanged, this, &ParamsPanel::onSliderValueChanged);
    connect(m_dewarpSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    dewarpRow->addWidget(m_dewarpSlider);
    optimizeLayout->addLayout(dewarpRow);

    m_stretchCheck = new QCheckBox(QString::fromUtf8("曲线拉伸"), m_optimizeGroup);
    m_stretchCheck->setStyleSheet(m_dewarpCheck->styleSheet());
    connect(m_stretchCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    optimizeLayout->addWidget(m_stretchCheck);

    containerLayout->addWidget(m_optimizeGroup);

    // 缩星组（默认折叠）
    m_starReduceGroup = createCollapsibleGroup(QString::fromUtf8("▶ 缩星"), false);
    auto* starLayout = new QVBoxLayout(m_starReduceGroup);
    starLayout->setSpacing(8);

    auto* starRow = new QHBoxLayout();
    m_starReduceCheck = new QCheckBox(QString::fromUtf8("启用"), m_starReduceGroup);
    m_starReduceCheck->setStyleSheet(m_dewarpCheck->styleSheet());
    connect(m_starReduceCheck, &QCheckBox::toggled, this, &ParamsPanel::onCheckChanged);
    starRow->addWidget(m_starReduceCheck);
    starLayout->addLayout(starRow);

    auto* strengthRow = new QHBoxLayout();
    auto* strengthLabel = new QLabel(QString::fromUtf8("强度:"), m_starReduceGroup);
    strengthLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    strengthRow->addWidget(strengthLabel);
    m_starReduceSlider = createSlider(0, 100, 50);
    m_starReduceSlider->setEnabled(false);
    m_starReduceSlider->setFixedWidth(120);
    connect(m_starReduceSlider, &QSlider::valueChanged, this, &ParamsPanel::onSliderValueChanged);
    connect(m_starReduceSlider, &QSlider::sliderReleased, this, &ParamsPanel::onSliderReleased);
    strengthRow->addWidget(m_starReduceSlider);
    starLayout->addLayout(strengthRow);

    containerLayout->addWidget(m_starReduceGroup);

    // 输出组（默认折叠）
    m_outputGroup = createCollapsibleGroup(QString::fromUtf8("▶ 输出"), false);
    auto* outputLayout = new QVBoxLayout(m_outputGroup);
    outputLayout->setSpacing(8);

    auto* formatRow = new QHBoxLayout();
    auto* formatLabel = new QLabel(QString::fromUtf8("格式:"), m_outputGroup);
    formatLabel->setStyleSheet("font-size: 12px; color: #C9D1D9; background-color: transparent;");
    formatRow->addWidget(formatLabel);
    m_outputFormat = new QComboBox(m_outputGroup);
    m_outputFormat->addItems({"TIFF 16-bit", "TIFF 32-bit", "PNG 16-bit", "FITS", "JPEG (预览)"});
    m_outputFormat->setStyleSheet(m_alignMethod->styleSheet());
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
    // 占位：后续实现预设保存
    emitParamsChanged();
}

void ParamsPanel::emitParamsChanged() {
    emit paramsChanged();
}
