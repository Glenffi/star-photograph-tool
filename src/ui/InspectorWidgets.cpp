#include "InspectorWidgets.h"

#include "StyleTokens.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kToggleThumbDiameter = StyleTokens::Controls::kSliderHandleSize;

QColor interpolateColor(const QColor& from, const QColor& to, qreal position) {
    const qreal amount = std::clamp(position, 0.0, 1.0);
    return QColor::fromRgbF(
        from.redF() + (to.redF() - from.redF()) * amount,
        from.greenF() + (to.greenF() - from.greenF()) * amount,
        from.blueF() + (to.blueF() - from.blueF()) * amount,
        from.alphaF() + (to.alphaF() - from.alphaF()) * amount);
}

class FineSlider final : public QSlider {
public:
    explicit FineSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent) {}

    int fineStep() const { return m_fineStep; }

    void setFineStep(int step) {
        m_fineStep = std::max(1, step);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        QSlider::mousePressEvent(event);
        if (event->button() != Qt::LeftButton) return;

        m_dragging = true;
        m_pressPosition = event->position();
        m_pressValue = value();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_dragging || !(event->buttons() & Qt::LeftButton)
            || !(event->modifiers() & Qt::ShiftModifier)) {
            QSlider::mouseMoveEvent(event);
            return;
        }

        QStyleOptionSlider option;
        initStyleOption(&option);
        const int handleLength = style()->pixelMetric(
            QStyle::PM_SliderLength, &option, this);
        const int availablePixels = std::max(1, width() - handleLength);
        const double pixelDelta = event->position().x() - m_pressPosition.x();
        const double range = static_cast<double>(maximum()) - minimum();
        double valueDelta = pixelDelta * range / availablePixels / 10.0;
        if (option.upsideDown) valueDelta = -valueDelta;

        const int position = std::clamp(
            m_pressValue + static_cast<int>(std::lround(valueDelta)),
            minimum(), maximum());
        setSliderPosition(position);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_dragging = false;
        QSlider::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            QSlider::keyPressEvent(event);
            return;
        }

        int direction = 0;
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Down:
            direction = -1;
            break;
        case Qt::Key_Right:
        case Qt::Key_Up:
            direction = 1;
            break;
        default:
            QSlider::keyPressEvent(event);
            return;
        }

        if (invertedControls()) direction = -direction;
        setValue(std::clamp(value() + direction * m_fineStep,
                            minimum(), maximum()));
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override {
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            QSlider::wheelEvent(event);
            return;
        }

        const QPoint angle = event->angleDelta();
        const int delta = angle.y() != 0 ? angle.y() : angle.x();
        if (delta == 0) {
            event->ignore();
            return;
        }

        int direction = delta > 0 ? 1 : -1;
        if (invertedControls()) direction = -direction;
        setValue(std::clamp(value() + direction * m_fineStep,
                            minimum(), maximum()));
        event->accept();
    }

private:
    int m_fineStep = 1;
    bool m_dragging = false;
    QPointF m_pressPosition;
    int m_pressValue = 0;
};

} // namespace

InspectorSection::InspectorSection(const QString& title, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("inspectorSection"));
    setProperty("component", QStringLiteral("inspectorSection"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("inspectorSectionHeader"));
    header->setProperty("role", QStringLiteral("sectionHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(StyleTokens::Spacing::kBase);

    m_titleLabel = new QLabel(title, header);
    m_titleLabel->setObjectName(QStringLiteral("inspectorSectionTitle"));
    m_titleLabel->setProperty(
        StyleTokens::Properties::kTextRole,
        QString::fromLatin1(StyleTokens::Properties::kTitle));
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_resetButton = new QPushButton(QString::fromUtf8("重置"), header);
    m_resetButton->setObjectName(QStringLiteral("inspectorSectionReset"));
    m_resetButton->setProperty(
        StyleTokens::Properties::kVariant,
        QString::fromLatin1(StyleTokens::Properties::kGhost));
    m_resetButton->setProperty("role", QStringLiteral("sectionReset"));
    m_resetButton->setAutoDefault(false);
    m_resetButton->setDefault(false);
    m_resetButton->setFocusPolicy(Qt::StrongFocus);
    m_resetButton->hide();

    headerLayout->addWidget(m_titleLabel);
    headerLayout->addWidget(m_resetButton);
    rootLayout->addWidget(header);

    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("inspectorSectionContent"));
    content->setProperty("role", QStringLiteral("sectionContent"));
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(
        0, StyleTokens::Spacing::kBase,
        0, StyleTokens::Spacing::kControlGap);
    m_contentLayout->setSpacing(StyleTokens::Spacing::kControlGap);
    rootLayout->addWidget(content);

    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("inspectorSectionDivider"));
    divider->setProperty(
        StyleTokens::Properties::kUiRole,
        QString::fromLatin1(StyleTokens::Properties::kSeparator));
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setAccessibleName(QString::fromUtf8("分区分隔线"));
    rootLayout->addWidget(divider);

    connect(m_resetButton, &QPushButton::clicked,
            this, &InspectorSection::resetRequested);
    updateAccessibleNames();
}

