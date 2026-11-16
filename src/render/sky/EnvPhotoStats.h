//
// EnvPhotoStats.h - Envの写真の鏡面キューブから、明るさの正規化と太陽の方位合わせに要る統計を求める(純関数)
//
// 標準の空の鏡面IBLにEnvの写真キューブを使うとき、写真のHDR値はそのまま使わず「今の標準の空の明るさ」へ
// 毎フレーム合わせる。そのために、写真の上半球の平均色(skyMean)と、太陽の方向を読み込み時に1回だけ求める。
// DX12型を持たない。ワーカースレッドから呼べる(共有状態なし)。
//
#pragma once

#include "../resource/EnvTextureLoader.h"

namespace rs::render {

// 上半球の最大輝度が、上半球の平均輝度のこの倍数以上のときだけ「太陽がある」とみなす。
inline constexpr double kPhotoSunMinRatio = 3.0;
// 太陽がある写真で、skyMean から除く太陽の周りの角度(度)。
inline constexpr double kPhotoSunExcludeDeg = 5.0;

struct EnvPhotoStats {
    bool ok = false;               // 入力がキューブで、skyMean が正の有限値
    bool sunFound = false;
    double sunDirection[3] = {0.0, 1.0, 0.0};  // 単位ベクトル(Y-up)。sunFound のときだけ意味がある
    double skyMean[3] = {0.0, 0.0, 0.0};       // リニアRGB。上半球のコサイン重み付き平均(太陽の周りを除く)
};

// 鏡面キューブのミップ0の6面から求める。対応形式は EnvTexelFormat の3種(Rgba8Srgbはリニアへ変換して読む)。
// 面の向きは AtmosphereMath の CubeFaceDirection と同じ(D3Dの並び。メッシュのシェーダが引く向きと同じ)。
EnvPhotoStats ComputeEnvPhotoStats(const EnvTextureData& specularCube);

} // namespace rs::render
