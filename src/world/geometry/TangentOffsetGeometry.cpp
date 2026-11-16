//
// TangentOffsetGeometry.cpp - 2接線交点(終点ノードスナップ用)・複線オフセットの
// 純粋幾何ソルバ。3点円弧フィレットはThreePointArcSolver.cpp、双円弧(biarc)は
// BiarcSolver.cppにある。
//
#include "../../core/Epsilon.h"
#include "GeometrySolvers.h"
#include "EditorGeometryTuning.h"

#include <cmath>

namespace rs::world {

using rs::core::Vec3;

// ============================== 2接線交点(終点ノードスナップ用) ==============================

// P0+s*t0 = target-u*t1 (s,u共に正) の交点を求める。t0/t1は単位・水平。
// 解なし(平行/交点が後方 s<eps or u<eps)は false。
bool IntersectTangentRays(const Vec3& p0, const Vec3& t0, const Vec3& target, const Vec3& t1,
                          Vec3& outIntersection) {
    // P0 + s*t0 - (-u)*t1 = target  ->  s*t0 + u*t1 = target - P0
    // 2元連立(水平面, x/z成分)を解く。
    Vec3 diff(target.x - p0.x, 0.0f, target.z - p0.z);
    float a = t0.x, b = t1.x;
    float c = t0.z, d = t1.z;
    float det = a * d - b * c;
    if (std::fabs(det) < rs::core::kHorizontalIntersectionDetEps) return false; // 平行
    float s = (diff.x * d - b * diff.z) / det;
    float u = (a * diff.z - diff.x * c) / det;
    constexpr float kMinParam = 0.05f;
    if (s < kMinParam || u < kMinParam) return false;
    outIntersection = p0 + t0 * s;
    return true;
}

Vec3 ComposeTangentDragCandidate(const Vec3& xzUnit, float originalTangentY, int side) {
    const float signMul = (side < 0) ? -1.0f : 1.0f;
    return Vec3(xzUnit.x * signMul, originalTangentY, xzUnit.z * signMul);
}

// ============================== OffsetCurve(複線オフセット) ==============================

namespace {

// 1つの符号付き半径 radius を進行方向右手へ offset[m] 平行移動した後の半径 R' を
// 求める。平行曲線の曲率変換 κ' = κ/(1-e·κ) は R' = R - offset と厳密に等価
// (1/(R-offset) = (1/R)/(1-offset/R))。
//   straightAllowed=true のとき |radius|≈0(曲率0=直線側)は R'=0 のまま有効とする
//     (緩和曲線の直線端で使う。直線を平行移動しても直線)。
//   false のときは |radius|≈0 を degenerate 扱いで拒否する(円弧区間で使う)。
// R' が最小半径未満、または符号反転(中心跨ぎ)なら false を返し rejectReason を設定。
bool OffsetSignedRadius(float radius, float offset, bool straightAllowed, float& outRadius,
                        const char*& rejectReason, const EditorGeometryTuning& tuning) {
    if (std::fabs(radius) < rs::core::kTrackPositionEpsM) {
        if (straightAllowed) {
            outRadius = 0.0f;
            return true;
        }
        rejectReason = "degenerate arc radius";
        return false;
    }
    float rPrime = radius - offset;
    if (std::fabs(rPrime) < tuning.minArcRadiusMeters) {
        rejectReason = "offset radius too small";
        return false;
    }
    if ((rPrime > 0.0f) != (radius > 0.0f)) {
        rejectReason = "offset crosses arc center";
        return false;
    }
    outRadius = rPrime;
    return true;
}

} // namespace

// curve を進行方向右手方向へ offset[m](正=右)平行移動したcurveを返す。
//   Line: 長さ不変でそのままコピー(平行移動は始点座標側で吸収されるためcurve
//         自体は同一)。
//   Arc:  theta = length/|radius|(旋回角の大きさ)。R' = radius - offset。
//         |R'| < 最小円弧半径、または符号反転(中心跨ぎ)なら invalid。
//         len' = |R'| * theta。
//   Transition(緩和曲線): 真の平行曲線は同一形状族の緩和曲線にならない
//         (κ'=κ/(1-e·κ) が非線形)ため、同形状の緩和曲線で近似する。端点曲率は
//         R'=R-offset で厳密一致(直線側 κ=0 はそのまま直線)、区間長は
//         L'=L-offset·Δθ で補正する(Δθ=区間の符号付き旋回角=L·(κ0+κ1)/2。
//         Clothoid/SineHalf では厳密、CubicParabola では小偏角近似)。長さ補正を
//         省くと端点が offset·Δθ(数十cm)ずれるため必須。offset·κ≪1 の軌間
//         スケールでは残差は高次微小で、境界の接線連続も近似的に保たれる。
OffsetCurveResult OffsetCurve(const std::vector<rs::world::AlignmentSegment>& curve, float offset) {
    const EditorGeometryTuning& tuning = EditorGeometryTuning::Embedded();
    OffsetCurveResult out;
    out.curve.reserve(curve.size());
    for (const auto& seg : curve) {
        if (seg.kind == rs::world::AlignmentSegment::Kind::Line) {
            out.curve.push_back(seg);
            continue;
        }
        if (seg.kind == rs::world::AlignmentSegment::Kind::Arc) {
            float rPrime;
            if (!OffsetSignedRadius(seg.radius, offset, /*straightAllowed=*/false, rPrime,
                                    out.rejectReason, tuning))
                return out;
            float theta = seg.length / std::fabs(seg.radius);
            rs::world::AlignmentSegment ns;
            ns.kind = rs::world::AlignmentSegment::Kind::Arc;
            ns.radius = rPrime;
            ns.length = std::fabs(rPrime) * theta;
            out.curve.push_back(ns);
            continue;
        }
        // Transition(緩和曲線): 端点半径をそれぞれオフセットし、長さを Δθ で補正する。
        float r0Prime, r1Prime;
        if (!OffsetSignedRadius(seg.radius, offset, /*straightAllowed=*/true, r0Prime,
                                out.rejectReason, tuning))
            return out;
        if (!OffsetSignedRadius(seg.radiusEnd, offset, /*straightAllowed=*/true, r1Prime,
                                out.rejectReason, tuning))
            return out;
        const float k0 = (std::fabs(seg.radius) < rs::core::kTrackPositionEpsM) ? 0.0f : 1.0f / seg.radius;
        const float k1 = (std::fabs(seg.radiusEnd) < rs::core::kTrackPositionEpsM) ? 0.0f : 1.0f / seg.radiusEnd;
        const float dTheta = seg.length * (k0 + k1) * 0.5f; // 符号付き旋回角
        const float lenPrime = seg.length - offset * dTheta;
        if (lenPrime < rs::core::kGeometryLengthEpsM) { // 長さが潰れる異常(軌間スケールでは通常起きない)
            out.rejectReason = "transition length collapsed";
            return out;
        }
        rs::world::AlignmentSegment ns;
        ns.kind = rs::world::AlignmentSegment::Kind::Transition;
        ns.radius = r0Prime;
        ns.radiusEnd = r1Prime;
        ns.length = lenPrime;
        ns.shape = seg.shape;
        out.curve.push_back(ns);
    }
    out.valid = true;
    return out;
}

// ============================== 3点フィレット再ソルブ ==============================

bool TryRefitThreePointArc(const Vec3& p0, const Vec3& dirA, float nodeATangentSign, const Vec3& p1,
                           const Vec3& dirB, const TransitionSpec& spec,
                           ThreePointArcSolveResult& outSol, Vec3& outStartTangent) {
    Vec3 t0raw(dirA.x, 0.0f, dirA.z);
    const float t0len = t0raw.Length();
    if (t0len < rs::core::kDegenerateVectorEps) return false;
    const Vec3 t0 = t0raw * (nodeATangentSign / t0len);
    Vec3 t1raw(dirB.x, 0.0f, dirB.z);
    const float t1len = t1raw.Length();
    if (t1len < rs::core::kDegenerateVectorEps) return false;
    const Vec3 t1n = t1raw * (1.0f / t1len);

    const Vec3 candidates[2] = {t1n, t1n * -1.0f};
    for (const Vec3& t1cand : candidates) {
        Vec3 c;
        if (!IntersectTangentRays(p0, t0, p1, t1cand, c)) continue;
        ThreePointArcSolveResult sol = SolveThreePointArc(p0, c, p1, spec);
        if (!(sol.valid && sol.acceptable)) continue;
        outSol = sol;
        outStartTangent = t0;
        return true;
    }
    return false;
}

} // namespace rs::world
