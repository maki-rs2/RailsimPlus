//
// Localization.cpp - 埋め込みカタログの解析と翻訳キーの解決。
//
#include "Localization.h"

#include <nlohmann/json.hpp>

namespace rs::app {
namespace {

void AddDiagnostic(std::vector<std::string>& outDiagnostics, const std::string& message) {
    outDiagnostics.push_back(message);
}

} // namespace

Localization Localization::FromJson(std::string_view jsonText, std::vector<std::string>& outDiagnostics) {
    Localization localization;
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonText);
    } catch (const nlohmann::json::exception& exception) {
        AddDiagnostic(outDiagnostics, std::string("Localization catalog JSON parse failed: ") + exception.what());
        return localization;
    }

    if (!root.is_object()) {
        AddDiagnostic(outDiagnostics, "Localization catalog root must be an object.");
        return localization;
    }

    for (nlohmann::json::const_iterator entry = root.begin(); entry != root.end(); ++entry) {
        if (!entry.value().is_object()) {
            AddDiagnostic(outDiagnostics, "Localization key '" + entry.key() + "' must contain an object.");
            continue;
        }

        std::map<Language, std::string> translations;
        for (const Language language : {Language::Japanese, Language::English}) {
            const char* languageCode = LanguageCode(language);
            const nlohmann::json::const_iterator value = entry.value().find(languageCode);
            if (value == entry.value().end()) {
                AddDiagnostic(outDiagnostics, "Localization key '" + entry.key() + "' is missing '" + languageCode + "'.");
                continue;
            }
            if (!value->is_string()) {
                AddDiagnostic(outDiagnostics, "Localization key '" + entry.key() + "' has a non-string '" + languageCode + "' value.");
                continue;
            }
            const std::string text = value->get<std::string>();
            if (text.empty()) {
                AddDiagnostic(outDiagnostics, "Localization key '" + entry.key() + "' has an empty '" + languageCode + "' value.");
                continue;
            }
            translations.emplace(language, text);
        }
        localization.catalog_.emplace(entry.key(), std::move(translations));
    }
    return localization;
}

void Localization::SetLanguage(Language language) {
    currentLanguage_ = language;
}

Language Localization::CurrentLanguage() const {
    return currentLanguage_;
}

std::string Localization::Text(std::string_view key) const {
    const std::string keyString(key);
    const std::map<std::string, std::map<Language, std::string>>::const_iterator entry = catalog_.find(keyString);
    if (entry == catalog_.end()) return keyString;

    const std::map<Language, std::string>::const_iterator current = entry->second.find(currentLanguage_);
    if (current != entry->second.end() && !current->second.empty()) return current->second;

    const std::map<Language, std::string>::const_iterator english = entry->second.find(Language::English);
    if (english != entry->second.end() && !english->second.empty()) return english->second;

    return keyString;
}

} // namespace rs::app
