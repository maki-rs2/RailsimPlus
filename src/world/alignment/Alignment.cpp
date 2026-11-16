#include "Alignment.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../../core/Epsilon.h"
#include "AlignmentSegmentSampler.h"
#include "CantProfile.h"
#include "../../core/Math.h"

namespace rs::world {

namespace {

// サンプル刻みの下限[m]。0や極小の刻みで分割数が爆発しないようにする。
constexpr float kMinSampleStepM = rs::core::kTrackPositionEpsM;

// セグメント境界を含む形で [0, length] を sampleStep 刻みに分割した弧長リストを返す。
// 端点(0とlength)は必ず含む。length<=0 の場合は {0} のみ。
std::vector<float> BuildLocalOffsets(float length, float sampleStep) {
    std::vector<float> offs;
    if (length <= rs::core::kZeroLengthEps) {
        offs.push_back(0.0f);
        return offs;
    }
    int n = static_cast<int>(std::ceil(length / std::max(sampleStep, kMinSampleStepM)));
    n = std::max(n, 1);
    offs.reserve(n + 1);
    for (int i = 0; i <= n; ++i) {
        float s = length * static_cast<float>(i) / static_cast<float>(n);
        offs.push_back(s);
    }
    return offs;
}

} // namespace

// CubicParabola/SineHalfのTransitionセグメントについて、piece内ローカル長さ割合uLocal
// (0..1)における曲率を返す。宣言はAlignment.h。
//
// 導出: 元の緩和曲線の相対形状進行度 S(u_global)(u_global∈[0,1]、S(0)=0,S(1)=1)を、
// F(u)=TransitionProfileF(shape,u,tau)として
//   S(u) = mirrored ? (1 - F(1-u)) : F(u)
// と定義する(SineHalfはF自体が対称なのでmirroredは無関係、CubicParabolaは元の
// BuildTransitionThetas実装にあった mirrored 分岐と同じ式)。このsegの実際の境界曲率
// (seg.radius/seg.radiusEnd)は、元の緩和曲線の S(domainStart)/S(domainEnd) に対応する
// 値であるという前提のもと、piece内ローカル割合uLocalにおける相対進行度は
//   g(uLocal) = [S(domainStart + uLocal·(domainEnd-domainStart)) - S(domainStart)]
//               / [S(domainEnd) - S(domainStart)]
// で与えられる(domainStart=0/domainEnd=1のときg(uLocal)=S(uLocal)に一致、既存
// (分割導入前)の動作と完全に一致する)。最終的な曲率は
//   kappa(uLocal) = k0 + (k1-k0)·g(uLocal)   (k0=seg.radiusの曲率, k1=seg.radiusEndの曲率)
float TransitionKappaWithDomain(const AlignmentSegment& seg, float uLocal) {
    const float k0 = CurvatureFromRadius(seg.radius);
    const float k1 = CurvatureFromRadius(seg.radiusEnd);
    const float dk = k1 - k0;
    const bool mirrored = std::fabs(k0) > std::fabs(k1);
    // domainStart==0&&domainEnd==1(未分割)なら、このセグメント自身のlength/radius/
    // radiusEndからtauを都度計算する(domainTauは無視、CubicParabola限定で使う値)。
    const bool isFullDomain = (seg.domainStart <= 0.0f) && (seg.domainEnd >= 1.0f);
    const float tau = isFullDomain ? (0.5f * seg.length * std::fabs(dk)) : seg.domainTau;

    auto sAt = [&](float uGlobal) -> float {
        const float f = TransitionProfileF(seg.shape, uGlobal, tau);
        if (seg.shape == AlignmentSegment::TransitionShape::SineHalf) return f;  // 対称、mirrored無関係
        return mirrored ? (1.0f - TransitionProfileF(seg.shape, 1.0f - uGlobal, tau)) : f;
    };

    const float sStart = sAt(seg.domainStart);
    const float sEnd = sAt(seg.domainEnd);
    const float denom = sEnd - sStart;
    const float uGlobal = seg.domainStart + uLocal * (seg.domainEnd - seg.domainStart);
    const float g = (std::fabs(denom) < rs::core::kZeroLengthEps) ? uLocal : (sAt(uGlobal) - sStart) / denom;

    return k0 + dk * g;
}

float CurvatureFromRadius(float radius) {
    if (std::fabs(radius) < rs::core::kZeroLengthEps) return 0.0f;
    return 1.0f / radius;
}

// 緩和曲線の曲率逓減形状 f(u)（u∈[0,1], f(0)=0, f(1)=1）。
// tau は CubicParabola 用の終端接線傾き係数 ≈ L·|κ1-κ0|/2（他形状は未使用）。
// CubicParabola は y=x³/(6RX) の厳密曲率 κ(x)=A·x·(1+(A x²/2)²)^{-3/2} (A=1/RX) を
// 端点で 1 に正規化したもの: f(u) = u·((1+τ²)/(1+τ²u⁴))^{3/2}。弧長 s≈x 近似
//（緩和曲線の偏角は小さい前提。τ→0 でクロソイドの f(u)=u に一致）。
float TransitionProfileF(AlignmentSegment::TransitionShape shape, float u, float tau) {
    u = std::clamp(u, 0.0f, 1.0f);
    switch (shape) {
        case AlignmentSegment::TransitionShape::SineHalf:
            return 0.5f * (1.0f - std::cos(rs::core::kPiF * u));
        case AlignmentSegment::TransitionShape::CubicParabola: {
            float t2 = tau * tau;
            float u2 = u * u;
            float ratio = (1.0f + t2) / (1.0f + t2 * u2 * u2);
            return u * ratio * std::sqrt(ratio);
        }
        case AlignmentSegment::TransitionShape::Clothoid:
        default:
            return u;
    }
}

rs::core::Vec3 ComputeRightVector(const rs::core::Vec3& dir, const rs::core::Vec3& up) {
    rs::core::Vec3 right = rs::core::Vec3::Cross(up, dir).Normalized();
    if (right.Length() < rs::core::kDegenerateVectorEps) {
        right = rs::core::Vec3::Cross(rs::core::Vec3(0, 1, 0), dir).Normalized();
        if (right.Length() < rs::core::kDegenerateVectorEps) right = rs::core::Vec3(1, 0, 0);
    }
    return right;
}

rs::core::Vec3 RotateAroundAxis(const rs::core::Vec3& v, const rs::core::Vec3& axis, float angleRad) {
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float dotAV = rs::core::Vec3::Dot(axis, v);
    return v * c + rs::core::Vec3::Cross(axis, v) * s + axis * (dotAV * (1.0f - c));
}

rs::core::Vec3 ComposeCantUp(const rs::core::Vec3& tangent, float cant,
                             const rs::core::Vec3& fallbackRight) {
    if (tangent.Length() < rs::core::kDegenerateVectorEps) return rs::core::Vec3(0.0f, 1.0f, 0.0f);
    const rs::core::Vec3 normalizedTangent = tangent.Normalized();

    const rs::core::Vec3 worldUp(0.0f, 1.0f, 0.0f);
    rs::core::Vec3 right = rs::core::Vec3::Cross(worldUp, normalizedTangent).Normalized();
    if (right.Length() < rs::core::kDegenerateVectorEps) right = fallbackRight.Normalized();
    if (right.Length() < rs::core::kDegenerateVectorEps) right = rs::core::Vec3(1.0f, 0.0f, 0.0f);

    const rs::core::Vec3 upGeom = rs::core::Vec3::Cross(normalizedTangent, right).Normalized();
    return (upGeom + right * cant).Normalized();
}

TransitionEndPose EvalTransitionEnd(AlignmentSegment::TransitionShape shape,
                                    float curvature, float length, int steps) {
    TransitionEndPose pose;
    if (length <= rs::core::kZeroLengthEps) return pose;
    if (curvature < 0.0f) curvature = -curvature;
    steps = std::max(steps, 8);
    const float tau = 0.5f * length * curvature;
    // ローカル座標: z=初期接線, x=曲がる側。θ とともに (dz,dx)=(cosθ,sinθ) を台形則で積算。
    float theta = 0.0f;
    float prevK = 0.0f;                       // κ(0)=0（f(0)=0）
    float prevCz = 1.0f, prevCx = 0.0f;       // (cos0, sin0)
    const float dl = length / static_cast<float>(steps);
    for (int i = 1; i <= steps; ++i) {
        float l = length * static_cast<float>(i) / static_cast<float>(steps);
        float k = curvature * TransitionProfileF(shape, l / length, tau);
        theta += 0.5f * (prevK + k) * dl;
        float cz = std::cos(theta), cx = std::sin(theta);
        pose.z += 0.5f * (prevCz + cz) * dl;
        pose.x += 0.5f * (prevCx + cx) * dl;
        prevK = k;
        prevCz = cz;
        prevCx = cx;
    }
    pose.theta = theta;
    return pose;
}

float AlignmentTotalLength(const std::vector<AlignmentSegment>& curve) {
    float total = 0.0f;
    for (const auto& seg : curve) total += seg.length;
    return total;
}

std::vector<TrackNodeSample> SampleAlignment(const std::vector<AlignmentSegment>& curve,
                                              const rs::core::Vec3& startPos,
                                              const rs::core::Vec3& startDir,
                                              const rs::core::Vec3& startUp,
                                              float cantStart, float cantEnd,
                                              const AlignmentSampleOptions& opt,
                                              float cantMid, float cantTransitionRatio,
                                              AlignmentSegment::TransitionShape cantTransitionShape,
                                              const std::vector<CantControlPoint>& cantPoints) {
    std::vector<TrackNodeSample> out;
    if (curve.empty()) return out;

    // ローカル基底: e_fwd0=起点接線, e_right0=起点右方向(=既存 BuildRailMesh の
    // +X 円弧規約に対応する軸)。up は e_fwd0×e_right0 と直交する平面内。
    rs::core::Vec3 eFwd0 = startDir.Normalized();
    if (eFwd0.Length() < rs::core::kDegenerateVectorEps) eFwd0 = rs::core::Vec3(0, 0, 1);
    rs::core::Vec3 upRef = startUp.Normalized();
    if (upRef.Length() < rs::core::kDegenerateVectorEps) upRef = rs::core::Vec3(0, 1, 0);
    // e_right0 = cross(upRef, eFwd0) 正規化（水平面内、既存円弧の+X相当）。
    // 例: eFwd0=(0,0,1), upRef=(0,1,0) -> cross((0,1,0),(0,0,1)) = (1,0,0) = +X。
    rs::core::Vec3 eRight0 = rs::core::Vec3::Cross(upRef, eFwd0).Normalized();
    if (eRight0.Length() < rs::core::kDegenerateVectorEps) {
        // eFwd0とupRefがほぼ平行（縦向き線形は本カーネルの対象外）。
        // フォールバックでワールドXを使う。
        eRight0 = rs::core::Vec3::Cross(rs::core::Vec3(0, 1, 0), eFwd0).Normalized();
        if (eRight0.Length() < rs::core::kDegenerateVectorEps) eRight0 = rs::core::Vec3(1, 0, 0);
    }
    // up0 を再直交化（起点でのアップ）。cross(eFwd0,eRight0)=(0,0,1)x(1,0,0)=(0,1,0)。
    rs::core::Vec3 up0 = rs::core::Vec3::Cross(eFwd0, eRight0).Normalized();

    const float totalLen = AlignmentTotalLength(curve);

    // ---- カントプロファイル ----
    // 詳細ロジックは CantProfile にある。
    const CantProfile cantProfile(curve, totalLen, cantStart, cantEnd, cantMid,
                                  cantTransitionRatio, cantTransitionShape, cantPoints);

    SampleCursor cur;
    cur.pos = startPos;
    cur.dir = eFwd0;
    cur.up = up0;
    for (const auto& seg : curve) {
        std::vector<float> localOffs = BuildLocalOffsets(seg.length, opt.sampleStep);
        if (seg.kind == AlignmentSegment::Kind::Line) {
            SampleLineSegment(seg, localOffs, cantProfile, cur, out);
        } else if (seg.kind == AlignmentSegment::Kind::Arc) {
            SampleArcSegment(seg, localOffs, cantProfile, cur, out);
        } else { // Transition(緩和曲線)
            SampleTransitionSegment(seg, localOffs, cantProfile, cur, out);
        }
    }

    return out;
}

// RebuildEdgeSpline / RebuildEdgesForNode は
// AlignmentGraphRebuild.cpp（TrackGraph を辿るグラフ操作レイヤー）に実装がある。
// 宣言は Alignment.h に統一する。

} // namespace rs::world
