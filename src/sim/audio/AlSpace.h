// AlSpace.h - ワールド座標からOpenAL座標への変換。
#pragma once

namespace rs::sim {

// ワールドは左手座標系(PerspectiveLH)だがOpenALは右手座標系を前提にAL_ORIENTATIONの
// forward×upから左右を導出するため、Z成分を反転してOpenAL側の座標系に変換する
// (無変換だと左右のパンニングが逆になる)。距離(減衰)には影響しない。
inline void ToAlSpace(const float world[3], float outAl[3]) {
    outAl[0] = world[0];
    outAl[1] = world[1];
    outAl[2] = -world[2];
}

} // namespace rs::sim
