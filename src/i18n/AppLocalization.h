#pragma once

#include <QString>

class QCoreApplication;

namespace AppLocalization {

inline constexpr const char* kSimplifiedChinese = "zh_CN";
inline constexpr const char* kEnglish = "en_US";

// Installs the application and Qt widget translators before any window is
// created. Existing users deliberately default to Simplified Chinese.
void install(QCoreApplication& application);

QString preferredLanguage();
void setPreferredLanguage(const QString& languageCode);
bool isEnglish();

// Starts a fresh process with the same command-line arguments. Language
// changes use a restart so every hand-built widget and worker shares one
// coherent locale instead of mixing old and new labels.
bool restartApplication();

}  // namespace AppLocalization
