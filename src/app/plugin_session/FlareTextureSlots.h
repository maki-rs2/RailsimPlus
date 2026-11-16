//
// FlareTextureSlots.h - レンズフレアの Texture 要素の画像に、LensFlarePass のスロット番号を割り当てる台帳
//
// 太陽(Env)も灯具(プラグイン)も、画像ファイル(絶対パス、UTF-8)をキーに同じ台帳を通す。
// 同じファイルは同じ番号を返す。一度割り当てたスロットは解放しない(Env や Scene が変わっても、
// 新しい画像は新しいスロットへ。使い切ったら以降の画像は割り当てない)。
// GPU・画像の読み込みには触らない。割り当てた画像の読み込みとアップロードは UploadPendingFlareTextures。
//
#pragma once

#include "../../render/effects/LensFlarePass.h"
#include "TextureSlotRegistry.h"

namespace rs::app {

class FlareTextureSlots : public TextureSlotRegistry {
public:
    FlareTextureSlots();
};

}  // namespace rs::app
