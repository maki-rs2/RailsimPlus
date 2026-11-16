#include "ImGuiInputBridge.h"

#include <iterator>

#include "imgui.h"

#include "InputSource.h"

namespace rs::app::input {
namespace {

// InputKeyの並びと1対1で対応させる(添字で引くため順序を崩さないこと)。
constexpr ImGuiKey kImGuiKeys[] = {ImGuiKey_W,      ImGuiKey_A,       ImGuiKey_S,
                                   ImGuiKey_D,      ImGuiKey_Z,       ImGuiKey_Y,
                                   ImGuiKey_R,
                                   ImGuiKey_Delete, ImGuiKey_Escape,  ImGuiKey_UpArrow,
                                   ImGuiKey_DownArrow, ImGuiKey_Space, ImGuiKey_F12,
                                   ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
                                   ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5,
                                   ImGuiKey_Q, ImGuiKey_E, ImGuiKey_F11};
static_assert(std::size(kImGuiKeys) == static_cast<size_t>(InputKey::Count),
              "kImGuiKeysはInputKeyの全要素と対応していること");

constexpr ImGuiMouseButton kImGuiButtons[] = {ImGuiMouseButton_Left, ImGuiMouseButton_Right,
                                              ImGuiMouseButton_Middle};
static_assert(std::size(kImGuiButtons) == static_cast<size_t>(PointerButton::Count),
              "kImGuiButtonsはPointerButtonの全要素と対応していること");

} // namespace

void PumpImGuiInput(InputSource& source, double dtSeconds) {
    source.BeginFrame(dtSeconds);
    const ImGuiIO& io = ImGui::GetIO();
    source.PushPointerMove(io.MousePos.x, io.MousePos.y);
    // クリック・ダブルクリック・ドラッグのエッジ判定はInputSource側が持つ。ここは
    // 押下状態をそのまま流すだけにする(ImGui側のタイマ・閾値へ依存させないため)。
    for (size_t index = 0; index < static_cast<size_t>(PointerButton::Count); ++index) {
        source.PushPointerButton(static_cast<PointerButton>(index),
                                 ImGui::IsMouseDown(kImGuiButtons[index]));
    }
    for (size_t index = 0; index < static_cast<size_t>(InputKey::Count); ++index) {
        source.PushKey(static_cast<InputKey>(index), ImGui::IsKeyDown(kImGuiKeys[index]));
    }
    source.PushWheel(io.MouseWheel);
    source.SetModifiers(KeyModifiers{io.KeyCtrl, io.KeyShift, io.KeyAlt});
    source.SetUiCapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
}

} // namespace rs::app::input
