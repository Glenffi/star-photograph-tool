#include "FileDialogUtils.h"

#include "StyleTokens.h"
#include "UiAssets.h"

#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <array>

namespace FileDialogUtils {
namespace {

struct ToolbarIcon {
    const char* objectName;
    UiAssets::Glyph glyph;
};

constexpr std::array<ToolbarIcon, 6> kToolbarIcons{{
    {"backButton", UiAssets::Glyph::ChevronLeft},
    {"forwardButton", UiAssets::Glyph::ChevronRight},
    {"toParentButton", UiAssets::Glyph::ArrowUp},
    {"newFolderButton", UiAssets::Glyph::NewFolder},
    {"listModeButton", UiAssets::Glyph::ListView},
    {"detailModeButton", UiAssets::Glyph::DetailView}
}};

void prepareToolbarIcons(QFileDialog& dialog) {
    applyToolbarIcons(dialog);
    // QFileDialog asks the platform style for standard icons again during its
    // first show event. Reapply once the modal event loop starts so those
    // late style-polish icons cannot replace the Nebulens set.
    QTimer::singleShot(0, &dialog, [&dialog]() {
        applyToolbarIcons(dialog);
    });
}

}  // namespace

void applyToolbarIcons(QFileDialog& dialog) {
    const QColor ink = StyleTokens::Colors::fromHex(StyleTokens::Colors::kTextPrimary);
    constexpr int iconSize = 24;
    for (const ToolbarIcon& item : kToolbarIcons) {
        auto* button = dialog.findChild<QToolButton*>(
            QString::fromLatin1(item.objectName));
        if (!button) continue;
        button->setProperty("dialogToolbarIcon", true);
        button->setIcon(UiAssets::controlIcon(item.glyph, ink, iconSize));
        button->setIconSize(QSize(iconSize, iconSize));
        button->setFocusPolicy(Qt::StrongFocus);
        if (button->accessibleName().isEmpty()) button->setAccessibleName(button->toolTip());
        button->setProperty("nebDialogIcon", true);
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

QStringList getOpenFileNames(QWidget* parent, const QString& caption,
                             const QString& directory,
                             const QString& filter) {
    QFileDialog dialog(parent, caption, directory, filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    prepareToolbarIcons(dialog);
    return dialog.exec() == QDialog::Accepted
        ? dialog.selectedFiles() : QStringList{};
}

QString getOpenFileName(QWidget* parent, const QString& caption,
                        const QString& directory, const QString& filter) {
    QFileDialog dialog(parent, caption, directory, filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    prepareToolbarIcons(dialog);
    if (dialog.exec() != QDialog::Accepted) return {};
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString getSaveFileName(QWidget* parent, const QString& caption,
                        const QString& suggested, const QString& filter,
                        const QString& defaultSuffix) {
    QFileDialog dialog(parent, caption, suggested, filter);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(defaultSuffix);
    prepareToolbarIcons(dialog);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
}

QString getExistingDirectory(QWidget* parent, const QString& caption,
                             const QString& directory) {
    QFileDialog dialog(parent, caption, directory);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setLabelText(
        QFileDialog::Accept,
        QCoreApplication::translate("Nebulens", "选择"));
    prepareToolbarIcons(dialog);
    if (dialog.exec() != QDialog::Accepted) return {};
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

}  // namespace FileDialogUtils
