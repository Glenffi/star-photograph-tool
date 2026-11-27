#include "PrintOrderPanel.h"
#include "ProductProofSamples.h"
#include "StyleTokens.h"
#include "UiAssets.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QColorSpace>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_MACOS)
#include <dlfcn.h>
#include <objc/objc.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {

enum class Step { Product, Assets, Output, Receipt };
enum class Light { Soft, Flow, Still };

constexpr auto kTiff = "tiff16";
constexpr auto kPng = "png8";
constexpr int kPageInset = 28;
constexpr int kSoftPeriod = 14000;
constexpr int kFlowPeriod = 9000;

bool reduceMotion(const QWidget* widget) {
#if defined(Q_OS_MACOS)
    // Cocoa 已加载 Objective-C 运行时，无需给应用追加链接依赖。
    static const auto lookupClass = reinterpret_cast<Class (*)(const char*)>(
        dlsym(RTLD_DEFAULT, "objc_lookUpClass"));
    static const auto selector = reinterpret_cast<SEL (*)(const char*)>(
        dlsym(RTLD_DEFAULT, "sel_registerName"));
    static const auto responds = reinterpret_cast<BOOL (*)(Class, SEL)>(
        dlsym(RTLD_DEFAULT, "class_respondsToSelector"));
    static void* const message = dlsym(RTLD_DEFAULT, "objc_msgSend");
    if (lookupClass && selector && responds && message) {
        const Class workspace = lookupClass("NSWorkspace");
        const SEL reduced = selector("accessibilityDisplayShouldReduceMotion");
        if (workspace && responds(workspace, reduced)) {
            const auto sendObject = reinterpret_cast<id (*)(id, SEL)>(message);
            const auto sendBool = reinterpret_cast<BOOL (*)(id, SEL)>(message);
            const id shared = sendObject(reinterpret_cast<id>(workspace), selector("sharedWorkspace"));
            if (sendBool(shared, reduced)) return true;
        }
    }
#elif defined(Q_OS_WIN)
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) {
        return true;
    }
#endif
    return widget->style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, widget) == 0;
}

QFont paperFont(QFont font, int size, QFont::Weight weight = QFont::Normal) {
    font.setFamilies({QStringLiteral("Baskerville"), QStringLiteral("Songti SC"),
                      QStringLiteral("Noto Serif CJK SC"), QStringLiteral("Georgia"),
                      QStringLiteral("SimSun")});
    font.setStyleHint(QFont::Serif);
    font.setPixelSize(size);
    font.setWeight(weight);
    return font;
}

QLabel* label(QWidget* parent, const char* name, const char* role = "body") {
    auto* result = new QLabel(parent);
    result->setObjectName(QString::fromLatin1(name));
    result->setProperty("printRole", QString::fromLatin1(role));
    result->setTextFormat(Qt::PlainText);
    return result;
}

QPushButton* command(QWidget* parent, const char* name, const char* role = "secondary") {
    auto* result = new QPushButton(parent);
    result->setObjectName(QString::fromLatin1(name));
    result->setProperty("printRole", QString::fromLatin1(role));
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

void drawDisclosure(QWidget* control) {
    QPainter painter(control);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(control->palette().color(QPalette::ButtonText), 1.3,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal x = control->layoutDirection() == Qt::RightToLeft ? 11 : control->width() - 11;
    const qreal y = control->height() / 2.0 - 1;
    painter.drawPolyline(QPolygonF{{x - 3.5, y}, {x, y + 3}, {x + 3.5, y}});
}

class DocumentMenuButton final : public QPushButton {
public:
    using QPushButton::QPushButton;
    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(text()) + 37, QPushButton::sizeHint().height());
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text.clear();
        option.features &= ~QStyleOptionButton::HasMenu;
        {
            QStylePainter painter(this);
            painter.drawControl(QStyle::CE_PushButton, option);
            const bool rtl = layoutDirection() == Qt::RightToLeft;
            const QRect title = rect().adjusted(rtl ? 24 : 5, 0, rtl ? -5 : -24, 0);
            painter.setPen(palette().color(QPalette::ButtonText));
            painter.drawText(title, Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine,
                             fontMetrics().elidedText(text(), Qt::ElideRight, title.width()));
        }
        drawDisclosure(this);
    }
};

class PaperCombo final : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        drawDisclosure(this);
    }
};

QComboBox* choices(QWidget* parent, const char* name) {
    auto* result = new PaperCombo(parent);
    result->setObjectName(QString::fromLatin1(name));
    return result;
}

class PathField final : public QLineEdit {
public:
    explicit PathField(QWidget* parent) : QLineEdit(parent) { setReadOnly(true); }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (hasFocus() || text().isEmpty()) {
            QLineEdit::paintEvent(event);
            return;
        }
        QStyleOptionFrame frame;
        initStyleOption(&frame);
        QStylePainter painter(this);
        painter.drawPrimitive(QStyle::PE_PanelLineEdit, frame);
        const QRect contents = style()->subElementRect(QStyle::SE_LineEditContents, &frame, this)
            .marginsRemoved(textMargins()).adjusted(1, 0, -1, 0);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(contents, Qt::AlignVCenter | Qt::TextSingleLine,
                         fontMetrics().elidedText(text(), Qt::ElideMiddle, contents.width()));
    }
};

QLineEdit* pathField(QWidget* parent, const char* name) {
    auto* result = new PathField(parent);
    result->setObjectName(QString::fromLatin1(name));
    result->setMinimumWidth(0);
    return result;
}

void syncPath(QLineEdit* field, const QString& path) {
    const QString shown = QDir::toNativeSeparators(path);
    if (field->text() == shown) return;
    const QSignalBlocker blocker(field);
    field->setText(shown);
    field->setToolTip(path.toHtmlEscaped());
}

class ElidedLabel final : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextFormat(Qt::PlainText);
    }

    void setValue(const QString& value) {
        if (value == text()) return;
        setText(value);
        setToolTip(value.toHtmlEscaped());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect area = contentsRect();
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(area, alignment() | Qt::TextSingleLine,
                         fontMetrics().elidedText(text().simplified(), Qt::ElideMiddle,
                                                  area.width()));
    }
};

class PaperSurface final : public QWidget {
public:
    explicit PaperSurface(QWidget* parent)
        : QWidget(parent), m_grain(QStringLiteral(":/print-order/vellum.png")) {
        setObjectName(QStringLiteral("printOrderPaper"));
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setLight(qreal strength, qreal travel) {
        if (m_strength == strength && m_travel == travel) return;
        m_strength = strength;
        m_travel = travel;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(StyleTokens::Colors::kBackgroundBase));
        if (!m_grain.isNull()) {
            const qreal dpr = devicePixelRatioF();
            const QSize pixels = size() * dpr;
            if (m_scaledGrain.size() != pixels || m_scaledGrain.devicePixelRatio() != dpr) {
                m_scaledGrain = m_grain.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                m_scaledGrain.setDevicePixelRatio(dpr);
            }
            painter.setOpacity(0.80);
            painter.drawPixmap(rect(), m_scaledGrain);
        }

        const qreal offset = width() * m_travel;
        QLinearGradient rake(-0.21 * width() + offset, 0.72 * height(),
                             1.21 * width() + offset, 0.28 * height());
        rake.setColorAt(0.05, QColor(107, 85, 50, 0));
        rake.setColorAt(0.24, QColor(91, 78, 54, 44));
        rake.setColorAt(0.35, QColor(113, 101, 70, 8));
        rake.setColorAt(0.48, QColor(255, 251, 226, 160));
        rake.setColorAt(0.59, QColor(255, 251, 234, 37));
        rake.setColorAt(0.71, QColor(102, 91, 64, 14));
        rake.setColorAt(0.86, QColor(102, 91, 64, 0));
        painter.setOpacity(m_strength);
        painter.fillRect(rect(), rake);
    }

private:
    QPixmap m_grain;
    QPixmap m_scaledGrain;
    qreal m_strength = 0.73;
    qreal m_travel = 0.0;
};

class Perforation final : public QWidget {
public:
    explicit Perforation(QWidget* parent) : QWidget(parent) { setFixedHeight(2); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QPen pen(QColor("#8b8168"), 1, Qt::CustomDashLine);
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({4, 3});
        painter.setPen(pen);
        painter.drawLine(0, 0, width(), 0);
        pen.setColor(QColor("#fff6df"));
        painter.setPen(pen);
        painter.drawLine(0, 1, width(), 1);
    }
};

class ProductChoice final : public QRadioButton {
public:
    explicit ProductChoice(QWidget* parent) : QRadioButton(parent) {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(57);
        m_check.setDuration(150);
        m_check.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_check, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_ink = value.toReal();
            update();
        });
        connect(this, &QRadioButton::toggled, this, [this](bool checked) {
            if (!m_animate || !isVisible()) {
                syncCheck();
                return;
            }
            m_check.stop();
            m_check.setStartValue(m_ink);
            m_check.setEndValue(checked ? 1.0 : 0.0);
            m_check.start();
        });
    }

    void setCopy(const QString& title, const QString& detail) {
        setText(title);
        m_detail = detail;
        setAccessibleDescription(detail);
        setToolTip(detail.toHtmlEscaped());
        update();
    }

    void setAnimate(bool animate) {
        if (m_animate == animate) return;
        m_animate = animate;
        if (!animate) syncCheck();
    }

    void syncCheck() {
        m_check.stop();
        m_ink = isChecked() ? 1 : 0;
        update();
    }

protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (isChecked() || (isEnabled() && underMouse())) {
            const int alpha = isDown() ? 30 : (isChecked() ? 15 : 11);
            painter.fillRect(rect().adjusted(0, 1, 0, 0), QColor(45, 72, 62, alpha));
        }
        painter.setPen(QColor(100, 93, 73, 104));
        painter.drawLine(0, 0, width(), 0);
        if (hasFocus()) {
            painter.setPen(QPen(QColor("#355a4e"), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect().adjusted(1, 2, -2, -2), 3, 3);
        }

        const QPointF center(13, height() / 2.0);
        const QColor green(isEnabled() ? "#2d4c40" : "#716e60");
        painter.setPen(QPen(isChecked() ? green : QColor("#777163"), 1.15));
        painter.setBrush(QColor(252, 248, 233, 85));
        painter.drawEllipse(center, 7.5, 7.5);
        if (m_ink > 0) {
            painter.setOpacity(m_ink);
            painter.setPen(Qt::NoPen);
            painter.setBrush(green);
            painter.drawEllipse(center, 4.25 * m_ink, 4.25 * m_ink);
            painter.setOpacity(1.0);
        }

        QFont type = font();
        type.setPixelSize(15);
        type.setWeight(QFont::DemiBold);
        painter.setFont(type);
        painter.setPen(isChecked() ? green : QColor(isEnabled() ? "#303129" : "#716e60"));
        const int textWidth = std::max(0, width() - 43);
        painter.drawText(QRect(32, 7, textWidth, 19), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, textWidth));

        type.setPixelSize(12);
        type.setWeight(QFont::Normal);
        painter.setFont(type);
        painter.setPen(QColor(isEnabled() ? "#4d4d40" : "#716e60"));
        painter.drawText(QRect(32, 27, textWidth, 28),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_detail);
    }

