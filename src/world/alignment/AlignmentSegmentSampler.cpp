#include "AlignmentSegmentSampler.h"

#include <cmath>
#include <numbers>

#include "../../core/Epsilon.h"
#include "../../core/Math.h"

namespace rs::world {

namespace {



// Transition セグメントの接線角 θ(l) を localOffs の各点で求める。
// Clothoidは κ の弧長積分の閉形式、SineHalf(未分割)も閉形式。それ以外
// (CubicParabola、およびdomain制約付きSineHalf)は台形則で数値積分する
// (TransitionKappaWithDomain)。
std::vector<float> BuildTransitionThetas(const AlignmentSegment& seg,
                                         const std::vector<float>& localOffs) {
    const float k0 = CurvatureFromRadius(seg.radius);
    const float k1 = CurvatureFromRadius(seg.radiusEnd);
    const float segLen = seg.length;
    const float dk = k1 - k0;
    std::vector<float> thetas(localOffs.size(), 0.0f);
    if (segLen <= rs::core::kZeroLengthEps) return thetas;

    const bool isFullDomain = (seg.domainStart <= 0.0f) && (seg.domainEnd >= 1.0f);

    if (seg.shape == AlignmentSegment::TransitionShape::Clothoid) {
        // ∫(k0 + dk·x/segLen)dx = k0·l + dk·l²/(2L)
        for (size_t i = 0; i < localOffs.size(); ++i) {
            float l = localOffs[i];
            thetas[i] = k0 * l + dk * (l * l) / (2.0f * segLen);
        }
    } else if (seg.shape == AlignmentSegment::TransitionShape::SineHalf && isFullDomain) {
        // ∫(1-cos(πx/segLen))/2 dx = l/2 - (segLen/2π)·sin(πl/segLen)
        for (size_t i = 0; i < localOffs.size(); ++i) {
            float l = localOffs[i];
            thetas[i] = k0 * l + dk * (0.5f * l - (segLen / (2.0f * rs::core::kPiF)) * std::sin(rs::core::kPiF * l / segLen));
        }
    } else { // CubicParabola(常時)、またはdomain制約付きSineHalf: κ の台形則
        float acc = 0.0f;
        float prevL = 0.0f;
        float prevK = TransitionKappaWithDomain(seg, 0.0f);
        for (size_t i = 0; i < localOffs.size(); ++i) {
            float l = localOffs[i];
            float k = TransitionKappaWithDomain(seg, l / segLen);
            if (i > 0) acc += 0.5f * (prevK + k) * (l - prevL);
            thetas[i] = acc;
            prevL = l;
            prevK = k;
        }
    }
    return thetas;
}

} // namespace

void SampleLineSegment(const AlignmentSegment& seg, const std::vector<float>& localOffs,
                       const CantProfile& cantProfile, SampleCursor& cur,
                       std::vector<TrackNodeSample>& out) {
    rs::core::Vec3 dir = cur.dir;
    rs::core::Vec3 right = ComputeRightVector(dir, cur.up);
    rs::core::Vec3 segStart = cur.pos;
    for (float lo : localOffs) {
        float s = cur.sCum + lo;
        TrackNodeSample smp;
        smp.kind = seg.kind;
        smp.pos = segStart + dir * lo;
        smp.tangent = dir;
        smp.up = ComposeCantUp(dir, cantProfile.CantAt(s), right);
        smp.cant = cantProfile.CantAt(s);
        smp.s = s;
        if (cur.first || lo > rs::core::kZeroLengthEps) out.push_back(smp);
        cur.first = false;
    }
    cur.pos = segStart + dir * seg.length;
    // 直進のため cur.dir/cur.up は不変。
    cur.sCum += seg.length;
}

void SampleArcSegment(const AlignmentSegment& seg, const std::vector<float>& localOffs,
                      const CantProfile& cantProfile, SampleCursor& cur,
                      std::vector<TrackNodeSample>& out) {
    const float arcRadius = seg.radius;
    const float curvature = CurvatureFromRadius(arcRadius);
    const float tilt = seg.axisTiltRad;
    const float cosTilt = std::cos(tilt);
    const float sinTilt = std::sin(tilt);
    rs::core::Vec3 segStart = cur.pos;
    // セグメント開始時点の接線/右方向/アップ(区間開始時の実際の3D姿勢)。
    // 複数セグメント(biarc等)を連結しても、直前区間が axisTiltRad で
    // 傾いていれば cur.dir/cur.up にその3D回転結果が反映済み。
    rs::core::Vec3 segFwd0 = cur.dir;
    rs::core::Vec3 segRight0 = ComputeRightVector(segFwd0, cur.up);
    rs::core::Vec3 segUp0 = rs::core::Vec3::Cross(segFwd0, segRight0).Normalized();
    // axisTiltRad による一般化（ロドリゲスの回転公式の閉形式）:
    // 円弧の回転軸を segUp0(この区間開始時の幾何アップ, =cross(segFwd0,segRight0))
    // から segRight0 側へ axisTiltRad だけ segFwd0 まわりに傾ける:
    //   axis(tilt) = segUp0*cos(tilt) + segRight0*sin(tilt)
    // axis(tilt) は常に segFwd0 と直交するため、半径 arcRadius の円弧の接線・位置は
    // 動径ベクトル radialVec(tilt) = axis(tilt) × segFwd0
    //            = segRight0*cos(tilt) - segUp0*sin(tilt)
    // を用いて次の閉形式になる（tilt=0 で水平円弧と一致）:
    //   dir(dtheta)      = segFwd0*cos(dtheta) + radialVec(tilt)*sin(dtheta)
    //   posLocal(dtheta) = segFwd0*arcRadius*sin(dtheta) + radialVec(tilt)*arcRadius*(1-cos(dtheta))
    // segUp0 自身は axis と直交しない(axis·segUp0=cos(tilt))ため、上の閉形式は
    // 使わず RotateAroundAxis(ロドリゲス公式の一般形)で区間終端の cur.up を求める。
    rs::core::Vec3 axis = segUp0 * cosTilt + segRight0 * sinTilt;
    rs::core::Vec3 radialVec = segRight0 * cosTilt - segUp0 * sinTilt;
    for (float lo : localOffs) {
        float s = cur.sCum + lo;
        float dtheta = curvature * lo; // theta(lo) = lo / arcRadius (セグメント内相対角)
        rs::core::Vec3 worldOffset;
        rs::core::Vec3 dirHere;
        // arcRadius=0(直線扱い)は上のLine分岐で処理されるためここでは |arcRadius|>=rs::core::kZeroLengthEps 前提。
        if (std::fabs(arcRadius) > rs::core::kZeroLengthEps) {
            float cd = std::cos(dtheta), sd = std::sin(dtheta);
            worldOffset = segFwd0 * (arcRadius * sd) + radialVec * (arcRadius * (1.0f - cd));
            dirHere = segFwd0 * cd + radialVec * sd;
        } else {
            worldOffset = segFwd0 * lo;
            dirHere = segFwd0;
        }
        TrackNodeSample smp;
        smp.kind = seg.kind;
        smp.pos = segStart + worldOffset;
        smp.tangent = dirHere;
        smp.up = ComposeCantUp(dirHere, cantProfile.CantAt(s), segRight0);
        smp.cant = cantProfile.CantAt(s);
        smp.s = s;
        if (cur.first || lo > rs::core::kZeroLengthEps) out.push_back(smp);
        cur.first = false;
    }
    float dthetaTotal = curvature * seg.length;
    if (std::fabs(arcRadius) > rs::core::kZeroLengthEps) {
        float cd = std::cos(dthetaTotal), sd = std::sin(dthetaTotal);
        cur.pos = segStart + segFwd0 * (arcRadius * sd) + radialVec * (arcRadius * (1.0f - cd));
        cur.dir = segFwd0 * cd + radialVec * sd;
        cur.up = RotateAroundAxis(segUp0, axis, dthetaTotal);
    } else {
        cur.pos = segStart + segFwd0 * seg.length;
        // dtheta=0相当のためcur.dir/cur.upは不変。
    }
    cur.sCum += seg.length;
}

void SampleTransitionSegment(const AlignmentSegment& seg, const std::vector<float>& localOffs,
                             const CantProfile& cantProfile, SampleCursor& cur,
                             std::vector<TrackNodeSample>& out) {
    const float segLen = seg.length;
    rs::core::Vec3 segStart = cur.pos;
    rs::core::Vec3 segFwd0 = cur.dir;
    rs::core::Vec3 segRight0 = ComputeRightVector(segFwd0, cur.up);

    std::vector<float> thetaLoc = BuildTransitionThetas(seg, localOffs);

    auto dirAtLocal = [&](float theta) {
        return segFwd0 * std::cos(theta) + segRight0 * std::sin(theta);
    };

    rs::core::Vec3 accumPos(0, 0, 0);
    float prevLocal = 0.0f;
    rs::core::Vec3 prevDir = dirAtLocal(0.0f);
    for (size_t idx = 0; idx < localOffs.size(); ++idx) {
        float lo = localOffs[idx];
        float theta = thetaLoc[idx];
        rs::core::Vec3 dirHere = dirAtLocal(theta);
        if (idx > 0) {
            float dl = lo - prevLocal;
            accumPos += (prevDir + dirHere) * (0.5f * dl); // 台形則
        }
        float s = cur.sCum + lo;
        TrackNodeSample smp;
        smp.kind = seg.kind;
        smp.pos = segStart + accumPos;
        smp.tangent = dirHere;
        smp.up = ComposeCantUp(dirHere, cantProfile.CantAt(s), segRight0);
        smp.cant = cantProfile.CantAt(s);
        smp.s = s;
        if (cur.first || lo > rs::core::kZeroLengthEps) out.push_back(smp);
        cur.first = false;
        prevLocal = lo;
        prevDir = dirHere;
    }
    cur.dir = dirAtLocal(thetaLoc.empty() ? 0.0f : thetaLoc.back());
    cur.pos = segStart + accumPos;
    // cur.up は水平面内回転のため不変。
    cur.sCum += segLen;
}

} // namespace rs::world
