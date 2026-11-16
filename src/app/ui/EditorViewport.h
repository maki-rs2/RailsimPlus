//
// EditorViewport.h - 敷設エディタのビューポート毎フレーム処理
//
// 「敷設エディタのビューポート tick」:
//   1. マウス/キーボード入力(ホバー・クリック・Undo/Redo・敷設面高さ追従)
//   2. 背景 ImDrawList へのオーバーレイ描画(edge/node/敷設プレビュー)
//   3. revision 変化時のレール押し出しメッシュ再構築→GPU アップロード
// いずれもウィンドウ描画専用で、ヘッドレス経路からは呼ばれない。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../render/mesh/Mesh.h"
#include "../camera/CameraControl.h"
#include "../gpu/CameraMath.h"
#include "../plugin_session/Rs2SceneApplier.h"
#include "../scene/TrackPluginPlacement.h"
#include "UiPanels.h"
#include "PluginSettingsViewportLink.h"
#include "SignalBlockViewportLink.h"
#include "WireConnectPoints.h"
#include "EditorViewportState.h"
#include "../input/InputSource.h"

namespace rs::editor {
class Editor;
struct PlacePreview;
}
namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class App;
class FormationAction;
class PluginSelector;
class SceneRenderer;
struct LayoutSession;

// TickEditorViewport の入出力。参照メンバは呼び出し側(main)の状態を直接読み書き
// する。
struct EditorViewportContext {
    rs::editor::Editor&                        editor;
    EditorViewportState&                       state;
    const CameraView&                          view;
    CameraController&                          camera;
    const rs::app::input::InputSource&         input;
    UINT                                       width;
    UINT                                       height;
    rs::editor::PlacePreview&                   lastArcPreview;   // out
    uint64_t&                                  meshRevision;     // in/out

    // レール系パレット(PluginSelector::TrackPluginEntries())のうち、各edgeが
    // 独立に持つtrackPluginsに含まれるものだけを重ねて配置する
    // ための追加コンテキスト。selectorの
    // EntryRevision()変化もmeshRevisionと同様のOR条件で再構築トリガーに使う。
    const PluginSelector&                      selector;
    rs::world::SceneRegistry&                   sceneRegistry;
    SceneRenderer&                             sceneRenderer; // 静的Scenery判定キャッシュ共有用
    App&                                       app; // TrackPlugin(Pier/Pole)のGPUインスタンスバッチ投入用
    uint64_t&                                  lastEntryRevision; // in/out
    uint64_t&                                  lastSceneRegistryRevision; // in/out(静的Scenery再構築トリガー)
    uint64_t&                                  lastSceneStructureRevision; // in/out(同、スイッチ値以外の変更。差分更新と全再構築の振り分け)
    std::vector<std::string>&                  log;               // out(配置ログ追記)

    // 自動配置物(Pier/Pole)の直近CollectTrackPluginPlacements結果(needRebuild時
    // のみ更新、それ以外のフレームはそのまま保持)。個別ピッキング用のキャッシュ。呼び出し側(main.cpp)が
    // 所有し参照で渡す(他のrevisionキャッシュと同じ扱い)。
    std::vector<TrackPluginPlacementItem>&      placementCache;    // out

    // 架線の接続点(丸の表示・1点目と2点目の選択)と径間1本ずつ(ホバー・選択・切断)の
    // ピッキング用キャッシュ。メッシュ再構築時のみ更新する。
    WireEditCache&                             wireEditCache;     // out

    // .rs2インポート由来の架線スパン一覧(Rs2SceneApplier::ImportRs2Sceneが
    // Scene直下LineList{Line{Link0/Link1}}から解決)。
    // WalkPoleRuns(native間隔走査)は.rs2インポートedgeを除外するため、このメッシュ
    // 再構築ループが並行してここからもワイヤーを生成する。手動接続/切断
    // WireEditCacheは軌道上の位置(edgeとs)で接続点を指すため、こちらのスパンには
    // 働かない(既知の制限)。
    std::vector<ImportedWireSpan>&             importedWireSpans; // in(.rs2読込/Scene切替でのみ差し替わる)

    // .rs2レイアウトのScene群とScene間ワープ接続テーブル。
    // オーバーレイのワープ端点マーカー描画で warpLinks.Find(activeIndex, node) を
    // 引くために参照する(読み取りのみ)。
    LayoutSession&                            layoutSession;        // in/out(同上、Sceneキャッシュ側のwireSpans)

    // 「編成」パネルと共有する配置UI状態(反転指定の読み取りと、クリック配置で
    // 発番したtrainIdの追記に使う)。所有者はModePanels。
    TrainPlacementUiState&                   trainPlacementUi;
    SceneryPlacementUiState&                   sceneryPlacementUi;
    // 配置済み編成の撤去(Deleteキー)を撤去ボタンと同じ経路にするための編成サービス。
    FormationAction&                           formationAction;

    // 現在の表示モード。Rail以外では敷設操作(クリック配置・Undo/Redo・削除)を
    // 無効化し、Rail/Train以外ではオーバーレイ線(edge/node/敷設プレビュー)を
    // 表示しない(メッシュ本体の再構築・アップロードはモードによらず継続する)。
    ViewMode                                   viewMode = ViewMode::Rail;

    // trueのとき、ビューポート側はEditorCancel(Esc)を読まない。EscをRmlUiワークスペースの
    // WorkspaceBackで1か所だけ処理するため。
    bool                                       escapeHandledByWorkspace = false;

    // プラグイン設定パネルとの受け渡し(--rmlui-workspaceのときだけ渡す。nullptrなら何もしない)。
    PluginSettingsViewportLink*                pluginSettingsLink = nullptr;
    SignalBlockViewportLink*                   signalBlockLink = nullptr;
};

void TickEditorViewport(const EditorViewportContext& ctx);

} // namespace rs::app
