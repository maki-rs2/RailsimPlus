#include "RmlUiFormationListView.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiContextMenu.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiFormationImages.h"
#include "RmlUiUpdateVariable.h"
#include "RmlUiTemplateSaveView.h"
#include "../../world/alignment/GuidJson.h"
#include "../action/FormationAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../ui/FormationPanel.h"

namespace rs::app::rmlui {

// カード内の車両1両。反転車両は印を付けて示す。
struct FormationCarModel {
    Rml::String label;
    bool reversed = false;
    bool focused = false;
    // 車両の絵のソース名("pbimg:..."、反転車両は反対側から撮った画像)。届くまでは空。
    Rml::String image;
    bool operator==(const FormationCarModel&) const = default;
};

// 編成カードのタグ1つ。idはタグの定義のid(外すときの引数)。
struct FormationTagModel {
    int id = 0;
    Rml::String label;
    bool operator==(const FormationTagModel&) const = default;
};

// 編成カード1枚。indexはイベントselect_formationの引数、idは編成のguid(16進)で、
// 削除・名前編集・右クリックメニューの対象に使う。
struct FormationCardModel {
    int index = 0;
    Rml::String id;
    Rml::String name;
    int carCount = 0;
    bool selected = false;
    // この編成から置いた列車があるか(カードに「配置済み」を出す)。
    bool placed = false;
    // 先頭車のアイコンのソース名(先頭が反転していれば進行方向の前面が写る向き)。車両が無い・届くまでは空。
    Rml::String icon;
    Rml::Vector<FormationCarModel> cars;
    Rml::Vector<FormationTagModel> tags;
    bool operator==(const FormationCardModel&) const = default;
};

struct FormationListModel {
    bool sectionVisible = false;
    bool panelVisible = false;
    Rml::Vector<FormationCardModel> cards;
    bool placementReversedHead = false;
    // 名前を編集中のカードのid。編集中でなければ空。
    Rml::String editingId;
};

namespace {

// 登録(RegisterModel)と更新(Refresh)の両方で使うバインド名。
constexpr const char* kSectionVisibleKey = "formationSectionVisible";
constexpr const char* kPanelVisibleKey = "formationPanelVisible";
constexpr const char* kCardsKey = "formationCards";
constexpr const char* kPlacementReversedHeadKey = "placementReversedHead";
constexpr const char* kEditingIdKey = "formationEditingId";

// 編成一覧の外枠と、名前の編集中の入力欄を見つけるためのセレクタ(workspace.rmlのクラス名)。
constexpr const char* kSectionClass = "formation-section";
constexpr const char* kEditingInputSelector = ".formation-card.is-editing .formation-card-name-input";

// カードのidは編成のguidの16進文字列。
std::optional<rs::world::Guid> ParseCardId(const std::string& cardId) {
    return rs::world::GuidFromHex(cardId);
}

void RegisterModelTypes(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<FormationCarModel> handle = constructor.RegisterStruct<FormationCarModel>()) {
        handle.RegisterMember("label", &FormationCarModel::label);
        handle.RegisterMember("reversed", &FormationCarModel::reversed);
        handle.RegisterMember("focused", &FormationCarModel::focused);
        handle.RegisterMember("image", &FormationCarModel::image);
    }
    constructor.RegisterArray<Rml::Vector<FormationCarModel>>();
    if (Rml::StructHandle<FormationTagModel> handle = constructor.RegisterStruct<FormationTagModel>()) {
        handle.RegisterMember("id", &FormationTagModel::id);
        handle.RegisterMember("label", &FormationTagModel::label);
    }
    constructor.RegisterArray<Rml::Vector<FormationTagModel>>();
    if (Rml::StructHandle<FormationCardModel> handle = constructor.RegisterStruct<FormationCardModel>()) {
        handle.RegisterMember("index", &FormationCardModel::index);
        handle.RegisterMember("id", &FormationCardModel::id);
        handle.RegisterMember("name", &FormationCardModel::name);
        handle.RegisterMember("carCount", &FormationCardModel::carCount);
        handle.RegisterMember("selected", &FormationCardModel::selected);
        handle.RegisterMember("placed", &FormationCardModel::placed);
        handle.RegisterMember("icon", &FormationCardModel::icon);
        handle.RegisterMember("cars", &FormationCardModel::cars);
        handle.RegisterMember("tags", &FormationCardModel::tags);
    }
    constructor.RegisterArray<Rml::Vector<FormationCardModel>>();
}

Rml::Vector<FormationCardModel> ToCardModels(const FormationViewModel& viewModel, RmlUiFormationImages& images,
                                              int focusedCarIndex) {
    Rml::Vector<FormationCardModel> cards;
    cards.reserve(viewModel.defs.size());
    for (std::size_t defIndex = 0; defIndex < viewModel.defs.size(); ++defIndex) {
        const FormationRowViewModel& def = viewModel.defs[defIndex];
        FormationCardModel card;
        card.index = static_cast<int>(defIndex);
        card.id = rs::world::GuidToHex(def.guid);
        card.name = def.name;
        card.carCount = static_cast<int>(def.carCount);
        card.selected = def.selected;
        card.placed = def.placed;
        for (const TrainTagViewModel& tag : def.tags) card.tags.push_back({tag.id, tag.label});
        card.cars.reserve(def.cars.size());
        for (std::size_t carIndex = 0; carIndex < def.cars.size(); ++carIndex) {
            const FormationCarViewModel& car = def.cars[carIndex];
            // 反転した車両は反対側の側面から撮った画像。
            const plugin_browser::ThumbnailView view =
                car.reversed ? plugin_browser::ThumbnailView::SideOpposite : plugin_browser::ThumbnailView::Side;
            card.cars.push_back({car.displayName, car.reversed, def.selected && static_cast<int>(carIndex) == focusedCarIndex,
                                 images.Want(car.folderUtf8, view)});
        }
        if (!def.cars.empty()) {
            const FormationCarViewModel& head = def.cars.front();
            card.icon = images.WantHeadIcon(head.folderUtf8, head.reversed);
        }
        cards.push_back(std::move(card));
    }
    return cards;
}

} // namespace

