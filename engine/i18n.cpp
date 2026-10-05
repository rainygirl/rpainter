#include "i18n.h"
#include <cstring>
#include <unordered_map>

namespace {
struct Entry { const char *t[5]; }; // ko, en, ja, it, fr
const Entry TABLE[] = {
#include "i18n_table.inc"
};
const char *CODES[5] = {"ko", "en", "ja", "it", "fr"};
int gLang = 0;
std::string gCode = "ko";

const std::unordered_map<std::string, const Entry *> &index() {
    static const std::unordered_map<std::string, const Entry *> m = [] {
        std::unordered_map<std::string, const Entry *> x;
        for (const Entry &e : TABLE) x[e.t[0]] = &e;
        return x;
    }();
    return m;
}
bool hasHangul(const std::string &s) {
    for (size_t i = 0; i + 2 < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0xea && c <= 0xed) return true; // UTF-8 lead bytes of the Hangul syllable block
    }
    return false;
}
// translated strings handed out as const char* must stay alive: composed results are interned here
const char *intern(const std::string &s) {
    static std::unordered_map<std::string, std::string> pool;
    return pool.emplace(s, s).first->second.c_str();
}
bool lookup(const std::string &key, std::string &out) {
    const auto it = index().find(key);
    if (it == index().end()) return false;
    out = it->second->t[gLang];
    return true;
}
std::string translate(const std::string &key) {
    std::string out;
    if (lookup(key, out)) return out;
    // surrounding whitespace, e.g. " 복사" or "저장됨: "
    size_t a = 0, b = key.size();
    while (a < b && key[a] == ' ') a++;
    while (b > a && key[b - 1] == ' ') b--;
    if (a > 0 || b < key.size()) return key.substr(0, a) + translate(key.substr(a, b - a)) + key.substr(b);
    // trailing " %1"
    if (key.size() > 3 && key.compare(key.size() - 3, 3, " %1") == 0) return translate(key.substr(0, key.size() - 3)) + " %1";
    // trailing parenthetical without Korean in it: "이동 (V)", "지우기 (Delete)"
    if (!key.empty() && key.back() == ')') {
        const size_t open = key.rfind(" (");
        if (open != std::string::npos && open > 0) {
            const std::string paren = key.substr(open);
            if (!hasHangul(paren) && lookup(key.substr(0, open), out)) return out + paren;
        }
    }
    return key;
}
}

void setLanguage(const std::string &locale) {
    std::string two = locale.substr(0, 2);
    for (char &c : two) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    gLang = 1; // English unless the language is one of ours
    for (int i = 0; i < 5; i++) if (two == CODES[i]) gLang = i;
    gCode = CODES[gLang];
}
const std::string &language() { return gCode; }
const char *trText(const char *ko) {
    if (gLang == 0 || !ko || !*ko) return ko;
    return intern(translate(ko));
}
