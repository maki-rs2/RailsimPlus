#include "Rs2CurveFit.h"

#include "../../world/geometry/EditorGeometryTuning.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../world/geometry/GeometrySolvers.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/EdgeShapeGenerator.h"
#include "../../world/alignment/TrackNode.h"

namespace rs::legacy_layout {

using rs::core::Vec3;
using rs::world::AlignmentSegment;

namespace {

Vec3 Horizontal(const Vec3& v) { return Vec3(v.x, 0.0f, v.z); }

// query(水平)から poly(水平, 折れ線)への最短距離が tolerance 以下か。弧長パラメータでの対応点比較だと
// 直線+円弧+直線のように区間境界の弧長がわずかにずれるだけで直線区間の点が円弧区間の
// 点と比較され誤差が過大評価されるため、幾何的な最近傍点距離(点-線分距離の最小)で見る。
// 最短距離そのものは要らず「許容以内の線分が1本でもあるか」だけなので、見つかった時点で打ち切る。
// hint(前回見つかった線分の番号)から順に探す。経路に沿って並んだ点列なら、次の点もほぼ同じ線分の近くにある。
bool IsWithinPolylineHorizontal(const std::vector<rs::world::TrackNodeSample>& poly, const Vec3& query,
                                float tolerance, size_t& hint) {
    if (poly.size() == 1) return (query - Horizontal(poly.front().pos)).Length() <= tolerance;
    const size_t segmentCount = poly.size() - 1;
    for (size_t k = 0; k < segmentCount; ++k) {
        const size_t i = (hint + k) % segmentCount;
        const Vec3 a = Horizontal(poly[i].pos);
        const Vec3 b = Horizontal(poly[i + 1].pos);
        const float t = static_cast<float>(rs::core::ClosestSegmentParameter(
            {a.x, a.y, a.z}, {b.x, b.y, b.z}, {query.x, query.y, query.z},
            rs::core::kImportedSegmentLengthSquaredEpsM2));
        const Vec3 proj(std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t));
        if ((query - proj).Length() <= tolerance) {
            hint = i;
            return true;
        }
    }
    return false;
}

// a の全点が b の折れ線から tolerance 以内にあるか(片方向Hausdorff距離が許容以下か)。
bool IsOneSidedHausdorffWithinHorizontal(const std::vector<rs::world::TrackNodeSample>& a,
                                         const std::vector<rs::world::TrackNodeSample>& b, float tolerance) {
    size_t hint = 0;
    for (const rs::world::TrackNodeSample& smp : a)
        if (!IsWithinPolylineHorizontal(b, Horizontal(smp.pos), tolerance, hint)) return false;
    return true;
}

// 3D接線t0/t1と弦chordが単一平面上に収まっているか(=単一円弧として無理なく近似できるか)を
// 判定する。t0/t1がほぼ平行(直線退化)な場合はtrue(判定不能=スキップの意味)を返す。
bool IsApproximatelyCoplanar(const Vec3& t0Raw, const Vec3& t1Raw, const Vec3& chord) {
    const Vec3 t0 = t0Raw.Normalized();
    const Vec3 t1 = t1Raw.Normalized();
    const Vec3 cross01 = Vec3::Cross(t0, t1);
    const float crossLen = cross01.Length();
    if (crossLen < rs::core::kImportedLineAngleEpsRad) return true;  // 直線退化: 平面が定義できないためスキップ

    const Vec3 axis = cross01 * (1.0f / crossLen);
    const float chordLen = chord.Length();
    if (chordLen < rs::core::kTrackPositionEpsM) return true;  // 弦がほぼ0: 判定不能

    const float chordAxisDot = std::fabs(Vec3::Dot(axis, chord * (1.0f / chordLen)));
    return chordAxisDot <= rs::world::EditorGeometryTuning::Embedded().importedCoplanarityToleranceDot;
}

// dir0→dir1(共に水平・正規化済み)の符号付き偏角[rad]。
// +側="+X側へ曲がる"(Alignment.hのArc規約、BiarcSolver.cppと同じ符号系:
// right0 = cross(up, dir0) を基準に dir1 がどちら側へ傾いたかで符号を決める)。
float SignedTurnAngle(const Vec3& dir0, const Vec3& dir1) {
    const Vec3 right0 = Vec3::Cross(Vec3(0, 1, 0), dir0).Normalized();
    return std::atan2(Vec3::Dot(dir1, right0), Vec3::Dot(dir1, dir0));
}

// 1区間(隣接Splitter2点間)分の中間データ。
struct Interval {
    float chordLen = 0.0f;  // 水平弦長[m]
    float angle = 0.0f;     // 符号付き偏角[rad]
};

void AppendLineSegment(std::vector<AlignmentSegment>& out, float length) {
    if (length < rs::core::kTrackPositionEpsM) return;
    AlignmentSegment seg;
    seg.kind = AlignmentSegment::Kind::Line;
    seg.length = length;
    out.push_back(seg);
}

// intervals[first..last](同符号の偏角を持つ区間群)から円弧1本を復元して追加する。
// 真円なら chordLen = 2R sin(angle/2) が厳密に成り立つため、区間ごとに R を逆算し
// 弦長で重み付け平均する(RailSim II 実データはこの式を満たす前提)。
// 弧長は「重み付け平均した半径 × 区間の総偏角」から求める(個々の弦長の単純合計より
// 真の弧長に近い)。
void AppendArcSegment(std::vector<AlignmentSegment>& out,
                      const std::vector<Interval>& intervals, size_t first, size_t last) {
    double weightedRadiusSum = 0.0;
    double weightSum = 0.0;
    double totalAngle = 0.0;
    for (size_t i = first; i <= last; ++i) {
        const Interval& iv = intervals[i];
        const double halfAngle = 0.5 * static_cast<double>(iv.angle);
        const double sinHalf = std::sin(halfAngle);
        if (std::fabs(sinHalf) < rs::core::kImportedHalfAngleSinEps) continue;  // 実質0偏角(直線扱い漏れ)は無視
        const double r = static_cast<double>(iv.chordLen) / (2.0 * sinHalf);
        weightedRadiusSum += r * iv.chordLen;
        weightSum += iv.chordLen;
        totalAngle += iv.angle;
    }
    if (weightSum < rs::core::kZeroLengthEps) return;  // 有効区間なし

    const double radius = weightedRadiusSum / weightSum;
    const double length = std::fabs(radius) * std::fabs(totalAngle);
    if (length < rs::core::kTrackPositionEpsM) return;

    AlignmentSegment seg;
    seg.kind = AlignmentSegment::Kind::Arc;
    seg.radius = static_cast<float>(radius);
    seg.length = static_cast<float>(length);
    out.push_back(seg);
}

} // namespace

