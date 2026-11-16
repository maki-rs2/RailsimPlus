//
// LayoutSession.h - .rs2レイアウト内の複数Scene(SceneList > Scene)を全件キャッシュし、
// Ctrl+上下キーでの切替を担う。
//
// RailSim IIの仕様: 1レイアウトは複数Scene(軌道網・配置物・環境・
// カメラを含む)で構成され、編成(TrainGroup)だけはレイアウト直下=全Scene共通。
// 本ファイルはSceneごとのTrackGraph/配置物/架線スパン/カメラをCPU側に全件保持し、
// 切替時は保持済みデータをEditor/SceneRegistry/CameraControllerへ再適用するだけで
// 完結させる(ファイル再パース無し)。Sceneryはid接頭辞にかかわらず全件がアクティブSceneの
// キャッシュへ保管される。編成(Train)エンティティはScene単位に属さないため対象外。
//
#pragma once

#include <string>
#include <vector>

#include "Rs2SceneApplier.h"
#include "../camera/CameraControl.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/Guid.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::editor {
class Editor;
}

namespace rs::world {
class SceneRegistry;
class GameClock;
struct SolarOverrides;
}

namespace rs::app {


// Scene 1個分のキャッシュ(ImportRs2Sceneの結果+Camera)。
struct LayoutSceneCache {
    rs::world::Guid guid = rs::world::NilGuid();
    std::string name;
    // このSceneのEnvプラグイン名(.rs2のEnvPlugin。空=未指定)。.rslへ保存・復元する。
    std::string envPlugin;
    // envPluginを解決したEnvフォルダ(読込時にプラグインルート候補から決める実行時専用の値。
    // 保存しない。空=未指定または見つからない)。
    std::string envFolderUtf8;
    rs::world::TrackGraph graph;
    rs::world::ManualPlacementOverlay overlay;
    std::vector<ImportedWireSpan> wireSpans;
    // このSceneに属するScenery(手動配置・アンカー設置・.rs2由来のすべて)の保管先。
    std::vector<rs::world::SceneEntity> manualScenery;
    bool hasCamera = false;
    FlyCamera camera;
};

// .rs2レイアウト1本分。scenes[activeIndex]が現在Editor/SceneRegistryへ反映されている内容。
struct LayoutSession {
    std::vector<LayoutSceneCache> scenes;
    size_t activeIndex = 0;
    // scenesが空(ライブ状態だけで保存する)ときの1Scene目のguid。保存のたびに振り直さないため保持する。
    rs::world::Guid liveSceneGuid = rs::world::NilGuid();

    // Scene間ワープ接続テーブル。1本のリンクが
    // 2つのScene graphを跨ぐため、個々のScene graphではなくレイアウト直下=全Scene
    // 共通のここに1個だけ保持する(TrainGroupがレイアウト直下にあるのと同じレイヤー)。
    // 空の場合はwarp無しとしてAdvanceAcrossScenesが正しく扱われる。
    rs::world::WarpLinkTable warpLinks;

