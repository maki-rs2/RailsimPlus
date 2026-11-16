//
// IconRectCrop.h - アイコン画像を IconRect(0〜1の比率の矩形)で切り出す
//
// プラグインヘッダの IconRect は IconTexture のうちアイコンに使う範囲を (u, v, 幅, 高さ) で示す。
// ブラウザは読み込んだ画像をこの範囲に切り出してからカードへ渡す(描画側で UV を扱わずに済む)。
// 純関数で、グローバルな状態を持たない。
//
#pragma once

#include "../../render/resource/TextureLoader.h"

namespace rs::app::plugin_browser {

// 既定(0,0,1,1)かどうか。切り出しが不要な場合の判定に使う。
bool IsFullIconRect(const float rect[4]);

// image を rect = {u, v, 幅, 高さ} の範囲に切り出して返す。範囲は画像の内側に丸める。
// 切り出し後の幅か高さが 1 画素未満になる矩形(幅 0 など)は、切り出さずに image をそのまま返す。
rs::render::ImageData CropIconRect(const rs::render::ImageData& image, const float rect[4]);

} // namespace rs::app::plugin_browser
