//
// TonemapCheck.h - --tonemap-check: トーンマップのHLSLとCPU基準実装(TonemapMath)の一致を確かめる
//
// ウィンドウなしでD3D12デバイスを作り、小さなHDR入力(灰色と色付き)を全モード・2通りの露出で
// 実際の TonemapPass に通して読み戻し、TonemapColor が計算する期待値(sRGB 8ビット)と比べる。
// 片方の式だけを変えたときに気づくための検証で、CPU側の式がテストと背景色の逆算の基準になる。
// 全件が許容誤差に収まれば0、ずれた組があれば0以外を返す。
//
#pragma once

namespace rs::app {

int RunTonemapCheck();

} // namespace rs::app
