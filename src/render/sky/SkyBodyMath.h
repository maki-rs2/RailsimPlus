//
// SkyBodyMath.h - プラグインの空の Sun.x・Moon.x の配置行列(純関数、DX12に依存しない)
//
// RailSim II(CEnvPlugin::Render / CMoon::Render)は、天体のメッシュを「カメラ位置 + 10 × 方向」に置き、オブジェクトの前方(+Z)を
// 太陽は太陽の方向、月は太陽と反対(-sdir)へ向ける。大きさは ModelScale。ここでは、カメラ位置を原点にした相対の配置で
// 「ローカル→クリップ」の行列を作る(カメラの平行移動を除いたビュー行列を使うので、天体は視点が動いても同じ方向に見える)。
//
#pragma once

#include "../../core/Math.h"

namespace rs::render {

// RailSim II と同じ、カメラから天体までの距離(ワールド単位)。
constexpr float kSkyBodyDistance = 10.0f;

// 天体オブジェクトの向き(左手系、row-vector。x = up × z、y = z × x)。forward は+Zにする向き(単位でなくてよい)、
// upHint は+Yにしたい向きの目安(forward と平行なら別の軸で代用する)。行は x・y・z の各軸(正規直交、回転のみ)。
rs::core::Mat4 MakeSkyBodyRotation(const rs::core::Vec3& forward, const rs::core::Vec3& upHint);

// ローカル座標の頂点を、カメラを原点とした位置へ置いてクリップ空間へ送る行列。
// 回転・scale・directionToBody×kSkyBodyDistance の平行移動 → view の回転部分 → proj の順。
// directionToBody はカメラから見た天体の方向(単位でなくてよい。長さ0なら原点に置く)。
rs::core::Mat4 MakeSkyBodyMvp(const rs::core::Mat4& view, const rs::core::Mat4& proj,
                              const rs::core::Vec3& directionToBody, const rs::core::Mat4& rotation,
                              float scale);

} // namespace rs::render
