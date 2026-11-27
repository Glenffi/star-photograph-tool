#include "RidgeComparisonDialog.h"
#include "StyleTokens.h"

#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr auto kManifestSchema = "nebulens.ridge-comparison";
constexpr int kManifestVersion = 1;
constexpr qint64 kMaxManifestBytes = 1024 * 1024;
constexpr std::array kCropNames{"ridge-single.png", "ridge-stack.png", "ridge-separated.png"};
constexpr int kColumns = static_cast<int>(kCropNames.size());
constexpr int kMaxSide = 4096;
constexpr qint64 kMaxPixels = 4 * 1024 * 1024;
constexpr qint64 kMaxFileBytes = 40 * 1024 * 1024;
constexpr int kGutter = 8;
constexpr int kCaptionGap = 6;
using Crops = std::array<QImage, kColumns>;

bool isLocalPath(const QString& path) {
    const QString clean = QDir::fromNativeSeparators(path);
    if (clean.isEmpty() || clean.startsWith(QStringLiteral("//")) ||
        clean.startsWith(QStringLiteral(":/"))) return false;
#ifdef Q_OS_WIN
    if (clean.size() >= 3 && clean[0].isLetter() && clean[1] == QLatin1Char(':') &&
        clean[2] == QLatin1Char('/')) return true;
#endif
    return QUrl(clean).scheme().isEmpty();
}

bool validateManifest(const QFileInfo& info, QString& error) {
    const auto unreadable = [&] {
        error = QCoreApplication::translate("Nebulens", "找不到或无法读取本地对照清单。");
        return false;
    };
    QFile file(info.filePath());
    if (info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly)) return unreadable();

    const QByteArray bytes = file.read(kMaxManifestBytes + 1);
    if (file.error() != QFileDevice::NoError) return unreadable();
    if (bytes.size() > kMaxManifestBytes) {
        error = QCoreApplication::translate("Nebulens", "对照清单文件过大。");
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        error = QCoreApplication::translate("Nebulens", "对照清单不是有效的 JSON 对象。");
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QLatin1String(kManifestSchema)) {
        error = QCoreApplication::translate("Nebulens", "无法识别对照清单格式。");
        return false;
    }
    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != kManifestVersion) {
        error = QCoreApplication::translate("Nebulens", "不支持此对照清单版本。");
        return false;
    }
    return true;
}

bool readCrop(const QDir& directory, const QString& name, const QSize& expectedSize,
              QImage& crop, QString& error) {
    const QFileInfo info(directory.filePath(name));
    if (info.isSymLink() || !info.isFile()) {
        error = QCoreApplication::translate("Nebulens", "缺少本地对照图片：%1").arg(name);
        return false;
    }
    const auto unreadable = [&] {
        error = QCoreApplication::translate("Nebulens", "无法读取对照图片：%1").arg(name);
        return false;
    };
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) return unreadable();
    if (file.size() > kMaxFileBytes) {
        error = QCoreApplication::translate("Nebulens", "对照图片文件过大：%1").arg(name);
        return false;
    }

    QImageReader reader(&file, QByteArrayLiteral("png"));
    reader.setAutoDetectImageFormat(false);
    reader.setAutoTransform(false);
    const QSize size = reader.size();
    if (size.isEmpty()) return unreadable();
    if (size.width() > kMaxSide || size.height() > kMaxSide ||
        qint64(size.width()) * size.height() > kMaxPixels) {
        error = QCoreApplication::translate("Nebulens", "对照图片尺寸过大：%1").arg(name);
        return false;
    }
    if (expectedSize.isValid() && size != expectedSize) {
        error = QCoreApplication::translate("Nebulens", "对照图片的裁片尺寸不一致：%1").arg(name);
        return false;
    }

    crop = reader.read();
    if (crop.isNull() || crop.size() != size) return unreadable();
    return true;
}

