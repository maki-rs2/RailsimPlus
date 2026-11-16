//
// ThumbnailLighting.h - サムネイルの太陽の向き(直射光と、環境光(IBL)を作るときの太陽で共通)
//
// 撮影のカメラは必ず +Z 側から -Z 向きに見る(方位0)。見たい向きは、モデルのほうをY軸まわりに回して合わせる
// (app層の PluginThumbnailRecipe)。こうすると太陽はカメラ基準で全変種に同じ向きになり、
// 環境光は1組で足りる。太陽の向きは、カメラ側(手前)・カメラから見て右・上の成分の合成で、
// 見えている面が影側にならない。
//
#pragma once

#include <cmath>

#include "../../core/Math.h"

namespace rs::render {

inline constexpr float kThumbnailSunTowardCameraWeight = 0.5f;
inline constexpr float kThumbnailSunTowardRightWeight = 0.35f;
inline constexpr float kThumbnailSunTowardUpWeight = 0.8f;

// 太陽に向かう向き(単位ベクトル、Y-up)。方位0のカメラは +Z 側にあり、カメラから見て「右」の成分は -X 側へ向ける。
inline rs::core::Vec3 ThumbnailSunDirectionToSun() {
    const float x = -kThumbnailSunTowardRightWeight;
    const float y = kThumbnailSunTowardUpWeight;
    const float z = kThumbnailSunTowardCameraWeight;
    const float length = std::sqrt(x * x + y * y + z * z);
    return rs::core::Vec3(x / length, y / length, z / length);
}

} // namespace rs::render
