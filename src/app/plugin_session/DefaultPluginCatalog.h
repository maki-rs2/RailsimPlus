//
// DefaultPluginCatalog.h - 種別ごとの既定プラグインの表と、解決・自動配置
//
// 種別ごとの違い(種別フォルダ名・既定のプラグイン名・新規レイアウトで自動配置するか)は
// この表だけで持ち、種別名での分岐は書かない。ユーザー設定(UserSettingsData::defaultPlugins)が
// あれば表の既定名より優先する。
//
#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "../../plugins/schema/PluginSchema.h"

#include "../../plugins/schema/PluginTypeTraits.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::world { class SceneRegistry; }

namespace rs::app {

struct UserSettingsData;

using DefaultPluginTraits = rs::legacy_pi::PluginTypeTraits;

inline constexpr std::size_t kDefaultPluginKindCount = magic_enum::enum_count<rs::legacy_pi::PluginType>() - 1;
inline constexpr std::array<DefaultPluginTraits, kDefaultPluginKindCount> kDefaultPluginKinds = []() {
    std::array<DefaultPluginTraits, kDefaultPluginKindCount> kinds{};
    for (std::size_t index = 0; index < kinds.size(); ++index)
        kinds[index] = rs::legacy_pi::kPluginTypeTraits[index + 1];
    return kinds;
}();

// 行の番号 + 1 が列挙の値と一致しているか(Unknown=0を表に含めないため、添字は1つずれる)。
constexpr bool IsDefaultPluginCatalogInEnumOrder() {
    for (std::size_t i = 0; i < std::size(kDefaultPluginKinds); ++i) {
        if (static_cast<std::size_t>(kDefaultPluginKinds[i].pluginType) != i + 1) return false;
    }
    return true;
}

static_assert(std::size(kDefaultPluginKinds) == kDefaultPluginKindCount,
              "kDefaultPluginKindsの行数がPluginType(Unknownを除く)と一致しない");
static_assert(IsDefaultPluginCatalogInEnumOrder(),
              "kDefaultPluginKindsの並びがPluginTypeの列挙順と一致しない");

// 種別の既定プラグインのフォルダを返す。優先順位はユーザー設定、表の既定名、無し。
// rootsUtf8はBuildPluginRootCandidatesで作った優先度順のルート候補。
// プラグインのルートの下に存在しなければnullopt。
std::optional<std::string> ResolveDefaultPlugin(rs::legacy_pi::PluginType type,
                                                const UserSettingsData& settings,
                                                const std::vector<std::string>& rootsUtf8);

// Configの選択中プラグイン(SelectedPlugin)のうち敷設プラグインセットの種別を、表の並びで解決して
// フォルダの列にする。実在しない名前は読み飛ばす。Configが無ければ空。
std::vector<std::string> ResolveConfigTrackPluginFolders(const std::vector<std::string>& rootsUtf8);

// Surfaceフォルダ1件を原点に置くSceneEntityを作る(registryへは入れない)。
// kindNameは種別フォルダ名("Surface")。sceneKeyUtf8は配置物のguidの導出に使い、シーンごとに一意にする。
std::vector<rs::world::SceneEntity> MakeSurfaceEntitiesFromFolder(const std::string& folderUtf8,
                                                                  const std::string& kindName,
                                                                  const std::string& sceneKeyUtf8);

// autoPlaceが真の種別の既定プラグインを、原点へ配置物として入れる。解決できない種別は
// 警告ログを出して置かない。戻り値は入れた件数。
std::size_t EnsureDefaultPlacements(rs::world::SceneRegistry& sceneRegistry,
                                    const UserSettingsData& settings,
                                    const std::vector<std::string>& rootsUtf8);

} // namespace rs::app
