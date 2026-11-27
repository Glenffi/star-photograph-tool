#pragma once

#include <QString>
#include <QStringList>

class QFileDialog;
class QWidget;

namespace FileDialogUtils {

// Qt's macOS template icons are nearly black inside Nebulens' dark non-native
// file dialog. Replace only the file-navigation toolbar icons and preserve the
// current platform style, translated labels, focus behavior, and shortcuts.
void applyToolbarIcons(QFileDialog& dialog);

QStringList getOpenFileNames(QWidget* parent, const QString& caption,
                             const QString& directory,
                             const QString& filter);
QString getOpenFileName(QWidget* parent, const QString& caption,
                        const QString& directory, const QString& filter);
QString getSaveFileName(QWidget* parent, const QString& caption,
                        const QString& suggested, const QString& filter,
                        const QString& defaultSuffix);
QString getExistingDirectory(QWidget* parent, const QString& caption,
                             const QString& directory);

}  // namespace FileDialogUtils
