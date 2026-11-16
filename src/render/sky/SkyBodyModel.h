//
// SkyBodyModel.h - プラグインの空の Sun.x・Moon.x のCPU側データ(メモリ上へ読んだ結果)
//
// .x は rs::legacy_x::LoadXFileAssimp、画像は LoadImageRGBA で読む(どちらも既存の読込。スレッドから呼んでよい)。
// 描くのに要るのは位置とUVだけ(ライティングなし)なので、法線などは捨てる。GPUへの上げ方は SkyBodyPass。
// 材質は先頭の1つだけ使う(テクスチャと色。天体のメッシュは1材質の想定)。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../resource/TextureLoader.h"

namespace rs::render {

struct SkyBodyModel {
    bool ok = false;
    std::string error;              // ok=false のときの理由(読めなかった・頂点なし・画像なし)
    std::vector<float> vertices;    // 頂点ごとに位置xyz・UVuv の5つ
    std::vector<std::uint32_t> indices;
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // 材質色。テクスチャに掛ける
    float ambient[3] = {1.0f, 1.0f, 1.0f};        // 材質のアンビエント。遠景がテクスチャに掛ける(Sun・Moonは使わない)
    ImageData image;                // R8G8B8A8(sRGBとして扱う)
    float scale = 1.0f;             // ModelScale
};

constexpr std::uint32_t kSkyBodyVertexFloats = 5;

// UTF-8パスの .x と、その先頭材質のテクスチャを読む。パスが空・読めない場合は ok=false(例外は投げない)。
SkyBodyModel LoadSkyBodyModel(const std::string& xPathUtf8, double modelScale);

// Sun・Moon の2体と、遠景(Landscape.x のドーム。同じ読込と形式を使う)。
struct SkyBodyImages {
    SkyBodyModel sun;
    SkyBodyModel moon;
    SkyBodyModel landscape;
};

} // namespace rs::render