QString InspectorSection::title() const {
    return m_titleLabel->text();
}

void InspectorSection::setTitle(const QString& title) {
    m_titleLabel->setText(title);
    updateAccessibleNames();
}

QVBoxLayout* InspectorSection::contentLayout() const {
    return m_contentLayout;
}

void InspectorSection::addWidget(QWidget* widget) {
    if (widget) m_contentLayout->addWidget(widget);
}

void InspectorSection::addLayout(QLayout* layout) {
    if (layout) m_contentLayout->addLayout(layout);
}

QPushButton* InspectorSection::resetButton() const {
    return m_resetButton;
}

void InspectorSection::setResetButtonVisible(bool visible) {
    m_resetButton->setVisible(visible);
}

void InspectorSection::setResetButtonText(const QString& text) {
    m_resetButton->setText(text);
    updateAccessibleNames();
}

void InspectorSection::updateAccessibleNames() {
    const QString sectionTitle = m_titleLabel->text();
    setAccessibleName(sectionTitle);
    m_titleLabel->setAccessibleName(sectionTitle);
    m_resetButton->setAccessibleName(
        QString::fromUtf8("%1：%2").arg(m_resetButton->text(), sectionTitle));
}

ValueSlider::ValueSlider(int minimum, int maximum, int value, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("valueSlider"));
    setProperty("component", QStringLiteral("valueSlider"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(StyleTokens::Spacing::kBase);

    auto* fineSlider = new FineSlider(this);
    m_slider = fineSlider;
    m_slider->setObjectName(QStringLiteral("valueSliderControl"));
    m_slider->setProperty("component", QStringLiteral("valueSliderControl"));
    m_slider->setFocusPolicy(Qt::StrongFocus);
    m_slider->setRange(minimum, maximum);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setObjectName(QStringLiteral("valueSliderValue"));
    m_valueLabel->setProperty(
        StyleTokens::Properties::kTextRole,
        QString::fromLatin1(StyleTokens::Properties::kMono));
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setFixedWidth(StyleTokens::Controls::kSliderValueWidth);
    m_valueLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_valueLabel->setFocusPolicy(Qt::NoFocus);

    layout->addWidget(m_slider, 1);
    layout->addWidget(m_valueLabel);

    connect(m_slider, &QSlider::valueChanged, this, [this](int currentValue) {
        updateValueLabel(currentValue);
        emit valueChanged(currentValue);
    });
    connect(m_slider, &QSlider::sliderMoved,
            this, &ValueSlider::sliderMoved);
    connect(m_slider, &QSlider::sliderPressed,
            this, &ValueSlider::sliderPressed);
    connect(m_slider, &QSlider::sliderReleased,
            this, &ValueSlider::sliderReleased);

    setAccessibleName(QString::fromUtf8("参数值"));
    setValue(value);
    updateValueLabel(m_slider->value());
}

int ValueSlider::minimum() const { return m_slider->minimum(); }
int ValueSlider::maximum() const { return m_slider->maximum(); }
int ValueSlider::value() const { return m_slider->value(); }
int ValueSlider::singleStep() const { return m_slider->singleStep(); }

int ValueSlider::fineStep() const {
    return static_cast<FineSlider*>(m_slider)->fineStep();
}

void ValueSlider::setRange(int minimum, int maximum) {
    m_slider->setRange(minimum, maximum);
    updateValueLabel(m_slider->value());
}

void ValueSlider::setMinimum(int minimum) {
    m_slider->setMinimum(minimum);
    updateValueLabel(m_slider->value());
}

void ValueSlider::setMaximum(int maximum) {
    m_slider->setMaximum(maximum);
    updateValueLabel(m_slider->value());
}

void ValueSlider::setValue(int value) {
    m_slider->setValue(value);
    updateValueLabel(m_slider->value());
}

void ValueSlider::setSingleStep(int step) {
    m_slider->setSingleStep(std::max(1, step));
}

void ValueSlider::setPageStep(int step) {
    m_slider->setPageStep(std::max(1, step));
}

void ValueSlider::setFineStep(int step) {
    static_cast<FineSlider*>(m_slider)->setFineStep(step);
}

void ValueSlider::setPrefix(const QString& prefix) {
    m_prefix = prefix;
    updateValueLabel(value());
}

void ValueSlider::setSuffix(const QString& suffix) {
    m_suffix = suffix;
    updateValueLabel(value());
}

void ValueSlider::setShowPositiveSign(bool show) {
    m_showPositiveSign = show;
    updateValueLabel(value());
}

void ValueSlider::setValueFormatter(ValueFormatter formatter) {
    m_formatter = std::move(formatter);
    updateValueLabel(value());
}

void ValueSlider::setValueLabelWidth(int width) {
    m_valueLabel->setFixedWidth(std::max(0, width));
}

void ValueSlider::setAccessibleName(const QString& name) {
    QWidget::setAccessibleName(name);
    m_slider->setAccessibleName(name);
    m_slider->setAccessibleDescription(
        QString::fromUtf8("按住 Shift 可用十分之一灵敏度微调"));
    m_valueLabel->setAccessibleName(QString::fromUtf8("%1当前值").arg(name));
}

QSlider* ValueSlider::slider() const { return m_slider; }
QLabel* ValueSlider::valueLabel() const { return m_valueLabel; }

void ValueSlider::updateValueLabel(int value) {
    QString text;
    if (m_formatter) {
        text = m_formatter(value);
    } else {
        const QString sign = m_showPositiveSign && value > 0
            ? QStringLiteral("+") : QString();
        text = m_prefix + sign + QString::number(value) + m_suffix;
    }
    m_valueLabel->setText(text);
    m_slider->setAccessibleDescription(
        QString::fromUtf8("当前值 %1。按住 Shift 可用十分之一灵敏度微调").arg(text));
}

CompactToggle::CompactToggle(const QString& text, QWidget* parent)
    : QCheckBox(text, parent),
      m_animation(new QPropertyAnimation(this, "thumbPosition", this)) {
    setObjectName(QStringLiteral("compactToggle"));
    setProperty("component", QStringLiteral("compactToggle"));
    setProperty(
        StyleTokens::Properties::kControlRole,
        QString::fromLatin1(StyleTokens::Properties::kToggle));
    setProperty("state", QStringLiteral("off"));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setTristate(false);
    setMinimumHeight(StyleTokens::Controls::kCompactHeight);
    if (!text.isEmpty()) setAccessibleName(text);
    setAccessibleDescription(QString::fromUtf8("按空格键切换"));

    m_trackOffColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kBackgroundOverlay);
    m_trackOnColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kAccentDim);
    m_thumbOffColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kTextFaint);
    m_thumbOnColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kAccent);
    m_focusColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kAccent);
    m_textColor = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kTextPrimary);

    m_animation->setDuration(StyleTokens::Motion::kFastMs);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(this, &QCheckBox::toggled, this, [this](bool checked) {
        setProperty("state", checked ? QStringLiteral("on")
                                      : QStringLiteral("off"));
        style()->unpolish(this);
        style()->polish(this);
        animateToCheckedState(checked);
    });
}

