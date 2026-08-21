#pragma once

#include <QCheckBox>
#include <QColor>
#include <QWidget>

#include <functional>

class QLabel;
class QLayout;
class QPropertyAnimation;
class QPushButton;
class QSlider;
class QVBoxLayout;

// 无背景卡片的检查器分区。标题、重置按钮和分隔线均暴露稳定的 QSS 钩子。
class InspectorSection final : public QWidget {
    Q_OBJECT

public:
    explicit InspectorSection(const QString& title, QWidget* parent = nullptr);

    QString title() const;
    void setTitle(const QString& title);

    QVBoxLayout* contentLayout() const;
    void addWidget(QWidget* widget);
    void addLayout(QLayout* layout);

    QPushButton* resetButton() const;
    void setResetButtonVisible(bool visible);
    void setResetButtonText(const QString& text);

signals:
    void resetRequested();

private:
    void updateAccessibleNames();

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_resetButton = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
};

// 水平参数滑杆。数值标签始终占位，Shift 拖动时灵敏度降为正常的 1/10。
class ValueSlider final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)

public:
    using ValueFormatter = std::function<QString(int)>;

    explicit ValueSlider(int minimum = 0, int maximum = 100, int value = 0,
                         QWidget* parent = nullptr);

    int minimum() const;
    int maximum() const;
    int value() const;
    int singleStep() const;
    int fineStep() const;

    void setRange(int minimum, int maximum);
    void setMinimum(int minimum);
    void setMaximum(int maximum);
    void setValue(int value);
    void setSingleStep(int step);
    void setPageStep(int step);
    void setFineStep(int step);

    void setPrefix(const QString& prefix);
    void setSuffix(const QString& suffix);
    void setShowPositiveSign(bool show);
    void setValueFormatter(ValueFormatter formatter);
    void setValueLabelWidth(int width);
    void setAccessibleName(const QString& name);

    QSlider* slider() const;
    QLabel* valueLabel() const;

signals:
    void valueChanged(int value);
    void sliderMoved(int value);
    void sliderPressed();
    void sliderReleased();

private:
    void updateValueLabel(int value);

    QSlider* m_slider = nullptr;
    QLabel* m_valueLabel = nullptr;
    QString m_prefix;
    QString m_suffix;
    bool m_showPositiveSign = false;
    ValueFormatter m_formatter;
};

// 标签在左、32x18 开关在右的紧凑 Toggle。颜色可由全局 QSS 的 qproperty-* 注入。
class CompactToggle final : public QCheckBox {
    Q_OBJECT
    Q_PROPERTY(QColor trackOffColor READ trackOffColor WRITE setTrackOffColor)
    Q_PROPERTY(QColor trackOnColor READ trackOnColor WRITE setTrackOnColor)
    Q_PROPERTY(QColor thumbOffColor READ thumbOffColor WRITE setThumbOffColor)
    Q_PROPERTY(QColor thumbOnColor READ thumbOnColor WRITE setThumbOnColor)
    Q_PROPERTY(QColor focusColor READ focusColor WRITE setFocusColor)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)
    Q_PROPERTY(qreal thumbPosition READ thumbPosition WRITE setThumbPosition)

public:
    explicit CompactToggle(const QString& text = QString(), QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    QColor trackOffColor() const;
    QColor trackOnColor() const;
    QColor thumbOffColor() const;
    QColor thumbOnColor() const;
    QColor focusColor() const;
    QColor textColor() const;

    void setTrackOffColor(const QColor& color);
    void setTrackOnColor(const QColor& color);
    void setThumbOffColor(const QColor& color);
    void setThumbOnColor(const QColor& color);
    void setFocusColor(const QColor& color);
    void setTextColor(const QColor& color);
    void setAnimationEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool hitButton(const QPoint& position) const override;
    void changeEvent(QEvent* event) override;

private:
    qreal thumbPosition() const;
    void setThumbPosition(qreal position);
    void animateToCheckedState(bool checked);
    QColor resolvedColor(const QColor& configured, QPalette::ColorRole fallback,
                         QPalette::ColorGroup group = QPalette::Active) const;

    QColor m_trackOffColor;
    QColor m_trackOnColor;
    QColor m_thumbOffColor;
    QColor m_thumbOnColor;
    QColor m_focusColor;
    QColor m_textColor;
    qreal m_thumbPosition = 0.0;
    bool m_animationEnabled = true;
    QPropertyAnimation* m_animation = nullptr;
};
