//
// LayoutSavePath.cpp - レイアウト保存先パスの解決
//
#include "LayoutSavePath.h"

#include <algorithm>
#include <cctype>

#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {

std::string Trim(const std::string& value) {
    const std::string::const_iterator first = std::find_if_not(
        value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
    const std::string::const_reverse_iterator last = std::find_if_not(
        value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; });
    return first == value.end() ? std::string() : std::string(first, last.base());
}

} // namespace

std::filesystem::path ResolveLayoutSavePath(const std::string& inputUtf8,
                                            const std::filesystem::path& saveDirectory) {
    const std::string trimmedInput = Trim(inputUtf8);
    if (trimmedInput.empty()) return {};

    std::filesystem::path resolvedPath(rs::platform_text::Utf8ToWide(trimmedInput));
    std::string extension = resolvedPath.extension().string();
    extension = rs::platform_text::ToAsciiLower(extension);
    if (extension != ".rsl") resolvedPath += L".rsl";
    if (!resolvedPath.is_absolute()) resolvedPath = saveDirectory / resolvedPath;
    return resolvedPath;
}

} // namespace rs::app
