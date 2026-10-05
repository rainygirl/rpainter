// User interface text. Source strings are Korean; translations (en, ja, it, fr) come from i18n/strings.txt.
#pragma once
#include <string>

// "ko", "en", "ja", "it" or "fr". Takes a locale name such as "ja_JP" or "fr-CA";
// anything that is not one of the five supported languages becomes English.
void setLanguage(const std::string &locale);
const std::string &language();
// Translation of a Korean source string in the current language. Unknown strings are returned unchanged.
// A trailing " (X)" (shortcut hints such as "(V)" or "(Ctrl+T)"), a trailing " %1" and surrounding spaces
// do not need their own entries.
const char *trText(const char *ko);
#define TR(s) trText(s)
