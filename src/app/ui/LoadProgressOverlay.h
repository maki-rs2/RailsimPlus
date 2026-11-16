//
// LoadProgressOverlay.h - レイアウト読込・Scene切替のフレーム分割ジョブ進捗表示
//
#pragma once

namespace rs::app {

class SceneRenderer;
class LayoutPersistenceAction;
class Localization;

// DrawLoadProgressOverlayの依存一式。
struct LoadProgressOverlayContext {
    SceneRenderer& sceneRenderer;
    const Localization& localization;
};

// ビューポート全体を覆う半透明オーバーレイ+中央の進捗バー。ImGui版の画面だけで描く(--rmlui-workspaceでは
// 同じ表示をRmlUiLoadingViewが出すので、ここは呼ばない)。
// SceneRenderer::SyncJobActive()がfalseの間は何も描画しない。SyncAndTickScene
// (Sync()呼び出し)の直後、AppRuntime.cppのRunFrameLoop内で呼ぶこと(ImGuiの
// 描画コマンドはApp::Render内のImGui::Render()より前であればよい。Sync()の
// 前=DrawFrameUiPanels側で呼ぶと、読込ジョブが実際に始まった最初のフレームで
// 1フレーム古いJobActive()==falseを見てオーバーレイが出ない)。
// 実際の多重読込要求の拒否・一時停止自体はLayoutPersistenceAction/
// AppRuntimeFrameSim.cpp側が担う(このオーバーレイは見た目の表示のみ)。
void DrawLoadProgressOverlay(const LoadProgressOverlayContext& ctx);

// レイアウト読込予約(LayoutPersistenceAction::RequestLoadLayout)の「読込中」表示。
// 起動時CLI(--openlayout)・「セーブデータ」パネルのボタン、どちらの読込予約でも
// 同じ1つのインジケータを描く(経路を分けない)。HasPendingLoad()がfalseの間は
// 何も描画しない。描画したら必ずMarkPendingLoadIndicatorShownを呼び、実処理
// (LoadLayout)が「読込中」表示が1回でも画面に出た後のフレームでのみ走るようにする
// (他パネルより後に呼ぶこと。DrawLoadProgressOverlayと同じ配置意図)。
void DrawPendingLayoutLoadIndicator(LayoutPersistenceAction& layoutAction, const Localization& localization);

} // namespace rs::app
