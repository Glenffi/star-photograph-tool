#pragma once

#include <QWidget>
#include "core/PresetManager.h"

class QScrollArea;
class QVBoxLayout;
class QComboBox;
class QSlider;
class QCheckBox;
class QGroupBox;
class QPushButton;
class QLabel;
class QTimer;
class QLineEdit;

class ParamsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamsPanel(QWidget* parent = nullptr);

    QString alignMethod() const;
    QString stackMethod() const;
    double kappaValue() const;
    bool dewarpEnabled() const;
    int dewarpStrength() const;
    bool stretchEnabled() const;
    bool starReduceEnabled() const;
    int starReduceStrength() const;
    QString outputFormat() const;
    QString outputPath() const;

    void updateRefFrameList(const QStringList& fileNames);
    void recommendStackMethod(int frameCount);
    void saveCurrentSettings();

signals:
    void paramsChanged();  // 参数发生任何变化时触发

private slots:
    void onGroupToggled(bool checked);
    void onSliderValueChanged(int value);
    void onSliderReleased();
    void onComboChanged(int index);
    void onCheckChanged(int state);
    void onRestoreDefaults();
    void onSavePreset();
    void onPresetChanged(int index);
    void emitParamsChanged();

private:
    void setupUI();
    void loadPreset();
    void applyPreset(const Preset& preset);
    void loadCustomPresets();
    QGroupBox* createCollapsibleGroup(const QString& title, bool expanded = true);
    QSlider* createSlider(int min, int max, int value, const QString& suffix = QString());

    // 预设
    QComboBox* m_presetCombo = nullptr;

    // 对齐组
    QGroupBox* m_alignGroup = nullptr;
    QComboBox* m_alignMethod = nullptr;
    QComboBox* m_refFrame = nullptr;

    // 堆栈组
    QGroupBox* m_stackGroup = nullptr;
    QComboBox* m_stackAlgorithm = nullptr;
    QSlider* m_kappaSlider = nullptr;
    QLabel* m_kappaLabel = nullptr;

    // 自动优化组
    QGroupBox* m_optimizeGroup = nullptr;
    QCheckBox* m_dewarpCheck = nullptr;
    QSlider* m_dewarpSlider = nullptr;
    QCheckBox* m_stretchCheck = nullptr;

    // 缩星组
    QWidget* m_starReduceGroup = nullptr;
    QCheckBox* m_starReduceCheck = nullptr;
    QSlider* m_starReduceSlider = nullptr;

    // 输出组
    QWidget* m_outputGroup = nullptr;
    QComboBox* m_outputFormat = nullptr;
    QComboBox* m_colorSpace = nullptr;
    QLineEdit* m_outputPath = nullptr;

    // 底部按钮
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_savePresetBtn = nullptr;

    // Debounce 定时器
    QTimer* m_debounceTimer = nullptr;
    
    // 用户是否手动修改过堆栈算法（避免智能推荐覆盖用户选择）
    bool m_userChangedStackMethod = false;
};
