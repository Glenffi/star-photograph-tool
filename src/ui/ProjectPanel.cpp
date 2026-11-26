#include "ProjectPanel.h"
#include "StyleTokens.h"
#include "UiAssets.h"
#include "../core/ThumbnailGenerator.h"
#include <QCoreApplication>
#include <QComboBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMenu>
#include <QAction>
#include <QDesktopServices>
#include <QCryptographicHash>
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
#include <QTimer>
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

class RoundedThumbnailLabel final : public QLabel {
public:
    explicit RoundedThumbnailLabel(QWidget* parent = nullptr)
        : QLabel(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
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
        setToolTip(QCoreApplication::translate("Nebulens", "参考帧"));
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
        return QCoreApplication::translate("Nebulens", "曝光 --");
    }

    if (exposureTime >= 1.0) {
        const int decimals = exposureTime < 10.0 ? 1 : 0;
        return QCoreApplication::translate("Nebulens", "%1 s").arg(exposureTime, 0, 'f', decimals);
    }

    return QCoreApplication::translate("Nebulens", "1/%1 s").arg(qRound(1.0 / exposureTime));
}

QString formatMetadata(const FileItem& item) {
    if (item.metadataFailed) {
        return QCoreApplication::translate("Nebulens", "RAW 读取失败");
    }
    if (!item.metadataLoaded) {
        return QCoreApplication::translate("Nebulens", "正在读取元数据...");
    }
    const QString iso = item.iso > 0
        ? QCoreApplication::translate("Nebulens", "ISO %1").arg(item.iso)
        : QCoreApplication::translate("Nebulens", "ISO --");
    const QString focalLength = item.focalLength > 0
        ? QCoreApplication::translate("Nebulens", "%1 mm").arg(item.focalLength)
        : QCoreApplication::translate("Nebulens", "焦距 --");
    return QCoreApplication::translate("Nebulens", "%1 · %2 · %3")
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
    setFixedSize(96, StyleTokens::Controls::kMaterialRowHeight);
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFocusPolicy(Qt::StrongFocus);

    m_thumbnailLabel = new RoundedThumbnailLabel(this);
    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        static_cast<RoundedThumbnailLabel*>(m_thumbnailLabel)
            ->setThumbnail(item.thumbnail);
    }
    m_referenceDot = new ReferenceDot(this);
    m_referenceDot->hide();

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_nameLabel->setObjectName(QStringLiteral("filmstripFileName"));
    m_nameLabel->setStyleSheet(QStringLiteral("font-size: %1px;")
        .arg(StyleTokens::Typography::kCaptionSize));
    m_nameLabel->setMinimumWidth(0);
    m_nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_nameLabel->setTextFormat(Qt::PlainText);
    setStringProperty(m_nameLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kMono);
    m_nameLabel->setText(m_fileName);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setMinimumWidth(0);
    m_metaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_metaLabel->setText(formatMetadata(item));
    setStringProperty(m_metaLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    m_metaLabel->hide();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_statusLabel->setObjectName(QStringLiteral("filmstripStatus"));
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: %1px;")
        .arg(StyleTokens::Typography::kCaptionSize));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    setStringProperty(m_statusLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    m_opacityEffect = new QGraphicsOpacityEffect(m_thumbnailLabel);
    m_thumbnailLabel->setGraphicsEffect(m_opacityEffect);
    updateStyle();
}

void FileCard::updateFromItem(const FileItem& item) {
    m_isReference = item.isReferenceFrame;
    m_isExcluded = item.isExcluded;
    m_fileName = item.fileName;
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
    if (m_selected == selected) return;
    m_selected = selected;
    updateStyle();
}

void FileCard::updateStyle() {
    setProperty("selected", m_selected);
    setProperty("hovered", m_hovered);
    setProperty("reference", m_isReference);
    setProperty("excluded", m_isExcluded);

    if (m_isReference && m_isExcluded) {
        m_statusLabel->setText(QCoreApplication::translate(
            "Nebulens", "参考 / 排除"));
    } else if (m_isReference) {
        m_statusLabel->setText(QCoreApplication::translate(
            "Nebulens", "参考"));
    } else if (m_isExcluded) {
        m_statusLabel->setText(QCoreApplication::translate(
            "Nebulens", "排除"));
    } else {
        m_statusLabel->clear();
    }
    m_statusLabel->setVisible(!m_statusLabel->text().isEmpty());
    updateStatusGeometry();

    m_referenceDot->setVisible(false);
    m_opacityEffect->setOpacity(m_isExcluded ? 0.4 : 1.0);

    QString accessible = m_fileName + QCoreApplication::translate("Nebulens", "，") +
        m_metaLabel->text();
    if (m_isReference) accessible += QCoreApplication::translate("Nebulens", "，参考帧");
    if (m_isExcluded) accessible += QCoreApplication::translate("Nebulens", "，已排除");
    setAccessibleName(accessible);
    setToolTip(accessible);

    update();
}