private:
    QVariantAnimation m_check;
    QString m_detail;
    qreal m_ink = 0.0;
    bool m_animate = false;
};

class ProofPrint final : public QAbstractButton {
public:
    explicit ProofPrint(QWidget* parent) : QAbstractButton(parent) {
        setObjectName(QStringLiteral("printOrderProof"));
        setMinimumSize(220, 146);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
    }

    void setImage(const QImage& image) {
        if (m_image.cacheKey() == image.cacheKey()) return;
        m_image = image;
        m_scaled = QPixmap();
        update();
    }

    void setCopy(const QString& caption, const QString& credit, const QString& open) {
        if (m_caption == caption && m_credit == credit && m_open == open) return;
        m_caption = caption;
        m_credit = credit;
        m_open = open;
        setAccessibleName(open.isEmpty() ? caption : open);
        setAccessibleDescription(open.isEmpty() ? credit : caption + QStringLiteral("\n") + credit);
        setToolTip(caption.toHtmlEscaped());
        update();
    }

    const QString& caption() const { return m_caption; }

    void setLight(qreal strength, qreal travel) {
        if (m_strength == strength && m_travel == travel) return;
        m_strength = strength;
        m_travel = travel;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect space = rect().adjusted(3, 1, -8, -51);
        const QSize aspect = m_image.isNull() ? QSize(3, 2) : m_image.size();
        QRect mount(QPoint(), aspect.scaled(space.size().expandedTo(QSize(1,1)), Qt::KeepAspectRatio));
        mount.moveCenter(space.center());
        if (m_image.isNull()) {
            // 没有自己的图像时留下空相片位，而不是一块像加载失败的黑色画布。
            painter.setPen(QPen(QColor(124, 113, 88, 135), 1));
            const QRect box = mount.adjusted(5, 5, -5, -5);
            for (const auto corner : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()}) {
                painter.drawLine(corner, corner + QPoint(corner.x() == box.left() ? 15 : -15, 0));
                painter.drawLine(corner, corner + QPoint(0, corner.y() == box.top() ? 15 : -15));
            }
            painter.setPen(QColor(StyleTokens::Colors::kTextSecondary));
            painter.setFont(paperFont(font(), 17));
            painter.drawText(box.adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap, m_caption);
            return;
        }
        const int dx = qRound(1.0 + m_travel * 19.0);
        for (int spread = 5; spread >= 0; --spread) {
            const QRect shadow = mount.translated(dx, 4)
                .adjusted(-spread, -spread, spread, spread);
            painter.fillRect(shadow, QColor(62, 50, 34, qRound(m_strength * 25)));
        }

        painter.fillRect(mount, QColor("#f3f2ec"));
        const qreal lightX = mount.x() + mount.width() * m_travel * 2.3;
        QLinearGradient rim(lightX, mount.bottom(), lightX + mount.width(), mount.top());
        rim.setColorAt(0, QColor(218, 219, 208, 40));
        rim.setColorAt(0.53, QColor(255, 255, 255, qRound(130.0 * m_strength)));
        rim.setColorAt(1, QColor(220, 222, 216, 35));
        painter.fillRect(mount, rim);
        painter.setPen(QColor(255, 255, 255, 172));
        painter.drawLine(mount.topLeft(), mount.topRight());

        const QRect opening = mount.adjusted(7, 7, -7, -10);
        painter.fillRect(opening, QColor("#191d1c"));
        if (!m_image.isNull()) {
            // 光影只画底纸、阴影和卡纸，不进入照片的绘制区域。
            QRect fit(QPoint(), m_image.size().scaled(opening.size(), Qt::KeepAspectRatio));
            fit.moveCenter(opening.center());
            const qreal dpr = devicePixelRatioF();
            const QSize pixels = fit.size() * dpr;
            if (m_scaled.isNull() || m_scaled.size() != pixels || m_scaled.devicePixelRatio() != dpr) {
                m_scaled = QPixmap::fromImage(m_image.scaled(pixels, Qt::IgnoreAspectRatio,
                                                              Qt::SmoothTransformation));
                m_scaled.setDevicePixelRatio(dpr);
            }
            painter.drawPixmap(fit, m_scaled);
        }
        if (hasFocus() || (isEnabled() && underMouse())) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor("#355a4e"), hasFocus() ? 2 : 1));
            painter.drawRect(mount.adjusted(-1, -1, 0, 0));
        }

        QFont face = font();
        face.setPixelSize(12);
        face.setUnderline(true);
        painter.setFont(face);
        const int openWidth = painter.fontMetrics().horizontalAdvance(m_open);
        const QRect caption(mount.x(), mount.bottom() + 9, mount.width(), 22);
        painter.setPen(QColor("#355347"));
        painter.drawText(caption, Qt::AlignRight | Qt::AlignVCenter, m_open);
        painter.setFont(paperFont(font(), 17, QFont::DemiBold));
        painter.setPen(QColor("#30362c"));
        const int room = std::max(0, caption.width() - (openWidth ? openWidth + 12 : 0));
        painter.drawText(caption, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_caption, Qt::ElideMiddle, room));

        face.setUnderline(false);
        painter.setFont(face);
        painter.setPen(QColor("#4e4e41"));
        painter.drawText(caption.translated(0, 19), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_credit, Qt::ElideRight, caption.width()));
    }

private:
    QImage m_image;
    QPixmap m_scaled;
    QString m_caption;
    QString m_credit;
    QString m_open;
    qreal m_strength = 0.73;
    qreal m_travel = 0.0;
};

class ReceiptStamp final : public QWidget {
public:
    explicit ReceiptStamp(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("printOrderReceiptStamp"));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setText(const QString& text) {
        m_text = text;
        setAccessibleName(text);
        updateGeometry();
        update();
    }

    void setInk(qreal ink) {
        m_ink = ink;
        update();
    }

    QSize sizeHint() const override {
        return {QFontMetrics(paperFont(font(), 15, QFont::DemiBold)).horizontalAdvance(m_text) + 29, 38};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(paperFont(font(), 15, QFont::DemiBold));
        painter.translate(rect().center());
        painter.rotate(-5.0 - 5.0 * (1.0 - m_ink));
        painter.scale(0.87 + 0.13 * m_ink, 0.87 + 0.13 * m_ink);
        painter.setOpacity(m_ink);
        const QRectF border(-width() / 2.0 + 5, -12, width() - 10, 24);
        painter.setPen(QPen(QColor("#854436"), 1.2));
        painter.drawRoundedRect(border, 1.5, 1.5);
        painter.drawRoundedRect(border.adjusted(2.5, 2.5, -2.5, -2.5), 1, 1);
        painter.drawText(border, Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    qreal m_ink = 1.0;
};

constexpr auto kStyle = R"qss(
#printOrderPanel QWidget { background-color: transparent; }
#printOrderPanel QLabel { color: @ink@; background: transparent; }
#paperMasthead QLabel#printOrderBrand {
    color: #294337; font-family: Baskerville, Georgia, "DejaVu Serif";
    font-size: 25px; font-style: italic; font-weight: 500;
}
#printOrderPanel QLabel[printRole="muted"] { color: #505044; font-size: 12px; }
#printOrderPanel QLabel[printRole="warning"] { color: #753e30; font-size: 12px; }
#printOrderPanel QLabel[printRole="heading"], #printOrderPanel QLabel#printOrderSummary {
    color: #252e27; font-weight: 600;
    font-family: Baskerville, "Songti SC", "Noto Serif CJK SC", Georgia, SimSun, "DejaVu Serif";
}
#printOrderPanel QLabel[printRole="heading"] { font-size: 27px; }
#printOrderPanel QLabel#printOrderSummary { font-size: 20px; }
#printOrderPanel QFrame[printRole="rule"] { border: none; background-color: @rule@; }
#printOrderPanel QStackedWidget { border: none; background: transparent; }
#printOrderPanel QRadioButton { min-height: 57px; max-height: 57px; }
#printOrderPanel QPushButton {
    color: @accent@; background: @stock@; border: 1px solid @rule@;
    border-radius: 3px; font-size: 13px; padding: 0 9px;
    min-height: 28px; max-height: 28px;
}
#printOrderPanel QPushButton:hover { background: #f9f4e4; border-color: #577164; }
#printOrderPanel QPushButton:pressed { background: #e0decb; }
#printOrderPanel QPushButton:focus { border-color: #315b4b; background: #fff9e8; }
#printOrderPanel QPushButton:disabled { background: #e2dcc9; color: #6e6c60; border-color: #ada58e; }
#printOrderPanel QPushButton[printRole="link"],
#printOrderPanel QPushButton[printRole="stage"] {
    background: transparent; border: 1px solid transparent; padding: 0 7px;
}
#printOrderPanel QPushButton[printRole="link"]:hover,
#printOrderPanel QPushButton[printRole="stage"]:hover { background: rgba(45, 72, 62, 20); }
#printOrderPanel QPushButton[printRole="link"]:focus,
#printOrderPanel QPushButton[printRole="stage"]:focus { border-color: #355a4e; }
#printOrderPanel QPushButton#printOrderFileButton {
    padding-left: 4px; padding-right: 24px; font-family: Baskerville, "Songti SC", Georgia, SimSun, serif;
    font-weight: 600; font-size: 19px; color: @ink@;
}
#printOrderPanel QPushButton#printOrderFileButton::menu-indicator {
    image: none; width: 0px;
}
#printOrderPanel QPushButton#printOrderQuickToggle:checked { color: @accent@; border-bottom: 1px solid @accent@; }
#printOrderPanel QPushButton#paperApplicationMenu { padding: 0; min-width: 28px; max-width: 28px; }
#printOrderPanel QPushButton#paperApplicationMenu::menu-indicator { image: none; width: 0; }
#printOrderPanel QMenu#printOrderFileMenu {
    color: #253d33; background: #f9f4e7; border: 1px solid #938b75; padding: 4px;
}
#printOrderPanel QMenu#printOrderFileMenu::item { padding: 5px 16px; }
#printOrderPanel QMenu#printOrderFileMenu::item:selected { background: #dce1d2; }
#printOrderPanel QMenu#printOrderFileMenu::item:disabled { color: #6e6c60; }
#printOrderPanel QPushButton[printRole="stage"] {
    color: #534f40; border-radius: 0; border-bottom: 2px solid transparent;
}
#printOrderPanel QPushButton[printRole="stage"]:disabled { color: #726c5b; }
#printOrderPanel QPushButton[printRole="stage"]:checked {
    border-bottom: 2px solid #2d5143; color: #213e31;
}
#printOrderPanel QPushButton[printRole="primary"] {
    color: #fffcef; background: @accent@; border: 2px solid @accent@;
    padding: 0 15px; font-size: 14px; font-weight: 600;
    min-width: 96px; min-height: 36px; max-height: 36px;
}
#printOrderPanel QPushButton[printRole="primary"]:hover { background: #233d33; }
#printOrderPanel QPushButton[printRole="primary"]:pressed { background: #193126; }
#printOrderPanel QPushButton[printRole="primary"]:focus { border-color: #8fae97; }
#printOrderPanel QPushButton[printRole="primary"]:disabled {
    color: #646255; background: #ded7c4; border-color: #a9a089;
}
#printOrderPanel QComboBox, #printOrderPanel QLineEdit {
    color: @ink@; background: @stock@; border: 1px solid @rule@;
    border-radius: 3px; padding: 0 8px; font-size: 13px;
    min-height: 28px; max-height: 28px;
}
#printOrderPanel QComboBox { padding-right: 20px; }
#printOrderPanel QComboBox::drop-down { width: 20px; border: none; }
#printOrderPanel QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
#printOrderPanel QComboBox:focus, #printOrderPanel QLineEdit:focus { border-color: #315b4b; }
#printOrderPanel QComboBox:disabled, #printOrderPanel QLineEdit:disabled {
    color: #67675a; background: #e3dfcf; border-color: #a59c86;
}
#printOrderPanel QComboBox QAbstractItemView {
    color: #30382d; background: #f9f4e7; border: 1px solid #938b75;
    selection-color: #223b31; selection-background-color: #dce1d2;
}
#printOrderPanel QToolButton[frameThumbnail="true"] {
    padding: 0; margin: 0; border: 1px solid #a89b80; background: #f3eddd;
    color: #3b493e; font-size: 10px;
}
#printOrderPanel QToolButton[frameThumbnail="true"]:checked {
    border: 2px solid #315747; background: #eff1e2;
}
#printOrderPanel QToolButton[frameThumbnail="true"]:focus { border: 2px solid #936e37; }
#printOrderPanel QDoubleSpinBox { color: #30382d; background: #f3edde; border: 1px solid #938b75; }
#printOrderPanel QProgressBar {
    color: transparent; background: #d0c6ae; border: 0; min-height: 5px; max-height: 5px;
}
#printOrderPanel QProgressBar::chunk { background: #365649; }
)qss";

