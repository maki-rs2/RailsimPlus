//
// InputSource.h - 入力イベントをまとめたフレーム単位のスナップショット
//
// 生産側(デバイス境界)はイベントを投入し、消費側はポーリングで問い合わせる。
// この形にすることで、イベント投入＋消費戻り値というRmlUiの入力モデルでも
// 対応できる。
// クリック・ダブルクリック・ドラッグのジェスチャ判定はこのクラスが持つ
// (UIフレームワーク側のタイマ・閾値へ依存させないため)。
//
#pragma once

#include <array>
#include <utility>
#include <vector>
#include <magic_enum/magic_enum.hpp>

#include "InputBindings.h"

namespace rs::app::input {

class InputSource {
public:
    explicit InputSource(std::vector<InputBinding> bindings = DefaultInputBindings());
    // 割当表を差し替える。押下中のキーやクリックの状態はそのまま残す。
    void SetBindings(std::vector<InputBinding> bindings) { bindings_ = std::move(bindings); }
    const std::vector<InputBinding>& Bindings() const { return bindings_; }
    void BeginFrame(double dtSeconds);
    void PushPointerMove(float x, float y);
    void PushPointerButton(PointerButton button, bool down);
    void PushWheel(float delta);
    void PushKey(InputKey key, bool down);
    void SetModifiers(KeyModifiers modifiers);
    void SetUiCapture(bool pointerCaptured, bool keyboardCaptured);
    void SetContextActive(InputContext context, bool active);
    bool ContextActive(InputContext context) const { return activeContexts_[static_cast<std::size_t>(context)]; }
    bool AllowsKeyboard(InputContext context) const;
    bool AllowsPointer(InputContext context) const;
    PointerButton ButtonFor(PointerAction action) const { return *GetPointerBinding(action).button; }
    bool MatchesPointer(PointerAction action) const { return MatchesPointerBinding(action, modifiers_); }

    bool Triggered(InputAction action) const;
    // Triggeredと同じ押下エッジの判定を、入力コンテキストが有効かを見ずに行う。割当のキーと修飾だけで決まる。
    // 編集コンテキストの外(鑑賞・シーン画面など)でも、編集系のキー(Undo)に応じたいときだけ使う。
    bool TriggeredInAnyContext(InputAction action) const;
    bool Held(InputAction action) const;
    PointerPos Pointer() const { return pointer_; }
    KeyModifiers Modifiers() const { return modifiers_; }
    bool PointerCapturedByUi() const { return pointerCapturedByUi_; }
    bool KeyboardCapturedByUi() const { return keyboardCapturedByUi_; }
    bool ButtonClicked(PointerButton button) const;
    bool ButtonReleased(PointerButton button) const;
    // 短く押してほとんど動かさずに離した(クリックとみなせる)フレームだけ真。
    // 長押しやドラッグ(視点操作など)の終わりでは偽になる。
    bool ButtonTapped(PointerButton button) const;
    bool ButtonDoubleClicked(PointerButton button) const;
    bool ButtonDragging(PointerButton button, float thresholdPx) const;
    float WheelDelta() const { return wheelDelta_; }
    // 入力の更新に渡した実時間の累計[秒](ゲーム内時刻ではない)。点滅など見た目の周期に使う。
    double ElapsedSeconds() const { return elapsedSeconds_; }

private:
    static constexpr size_t Index(PointerButton button) { return static_cast<size_t>(button); }
    static constexpr size_t Index(InputKey key) { return static_cast<size_t>(key); }
    bool Matches(const InputBinding& binding) const;

    std::vector<InputBinding> bindings_;
    std::array<bool, magic_enum::enum_count<InputContext>()> activeContexts_ = [] {
        std::array<bool, magic_enum::enum_count<InputContext>()> contexts{};
        contexts.fill(true);
        return contexts;
    }();
    std::array<bool, static_cast<size_t>(InputKey::Count)> keys_{};
    std::array<bool, static_cast<size_t>(InputKey::Count)> keyPressed_{};
    std::array<bool, static_cast<size_t>(PointerButton::Count)> buttons_{};
    std::array<bool, static_cast<size_t>(PointerButton::Count)> buttonClicked_{};
    std::array<bool, static_cast<size_t>(PointerButton::Count)> buttonReleased_{};
    std::array<bool, static_cast<size_t>(PointerButton::Count)> buttonTapped_{};
    std::array<double, static_cast<size_t>(PointerButton::Count)> pressTime_{};
    std::array<bool, static_cast<size_t>(PointerButton::Count)> buttonDoubleClicked_{};
    std::array<PointerPos, static_cast<size_t>(PointerButton::Count)> dragStart_{};
    std::array<double, static_cast<size_t>(PointerButton::Count)> lastClickTime_{};
    PointerPos pointer_{};
    KeyModifiers modifiers_{};
    double elapsedSeconds_ = 1.0;
    float wheelDelta_ = 0.0f;
    bool pointerCapturedByUi_ = false;
    bool keyboardCapturedByUi_ = false;
};

} // namespace rs::app::input
