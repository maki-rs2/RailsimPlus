#include "InputSource.h"

namespace rs::app::input {
namespace {

// ダブルクリックとみなす間隔[秒]。判定を入力層が持つのが要件であり、
// UIフレームワーク側のタイマへ委ねない。
constexpr double kDoubleClickIntervalSeconds = 0.30;

// クリック(タップ)とみなす条件: 押してから離すまでがこの時間以内で、移動量がこの距離未満。
// これを超えたものは長押し・ドラッグとして扱い、クリック操作を起こさない。
constexpr double kTapMaxSeconds = 0.25;
constexpr float kTapMaxMovePx = 5.0f;

} // namespace

InputSource::InputSource(std::vector<InputBinding> bindings) : bindings_(std::move(bindings)) {
    // 起動直後の最初のクリックがダブルクリック扱いにならないよう、十分過去を入れる。
    lastClickTime_.fill(-kDoubleClickIntervalSeconds);
}

void InputSource::BeginFrame(double dtSeconds) {
    keyPressed_.fill(false);
    buttonClicked_.fill(false);
    buttonReleased_.fill(false);
    buttonTapped_.fill(false);
    buttonDoubleClicked_.fill(false);
    wheelDelta_ = 0.0f;
    elapsedSeconds_ += dtSeconds;
}

void InputSource::PushPointerMove(float x, float y) {
    pointer_ = PointerPos{x, y};
}

void InputSource::PushPointerButton(PointerButton button, bool down) {
    const size_t index = Index(button);
    if (buttons_[index] == down) return;
    buttons_[index] = down;
    if (!down) {
        buttonReleased_[index] = true;
        const float dx = pointer_.x - dragStart_[index].x;
        const float dy = pointer_.y - dragStart_[index].y;
        buttonTapped_[index] = elapsedSeconds_ - pressTime_[index] <= kTapMaxSeconds &&
                               dx * dx + dy * dy < kTapMaxMovePx * kTapMaxMovePx;
        return;
    }
    buttonClicked_[index] = true;
    dragStart_[index] = pointer_;
    pressTime_[index] = elapsedSeconds_;
    if (elapsedSeconds_ - lastClickTime_[index] <= kDoubleClickIntervalSeconds) {
        buttonDoubleClicked_[index] = true;
    }
    lastClickTime_[index] = elapsedSeconds_;
}

void InputSource::PushWheel(float delta) {
    wheelDelta_ += delta;
}

void InputSource::PushKey(InputKey key, bool down) {
    const size_t index = Index(key);
    if (keys_[index] == down) return;
    keys_[index] = down;
    if (down) keyPressed_[index] = true;
}

void InputSource::SetModifiers(KeyModifiers modifiers) {
    modifiers_ = modifiers;
}

void InputSource::SetUiCapture(bool pointerCaptured, bool keyboardCaptured) {
    pointerCapturedByUi_ = pointerCaptured;
    keyboardCapturedByUi_ = keyboardCaptured;
}

void InputSource::SetContextActive(InputContext context, bool active) {
    activeContexts_[static_cast<std::size_t>(context)] = active;
}

bool InputSource::AllowsKeyboard(InputContext context) const {
    return ContextActive(context) &&
           (context == InputContext::Ui || !keyboardCapturedByUi_);
}

bool InputSource::AllowsPointer(InputContext context) const {
    return ContextActive(context) &&
           (context == InputContext::Ui || !pointerCapturedByUi_);
}

bool InputSource::Matches(const InputBinding& binding) const {
    // UI以外のコンテキストのキーActionは、UIがキーボードを取っている間は発火させない
    // (テキスト入力中にショートカットが誤発火するのを防ぐ)。ポインタ側の捕捉は
    // 別ゲートであり、ここでは見ない。
    if (binding.key == InputKey::None || !AllowsKeyboard(binding.context)) return false;
    return keys_[Index(binding.key)] && modifiers_.ctrl == binding.ctrl &&
           modifiers_.shift == binding.shift && modifiers_.alt == binding.alt;
}

bool InputSource::Triggered(InputAction action) const {
    for (const InputBinding& binding : bindings_) {
        if (binding.action != action || binding.held) continue;
        if (Matches(binding) && keyPressed_[Index(binding.key)]) return true;
    }
    return false;
}

bool InputSource::TriggeredInAnyContext(InputAction action) const {
    if (keyboardCapturedByUi_) return false;
    for (const InputBinding& binding : bindings_) {
        if (binding.action != action || binding.held || binding.key == InputKey::None) continue;
        if (keys_[Index(binding.key)] && keyPressed_[Index(binding.key)] && modifiers_.ctrl == binding.ctrl &&
            modifiers_.shift == binding.shift && modifiers_.alt == binding.alt) {
            return true;
        }
    }
    return false;
}

bool InputSource::Held(InputAction action) const {
    for (const InputBinding& binding : bindings_) {
        if (binding.action != action || !binding.held) continue;
        if (Matches(binding)) return true;
    }
    return false;
}

bool InputSource::ButtonClicked(PointerButton button) const {
    return buttonClicked_[Index(button)];
}

bool InputSource::ButtonReleased(PointerButton button) const {
    return buttonReleased_[Index(button)];
}

bool InputSource::ButtonTapped(PointerButton button) const {
    return buttonTapped_[Index(button)];
}

bool InputSource::ButtonDoubleClicked(PointerButton button) const {
    return buttonDoubleClicked_[Index(button)];
}

bool InputSource::ButtonDragging(PointerButton button, float thresholdPx) const {
    if (!buttons_[Index(button)]) return false;
    const PointerPos start = dragStart_[Index(button)];
    const float dx = pointer_.x - start.x;
    const float dy = pointer_.y - start.y;
    return dx * dx + dy * dy >= thresholdPx * thresholdPx;
}

} // namespace rs::app::input
