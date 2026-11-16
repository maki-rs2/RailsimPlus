//
// LensFlareStyleConvert.h - PI定義の LensFlare{} を、描画側の FlareStyle へ変換する
//
// 太陽(Env の Sun.LensFlare)と灯具(Headlight.LensFlare)が共用する。
// 度→コサイン、Distance の昇順(同じ値は宣言順)への並べ替え、Texture 要素へのスロット番号の割り当て。
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "../../plugins/schema/LensFlareSchema.h"
#include "../../render/effects/LensFlareMath.h"

namespace rs::app {

// Texture 要素のファイル名(定義のまま)から、スロット番号を返す。割り当てられないときは
// kMaxFlareTextures 以上の値を返す(その要素は描かれない)。
using TextureSlotAssigner = std::function<std::uint32_t(const std::string& texFileName)>;

rs::render::FlareStyle ConvertLensFlare(const rs::legacy_pi::LensFlareDef& def, const TextureSlotAssigner& assignSlot);

float CosOfDegrees(double degrees);
rs::render::FlareColor ToFlareColor(const rs::legacy_pi::PiColor& color);

}  // namespace rs::app
