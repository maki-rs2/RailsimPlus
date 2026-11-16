//
// PostProcessFormats.h - HDR配管で使うレンダーターゲット形式
//
// DX12のPSO生成側(.cpp)が使う形式定数。公開ヘッダのシグネチャには出さない。
//
#pragma once

#include <dxgiformat.h>

namespace rs::render {

// シーンの描画先(空・不透明・LateOpaque・ハイライト・WBOITコンポジット)の形式。
// WBOITの蓄積バッファ(kWboitAccumFormat)とは別の資源・別の定数。
constexpr DXGI_FORMAT kSceneHdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

// トーンマップパスの出力形式。本編のバックバッファのシーン用RTV(sRGB)と、
// サムネイルの撮影用RTがどちらもこの形式なので、PSOを共用できる。
// シェーダはリニア値を出力し、sRGBへの変換はRTVが行う。
constexpr DXGI_FORMAT kTonemapOutputFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

} // namespace rs::render
