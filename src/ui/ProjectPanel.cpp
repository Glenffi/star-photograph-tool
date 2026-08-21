#include "ProjectPanel.h"
#include "StyleTokens.h"
#include "UiAssets.h"
#include "../core/ThumbnailGenerator.h"
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QMenu>
#include <QAction>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QGraphicsOpacityEffect>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

void setStringProperty(QObject* object, const char* propertyName,
                       const char* value) {
    object->setProperty(propertyName, QString::fromLatin1(value));
}

void refreshStyle(QWidget* widget) {
    if (!widget || !widget->style()) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

UiAssets::Glyph sceneGlyph(ProcessingScene scene) {
    switch (scene) {
    case ProcessingScene::SingleFrame:
        return UiAssets::Glyph::SingleFrame;
    case ProcessingScene::Nightscape:
        return UiAssets::Glyph::Nightscape;
    case ProcessingScene::DeepSky:
        return UiAssets::Glyph::DeepSky;
    case ProcessingScene::SkyGround:
        return UiAssets::Glyph::SkyGround;
    case ProcessingScene::StarTrail:
        return UiAssets::Glyph::StarTrail;
    case ProcessingScene::Timelapse:
        return UiAssets::Glyph::Timelapse;
    }
    return UiAssets::Glyph::Scenes;
}

QString sceneName(ProcessingScene scene) {
    switch (scene) {
    case ProcessingScene::SingleFrame:
        return QString::fromUtf8("单张精修");
    case ProcessingScene::Nightscape:
        return QString::fromUtf8("银河星景");
    case ProcessingScene::DeepSky:
        return QString::fromUtf8("深空天体");
    case ProcessingScene::SkyGround:
        return QString::fromUtf8("天地分离");
    case ProcessingScene::StarTrail:
        return QString::fromUtf8("星轨合成");
    case ProcessingScene::Timelapse:
        return QString::fromUtf8("延时序列");
    }
    return QString();
}

class RoundedThumbnailLabel final : public QLabel {
public:
    explicit RoundedThumbnailLabel(QWidget* parent = nullptr)
        : QLabel(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(48, 48);
    }

    void setThumbnail(const QPixmap& thumbnail) {
        m_thumbnail = thumbnail;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF target = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(target, StyleTokens::Radius::kSmall,
                            StyleTokens::Radius::kSmall);
        painter.setClipPath(clip);
        painter.fillRect(rect(), StyleTokens::Colors::fromHex(
                                     StyleTokens::Colors::kBackgroundRaised));

        if (!m_thumbnail.isNull()) {
            const QPixmap scaled = m_thumbnail.scaled(
                size(), Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation);
            const QPoint topLeft((width() - scaled.width()) / 2,
                                 (height() - scaled.height()) / 2);
            painter.drawPixmap(topLeft, scaled);
        }

        painter.setClipping(false);
        painter.setPen(QPen(StyleTokens::Colors::fromHex(
                                StyleTokens::Colors::kLineSubtle),
                            1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(target, StyleTokens::Radius::kSmall,
                                StyleTokens::Radius::kSmall);
    }

private:
    QPixmap m_thumbnail;
};

class ReferenceDot final : public QWidget {
public:
    explicit ReferenceDot(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(8, 8);
        setToolTip(QString::fromUtf8("参考帧"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(StyleTokens::Colors::fromHex(
                                StyleTokens::Colors::kBackgroundBase),
                            1.0));
        painter.setBrush(StyleTokens::Colors::fromHex(
            StyleTokens::Colors::kAccent));
        painter.drawEllipse(QRectF(1.0, 1.0, 6.0, 6.0));
    }
};

QString formatExposure(double exposureTime) {
    if (exposureTime <= 0.0) {
        return QString::fromUtf8("曝光 --");
    }

    if (exposureTime >= 1.0) {
        const int decimals = exposureTime < 10.0 ? 1 : 0;
        return QString::fromUtf8("%1 s").arg(exposureTime, 0, 'f', decimals);
    }

    return QString::fromUtf8("1/%1 s").arg(qRound(1.0 / exposureTime));
}

QString formatMetadata(const FileItem& item) {
    if (item.metadataFailed) {
        return QString::fromUtf8("RAW 读取失败");
    }
    if (!item.metadataLoaded) {
        return QString::fromUtf8("正在读取元数据...");
    }
    const QString iso = item.iso > 0
        ? QString::fromUtf8("ISO %1").arg(item.iso)
        : QString::fromUtf8("ISO --");
    const QString focalLength = item.focalLength > 0
        ? QString::fromUtf8("%1 mm").arg(item.focalLength)
        : QString::fromUtf8("焦距 --");
    return QString::fromUtf8("%1 · %2 · %3")
        .arg(iso, formatExposure(item.exposureTime), focalLength);
}

} // namespace

// ==================== FileCard ====================

FileCard::FileCard(const FileItem& item, QWidget* parent)
    : QWidget(parent)
    , m_isReference(item.isReferenceFrame)
    , m_isExcluded(item.isExcluded)
    , m_fileName(item.fileName)
{
    setFixedHeight(StyleTokens::Controls::kMaterialRowHeight);
    setMinimumWidth(0);
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 8, 4);
    layout->setSpacing(8);

    m_thumbnailLabel = new RoundedThumbnailLabel(this);
    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        static_cast<RoundedThumbnailLabel*>(m_thumbnailLabel)
            ->setThumbnail(item.thumbnail);
    }
    layout->addWidget(m_thumbnailLabel, 0, Qt::AlignVCenter);

    m_referenceDot = new ReferenceDot(m_thumbnailLabel);
    m_referenceDot->move(m_thumbnailLabel->width() - 10, 2);

    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(1);
    textLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setMinimumWidth(0);
    m_nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_nameLabel->setToolTip(item.fileName);
    setStringProperty(m_nameLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kMono);
    m_nameLabel->setText(m_fileName);
    textLayout->addWidget(m_nameLabel);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setMinimumWidth(0);
    m_metaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_metaLabel->setText(formatMetadata(item));
    setStringProperty(m_metaLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    textLayout->addWidget(m_metaLabel);

    layout->addLayout(textLayout, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setFixedWidth(52);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setStringProperty(m_statusLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacityEffect);
    updateStyle();
}

void FileCard::updateFromItem(const FileItem& item) {
    m_isReference = item.isReferenceFrame;
    m_isExcluded = item.isExcluded;
    m_fileName = item.fileName;
    m_nameLabel->setToolTip(m_fileName);
    updateElidedName();

    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        static_cast<RoundedThumbnailLabel*>(m_thumbnailLabel)
            ->setThumbnail(item.thumbnail);
    }

    m_metaLabel->setText(formatMetadata(item));
    m_metaLabel->setProperty(
        StyleTokens::Properties::kStatusRole,
        item.metadataFailed
            ? QVariant(QString::fromLatin1(StyleTokens::Properties::kError))
            : QVariant());
    refreshStyle(m_metaLabel);

    updateStyle();
}

void FileCard::setSelected(bool selected) {
    m_selected = selected;
    updateStyle();
}

void FileCard::updateStyle() {
    setProperty("selected", m_selected);
    setProperty("hovered", m_hovered);
    setProperty("reference", m_isReference);
    setProperty("excluded", m_isExcluded);

    if (m_isReference && m_isExcluded) {
        m_statusLabel->setText(QString::fromUtf8("参考·排除"));
    } else if (m_isReference) {
        m_statusLabel->setText(QString::fromUtf8("参考"));
    } else if (m_isExcluded) {
        m_statusLabel->setText(QString::fromUtf8("已排除"));
    } else {
        m_statusLabel->setText("");
    }

    m_referenceDot->setVisible(m_isReference);
    m_opacityEffect->setOpacity(m_isExcluded ? 0.4 : 1.0);

    QString accessible = m_fileName + QString::fromUtf8("，") +
        m_metaLabel->text();
    if (m_isReference) accessible += QString::fromUtf8("，参考帧");
    if (m_isExcluded) accessible += QString::fromUtf8("，已排除");
    setAccessibleName(accessible);

    update();
}

void FileCard::updateElidedName() {
    const int availableWidth = m_nameLabel->width();
    if (availableWidth <= 0) {
        m_nameLabel->setText(m_fileName);
        return;
    }
    m_nameLabel->setText(
        m_nameLabel->fontMetrics().elidedText(m_fileName, Qt::ElideMiddle, availableWidth)
    );
}

void FileCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void FileCard::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FileCard::enterEvent(QEnterEvent* event) {
    m_hovered = true;
    updateStyle();
    QWidget::enterEvent(event);
}

void FileCard::leaveEvent(QEvent* event) {
    m_hovered = false;
    updateStyle();
    QWidget::leaveEvent(event);
}

void FileCard::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    if (m_selected) {
        QColor selection = StyleTokens::Colors::fromHex(
            StyleTokens::Colors::kAccent);
        selection.setAlpha(31);
        painter.fillRect(rect(), selection);
        painter.fillRect(QRect(0, 0, 2, height()),
                         StyleTokens::Colors::fromHex(
                             StyleTokens::Colors::kAccent));
    } else if (m_hovered) {
        painter.fillRect(rect(), StyleTokens::Colors::fromHex(
                                     StyleTokens::Colors::kBackgroundOverlay));
    }
}

void FileCard::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateElidedName();
}

