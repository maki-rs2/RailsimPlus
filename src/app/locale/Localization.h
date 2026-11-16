//
// Localization.h - 埋め込みカタログからUI文言を解決するローカライズ基盤。
//
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rs::app {

enum class Language { Japanese, English };

// 言語の短いコード("ja"・"en")。設定ファイルの保存形式と一致する。
inline const char* LanguageCode(Language language) {
    switch (language) {
    case Language::Japanese: return "ja";
    case Language::English: return "en";
    }
    return "en";
}

class Localization {
public:
    static Localization FromJson(std::string_view json, std::vector<std::string>& outDiagnostics);

    void SetLanguage(Language language);
    Language CurrentLanguage() const;
    std::string Text(std::string_view key) const;

private:
    std::map<std::string, std::map<Language, std::string>> catalog_;
    Language currentLanguage_ = Language::English;
};

} // namespace rs::app
