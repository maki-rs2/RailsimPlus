#include "PluginTreeConfigSource.h"

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"

namespace rs::app::plugin_browser {
namespace {

namespace fs = std::filesystem;

// 優先順(先が優先)。
constexpr const wchar_t* kConfigFileNames[] = {L"Config2k.txt", L"Config.txt"};

} // namespace

std::optional<fs::path> SelectPluginTreeConfigPath(const std::vector<std::string>& rootsUtf8) {
    for (const wchar_t* fileName : kConfigFileNames) {
        for (const std::string& root : rootsUtf8) {
            const fs::path path = fs::path(rs::platform_text::Utf8ToWide(root)) / fileName;
            std::error_code ec;
            if (fs::is_regular_file(path, ec)) return path;
        }
    }
    return std::nullopt;
}

std::optional<rs::legacy_layout::PluginTreeSet> LoadPluginTreeSet(const std::vector<std::string>& rootsUtf8) {
    const std::optional<fs::path> path = SelectPluginTreeConfigPath(rootsUtf8);
    if (!path.has_value()) return std::nullopt;

    std::vector<std::string> warnings;
    std::optional<rs::legacy_layout::PluginTreeSet> trees = rs::legacy_layout::ParsePluginTreeFile(*path, warnings);
    if (!trees.has_value()) {
        spdlog::warn("[plugin_browser] ディレクトリ分けのファイルを読めません: {}", path->string());
        return std::nullopt;
    }
    for (const std::string& warning : warnings) spdlog::warn("[plugin_browser] ディレクトリ分け: {}", warning);
    return trees;
}

std::map<std::string, std::string> LoadConfigSelectedPlugins(const std::vector<std::string>& rootsUtf8) {
    std::map<std::string, std::string> selected;
    const std::optional<fs::path> path = SelectPluginTreeConfigPath(rootsUtf8);
    if (!path.has_value()) return selected;
    const std::optional<std::vector<std::pair<std::string, std::string>>> parsed =
        rs::legacy_layout::ParseSelectedPluginFile(*path);
    if (!parsed.has_value()) {
        spdlog::warn("[plugin_browser] 選択中プラグインの設定を読めません: {}", path->string());
        return selected;
    }
    for (const auto& [kind, name] : *parsed) {
        if (!name.empty()) selected[kind] = name;
    }
    return selected;
}

} // namespace rs::app::plugin_browser