class ComparisonView final : public QAbstractScrollArea {
public:
    explicit ComparisonView(Crops crops, QWidget* parent)
        : QAbstractScrollArea(parent), m_crops(std::move(crops)), m_cropSize(m_crops.front().size()),
          m_center(m_cropSize.width() / 2.0, m_cropSize.height() / 2.0) {
        setObjectName(QStringLiteral("ridgeComparisonView"));
        setFrameShape(QFrame::NoFrame);
        setFocusPolicy(Qt::StrongFocus);
        constexpr std::array names{"ridgeComparisonSingleTitle", "ridgeComparisonStackTitle",
                                   "ridgeComparisonSeparatedTitle"};
        const std::array captions{
            QCoreApplication::translate("Nebulens", "单张"),
            QCoreApplication::translate("Nebulens", "普通堆栈"),
            QCoreApplication::translate("Nebulens", "天地分离")
        };
        for (int i = 0; i < kColumns; ++i) {
            auto* caption = new QLabel(captions[i], viewport());
            caption->setObjectName(QString::fromLatin1(names[i]));
            caption->setTextFormat(Qt::PlainText);
            caption->setProperty(StyleTokens::Properties::kTextRole,
                                 StyleTokens::Properties::kSection);
            caption->setAlignment(Qt::AlignCenter);
            caption->setWordWrap(true);
            m_captions[i] = caption;
        }
        setMinimumSize(318, 160);
        horizontalScrollBar()->setSingleStep(24);
        verticalScrollBar()->setSingleStep(24);
        layoutPanes();
    }

    void setActualSize(bool actualSize) {
        m_actualSize = actualSize;
        layoutPanes();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QAbstractScrollArea::resizeEvent(event);
        layoutPanes();
    }

    void scrollContentsBy(int dx, int dy) override {
        if (m_actualSize && !m_layingOut) {
            const QSize pane = paneRect(0).size();
            if (dx) m_center.setX(horizontalScrollBar()->value() + pane.width() / 2.0);
            if (dy) m_center.setY(verticalScrollBar()->value() + pane.height() / 2.0);
        }
        viewport()->update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(),
                         StyleTokens::Colors::fromHex(StyleTokens::Colors::kBackgroundBase));
        const QSize pane = paneRect(0).size();
        if (pane.isEmpty()) return;

        QSize imageSize = m_cropSize;
        QPoint offset;
        if (m_actualSize) {
            offset = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
        } else {
            imageSize.scale(pane, Qt::KeepAspectRatio);
            imageSize = imageSize.expandedTo(QSize(1, 1));
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
        }
        // 三栏共用尺寸和像素偏移，不会因单独的滚动范围而失去对齐。
        const QPoint origin(std::max(0, (pane.width() - imageSize.width()) / 2),
                            std::max(0, (pane.height() - imageSize.height()) / 2));
        for (int i = 0; i < kColumns; ++i) {
            const QRect bounds = paneRect(i);
            painter.fillRect(bounds,
                             StyleTokens::Colors::fromHex(StyleTokens::Colors::kBackgroundCanvas));
            painter.save();
            painter.setClipRect(bounds);
            painter.drawImage(QRect(bounds.topLeft() + origin - offset, imageSize), m_crops[i]);
            painter.restore();
        }
    }

