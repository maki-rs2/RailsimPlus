//
// SunFlareSetup.h - 現在のEnvの定義から、太陽のレンズフレアと白飛びの設定(SunFlareSettings)を作る
//
// 定義(LensFlareDef・Whiteout)は度・宣言順・ファイル名のまま持っているので、ここで描画側の形にする:
// 度→コサイン、Distance の昇順(同じ値は宣言順)への並べ替え、Texture 要素へのスロット番号の割り当て。
// Envなし(標準の空)のときだけ、組み込みの既定値を使う。Envを使っていて LensFlare・Whiteout が
// 定義に無いときは、それぞれ出さない。
//
#pragma once

#include "../../plugins/schema/EnvSchema.h"
#include "../../render/effects/SunFlareSettings.h"
#include "LensFlareStyleConvert.h"

namespace rs::app {

struct SunFlareSetup {
    rs::render::SunFlareSettings settings;
};

// env が nullptr(Envなし)なら組み込みの既定値。Texture 要素のスロット番号は assignSlot が決める
// (Envのフォルダからの相対パスを渡す。呼び出し側が絶対パスにして台帳 FlareTextureSlots へ通す)。
SunFlareSetup BuildSunFlareSetup(const rs::legacy_pi::EnvDefinition* env, const TextureSlotAssigner& assignSlot);

} // namespace rs::app
