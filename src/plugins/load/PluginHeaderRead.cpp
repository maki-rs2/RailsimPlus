#include "PluginHeaderRead.h"

#include <cctype>
#include <string_view>
#include <vector>

#include "../../platform/text/Encoding.h"
#include "../schema/parser/Diagnostics.h"
#include "../schema/parser/Parser.h"
#include "../schema/sidecar/StationSidecar.h"
#include "PluginPaths.h"

namespace rs::legacy_plugin {

namespace {

constexpr std::string_view kHeaderBlockName = "PluginHeader";

bool IsIdentifierChar(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '_' || uch >= 0x80;
}

// posが文字列・コメントの始まりなら、その終わりの次の位置を返す。そうでなければposを返す。
std::size_t SkipStringOrComment(const std::string& text, std::size_t pos) {
    const std::size_t size = text.size();
    if (text[pos] == '"') {
        std::size_t i = pos + 1;
        while (i < size && text[i] != '"') i += (text[i] == '\\' && i + 1 < size) ? 2 : 1;
        return i < size ? i + 1 : size;
    }
    if (text[pos] == '/' && pos + 1 < size && text[pos + 1] == '/') {
        const std::size_t newline = text.find('\n', pos);
        return newline == std::string::npos ? size : newline + 1;
    }
    if (text[pos] == '/' && pos + 1 < size && text[pos + 1] == '*') {
        const std::size_t close = text.find("*/", pos + 2);
        return close == std::string::npos ? size : close + 2;
    }
    return pos;
}

} // namespace

std::size_t FindPluginHeaderEnd(const std::string& utf8) {
    const std::size_t size = utf8.size();
    int depth = 0;
    bool inHeader = false;
    std::size_t i = 0;
    while (i < size) {
        const std::size_t skipped = SkipStringOrComment(utf8, i);
        if (skipped != i) {
            i = skipped;
            continue;
        }
        const char ch = utf8[i];
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (inHeader && depth == 0) return i + 1;
        } else if (depth == 0 && !inHeader && IsIdentifierChar(ch)) {
            std::size_t end = i;
            while (end < size && IsIdentifierChar(utf8[end])) ++end;
            inHeader = std::string_view(utf8).substr(i, end - i) == kHeaderBlockName;
            i = end;
            continue;
        }
        ++i;
    }
    return std::string::npos;
}

PluginHeaderReadResult ReadPluginHeader(const std::wstring& folder) {
    PluginHeaderReadResult result;

    const std::vector<std::wstring> definitionFiles = FindDefinitionFiles(folder);
    if (definitionFiles.empty()) {
        result.noDefinition = true;
        return result;
    }
    const std::wstring& definitionFile = definitionFiles.front();

    bool readOk = false;
    std::string text = rs::platform_text::ReadFileAsUtf8(definitionFile, readOk);
    if (!readOk) return result;
    const std::size_t headerEnd = FindPluginHeaderEnd(text);
    if (headerEnd != std::string::npos) text.resize(headerEnd);

    rs::legacy_pi::Diagnostics diagnostics;
    const rs::legacy_pi::PiDocument doc = rs::legacy_pi::Parse(text, diagnostics);
    for (const rs::legacy_pi::PiBlock& node : doc.nodes) {
        if (node.name != "PluginHeader") continue;
        result.header = rs::legacy_pi::BuildPluginHeader(node);
        result.ok = true;
        break;
    }
    if (!result.ok) return result;

    if (!result.header.iconTexture.empty()) {
        const std::wstring iconPath = ResolvePath(DirOf(definitionFile), result.header.iconTexture);
        if (FileExists(iconPath)) result.iconPathUtf8 = rs::platform_text::WideToUtf8(iconPath);
    }

    if (result.header.type == rs::legacy_pi::PluginType::Station) {
        result.hasSignal = rs::legacy_pi::sidecar::LoadStationSidecar(folder).hasSignal;
    }
    return result;
}

} // namespace rs::legacy_plugin
