//
// WorkspaceEditorBridge.h - RmlUi固定ワークスペースの画面状態と編集ツール(Editor/ViewMode)のつなぎ
//
// --rmlui-workspace のときだけ毎フレーム呼ぶ。向きは WorkspaceShell から ViewMode への一方向で、
// ViewMode の側から画面状態は変えない。ただしEscの取り消し可否と、ビューポートのクリックによる
// プラグイン設定パネルの開く要求は画面状態側へ渡す(Editorの選択とは連動しない)。
//
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../action/GameSettingsAction.h"
#include "../action/RenderSettingsAction.h"
#include "../../editor/tool/WarpLinkAuthoring.h"
#include "../../core/Math.h"
#include "../input/InputSource.h"
#include "../ui/PluginSettingsViewportLink.h"
#include "../ui/SignalBlockViewportLink.h"
#include "../ui/ViewMode.h"

namespace rs::editor {
class Editor;
struct PlacePreview;
} // namespace rs::editor
namespace rs::world {
class SceneRegistry;
} // namespace rs::world

namespace rs::app {

class LayoutPersistenceAction;
class PluginSelector;
class SceneRenderer;
struct UserSettingsData;
struct TrainPlacementUiState;
struct LayoutDocumentState;
class CabinViewCamera;
class CameraController;
struct LayoutSession;
struct ImportedWireSpan;
class Localization;

namespace plugin_browser {
class PluginBrowserService;
}

namespace rmlui {
class RmlUiIntegration;
}

// Tickが操作の対象として使う参照の束。どれも呼び出し側が所有する。
struct WorkspaceBridgeServices {
    PluginSelector& pluginSelector;
    UserSettingsData& userSettings;
    plugin_browser::PluginBrowserService& pluginBrowser;
    // プラグイン設定パネルのスイッチ一覧の書き込み先(個体の置き場と、スイッチ定義を引く描画側)。
    rs::world::SceneRegistry& sceneRegistry;
    SceneRenderer& sceneRenderer;
    // フォルダ選択ダイアログの親にするアプリのウィンドウ(HWND)。
    void* windowHandle;
    // 情景配置の矢印キーの回転刻み[度]の持ち主(ImGui版の入力欄と同じ値。SceneryPlacementUiState::yawStepDeg)。
    float& sceneryYawStepDeg;
    // 情景配置の連続配置の持ち主(SceneryPlacementUiState::continuousPlacement)。
    bool& sceneryContinuous;
    bool& scenerySnapToRail;
    // 編成一覧(列車モードの左タブ下段)の操作の対象。ImGui版の編成パネルと同じ持ち主を指す。
    TrainPlacementUiState& trainPlacementUi;
    LayoutDocumentState& layoutDocument;
    CabinViewCamera& cabinCamera;
    // 配置済みの編成のカードをダブルクリックしたときの、カメラの移動先。
    CameraController& camera;
    // 編成情報カードをアクティブSceneの編成に絞る(LayoutSession::activeIndex)。
    // 見つからないプラグインの差し替え・削除は、Sceneのキャッシュも書き換えるため非constで持つ。
    LayoutSession& layoutSession;
    rs::editor::WarpLinkAuthoring& warpAuthoring;
    // 見つからないプラグインの集計と差し替えが架線のライブ配列を読み書きする。
    std::vector<ImportedWireSpan>& importedWireSpans;
    // 画面の文言(翻訳キー)の引き先。
    const Localization& localization;
    // ゲーム設定ウィンドウの操作の対象。ImGui版のゲーム設定パネルと同じ参照を指す。
    GameSettingsRefs gameSettings;
    // ゲーム設定ウィンドウの描画タブの操作の対象。ImGui版の描画設定パネルと同じ参照を指す。
    RenderSettingsRefs renderSettings;
    // シーン画面の一覧と操作の対象(ImGuiのセーブデータ画面と同じ操作を通る)。
    LayoutPersistenceAction& layoutAction;
};

class WorkspaceEditorBridge {
public:
    // TickEditorViewportより前に呼ぶ。順序は
    //   1. ビューポートのクリックでパネルを開く(前のフレームのTickEditorViewportが書いた
    //      clickedEntityId。1フレーム遅れて開く)
    //   2. パネルの対象の個体が無くなっていたらパネルを閉じる
    //   3. Esc(WorkspaceBack)。取り消せるツール操作があればそちらを先に取り消す
    //   4. 画面状態からViewMode・ToolModeを反映
    //   5. 敷設エディタが出ているときだけ、敷設エディタの表示値を更新(1フレーム前のプレビューを見る)
    //   6. プラグインブラウザの裏側(走査)を進め、左タブがブラウザのときだけブラウザの表示値を更新
    //      (情景配置の回転刻みの入力欄の表示値もここで更新)
    //      (列車モードで左タブがブラウザのときだけ、その下段の編成一覧の表示値も更新)
    //      (列車モードのときは、編成情報カードの文字・色も更新。位置はカメラ確定後のUpdateTrainInfoCardsが決める)
    //   7. ルート管理ウィンドウが見えているか、フォルダ選択ダイアログが開いているときだけ、その表示値を更新
    //   7b. 見つからないプラグインの一覧: 状態の写しが変わったときだけ作り直し(毎フレームは歩かない)、
    //      警告バッジの数を更新する。ウィンドウが見えている間に全部解決したら閉じる
    //   7c. ゲーム設定ウィンドウ: フォルダ選択ダイアログの結果は毎フレーム受け取り、画面の値は
    //      ウィンドウが見えているときだけ更新する。framesPerSecondはfps表示の元の値(平均)
    //   8. プラグイン設定パネルが見えているときだけ、スイッチ一覧の表示値を更新(対象が消えた閉じ処理のあと)
    //   9. ビューポートへ渡す値(クリックで開けるか・パネルの対象)を入れる。Escや遷移で閉じたぶんが
    //      同じフレームに反映されるよう、状態を変える処理のあとで入れる
    void Tick(rmlui::RmlUiIntegration& rmlUi, const rs::app::input::InputSource& input,
              rs::editor::Editor& editor, const rs::editor::PlacePreview& lastArcPreview,
              ViewMode& viewMode, const WorkspaceBridgeServices& services,
              PluginSettingsViewportLink& pluginSettingsLink, const SignalBlockViewportLink& signalBlockLink,
              double deltaSeconds, float framesPerSecond);

    // カメラ確定後に呼ぶ。編成情報カードと閉塞ラベルの位置、カードの距離による省略を次のRmlUiの更新へ渡す。
    // viewProj/eyeはこのフレームの描画と同じカメラ、screenWidth/Heightはビューポート[px]。
    void UpdateTrainInfoCards(rmlui::RmlUiIntegration& rmlUi, const rs::core::Mat4& viewProj,
                              const rs::core::Vec3& eye, float screenWidth, float screenHeight);

private:
    // ビューへ操作の対象を渡したか(最初のTickで1回だけ渡す)。
    bool layingEditorAttached_ = false;
    bool pluginBrowserAttached_ = false;
    bool pluginRootsAttached_ = false;
    bool missingPluginsAttached_ = false;
    bool gameSettingsAttached_ = false;
    bool pluginSettingsAttached_ = false;
    bool sceneryYawStepAttached_ = false;
    bool formationListAttached_ = false;
    bool diaPanelAttached_ = false;
};

} // namespace rs::app
