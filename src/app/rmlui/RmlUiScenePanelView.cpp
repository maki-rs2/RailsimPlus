#include "RmlUiScenePanelView.h"

#include <utility>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdatePhase.h"
#include "../action/LayoutPersistenceAction.h"
#include "../action/SceneAction.h"
#include "../action/ScenePanelViewModel.h"
#include "../locale/Localization.h"

namespace rs::app::rmlui {
using namespace workspace;

RmlUiScenePanelView::RmlUiScenePanelView(ToastQueue& queue) : queue_(queue) {}

void RmlUiScenePanelView::RegisterModel(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<SceneEnvOption> option = constructor.RegisterStruct<SceneEnvOption>();
    option.RegisterMember("name", &SceneEnvOption::name_);
    option.RegisterMember("disabled", &SceneEnvOption::disabled_);
    constructor.RegisterArray<std::vector<SceneEnvOption>>();
    Rml::StructHandle<SceneSurfaceChoice> surface = constructor.RegisterStruct<SceneSurfaceChoice>();
    surface.RegisterMember("name", &SceneSurfaceChoice::name_);
    constructor.RegisterArray<std::vector<SceneSurfaceChoice>>();
    Rml::StructHandle<ScenePanelRow> row = constructor.RegisterStruct<ScenePanelRow>();
    row.RegisterMember("name", &ScenePanelRow::name_);
    row.RegisterMember("current", &ScenePanelRow::current_);
    row.RegisterMember("editing", &ScenePanelRow::editing_);
    row.RegisterMember("draft", &ScenePanelRow::draft_);
    row.RegisterMember("env", &ScenePanelRow::env_);
    row.RegisterMember("envOptions", &ScenePanelRow::envOptions_);
    row.RegisterMember("surface", &ScenePanelRow::surface_);
    constructor.RegisterArray<std::vector<ScenePanelRow>>();
    Rml::StructHandle<ScenePanelPresentation> panel = constructor.RegisterStruct<ScenePanelPresentation>();
    panel.RegisterMember("active", &ScenePanelPresentation::active_);
    panel.RegisterMember("rows", &ScenePanelPresentation::rows_);
    panel.RegisterMember("surfaceChoices", &ScenePanelPresentation::surfaceChoices_);
    panel.RegisterMember("adding", &ScenePanelPresentation::adding_);
    panel.RegisterMember("canAdd", &ScenePanelPresentation::canAdd_);
    panel.RegisterMember("addSelection", &ScenePanelPresentation::addSelection_);
    panel.RegisterMember("canDelete", &ScenePanelPresentation::canDelete_);
    panel.RegisterMember("showArrows", &ScenePanelPresentation::showArrows_);
    panel.RegisterMember("currentName", &ScenePanelPresentation::currentName_);
    constructor.Bind("scene", &presentation_);
    constructor.BindEventCallback("scene_ui", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnUi(event, arguments); });
    constructor.BindEventCallback("scene_action", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnAction(event, arguments); });
    constructor.BindEventCallback("scene_edit", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) { OnEdit(event); });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiScenePanelView::SetActive(bool active) {
    if (presentation_.active_ != active) {
        presentation_.active_ = active;
        presentation_.CancelRename();
        presentation_.CloseAdd();
        presentation_.draggingRow_ = -1;
        // 開くたびに、入っている地形・環境を調べ直す(プラグインを入れ替えたあとでも最新になる)。
        if (active && action_ != nullptr) action_->RescanSceneChoices();
        pendingFocus_ = false;
        modelHandle_.DirtyVariable("scene");
    }
}

void RmlUiScenePanelView::Refresh() {
    if (action_ != nullptr && action_->FillScenePanel(presentation_)) modelHandle_.DirtyVariable("scene");
    if (!pendingFocus_ || document_ == nullptr) return;
    Rml::Element* const element = document_->QuerySelector(".scene-name-input.is-editing");
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    if (input == nullptr || !input->IsVisible()) return;
    pendingFocus_ = false;
    input->SetValue(presentation_.rows_[presentation_.editingRow_].draft_);
    input->Focus();
    input->Select();
}

bool RmlUiScenePanelView::CloseTransient() {
    if (!presentation_.CloseTransient()) return false;
    pendingFocus_ = false;
    modelHandle_.DirtyVariable("scene");
    return true;
}

