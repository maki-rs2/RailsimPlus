//
// BiarcSolver.cpp - 双円弧(biarc)ソルバ(S字接続)。敷設済みノード同士の
// 任意接線ペアを等パラメータd方式の一般biarc閉形式解で結ぶ。3点円弧
// フィレットはThreePointArcSolver.cpp、複線オフセット/2接線交点は
// TangentOffsetGeometry.cppにある。共通宣言はGeometrySolvers.h。
//
#include "../../core/Epsilon.h"
#include "GeometrySolvers.h"
#include "EditorGeometryTuning.h"

#include <cmath>

namespace rs::world {

using rs::core::Vec3;

// ============================== 双円弧(biarc)ソルバ(S字接続) ==============================
//
// 任意の水平接線ペア(t0,t1)に対する等パラメータd方式の一般biarc閉形式解。
// 制御多角形 P0, A=P0+d*t0, B=P1-d*t1, P1 で |B-A|=2d となる d を二次方程式で解き、
// 接続点J=(A+B)/2 を境に弧2本(半径は弧ごとに異なってよい)を張る。J での接線は
// dir(B-A) で両弧共通になるため G1 連続が構成的に成り立つ。
// 平行同方向オフセット(渡り線)では対称等半径S字と同一解になる。
namespace {

// 弧1本分のセグメントを作る。chord=弦ベクトル(水平)、tangent=その弧の
// 「弦に対して旋回角の半分だけ傾いた側の接線」(始端接線または終端接線)。
// chordFromTangent=true なら tangent は始端接線、false なら終端接線。
// 旋回角が実質0なら直線セグメントに退化させる。
bool BuildBiarcSegment(const Vec3& chord, const Vec3& tangent, bool tangentIsStart,
                       rs::world::AlignmentSegment& outSeg, float& outTheta, float& outRadius,
                       const char*& rejectReason) {
    Vec3 up(0, 1, 0);
    float chordLen = chord.Length();
    if (chordLen < rs::core::kGeometryLengthEpsM) {
        rejectReason = "biarc: degenerate chord";
        return false;
    }
    // 符号付き半旋回角: 始端接線→弦 (または 弦→終端接線) のなす角。
    float halfAngle;
    if (tangentIsStart) {
        Vec3 rightT = Vec3::Cross(up, tangent).Normalized();
        halfAngle = std::atan2(Vec3::Dot(chord, rightT), Vec3::Dot(chord, tangent));
    } else {
        Vec3 u = chord * (1.0f / chordLen);
        Vec3 rightU = Vec3::Cross(up, u).Normalized();
        halfAngle = std::atan2(Vec3::Dot(tangent, rightU), Vec3::Dot(tangent, u));
    }
    float theta = 2.0f * halfAngle;
    outTheta = theta;
    if (std::fabs(halfAngle) < rs::core::kBiarcHalfAngleEpsRad) {
        // 実質直線。
        outSeg.kind = rs::world::AlignmentSegment::Kind::Line;
        outSeg.radius = 0.0f;
        outSeg.length = chordLen;
        outRadius = 0.0f;
        return true;
    }
    float arcRadius = chordLen / (2.0f * std::fabs(std::sin(halfAngle)));
    float sgn = (theta >= 0.0f) ? 1.0f : -1.0f;
    outSeg.kind = rs::world::AlignmentSegment::Kind::Arc;
    outSeg.radius = sgn * arcRadius;
    outSeg.length = arcRadius * std::fabs(theta);
    outRadius = sgn * arcRadius;
    return true;
}

} // namespace

BiarcSolveResult SolveBiarc(const Vec3& p0, const Vec3& t0In, const Vec3& p1, const Vec3& t1In) {
    const EditorGeometryTuning& tuning = EditorGeometryTuning::Embedded();
    BiarcSolveResult r;

    Vec3 t0h(t0In.x, 0.0f, t0In.z);
    float t0Len = t0h.Length();
    Vec3 t1h(t1In.x, 0.0f, t1In.z);
    float t1Len = t1h.Length();
    if (t0Len < rs::core::kDegenerateVectorEps || t1Len < rs::core::kDegenerateVectorEps) {
        r.rejectReason = "biarc: degenerate tangent";
        return r;
    }
    Vec3 t0 = t0h * (1.0f / t0Len);
    Vec3 t1 = t1h * (1.0f / t1Len);

    Vec3 v(p1.x - p0.x, 0.0f, p1.z - p0.z);
    float vv = Vec3::Dot(v, v);
    if (vv < rs::core::kPlacementSegmentLengthSquaredEpsM2) {
        r.rejectReason = "biarc: endpoints coincide";
        return r;
    }

    // |v - d(t0+t1)|^2 = (2d)^2 を d について解く:
    //   d^2*(2-2c) + 2d*(v・t) - v・v = 0, c = t0・t1, t = t0+t1
    float c = Vec3::Dot(t0, t1);
    Vec3 tsum = t0 + t1;
    float vt = Vec3::Dot(v, tsum);
    float d;
    float a2 = 2.0f - 2.0f * c;
    if (a2 > rs::core::kDirectionParallelEps) {
        // a2*d^2 + 2*vt*d - vv = 0 の正根(根の積<0なので必ず存在)。
        float disc = vt * vt + a2 * vv;
        d = (-vt + std::sqrt(disc)) / a2;
    } else {
        // 平行同方向。前方でなければ解なし。
        if (vt <= rs::core::kTrackPositionEpsM) {
            r.rejectReason = "biarc: not forward";
            return r;
        }
        d = vv / (2.0f * vt);
    }
    if (!(d > rs::core::kTrackPositionEpsM) || !std::isfinite(d)) {
        r.rejectReason = "biarc: no positive solution";
        return r;
    }

    Vec3 a = p0 + t0 * d;
    Vec3 b = p1 - t1 * d;
    Vec3 joint = (a + b) * 0.5f;
    joint.y = 0.0f;
    Vec3 p0h(p0.x, 0.0f, p0.z), p1h(p1.x, 0.0f, p1.z);

    rs::world::AlignmentSegment seg1, seg2;
    float theta1 = 0.0f, theta2 = 0.0f, radius1 = 0.0f, radius2 = 0.0f;
    if (!BuildBiarcSegment(joint - p0h, t0, /*tangentIsStart=*/true, seg1, theta1, radius1,
                           r.rejectReason))
        return r;
    if (!BuildBiarcSegment(p1h - joint, t1, /*tangentIsStart=*/false, seg2, theta2, radius2,
                           r.rejectReason))
        return r;

    r.valid = true;
    r.theta = theta1;
    r.radius = std::fabs(radius1);
    r.theta2 = theta2;
    r.radius2 = std::fabs(radius2);
    r.joint = joint;
    r.endTangent = t1;
    r.curve.push_back(seg1);
    r.curve.push_back(seg2);

    for (const auto& seg : r.curve) {
        if (seg.kind != rs::world::AlignmentSegment::Kind::Arc) continue;
        if (std::fabs(seg.radius) < tuning.minArcRadiusMeters) {
            r.rejectReason = "biarc: radius too small";
            return r;
        }
    }
    if (std::fabs(theta1) >= tuning.maxTurnAngleRadians ||
        std::fabs(theta2) >= tuning.maxTurnAngleRadians) {
        r.rejectReason = "biarc: theta>=175deg";
        return r;
    }
    if (seg1.length + seg2.length < tuning.minAcceptableLengthMeters) {
        r.rejectReason = "biarc: length<0.5m";
        return r;
    }

    r.acceptable = true;
    return r;
}

} // namespace rs::world