const QString& orderStyleSheet() {
    static const QString style = [] {
        QString sheet = QString::fromLatin1(kStyle);
        sheet.replace(QLatin1String("@ink@"), QLatin1String(StyleTokens::Colors::kTextPrimary));
        sheet.replace(QLatin1String("@stock@"), QLatin1String(StyleTokens::Colors::kBackgroundRaised));
        sheet.replace(QLatin1String("@rule@"), QLatin1String(StyleTokens::Colors::kLineSubtle));
        sheet.replace(QLatin1String("@accent@"), QLatin1String(StyleTokens::Colors::kAccent));
        return sheet;
    }();
    return style;
}

}  // namespace

struct PrintOrderPanel::Ui : QObject {
    explicit Ui(PrintOrderPanel* panel)
        : QObject(panel), owner(panel), ambientTimer(this), preferenceTimer(this) {
        build();
        connectControls();
        leftFade = new QGraphicsOpacityEffect(pages);
        leftFade->setOpacity(1);
        leftFade->setEnabled(false);
        pages->setGraphicsEffect(leftFade);
        transition.setDuration(300);
        transition.setStartValue(0.0);
        transition.setEndValue(1.0);
        transition.setEasingCurve(QEasingCurve::OutCubic);
        connect(&transition, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal ink = value.toReal();
            leftFade->setOpacity(std::min(1.0, 0.56 + 0.52 * ink));
            stamp->setInk(ink);
        });
        connect(&transition, &QVariantAnimation::finished, this, &Ui::finishTransition);
        retranslate();

        ambientTimer.setObjectName(QStringLiteral("printOrderAmbientTimer"));
        ambientTimer.setInterval(33);
        connect(&ambientTimer, &QTimer::timeout, this, &Ui::paintLight);
        preferenceTimer.setObjectName(QStringLiteral("printOrderMotionPreferenceTimer"));
        preferenceTimer.setInterval(800);
        connect(&preferenceTimer, &QTimer::timeout, this, &Ui::updateMotion);