std::vector<AlignmentSegment> FitCurveFromSplitters(const std::vector<Rs2Splitter>& splitters) {
    std::vector<AlignmentSegment> out;
    if (splitters.size() < 2) return out;

    std::vector<Interval> intervals;
    intervals.reserve(splitters.size() - 1);
    for (size_t i = 0; i + 1 < splitters.size(); ++i) {
        const Vec3 p0 = Horizontal(splitters[i].pos);
        const Vec3 p1 = Horizontal(splitters[i + 1].pos);
        const float chordLen = (p1 - p0).Length();
        if (chordLen < rs::core::kTrackPositionEpsM) continue;  // 重複点はスキップ

        const Vec3 d0 = Horizontal(splitters[i].dir).Normalized();
        const Vec3 d1 = Horizontal(splitters[i + 1].dir).Normalized();
        intervals.push_back({chordLen, SignedTurnAngle(d0, d1)});
    }
    if (intervals.empty()) return out;

    auto sign = [](float angle) -> int {
        if (std::fabs(angle) < rs::core::kImportedLineAngleEpsRad) return 0;
        return angle > 0.0f ? 1 : -1;
    };

    size_t runStart = 0;
    int runSign = sign(intervals[0].angle);
    for (size_t i = 1; i <= intervals.size(); ++i) {
        const bool atEnd = (i == intervals.size());
        const int s = atEnd ? runSign : sign(intervals[i].angle);
        if (!atEnd && s == runSign) continue;

        if (runSign == 0) {
            float length = 0.0f;
            for (size_t k = runStart; k < i; ++k) length += intervals[k].chordLen;
            AppendLineSegment(out, length);
        } else {
            AppendArcSegment(out, intervals, runStart, i - 1);
        }
        if (!atEnd) {
            runStart = i;
            runSign = s;
        }
    }
    return out;
}

std::vector<AlignmentSegment> BuildCurveFromSplitters3D(const std::vector<Rs2Splitter>& splitters) {
    // 変換本体は world 側(EdgeShapeGenerator)に1つだけ置く。始点フレームは先頭Splitterの
    // up(RailSim II が保存したup、cant込み)で、BuildEdge/RebuildEdgeSplineの nodeA.up と一致させる。
    std::vector<rs::world::EdgeEndPose> poses;
    poses.reserve(splitters.size());
    for (const Rs2Splitter& s : splitters) poses.push_back({s.pos, s.dir, s.up});
    return rs::world::BuildCurveFromPoseChain(poses);
}

bool IsThreePointFilletRepresentable(const rs::world::TrackEdge& edge,
                                     const rs::world::TrackNode& nodeA,
                                     const rs::world::TrackNode& nodeB) {
    if (edge.curve.empty() || edge.spline.size() < 2) return false;

    // 共面性チェック: t0,t1,chordが単一平面上に収まらない(=縦断方向に捻れている)場合は、
    // 単一円弧では再現不能と判定して早期リターンする(フル3D円弧は扱わない)。
    if (!IsApproximatelyCoplanar(nodeA.tangent, nodeB.tangent, nodeB.pos - nodeA.pos)) return false;

    rs::world::TransitionSpec spec;  // shape既定Clothoid, length=0(判定は緩和曲線なしの円弧で行う)
    rs::world::ThreePointArcSolveResult sol;
    Vec3 startTangent;
    if (!rs::world::TryRefitThreePointArc(nodeA.pos, nodeA.tangent, edge.nodeATangentSign, nodeB.pos,
                                           nodeB.tangent, spec, sol, startTangent))
        return false;

    const std::vector<rs::world::TrackNodeSample> fitted = rs::world::SampleAlignment(
        sol.curve, nodeA.pos, startTangent, nodeA.up, 0.0f, 0.0f);
    if (fitted.size() < 2) return false;

    // 弧長パラメータでの対応点比較は区間境界のわずかなずれで誤差が過大評価されるため、
    // 双方向の最近傍点距離(Hausdorff距離の近似)で経路全体の一致度を見る。
    const float tolerance = rs::world::EditorGeometryTuning::Embedded().importedFilletFitToleranceM;
    return IsOneSidedHausdorffWithinHorizontal(edge.spline, fitted, tolerance) &&
           IsOneSidedHausdorffWithinHorizontal(fitted, edge.spline, tolerance);
}

} // namespace rs::legacy_layout