void FileCard::updateElidedName() {
    // The badge stylesheet reserves four pixels of padding on either side.
    const int availableWidth = m_nameLabel->width() - 8;
    if (availableWidth <= 0) {
        m_nameLabel->setText(m_fileName);
        return;
    }
    m_nameLabel->setText(
        m_nameLabel->fontMetrics().elidedText(m_fileName, Qt::ElideMiddle, availableWidth)
    );
}

void FileCard::updateStatusGeometry() {
    const QSize preferred = m_statusLabel->sizeHint();
    const int badgeWidth = std::min(width() - 12, preferred.width());
    m_statusLabel->setGeometry(width() - 6 - badgeWidth, 6,
                               badgeWidth, preferred.height());
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

void FileCard::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if (event->reason() != Qt::MouseFocusReason) emit clicked();
    update();
}

void FileCard::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    update();
}

void FileCard::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    if (m_selected) {
        QColor selection = StyleTokens::Colors::fromHex(
            StyleTokens::Colors::kAccent);
        selection.setAlpha(22);
        painter.fillRect(rect(), selection);
        for (int inset = 0; inset < 3; ++inset) {
            QColor edge = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kAccentStrong);
            edge.setAlpha(170 - inset * 52);
            painter.setPen(QPen(edge, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(
                QRectF(rect()).adjusted(inset + 0.5, inset + 0.5,
                                        -inset - 0.5, -inset - 0.5),
                StyleTokens::Radius::kSmall,
                StyleTokens::Radius::kSmall);
        }
    } else if (m_hovered) {
        painter.fillRect(rect(), StyleTokens::Colors::fromHex(
                                     StyleTokens::Colors::kBackgroundOverlay));
    }
    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(StyleTokens::Colors::fromHex(
                                StyleTokens::Colors::kTextPrimary),
                            2.0));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1),
                                StyleTokens::Radius::kSmall,
                                StyleTokens::Radius::kSmall);
    }
}

