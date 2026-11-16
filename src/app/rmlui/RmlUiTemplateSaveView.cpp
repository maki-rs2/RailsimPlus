#include "RmlUiTemplateSaveView.h"

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiConfirmDialog.h"
#include "../action/TemplateAction.h"
#include "../plugin_session/TemplateSaver.h"

namespace rs::app::rmlui {
using namespace workspace;

RmlUiTemplateSaveView::RmlUiTemplateSaveView(ToastQueue& queue)
    : queue_(queue) {}

void RmlUiTemplateSaveView::Attach(rs::editor::Editor& editor, PluginSelector& selector,
                                   const std::vector<std::string>& pluginRoots) {
    editor_ = &editor;
    selector_ = &selector;
    pluginRoots_ = &pluginRoots;
}

void RmlUiTemplateSaveView::RegisterModel(Rml::DataModelConstructor& constructor, Rml::Context& context) {
    context_ = &context;
    Rml::StructHandle<TemplateSavePresentation> model = constructor.RegisterStruct<TemplateSavePresentation>();
    model.RegisterMember("layingEditing", &TemplateSavePresentation::layingEditing_);
    model.RegisterMember("formationId", &TemplateSavePresentation::formationId_);
    model.RegisterMember("draft", &TemplateSavePresentation::draft_);
    constructor.Bind("templateSave", &presentation_);
    constructor.BindEventCallback("template_save_edit", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList&) { OnEdit(event); });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiTemplateSaveView::RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog) {
    menu.RegisterProvider(ContextMenuKind::LayingSet, this);
    dialog_ = &dialog;
}

void RmlUiTemplateSaveView::Begin(TemplateSaveTarget target, const std::string& targetId) {
    // 名前入力を始める時点の保存先の一覧。保存先のルートが無いときは空(保存の側が通知する)。
    const std::string root = pluginRoots_ != nullptr && !pluginRoots_->empty() ? pluginRoots_->front() : std::string();
    const TemplateKind kind = target == TemplateSaveTarget::LayingSet ? TemplateKind::RailwayPluginSet
                                                                       : TemplateKind::TrainGroupTemplate;
    presentation_.sources_[static_cast<std::size_t>(target)].existingNames_ = ListTemplateNames(root, kind);
    presentation_.Begin(target, targetId);
    pendingFocus_ = true;
    modelHandle_.DirtyVariable("templateSave");
}

void RmlUiTemplateSaveView::Refresh() {
    if (!pendingFocus_ || context_ == nullptr || !presentation_.IsEditing()) return;
    Rml::ElementDocument* const document = context_->GetDocument(0);
    if (document == nullptr) return;
    const char* const selector = presentation_.layingEditing_
        ? ".laying-plugin-set.is-editing .formation-card-name-input"
        : ".formation-card.is-template-saving .formation-card-name-input";
    Rml::ElementFormControlInput* const input =
        rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->QuerySelector(selector));
    if (input == nullptr || !input->IsVisible(true)) return;
    pendingFocus_ = false;
    input->SetValue(presentation_.draft_);
    input->Focus();
    input->Select();
}

void RmlUiTemplateSaveView::SetActive(bool rail, bool train) {
    railActive_ = rail;
    if ((presentation_.layingEditing_ && !rail) || (IsEditingFormation() && !train)) CloseTransient();
}

bool RmlUiTemplateSaveView::CloseTransient() {
    if (!presentation_.IsEditing()) return false;
    presentation_.Cancel();
    pendingFocus_ = false;
    modelHandle_.DirtyVariable("templateSave");
    return true;
}

void RmlUiTemplateSaveView::OnEdit(Rml::Event& event) {
    if (!presentation_.IsEditing()) return;
    event.StopPropagation();
    if (event.GetId() == Rml::EventId::Keydown) {
        if (event.GetParameter<int>("key_identifier", 0) == static_cast<int>(Rml::Input::KI_ESCAPE)) {
            CloseTransient();
            event.GetCurrentElement()->Blur();
        }
        return;
    }
    Rml::ElementFormControlInput* const input =
        rmlui_dynamic_cast<Rml::ElementFormControlInput*>(event.GetCurrentElement());
    if (input == nullptr) return;
    presentation_.draft_ = input->GetValue();
    if (event.GetId() != Rml::EventId::Blur && !event.GetParameter<bool>("linebreak", false)) return;
    const std::optional<TemplateSaveRequest> request = presentation_.Commit();
    if (presentation_.IsEditing()) return;
    pendingFocus_ = false;
    // 確認を開く前に入力を終え、blurで同じ操作が確定されるのを防ぐ。
    input->Blur();
    modelHandle_.DirtyVariable("templateSave");
    if (request) Run(*request);
    else if (presentation_.overwritePending_) OpenOverwrite();
}

void RmlUiTemplateSaveView::OpenOverwrite() {
    if (dialog_ == nullptr) return;
    ConfirmDialogContent content;
    content.message = presentation_.overwriteMessage_;
    content.confirmLabel = "上書き";
    content.onConfirm = [this]() {
        const std::optional<TemplateSaveRequest> request = presentation_.ConfirmOverwrite();
        if (request) Run(*request);
    };
    content.onCancel = [this]() { presentation_.Cancel(); };
    dialog_->Open(std::move(content));
}

void RmlUiTemplateSaveView::Run(const TemplateSaveRequest& request) {
    if (editor_ == nullptr || selector_ == nullptr || pluginRoots_ == nullptr) return;
    using Save = void (*)(ToastQueue&, const template_action::TemplateSaveRefs&, std::string_view, std::string_view);
    static constexpr std::array<Save, 2> kSave{
        template_action::SaveLayingTemplate, template_action::SaveFormationTemplate};
    const template_action::TemplateSaveRefs refs{*editor_, *selector_, *pluginRoots_};
    kSave[static_cast<std::size_t>(request.target_)](queue_, refs, request.targetId_, request.name_);
}

bool RmlUiTemplateSaveView::TargetExists(ContextMenuKind, const std::string& targetId) const {
    return railActive_ && targetId == "layingSet";
}

bool RmlUiTemplateSaveView::IsItemEnabled(ContextMenuKind kind, std::string_view,
                                        const std::string& targetId) const {
    return TargetExists(kind, targetId);
}

void RmlUiTemplateSaveView::OnItemChosen(ContextMenuKind, std::string_view itemId, const std::string& targetId) {
    if (itemId == "addToTemplate") Begin(TemplateSaveTarget::LayingSet, targetId);
}
} // namespace rs::app::rmlui