    bool HasMultipleScenes() const { return scenes.size() > 1; }
};

// 現在アクティブなSceneのライブ状態をキャッシュへ書き戻す。sessionが空なら何もしない。
// importedWireSpansがnullptrなら架線スパンはキャッシュ済みの値を維持する
// (架線のライブ配列を持たない呼び出し側向け)。
void WriteBackActiveScene(LayoutSession& session, const rs::editor::Editor& editor,
                          rs::world::SceneRegistry& sceneRegistry,
                          const CameraController& camera,
                          const std::vector<ImportedWireSpan>* importedWireSpans);

// 保存用にライブ状態をキャッシュへ複写する。ライブのエンティティは変更しない。
void CopyBackActiveScene(LayoutSession& session, const rs::editor::Editor& editor,
                         const rs::world::SceneRegistry& sceneRegistry,
                         const CameraController& camera,
                         const std::vector<ImportedWireSpan>* importedWireSpans);

// LayoutSceneCache 1件をEditor/SceneRegistry/CameraController/importedWireSpansへ反映する。
void ApplySceneCacheToLive(const LayoutSceneCache& cache, rs::editor::Editor& editor,
                           rs::world::SceneRegistry& sceneRegistry, CameraController& camera,
                           std::vector<ImportedWireSpan>& importedWireSpans);

// 新しいSceneの名前「シーン(n)」を返す。nは追加後のScene数で、既存名と被るときは増やす。
std::string MakeNewSceneName(const std::vector<LayoutSceneCache>& scenes);

// ライブ状態だけのセッションに最初のキャッシュを作る。ライブ側は変更しない。
void EnsureLayoutScene(LayoutSession& session, const rs::editor::Editor& editor,
                       const rs::world::SceneRegistry& registry, const CameraController& camera,
                       const std::vector<ImportedWireSpan>& wireSpans);

size_t SwitchLayoutSceneTo(size_t index, rs::editor::Editor& editor, rs::world::SceneRegistry& registry,
                           CameraController& camera, std::vector<ImportedWireSpan>& wireSpans,
                           LayoutSession& session);

// 地形(Surface)を原点に置いた空のSceneを末尾へ足し、そのSceneへ切り替える。環境はデフォルトの空(envPluginが空)。
// surfaceFolderUtf8が空なら何も変えずfalse。現在のSceneのライブ状態は、足す前にキャッシュへ書き戻す
// (warpLinksが持つ既存のScene番号は、末尾追加なのでずれない)。
bool AddLayoutScene(const std::string& surfaceFolderUtf8, rs::editor::Editor& editor,
                    rs::world::SceneRegistry& registry, CameraController& camera,
                    std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session);

// Sceneの環境プラグイン名と、その解決結果のフォルダを入れる(ライブ状態は入れ替えない。描画へはActiveEnvが追従する)。
// 範囲外・同じ環境(綴りの違いも含めて同じ値)ならfalseで何も変えない。
bool SetLayoutSceneEnv(size_t index, const std::string& envPlugin, const std::string& envFolderUtf8,
                       rs::editor::Editor& editor, rs::world::SceneRegistry& registry, CameraController& camera,
                       std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session);

// nameが空・同じ名前・範囲外ならfalseで何も変えない。
bool RenameLayoutScene(size_t index, const std::string& name, rs::editor::Editor& editor,
                       rs::world::SceneRegistry& registry, CameraController& camera,
                       std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session);

// 戻り値は旧添字から新添字への対応。無効な指定・同じ位置への移動なら空。
std::vector<size_t> ReorderLayoutScene(size_t source, size_t destination, rs::editor::Editor& editor,
                                      rs::world::SceneRegistry& registry, CameraController& camera,
                                      std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session);

// 付け替え表(oldToNew[旧]=新)で、LayoutSessionと配置物が持つScene番号(今のScene・ワープ・Carの所属)を付け替える。
// 消えるScene(kRemovedScene)を端に持つワープは捨てる。今のSceneが消えるときだけ、activeIndexを動かさない。
void RemapSessionSceneIndices(LayoutSession& session, rs::world::SceneRegistry& registry,
                              const std::vector<size_t>& oldToNew);

// direction(+1=次のScene、-1=前のScene)分だけsession.activeIndexをラップアラウンドで
// 進め、そのSceneのキャッシュ済み内容をeditor/sceneRegistry/camera/importedWireSpansへ
// 再適用する(ファイル再パース・GPU再ロード判定はSceneRenderer::Syncの通常差分検知に
// 任せる)。session.scenes.empty()なら何もしない。戻り値は切替後のactiveIndex。
size_t SwitchLayoutScene(int direction, rs::editor::Editor& editor, rs::world::SceneRegistry& sceneRegistry,
                         CameraController& camera, std::vector<ImportedWireSpan>& importedWireSpans,
                         LayoutSession& session);

} // namespace rs::app