void FileCard::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_thumbnailLabel) m_thumbnailLabel->setGeometry(rect().adjusted(3, 3, -3, -3));
    if (m_nameLabel) {
        const int nameHeight = std::max(16, m_nameLabel->sizeHint().height());
        m_nameLabel->setGeometry(6, height() - nameHeight - 5,
                                 std::max(1, width() - 12), nameHeight);
        updateElidedName();
    }
    if (m_statusLabel) updateStatusGeometry();
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
    m_undoTimer = new QTimer(this);
    m_undoTimer->setSingleShot(true);
    m_undoTimer->setInterval(5000);
    connect(m_undoTimer, &QTimer::timeout, this, [this]() {
        m_lastRemovedItem.reset();
        m_lastRemovedIndex = -1;
        emit undoAvailabilityChanged(false, QString());
    });
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
            ready ? QCoreApplication::translate("Nebulens", "已就绪")
                  : QCoreApplication::translate("Nebulens", "来源不完整"));
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

    setFixedHeight(StyleTokens::Layout::kFilmstripHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(setupHeader());

    auto* contentWidget = new QWidget(this);
    contentWidget->setObjectName(QStringLiteral("filmstripContent"));
    setStringProperty(contentWidget, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    setupEmptyState();
    contentLayout->addWidget(m_emptyState, 1);

    setupFileList();
    contentLayout->addWidget(m_scrollArea, 1);
    m_scrollArea->setVisible(false);

    layout->addWidget(contentWidget, 1);

    setupBottomBar();
    m_bottomLabel->hide();

    m_contextMenu = new QMenu(this);

    m_referenceAction = new QAction(QCoreApplication::translate("Nebulens", "设为参考帧"), this);
    connect(m_referenceAction, &QAction::triggered, this, &ProjectPanel::onSetReferenceFrame);
    m_contextMenu->addAction(m_referenceAction);

    m_excludeAction = new QAction(QCoreApplication::translate("Nebulens", "排除 / 恢复"), this);
    connect(m_excludeAction, &QAction::triggered, this, &ProjectPanel::onExcludeSelected);
    m_contextMenu->addAction(m_excludeAction);

    m_contextMenu->addSeparator();
    m_removeAction = new QAction(QCoreApplication::translate("Nebulens", "从列表移除"), this);
    connect(m_removeAction, &QAction::triggered, this, &ProjectPanel::onRemoveFromList);
    m_contextMenu->addAction(m_removeAction);

    m_contextMenu->addSeparator();
    m_revealAction = new QAction(QCoreApplication::translate("Nebulens", "在文件管理器中显示"), this);
    connect(m_revealAction, &QAction::triggered,
            this, &ProjectPanel::onRevealInFileManager);
    m_contextMenu->addAction(m_revealAction);

    m_metadataAction = new QAction(QCoreApplication::translate("Nebulens", "查看元数据"), this);
    connect(m_metadataAction, &QAction::triggered,
            this, &ProjectPanel::onViewMetadata);
    m_contextMenu->addAction(m_metadataAction);

    updateEditingState();
    updateBottomBar();
}

QWidget* ProjectPanel::setupHeader() {
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("filmstripHeader"));
    header->setFixedWidth(112);
    setStringProperty(header, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);

    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(13, 12, 10, 10);
    headerLayout->setSpacing(6);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(StyleTokens::Spacing::kBase);

    auto* titleLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "亮场素材"), header);
    titleLabel->setObjectName(QStringLiteral("filmstripKicker"));
    titleLabel->setStyleSheet(QStringLiteral("font-size: %1px; letter-spacing: 0px;")
        .arg(StyleTokens::Typography::kCaptionSize));
    titleRow->addWidget(titleLabel);

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
    m_headerImportBtn->setToolTip(QCoreApplication::translate("Nebulens", "导入 RAW 素材"));
    m_headerImportBtn->setAccessibleName(m_headerImportBtn->toolTip());
    connect(m_headerImportBtn, &QPushButton::clicked,
            this, &ProjectPanel::onImportClicked);
    titleRow->addWidget(m_headerImportBtn);
    headerLayout->addLayout(titleRow);

    m_countLabel = new QLabel(QStringLiteral("0 / 0"), header);
    m_countLabel->setObjectName(QStringLiteral("filmstripCount"));
    setStringProperty(m_countLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kTitle);
    headerLayout->addWidget(m_countLabel);
    auto* hint = new QLabel(QCoreApplication::translate("Nebulens", "点击查看 · 空格排除"), header);
    hint->setToolTip(hint->text());
    hint->setText(QCoreApplication::translate("Nebulens", "空格排除"));
    setStringProperty(hint, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    headerLayout->addWidget(hint);
    headerLayout->addStretch(1);
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
    auto* title = new QLabel(QCoreApplication::translate("Nebulens", "校准帧"), panel);
    setStringProperty(title, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kTitle);
    titleRow->addWidget(title);
    m_calibrationReady = new QLabel(QCoreApplication::translate("Nebulens", "来源不完整"), panel);
    setStringProperty(m_calibrationReady,
                      StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kCaption);
    titleRow->addWidget(m_calibrationReady);
    titleRow->addStretch();
    m_calibrationSettingsBtn = new QPushButton(
        QCoreApplication::translate("Nebulens", "设置"), panel);
    setStringProperty(m_calibrationSettingsBtn,
                      StyleTokens::Properties::kVariant,
                      StyleTokens::Properties::kGhost);
    m_calibrationSettingsBtn->setAccessibleName(
        QCoreApplication::translate("Nebulens", "设置深空校准帧"));
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
        valueLabel = new QLabel(QCoreApplication::translate("Nebulens", "未设置"), panel);
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
    m_emptyLayout->setContentsMargins(20, 10, 20, 10);
    m_emptyLayout->setSpacing(4);
    m_emptyLayout->setAlignment(Qt::AlignCenter);

    auto* promptLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "暂无素材 · 可拖入"), m_emptyState);
    setStringProperty(promptLabel, StyleTokens::Properties::kTextRole,
                      StyleTokens::Properties::kSecondary);
    promptLabel->setAlignment(Qt::AlignCenter);
    promptLabel->setWordWrap(false);
    m_emptyLayout->addWidget(promptLabel);

    m_emptyImportBtn = new QPushButton(QCoreApplication::translate("Nebulens", "选择文件"), m_emptyState);
    setStringProperty(m_emptyImportBtn, StyleTokens::Properties::kVariant,
                      StyleTokens::Properties::kGhost);
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    m_emptyImportBtn->setAccessibleName(
        QCoreApplication::translate("Nebulens", "选择 RAW 素材文件"));
    connect(m_emptyImportBtn, &QPushButton::clicked, this, &ProjectPanel::onImportClicked);
    m_emptyImportBtn->hide();
}

