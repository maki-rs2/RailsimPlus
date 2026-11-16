#pragma once

#include <cstdint>
#include <functional>

#include <RmlUi/Core/DataModelHandle.h>

#include "../action/DiaPanelAction.h"
#include "../workspace/DiaPanelPresentation.h"

namespace Rml {
class ElementDocument;
class Event;
} // namespace Rml

namespace rs::editor {
class Editor;
} // namespace rs::editor

namespace rs::app {
class ToastQueue;
class CameraController;
class PluginSelector;
class SceneRenderer;
struct LayoutSession;
} // namespace rs::app

namespace rs::app::rmlui {
class RmlUiConfirmDialog;

class RmlUiDiaPanelView {
public:
    explicit RmlUiDiaPanelView(ToastQueue& queue);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    void RegisterPopups(RmlUiConfirmDialog& dialog) { confirmDialog_ = &dialog; }
    // 表示値を作る元と操作の対象を渡す。switchScene は相対の移動量で、LayoutPersistenceAction::SwitchScene と同じ。
    void Attach(rs::editor::Editor& editor, LayoutSession& session, PluginSelector& selector, SceneRenderer& renderer,
                CameraController& camera, std::function<void(int)> switchScene);
    void SetActive(bool active);
    // 毎フレーム呼ぶ。シーンをまたぐ要求を進め、データが変わっていれば表示値を作り直し、
    // 3D画面で選ばれた番線へ左タブを移す。ダイヤモードでないときは何もしない。
    void Tick();
    void Refresh();
    // Escで1段階戻る(要素編集 → 番線 → 一覧)。一覧にいるとき・パネルを開いていないときは何もせずfalse。
    bool CloseTransient();

private:
    void OnUi(Rml::Event& event, const Rml::VariantList& arguments);
    void OnAction(Rml::Event& event, const Rml::VariantList& arguments, bool commit);
    void OnHover(const Rml::VariantList& arguments);
    void OpenTarget(int targetIndex);
    void ProcessNavigation();
    void Rebuild();
    void SyncSelectedPlatform();
    bool Attached() const { return editor_ != nullptr; }

    ToastQueue& queue_;
    RmlUiConfirmDialog* confirmDialog_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    bool pendingFocus_ = false;
    Rml::DataModelHandle modelHandle_;
    workspace::DiaPanelPresentation presentation_;

    rs::editor::Editor* editor_ = nullptr;
    LayoutSession* session_ = nullptr;
    PluginSelector* selector_ = nullptr;
    SceneRenderer* renderer_ = nullptr;
    CameraController* camera_ = nullptr;
    std::function<void(int)> switchScene_;
    DiaPanelNavigation navigation_;
    // 表示値を作った時点のデータの世代。変わったときだけ作り直す。
    bool built_ = false;
    bool forceRebuild_ = false;
    std::uint64_t builtRevision_ = 0;
    std::size_t builtActiveScene_ = 0;
    std::size_t builtSceneCount_ = 0;
    // 最後に見た選択中の番線。3D画面での選択の変化と、左タブから自分で開いた選択を区別する。
    rs::world::Guid lastSelectedPlatform_ = rs::world::NilGuid();
};
} // namespace rs::app::rmlui
