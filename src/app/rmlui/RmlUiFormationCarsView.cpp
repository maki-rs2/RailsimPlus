#include "RmlUiFormationCarsView.h"

#include <vector>
#include <algorithm>
#include <windows.h>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include "../action/FormationAction.h"
#include "../action/TemplateAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../workspace/FormationCarSlots.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"

namespace rs::app::rmlui {

// 強調する`+`。どれでもないときは、カードのidが空でスロットが負。
struct FormationDropState {
    Rml::String cardId;
    int slot = -1;
    bool dragging = false;
};

namespace {

constexpr const char* kDropCardKey = "formationDropCard";
constexpr const char* kDropSlotKey = "formationDropSlot";
// ドラッグ中(何かをつかんでいる間)か。車両の間の`+`はこの間だけ出す。
constexpr const char* kDraggingKey = "formationDragging";
constexpr int kNoSlot = -1;

} // namespace

RmlUiFormationCarsView::RmlUiFormationCarsView() : dropState_(std::make_unique<FormationDropState>()) {}
RmlUiFormationCarsView::~RmlUiFormationCarsView() = default;

void RmlUiFormationCarsView::Attach(const FormationRefs& refs, const plugin_browser::PluginBrowserService& browser,
                                    ToastQueue& queue) {
    refs_ = std::make_unique<FormationRefs>(refs);
    browser_ = &browser;
    queue_ = &queue;
}

void RmlUiFormationCarsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    constructor.Bind(kDropCardKey, &dropState_->cardId);
    constructor.Bind(kDropSlotKey, &dropState_->slot);
    constructor.Bind(kDraggingKey, &dropState_->dragging);
    constructor.BindEventCallback("focus_formation_car", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
        const int index = FirstEventArgument<int>(arguments, -1);
        if (index >= 0) focusedCarIndex_ = index;
    });

    constructor.BindEventCallback(
        "remove_formation_car", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            // 車両・カードのクリック(選択)へ伝えない。
            event.StopPropagation();
            const std::optional<std::size_t> defIndex = FindDefIndex(FirstEventArgument<Rml::String>(arguments, Rml::String()));
            const int carIndex = EventArgumentAt<int>(arguments, 1, -1);
            if (defIndex.has_value() && carIndex >= 0) FormationAction(*refs_).RemoveCar(*defIndex, static_cast<std::size_t>(carIndex));
        });
    constructor.BindEventCallback(
        "toggle_formation_car_reversed",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            event.StopPropagation();
            const std::optional<std::size_t> defIndex = FindDefIndex(FirstEventArgument<Rml::String>(arguments, Rml::String()));
            const int carIndex = EventArgumentAt<int>(arguments, 1, -1);
            if (!defIndex.has_value() || carIndex < 0) return;
            FormationAction action(*refs_);
            const FormationViewModel viewModel = action.ViewModel();
            const std::vector<FormationCarViewModel>& cars = viewModel.defs[*defIndex].cars;
            if (static_cast<std::size_t>(carIndex) >= cars.size()) return;
            action.SetCarReversed(*defIndex, static_cast<std::size_t>(carIndex), !cars[static_cast<std::size_t>(carIndex)].reversed);
        });

    constructor.BindEventCallback(
        "begin_drag_plugin", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            BeginPluginDrag(FirstEventArgument<int>(arguments, -1));
        });
    constructor.BindEventCallback(
        "begin_drag_car", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            BeginCarDrag(FirstEventArgument<Rml::String>(arguments, Rml::String()), EventArgumentAt<int>(arguments, 1, -1));
        });
    constructor.BindEventCallback(
        "begin_drag_template", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            BeginTemplateDrag(FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback(
        "end_drag", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { EndDrag(); });
    constructor.BindEventCallback(
        "drag_over_slot", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            OnDragOver(event, FirstEventArgument<Rml::String>(arguments, Rml::String()), EventArgumentAt<int>(arguments, 1, kNoSlot));
        });
    constructor.BindEventCallback(
        "drag_out_slot", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            OnDragOut(event, FirstEventArgument<Rml::String>(arguments, Rml::String()), EventArgumentAt<int>(arguments, 1, kNoSlot));
        });
    constructor.BindEventCallback(
        "drop_on_slot", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            OnDrop(event, FirstEventArgument<Rml::String>(arguments, Rml::String()), EventArgumentAt<int>(arguments, 1, kNoSlot));
        });

    modelHandle_ = constructor.GetModelHandle();
}

