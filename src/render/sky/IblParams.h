//
// IblParams.h - IBL(画像ベースの環境光)の資源の大きさとサンプル数
//
// 調整値ではない(JSONには出さない)。シェーダ(IblShader.h)の定数とCPU側の資源生成の両方が
// この値を使う。形式は環境ソース・拡散・鏡面がR16G16B16A16_FLOAT、BRDF LUTがR16G16_FLOAT。
//
#pragma once

#include <cstdint>

namespace rs::render::ibl {

// キューブの面の数(配列スライス 0〜5 = +X,-X,+Y,-Y,+Z,-Z。D3D標準の並び)。
constexpr std::uint32_t kIblCubeFaceCount = 6;

// 環境ソース(空+地面の放射輝度)の1辺(ミップ0)。ミップは、焼き込みのあとに2x2平均で作る
// (畳み込みが、サンプルごとにミップを選ぶ filtered importance sampling のため)。
constexpr std::uint32_t kIblEnvSourceSize = 256;
constexpr std::uint32_t kIblEnvSourceMipCount = 9; // 256,128,...,1
static_assert((kIblEnvSourceSize >> (kIblEnvSourceMipCount - 1)) == 1, "最後のミップが1x1になる枚数にする");
// 拡散(放射照度÷π)の1辺。滑らかなので小さくてよい。
constexpr std::uint32_t kIblDiffuseSize = 32;
// 鏡面の1辺(ミップ0)。ミップ番号 m の roughness は m/(ミップ数-1) の線形。
constexpr std::uint32_t kIblSpecularSize = 256;
constexpr std::uint32_t kIblSpecularMipCount = 9; // 256,128,...,1
static_assert((kIblSpecularSize >> (kIblSpecularMipCount - 1)) == 1, "最後のミップが1x1になる枚数にする");
// BRDF LUT(横=NdotV、縦=roughness)の1辺。
constexpr std::uint32_t kIblBrdfLutSize = 128;

// 毎回同じ値になる低食い違い列(Hammersley)のサンプル数。
constexpr std::uint32_t kIblDiffuseSampleCount = 256;
constexpr std::uint32_t kIblSpecularSampleCount = 256;
constexpr std::uint32_t kIblBrdfSampleCount = 256;
// 大気の空だけの上半球平均(写真の空の層の明るさを合わせる基準)のサンプル数。1x1の1画素で、1度の生成に1回だけ描く。
constexpr std::uint32_t kIblSkyMeanSampleCount = 1024;

// 写真の空の層(環境ソースの焼き込み)。写真の太陽の周りを写真の平均色へ置き換える半径[度]。
// EnvPhotoStats が平均から太陽の周りを除く半径(kPhotoSunExcludeDeg相当)と同じ値にする。
constexpr float kIblPhotoSunMaskDeg = 5.0f;
// 写真の平均の輝度がこれより小さいときの割り算の下限(倍率が発散しないように)。
constexpr float kIblPhotoLuminanceMin = 1e-4f;

} // namespace rs::render::ibl
