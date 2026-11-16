//
// CascadeFit.h - 太陽光のシャドウマップの投影を、カメラの視錐台に合わせて決める純関数
//
// 視錐台のうち、近クリップからシャドウ距離までの部分を包む球に、ライト空間の正射影を合わせる。
// 球なので、カメラが回っても射影の大きさは変わらず、影の縁がちらつかない。ライト空間のXYは
// テクセル幅の整数倍にスナップするので、カメラが動いても影が泳がない。
// 描画(GPU)には依存しない。計算は倍精度で行い、行列を作る最後に単精度へ落とす。
//
// 行列は他の描画コードと同じ row-vector 慣習(v' = v * M)・左手系。深度は通常方向(近=0、遠=1)で、
// 主深度バッファの Reversed-Z とは独立。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../core/Math.h"

namespace rs::render {

// 同時に持てるカスケードの最大数。
constexpr std::size_t kMaxShadowCascades = 4;
// 対数分割の起点の下限[m]。
constexpr float kSplitNearFloorM = 1.0f;

struct CascadeFitInput {
    // カメラのビュー行列(剛体変換)。
    rs::core::Mat4 view = rs::core::Mat4::Identity();
    float fovYRadians = 1.0f;
    float aspect = 1.0f;
    float nearDistance = 0.02f;
    // カメラの遠クリップまでの距離。
    float farDistance = 1000.0f;
    // この距離で視錐台の奥を打ち切る(カメラの遠クリップより遠ければ遠クリップが優先)。
    float shadowDistance = 100.0f;
    // 光が進む向き(正規化していなくてよい)。
    rs::core::Vec3 lightDirection = rs::core::Vec3(0.0f, -1.0f, 0.0f);
    // シャドウマップ1枚の一辺[テクセル]。
    std::uint32_t shadowMapSize = 2048;
    // 視錐台の外のライト側にある影を落とすものが届くよう、ライト側へ延ばす距離[m]。
    float casterExtensionM = 300.0f;
    // カスケードの数(1..kMaxShadowCascades)。
    std::uint32_t cascadeCount = 1;
    // 分割の混合係数。0=等間隔、1=対数(近くを細かく切る)。
    float splitLambda = 0.75f;
    // 隣のカスケードとブレンドする範囲の幅(自分の受け持つ距離に対する割合)。0ならブレンドしない。
    // 奥側のカスケードは、この範囲の分だけ手前から覆うので、ブレンドの相手のマップに必ず値がある。
    float blendFraction = 0.0f;
};

struct CascadeFitCascade {
    // ワールド座標からライトのクリップ座標(XY=-1..1、Z=0..1)への行列。
    rs::core::Mat4 viewProj = rs::core::Mat4::Identity();
    // 視錐台を包む球の半径[m]。
    float radius = 0.0f;
    // シャドウマップ1テクセルのワールドでの幅[m]。
    float texelWorldSize = 0.0f;
    std::int64_t snapX = 0;
    std::int64_t snapY = 0;
    // このカスケードが受け持つ、カメラ前方向の奥側の距離[m]。
    float farSplit = 0.0f;
};

struct CascadeFitOutput {
    std::array<CascadeFitCascade, kMaxShadowCascades> cascades;
    // 有効なカスケードの数。入力が不正(光の向きがゼロ、距離が0以下など)なら0。
    std::uint32_t count = 0;
};

// カスケードの奥側の境界(カメラ前方向の距離[m])を求める。practical split(対数と等間隔の混合)で、
// 最後の境界は farDistance。対数側の起点は、近クリップに寄りすぎないよう kSplitNearFloorM 以上にする。
std::array<float, kMaxShadowCascades> ComputeCascadeSplits(float nearDistance, float farDistance,
                                                           std::uint32_t cascadeCount, float lambda);

// 視錐台に合わせたカスケードを求める。count は cascadeCount(入力が不正なら0)。
CascadeFitOutput FitCascades(const CascadeFitInput& input);

} // namespace rs::render