void ProjectPanel::setupFileList() {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setObjectName(QStringLiteral("filmstripScroll"));
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStringProperty(m_scrollArea, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    setStringProperty(m_scrollArea->viewport(),
                      StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);

    m_listContainer = new QWidget();
    m_listContainer->setObjectName(QStringLiteral("filmstripContent"));
    m_listContainer->setSizePolicy(QSizePolicy::Minimum,
                                   QSizePolicy::Fixed);
    setStringProperty(m_listContainer, StyleTokens::Properties::kUiRole,
                      StyleTokens::Properties::kPanel);
    m_listLayout = new QHBoxLayout(m_listContainer);
    m_listLayout->setSizeConstraint(QLayout::SetMinimumSize);
    m_listLayout->setContentsMargins(10, 9, 10, 9);
    m_listLayout->setSpacing(7);
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
    m_bottomLabel->setText(QCoreApplication::translate("Nebulens", "0 张 · 还需 2 张"));
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

void ProjectPanel::addFileCard(const FileItem& item, int index) {
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
                ? QCoreApplication::translate("Nebulens", "当前参考帧")
                : QCoreApplication::translate("Nebulens", "设为参考帧"));
        m_referenceAction->setEnabled(
            m_editingEnabled && !item.isReferenceFrame);
        m_excludeAction->setText(
            item.isExcluded
                ? QCoreApplication::translate("Nebulens", "恢复此帧")
                : QCoreApplication::translate("Nebulens", "排除此帧"));
        m_excludeAction->setEnabled(
            m_editingEnabled && (!item.isReferenceFrame || item.isExcluded));
        m_removeAction->setEnabled(m_editingEnabled);
        m_contextMenu->exec(card->mapToGlobal(pos));
        m_contextMenuIndex = -1;
    });
    const int insertionIndex = index < 0
        ? m_cards.size()
        : std::clamp(index, 0, static_cast<int>(m_cards.size()));
    m_cards.insert(insertionIndex, card);
    m_listLayout->insertWidget(insertionIndex, card);
}

void ProjectPanel::clearFiles() {
    if (!m_editingEnabled) return;

    const bool hadReference = !referenceFramePath().isEmpty();
    for (auto* card : m_cards) {
        card->deleteLater();
    }
    m_cards.clear();
    m_fileItems.clear();
    m_pendingThumbnailPaths.clear();
    if (m_undoTimer) m_undoTimer->stop();
    m_lastRemovedItem.reset();
    m_lastRemovedIndex = -1;
    emit undoAvailabilityChanged(false, QString());
    m_currentIndex = -1;
    showEmptyState();
    updateBottomBar();
    emit filesChanged();
    if (hadReference) emit referenceFrameChanged();
}

void ProjectPanel::removeSelected() {
    if (!m_editingEnabled) return;
    if (m_currentIndex < 0 || m_currentIndex >= m_cards.size()) return;

    removeAt(m_currentIndex);
}

void ProjectPanel::removeAt(int idx) {
    if (!m_editingEnabled || idx < 0 || idx >= m_cards.size()) return;

    // Keep the canvas and list selection in sync after removing the active
    // source. Selecting the nearest surviving frame also invalidates any
    // asynchronous preview result that may arrive for the removed file.
    m_lastRemovedItem = m_fileItems[idx];
    m_lastRemovedIndex = idx;
    const bool removedReference = m_fileItems[idx].isReferenceFrame;
    if (m_undoTimer) m_undoTimer->start();
    emit undoAvailabilityChanged(true, m_fileItems[idx].fileName);

    const int previousCurrent = m_currentIndex;
    const bool removedCurrent = previousCurrent == idx;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);

    if (m_cards.isEmpty()) {
        m_currentIndex = -1;
        showEmptyState();
        emit fileSelected(QString());
    } else if (removedCurrent) {
        m_currentIndex = -1;
        setCurrentIndex(std::min(idx, static_cast<int>(m_cards.size()) - 1));
    } else {
        m_currentIndex = previousCurrent > idx
            ? previousCurrent - 1 : previousCurrent;
        updateAllCardStyles();
    }
    updateBottomBar();
    emit filesChanged();
    if (removedReference) emit referenceFrameChanged();
}

