#pragma once

#include <QStringList>
#include <QWidget>
#include "core/PresetManager.h"
#include "core/SkyGroundMask.h"

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
class QTabWidget;
enum class ProcessingScene;

class ParamsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamsPanel(QWidget* parent = nullptr);

    QString alignMethod() const;
    QString stackMethod() const;
    double kappaValue() const;
    bool autoRejectLowQualityFrames() const;
    bool photometricNormalizationEnabled() const;
    bool dewarpEnabled() const;
    int dewarpStrength() const;
    bool noiseReductionEnabled() const;
    int noiseReductionStrength() const;
    bool modifiedCameraColorEnabled() const;
    bool stretchEnabled() const;
    bool starReduceEnabled() const;
    int starReduceStrength() const;
    QString outputFormat() const;
    QString outputPath() const;
    QString selectedReferenceFrame() const;

    void updateRefFrameList(const QStringList& fileNames);
    void recommendStackMethod(int frameCount);
    void saveCurrentSettings();
    void setOutputPath(const QString& path);

    bool skyGroundSeparationEnabled() const;
    SkyGroundMask::Mode skyGroundMode() const;
    QString userMaskPath() const;
    int featherRadius() const;
    QString groundStackMethod() const;
    int groundDetailStrength() const;
    int timelapseWindowSize() const;
    int timelapseStrength() const;
    int timelapseMotionProtection() const;
    bool timelapseProtectGround() const;
    QStringList darkFramePaths() const;
    QStringList flatFramePaths() const;
    QStringList biasFramePaths() const;
    QString upstreamSignature() const;
    QString finishingSignature() const;
    QString processingSignature() const;
    void setMaskPreview(const std::vector<uint8_t>& mask, int w, int h);
    void setDetectMaskEnabled(bool enabled);
    void applySceneProfile(ProcessingScene scene);

signals:
    void paramsChanged();  // 参数发生任何变化时触发
    void maskPreviewRequested(); // 用户点击"检测地景"时发射

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

    void updateSkyGroundControls();
    void updateStackMethodDescription();
    void markPresetCustom();
    void importCalibrationFrames(QStringList& paths, QLabel* countLabel,
                                 QPushButton* clearButton,
                                 const QString& dialogTitle);
    void clearCalibrationFrames(QStringList& paths, QLabel* countLabel,
                                QPushButton* clearButton);
    void updateCalibrationCount(QLabel* label, QPushButton* clearButton,
                                int count);

    // 预设
    QWidget* m_presetBar = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QLabel* m_titleLabel = nullptr;
    QTabWidget* m_tabs = nullptr;

    // 对齐组
    QGroupBox* m_alignGroup = nullptr;
    QComboBox* m_alignMethod = nullptr;
    QComboBox* m_refFrame = nullptr;

    // 堆栈组
    QGroupBox* m_stackGroup = nullptr;
    QComboBox* m_stackAlgorithm = nullptr;
    QLabel* m_stackMethodDescription = nullptr;
    QLabel* m_kappaNameLabel = nullptr;
    QSlider* m_kappaSlider = nullptr;
    QLabel* m_kappaLabel = nullptr;
    QCheckBox* m_autoRejectQualityCheck = nullptr;
    QCheckBox* m_photometricCheck = nullptr;

    // 延时序列组
    QGroupBox* m_timelapseGroup = nullptr;
    QComboBox* m_timelapseWindow = nullptr;
    QSlider* m_timelapseStrengthSlider = nullptr;
    QLabel* m_timelapseStrengthLabel = nullptr;
    QSlider* m_timelapseMotionProtectionSlider = nullptr;
    QLabel* m_timelapseMotionProtectionLabel = nullptr;
    QCheckBox* m_timelapseProtectGroundCheck = nullptr;

    // 深空校准帧仅在当前会话内保存，避免下次启动引用已经移动的文件。
    QGroupBox* m_calibrationGroup = nullptr;
    QLabel* m_darkFrameCount = nullptr;
    QLabel* m_flatFrameCount = nullptr;
    QLabel* m_biasFrameCount = nullptr;
    QPushButton* m_darkFrameClear = nullptr;
    QPushButton* m_flatFrameClear = nullptr;
    QPushButton* m_biasFrameClear = nullptr;
    QStringList m_darkFramePaths;
    QStringList m_flatFramePaths;
    QStringList m_biasFramePaths;

    // 自动优化组
    QGroupBox* m_optimizeGroup = nullptr;
    QCheckBox* m_dewarpCheck = nullptr;
    QSlider* m_dewarpSlider = nullptr;
    QLabel* m_dewarpLabel = nullptr;
    QCheckBox* m_noiseReductionCheck = nullptr;
    QSlider* m_noiseReductionSlider = nullptr;
    QLabel* m_noiseReductionLabel = nullptr;
    QCheckBox* m_modifiedCameraColorCheck = nullptr;
    QCheckBox* m_stretchCheck = nullptr;

    // 缩星组
    QWidget* m_starReduceGroup = nullptr;
    QCheckBox* m_starReduceCheck = nullptr;
    QSlider* m_starReduceSlider = nullptr;
    QLabel* m_starReduceLabel = nullptr;

    // 输出组
    QWidget* m_outputGroup = nullptr;
    QComboBox* m_outputFormat = nullptr;
    QLineEdit* m_outputPath = nullptr;

    // 底部按钮
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_savePresetBtn = nullptr;

    // 天地分离组
    QCheckBox* m_skyGroundCheck = nullptr;
    QComboBox* m_skyGroundMode = nullptr;
    QLabel* m_skyGroundModeLabel = nullptr;
    QPushButton* m_detectMaskBtn = nullptr;
    QPushButton* m_importMaskBtn = nullptr;
    QLabel* m_maskPathLabel = nullptr;
    QSlider* m_featherSlider = nullptr;
    QLabel* m_featherLabel = nullptr;
    QLabel* m_featherNameLabel = nullptr;
    QComboBox* m_groundStackMethod = nullptr;
    QLabel* m_groundStackNameLabel = nullptr;
    QSlider* m_groundDetailSlider = nullptr;
    QLabel* m_groundDetailLabel = nullptr;
    QLabel* m_groundDetailNameLabel = nullptr;
    QString m_userMaskPath;

    // Debounce 定时器
    QTimer* m_debounceTimer = nullptr;
    
    // 用户是否手动修改过堆栈算法（避免智能推荐覆盖用户选择）
    bool m_userChangedStackMethod = false;
};