QSize CompactToggle::sizeHint() const {
    const int textWidth = text().isEmpty() ? 0
        : fontMetrics().horizontalAdvance(text()) + StyleTokens::Spacing::kBase;
    return QSize(textWidth + StyleTokens::Controls::kToggleWidth,
                 StyleTokens::Controls::kCompactHeight);
}

QSize CompactToggle::minimumSizeHint() const {
    return QSize(StyleTokens::Controls::kToggleWidth,
                 StyleTokens::Controls::kCompactHeight);
}

QColor CompactToggle::trackOffColor() const { return m_trackOffColor; }
QColor CompactToggle::trackOnColor() const { return m_trackOnColor; }
QColor CompactToggle::thumbOffColor() const { return m_thumbOffColor; }
QColor CompactToggle::thumbOnColor() const { return m_thumbOnColor; }
QColor CompactToggle::focusColor() const { return m_focusColor; }
QColor CompactToggle::textColor() const { return m_textColor; }

void CompactToggle::setTrackOffColor(const QColor& color) {
    m_trackOffColor = color;
    update();
}

void CompactToggle::setTrackOnColor(const QColor& color) {
    m_trackOnColor = color;
    update();
}

void CompactToggle::setThumbOffColor(const QColor& color) {
    m_thumbOffColor = color;
    update();
}

void CompactToggle::setThumbOnColor(const QColor& color) {
    m_thumbOnColor = color;
    update();
}

void CompactToggle::setFocusColor(const QColor& color) {
    m_focusColor = color;
    update();
}

void CompactToggle::setTextColor(const QColor& color) {
    m_textColor = color;
    update();
}