bool ProjectPanel::undoLastRemoval() {
    if (!m_editingEnabled || !m_lastRemovedItem) return false;
    if (m_undoTimer) m_undoTimer->stop();
    const int index = std::clamp(
        m_lastRemovedIndex, 0, static_cast<int>(m_fileItems.size()));
    const FileItem item = *m_lastRemovedItem;
    if (item.isReferenceFrame) {
        for (int i = 0; i < m_fileItems.size(); ++i) {
            if (!m_fileItems[i].isReferenceFrame) continue;
            m_fileItems[i].isReferenceFrame = false;
            updateCard(i);
        }
    }
    m_fileItems.insert(index, item);
    addFileCard(item, index);
    m_lastRemovedItem.reset();
    m_lastRemovedIndex = -1;
    showFileList();
    setCurrentIndex(index);
    updateBottomBar();
    emit undoAvailabilityChanged(false, QString());
    emit filesChanged();
    if (item.isReferenceFrame) emit referenceFrameChanged();
    return true;
}

void ProjectPanel::toggleSelectedExclusion() {
    toggleExclusionAt(m_currentIndex);
}

void ProjectPanel::toggleExclusionAt(int index) {
    if (!m_editingEnabled || index < 0 || index >= m_fileItems.size()) return;
    FileItem& item = m_fileItems[index];
    if (item.isReferenceFrame && !item.isExcluded) return;
    item.isExcluded = !item.isExcluded;
    updateCard(index);
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::setReferenceFrame(const QString& filePath) {
    if (!m_editingEnabled) return;

    const int target = filePath.isEmpty() ? -1 : findIndexByPath(filePath);
    if (target < 0 && !filePath.isEmpty()) return;

    bool changed = false;
    bool inclusionChanged = false;
    for (int i = 0; i < m_fileItems.size(); ++i) {
        FileItem& item = m_fileItems[i];
        const bool isReference = i == target;
        const bool include = isReference && item.isExcluded;
        if (item.isReferenceFrame == isReference && !include) continue;
        item.isReferenceFrame = isReference;
        if (include) {
            item.isExcluded = false;
            inclusionChanged = true;
        }
        changed = true;
        updateCard(i);
    }
    if (!changed) return;
    updateBottomBar();
    // The reference selector needs the restored source in its candidate list first.
    if (inclusionChanged) emit filesChanged();
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

QString ProjectPanel::processingSourceSignature() const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const FileItem& item : m_fileItems) {
        if (item.isExcluded) continue;
        const QFileInfo info(item.filePath);
        QString identity = info.canonicalFilePath();
        if (identity.isEmpty()) identity = info.absoluteFilePath();
        const QByteArray encoded = identity.toUtf8();
        hash.addData(QByteArray::number(encoded.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(encoded);
        // Detect same-path replacements without opening or hashing RAW data.
        const QDateTime modified = info.lastModified();
        hash.addData(QByteArrayView(":", 1));
        hash.addData(QByteArray::number(info.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(modified.isValid()
            ? QByteArray::number(modified.toMSecsSinceEpoch()) : QByteArray("missing"));
        hash.addData(QByteArrayView("\n", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
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
    if (m_currentIndex == index) {
        m_scrollArea->ensureWidgetVisible(m_cards[index]);
        return;
    }

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
        ? QCoreApplication::translate("Nebulens", "已就绪")
        : QCoreApplication::translate("Nebulens", "还需 %1 张").arg(required - available);
    m_bottomLabel->setText(
        QCoreApplication::translate("Nebulens", "%1 张 · %2").arg(available).arg(status));

    const QString reference = referenceName.isEmpty()
        ? QCoreApplication::translate("Nebulens", "自动")
        : referenceName;
    m_bottomLabel->setToolTip(
        QCoreApplication::translate("Nebulens", "共 %1 张，排除 %2 张，参考帧：%3")
            .arg(total)
            .arg(excluded)
            .arg(reference));
    m_bottomLabel->setProperty(
        StyleTokens::Properties::kStatusRole,
        QString::fromLatin1(ready ? StyleTokens::Properties::kSuccess
                                  : StyleTokens::Properties::kWarning));
    refreshStyle(m_bottomLabel);
    if (m_countLabel) {
        m_countLabel->setText(QStringLiteral("%1 / %2")
                                  .arg(available)
                                  .arg(total));
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
    toggleExclusionAt(m_contextMenuIndex);
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
    removeAt(m_contextMenuIndex);
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
