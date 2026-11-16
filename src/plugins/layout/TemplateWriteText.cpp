#include "TemplateWriteText.h"

#include "../../platform/text/Encoding.h"

namespace rs::legacy_layout {

bool IsWritableTemplateString(const std::string& utf8) {
    for (const char c : utf8) {
        if (c == '"' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    }
    // 変換できない文字は '?' に化けるため、往復して同じ文字列に戻るかで見分ける。
    return rs::platform_text::Cp932ToUtf8(rs::platform_text::Utf8ToCp932(utf8)) == utf8;
}

std::string QuoteTemplateString(const std::string& utf8) {
    return "\"" + utf8 + "\"";
}

}  // namespace rs::legacy_layout