bool RmlUiFormationCarsView::HandleKeyDown(unsigned int virtualKey, Rml::Element* hover) {
    if (!refs_ || hover == nullptr) return false;
    bool inCar = false;
    for (Rml::Element* element = hover; element != nullptr; element = element->GetParentNode()) {
        if (element->IsClassSet("formation-panel-car")) {
            inCar = true;
            break;
        }
        if (element->IsClassSet("formation-panel")) break;
    }
    if (!inCar) return false;
    FormationAction action(*refs_);
    const FormationViewModel viewModel = action.ViewModel();
    std::optional<std::size_t> selectedDef;
    for (std::size_t i = 0; i < viewModel.defs.size(); ++i) {
        if (viewModel.defs[i].selected) selectedDef = i;
    }
    if (!selectedDef.has_value()) return false;
    const std::vector<FormationCarViewModel>& cars = viewModel.defs[*selectedDef].cars;
    if (cars.empty()) return false;
    const bool hasFocusedCar = focusedCarIndex_ >= 0 && static_cast<std::size_t>(focusedCarIndex_) < cars.size();
    if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT) {
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (!hasFocusedCar) return true;
            const int direction = virtualKey == VK_LEFT ? -1 : 1;
            const int target = focusedCarIndex_ + direction;
            if (target >= 0 && static_cast<std::size_t>(target) < cars.size()) {
                action.MoveCar(static_cast<std::size_t>(focusedCarIndex_), direction);
                focusedCarIndex_ = target;
            }
        } else {
            const int direction = virtualKey == VK_LEFT ? -1 : 1;
            focusedCarIndex_ = hasFocusedCar
                                   ? std::clamp(focusedCarIndex_ + direction, 0, static_cast<int>(cars.size()) - 1)
                                   : (direction > 0 ? 0 : static_cast<int>(cars.size()) - 1);
        }
        return true;
    }
    if (virtualKey == VK_DELETE) {
        if (!hasFocusedCar) focusedCarIndex_ = 0;
        action.RemoveCar(*selectedDef, static_cast<std::size_t>(focusedCarIndex_));
        focusedCarIndex_ = cars.size() <= 1 ? -1 : std::min(focusedCarIndex_, static_cast<int>(cars.size()) - 2);
        return true;
    }
    return false;
}

bool RmlUiFormationCarsView::InsertPluginAfterFocused(int entryId) {
    if (!refs_ || browser_ == nullptr || entryId < 0) return false;
    const std::vector<plugin_browser::PluginCatalogEntry>& entries = browser_->Catalog().Entries();
    if (static_cast<std::size_t>(entryId) >= entries.size()) return false;
    FormationAction action(*refs_);
    const FormationViewModel viewModel = action.ViewModel();
    for (std::size_t defIndex = 0; defIndex < viewModel.defs.size(); ++defIndex) {
        if (!viewModel.defs[defIndex].selected) continue;
        const std::size_t slot = focusedCarIndex_ < 0 ? viewModel.defs[defIndex].cars.size()
                                                       : std::min(static_cast<std::size_t>(focusedCarIndex_ + 1),
                                                                  viewModel.defs[defIndex].cars.size());
        action.InsertCar(defIndex, slot, entries[static_cast<std::size_t>(entryId)].folderUtf8);
        focusedCarIndex_ = static_cast<int>(slot);
        return true;
    }
    return false;
}

std::optional<std::size_t> RmlUiFormationCarsView::FindDefIndex(const std::string& cardId) const {
    if (!refs_) return std::nullopt;
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(cardId);
    if (!guid.has_value()) return std::nullopt;
    return FormationAction(*refs_).FindFormationIndex(*guid);
}

void RmlUiFormationCarsView::BeginPluginDrag(int entryId) {
    payload_ = Payload();
    if (browser_ == nullptr || entryId < 0) return;
    const std::vector<plugin_browser::PluginCatalogEntry>& entries = browser_->Catalog().Entries();
    if (static_cast<std::size_t>(entryId) >= entries.size()) return;
    payload_.kind = PayloadKind::Plugin;
    payload_.folderUtf8 = entries[static_cast<std::size_t>(entryId)].folderUtf8;
    UpdateVariable(modelHandle_, kDraggingKey, dropState_->dragging, true);
}

