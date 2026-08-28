#include "i18n/AppLocalization.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

QString standardButtonText(QFileDialog& dialog,
                           QDialogButtonBox::StandardButton button) {
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    if (!buttonBox || !buttonBox->button(button)) return {};
    return buttonBox->button(button)->text();
}

std::unique_ptr<QFileDialog> localizedOpenDialog() {
    auto dialog = std::make_unique<QFileDialog>();
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->setFileMode(QFileDialog::ExistingFiles);
    dialog->setWindowTitle(QCoreApplication::translate(
        "Nebulens", "选择 RAW 文件"));
    dialog->setNameFilter(QCoreApplication::translate(
        "Nebulens",
        "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef *.cr3);;所有文件 (*)"));
    dialog->ensurePolished();
    return dialog;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(),
          "Temporary settings directory should be available");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());

    AppLocalization::setPreferredLanguage(
        QString::fromLatin1(AppLocalization::kSimplifiedChinese));
    AppLocalization::install(app);
    auto chineseDialog = localizedOpenDialog();
    check(chineseDialog->testOption(QFileDialog::DontUseNativeDialog),
          "Localized file dialogs should use the Qt implementation");
    check(chineseDialog->windowTitle() == QString::fromUtf8("选择 RAW 文件") &&
              standardButtonText(*chineseDialog, QDialogButtonBox::Cancel)
                  .contains(QString::fromUtf8("取消")),
          "Chinese mode should localize the file dialog and standard buttons");

    AppLocalization::setPreferredLanguage(
        QString::fromLatin1(AppLocalization::kEnglish));
    AppLocalization::install(app);
    auto englishDialog = localizedOpenDialog();
    check(englishDialog->windowTitle() == QStringLiteral("Select RAW Files") &&
              standardButtonText(*englishDialog, QDialogButtonBox::Cancel)
                  .contains(QStringLiteral("Cancel")),
          "English mode should localize the file dialog and standard buttons");

    std::cout << "Localization dialog tests passed\n";
    return 0;
}
