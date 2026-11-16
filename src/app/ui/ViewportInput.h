//
// ViewportInput.h - 敷設エディタビューポートの入力処理(EditorViewport.cppより分割)
//
// マウス/キーボード入力によるnode/edge/配置物/Scenery/Trainの選択・ドラッグ・
// Undo/Redo・削除を処理する。TickEditorViewportから毎フレーム呼ばれる。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../input/InputTypes.h"
#include "../gpu/CameraMath.h"
#include "../plugin_session/PluginSelector.h"
#include "../scene/TrackPluginPlacement.h"
#include "../scene/SceneRenderer.h"
#include "EditorViewport.h"

namespace rs::app {

// マウス座標がビューポート(ゲームウィンドウのクライアント領域)内かどうか。
// Win32のImGuiバックエンドはウィンドウがフォーカスを持つ限り、カーソルが
// クライアント領域外(デスクトップの他ウィンドウ上等)にあってもGetCursorPos
// ベースでio.MousePosを更新し続けるため、これをチェックせずにPick*へ座標を
// そのまま渡すと、ウィンドウ外にマウスがあってもホバー判定が反応してしまう。各ホバー/ピック系ハンドラの入口で共通に使う。
bool IsMouseInViewport(rs::app::input::PointerPos mouse, UINT width, UINT height);

bool PickSceneEntity(SceneRenderer& sceneRenderer, const rs::world::SceneRegistry& registry,
                     const rs::world::TrackGraph& graph, std::vector<std::string>& log,
                     const rs::core::Ray& ray, float screenX, float screenY,
                     const rs::core::Mat4& viewProj, UINT width, UINT height, float pxRadius,
                     rs::world::Guid& outEntityId);

// 自動配置物(Pier/Pole)の個別ピッキング。
// items(直近のCollectTrackPluginPlacements結果キャッシュ)から、レイに最も近い1件を
// AABB交差(無ければ原点+pxRadius相当のワールド半径フォールバック)で探す。
// PlacementDragInput.cppからRail/Scenery両モード共通で使うため公開する。
bool PickTrackPluginPlacement(const std::vector<TrackPluginPlacementItem>& items,
                             const rs::world::TrackGraph& graph, const PluginSelector& selector,
                             SceneRenderer& sceneRenderer,
                             std::vector<std::string>& log, const rs::core::Ray& ray,
                             float screenX, float screenY, const rs::core::Mat4& viewProj,
                             UINT width, UINT height, float pxRadius, size_t& outIndex);

// folderUtf8の種別(Pier/Pole)から、手動追加/移動時に実体化済み個体へ与えるべき
// keepUpright/verticalOffsetMを決める。CollectTrackPluginPlacements(自動配置)が
// 同じfolderへ与える値と揃える(Pier/Poleは直立、Poleは架線支持点の高さ=edge上で
// 組になっているLineのTrolleyAlt+Height)。これを省くと手動追加/移動したPoleが
// Lineの高さから外れて描画される。PlacementDragInput.cppが
// クロスエッジ移動の候補edgeごとの再解決にも使うため公開する。
struct PlacementAttrs {
    bool keepUpright = false;
    float verticalOffsetM = 0.0f;
};
PlacementAttrs ResolvePlacementAttrs(const std::string& folderUtf8, const rs::world::Guid& edge,
                                     const rs::world::TrackGraph& graph,
                                     const std::vector<rs::app::TrackPluginEntry>& entries);

// マウス/キーボード入力によるnode/edge/配置物の選択・ドラッグ・Undo/Redo・削除を処理する。
void HandleEditorInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                      rs::app::input::PointerPos mouse, bool editingEnabled);

// Scenery(駅舎/施設等)のホバー/選択判定。SceneryモードのみでPickSceneEntityを
// 評価する(Railモードのnode/edge/配置物選択とは選択対象が排他、表示モードで
// プラグイン種別を分離する)。選択+ハイライトのみで削除/移動は対象外。
void HandleSceneEntitySelectionInput(const EditorViewportContext& ctx,
                                     const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse,
                                     bool sceneEntitySelectionEnabled);

// Train(編成車両)のホバーハイライト判定。Trainモードのみで評価する。RailSim II では
// 「配置・撤去のハイライト」と「配置プラグインのスイッチ編集」が別の選択系統で、
// 後者は全プラグイン種別共通のクリックのみの軽量方式で、別の処理が担う。ここではホバーハイライト(Editor::hoveredSceneEntityId)のみを
// 設定し、クリック選択とDeleteキー削除はTrainPlacementInput側が担う
// (クリックが配置と競合するため、判定順序を配置入力と同じ場所に集約する)。
// 戻り値: マウス直下に配置済み車両があるか(trueなら配置入力側がクリックを
// 配置に使わず選択に回す)。
bool HandleTrainHoverInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, bool trainHoverEnabled);

} // namespace rs::app