// ==================== ProjectPanel ====================

ProjectPanel::ProjectPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setAcceptDrops(true);

    m_thumbnailGen = new ThumbnailGenerator(this);
    connect(m_thumbnailGen, &ThumbnailGenerator::thumbnailReady,
            this, &ProjectPanel::onThumbnailReady);
    connect(m_thumbnailGen, &ThumbnailGenerator::metadataReady,
            this, &ProjectPanel::onMetadataReady);
}

ProjectPanel::~ProjectPanel() = default;

void ProjectPanel::setScene(ProcessingScene scene) {
    if (!m_sceneCombo) {
        m_scene = scene;
        return;
    }

    const int index = m_sceneCombo->findData(static_cast<int>(scene));
    if (index < 0) return;

    m_scene = scene;
    const QSignalBlocker blocker(m_sceneCombo);
    m_sceneCombo->setCurrentIndex(index);
    if (m_calibrationSummary) {
        m_calibrationSummary->setVisible(scene == ProcessingScene::DeepSky);
    }
    updateBottomBar();
}

ProcessingScene ProjectPanel::currentScene() const {
    return m_scene;
}

void ProjectPanel::setEditingEnabled(bool enabled) {
    if (m_editingEnabled == enabled) return;
    m_editingEnabled = enabled;
    updateEditingState();
}