        owner->installEventFilter(this);
        watchWindow();
        connect(qApp, &QGuiApplication::applicationStateChanged, this, &Ui::updateMotion);
        connect(qApp, &QGuiApplication::focusWindowChanged, this, &Ui::updateMotion);
    }

    struct Imports {
        QWidget* row;
        QPushButton* files;
        QPushButton* folder;
    };

    PrintOrderPanel* const owner;
    PaperSurface* paper = nullptr;
    QWidget* header = nullptr;
    QStackedWidget* faces = nullptr;
    QWidget* form = nullptr;
    QWidget* detail = nullptr;
    std::optional<WorkflowStage> lastDetailStage;
    QPointer<QWidget> returnFocus;
    ProofPrint* proof = nullptr;
    QStackedWidget* pages = nullptr;
    QButtonGroup* stageGroup = nullptr;
    QButtonGroup* productGroup = nullptr;
    std::array<QPushButton*, 3> stageButtons{};
    std::array<ProductChoice*, 3> productButtons{};
    QLabel* lightLabel = nullptr;
    QComboBox* lightField = nullptr;
    QLabel* detailContext = nullptr;
    QPushButton* orderFiles = nullptr;
    QAction* openOrder = nullptr;
    QAction* saveOrder = nullptr;
    QPushButton* applicationMenus = nullptr;
    QMenu* applicationMenu = nullptr;
    QLabel* productTitle = nullptr;
    QLabel* assetsTitle = nullptr;
    QLabel* outputTitle = nullptr;
    QLabel* receiptTitle = nullptr;
    QLabel* sourceLabel = nullptr;
    ElidedLabel* sourceValue = nullptr;
    QLabel* inputCountLabel = nullptr;
    QLabel* inputCountValue = nullptr;
    QWidget* subjectRow = nullptr;
    QLabel* subjectLabel = nullptr;
    QComboBox* subject = nullptr;
    QLabel* assetNote = nullptr;
    QLabel* calibrationNote = nullptr;
    Imports assetImports{};
    QPushButton* manageAssets = nullptr;
    QPushButton* calibration = nullptr;
    QPushButton* ground = nullptr;
    QLabel* formatLabel = nullptr;
    QComboBox* formatField = nullptr;
    QLabel* directoryLabel = nullptr;
    QLineEdit* directoryField = nullptr;
    QPushButton* quickToggle = nullptr;
    QWidget* exposureRow = nullptr;
    QSlider* exposureSlider = nullptr;
    QDoubleSpinBox* exposureSpin = nullptr;
    QPushButton* chooseDirectory = nullptr;
    QLabel* outputNote = nullptr;
    Imports outputImports{};
    QPushButton* outputCalibration = nullptr;
    ElidedLabel* taskNote = nullptr;
    QProgressBar* taskProgress = nullptr;
    ReceiptStamp* stamp = nullptr;
    QLabel* resultCountLabel = nullptr;
    QLabel* resultCountValue = nullptr;
    ElidedLabel* frameSummary = nullptr;
    QPushButton* frameDetails = nullptr;
    QLabel* resultPathLabel = nullptr;
    QLineEdit* resultPath = nullptr;
    QLabel* summaryLabel = nullptr;
    ElidedLabel* summaryValue = nullptr;
    QPushButton* ridgeComparison = nullptr;
    QPushButton* advanced = nullptr;
    QPushButton* back = nullptr;
    QPushButton* next = nullptr;
    QPushButton* newOrder = nullptr;
    QPushButton* reveal = nullptr;

    Step step = Step::Product;
    ProcessingScene scene = ProcessingScene::Nightscape;
    int included = 0;
    int total = 0;
    QString sourceName;
    bool calibrationReady = false;
    QString calibrationHint;
    QString format = QString::fromLatin1(kTiff);
    QString directory;
    QImage sampleImage;
    int sampleIndex = 0;
    bool productSampleChosen = false;
    QWidget* sampleTools = nullptr;
    QWidget* frameStrip = nullptr;
    QList<QToolButton*> frameButtons;
    QPushButton* sampleToggle = nullptr;
    QImage proofImage;
    QString proofCaption;
    bool proofIsSource = false;
    int evTenths = 0;
    qint64 draftKey = 0;
    int draftEv = 0;
    QImage draftBase;
    QImage draftImage;
    bool ridgeComparisonAvailable = false;
    bool busy = false;
    bool cancelPending = false;
    bool retry = false;
    int progress = -1;
    QString status;
    QString receiptPath;
    int receiptFrames = 0;
    struct FrameCounts {
        int submitted;
        int used;
        int skipped;
        int manual;
    };
    std::optional<FrameCounts> frameCounts;

    Light selectedLight = Light::Soft;
    bool systemReduced = false;
    bool motionAllowed = false;
    bool inspectingProof = false;
    QPointer<QWidget> watchedWindow;
    QTimer ambientTimer;
    QTimer preferenceTimer;
    QElapsedTimer elapsed;
    qint64 lightOffset = 2600;
    QGraphicsOpacityEffect* leftFade = nullptr;
    QVariantAnimation transition;

    static QFrame* rule(QWidget* parent, Qt::Orientation orientation) {
        auto* line = new QFrame(parent);
        line->setProperty("printRole", QStringLiteral("rule"));
        if (orientation == Qt::Horizontal) line->setFixedHeight(1);
        else line->setFixedSize(1, 15);
        return line;
    }

    static QLabel* note(QWidget* parent, const char* name, const char* role = "muted") {
        auto* result = label(parent, name, role);
        result->setWordWrap(true);
        return result;
    }

    static void pair(QVBoxLayout* layout, QLabel* key, QWidget* value) {
        auto* row = new QHBoxLayout;
        row->setSpacing(12);
        row->addWidget(key);
        row->addWidget(value, 1);
        layout->addLayout(row);
    }

    QVBoxLayout* page(const char* name, const char* titleName, QLabel*& heading) {
        auto* widget = new QWidget(pages);
        widget->setObjectName(QString::fromLatin1(name));
        auto* content = new QVBoxLayout(widget);
        content->setContentsMargins(0, 0, 0, 0);
        content->setSpacing(7);
        heading = label(widget, titleName, "heading");
        pages->addWidget(widget);
        content->addStretch();
        return content;
    }

    Imports imports(QWidget* parent, const char* filesName, const char* folderName,
                    QPushButton** manage = nullptr) {
        auto* row = new QWidget(parent);
        auto* actions = new QHBoxLayout(row);
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(6);
        auto* files = command(row, filesName);
        auto* folder = command(row, folderName);
        actions->addWidget(files);
        actions->addWidget(folder);
        if (manage) {
            *manage = command(row, "printOrderManageAssets", "link");
            actions->addWidget(*manage);
        }
        actions->addStretch();
        return {row, files, folder};
    }

    void build() {
        owner->setObjectName(QStringLiteral("printOrderPanel"));
        owner->setAttribute(Qt::WA_StyledBackground);
        owner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* outer = new QVBoxLayout(owner);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        paper = new PaperSurface(owner);
        outer->addWidget(paper, 1);
        auto* leaf = new QVBoxLayout(paper);
        leaf->setContentsMargins(0, 0, 0, 0);
        leaf->setSpacing(0);
        header = new QWidget(paper);
        header->setObjectName(QStringLiteral("paperMasthead"));
        header->setAttribute(Qt::WA_StyledBackground);
        header->setMinimumHeight(56);
        auto* rail = new QHBoxLayout(header);
        rail->setContentsMargins(kPageInset, 0, kPageInset, 0);
        rail->setSpacing(8);
        auto* brand = label(header, "printOrderBrand");
        brand->setText(QStringLiteral("Nebulens"));
        lightLabel = label(header, "printOrderLightLabel");
        lightLabel->setVisible(owner->width() >= 1080);
        lightField = choices(header, "printOrderLight");
        lightLabel->setBuddy(lightField);
        rail->addWidget(brand);
        rail->addWidget(rule(header, Qt::Vertical));
        orderFiles = new DocumentMenuButton(header);
        orderFiles->setObjectName(QStringLiteral("printOrderFileButton"));
        orderFiles->setProperty("printRole", QStringLiteral("link"));
        orderFiles->setCursor(Qt::PointingHandCursor);
        auto* orderMenu = new QMenu(orderFiles);
        orderMenu->setObjectName(QStringLiteral("printOrderFileMenu"));
        openOrder = new QAction(orderMenu);
        openOrder->setObjectName(QStringLiteral("printOrderOpen"));
        saveOrder = new QAction(orderMenu);
        saveOrder->setObjectName(QStringLiteral("printOrderSave"));
        orderMenu->addAction(openOrder);
        orderMenu->addAction(saveOrder);
        orderFiles->setMenu(orderMenu);
        rail->addWidget(orderFiles);
        rail->addStretch(1);
        stageGroup = new QButtonGroup(this);
        constexpr std::array stageNames{
            "printOrderStepProduct", "printOrderStepAssets", "printOrderStepOutput"
        };
        for (int i = 0; i < static_cast<int>(stageButtons.size()); ++i) {
            auto* stage = command(header, stageNames[i], "stage");
            stage->setCheckable(true);
            stageGroup->addButton(stage, i);
            stageButtons[i] = stage;
            rail->addWidget(stage);
        }
        detailContext = label(header, "paperDetailContext", "muted");
        rail->addWidget(detailContext);
        detailContext->hide();
        rail->addSpacing(10);
        rail->addWidget(lightLabel);
        rail->addWidget(lightField);
        applicationMenus = command(header, "paperApplicationMenu", "link");
        applicationMenus->setIcon(UiAssets::icon(UiAssets::Glyph::More, QColor("#304e42")));
        applicationMenu = new QMenu(applicationMenus);
        applicationMenus->setMenu(applicationMenu);
        applicationMenus->hide();
        rail->addWidget(applicationMenus);
        leaf->addWidget(header);
        auto* line = rule(paper, Qt::Horizontal);
        line->setStyleSheet(QStringLiteral("background:rgba(121,109,82,105); border:0;"));
        leaf->addWidget(line);

        faces = new QStackedWidget(paper);
        faces->setObjectName(QStringLiteral("printOrderFaces"));
        faces->setFrameShape(QFrame::NoFrame);
        faces->setStyleSheet(QStringLiteral("QStackedWidget#printOrderFaces { background:transparent; border:0; }"));
        leaf->addWidget(faces, 1);
        form = new QWidget(faces);
        form->setObjectName(QStringLiteral("printOrderForm"));
        faces->addWidget(form);
        auto* sheet = new QVBoxLayout(form);
        sheet->setContentsMargins(0, 0, 0, 0);
        sheet->setSpacing(0);

        auto* spread = new QHBoxLayout;
        spread->setContentsMargins(kPageInset, 8, kPageInset, 7);
        spread->setSpacing(30);
        pages = new QStackedWidget(form);
        pages->setObjectName(QStringLiteral("printOrderPages"));
        pages->setFrameStyle(QFrame::NoFrame);
        buildProduct();
        buildAssets();
        buildOutput();
        buildReceipt();
        spread->addWidget(pages, 5);
        auto* proofColumn = new QVBoxLayout;
        proofColumn->setContentsMargins(0, 0, 0, 0);
        proofColumn->setSpacing(0);
        proof = new ProofPrint(form);
        proofColumn->addWidget(proof, 1);
        buildSampleTools(proofColumn);
        spread->addLayout(proofColumn, 7);
        sheet->addLayout(spread, 1);
        sheet->addWidget(new Perforation(form));

        auto* stub = new QHBoxLayout;
        stub->setContentsMargins(kPageInset, 8, kPageInset, 9);
        stub->setSpacing(9);
        auto* summary = new QVBoxLayout;
        summary->setSpacing(0);
        summaryLabel = label(form, "printOrderSummaryLabel", "muted");
        summaryValue = new ElidedLabel(form);
        summaryValue->setObjectName(QStringLiteral("printOrderSummary"));
        summary->addWidget(summaryLabel);
        summary->addWidget(summaryValue);
        stub->addLayout(summary, 1);
        ridgeComparison = command(form, "printOrderRidgeComparison");
        advanced = command(form, "printOrderAdvanced", "link");
        back = command(form, "printOrderBack", "link");
        next = command(form, "printOrderNext", "primary");
        newOrder = command(form, "printOrderNewOrder", "link");
        reveal = command(form, "printOrderReveal", "primary");
        stub->addWidget(ridgeComparison);
        stub->addWidget(advanced);
        stub->addWidget(back);
        stub->addWidget(next);
        stub->addWidget(newOrder);
        stub->addWidget(reveal);
        sheet->addLayout(stub);
        // 局部版式只属于单据正面与页首。附页沿用应用控件样式，不继承首页按钮的限高。
        form->setStyleSheet(orderStyleSheet());
        header->setStyleSheet(orderStyleSheet());
        loadSample(0);
    }

    void buildSampleTools(QVBoxLayout* column) {
        sampleTools = new QWidget(form);
        sampleTools->setFixedHeight(44);
        auto* row = new QHBoxLayout(sampleTools);
        row->setContentsMargins(1, 1, 1, 1);
        row->setSpacing(4);
        frameStrip = new QWidget(sampleTools);
        frameStrip->setObjectName(QStringLiteral("printOrderSampleStrip"));
        auto* strip = new QHBoxLayout(frameStrip);
        strip->setContentsMargins(0,0,0,0);
        strip->setSpacing(4);
        const int frames = ProductProofSamples::count(ProcessingScene::Timelapse);
        for (int i = 0; i < frames; ++i) {
            auto* button = new QToolButton(frameStrip);
            button->setObjectName(QStringLiteral("printOrderSampleFrame%1").arg(i+1));
            button->setProperty("frameThumbnail", true);
            button->setCheckable(true);
            button->setAutoExclusive(true);
            button->setFocusPolicy(Qt::StrongFocus);
            button->setFixedSize(47, 41);
            button->setIconSize(QSize(41, 23));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setText(QStringLiteral("%1").arg(i+1, 2, 10, QLatin1Char('0')));
            const auto frame = ProductProofSamples::frame(ProcessingScene::Timelapse, i);
            button->setIcon(QPixmap::fromImage(frame.scaled(button->iconSize()*2,
                                      Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            frameButtons.append(button);
            strip->addWidget(button);
            connect(button, &QToolButton::clicked, this, [this, i] {
                if (scene == ProcessingScene::Timelapse && wantsSample()) {
                    loadSample(i);
                    refresh();
                }
            });
        }
        for (int direction : {-1, 1}) {
            auto* key = new QShortcut(QKeySequence(direction < 0 ? Qt::Key_Left : Qt::Key_Right), frameStrip);
            key->setContext(Qt::WidgetWithChildrenShortcut);
            connect(key, &QShortcut::activated, this, [this, direction] {
                if (frameButtons.isEmpty() || !wantsSample()) return;
                const int index = (sampleIndex + direction + int(frameButtons.size())) % int(frameButtons.size());
                frameButtons[index]->setFocus(Qt::OtherFocusReason);
                frameButtons[index]->click();
            });
        }
        row->addStretch();
        row->addWidget(frameStrip);
        sampleToggle = command(sampleTools, "printOrderSampleToggle", "link");
        row->addWidget(sampleToggle);
        row->addStretch();
        column->addWidget(sampleTools);
    }

    void loadSample(int index) {
        sampleIndex = std::clamp(index, 0, std::max(0, ProductProofSamples::count(scene)-1));
        sampleImage = ProductProofSamples::frame(scene, sampleIndex);
        if (scene == ProcessingScene::Timelapse && sampleIndex < frameButtons.size())
            frameButtons[sampleIndex]->setChecked(true);
    }

    void buildProduct() {
        auto* content = page("printOrderProductPage", "printOrderProductTitle", productTitle);
        auto* parent = productTitle->parentWidget();
        content->addWidget(productTitle);
        content->setSpacing(0);
        content->addSpacing(10);
        productGroup = new QButtonGroup(this);
        constexpr std::array names{
            "printOrderProductStill", "printOrderProductStarTrail", "printOrderProductTimelapse"
        };
        for (int i = 0; i < static_cast<int>(productButtons.size()); ++i) {
            auto* option = new ProductChoice(parent);
            option->setObjectName(QString::fromLatin1(names[i]));
            productGroup->addButton(option, i);
            productButtons[i] = option;
            content->addWidget(option);
        }
        content->addWidget(rule(parent, Qt::Horizontal));
        content->addStretch();
    }

    void buildAssets() {
        auto* content = page("printOrderAssetsPage", "printOrderAssetsTitle", assetsTitle);
        auto* parent = assetsTitle->parentWidget();
        content->addWidget(assetsTitle);
        content->addWidget(rule(parent, Qt::Horizontal));
        sourceLabel = label(parent, "printOrderSourceLabel", "muted");
        sourceValue = new ElidedLabel(parent);
        sourceValue->setObjectName(QStringLiteral("printOrderSourceName"));
        sourceValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pair(content, sourceLabel, sourceValue);
        inputCountLabel = label(parent, "printOrderInputCountLabel", "muted");
        inputCountValue = label(parent, "printOrderInputCount");
        inputCountValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pair(content, inputCountLabel, inputCountValue);

        subjectRow = new QWidget(parent);
        auto* subjectLayout = new QHBoxLayout(subjectRow);
        subjectLayout->setContentsMargins(0, 0, 0, 0);
        subjectLayout->setSpacing(12);
        subjectLabel = label(subjectRow, "printOrderSubjectLabel", "muted");
        subject = choices(subjectRow, "printOrderSubject");
        subjectLabel->setBuddy(subject);
        subjectLayout->addWidget(subjectLabel);
        subjectLayout->addWidget(subject, 1);
        content->addWidget(subjectRow);

        assetNote = note(parent, "printOrderAssetNote");
        calibrationNote = note(parent, "printOrderCalibrationNote");
        content->addWidget(assetNote);
        content->addWidget(calibrationNote);
        assetImports = imports(parent, "printOrderImportFiles", "printOrderImportFolder", &manageAssets);
        content->addWidget(assetImports.row);
        auto* sceneActions = new QHBoxLayout;
        sceneActions->setSpacing(6);
        calibration = command(parent, "printOrderCalibration", "link");
        ground = command(parent, "printOrderGround", "link");
        sceneActions->addWidget(calibration);
        sceneActions->addWidget(ground);
        sceneActions->addStretch();
        content->addLayout(sceneActions);
        content->addStretch();
    }

    void buildOutput() {
        auto* content = page("printOrderOutputPage", "printOrderOutputTitle", outputTitle);
        auto* parent = outputTitle->parentWidget();
        content->addWidget(outputTitle);
        content->addWidget(rule(parent, Qt::Horizontal));
        formatLabel = label(parent, "printOrderFormatLabel", "muted");
        formatField = choices(parent, "printOrderFormat");
        formatLabel->setBuddy(formatField);
        pair(content, formatLabel, formatField);
        directoryLabel = label(parent, "printOrderDirectoryLabel", "muted");
        directoryField = pathField(parent, "printOrderDirectory");
        directoryLabel->setBuddy(directoryField);
        content->addWidget(directoryLabel);
        auto* pathRow = new QHBoxLayout;
        pathRow->setSpacing(6);
        pathRow->addWidget(directoryField, 1);
        chooseDirectory = command(parent, "printOrderChooseDirectory");
        pathRow->addWidget(chooseDirectory);
        content->addLayout(pathRow);
        quickToggle = command(parent, "printOrderQuickToggle", "link");
        quickToggle->setCheckable(true);
        content->addWidget(quickToggle, 0, Qt::AlignLeft);
        exposureRow = new QWidget(parent);
        auto* adjustments = new QHBoxLayout(exposureRow);
        adjustments->setContentsMargins(0, 0, 0, 0);
        adjustments->setSpacing(8);
        exposureSlider = new QSlider(Qt::Horizontal, exposureRow);
        exposureSlider->setObjectName(QStringLiteral("printOrderExposureSlider"));
        exposureSlider->setRange(-50, 50);
        exposureSpin = new QDoubleSpinBox(exposureRow);
        exposureSpin->setObjectName(QStringLiteral("printOrderQuickExposure"));
        exposureSpin->setDecimals(1);
        exposureSpin->setRange(-5, 5);
        exposureSpin->setSingleStep(0.1);
        exposureSpin->setSuffix(QStringLiteral(" EV"));
        exposureSpin->setKeyboardTracking(false);
        exposureSpin->setMinimumWidth(88);
        adjustments->addWidget(exposureSlider, 1);
        adjustments->addWidget(exposureSpin);
        content->addWidget(exposureRow);
        outputNote = note(parent, "printOrderOutputNote", "warning");
        content->addWidget(outputNote);
        outputImports = imports(parent, "printOrderOutputImportFiles", "printOrderOutputImportFolder");
        content->addWidget(outputImports.row);
        outputCalibration = command(parent, "printOrderOutputCalibration", "link");
        content->addWidget(outputCalibration, 0, Qt::AlignLeft);
        taskNote = new ElidedLabel(parent);
        taskNote->setProperty("printRole", QStringLiteral("muted"));
        taskNote->setObjectName(QStringLiteral("printOrderTaskStatus"));
        content->addWidget(taskNote);
        taskProgress = new QProgressBar(parent);
        taskProgress->setObjectName(QStringLiteral("printOrderTaskProgress"));
        taskProgress->setRange(0, 100);
        taskProgress->setTextVisible(false);
        content->addWidget(taskProgress);
        content->addStretch();
    }

    void buildReceipt() {
        auto* content = page("printOrderReceiptPage", "printOrderReceiptTitle", receiptTitle);
        auto* parent = receiptTitle->parentWidget();
        auto* heading = new QHBoxLayout;
        heading->setSpacing(12);
        heading->addWidget(receiptTitle);
        stamp = new ReceiptStamp(parent);
        heading->addWidget(stamp);
        heading->addStretch();
        content->addLayout(heading);
        content->addWidget(rule(parent, Qt::Horizontal));
        resultCountLabel = label(parent, "printOrderResultCountLabel", "muted");
        resultCountValue = label(parent, "printOrderResultCount");
        resultCountValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pair(content, resultCountLabel, resultCountValue);
        frameSummary = new ElidedLabel(parent);
        frameSummary->setObjectName(QStringLiteral("printOrderFrameSummary"));
        frameSummary->setProperty("printRole", QStringLiteral("muted"));
        content->addWidget(frameSummary);
        resultPathLabel = label(parent, "printOrderResultPathLabel", "muted");
        resultPath = pathField(parent, "printOrderResultPath");
        resultPathLabel->setBuddy(resultPath);
        content->addWidget(resultPathLabel);
        content->addWidget(resultPath);
        frameDetails = command(parent, "printOrderFrameDetails");
        content->addWidget(frameDetails, 0, Qt::AlignLeft);
        content->addStretch();
    }

    void connectControls();
    void retranslate();
    void refresh();
    void syncScene();
    void syncOutput();
    void setStep(Step destination);
    void updateMotion();
    void paintLight();
    void pauseLight();
    void finishTransition();
    void watchWindow();
    bool eventFilter(QObject* watched, QEvent* event) override;

    bool isStill() const {
        return scene != ProcessingScene::StarTrail && scene != ProcessingScene::Timelapse;
    }
    bool hasReceipt() const { return !receiptPath.isEmpty() && !busy; }
    void discardReceipt() {
        receiptPath.clear();
        receiptFrames = 0;
        frameCounts.reset();
    }
    bool editable() const { return !busy && !hasReceipt(); }
    bool wantsSample() const {
        return step == Step::Product && editable() &&
               ((total == 0 && proofImage.isNull()) || productSampleChosen);
    }
    bool wantsExposureDraft() const {
        return proofIsSource && !proofImage.isNull() && !wantsSample() && editable() &&
               step == Step::Output && scene != ProcessingScene::Timelapse && evTenths != 0;
    }
    const QImage& displayedProof() const {
        if (wantsSample()) return sampleImage;
        return wantsExposureDraft() ? draftImage : proofImage;
    }
    void updateExposureDraft(bool forSourceCanvas = false) {
        if (!wantsExposureDraft() && !forSourceCanvas) return;
        if (draftKey != proofImage.cacheKey()) {
            draftKey = proofImage.cacheKey();
            draftImage = {};
            draftBase = proofImage;
            // 来源已经是有界浏览图。与画布保持相同尺寸，100% 和蒙版坐标都不会变。
            if (std::max(draftBase.width(), draftBase.height()) > 2400)
                draftBase = draftBase.scaled(2400,2400,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            if (draftBase.colorSpace().isValid() && draftBase.colorSpace() != QColorSpace(QColorSpace::SRgb))
                draftBase.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
            draftBase = draftBase.convertToFormat(QImage::Format_RGB888);
        }
        if (draftEv == evTenths && !draftImage.isNull()) return;
        // 仅用于相机浏览图的曝光方向试样；正式成品仍从线性 RAW 走原来的完整管线。
        draftEv = evTenths;
        const double gain = std::exp2(draftEv / 10.0);
        std::array<uchar,256> table;
        for (int i=0; i<256; ++i) {
            const double s=i/255.0;
            const double y=std::clamp(gain*(s<=0.04045 ? s/12.92 : std::pow((s+0.055)/1.055, 2.4)),0.0,1.0);
            table[i]=static_cast<uchar>(qRound(255*(y<=0.0031308 ? 12.92*y : 1.055*std::pow(y,1.0/2.4)-0.055)));
        }
        draftImage = draftBase.copy();
        for (int y=0; y<draftImage.height(); ++y) {
            auto* row=draftImage.scanLine(y);
            for (int i=0; i<draftImage.width()*3; ++i) row[i]=table[row[i]];
        }
        draftImage.setColorSpace(QColorSpace::SRgb);
    }
    bool needsCalibration() const {
        return scene == ProcessingScene::DeepSky && !calibrationReady;
    }
    int requiredFrames() const {
        if (!isStill()) return 3;
        return scene == ProcessingScene::SingleFrame ? 1 : 2;
    }
    bool canProcess() const {
        return included >= requiredFrames() && !needsCalibration() &&
            formatField->currentIndex() >= 0 && !directory.trimmed().isEmpty();
    }
    QString expectedCount() const;
    QString missingFrames() const;
    QString calibrationText() const;
};

void PrintOrderPanel::Ui::connectControls() {
    connect(openOrder, &QAction::triggered, this, [this]() {
        if (!busy) emit owner->openOrderRequested();
    });
    connect(saveOrder, &QAction::triggered, this, [this]() {
        if (!busy) emit owner->saveOrderRequested();
    });
    connect(ridgeComparison, &QPushButton::clicked, this, [this]() {
        if (!busy && ridgeComparisonAvailable) emit owner->ridgeComparisonRequested();
    });
    connect(frameDetails, &QPushButton::clicked, this, [this]() {
        if (hasReceipt() && frameCounts) emit owner->frameReceiptRequested();
    });
    connect(stageGroup, &QButtonGroup::idClicked, this, [this](int index) {
        if (detail && faces->currentWidget() == detail) emit owner->formRequested();
        setStep(static_cast<Step>(index));
    });
    const auto selectProduct = [this](ProcessingScene requested) {
        if (!editable()) return;
        productSampleChosen = step == Step::Product;
        owner->setScene(requested);
        emit owner->productRequested(requested);
    };
    connect(productGroup, &QButtonGroup::idClicked, this, [selectProduct](int id) {
        constexpr std::array scenes{
            ProcessingScene::Nightscape, ProcessingScene::StarTrail, ProcessingScene::Timelapse
        };
        selectProduct(scenes[id]);
    });
    connect(subject, &QComboBox::activated, this, [this, selectProduct](int index) {
        selectProduct(static_cast<ProcessingScene>(subject->itemData(index).toInt()));
    });
    for (const Imports& controls : {assetImports, outputImports}) {
        connect(controls.files, &QPushButton::clicked, owner, &PrintOrderPanel::importFilesRequested);
        connect(controls.folder, &QPushButton::clicked, owner, &PrintOrderPanel::importFolderRequested);
    }
    const auto openAdvanced = [this](WorkflowStage stage, QWidget* origin) {
        if (busy) return;
        returnFocus = origin;
        emit owner->advancedRequested(stage);
    };
    connect(manageAssets, &QPushButton::clicked, this, [this, openAdvanced]() {
        openAdvanced(WorkflowStage::Assets, manageAssets);
    });
    for (auto* button : {calibration, outputCalibration}) {
        connect(button, &QPushButton::clicked, this, [button, openAdvanced]() {
            openAdvanced(WorkflowStage::Calibration, button);
        });
    }
    connect(ground, &QPushButton::clicked, this, [this, openAdvanced]() {
        openAdvanced(WorkflowStage::Stacking, ground);
    });
    connect(advanced, &QPushButton::clicked, this, [this, openAdvanced]() {
        openAdvanced(lastDetailStage.value_or(isStill() ? WorkflowStage::Adjustment : WorkflowStage::Stacking), advanced);
    });
    connect(proof, &QAbstractButton::clicked, owner, &PrintOrderPanel::proofRequested);
    connect(sampleToggle, &QPushButton::clicked, this, [this] {
        productSampleChosen = !productSampleChosen;
        refresh();
    });
    connect(quickToggle, &QPushButton::toggled, this, [this] { refresh(); });
    connect(exposureSlider, &QSlider::valueChanged, this, [this](int value) {
        owner->setExposureTenths(value);
        emit owner->exposureRequested(value);
    });
    connect(exposureSpin, &QDoubleSpinBox::valueChanged, this, [this](double ev) {
        const int value = qRound(ev * 10);
        owner->setExposureTenths(value);
        emit owner->exposureRequested(value);
    });
    connect(formatField, &QComboBox::activated, this, [this](int index) {
        if (!editable()) return;
        const QString selected = formatField->itemData(index).toString();
        format = selected;
        refresh();
        emit owner->formatRequested(selected);
    });
    connect(chooseDirectory, &QPushButton::clicked, owner, &PrintOrderPanel::directoryRequested);
    connect(back, &QPushButton::clicked, this, [this]() {
        if (step == Step::Output) setStep(Step::Assets);
        else if (step == Step::Assets) setStep(Step::Product);
    });
    connect(next, &QPushButton::clicked, this, [this]() {
        if (busy) {
            if (cancelPending) return;
            cancelPending = true;
            refresh();
            emit owner->processRequested();
        } else if (editable()) {
            switch (step) {
            case Step::Product: setStep(Step::Assets); break;
            case Step::Assets: setStep(Step::Output); break;
            case Step::Output:
                if (canProcess()) emit owner->processRequested();
                break;
            case Step::Receipt: break;
            }
        }
    });
    connect(newOrder, &QPushButton::clicked, owner, &PrintOrderPanel::newOrderRequested);
    connect(reveal, &QPushButton::clicked, this, [this]() {
        if (!hasReceipt()) return;
        const QString path = receiptPath;
        emit owner->revealRequested(path);
    });
    connect(lightField, &QComboBox::activated, this, [this](int index) {
        const auto light = static_cast<Light>(index);
        if (systemReduced || selectedLight == light) return;
        selectedLight = light;
        pauseLight();
        lightOffset = 2600;
        updateMotion();
    });
}

void PrintOrderPanel::Ui::retranslate() {
    owner->setAccessibleName(QCoreApplication::translate("Nebulens", "冲印单"));
    lightLabel->setText(QCoreApplication::translate("Nebulens", "纸面光影"));
    lightField->setAccessibleName(lightLabel->text());
    {
        const QSignalBlocker blocker(lightField);
        lightField->clear();
        lightField->addItem(QCoreApplication::translate("Nebulens", "柔光"));
        lightField->addItem(QCoreApplication::translate("Nebulens", "流光"));
        lightField->addItem(QCoreApplication::translate("Nebulens", "静止"));
        lightField->setCurrentIndex(static_cast<int>(systemReduced ? Light::Still : selectedLight));
    }
    lightField->setToolTip(systemReduced
        ? QCoreApplication::translate("Nebulens", "已遵循系统的减弱动态效果设置")
        : QString());
    orderFiles->setText(QCoreApplication::translate("Nebulens", "冲印单"));
    orderFiles->setAccessibleName(QCoreApplication::translate("Nebulens", "冲印单菜单"));
    orderFiles->setToolTip(orderFiles->accessibleName());
    applicationMenus->setAccessibleName(QCoreApplication::translate("Nebulens", "菜单"));
    applicationMenus->setToolTip(applicationMenus->accessibleName());
    openOrder->setText(QCoreApplication::translate("Nebulens", "打开冲印单"));
    saveOrder->setText(QCoreApplication::translate("Nebulens", "保存冲印单"));
    stageButtons[0]->setText(QStringLiteral("01   %1").arg(QCoreApplication::translate("Nebulens", "成品")));
    stageButtons[1]->setText(QStringLiteral("02   %1").arg(QCoreApplication::translate("Nebulens", "素材")));
    stageButtons[2]->setText(QStringLiteral("03   %1").arg(QCoreApplication::translate("Nebulens", "保存")));

    productTitle->setText(QCoreApplication::translate("Nebulens", "想得到什么？"));
    productButtons[0]->setCopy(QCoreApplication::translate("Nebulens", "一张成片"),
        QCoreApplication::translate("Nebulens", "精修单张，或合成多张以降低噪点"));
    productButtons[1]->setCopy(QCoreApplication::translate("Nebulens", "一张星轨"),
        QCoreApplication::translate("Nebulens", "将连续拍摄的星点连成轨迹"));
    productButtons[2]->setCopy(QCoreApplication::translate("Nebulens", "一组延时照片"),
        QCoreApplication::translate("Nebulens", "逐张输出，留给剪辑时编排"));

    assetsTitle->setText(QCoreApplication::translate("Nebulens", "核对素材"));
    sourceLabel->setText(QCoreApplication::translate("Nebulens", "拍摄组"));
    inputCountLabel->setText(QCoreApplication::translate("Nebulens", "选用素材"));
    subjectLabel->setText(QCoreApplication::translate("Nebulens", "拍摄对象"));
    subject->setAccessibleName(subjectLabel->text());
    {
        const QSignalBlocker blocker(subject);
        subject->clear();
        subject->addItem(QCoreApplication::translate("Nebulens", "星景"),
                         static_cast<int>(ProcessingScene::Nightscape));
        subject->addItem(QCoreApplication::translate("Nebulens", "天地分离"),
                         static_cast<int>(ProcessingScene::SkyGround));
        subject->addItem(QCoreApplication::translate("Nebulens", "深空天体"),
                         static_cast<int>(ProcessingScene::DeepSky));
    }
    for (const Imports& controls : {assetImports, outputImports}) {
        controls.files->setText(QCoreApplication::translate("Nebulens", "导入文件"));
        controls.folder->setText(QCoreApplication::translate("Nebulens", "导入文件夹"));
    }
    manageAssets->setText(QCoreApplication::translate("Nebulens", "管理素材"));
    ground->setText(QCoreApplication::translate("Nebulens", "地景设置"));

    outputTitle->setText(QCoreApplication::translate("Nebulens", "交付设置"));
    formatLabel->setText(QCoreApplication::translate("Nebulens", "文件格式"));
    formatField->setAccessibleName(formatLabel->text());
    {
        const QSignalBlocker blocker(formatField);
        formatField->clear();
        formatField->addItem(QCoreApplication::translate("Nebulens", "TIFF · 16 位"),
                             QString::fromLatin1(kTiff));
        formatField->addItem(QCoreApplication::translate("Nebulens", "PNG · 8 位"),
                             QString::fromLatin1(kPng));
    }
    directoryLabel->setText(QCoreApplication::translate("Nebulens", "保存到"));
    directoryField->setAccessibleName(directoryLabel->text());
    directoryField->setPlaceholderText(QCoreApplication::translate("Nebulens", "未选择目录"));
    chooseDirectory->setText(QCoreApplication::translate("Nebulens", "选择目录…"));
    quickToggle->setText(QCoreApplication::translate("Nebulens", "曝光微调"));
    exposureSlider->setAccessibleName(QCoreApplication::translate("Nebulens", "曝光微调"));
    exposureSpin->setAccessibleName(exposureSlider->accessibleName());
    for (int i = 0; i < frameButtons.size(); ++i) {
        const QString name = QCoreApplication::translate("Nebulens", "示例第 %1 帧，共 %2 帧")
            .arg(i+1).arg(frameButtons.size());
        frameButtons[i]->setAccessibleName(name);
        frameButtons[i]->setToolTip(name);
    }
    outputCalibration->setText(QCoreApplication::translate("Nebulens", "补齐校准"));
    taskProgress->setAccessibleName(QCoreApplication::translate("Nebulens", "冲印进度"));

    receiptTitle->setText(QCoreApplication::translate("Nebulens", "冲印回执"));
    stamp->setText(QCoreApplication::translate("Nebulens", "已出样"));
    resultCountLabel->setText(QCoreApplication::translate("Nebulens", "交付"));
    resultPathLabel->setText(QCoreApplication::translate("Nebulens", "实际保存位置"));
    frameDetails->setText(QCoreApplication::translate("Nebulens", "用片明细"));
    resultPath->setAccessibleName(resultPathLabel->text());
    ridgeComparison->setText(QCoreApplication::translate("Nebulens", "山脊对照"));
    advanced->setText(QCoreApplication::translate("Nebulens", "冲印参数"));
    back->setText(QCoreApplication::translate("Nebulens", "上一步"));
    newOrder->setText(QCoreApplication::translate("Nebulens", "重新开单"));
    reveal->setText(QCoreApplication::translate("Nebulens", "打开目录"));
    syncScene();
    syncOutput();
    refresh();
}

void PrintOrderPanel::Ui::syncScene() {
    auto* selectedProduct = productButtons[scene == ProcessingScene::StarTrail ? 1
        : (scene == ProcessingScene::Timelapse ? 2 : 0)];
    if (!selectedProduct->isChecked()) {
        const QSignalBlocker blocker(productGroup);
        const QSignalBlocker still(productButtons[0]);
        const QSignalBlocker trail(productButtons[1]);
        const QSignalBlocker timelapse(productButtons[2]);
        selectedProduct->setChecked(true);
        for (auto* option : productButtons) option->syncCheck();
    }
    if (isStill()) {
        const QSignalBlocker blocker(subject);
        const auto chosen = scene == ProcessingScene::SingleFrame ? ProcessingScene::Nightscape : scene;
        subject->setCurrentIndex(subject->findData(static_cast<int>(chosen)));
    }
}

void PrintOrderPanel::Ui::syncOutput() {
    const QSignalBlocker formatBlocker(formatField);
    formatField->setCurrentIndex(formatField->findData(format));
    syncPath(directoryField, directory);
}

QString PrintOrderPanel::Ui::expectedCount() const {
    if (scene == ProcessingScene::Timelapse) {
        return included == 0 ? QCoreApplication::translate("Nebulens", "一组延时照片")
            : QCoreApplication::translate("Nebulens", "%1 张延时照片").arg(included);
    }
    return scene == ProcessingScene::StarTrail
        ? QCoreApplication::translate("Nebulens", "一张星轨")
        : QCoreApplication::translate("Nebulens", "一张成片");
}

QString PrintOrderPanel::Ui::missingFrames() const {
    const int remaining = requiredFrames() - included;
    if (remaining <= 0) return {};
    if (total == 0 && isStill() &&
        (scene == ProcessingScene::Nightscape || scene == ProcessingScene::SingleFrame)) {
        return QCoreApplication::translate("Nebulens", "尚未导入 RAW 素材");
    }
    return QCoreApplication::translate("Nebulens", "还需要 %1 张选用的 RAW 素材").arg(remaining);
}

QString PrintOrderPanel::Ui::calibrationText() const {
    if (!calibrationHint.isEmpty()) return calibrationHint;
    if (calibrationReady) return QCoreApplication::translate("Nebulens", "校准素材已就绪");
    return QCoreApplication::translate("Nebulens",
        "请补齐 Dark、Flat，以及 Bias 或同曝光 Dark Flat；也可导入 Master");
}

void PrintOrderPanel::Ui::refresh() {
    const bool complete = hasReceipt();
    const bool editing = editable();
    const bool deepSky = scene == ProcessingScene::DeepSky;
    const QString missing = missingFrames();
    const QString calibrate = deepSky ? calibrationText() : QString();

    sourceValue->setValue(sourceName.isEmpty()
        ? QCoreApplication::translate("Nebulens", "未设置") : sourceName);
    inputCountValue->setText(QCoreApplication::translate("Nebulens", "%1 / %2 张")
                                 .arg(included).arg(total));
    subjectRow->setVisible(isStill());
    assetNote->setText(missing);
    assetNote->setVisible(!missing.isEmpty());
    calibrationNote->setText(calibrate);
    calibrationNote->setToolTip(calibrate.toHtmlEscaped());
    calibrationNote->setVisible(deepSky);
    calibration->setText(needsCalibration()
        ? QCoreApplication::translate("Nebulens", "补齐校准")
        : QCoreApplication::translate("Nebulens", "校准设置"));
    calibration->setVisible(deepSky);
    ground->setVisible(scene == ProcessingScene::SkyGround);
    manageAssets->setVisible(total > 0);
    assetImports.row->setEnabled(editing);

    QString issue;
    if (editing) {
        if (!missing.isEmpty()) issue = missing;
        else if (needsCalibration()) issue = calibrate;
        else if (formatField->currentIndex() < 0) {
            issue = QCoreApplication::translate("Nebulens", "请选择文件格式");
        } else if (directory.trimmed().isEmpty()) {
            issue = QCoreApplication::translate("Nebulens", "请选择输出目录");
        }
    }
    outputNote->setText(issue);
    outputNote->setToolTip(issue.toHtmlEscaped());
    outputNote->setVisible(!issue.isEmpty());
    outputImports.row->setVisible(editing && !missing.isEmpty());
    outputCalibration->setVisible(editing && needsCalibration());
    formatField->setEnabled(editing);
    chooseDirectory->setEnabled(editing);

    QString taskText = status;
    if (cancelPending) taskText = QCoreApplication::translate("Nebulens", "正在取消…");
    else if (retry && taskText.isEmpty()) {
        taskText = QCoreApplication::translate("Nebulens", "本次未保存成品");
    }
    taskNote->setValue(taskText);
    taskNote->setVisible(!complete && !taskText.isEmpty());
    {
        const QSignalBlocker blocker(taskProgress);
        taskProgress->setValue(std::max(0, progress));
    }
    taskProgress->setVisible(busy && progress >= 0);

    updateExposureDraft();
    proof->setImage(displayedProof());
    const bool sequenceExample = wantsSample() && scene == ProcessingScene::Timelapse;
    frameStrip->setVisible(sequenceExample && frameButtons.size() > 1);
    sampleToggle->setVisible(total > 0 && step == Step::Product && editing);
    sampleToggle->setText(productSampleChosen ? QCoreApplication::translate("Nebulens", "看本单印样")
                                               : QCoreApplication::translate("Nebulens", "看成品示例"));
    if (wantsSample() && !sampleImage.isNull()) {
        QString type = expectedCount();
        if (sequenceExample) type = QCoreApplication::translate("Nebulens", "延时照片 %1 / %2")
            .arg(sampleIndex+1).arg(frameButtons.size());
        proof->setCopy(QCoreApplication::translate("Nebulens", "示例印样") + QStringLiteral(" · ") + type,
                       ProductProofSamples::credit(scene),
                       QCoreApplication::translate("Nebulens", "查看大图"));
        proof->setEnabled(true);
    } else {
        proof->setEnabled(!displayedProof().isNull());
        if (displayedProof().isNull()) {
            proof->setCopy(wantsSample() ? QCoreApplication::translate("Nebulens", "示例暂不可用") :
                           (proofCaption.isEmpty() ? QCoreApplication::translate("Nebulens", "等待原片") : proofCaption),
                           {}, {});
        } else if (wantsExposureDraft()) {
            proof->setCopy(owner->exposureDraftCaption(),
                           QCoreApplication::translate("Nebulens", "基于原片预览，非正式成品"),
                           QCoreApplication::translate("Nebulens", "查看大图"));
        } else {
            proof->setCopy(proofCaption.isEmpty() ? QCoreApplication::translate("Nebulens", "印样")
                                                 : proofCaption,
                           {}, QCoreApplication::translate("Nebulens", "查看大图"));
        }
    }

    const Step shown = busy ? Step::Output : (complete ? Step::Receipt : step);
    const bool onDetail = detail && faces->currentWidget() == detail;
    detailContext->setVisible(onDetail);
    detailContext->setText((complete ? QCoreApplication::translate("Nebulens", "冲印回执") :
        stageButtons[std::min(static_cast<int>(shown), 2)]->text()) +
        QStringLiteral("  /  ") + QCoreApplication::translate("Nebulens", "冲印参数"));
    auto* currentStage = stageButtons[std::min(static_cast<int>(shown), 2)];
    {
        const QSignalBlocker blocker(stageGroup);
        currentStage->setChecked(true);
    }
    for (auto* stage : stageButtons) {
        stage->setVisible(!onDetail);
        stage->setEnabled(editing);
    }
    for (auto* option : productButtons) option->setEnabled(editing);
    subject->setEnabled(editing);
    calibration->setEnabled(editing);
    ground->setEnabled(editing);

    orderFiles->setEnabled(!busy);
    openOrder->setEnabled(!busy);
    saveOrder->setEnabled(!busy);
    if (busy && orderFiles->menu()->isVisible()) orderFiles->menu()->close();
    ridgeComparison->setVisible(ridgeComparisonAvailable);
    ridgeComparison->setEnabled(!busy && ridgeComparisonAvailable);
    advanced->setVisible(!busy);
    const bool quickAvailable = shown == Step::Output && editing && total > 0 &&
                                scene != ProcessingScene::Timelapse;
    quickToggle->setVisible(quickAvailable);
    exposureRow->setVisible(quickAvailable && quickToggle->isChecked());
    back->setVisible(editing && shown != Step::Product);
    newOrder->setVisible(complete);
    reveal->setVisible(complete);
    next->setVisible(!complete);
    next->setEnabled(busy ? !cancelPending : (shown != Step::Output || canProcess()));

    const bool hasFrameDetails = complete && frameCounts.has_value();
    QString frameText;
    if (hasFrameDetails) {
        const auto& counts = *frameCounts;
        frameText = counts.manual > 0
            ? QCoreApplication::translate("Nebulens", "入选 %1 张，采用 %2 张 · 跳过 %3 张 · 手动排除 %4 张")
                  .arg(counts.submitted).arg(counts.used).arg(counts.skipped).arg(counts.manual)
            : QCoreApplication::translate("Nebulens", "入选 %1 张，采用 %2 张 · 跳过 %3 张")
                  .arg(counts.submitted).arg(counts.used).arg(counts.skipped);
    }
    frameSummary->setValue(frameText);
    frameSummary->setVisible(hasFrameDetails);
    frameDetails->setVisible(hasFrameDetails);
    frameDetails->setEnabled(hasFrameDetails);

    if (complete) {
        const QString count = QCoreApplication::translate("Nebulens", "%1 张成品").arg(receiptFrames);
        resultCountValue->setText(count);
        summaryLabel->setText(QCoreApplication::translate("Nebulens", "本单交付"));
        summaryValue->setValue(count);
        syncPath(resultPath, receiptPath);
    } else if (busy) {
        summaryLabel->setText(QCoreApplication::translate("Nebulens", "冲印进度"));
        summaryValue->setValue(progress >= 0 ? QStringLiteral("%1%").arg(progress)
            : QCoreApplication::translate("Nebulens", "正在冲印"));
        next->setText(cancelPending ? QCoreApplication::translate("Nebulens", "正在取消…")
                                    : QCoreApplication::translate("Nebulens", "取消"));
    } else {
        summaryLabel->setText(QCoreApplication::translate("Nebulens", "预计交付"));
        summaryValue->setValue(expectedCount());
        switch (shown) {
        case Step::Product:
            next->setText(QCoreApplication::translate("Nebulens", "核对素材"));
            break;
        case Step::Assets:
            next->setText(QCoreApplication::translate("Nebulens", "交付设置"));
            break;
        case Step::Output:
            next->setText(retry ? QCoreApplication::translate("Nebulens", "重试")
                                : QCoreApplication::translate("Nebulens", "保存成品"));
            break;
        case Step::Receipt: break;
        }
    }
    if (receiptPath.isEmpty()) {
        syncPath(resultPath, {});
        resultCountValue->clear();
    }
    if (pages->currentIndex() != static_cast<int>(shown)) {
        finishTransition();
        pages->setCurrentIndex(static_cast<int>(shown));
        if (motionAllowed && !busy) {
            leftFade->setOpacity(0.56);
            leftFade->setEnabled(true);
            stamp->setInk(0);
            transition.start();
        }
    }
}

void PrintOrderPanel::Ui::setStep(Step destination) {
    if (!editable() || step == destination) return;
    step = destination;
    productSampleChosen = false;
    refresh();
    if (step == Step::Product) {
        productGroup->checkedButton()->setFocus(Qt::OtherFocusReason);
    } else if (step == Step::Assets) {
        (total > 0 && isStill() ? static_cast<QWidget*>(subject) : assetImports.files)
            ->setFocus(Qt::OtherFocusReason);
    } else if (step == Step::Output) {
        formatField->setFocus(Qt::OtherFocusReason);
    }
}

void PrintOrderPanel::Ui::finishTransition() {
    if (transition.state() != QAbstractAnimation::Stopped) transition.stop();
    leftFade->setOpacity(1);
    leftFade->setEnabled(false);
    stamp->setInk(1);
}

void PrintOrderPanel::Ui::watchWindow() {
    QWidget* current = owner->window();
    if (current == watchedWindow) return;
    if (watchedWindow && watchedWindow != owner) watchedWindow->removeEventFilter(this);
    watchedWindow = current;
    if (current != owner) current->installEventFilter(this);
}

bool PrintOrderPanel::Ui::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::Hide:
        preferenceTimer.stop();
        pauseLight();
        break;
    case QEvent::Show:
    case QEvent::ParentChange:
        if (watched == owner) watchWindow();
        updateMotion();
        break;
    case QEvent::WindowStateChange:
    case QEvent::WindowActivate:
    case QEvent::WindowBlocked:
    case QEvent::WindowUnblocked:
    case QEvent::StyleChange:
        updateMotion();
        break;
    default:
        break;
    }
    return false;
}

void PrintOrderPanel::Ui::updateMotion() {
    const auto state = QGuiApplication::applicationState();
    if (inspectingProof || !owner->isVisible() || owner->window()->isMinimized() ||
        state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) {
        preferenceTimer.stop();
        pauseLight();
        return;
    }
    if (!preferenceTimer.isActive()) preferenceTimer.start();
    if (const bool reduced = reduceMotion(owner); reduced != systemReduced) {
        systemReduced = reduced;
        lightField->setToolTip(reduced
            ? QCoreApplication::translate("Nebulens", "已遵循系统的减弱动态效果设置")
            : QString());
        lightField->setEnabled(!reduced);
        const QSignalBlocker blocker(lightField);
        lightField->setCurrentIndex(static_cast<int>(reduced ? Light::Still : selectedLight));
    }
    if (systemReduced || selectedLight == Light::Still) {
        pauseLight();
        paper->setLight(0.73, 0.0);
        proof->setLight(0.73, 0.0);
        return;
    }
    if (const auto* modal = QApplication::activeModalWidget(); modal && modal->window() != watchedWindow) {
        pauseLight();
        return;
    }
    if (!ambientTimer.isActive()) {
        motionAllowed = true;
        for (auto* option : productButtons) option->setAnimate(true);
        elapsed.start();
        paintLight();
        ambientTimer.start();
    }
}

void PrintOrderPanel::Ui::paintLight() {
    const bool flow = selectedLight == Light::Flow;
    const int duration = flow ? kFlowPeriod : kSoftPeriod;
    const qreal phase = ((lightOffset + elapsed.elapsed()) % duration) / qreal(duration);
    const qreal sweep = -std::cos(6.283185307179586 * phase);
    const qreal strength = (flow ? 0.95 : 0.77) * (0.93 + 0.07 * sweep);
    const qreal travel = (flow ? 0.25 : 0.16) * sweep;
    paper->setLight(strength, travel);
    proof->setLight(strength, travel);
}

void PrintOrderPanel::Ui::pauseLight() {
    if (!motionAllowed) return;
    motionAllowed = false;
    ambientTimer.stop();
    lightOffset = (lightOffset + elapsed.elapsed()) % (selectedLight == Light::Flow ? kFlowPeriod
                                                                                    : kSoftPeriod);
    elapsed.invalidate();
    for (auto* option : productButtons) option->setAnimate(false);
    finishTransition();
}

PrintOrderPanel::PrintOrderPanel(QWidget* parent) : QWidget(parent) {
    m_ui = std::make_unique<Ui>(this);
}

PrintOrderPanel::~PrintOrderPanel() = default;

void PrintOrderPanel::setScene(ProcessingScene scene) {
    const bool changed = m_ui->scene != scene;
    if (changed) {
        m_ui->frameCounts.reset();
        m_ui->lastDetailStage.reset();
    }
    m_ui->scene = scene;
    if (changed) m_ui->loadSample(0);
    m_ui->syncScene();
    m_ui->refresh();
}

void PrintOrderPanel::setProject(int included, int total, const QString& sourceName,
                                 bool calibrationReady, const QString& calibrationHint) {
    if (m_ui->total != total) m_ui->productSampleChosen = false;
    m_ui->total = std::max(0, total);
    m_ui->included = std::clamp(included, 0, m_ui->total);
    m_ui->sourceName = sourceName;
    m_ui->calibrationReady = calibrationReady;
    m_ui->calibrationHint = calibrationHint;
    m_ui->refresh();
}

void PrintOrderPanel::setOutput(const QString& format, const QString& directory) {
    m_ui->format = format;
    m_ui->directory = directory;
    m_ui->syncOutput();
    m_ui->refresh();
}

void PrintOrderPanel::setProof(const QImage& image, const QString& caption, bool isSource) {
    m_ui->proofImage = image;
    m_ui->proofIsSource = isSource;
    m_ui->proofCaption = caption;
    m_ui->refresh();
}

QImage PrintOrderPanel::proofImage() const {
    return m_ui->displayedProof();
}

QString PrintOrderPanel::proofCaption() const { return m_ui->proof->caption(); }

QImage PrintOrderPanel::sourceExposureProof() {
    if (!m_ui->proofIsSource || m_ui->proofImage.isNull() || !m_ui->evTenths ||
        m_ui->scene == ProcessingScene::Timelapse) return {};
    m_ui->updateExposureDraft(true);
    return m_ui->draftImage;
}

QString PrintOrderPanel::exposureDraftCaption() const {
    const QString ev = (m_ui->evTenths > 0 ? QStringLiteral("+") : QString()) +
                       QLocale().toString(m_ui->evTenths / 10.0, 'f', 1);
    return QCoreApplication::translate("Nebulens", "曝光试样 · %1 EV").arg(ev);
}

QWidget* PrintOrderPanel::masthead() const { return m_ui->header; }

void PrintOrderPanel::setApplicationMenus(const QList<QAction*>& actions) {
    m_ui->applicationMenu->addActions(actions);
    m_ui->applicationMenus->setVisible(!actions.isEmpty());
    if (!actions.isEmpty()) m_ui->applicationMenus->setShortcut(QKeySequence(Qt::Key_F10));
}

void PrintOrderPanel::setDetailPage(QWidget* page) {
    Q_ASSERT(!m_ui->detail && page);
    m_ui->detail = page;
    m_ui->faces->addWidget(page);
}

bool PrintOrderPanel::showingDetailPage() const {
    return m_ui->detail && m_ui->faces->currentWidget() == m_ui->detail;
}

void PrintOrderPanel::rememberDetailStage(WorkflowStage stage) { m_ui->lastDetailStage = stage; }

void PrintOrderPanel::showDetailPage(bool open) {
    if (!m_ui->detail || showingDetailPage() == open) return;
    if (open && !m_ui->returnFocus) {
        QWidget* focus = QApplication::focusWidget();
        m_ui->returnFocus = focus && m_ui->form->isAncestorOf(focus) ? focus : m_ui->advanced;
    }
    m_ui->faces->setCurrentWidget(open ? m_ui->detail : m_ui->form);
    m_ui->refresh();
    if (!open) {
        QWidget* target = m_ui->returnFocus;
        if (!target || !target->isVisible() || !target->isEnabled()) target = m_ui->advanced;
        if (target->isVisible() && target->isEnabled()) target->setFocus(Qt::OtherFocusReason);
        m_ui->returnFocus.clear();
    }
    m_ui->updateMotion();
}

void PrintOrderPanel::setExposureTenths(int value) {
    const QSignalBlocker slider(m_ui->exposureSlider), field(m_ui->exposureSpin);
    m_ui->exposureSlider->setValue(value);
    m_ui->exposureSpin->setValue(value * 0.1);
    if (m_ui->evTenths != value) {
        m_ui->evTenths = value;
        m_ui->refresh();
    }
}

void PrintOrderPanel::setProofInspectionActive(bool active) {
    if (m_ui->inspectingProof == active) return;
    m_ui->inspectingProof = active;
    m_ui->updateMotion();
}

void PrintOrderPanel::setTask(bool busy, int progress, const QString& status) {
    const bool starting = busy && !m_ui->busy;
    const bool finishing = !busy && m_ui->busy;
    m_ui->busy = busy;
    m_ui->progress = std::clamp(progress, -1, 100);
    m_ui->status = status;
    if (starting) {
        m_ui->step = Step::Output;
        m_ui->discardReceipt();
        m_ui->cancelPending = false;
        m_ui->retry = false;
    }
    if (finishing) {
        if (m_ui->cancelPending && status.isEmpty()) {
            m_ui->status = QCoreApplication::translate("Nebulens", "已取消冲印");
        }
        m_ui->cancelPending = false;
        m_ui->retry = m_ui->receiptPath.isEmpty();
    }
    m_ui->refresh();
}

void PrintOrderPanel::setReceipt(const QString& path, int frameCount, const FrameReceipt& info) {
    if (path.trimmed().isEmpty() || frameCount <= 0) {
        clearReceipt();
        return;
    }
    m_ui->step = Step::Output;
    m_ui->receiptPath = path;
    m_ui->receiptFrames = frameCount;
    m_ui->frameCounts.reset();
    if (!info.rows.isEmpty()) {
        m_ui->frameCounts = Ui::FrameCounts{
            info.submittedCount(), info.usedCount(), info.skippedCount(), info.manualCount()};
    }
    m_ui->retry = false;
    m_ui->refresh();
}

void PrintOrderPanel::clearReceipt() {
    if (m_ui->hasReceipt()) {
        m_ui->status.clear();
        m_ui->progress = -1;
    }
    m_ui->discardReceipt();
    m_ui->refresh();
}

void PrintOrderPanel::setRidgeComparisonAvailable(bool available) {
    if (m_ui->ridgeComparisonAvailable == available) return;
    m_ui->ridgeComparisonAvailable = available;
    m_ui->refresh();
}

void PrintOrderPanel::showReopenedOrder() {
    if (m_ui->busy) return;
    m_ui->step = m_ui->total > 0 ? Step::Assets : Step::Product;
    m_ui->retry = false;
    m_ui->progress = -1;
    m_ui->status.clear();
    clearReceipt();
}

void PrintOrderPanel::resetOrder() {
    showDetailPage(false);
    m_ui->step = Step::Product;
    m_ui->scene = ProcessingScene::Nightscape;
    m_ui->lastDetailStage.reset();
    m_ui->returnFocus.clear();
    m_ui->included = 0;
    m_ui->total = 0;
    m_ui->sourceName.clear();
    m_ui->calibrationReady = false;
    m_ui->calibrationHint.clear();
    m_ui->proofImage = QImage();
    m_ui->draftImage = {};
    m_ui->draftBase = {};
    m_ui->draftKey = 0;
    m_ui->proofIsSource = false;
    m_ui->proofCaption.clear();
    m_ui->productSampleChosen = false;
    m_ui->loadSample(0);
    m_ui->quickToggle->setChecked(false);
    m_ui->ridgeComparisonAvailable = false;
    m_ui->busy = false;
    m_ui->cancelPending = false;
    m_ui->retry = false;
    m_ui->progress = -1;
    m_ui->status.clear();
    m_ui->syncScene();
    clearReceipt();
}

void PrintOrderPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_ui) m_ui->lightLabel->setVisible(width() >= 1080);
}

void PrintOrderPanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_ui) m_ui->retranslate();
}
