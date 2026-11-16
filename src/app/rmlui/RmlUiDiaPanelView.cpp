#include "RmlUiDiaPanelView.h"

#include <algorithm>
#include <string_view>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiDiaPanelModel.h"
#include "RmlUiUpdatePhase.h"
#include "../action/DiaPanelPresentationBuilder.h"
#include "../action/StationDiaAction.h"
#include "../camera/CameraControl.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginSelector.h"
#include "../scene/SceneRenderer.h"
#include "../../editor/tool/Editor.h"

namespace rs::app::rmlui {
namespace {
using namespace workspace;

using Action = void (*)(const DiaPanelActionRefs&, const DiaPanelActionRequest&);
struct ActionBinding {
    std::string_view name;
    Action action;
};
// 操作名と入口の対応は、入力欄・ボタン・ドロップで共有する。
constexpr ActionBinding kActions[] = {
    {"FlyToStation", dia_panel_action::FlyToStation}, {"RenameStation", dia_panel_action::RenameStation},
    {"DeleteStation", dia_panel_action::DeleteStation}, {"MovePlatform", dia_panel_action::MovePlatform},
    {"ToggleDoor", dia_panel_action::ToggleDoor}, {"ChangeOpening", dia_panel_action::ChangeOpening},
    {"AddLayer", dia_panel_action::AddLayer}, {"DeleteLayer", dia_panel_action::DeleteLayer},
    {"ReorderLayer", dia_panel_action::ReorderLayer}, {"AddElement", dia_panel_action::AddElement},
    {"DeleteElement", dia_panel_action::DeleteElement}, {"DisableElement", dia_panel_action::DisableElement},
    {"ReorderElement", dia_panel_action::ReorderElement}, {"ChangeOperation", dia_panel_action::ChangeOperation},
    {"ChangeDepartureType", dia_panel_action::ChangeDepartureType},
    {"ChangeDepartureTime", dia_panel_action::ChangeDepartureTime},
    {"AddDepartureTime", dia_panel_action::AddDepartureTime},
    {"DeleteDepartureTime", dia_panel_action::DeleteDepartureTime},
    {"ChangeConnectionWait", dia_panel_action::ChangeConnectionWait},
    {"ResetStopPosition", dia_panel_action::ResetStopPosition}, {"ChangeStopPosition", dia_panel_action::ChangeStopPosition}, {"ChangeRunInfo", dia_panel_action::ChangeRunInfo},
    {"AddRunInfo", dia_panel_action::AddRunInfo}, {"DeleteRunInfo", dia_panel_action::DeleteRunInfo},
    {"EditCondition", dia_panel_action::EditCondition},
    {"ChangeBranchHand", dia_panel_action::ChangeBranchHand}};

// シーンの切り替えが終わるのを待つ最大のフレーム数(読込中などで切り替えが続けて断られるとき)。
constexpr int kMaxNavigationWaitFrames = 600;

template <typename T>
T* At(std::vector<T>& values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? &values[index] : nullptr;
}
} // namespace

RmlUiDiaPanelView::RmlUiDiaPanelView(ToastQueue& queue) : queue_(queue) {}

void RmlUiDiaPanelView::RegisterModel(Rml::DataModelConstructor& constructor) {
    RegisterDiaPanelModel(constructor);
    constructor.Bind("dia", &presentation_);
    constructor.BindEventCallback("dia_ui", [this](Rml::DataModelHandle, Rml::Event& event,
                                                   const Rml::VariantList& arguments) { OnUi(event, arguments); });
    constructor.BindEventCallback("dia_action", [this](Rml::DataModelHandle, Rml::Event& event,
                                                       const Rml::VariantList& arguments) { OnAction(event, arguments, false); });
    constructor.BindEventCallback("dia_commit", [this](Rml::DataModelHandle, Rml::Event& event,
                                                       const Rml::VariantList& arguments) { OnAction(event, arguments, true); });
    constructor.BindEventCallback("dia_hover", [this](Rml::DataModelHandle, Rml::Event&,
                                                      const Rml::VariantList& arguments) { OnHover(arguments); });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiDiaPanelView::Attach(rs::editor::Editor& editor, LayoutSession& session, PluginSelector& selector,
                               SceneRenderer& renderer, CameraController& camera, std::function<void(int)> switchScene) {
    editor_ = &editor;
    session_ = &session;
    selector_ = &selector;
    renderer_ = &renderer;
    camera_ = &camera;
    switchScene_ = std::move(switchScene);
}

void RmlUiDiaPanelView::SetActive(bool active) {
    if (presentation_.active_ == active) return;
    presentation_.SetActive(active);
    pendingFocus_ = false;
    navigation_ = {};
    built_ = false;
    if (active && Attached()) lastSelectedPlatform_ = editor_->selectedPlatform;
    modelHandle_.DirtyVariable("dia");
}

bool RmlUiDiaPanelView::CloseTransient() {
    if (!presentation_.active_ || presentation_.screen_ == 0) return false;
    presentation_.Back();
    modelHandle_.DirtyVariable("dia");
    return true;
}

void RmlUiDiaPanelView::Refresh() {
    if (!pendingFocus_ || document_ == nullptr) return;
    Rml::Element* const input = document_->QuerySelector(".dia-name-input.dia-editing");
    if (input == nullptr) return;
    pendingFocus_ = false;
    input->Focus();
}

void RmlUiDiaPanelView::Tick() {
    if (!presentation_.active_ || !Attached()) return;
    ProcessNavigation();
    const std::size_t sceneCount = std::max<std::size_t>(1, session_->scenes.size());
    if (!built_ || forceRebuild_ || builtRevision_ != editor_->DocumentRevision() ||
        builtActiveScene_ != session_->activeIndex || builtSceneCount_ != sceneCount) {
        Rebuild();
    }
    SyncSelectedPlatform();
}

void RmlUiDiaPanelView::Rebuild() {
    DiaPanelBuildInput input;
    if (session_->scenes.empty()) {
        input.scenes.push_back({std::string(), &editor_->graph});
    } else {
        for (std::size_t index = 0; index < session_->scenes.size(); ++index) {
            const bool active = index == session_->activeIndex;
            input.scenes.push_back({session_->scenes[index].name, active ? &editor_->graph : &session_->scenes[index].graph});
        }
        input.activeScene = static_cast<int>(session_->activeIndex);
    }
    for (const Formation& formation : selector_->Formations()) input.formations.push_back({formation.guid, formation.name});
    for (const rs::world::TrainTagDef& tag : renderer_->TrainTagDefs()) input.tags.push_back({tag.id, tag.label});
    DiaPanelPresentation next = BuildDiaPanelPresentation(input);
    CarryOverDiaPanelState(presentation_, next);
    presentation_ = std::move(next);
    built_ = true;
    forceRebuild_ = false;
    builtRevision_ = editor_->DocumentRevision();
    builtActiveScene_ = session_->activeIndex;
    builtSceneCount_ = std::max<std::size_t>(1, session_->scenes.size());
    modelHandle_.DirtyVariable("dia");
}

// 切り替えが終わるまで要求を持ち続ける(読込中は切り替えが断られるので、毎フレーム頼み直す)。
void RmlUiDiaPanelView::ProcessNavigation() {
    if (!navigation_.pending_) return;
    const int active = static_cast<int>(session_->activeIndex);
    if (active != navigation_.sceneIndex_) {
        if (++navigation_.waitedFrames_ > kMaxNavigationWaitFrames) {
            navigation_ = {};
            return;
        }
        if (switchScene_) switchScene_(navigation_.sceneIndex_ - active);
        if (static_cast<int>(session_->activeIndex) != navigation_.sceneIndex_) return;
    }
    if (navigation_.selectPlatform_) {
        StationDiaAction(*editor_).SelectPlatform(navigation_.platformId_);
        lastSelectedPlatform_ = navigation_.platformId_;
    }
    if (navigation_.fly_) camera_->FlyTo({navigation_.flyX_, navigation_.flyY_, navigation_.flyZ_});
    navigation_ = {};
}

// ダイヤモードで3D画面から番線が選ばれたら、左タブもその番線の画面へ移す。
void RmlUiDiaPanelView::SyncSelectedPlatform() {
    const rs::world::Guid selected = editor_->selectedPlatform;
    if (selected == lastSelectedPlatform_) return;
    lastSelectedPlatform_ = selected;
    if (rs::world::GuidIsNil(selected)) return;
    for (std::size_t index = 0; index < presentation_.targets_.size(); ++index) {
        const DiaPanelTarget& target = presentation_.targets_[index];
        if (target.platformId_ != selected || target.sceneIndex_ != presentation_.activeScene_) continue;
        if (presentation_.screen_ == 0 || presentation_.targetIndex_ != static_cast<int>(index)) {
            presentation_.OpenTarget(static_cast<int>(index));
            modelHandle_.DirtyVariable("dia");
        }
        return;
    }
}

// 左タブで番線を開く。アクティブなシーンの番線なら選択へ反映し、別のシーンの番線なら切り替えを頼む
// (切り替えが終わるまでは、キャッシュの値で表示するだけで操作はしない)。
void RmlUiDiaPanelView::OpenTarget(int targetIndex) {
    if (!presentation_.OpenTarget(targetIndex) || !Attached()) return;
    const DiaPanelTarget& target = presentation_.targets_[static_cast<std::size_t>(targetIndex)];
    navigation_ = {};
    if (target.sceneIndex_ == presentation_.activeScene_) {
        StationDiaAction(*editor_).SelectPlatform(target.platformId_);
        lastSelectedPlatform_ = target.platformId_;
        return;
    }
    navigation_.pending_ = true;
    navigation_.sceneIndex_ = target.sceneIndex_;
    navigation_.selectPlatform_ = true;
    navigation_.platformId_ = target.platformId_;
}

void RmlUiDiaPanelView::OpenPlatform(const rs::world::Guid& platformId) {
    for (std::size_t index = 0; index < presentation_.targets_.size(); ++index) {
        const DiaPanelTarget& target = presentation_.targets_[index];
        if (target.platformId_ != platformId || target.sceneIndex_ != presentation_.activeScene_) continue;
        OpenTarget(static_cast<int>(index));
        modelHandle_.DirtyVariable("dia");
        return;
    }
}

void RmlUiDiaPanelView::OnUi(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    const int index = EventArgumentAt<int>(arguments, 1, -1);
    if (kind == "target") OpenTarget(index);
    else if (kind == "layer") presentation_.OpenLayer(index);
    else if (kind == "back") presentation_.Back();
    else if (kind == "home") presentation_.BackToList();
    else if (kind == "fold") presentation_.ToggleFold(index);
    else if (kind == "element") presentation_.OpenElement(index);
    else if (kind == "condition") presentation_.ToggleCondition(index);
    else if (kind == "times") presentation_.ToggleTimes(index);
    else if (kind == "addLayer") presentation_.addingLayer_ = !presentation_.addingLayer_;
    else if (kind == "addOverwrite") presentation_.addingOverwrite_ = !presentation_.addingOverwrite_;
    else if (kind == "rename") {
        presentation_.BeginRename(index);
        document_ = event.GetCurrentElement()->GetOwnerDocument();
        pendingFocus_ = true;
    } else if (kind == "drag") {
        presentation_.dragIndex_ = index;
        presentation_.dragKind_ = EventArgumentAt<std::string>(arguments, 2, "");
    } else if (kind == "dragEnd") {
        presentation_.dragIndex_ = -1;
        presentation_.dragKind_.clear();
    }
    modelHandle_.DirtyVariable("dia");
}

void RmlUiDiaPanelView::OnHover(const Rml::VariantList& arguments) {
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    const int index = EventArgumentAt<int>(arguments, 1, -1);
    const bool hovered = EventArgumentAt<bool>(arguments, 2, false);
    bool* flag = nullptr;
    if (kind == "list") {
        if (workspace::DiaPanelListRow* row = At(presentation_.rows_, index)) flag = &row->hovered_;
    } else if (workspace::DiaPanelTarget* target = At(presentation_.targets_, presentation_.targetIndex_)) {
        if (kind == "layer") {
            if (workspace::DiaPanelLayer* layer = At(target->layers_, index)) flag = &layer->hovered_;
        } else if (workspace::DiaPanelLayer* layer = presentation_.CurrentLayer()) {
            if (workspace::DiaPanelElement* element = At(layer->elements_, index)) {
                if (kind == "element") flag = &element->hovered_;
                else if (workspace::DiaPanelField* time = At(element->times_, EventArgumentAt<int>(arguments, 3, -1))) {
                    flag = &time->hovered_;
                }
            }
        }
    }
    if (flag != nullptr && *flag != hovered) {
        *flag = hovered;
        modelHandle_.DirtyVariable("dia");
    }
}

void RmlUiDiaPanelView::OnAction(Rml::Event& event, const Rml::VariantList& arguments, bool commit) {
    event.StopPropagation();
    // チェックボックス・選択のchangeは、データ結線が値を書き換えたときにも届く(別の番線へ移ったときなど)。
    // 切り替えの操作は、ユーザーが変えたときだけ行う。
    if (event.GetId() == Rml::EventId::Change && IsInsideContextUpdate()) return;
    const std::string name = EventArgumentAt<std::string>(arguments, 0, "");
    if (commit && event.GetId() == Rml::EventId::Keydown) {
        const int key = event.GetParameter<int>("key_identifier", 0);
        if (key == static_cast<int>(Rml::Input::KI_ESCAPE) && name == "RenameStation") {
            presentation_.EndRename();
            pendingFocus_ = false;
            modelHandle_.DirtyVariable("dia");
            return;
        }
        if (key != static_cast<int>(Rml::Input::KI_RETURN)) return;
    }
    DiaPanelActionRequest request;
    request.targetIndex_ = presentation_.targetIndex_;
    request.layerIndex_ = presentation_.layerIndex_;
    request.rowIndex_ = EventArgumentAt<int>(arguments, 1, -1);
    request.itemIndex_ = EventArgumentAt<int>(arguments, 2, -1);
    request.field_ = EventArgumentAt<std::string>(arguments, 3, "");
    request.value_ = EventArgumentAt<std::string>(arguments, 4, "");
    if (commit || event.GetId() == Rml::EventId::Change) {
        if (Rml::ElementFormControl* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(event.GetCurrentElement())) {
            request.value_ = control->GetValue();
        }
    }
    if (name == "RenameStation") {
        workspace::DiaPanelListRow* const row = At(presentation_.rows_, request.rowIndex_);
        if (row == nullptr || !row->editing_) return;
        presentation_.EndRename();
        pendingFocus_ = false;
    }
    if (event.GetId() == Rml::EventId::Dragdrop) {
        const std::string expected = name == "MovePlatform" ? "platform" : name == "ReorderLayer" ? "layer" : "element";
        if (presentation_.dragKind_ != expected || presentation_.dragIndex_ < 0) return;
        request.sourceIndex_ = presentation_.dragIndex_;
    }
    if (!Attached()) return;
    for (const ActionBinding& binding : kActions) {
        if (binding.name != name) continue;
        const DiaPanelActionRefs refs{queue_, *editor_, presentation_, navigation_};
        if (name == "DeleteStation" && confirmDialog_ != nullptr) {
            // 確認を待つ間に一覧が作り直されても別の駅を消さないよう、開いた時点の駅のGuidを覚えておく。
            const workspace::DiaPanelListRow* const row = At(presentation_.rows_, request.rowIndex_);
            if (row == nullptr) return;
            confirmDialog_->Open("この駅を削除しますか? 番線とルーチン表は残ります。", "削除", true,
                                 [this, request, action = binding.action, id = row->id_, scene = row->sceneIndex_]() {
                                     const workspace::DiaPanelListRow* const current = At(presentation_.rows_, request.rowIndex_);
                                     if (current == nullptr || current->kind_ != "station" || current->id_ != id ||
                                         current->sceneIndex_ != scene) {
                                         return;
                                     }
                                     action({queue_, *editor_, presentation_, navigation_}, request);
                                     forceRebuild_ = true;
                                 });
        } else {
            binding.action(refs, request);
        }
        break;
    }
    // 書換える項目を選んだら、選択肢の一覧は閉じる。
    if (name == "AddRunInfo") presentation_.addingOverwrite_ = false;
    // 操作で値が変わらなかったとき(読めない入力など)も、入力欄を元の値へ戻すため作り直す。
    forceRebuild_ = true;
    modelHandle_.DirtyVariable("dia");
}
} // namespace rs::app::rmlui