void ProjectPanel::setCalibrationSummary(
    const QString& dark, const QString& flat, const QString& bias,
    const QString& darkFlat, bool ready) {
    if (m_darkSummary) m_darkSummary->setText(dark);
    if (m_flatSummary) m_flatSummary->setText(flat);
    if (m_biasSummary) m_biasSummary->setText(bias);
    if (m_darkFlatSummary) m_darkFlatSummary->setText(darkFlat);
    if (m_calibrationReady) {
        m_calibrationReady->setText(
            ready ? QString::fromUtf8("已就绪")
                  : QString::fromUtf8("来源不完整"));
        m_calibrationReady->setProperty(
            StyleTokens::Properties::kStatusRole,
            QString::fromLatin1(
                ready ? StyleTokens::Properties::kSuccess
                      : StyleTokens::Properties::kWarning));
        refreshStyle(m_calibrationReady);
    }
}

void ProjectPanel::setupUI() {
    setObjectName("projectPanel");
    setStringProperty(this, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(setupHeader());

    auto* headerSeparator = new QFrame(this);
    setStringProperty(headerSeparator, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kSeparator);
    layout->addWidget(headerSeparator);

    auto* contentWidget = new QWidget(this);
    setStringProperty(contentWidget, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    setupEmptyState();
    contentLayout->addWidget(m_emptyState, 1, Qt::AlignCenter);

    setupFileList();
    contentLayout->addWidget(m_scrollArea, 1);
    m_scrollArea->setVisible(false);

    layout->addWidget(contentWidget, 1);

    m_calibrationSummary = setupCalibrationSummary();
    m_calibrationSummary->setVisible(false);
    layout->addWidget(m_calibrationSummary);

    setupBottomBar();
    auto* bottomSeparator = new QFrame(this);
    setStringProperty(bottomSeparator, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kSeparator);
    layout->addWidget(bottomSeparator);
    layout->addWidget(m_bottomLabel);

    m_contextMenu = new QMenu(this);

    m_referenceAction = new QAction(QString::fromUtf8("设为参考帧"), this);
    connect(m_referenceAction, &QAction::triggered, this, &ProjectPanel::onSetReferenceFrame);
    m_contextMenu->addAction(m_referenceAction);

    m_excludeAction = new QAction(QString::fromUtf8("排除 / 恢复"), this);
    connect(m_excludeAction, &QAction::triggered, this, &ProjectPanel::onExcludeSelected);
    m_contextMenu->addAction(m_excludeAction);

    m_contextMenu->addSeparator();
    m_removeAction = new QAction(QString::fromUtf8("从列表移除"), this);
    connect(m_removeAction, &QAction::triggered, this, &ProjectPanel::onRemoveFromList);
    m_contextMenu->addAction(m_removeAction);

    m_contextMenu->addSeparator();
    m_revealAction = new QAction(QString::fromUtf8("在文件管理器中显示"), this);
    connect(m_revealAction, &QAction::triggered,
            this, &ProjectPanel::onRevealInFileManager);
    m_contextMenu->addAction(m_revealAction);

    m_metadataAction = new QAction(QString::fromUtf8("查看元数据"), this);
    connect(m_metadataAction, &QAction::triggered,
            this, &ProjectPanel::onViewMetadata);
    m_contextMenu->addAction(m_metadataAction);

    updateEditingState();
    updateBottomBar();
}

QWidget* ProjectPanel::setupHeader() {
    auto* header = new QWidget(this);
    header->setObjectName("projectHeader");
    setStringProperty(header, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);

    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(
        StyleTokens::Spacing::kPanelPadding, 12,
        StyleTokens::Spacing::kPanelPadding, 12);
    headerLayout->setSpacing(StyleTokens::Spacing::kControlGap);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(StyleTokens::Spacing::kBase);

    auto* titleLabel = new QLabel(QString::fromUtf8("素材"), header);
    setStringProperty(titleLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kTitle);
    titleRow->addWidget(titleLabel);

    m_countLabel = new QLabel(QString::fromUtf8("0 张"), header);
    setStringProperty(m_countLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    titleRow->addWidget(m_countLabel);
    titleRow->addStretch();

    m_headerImportBtn = new QPushButton(header);
    setStringProperty(m_headerImportBtn, StyleTokens::Properties::kVariant,
                      StyleTokens::Properties::kIcon);
    m_headerImportBtn->setIcon(UiAssets::icon(
        UiAssets::Glyph::Add,
        StyleTokens::Colors::fromHex(StyleTokens::Colors::kTextSecondary)));
    m_headerImportBtn->setIconSize(
        QSize(StyleTokens::Controls::kIconSize,
              StyleTokens::Controls::kIconSize));
    m_headerImportBtn->setCursor(Qt::PointingHandCursor);
    m_headerImportBtn->setToolTip(QString::fromUtf8("导入 RAW 素材"));
    m_headerImportBtn->setAccessibleName(m_headerImportBtn->toolTip());
    connect(m_headerImportBtn, &QPushButton::clicked,
            this, &ProjectPanel::onImportClicked);
    titleRow->addWidget(m_headerImportBtn);
    headerLayout->addLayout(titleRow);

    m_sceneCombo = new QComboBox(header);
    m_sceneCombo->setAccessibleName(QString::fromUtf8("处理场景"));
    m_sceneCombo->setToolTip(QString::fromUtf8("选择处理场景"));
    const QColor iconColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kTextSecondary);
    const ProcessingScene scenes[] = {
        ProcessingScene::SingleFrame,
        ProcessingScene::Nightscape,
        ProcessingScene::DeepSky,
        ProcessingScene::SkyGround,
        ProcessingScene::StarTrail,
        ProcessingScene::Timelapse
    };
    for (const ProcessingScene scene : scenes) {
        m_sceneCombo->addItem(
            UiAssets::icon(sceneGlyph(scene), iconColor), sceneName(scene),
            static_cast<int>(scene));
    }
    m_sceneCombo->setCurrentIndex(
        m_sceneCombo->findData(static_cast<int>(m_scene)));
    connect(m_sceneCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (index < 0) return;
                m_scene = static_cast<ProcessingScene>(
                    m_sceneCombo->itemData(index).toInt());
                updateBottomBar();
                emit sceneChanged(m_scene);
            });
    headerLayout->addWidget(m_sceneCombo);
    return header;
}

QWidget* ProjectPanel::setupCalibrationSummary() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("calibrationSummary"));
    setStringProperty(panel, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(
        StyleTokens::Spacing::kPanelPadding,
        StyleTokens::Spacing::kControlGap,
        StyleTokens::Spacing::kPanelPadding,
        StyleTokens::Spacing::kControlGap);
    layout->setSpacing(StyleTokens::Spacing::kMicro);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QString::fromUtf8("校准帧"), panel);
    setStringProperty(title, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kTitle);
    titleRow->addWidget(title);
    m_calibrationReady = new QLabel(QString::fromUtf8("来源不完整"), panel);
    setStringProperty(m_calibrationReady,
                      StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    titleRow->addWidget(m_calibrationReady);
    titleRow->addStretch();
    m_calibrationSettingsBtn = new QPushButton(
        QString::fromUtf8("设置"), panel);
    setStringProperty(m_calibrationSettingsBtn,
                      StyleTokens::Properties::kVariant,
                      StyleTokens::Properties::kGhost);
    m_calibrationSettingsBtn->setAccessibleName(
        QString::fromUtf8("设置深空校准帧"));
    connect(m_calibrationSettingsBtn, &QPushButton::clicked,
            this, &ProjectPanel::calibrationSettingsRequested);
    titleRow->addWidget(m_calibrationSettingsBtn);
    layout->addLayout(titleRow);

    const auto addRow = [panel, layout](const QString& name,
                                        QLabel*& valueLabel) {
        auto* row = new QHBoxLayout();
        row->setSpacing(StyleTokens::Spacing::kBase);
        auto* nameLabel = new QLabel(name, panel);
        nameLabel->setFixedWidth(64);
        setStringProperty(nameLabel, StyleTokens::Properties::kTextRole,
                          StyleTokens::Properties::kCaption);
        row->addWidget(nameLabel);
        valueLabel = new QLabel(QString::fromUtf8("未设置"), panel);
        setStringProperty(valueLabel, StyleTokens::Properties::kTextRole,
                          StyleTokens::Properties::kMono);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(valueLabel, 1);
        layout->addLayout(row);
    };
    addRow(QStringLiteral("Dark"), m_darkSummary);
    addRow(QStringLiteral("Flat"), m_flatSummary);
    addRow(QStringLiteral("Bias"), m_biasSummary);
    addRow(QStringLiteral("Dark Flat"), m_darkFlatSummary);

    auto* divider = new QFrame(panel);
    setStringProperty(divider, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kSeparator);
    layout->addWidget(divider);
    return panel;
}