void CompactToggle::setAnimationEnabled(bool enabled) {
    m_animationEnabled = enabled;
    if (!enabled) {
        m_animation->stop();
        setThumbPosition(isChecked() ? 1.0 : 0.0);
    }
}

void CompactToggle::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect bounds = contentsRect();
    const QRectF track(
        bounds.right() - StyleTokens::Controls::kToggleWidth + 1,
        bounds.center().y() - StyleTokens::Controls::kToggleHeight / 2.0,
        StyleTokens::Controls::kToggleWidth,
        StyleTokens::Controls::kToggleHeight);

    const QPalette::ColorGroup group = isEnabled()
        ? QPalette::Active : QPalette::Disabled;
    QColor trackOff = resolvedColor(m_trackOffColor, QPalette::Button, group);
    QColor trackOn = resolvedColor(m_trackOnColor, QPalette::Highlight, group);
    QColor thumbOff = resolvedColor(m_thumbOffColor, QPalette::Mid, group);
    QColor thumbOn = resolvedColor(m_thumbOnColor, QPalette::Highlight, group);

    if (!isEnabled()) {
        trackOff = StyleTokens::Colors::fromHex(StyleTokens::Colors::kLineSubtle);
        trackOn = trackOff;
        thumbOff = StyleTokens::Colors::fromHex(
            StyleTokens::Colors::kBackgroundOverlay);
        thumbOn = thumbOff;
    }

    const qreal position = std::clamp(m_thumbPosition, 0.0, 1.0);
    const QColor trackColor = interpolateColor(trackOff, trackOn, position);
    const QColor thumbColor = interpolateColor(thumbOff, thumbOn, position);

    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(
        track, StyleTokens::Controls::kToggleHeight / 2.0,
        StyleTokens::Controls::kToggleHeight / 2.0);

    constexpr qreal inset =
        (StyleTokens::Controls::kToggleHeight - kToggleThumbDiameter) / 2.0;
    const qreal leftCenter = track.left() + inset + kToggleThumbDiameter / 2.0;
    const qreal rightCenter = track.right() - inset - kToggleThumbDiameter / 2.0;
    const qreal thumbCenter = leftCenter + (rightCenter - leftCenter) * position;
    painter.setBrush(thumbColor);
    painter.drawEllipse(QPointF(thumbCenter, track.center().y()),
                        kToggleThumbDiameter / 2.0,
                        kToggleThumbDiameter / 2.0);

    if (!text().isEmpty()) {
        const QRect textRect(bounds.left(), bounds.top(),
                             std::max(0, static_cast<int>(track.left())
                                             - bounds.left()
                                             - StyleTokens::Spacing::kBase),
                             bounds.height());
        const QColor labelColor = isEnabled()
            ? resolvedColor(m_textColor, QPalette::WindowText, group)
            : StyleTokens::Colors::fromHex(StyleTokens::Colors::kTextFaint);
        painter.setPen(labelColor);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight,
                                                  textRect.width()));
    }

    if (hasFocus()) {
        const QColor ring = resolvedColor(
            m_focusColor, QPalette::Highlight, QPalette::Active);
        QPen pen(ring, 1.5);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-2, -2, 2, 2),
                                StyleTokens::Controls::kToggleHeight / 2.0 + 2,
                                StyleTokens::Controls::kToggleHeight / 2.0 + 2);
    }
}

bool CompactToggle::hitButton(const QPoint& position) const {
    return contentsRect().contains(position);
}

void CompactToggle::changeEvent(QEvent* event) {
    QCheckBox::changeEvent(event);
    switch (event->type()) {
    case QEvent::EnabledChange:
    case QEvent::FontChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
        updateGeometry();
        update();
        break;
    default:
        break;
    }
}

qreal CompactToggle::thumbPosition() const {
    return m_thumbPosition;
}

void CompactToggle::setThumbPosition(qreal position) {
    m_thumbPosition = std::clamp(position, 0.0, 1.0);
    update();
}

void CompactToggle::animateToCheckedState(bool checked) {
    const qreal target = checked ? 1.0 : 0.0;
    if (!m_animationEnabled || !isVisible()) {
        m_animation->stop();
        setThumbPosition(target);
        return;
    }

    m_animation->stop();
    m_animation->setStartValue(m_thumbPosition);
    m_animation->setEndValue(target);
    m_animation->start();
}

QColor CompactToggle::resolvedColor(const QColor& configured,
                                    QPalette::ColorRole fallback,
                                    QPalette::ColorGroup group) const {
    return configured.isValid() ? configured : palette().color(group, fallback);
}