RmlUiFormationListView::RmlUiFormationListView() : model_(std::make_unique<FormationListModel>()) {}
RmlUiFormationListView::~RmlUiFormationListView() = default;

void RmlUiFormationListView::Attach(const FormationRefs& refs, plugin_browser::PluginBrowserService& browser,
                                    ToastQueue& queue) {
    refs_ = std::make_unique<FormationRefs>(refs);
    cars_.Attach(refs, browser, queue);
}

void RmlUiFormationListView::RegisterModel(Rml::DataModelConstructor& constructor) {
    RegisterModelTypes(constructor);
    constructor.Bind(kSectionVisibleKey, &model_->sectionVisible);
    constructor.Bind(kPanelVisibleKey, &model_->panelVisible);
    constructor.Bind(kCardsKey, &model_->cards);
    constructor.Bind(kPlacementReversedHeadKey, &model_->placementReversedHead);
    constructor.Bind(kEditingIdKey, &model_->editingId);

    constructor.BindEventCallback(
        "select_formation", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const int index = EventArgumentAt<int>(arguments, 0, -1);
            if (index >= 0) FormationAction(*refs_).SelectFormation(static_cast<std::size_t>(index));
        });
    constructor.BindEventCallback(
        "add_formation", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (refs_) FormationAction(*refs_).AddFormation();
        });
    constructor.BindEventCallback(
        "toggle_placement_reversed", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (refs_) FormationAction(*refs_).SetPlacementReversedHead(!refs_->uiState.reversedHead);
        });

    constructor.BindEventCallback(
        "remove_formation_card", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            // カード全体のクリック(選択)へ伝えない。
            event.StopPropagation();
            RequestRemove(FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    // カードのタグの`×`。その編成からタグを外す(カードの選択へは伝えない)。
    constructor.BindEventCallback(
        "remove_formation_tag", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            event.StopPropagation();
            if (!refs_) return;
            const std::optional<rs::world::Guid> guid = ParseCardId(FirstEventArgument<Rml::String>(arguments, Rml::String()));
            const int tagId = EventArgumentAt<int>(arguments, 1, -1);
            if (guid.has_value() && tagId >= 0) FormationAction(*refs_).SetFormationTagAssigned(*guid, tagId, false);
        });
    constructor.BindEventCallback(
        "begin_rename_formation", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            OnBeginRename(event, FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback(
        "commit_formation_name",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { OnCommitName(event); });
    constructor.BindEventCallback(
        "key_formation_name",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { OnKeyName(event); });
    constructor.BindEventCallback(
        "stop_card_click", [](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
            // 名前の入力欄の中のクリックでカードの選択を動かさない。
            event.StopPropagation();
        });

    cars_.RegisterModel(constructor);
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiFormationListView::RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog,
                                           RmlUiTemplateSaveView& templateSave) {
    menu.RegisterProvider(workspace::ContextMenuKind::FormationCard, this);
    confirmDialog_ = &dialog;
    templateSave_ = &templateSave;
}

bool RmlUiFormationListView::HandleArrowKey(int direction, Rml::Element* hover) {
    if (!refs_) return false;
    bool inSection = false;
    for (Rml::Element* element = hover; element != nullptr && !inSection; element = element->GetParentNode()) {
        inSection = element->IsClassSet(kSectionClass);
    }
    if (!inSection) return false;

    const FormationViewModel viewModel = FormationAction(*refs_).ViewModel();
    const int count = static_cast<int>(viewModel.defs.size());
    if (count == 0) return false;
    int selected = -1;
    for (int defIndex = 0; defIndex < count; ++defIndex) {
        if (viewModel.defs[static_cast<std::size_t>(defIndex)].selected) selected = defIndex;
    }
    // 選択が無いときは、下なら先頭、上なら末尾から始める。
    int next = selected < 0 ? (direction > 0 ? 0 : count - 1) : selected + direction;
    if (next >= 0 && next < count) FormationAction(*refs_).SelectFormation(static_cast<std::size_t>(next));
    return true;
}

bool RmlUiFormationListView::TargetExists(workspace::ContextMenuKind, const std::string& targetId) const {
    if (!refs_) return false;
    const std::optional<rs::world::Guid> guid = ParseCardId(targetId);
    return guid.has_value() && FormationAction(*refs_).FindFormationIndex(*guid).has_value();
}

bool RmlUiFormationListView::IsItemEnabled(workspace::ContextMenuKind, std::string_view,
                                           const std::string&) const {
    return true;
}

void RmlUiFormationListView::OnItemChosen(workspace::ContextMenuKind, std::string_view itemId,
                                          const std::string& targetId) {
    if (!refs_) return;
    if (itemId == "addToTemplate" && templateSave_ != nullptr) {
        EndEditing();
        templateSave_->Begin(workspace::TemplateSaveTarget::Formation, targetId);
        return;
    }
    if (itemId == "remove") {
        RequestRemove(targetId);
        return;
    }
    if (itemId != "duplicate") return;
    const std::optional<rs::world::Guid> guid = ParseCardId(targetId);
    if (!guid.has_value()) return;
    FormationAction action(*refs_);
    const std::optional<std::size_t> defIndex = action.FindFormationIndex(*guid);
    if (defIndex.has_value()) action.DuplicateFormation(*defIndex);
}

void RmlUiFormationListView::RequestRemove(const std::string& cardId) {
    if (!refs_ || confirmDialog_ == nullptr) return;
    const std::optional<rs::world::Guid> guid = ParseCardId(cardId);
    if (!guid.has_value()) return;
    FormationAction action(*refs_);
    const std::optional<std::size_t> defIndex = action.FindFormationIndex(*guid);
    if (!defIndex.has_value()) return;

    std::string message = "「" + action.ViewModel().defs[*defIndex].name + "」を削除しますか?";
    const std::size_t placedCount = action.PlacedTrainCount(*guid);
    if (placedCount > 0) {
        message += "\n配置済みの列車(" + std::to_string(placedCount) + "編成)のタグが空になります。";
    }
    // 確認の間に編成の並びが変わっても取り違えないよう、guidで覚えて確定時に添字を引き直す。
    const rs::world::Guid target = *guid;
    confirmDialog_->Open(message, "削除", true, [this, target]() {
        if (!refs_) return;
        FormationAction confirmedAction(*refs_);
        const std::optional<std::size_t> index = confirmedAction.FindFormationIndex(target);
        if (index.has_value()) confirmedAction.RemoveFormation(*index);
    });
}

void RmlUiFormationListView::OnBeginRename(Rml::Event& event, const std::string& cardId) {
    if (templateSave_ != nullptr && templateSave_->IsEditingFormation()) return;
    if (!refs_) return;
    const std::optional<rs::world::Guid> guid = ParseCardId(cardId);
    if (!guid.has_value()) return;
    const std::optional<std::size_t> defIndex = FormationAction(*refs_).FindFormationIndex(*guid);
    Rml::Element* const nameElement = event.GetCurrentElement();
    if (!defIndex.has_value() || nameElement == nullptr) return;

    // 入力欄はこの更新ではまだ見えない。編集中のカードを示す変数だけ今立てて、フォーカスは次のRefreshで移す。
    document_ = nameElement->GetOwnerDocument();
    editingInitialName_ = FormationAction(*refs_).ViewModel().defs[*defIndex].name;
    pendingFocus_ = true;
    model_->editingId = cardId;
    modelHandle_.DirtyVariable(kEditingIdKey);
}

void RmlUiFormationListView::FocusEditingInput() {
    if (document_ == nullptr) return;
    Rml::Element* const element = document_->QuerySelector(kEditingInputSelector);
    Rml::ElementFormControlInput* const input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
    if (input == nullptr) return; // まだ見えていない。次のRefreshで再び試す
    pendingFocus_ = false;
    input->SetValue(editingInitialName_);
    input->Focus();
    input->Select();
}

void RmlUiFormationListView::OnCommitName(Rml::Event& event) {
    if (templateSave_ != nullptr && templateSave_->IsEditingFormation()) {
        templateSave_->OnEdit(event);
        return;
    }
    if (!refs_ || model_->editingId.empty()) return;
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(event.GetCurrentElement());
    if (control == nullptr) return;
    // 入力途中(changeでlinebreakなし)は何もしない。Enter(linebreak)とblurで確定する。
    const bool isBlur = event.GetId() == Rml::EventId::Blur;
    if (!isBlur && !event.GetParameter<bool>("linebreak", false)) return;

    // blurはクリックによる選択の移動より先に届くので、編集を始めたカードのguidから添字を引く。
    const std::string name = control->GetValue();
    const std::optional<rs::world::Guid> guid = ParseCardId(model_->editingId);
    if (guid.has_value() && !name.empty()) {
        FormationAction action(*refs_);
        const std::optional<std::size_t> defIndex = action.FindFormationIndex(*guid);
        // 空文字は元の名前のままにする。
        if (defIndex.has_value()) action.RenameFormation(*defIndex, name);
    }
    EndEditing();
}

void RmlUiFormationListView::OnKeyName(Rml::Event& event) {
    if (templateSave_ != nullptr && templateSave_->IsEditingFormation()) {
        templateSave_->OnEdit(event);
        return;
    }
    if (event.GetParameter<int>("key_identifier", 0) != static_cast<int>(Rml::Input::KI_ESCAPE)) return;
    EndEditing();
    event.StopPropagation();
}

void RmlUiFormationListView::EndEditing() {
    pendingFocus_ = false;
    model_->editingId.clear();
    modelHandle_.DirtyVariable(kEditingIdKey);
}

void RmlUiFormationListView::Refresh(bool sectionVisible, bool panelVisible, RmlUiFormationImages& images) {
    if (!refs_ || !modelHandle_) return;
    UpdateVariable(modelHandle_, kSectionVisibleKey, model_->sectionVisible, sectionVisible);
    UpdateVariable(modelHandle_, kPanelVisibleKey, model_->panelVisible, panelVisible);
    // 名前の入力欄は左タブの中にあるので、タブが見えない間は編集をやめる。
    if (!sectionVisible) {
        pendingFocus_ = false;
        UpdateVariable(modelHandle_, kEditingIdKey, model_->editingId, Rml::String());
    }
    if (!panelVisible) return;

    const FormationViewModel viewModel = FormationAction(*refs_).ViewModel();
    // 編集中のカードが消えたら(削除など)、編集をやめる。
    const std::optional<rs::world::Guid> editingGuid = ParseCardId(model_->editingId);
    if (!model_->editingId.empty() &&
        (!editingGuid.has_value() || !FormationAction(*refs_).FindFormationIndex(*editingGuid).has_value())) {
        EndEditing();
    }
    if (pendingFocus_) FocusEditingInput();
    const Rml::Vector<FormationCardModel> cards = ToCardModels(viewModel, images, cars_.FocusedCarIndex());
    UpdateVariable(modelHandle_, kCardsKey, model_->cards, cards);
    UpdateVariable(modelHandle_, kPlacementReversedHeadKey, model_->placementReversedHead,
                   viewModel.placementReversedHead);
}

} // namespace rs::app::rmlui