private:
    QRect paneRect(int column) const {
        const int spacing = (kColumns - 1) * kGutter;
        const int width = std::max(0, (viewport()->width() - spacing) / kColumns);
        const int inset = std::max(0, (viewport()->width() - kColumns * width - spacing) / 2);
        return {inset + column * (width + kGutter), m_imageTop, width,
                std::max(0, viewport()->height() - m_imageTop)};
    }

    void layoutPanes() {
        const QScopedValueRollback layingOut(m_layingOut, true);
        const int width = paneRect(0).width();
        int captionHeight = 0;
        for (const auto* caption : m_captions) {
            captionHeight = std::max(captionHeight, caption->heightForWidth(width));
        }
        m_imageTop = captionHeight + kCaptionGap;
        for (int i = 0; i < kColumns; ++i) {
            m_captions[i]->setGeometry(paneRect(i).left(), 0, width, captionHeight);
        }
        const QSize pane = paneRect(0).size();
        updateScrollBar(horizontalScrollBar(), pane.width(), m_cropSize.width(), m_center.x());
        updateScrollBar(verticalScrollBar(), pane.height(), m_cropSize.height(), m_center.y());
        viewport()->update();
    }

    void updateScrollBar(QScrollBar* bar, int page, int extent, qreal center) const {
        bar->setPageStep(page);
        bar->setRange(0, m_actualSize ? std::max(0, extent - page) : 0);
        if (m_actualSize) bar->setValue(qRound(center - page / 2.0));
    }

    const Crops m_crops;
    const QSize m_cropSize;
    QPointF m_center;
    std::array<QLabel*, kColumns> m_captions{};
    int m_imageTop = 0;
    bool m_actualSize = false;
    bool m_layingOut = false;
};

}  // namespace

RidgeComparisonDialog::RidgeComparisonDialog(Crops crops, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("ridgeComparisonDialog"));
    setWindowTitle(QCoreApplication::translate("Nebulens", "山脊对照"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    auto* tools = new QHBoxLayout;
    auto* fit = new QRadioButton(QCoreApplication::translate("Nebulens", "适合窗口"), this);
    fit->setObjectName(QStringLiteral("ridgeComparisonFit"));
    fit->setChecked(true);
    auto* actual = new QRadioButton(QCoreApplication::translate("Nebulens", "100%"), this);
    actual->setObjectName(QStringLiteral("ridgeComparisonActualSize"));
    auto* viewModes = new QHBoxLayout;
    viewModes->setSpacing(StyleTokens::Spacing::kMicro);
    for (auto* mode : {fit, actual}) {
        mode->setProperty("viewMode", true);
        mode->setAccessibleName(mode->text());
        mode->setFocusPolicy(Qt::StrongFocus);
        mode->setCursor(Qt::PointingHandCursor);
        viewModes->addWidget(mode);
    }
    tools->addLayout(viewModes);
    tools->addStretch();
    auto* close = new QDialogButtonBox(this);
    auto* back = close->addButton(QCoreApplication::translate("Nebulens", "返回冲印单"),
                                  QDialogButtonBox::RejectRole);
    back->setProperty(StyleTokens::Properties::kVariant, StyleTokens::Properties::kGhost);
    back->setAutoDefault(false);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    tools->addWidget(close);
    layout->addLayout(tools);

    auto* comparison = new ComparisonView(std::move(crops), this);
    comparison->setAccessibleName(windowTitle());
    connect(actual, &QRadioButton::toggled, comparison, &ComparisonView::setActualSize);
    layout->addWidget(comparison, 1);
    fit->setFocus(Qt::OtherFocusReason);

    const QSize preferred(920, 540);
    const QScreen* display = screen();
    resize(display ? preferred.boundedTo(display->availableGeometry().size()) : preferred);
}

bool RidgeComparisonDialog::openForManifest(const QString& path, QWidget* parent, QString& error) {
    error.clear();
    if (!isLocalPath(path)) {
        error = QCoreApplication::translate("Nebulens", "请选择本地对照清单。");
        return false;
    }
    const QFileInfo manifest(path);
    if (!validateManifest(manifest, error)) return false;

    const QDir directory = manifest.absoluteDir();
    Crops crops;
    QSize expectedSize;
    for (int i = 0; i < kColumns; ++i) {
        const auto name = QString::fromLatin1(kCropNames[i]);
        if (!readCrop(directory, name, expectedSize, crops[i], error)) return false;
        if (i == 0) expectedSize = crops[i].size();
    }

    auto* dialog = new RidgeComparisonDialog(std::move(crops), parent);
    dialog->open();
    return true;
}
