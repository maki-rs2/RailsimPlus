#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/Guid.h"
#include "../action/LayoutActionResult.h"

namespace rs::editor {
class Editor;
}

namespace rs::world {
class GameClock;
class SceneRegistry;
struct SolarOverrides;
}

namespace rs::app {

const std::vector<std::string>& EmptyPluginRoots();

struct LayoutSession;
class SceneRenderer;
class CameraController;
class PluginSelector;

using PluginTypeClassifier =
    std::function<rs::legacy_pi::PluginType(const std::string& folderUtf8)>;

rs::legacy_pi::PluginType DefaultClassifyPluginType(const std::string& folderUtf8);
// SaveRslLayout/DecodeRslが必要とする外部状態への参照をまとめた束。
// 永続化対象を追加するたびに関数シグネチャを変えずに済むようにするための集約。
struct RslPersistenceRefs {
    LayoutSession& session;
    rs::editor::Editor& editor;
    rs::world::SceneRegistry& sceneRegistry;
    rs::world::GameClock& gameClock;
    // 太陽オーバーライド(自転/公転の強制モード)。UIの「太陽位置」設定とレイアウトの
    // 両方から書かれるレイアウト状態なのでworld.environmentとして保存する。
    rs::world::SolarOverrides& solarOverrides;
    SceneRenderer& sceneRenderer;
    CameraController& camera;
    PluginSelector& selector;
    // 架線スパンのライブ配列。nullptrなら呼び出し側が架線を保持しないことを表し、保存時は
    // Sceneキャッシュ側の値をそのまま維持し、読込時は復元した架線を捨てる。
    std::vector<ImportedWireSpan>* importedWireSpans = nullptr;
    const std::vector<std::string>& pluginRoots = EmptyPluginRoots();
    const rs::world::Guid* documentGuid = nullptr;
    // info.thumbnailへ入れる文字列(Base64のJPEG)。nullptrなら書かない。
    const std::string* thumbnailBase64 = nullptr;
};

LayoutActionResult SaveRslLayout(const std::string& pathUtf8, const RslPersistenceRefs& refs);
LayoutActionResult SaveRslLayout(const std::string& pathUtf8, const RslPersistenceRefs& refs,
                            const PluginTypeClassifier& classifier);
}  // namespace rs::app
