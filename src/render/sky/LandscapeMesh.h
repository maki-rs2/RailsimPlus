//
// LandscapeMesh.h - 標準の空の組み込み遠景のメッシュ(視点中心の帯。CPUの純関数)
//
// 頂点は位置(原点からの方向そのもの、単位長)+UV。
// 方向 = (sin(az)cos(el), sin(el), cos(az)cos(el))。UVのu = az/360度、v = (上端の仰角 - el)/帯の高さ。
// 帯の下端より下は、最下行の色で真下まで続く縁(vは常に1)。真下は1頂点(el=-90度)で閉じる。
// DX12型を持たない。GPUへの積み込みは LandscapeCube が行う。
//
#pragma once

#include <cstdint>
#include <vector>

namespace rs::render {

struct LandscapeVertex {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float uv[2] = {0.0f, 0.0f};
};

struct LandscapeMeshData {
    std::vector<LandscapeVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// 画像の約束(assets/sky/Landscape.png): 縦は仰角+35度(上端)〜-10度(下端)。
inline constexpr float kLandscapeTopElevationDeg = 35.0f;
inline constexpr float kLandscapeBottomElevationDeg = -10.0f;
// 方位の分割数。画像の幅4096px÷256=16px。弦で近似する誤差は方位角で十分小さい。
inline constexpr int kLandscapeAzimuthSegments = 256;
// 帯の仰角方向の分割数(45度÷32≒1.4度)。
inline constexpr int kLandscapeBandSegments = 32;
// 縁(下端から真下まで)の仰角方向の分割数。vは一定なので見た目に影響せず、少なくてよい。
inline constexpr int kLandscapeSkirtSegments = 4;

// 帯+縁のメッシュ。縫い目(az=0と360度)の頂点は重ねて持ち、uを0と1にする。
LandscapeMeshData BuildLandscapeBandMesh(int azimuthSegments = kLandscapeAzimuthSegments,
                                         int bandSegments = kLandscapeBandSegments,
                                         int skirtSegments = kLandscapeSkirtSegments);

// キューブの1面を90度FOVで描くときのカメラの向き。方向 d の面内の位置は
// (dot(d,right), dot(d,up)) / dot(d,forward)(右・上が+)。スライス 0〜5 = +X,-X,+Y,-Y,+Z,-Z で、
// AtmosphereMath の CubeFaceDirection / IblShader.h の CubeFaceDirection と同じ向き(変えるときは3つとも)。
struct LandscapeFaceBasis {
    float right[3];
    float up[3];
    float forward[3];
};

// face は 0〜5(範囲外は5に丸める)。
const LandscapeFaceBasis& LandscapeCubeFaceBasis(int face);

} // namespace rs::render