void RmlUiScenePanelView::OnUi(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    const int row = EventArgumentAt<int>(arguments, 1, -1);
    if (kind == "rename" && presentation_.BeginRename(row)) {
        document_ = event.GetCurrentElement()->GetOwnerDocument();
        pendingFocus_ = true;
    } else if (kind == "drag" && presentation_.ValidRow(row)) {
        presentation_.draggingRow_ = row;
    } else if (kind == "dragEnd") {
        presentation_.draggingRow_ = -1;
    } else if (kind == "add" && action_ != nullptr) {
        // ポップアップを開く前に、入っている地形を調べ直して選択肢へ反映する。
        action_->RescanSceneChoices();
        action_->FillScenePanel(presentation_);
        presentation_.OpenAdd();
    } else if (kind == "addSelect") {
        presentation_.SelectAddSurface(row);
    } else if (kind == "addCancel") {
        presentation_.CloseAdd();
    }
    modelHandle_.DirtyVariable("scene");
}

void RmlUiScenePanelView::OpenDelete(int row) {
    if (dialog_ == nullptr || action_ == nullptr || localization_ == nullptr || !presentation_.ValidRow(row)) return;
    const ScenePanelRow& scene = presentation_.rows_[row];
    if (!presentation_.OpenDelete(row, BuildSceneDeleteMessage(*localization_, scene.name_, scene.warpCount_,
                                                               scene.trainCount_))) return;
    ConfirmDialogContent content;
    content.message = presentation_.deleteMessage_;
    content.confirmLabel = localization_->Text("scene.delete.confirm_button");
    content.danger = true;
    content.onConfirm = [this, row]() {
        scene_action::DeleteScene(*action_, queue_, *localization_, row);
        presentation_.CloseDelete();
    };
    content.onCancel = [this]() { presentation_.CloseDelete(); };
    dialog_->Open(std::move(content));
}

void RmlUiScenePanelView::ConfirmAdd() {
    const SceneSurfaceChoice* const surface = presentation_.SelectedAddSurface();
    if (surface == nullptr || action_ == nullptr) return;
    if (scene_action::AddScene(*action_, queue_, surface->folderUtf8_)) presentation_.CloseAdd();
    modelHandle_.DirtyVariable("scene");
}

void RmlUiScenePanelView::OnAction(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    const int row = EventArgumentAt<int>(arguments, 1, -1);
    if (action_ == nullptr) return;
    if (kind == "switch" && presentation_.ValidRow(row)) {
        scene_action::SwitchScene(*action_, queue_, row);
    } else if (kind == "previous") scene_action::PreviousScene(*action_, queue_);
    else if (kind == "next") scene_action::NextScene(*action_, queue_);
    else if (kind == "addConfirm") ConfirmAdd();
    else if (kind == "delete") OpenDelete(row);
    else if (kind == "env") {
        if (IsInsideContextUpdate()) return; // 画面の値を書き戻しただけのchangeでは操作しない
        int index = -1;
        if (!presentation_.ValidRow(row) || !ParseIntText(event.GetParameter<std::string>("value", ""), index)) return;
        const std::vector<SceneEnvOption>& options = presentation_.rows_[row].envOptions_;
        if (index >= 0 && static_cast<size_t>(index) < options.size() && !options[index].disabled_) {
            scene_action::ApplyEnvironment(*action_, queue_, row, options[index].value_);
        }
        modelHandle_.DirtyVariable("scene"); // 受け付けなかったときも、プルダウンを今の値へ戻す
    }
    else if (kind == "drop" && presentation_.ValidRow(row) && presentation_.ValidRow(presentation_.draggingRow_)) {
        scene_action::ReorderScene(*action_, queue_, presentation_.draggingRow_, row);
        presentation_.draggingRow_ = -1;
    }
}

void RmlUiScenePanelView::OnEdit(Rml::Event& event) {
    if (presentation_.editingRow_ < 0) return;
    if (event.GetId() == Rml::EventId::Keydown) {
        if (event.GetParameter<int>("key_identifier", 0) == static_cast<int>(Rml::Input::KI_ESCAPE)) {
            CloseTransient();
            event.GetCurrentElement()->Blur();
            event.StopPropagation();
        }
        return;
    }
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(event.GetCurrentElement());
    if (input == nullptr) return;
    presentation_.rows_[presentation_.editingRow_].draft_ = input->GetValue();
    if (event.GetId() != Rml::EventId::Blur && !event.GetParameter<bool>("linebreak", false)) return;
    const std::optional<SceneRenameRequest> request = presentation_.CommitRename();
    pendingFocus_ = false;
    if (request && action_ != nullptr) scene_action::RenameScene(*action_, queue_, request->row_, request->name_);
    input->Blur();
    modelHandle_.DirtyVariable("scene");
}
}
