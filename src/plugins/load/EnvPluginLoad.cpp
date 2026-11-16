#include "EnvPluginLoad.h"

#include <algorithm>
#include <filesystem>

#include "PluginPaths.h"
#include "../../platform/text/Encoding.h"
#include "../schema/parser/Parser.h"

namespace rs::legacy_plugin {

EnvPluginLoadResult LoadEnvPlugin(const std::wstring& folder, bool preferKBuild) {
    EnvPluginLoadResult result;
    result.defPath = PickDefinitionFile(folder, preferKBuild);
    if (result.defPath.empty()) {
        result.noDefinition = true;
        return result;
    }

    rs::legacy_pi::Diagnostics diagnostics;
    const rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(result.defPath, diagnostics);
    if (!doc.nodes.empty()) {
        result.definition = rs::legacy_pi::BuildEnvDefinition(doc, diagnostics);
        result.ok = true;  // 構文エラーがあっても読めた分は使う(エラーはdiagnosticsに残る)
    }
    result.diagnostics = diagnostics.Items();
    return result;
}

std::string ResolveEnvTexturePath(const std::string& envFolderUtf8, const std::string& relativePathUtf8) {
    // 先頭の区切りは絶対パス扱いになりフォルダ基準でなくなるので取り除く。
    std::size_t start = 0;
    while (start < relativePathUtf8.size() &&
           (relativePathUtf8[start] == '/' || relativePathUtf8[start] == '\\')) {
        ++start;
    }
    if (start >= relativePathUtf8.size()) return {};

    const std::filesystem::path folder(rs::platform_text::Utf8ToWide(envFolderUtf8));
    // 定義はバックスラッシュ区切りでも書かれる。区切りを'/'にそろえてからパスにする。
    std::wstring relativeWide = rs::platform_text::Utf8ToWide(relativePathUtf8.substr(start));
    std::replace(relativeWide.begin(), relativeWide.end(), L'\\', L'/');
    const std::filesystem::path relative(relativeWide);
    return rs::platform_text::WideToUtf8((folder / relative).lexically_normal().generic_wstring());
}

} // namespace rs::legacy_plugin
