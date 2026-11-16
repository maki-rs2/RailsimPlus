#include "Encoding.h"

#include <filesystem>
#include <optional>

#include "../file/FileRead.h"
#include "EncodingBackend.h"

namespace rs::platform_text {

std::string Cp932ToUtf8(const std::string& sjis) {
    return WideToUtf8(Cp932ToWide(sjis));
}

std::string Utf8ToCp932(const std::string& utf8) {
    return WideToCp932(Utf8ToWide(utf8));
}

bool HasUtf8Bom(const std::string& bytes) {
    return bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
           static_cast<unsigned char>(bytes[1]) == 0xBB &&
           static_cast<unsigned char>(bytes[2]) == 0xBF;
}

std::string ReadFileAsUtf8(const std::wstring& path, bool& ok) {
    ok = false;
    const std::optional<std::string> loaded =
        rs::platform_file::ReadFileText(std::filesystem::path(path));
    if (!loaded) return {};
    const std::string& raw = *loaded;
    ok = true;
    if (HasUtf8Bom(raw)) {
        return raw.substr(3); // 既に UTF-8。BOM を除去。
    }
    return Cp932ToUtf8(raw);
}

} // namespace rs::platform_text
