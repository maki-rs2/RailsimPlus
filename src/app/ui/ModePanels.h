//
// ModePanels.h - 表示モード(Rail/Train/Scenery/GameSettings等)ごとのパネル切替
//
// モード表による表示判定と、パネル局所UI状態
// (プラグインセレクター/入力バッファ/配置UI状態)をまとめる。editor は毎フレームTickを持つサブシステムであり
// パネル局所状態ではないため、ここでは所有せず Draw() の引数として受け取る。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/WarpLinkAuthoring.h"
#include "../../world/time/WorldRefs.h"
#include "UiPanels.h"
#include "EditorViewportState.h"
#include "StationDiaPanel.h"
#include "SignalBlockPanel.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor { class Editor; }

namespace rs::world {
class SceneRegistry;
}

namespace rs::sim {
class AudioEngine;
}

namespace rs::app {

class App;
class CameraController;
class CabinViewCamera;
class LayoutPersistenceAction;
struct LayoutDocumentState;
class PluginSelector;
class SceneRenderer;

struct ModePanelsContext {
    App& app;
    CameraController& camera;
    bool viewerMode;
    bool rmlWorkspace = false;
    rs::editor::Editor& editor;
    rs::editor::PlacePreview& lastArcPreview;
    const rs::world::TrackGraph& graph;
    PluginSelector& selector;
    rs::world::SceneRegistry& sceneRegistry;
    SceneRenderer& sceneRenderer;
    std::vector<std::string>& loadLog;
    rs::world::WorldRefs world;
    Localization& localization;
    UserSettingsData& userSettings;
    // DecodeRs2が解決した架線スパン一覧の格納先(EditorViewportContext.importedWireSpansと
    // 同じ実体を指す)。
    std::vector<ImportedWireSpan>& importedWireSpans;
    // 読込済み.rs2の複数Scene(Ctrl+上下キー切替と共有)。
    LayoutSession& layoutSession;
    LayoutPersistenceAction& layoutAction;
    LayoutDocumentState& layoutDocument;
    // 運転台視点のトグル先。
    CabinViewCamera& cabinCamera;
    // ゲーム設定パネルの出力デバイス選択・列挙に使う。
    rs::sim::AudioEngine& audioEngine;
    // パス選択ダイアログの親にするアプリのウィンドウ(HWND)。RmlUi側へ渡す値と同じ。
    void* windowHandle;
};

class ModePanels {
public:
    // 選択中の viewMode に応じたパネル群を描画する(表示切り替えパネル自体は
    // main.cppがDrawViewModeSwitchPanelで別途描く)。
    void Draw(ViewMode viewMode, const ModePanelsContext& ctx);

    // Trainモードのクリック配置(TrainPlacementInput)が反転指定の読み取りと
    // 配置済みグループIDの追記に使う。
    TrainPlacementUiState& TrainPlacementUi() { return trainPlacementUi_; }
    SceneryPlacementUiState& SceneryPlacementUi() { return sceneryPlacementUi_; }
    EditorViewportState& ViewportState() { return viewportState_; }
    SaveDataPanelState& SaveDataPanel() { return saveDataPanelState_; }
    rs::editor::WarpLinkAuthoring& WarpAuthoring() { return warpAuthoring_; }

private:
    // パス欄の選択ダイアログ。全パネルで共有する1つだけ(BeginFrameはDrawの先頭で毎フレーム呼ぶ)。
    ImGuiPathPicker pathPicker_;

    std::string railListError_;
    std::string railTemplateError_;

    EditorViewportState viewportState_;
    FormationPanelState formationPanelState_;
    TrainPlacementUiState trainPlacementUi_;
    rs::editor::WarpLinkAuthoring warpAuthoring_;

    std::string slotListError_;
    SceneryPlacementUiState sceneryPlacementUi_;
    // 駅・ダイヤパネルと、その番線ルーチン表の局所状態を所有する。
    StationDiaPanelState stationDiaPanelState_;

    GameSettingsPanelState gameSettingsPanelState_;
    SaveDataPanelState saveDataPanelState_;
};

} // namespace rs::app
