//
// RmlUiPluginSettingsView.h - プラグイン設定パネル(移動できるウィンドウ)のスイッチ一覧を
// データモデル"workspace"へ結ぶ。
//
// 行の組み立てと値の書き込みは、ImGui版と同じ
// SceneEntitySwitchAction(SwitchList.hの行・書き込み先)を通す。RmlUiWorkspaceViewがメンバとして持ち、
// RegisterModelで変数とイベントを足す。パネルの開閉と今の対象の個体IDはWorkspaceShellが持ち、
// ここは値と操作だけを受け持つ(DOMのid検索・クラスの付け外しはしない)。
//
// 選択肢のあるスイッチはコンボ(変更で即書き込む)。選択肢の無いスイッチは値を持たないので、名前があれば
// 見出し、名前が空白だけなら空きとして並べる(行の出し方はSwitchList.hのGetSwitchRowKind)。
//
#pragma once

#include <memory>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../../world/alignment/Guid.h"

namespace rs::app {
class SceneRenderer;
} // namespace rs::app

namespace rs::world {
class SceneRegistry;
} // namespace rs::world

namespace rs::editor {
class Editor;
} // namespace rs::editor

namespace rs::app::rmlui {

struct PluginSwitchesModel;

class RmlUiPluginSettingsView {
public:
    RmlUiPluginSettingsView();
    ~RmlUiPluginSettingsView();
    RmlUiPluginSettingsView(const RmlUiPluginSettingsView&) = delete;
    RmlUiPluginSettingsView& operator=(const RmlUiPluginSettingsView&) = delete;

    // 個体の置き場と、スイッチ定義を引く描画側、信号モデルの書き込み先(Undo付きのEditor)を渡す。
    // どれも呼び出し側が所有し、このビューより長く生きること。
    void Attach(rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer, rs::editor::Editor& editor);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // パネルが見えているフレームに毎回呼ぶ。targetEntityIdは今開いている対象の個体ID。
    // 値が変わったときだけデータモデルへ反映する(対象が変わったときは一覧を入れ替える)。Attach前は何もしない。
    void Refresh(const rs::world::Guid& targetEntityId);

private:
    void OnCommitRow(Rml::Event& event, const Rml::VariantList& arguments);

    rs::world::SceneRegistry* sceneRegistry_ = nullptr;
    SceneRenderer* sceneRenderer_ = nullptr;
    rs::editor::Editor* editor_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<PluginSwitchesModel> model_;
    // 一覧を作った対象の個体ID(書き込み先)。
    rs::world::Guid targetEntityId_ = rs::world::NilGuid();
};

} // namespace rs::app::rmlui