void ProjectPanel::setupEmptyState() {
    m_emptyState = new QWidget(this);
    setStringProperty(m_emptyState, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    m_emptyLayout = new QVBoxLayout(m_emptyState);
    m_emptyLayout->setContentsMargins(
        StyleTokens::Spacing::kPanelPadding,
        StyleTokens::Spacing::kPanelPadding,
        StyleTokens::Spacing::kPanelPadding,
        StyleTokens::Spacing::kPanelPadding);
    m_emptyLayout->setSpacing(StyleTokens::Spacing::kBase);
    m_emptyLayout->setAlignment(Qt::AlignCenter);

    auto* promptLabel = new QLabel(
        QString::fromUtf8("拖入一组 RAW 开始"), m_emptyState);
    setStringProperty(promptLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kSecondary);
    promptLabel->setAlignment(Qt::AlignCenter);
    promptLabel->setWordWrap(true);
    m_emptyLayout->addWidget(promptLabel);

    m_emptyImportBtn = new QPushButton(QString::fromUtf8("选择文件"), m_emptyState);
    setStringProperty(m_emptyImportBtn, StyleTokens::Properties::kVariant,
                      StyleTokens::Properties::kGhost);
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    m_emptyImportBtn->setAccessibleName(
        QString::fromUtf8("选择 RAW 素材文件"));
    connect(m_emptyImportBtn, &QPushButton::clicked, this, &ProjectPanel::onImportClicked);
    m_emptyLayout->addWidget(m_emptyImportBtn, 0, Qt::AlignCenter);
}

void ProjectPanel::setupFileList() {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStringProperty(m_scrollArea, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    setStringProperty(m_scrollArea->viewport(),
                      StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);

    m_listContainer = new QWidget();
    setStringProperty(m_listContainer, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(8, 4, 8, 4);
    m_listLayout->setSpacing(0);
    m_listLayout->addStretch();

    m_scrollArea->setWidget(m_listContainer);
}

void ProjectPanel::setupBottomBar() {
    m_bottomLabel = new QLabel(this);
    m_bottomLabel->setFixedHeight(32);
    m_bottomLabel->setContentsMargins(
        StyleTokens::Spacing::kPanelPadding, 0,
        StyleTokens::Spacing::kPanelPadding, 0);
    setStringProperty(m_bottomLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kStatus);
    m_bottomLabel->setText(QString::fromUtf8("0 张 · 还需 2 张"));
}

void ProjectPanel::showFileList() {
    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);
}

void ProjectPanel::showEmptyState() {
    m_emptyState->setVisible(true);
    m_scrollArea->setVisible(false);
}

void ProjectPanel::addFiles(const QStringList& filePaths) {
    if (!m_editingEnabled) return;

    bool added = false;
    QStringList thumbnailQueue;
    for (const QString& filePath : filePaths) {
        if (findIndexByPath(filePath) >= 0) continue;

        FileItem item;
        item.filePath = filePath;
        item.fileName = QFileInfo(filePath).fileName();

        // 元数据提取已改为异步：由 ThumbnailGenerator 在 worker 线程加载
        // 完成后通过 metadataReady 信号传回主线程

        m_fileItems.append(item);
        addFileCard(item);
        thumbnailQueue.append(filePath);
        added = true;
    }

    if (added) {
        showFileList();
        // Import should immediately produce a useful preview instead of
        // leaving the centre canvas in its empty state.
        QString centralPreviewPath;
        if (m_currentIndex < 0 && !m_cards.isEmpty()) {
            setCurrentIndex(0);
            centralPreviewPath = m_fileItems[0].filePath;
        }
        // The selected first frame is decoded by PreviewPanel and feeds its
        // thumbnail back into this list. Skip a duplicate task for that file;
        // the remaining cards can populate concurrently in the background.
        for (const QString& filePath : thumbnailQueue) {
            if (filePath == centralPreviewPath) continue;
            if (centralPreviewPath.isEmpty()) {
                m_thumbnailGen->generateAsync(filePath, 96);
            } else {
                m_pendingThumbnailPaths.append(filePath);
            }
        }
        updateBottomBar();
        emit filesChanged();
    }
}

void ProjectPanel::applyPreviewData(const QString& filePath,
                                    const QImage& image, int iso,
                                    double exposureTime, double aperture,
                                    int focalLength) {
    const int index = findIndexByPath(filePath);
    if (index < 0 || image.isNull()) return;
    m_fileItems[index].thumbnail = QPixmap::fromImage(image.scaled(
        96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_fileItems[index].hasThumbnail = true;
    m_fileItems[index].iso = iso;
    m_fileItems[index].exposureTime = exposureTime;
    m_fileItems[index].aperture = aperture;
    m_fileItems[index].focalLength = focalLength;
    m_fileItems[index].metadataLoaded = true;
    m_fileItems[index].metadataFailed = false;
    updateCard(index);
    updateBottomBar();
    for (const QString& pending : std::exchange(
             m_pendingThumbnailPaths, QStringList())) {
        m_thumbnailGen->generateAsync(pending, 96);
    }
}

void ProjectPanel::requestThumbnail(const QString& filePath) {
    if (findIndexByPath(filePath) >= 0) {
        m_thumbnailGen->generateAsync(filePath, 96);
    }
    for (const QString& pending : std::exchange(
             m_pendingThumbnailPaths, QStringList())) {
        m_thumbnailGen->generateAsync(pending, 96);
    }
}

void ProjectPanel::addFileCard(const FileItem& item) {
    auto* card = new FileCard(item, m_listContainer);
    connect(card, &FileCard::clicked, this, [this, card]() {
        setCurrentIndex(m_cards.indexOf(card));
    });
    connect(card, &FileCard::customContextMenuRequested, this, [this, card](const QPoint& pos) {
        m_contextMenuIndex = m_cards.indexOf(card);
        if (m_contextMenuIndex < 0) return;
        const FileItem& item = m_fileItems[m_contextMenuIndex];
        m_referenceAction->setText(
            item.isReferenceFrame
                ? QString::fromUtf8("当前参考帧")
                : QString::fromUtf8("设为参考帧"));
        m_referenceAction->setEnabled(
            m_editingEnabled && !item.isReferenceFrame);
        m_excludeAction->setText(
            item.isExcluded
                ? QString::fromUtf8("恢复此帧")
                : QString::fromUtf8("排除此帧"));
        m_excludeAction->setEnabled(
            m_editingEnabled && (!item.isReferenceFrame || item.isExcluded));
        m_removeAction->setEnabled(m_editingEnabled);
        m_contextMenu->exec(card->mapToGlobal(pos));
        m_contextMenuIndex = -1;
    });
    m_cards.append(card);
    // 插入到 stretch 之前
    m_listLayout->insertWidget(m_listLayout->count() - 1, card);
}

void ProjectPanel::clearFiles() {
    if (!m_editingEnabled) return;

    for (auto* card : m_cards) {
        card->deleteLater();
    }
    m_cards.clear();
    m_fileItems.clear();
    m_pendingThumbnailPaths.clear();
    m_currentIndex = -1;
    showEmptyState();
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::removeSelected() {
    if (!m_editingEnabled) return;
    if (m_currentIndex < 0 || m_currentIndex >= m_cards.size()) return;

    // Keep the canvas and list selection in sync after removing the active
    // source. Selecting the nearest surviving frame also invalidates any
    // asynchronous preview result that may arrive for the removed file.
    const int idx = m_currentIndex;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);
    m_currentIndex = -1;

    if (m_cards.isEmpty()) {
        showEmptyState();
        emit fileSelected(QString());
    } else {
        setCurrentIndex(std::min(idx, static_cast<int>(m_cards.size()) - 1));
    }
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::setReferenceFrame(const QString& filePath) {
    if (!m_editingEnabled) return;

    bool changed = false;
    for (int i = 0; i < m_fileItems.size(); ++i) {
        const bool isReference = m_fileItems[i].filePath == filePath;
        changed = changed || m_fileItems[i].isReferenceFrame != isReference;
        m_fileItems[i].isReferenceFrame = isReference;
        if (isReference && m_fileItems[i].isExcluded) {
            m_fileItems[i].isExcluded = false;
            changed = true;
        }
        updateCard(i);
    }
    if (!changed) return;
    updateBottomBar();
    emit referenceFrameChanged();
}

void ProjectPanel::setReferenceFrame(int index) {
    if (index < 0 || index >= m_fileItems.size()) return;
    setReferenceFrame(m_fileItems[index].filePath);
}

QStringList ProjectPanel::includedFilePaths() const {
    QStringList paths;
    for (const auto& item : m_fileItems) {
        if (!item.isExcluded) {
            paths.append(item.filePath);
        }
    }
    return paths;
}

QString ProjectPanel::referenceFramePath() const {
    for (const auto& item : m_fileItems) {
        if (item.isReferenceFrame) return item.filePath;
    }
    return QString();
}

QStringList ProjectPanel::filePaths() const {
    QStringList paths;
    for (const auto& item : m_fileItems) {
        if (!item.isExcluded) {
            paths.append(item.filePath);
        }
    }
    return paths;
}

QString ProjectPanel::currentFilePath() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_fileItems.size()) {
        return QString();
    }
    return m_fileItems[m_currentIndex].filePath;
}

void ProjectPanel::setCurrentIndex(int index) {
    if (index < 0 || index >= m_fileItems.size()) return;

    // 清除旧选中
    if (m_currentIndex >= 0 && m_currentIndex < m_cards.size()) {
        m_cards[m_currentIndex]->setSelected(false);
    }

    m_currentIndex = index;
    m_cards[m_currentIndex]->setSelected(true);

    // 确保可见
    m_scrollArea->ensureWidgetVisible(m_cards[m_currentIndex]);

    emit fileSelected(m_fileItems[m_currentIndex].filePath);
    updateBottomBar();
}

void ProjectPanel::updateCard(int index) {
    if (index < 0 || index >= m_cards.size()) return;
    m_cards[index]->updateFromItem(m_fileItems[index]);
}

void ProjectPanel::updateAllCardStyles() {
    for (int i = 0; i < m_cards.size(); ++i) {
        m_cards[i]->setSelected(i == m_currentIndex);
    }
}

void ProjectPanel::updateBottomBar() {
    const int total = m_fileItems.size();
    int excluded = 0;
    QString referenceName;
    for (const auto& item : m_fileItems) {
        if (item.isExcluded) {
            ++excluded;
        }
        if (item.isReferenceFrame) {
            referenceName = item.fileName;
        }
    }

    const int available = total - excluded;
    const int required = minimumFrameCount();
    const bool ready = available >= required;
    const QString status = ready
        ? QString::fromUtf8("已就绪")
        : QString::fromUtf8("还需 %1 张").arg(required - available);
    m_bottomLabel->setText(
        QString::fromUtf8("%1 张 · %2").arg(available).arg(status));

    const QString reference = referenceName.isEmpty()
        ? QString::fromUtf8("自动")
        : referenceName;
    m_bottomLabel->setToolTip(
        QString::fromUtf8("共 %1 张，排除 %2 张，参考帧：%3")
            .arg(total)
            .arg(excluded)
            .arg(reference));
    m_bottomLabel->setProperty(
        StyleTokens::Properties::kStatusRole,
        QString::fromLatin1(ready ? StyleTokens::Properties::kSuccess
                                  : StyleTokens::Properties::kWarning));
    refreshStyle(m_bottomLabel);
    if (m_countLabel) {
        m_countLabel->setText(QString::fromUtf8("%1 张").arg(total));
    }
}

int ProjectPanel::minimumFrameCount() const {
    switch (m_scene) {
    case ProcessingScene::SingleFrame:
        return 1;
    case ProcessingScene::StarTrail:
    case ProcessingScene::Timelapse:
        return 3;
    case ProcessingScene::Nightscape:
    case ProcessingScene::DeepSky:
    case ProcessingScene::SkyGround:
        return 2;
    }
    return 1;
}

void ProjectPanel::updateEditingState() {
    if (m_sceneCombo) m_sceneCombo->setEnabled(m_editingEnabled);
    if (m_headerImportBtn) m_headerImportBtn->setEnabled(m_editingEnabled);
    if (m_emptyImportBtn) m_emptyImportBtn->setEnabled(m_editingEnabled);
    if (m_calibrationSettingsBtn) {
        m_calibrationSettingsBtn->setEnabled(m_editingEnabled);
    }
    if (m_referenceAction) m_referenceAction->setEnabled(m_editingEnabled);
    if (m_excludeAction) m_excludeAction->setEnabled(m_editingEnabled);
    if (m_removeAction) m_removeAction->setEnabled(m_editingEnabled);
    setAcceptDrops(m_editingEnabled);
}

void ProjectPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateBottomBar();
}

int ProjectPanel::findIndexByPath(const QString& filePath) const {
    for (int i = 0; i < m_fileItems.size(); ++i) {
        if (m_fileItems[i].filePath == filePath) return i;
    }
    return -1;
}

void ProjectPanel::onThumbnailReady(const QString& filePath, const QPixmap& thumbnail) {
    int idx = findIndexByPath(filePath);
    if (idx < 0) return;
    m_fileItems[idx].thumbnail = thumbnail;
    m_fileItems[idx].hasThumbnail = !thumbnail.isNull();
    updateCard(idx);
}

void ProjectPanel::onMetadataReady(const QString& filePath, int iso,
                                   double exposureTime, double aperture,
                                   int focalLength, bool loaded) {
    int idx = findIndexByPath(filePath);
    if (idx < 0) return;
    m_fileItems[idx].iso = iso;
    m_fileItems[idx].exposureTime = exposureTime;
    m_fileItems[idx].aperture = aperture;
    m_fileItems[idx].focalLength = focalLength;
    m_fileItems[idx].metadataLoaded = loaded;
    m_fileItems[idx].metadataFailed = !loaded;
    updateCard(idx);
    updateBottomBar();
}

void ProjectPanel::onCustomContextMenu(const QPoint& pos) {
    Q_UNUSED(pos)
    // 通过卡片触发
}

void ProjectPanel::onExcludeSelected() {
    if (!m_editingEnabled) return;
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;
    if (m_fileItems[m_contextMenuIndex].isReferenceFrame &&
        !m_fileItems[m_contextMenuIndex].isExcluded) return;
    m_fileItems[m_contextMenuIndex].isExcluded = !m_fileItems[m_contextMenuIndex].isExcluded;
    updateCard(m_contextMenuIndex);
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::onSetReferenceFrame() {
    if (!m_editingEnabled) return;
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;
    setReferenceFrame(m_fileItems[m_contextMenuIndex].filePath);
    emit filesChanged();
}

void ProjectPanel::onViewMetadata() {
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;
    emit requestMetadata(m_fileItems[m_contextMenuIndex].filePath);
}

void ProjectPanel::onRevealInFileManager() {
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;

    const QString filePath = m_fileItems[m_contextMenuIndex].filePath;
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                            {QStringLiteral("-R"), filePath});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,") + QDir::toNativeSeparators(filePath)});
#else
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
#endif
}

void ProjectPanel::onRemoveFromList() {
    if (!m_editingEnabled) return;
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;

    const int idx = m_contextMenuIndex;
    const bool removedCurrent = m_currentIndex == idx;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);

    if (removedCurrent) {
        m_currentIndex = -1;
    } else if (m_currentIndex > idx) {
        m_currentIndex--;
    }

    if (m_cards.isEmpty()) {
        showEmptyState();
        emit fileSelected(QString());
    } else if (removedCurrent) {
        setCurrentIndex(std::min(idx, static_cast<int>(m_cards.size()) - 1));
    }
    updateAllCardStyles();
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::onImportClicked() {
    if (!m_editingEnabled) return;
    emit filesDropped(QStringList());
}

void ProjectPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (m_editingEnabled && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ProjectPanel::dropEvent(QDropEvent* event) {
    if (!m_editingEnabled) {
        event->ignore();
        return;
    }

    const QMimeData* mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    QStringList filePaths;
    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            filePaths.append(url.toLocalFile());
        }
    }

    if (!filePaths.isEmpty()) {
        addFiles(filePaths);
        emit filesDropped(filePaths);
    }
}
