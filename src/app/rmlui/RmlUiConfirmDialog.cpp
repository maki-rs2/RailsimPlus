#include "RmlUiConfirmDialog.h"

#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

namespace rs::app::rmlui {

namespace {

constexpr const char* kRootId = "confirm-dialog";
constexpr const char* kMessageId = "confirm-dialog-message";
constexpr const char* kCancelId = "confirm-dialog-cancel";
constexpr const char* kSecondaryId = "confirm-dialog-secondary";
constexpr const char* kConfirmId = "confirm-dialog-ok";

// 利用者の入力(編成名など)を含むので、RMLとして解釈させずに文字列のまま入れる。
void SetPlainText(Rml::ElementDocument& document, Rml::Element& element, const std::string& text) {
    while (element.GetFirstChild() != nullptr) element.RemoveChild(element.GetFirstChild());
    element.AppendChild(document.CreateTextNode(text));
}

// 文言が空のボタンは出さない。
void SetButton(Rml::ElementDocument& document, Rml::Element& button, const std::string& label) {
    SetPlainText(document, button, label);
    button.SetClass("is-hidden", label.empty());
}

} // namespace

RmlUiConfirmDialog::RmlUiConfirmDialog(Rml::ElementDocument& document) : document_(document) {
    root_ = document.GetElementById(kRootId);
    message_ = document.GetElementById(kMessageId);
    cancelButton_ = document.GetElementById(kCancelId);
    secondaryButton_ = document.GetElementById(kSecondaryId);
    confirmButton_ = document.GetElementById(kConfirmId);
    if (message_ == nullptr || cancelButton_ == nullptr || secondaryButton_ == nullptr || confirmButton_ == nullptr) {
        root_ = nullptr;
    }
    if (root_ == nullptr) return;
    root_->AddEventListener("click", this);
}

RmlUiConfirmDialog::~RmlUiConfirmDialog() {
    if (root_ != nullptr) root_->RemoveEventListener("click", this);
}

void RmlUiConfirmDialog::Open(const std::string& message, const std::string& confirmLabel, bool danger,
                              ConfirmCallback onConfirm) {
    ConfirmDialogContent content;
    content.message = message;
    content.confirmLabel = confirmLabel;
    content.danger = danger;
    content.onConfirm = std::move(onConfirm);
    Open(std::move(content));
}

void RmlUiConfirmDialog::Open(ConfirmDialogContent content) {
    if (root_ == nullptr) return;
    // 入力欄にフォーカスが残っていると、ダイアログの間も打った文字がそこへ入ってしまう。
    if (Rml::Context* const context = document_.GetContext()) {
        if (Rml::Element* const focus = context->GetFocusElement()) focus->Blur();
    }
    SetPlainText(document_, *message_, content.message);
    SetButton(document_, *cancelButton_, content.cancelLabel);
    SetButton(document_, *secondaryButton_, content.secondaryLabel);
    SetButton(document_, *confirmButton_, content.confirmLabel);
    confirmButton_->SetClass("danger", content.danger);
    confirmButton_->SetClass("primary", !content.danger);
    content_ = std::move(content);
    open_ = true;
    root_->SetClass("open", true);
}

void RmlUiConfirmDialog::Close() {
    if (!open_) return;
    Finish(&ConfirmDialogContent::onCancel);
}

bool RmlUiConfirmDialog::HandleKeyDown(ContextMenuKey key, bool isRepeat) {
    if (!open_) return false;
    if (isRepeat) return true;
    if (key == ContextMenuKey::Enter) Finish(&ConfirmDialogContent::onConfirm);
    else if (key == ContextMenuKey::Escape) Close();
    return true;
}

void RmlUiConfirmDialog::ProcessEvent(Rml::Event& event) {
    if (!open_) return;
    Rml::Element* const target = event.GetTargetElement();
    if (IsInside(target, confirmButton_)) Finish(&ConfirmDialogContent::onConfirm);
    else if (IsInside(target, secondaryButton_)) Finish(&ConfirmDialogContent::onSecondary);
    else if (IsInside(target, cancelButton_)) Close();
}

void RmlUiConfirmDialog::Finish(ConfirmCallback ConfirmDialogContent::*choice) {
    const ConfirmCallback callback = std::move(content_.*choice);
    content_ = ConfirmDialogContent{};
    open_ = false;
    root_->SetClass("open", false);
    if (callback) callback();
}

bool RmlUiConfirmDialog::IsInside(Rml::Element* target, const Rml::Element* element) const {
    for (; target != nullptr && target != root_; target = target->GetParentNode()) {
        if (target == element) return true;
    }
    return false;
}

} // namespace rs::app::rmlui
