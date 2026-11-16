//
// FlareTextureUpload.h - 台帳に新しく割り当てたスロットの画像を読み、LensFlarePass へ上げる
//
// 画像は数枚の小さなものなので、読み込みは呼び出したスレッドで行う(描画スレッドの外から呼ばないこと)。
// 新しいスロットは、まだ描画に使われていないので、GPUの完了は待たない。
//
#pragma once

#include "../../render/effects/LensFlarePass.h"
#include "FlareTextureSlots.h"

namespace rs::app {

// pass が準備できていなければ何もしない(割り当ては次回に持ち越す)。読めない・上げられない画像は警告を出し、
// そのスロットを使う要素は描かれない。台帳が満杯になっていたら、警告を1回だけ出す。
void UploadPendingFlareTextures(FlareTextureSlots& slots, rs::render::LensFlarePass& pass);

}  // namespace rs::app
