//
// InputTypes.h - デバイス非依存の入力の基本型
//
// Action ID・入力コンテキスト・キー・ポインタボタンの列挙。ImGui/RmlUi/Win32の
// いずれにも依存しない。
//
#pragma once

namespace rs::app::input {

enum class PointerButton { Left, Right, Middle, Count };
enum class InputContext { Ui, Edit, Drive, View, Photo };
enum class InputAction {
    EditorUndo, EditorRedo, EditorDelete, EditorCancel,
    CameraMoveForward, CameraMoveBack, CameraMoveLeft, CameraMoveRight,
    ViewScenePrev, ViewSceneNext,
    // 敷設・施設配置の高さオフセットを1歩ずつ上下(PlacementHeightAction)。
    EditorHeightUp, EditorHeightDown,
    // 情景配置プレビュー中の向き(yaw)を左右矢印で回す(SceneryYawAction)。
    SceneryYawLeft, SceneryYawRight,
    EditorSnapDisable,  // 押している間、敷設のスナップ(ノード・並走・edge上)を解除する
    // 運転台視点(CabinPos)のWASD+上下矢印移動。
    // CameraMoveForward等とは別系統(イージング無し)のため専用Actionを設ける。
    CabinMoveForward, CabinMoveBack, CabinMoveLeft, CabinMoveRight, CabinMoveUp, CabinMoveDown,
    // 固定ワークスペースのEsc相当。WorkspaceShell::Backへ渡す。
    // 生キー(VK_ESCAPE)を直接読まずAction経由にする。
    WorkspaceBack,
    SelectModeRail, SelectModeTrain, SelectModeSignal, SelectModeDia, SelectModeScenery,
    // 3Dだけ(UIなし)のスクリーンショット保存(F12)。
    TakeScreenshot,
    // UIをすべて隠す・戻す(F11)。
    ToggleUi,
    CameraRollLeft, CameraRollRight, CameraRollReset,
};
enum class InputKey { W, A, S, D, Z, Y, R, Delete, Escape, UpArrow, DownArrow, Space, F12, LeftArrow, RightArrow,
                      F1, F2, F3, F4, F5, Q, E, F11, Count,
                      // 未割当(割当表と保存テキストの上だけにある値)。Count未満の配列の添字には使わない。
                      None };

struct PointerPos { float x = 0.0f; float y = 0.0f; };
struct KeyModifiers { bool ctrl = false; bool shift = false; bool alt = false; };

} // namespace rs::app::input
