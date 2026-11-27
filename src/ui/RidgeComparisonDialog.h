#pragma once

#include <QDialog>
#include <QImage>

#include <array>

class RidgeComparisonDialog final : public QDialog {
public:
    // 验证本地对照清单的 schema/version，只读取同目录固定命名的三张裁片。
    static bool openForManifest(const QString& path, QWidget* parent, QString& error);

private:
    RidgeComparisonDialog(std::array<QImage, 3> crops, QWidget* parent);
};
