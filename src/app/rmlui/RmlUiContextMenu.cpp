#include "RmlUiContextMenu.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "RmlUiEnumKey.h"
#include "RmlUiIntegration.h"

namespace rs::app::rmlui {

namespace {

using workspace::ContextMenuItemDef;
using workspace::ContextMenuKind;

constexpr const char* kMenuElementId = "context-menu";
constexpr const char* kKindAttribute = "data-context-menu";
constexpr const char* kTargetAttribute = "data-context-id";

// 位置の単位はRmlUiの画面のpx(マウス座標・ビューポートと同じ)。
Rml::String PxString(float value) {
    return std::format("{:.2f}px", value);
}

} // namespace

RmlUiContextMenu::RmlUiContextMenu(Rml::ElementDocument& document) : document_(document) {
    menu_ = document.GetElementById(kMenuElementId);
    if (menu_ == nullptr) return;
    document.AddEventListener("mouseup", this, true);
    menu_->AddEventListener("click", this);
    menu_->AddEventListener("mouseover", this);
}

RmlUiContextMenu::~RmlUiContextMenu() {
    if (menu_ == nullptr) return;
    document_.RemoveEventListener("mouseup", this, true);
    menu_->RemoveEventListener("click", this);
    menu_->RemoveEventListener("mouseover", this);
}

void RmlUiContextMenu::RegisterProvider(ContextMenuKind kind, workspace::IContextMenuProvider* provider) {
    providers_[workspace::EnumIndex(kind)] = provider;
}

void RmlUiContextMenu::Close() {
    if (!open_) return;
    open_ = false;
    placed_ = false;
    highlight_ = -1;
    menu_->SetClass("open", false);
    menu_->SetClass("placed", false);
}

bool RmlUiContextMenu::CloseIfOutside(float x, float y) {
    if (!open_) return false;
    const Rml::Vector2f origin = menu_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const bool inside = x >= origin.x && x < origin.x + menu_->GetOffsetWidth() && y >= origin.y &&
                        y < origin.y + menu_->GetOffsetHeight();
    if (inside) return false;
    Close();
    return true;
}

bool RmlUiContextMenu::HandleKeyDown(ContextMenuKey key) {
    if (!open_) return false;
    switch (key) {
        case ContextMenuKey::Up: SetHighlight(workspace::MoveContextMenuHighlight(EnabledFlags(), highlight_, -1)); break;
        case ContextMenuKey::Down: SetHighlight(workspace::MoveContextMenuHighlight(EnabledFlags(), highlight_, +1)); break;
        case ContextMenuKey::Enter: ChooseItem(highlight_); break;
        case ContextMenuKey::Escape: Close(); break;
    }
    return true;
}

void RmlUiContextMenu::Sync(const Rml::Context& context) {
    if (!open_) return;
    const workspace::IContextMenuProvider* const provider = providers_[workspace::EnumIndex(kind_)];
    if (provider == nullptr || !provider->TargetExists(kind_, targetId_)) {
        Close();
        return;
    }
    RefreshEnabled();
    if (placed_) return;
    const float width = menu_->GetOffsetWidth();
    const float height = menu_->GetOffsetHeight();
    // 開いた直後のフレームはレイアウトがまだ無い。大きさが決まってから置く。
    if (width <= 0.0f || height <= 0.0f) return;
    const Rml::Vector2i viewport = context.GetDimensions();
    const workspace::MenuPoint at = workspace::PlaceContextMenu(
        cursor_, {width, height}, {static_cast<float>(viewport.x), static_cast<float>(viewport.y)});
    menu_->SetProperty("left", PxString(at.x));
    menu_->SetProperty("top", PxString(at.y));
    menu_->SetClass("placed", true);
    placed_ = true;
}

void RmlUiContextMenu::ProcessEvent(Rml::Event& event) {
    const Rml::String& type = event.GetType();
    if (type == "mouseup") OnMouseUp(event);
    else if (type == "mouseover") OnMouseOver(event);
    else if (type == "click") OnClick(event);
}

void RmlUiContextMenu::OnMouseUp(Rml::Event& event) {
    if (event.GetParameter<int>("button", -1) != kMouseButtonRight) return;
    Rml::Element* const target = event.GetTargetElement();
    // メニューの中の右クリックでは開き直さない。
    if (ItemIndexOf(target) >= 0) return;
    for (Rml::Element* element = target; element != nullptr && element != &document_; element = element->GetParentNode()) {
        if (!element->HasAttribute(kKindAttribute)) continue;
        const std::optional<ContextMenuKind> kind =
            ParseEnumKey<ContextMenuKind>(element->GetAttribute<Rml::String>(kKindAttribute, Rml::String()));
        if (!kind.has_value()) return;
        const workspace::MenuPoint cursor{event.GetParameter<float>("mouse_x", 0.0f),
                                          event.GetParameter<float>("mouse_y", 0.0f)};
        Open(*kind, element->GetAttribute<Rml::String>(kTargetAttribute, Rml::String()), cursor);
        return;
    }
}

void RmlUiContextMenu::Open(ContextMenuKind kind, const std::string& targetId, workspace::MenuPoint cursor) {
    workspace::IContextMenuProvider* const provider = providers_[workspace::EnumIndex(kind)];
    if (provider == nullptr || !provider->TargetExists(kind, targetId)) return;
    Close();

    while (menu_->GetFirstChild() != nullptr) menu_->RemoveChild(menu_->GetFirstChild());
    itemElements_.clear();
    for (const ContextMenuItemDef& def : workspace::GetContextMenuItems(kind)) {
        Rml::ElementPtr item = document_.CreateElement("div");
        item->SetClass("context-menu-item", true);
        item->SetClass("danger", def.danger);
        item->SetClass("separator-before", def.separatorBefore);
        item->AppendChild(document_.CreateTextNode(def.label));
        itemElements_.push_back(menu_->AppendChild(std::move(item)));
    }

    kind_ = kind;
    targetId_ = targetId;
    cursor_ = cursor;
    highlight_ = -1;
    placed_ = false;
    open_ = true;
    RefreshEnabled();
    menu_->SetProperty("left", "0px");
    menu_->SetProperty("top", "0px");
    menu_->SetClass("open", true);
}

void RmlUiContextMenu::OnMouseOver(Rml::Event& event) {
    const int index = ItemIndexOf(event.GetTargetElement());
    if (index < 0 || !EnabledFlags()[static_cast<std::size_t>(index)]) return;
    SetHighlight(index);
}

void RmlUiContextMenu::OnClick(Rml::Event& event) {
    ChooseItem(ItemIndexOf(event.GetTargetElement()));
}

void RmlUiContextMenu::ChooseItem(int index) {
    if (!open_ || index < 0 || index >= static_cast<int>(itemElements_.size())) return;
    if (!EnabledFlags()[static_cast<std::size_t>(index)]) return;
    const ContextMenuKind kind = kind_;
    const std::string targetId = targetId_;
    const std::string_view itemId = workspace::GetContextMenuItems(kind)[static_cast<std::size_t>(index)].id;
    workspace::IContextMenuProvider* const provider = providers_[workspace::EnumIndex(kind)];
    // 提供者の処理で対象が消えたりメニューが作り直されたりしても影響しないよう、先に閉じてから渡す。
    Close();
    if (provider != nullptr) provider->OnItemChosen(kind, itemId, targetId);
}

int RmlUiContextMenu::ItemIndexOf(Rml::Element* element) const {
    for (; element != nullptr && element != menu_; element = element->GetParentNode()) {
        for (std::size_t i = 0; i < itemElements_.size(); ++i) {
            if (itemElements_[i] == element) return static_cast<int>(i);
        }
    }
    return -1;
}

void RmlUiContextMenu::SetHighlight(int index) {
    highlight_ = index;
    for (std::size_t i = 0; i < itemElements_.size(); ++i) {
        itemElements_[i]->SetClass("highlighted", static_cast<int>(i) == highlight_);
    }
}

std::vector<bool> RmlUiContextMenu::EnabledFlags() const {
    std::vector<bool> flags;
    const workspace::IContextMenuProvider* const provider = providers_[workspace::EnumIndex(kind_)];
    for (const ContextMenuItemDef& def : workspace::GetContextMenuItems(kind_)) {
        flags.push_back(provider != nullptr && provider->IsItemEnabled(kind_, def.id, targetId_));
    }
    return flags;
}

void RmlUiContextMenu::RefreshEnabled() {
    const std::vector<bool> flags = EnabledFlags();
    for (std::size_t i = 0; i < itemElements_.size() && i < flags.size(); ++i) {
        itemElements_[i]->SetClass("disabled", !flags[i]);
    }
}

} // namespace rs::app::rmlui