void RmlUiFormationCarsView::BeginCarDrag(const std::string& cardId, int carIndex) {
    payload_ = Payload();
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(cardId);
    if (!guid.has_value() || carIndex < 0) return;
    payload_.kind = PayloadKind::Car;
    payload_.formationId = *guid;
    payload_.carIndex = static_cast<std::size_t>(carIndex);
    UpdateVariable(modelHandle_, kDraggingKey, dropState_->dragging, true);
}

void RmlUiFormationCarsView::BeginTemplateDrag(const std::string& name) {
    payload_ = Payload();
    if (name.empty()) return;
    payload_.kind = PayloadKind::Template;
    payload_.templateName = name;
    UpdateVariable(modelHandle_, kDraggingKey, dropState_->dragging, true);
}

void RmlUiFormationCarsView::EndDrag() {
    payload_ = Payload();
    SetDropTarget(Rml::String(), kNoSlot);
    UpdateVariable(modelHandle_, kDraggingKey, dropState_->dragging, false);
}

std::optional<RmlUiFormationCarsView::DropPlan> RmlUiFormationCarsView::ResolveDrop(const std::string& cardId,
                                                                                    int slot) const {
    if (payload_.kind == PayloadKind::None || slot < 0) return std::nullopt;
    const std::optional<std::size_t> defIndex = FindDefIndex(cardId);
    if (!defIndex.has_value()) return std::nullopt;
    const std::size_t carCount = FormationAction(*refs_).ViewModel().defs[*defIndex].cars.size();
    const std::size_t slotIndex = static_cast<std::size_t>(slot);

    DropPlan plan;
    plan.defIndex = *defIndex;
    plan.slot = slotIndex;
    plan.payload = payload_;
    if (payload_.kind == PayloadKind::Plugin || payload_.kind == PayloadKind::Template) {
        return workspace::IsValidInsertSlot(carCount, slotIndex) ? std::optional<DropPlan>(plan) : std::nullopt;
    }
    // 車両の移動は同じ編成の中だけ。元の編成は、ドラッグ中に並びが変わっても取り違えないようguidで引き直す。
    if (payload_.formationId != rs::world::GuidFromHex(cardId).value_or(rs::world::NilGuid())) return std::nullopt;
    return workspace::ResolveCarMoveTarget(carCount, payload_.carIndex, slotIndex).has_value() ? std::optional<DropPlan>(plan)
                                                                                                 : std::nullopt;
}

void RmlUiFormationCarsView::SetDropTarget(const std::string& cardId, int slot) {
    UpdateVariable(modelHandle_, kDropCardKey, dropState_->cardId, Rml::String(cardId));
    UpdateVariable(modelHandle_, kDropSlotKey, dropState_->slot, slot);
}

// ドロップ先(`+`の帯・編成カード)は中に子要素を持つ。子要素の上でも泡のように届くので、対象の確認はしない。
// 子から子へ動くときはdragout→dragoverが同じ更新の中で続き、強調は描画の前に付け直される。
void RmlUiFormationCarsView::OnDragOver(Rml::Event&, const std::string& cardId, int slot) {
    if (ResolveDrop(cardId, slot).has_value()) SetDropTarget(cardId, slot);
}

void RmlUiFormationCarsView::OnDragOut(Rml::Event&, const std::string& cardId, int slot) {
    if (dropState_->cardId == cardId && dropState_->slot == slot) SetDropTarget(Rml::String(), kNoSlot);
}

void RmlUiFormationCarsView::OnDrop(Rml::Event&, const std::string& cardId, int slot) {
    const std::optional<DropPlan> plan = ResolveDrop(cardId, slot);
    if (plan.has_value()) {
        FormationAction action(*refs_);
        if (plan->payload.kind == PayloadKind::Plugin) {
            action.InsertCar(plan->defIndex, plan->slot, plan->payload.folderUtf8);
        } else if (plan->payload.kind == PayloadKind::Template) {
            if (queue_ != nullptr) template_action::InsertFormationTemplate(*queue_, plan->payload.templateName);
        } else {
            // 元の車両の添字は、ドロップ時点の編成の並びに対するもの(ドラッグ中に並びが変わらない前提)。
            action.MoveCarToSlot(plan->defIndex, plan->payload.carIndex, plan->slot);
        }
    }
    EndDrag();
}

} // namespace rs::app::rmlui
