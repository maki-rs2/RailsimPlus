#include "SceneChoiceCatalog.h"

#include <algorithm>
#include <filesystem>
#include <set>

#include "ActiveEnv.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/load/EnvPluginLoad.h"
#include "../../plugins/load/PluginHeaderRead.h"

namespace rs::app {
namespace {

// 種別フォルダ(Surface・Env)の直下のフォルダを、先のルートを優先して列挙する。受け入れるかは accept が決める。
template <typename Accept>
std::vector<SceneChoice> ScanKindFolders(const std::vector<std::string>& rootCandidatesUtf8, const char* kindFolder,
                                         const Accept& accept) {
    std::vector<SceneChoice> choices;
    std::set<std::string> seen;
    for (const std::string& rootUtf8 : rootCandidatesUtf8) {
        const std::filesystem::path kindDirectory =
            std::filesystem::path(rs::platform_text::Utf8ToWide(rootUtf8)) / kindFolder;
        std::error_code error;
        std::filesystem::directory_iterator it(kindDirectory, error);
        if (error) continue;
        for (const std::filesystem::directory_entry& entry : it) {
            if (!entry.is_directory(error)) continue;
            SceneChoice choice;
            choice.name = rs::platform_text::WideToUtf8(entry.path().filename().wstring());
            // 同じ名前は先のルートが先に採用される。後のルートの同名は、定義の有無に関わらず隠れる。
            if (!seen.insert(rs::platform_text::ToAsciiLower(choice.name)).second) continue;
            choice.folderUtf8 = rs::platform_text::WideToUtf8(entry.path().generic_wstring());
            if (accept(entry.path())) choices.push_back(std::move(choice));
        }
    }
    std::sort(choices.begin(), choices.end(), [](const SceneChoice& a, const SceneChoice& b) {
        return rs::platform_text::ToAsciiLower(a.name) < rs::platform_text::ToAsciiLower(b.name);
    });
    return choices;
}

}  // namespace

SceneChoiceCatalog ScanSceneChoices(const std::vector<std::string>& rootCandidatesUtf8) {
    SceneChoiceCatalog catalog;
    catalog.surfaces = ScanKindFolders(rootCandidatesUtf8, "Surface", [](const std::filesystem::path& folder) {
        const rs::legacy_plugin::PluginHeaderReadResult header = rs::legacy_plugin::ReadPluginHeader(folder.wstring());
        return header.ok && header.header.type == rs::legacy_pi::PluginType::Surface;
    });
    catalog.envs = ScanKindFolders(rootCandidatesUtf8, "Env", [](const std::filesystem::path& folder) {
        const rs::legacy_plugin::EnvPluginLoadResult loaded = rs::legacy_plugin::LoadEnvPlugin(folder.wstring());
        if (!loaded.ok) return false;
        const std::string folderUtf8 = rs::platform_text::WideToUtf8(folder.generic_wstring());
        return !ResolveEnvCubePaths(folderUtf8, loaded.definition).Empty();
    });
    return catalog;
}

}  // namespace rs::app
