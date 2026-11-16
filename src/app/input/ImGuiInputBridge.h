//
// ImGuiInputBridge.h - ImGuiの入力状態をInputSourceへ流すデバイス境界
//
// 入力の読み取りでImGuiに依存してよいのはこのファイルだけ。UIフレームワークを
// 差し替えるときは、この生産側の実装だけを置き換える。
//
#pragma once

namespace rs::app::input {

class InputSource;

// 毎フレーム、ImGuiのNewFrame後に1回だけ呼ぶ。
void PumpImGuiInput(InputSource& source, double dtSeconds);

} // namespace rs::app::input
