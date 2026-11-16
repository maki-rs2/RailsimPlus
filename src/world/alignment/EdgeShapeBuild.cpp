//
// EdgeShapeBuild.cpp - 両端ノードの3D姿勢から edge の curve と spline を作る
//
#include "EdgeShapeBuild.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../../core/Epsilon.h"
#include "../geometry/GeometrySolvers.h"
#include "AutoCant.h"

namespace rs::world {

using rs::core::Vec3;

namespace {

constexpr float kMinHorizontalDirLen = rs::core::kHorizontalDirectionEps;  // ほぼ鉛直な接線は扱わない

EdgeShapeBuild Fail(const std::string& message) {
    EdgeShapeBuild result;
    result.error = message;
    return result;
}

bool IsFiniteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// 終点の残差を s に比例して全サンプルへ配り、最後のサンプルを end の位置・接線にそろえる。
// 配る前の残差が許容を超えていたら失敗として理由を返す(空文字なら成功)。
std::string CloseEdgeEnd(std::vector<TrackNodeSample>& spline, const EdgeEndPose& end,
                         double posTolM, double dirTolDeg) {
    if (spline.size() < 2) return "サンプルが2点未満";
    const TrackNodeSample& last = spline.back();
    const Vec3 residual = end.pos - last.pos;
    const double posErr = residual.Length();
    const Vec3 endDir = end.dir.Normalized();
    const double dirErrDeg = rs::core::AngleBetweenDeg(last.tangent.Normalized(), endDir);
    if (!std::isfinite(posErr) || !std::isfinite(dirErrDeg)) return "終点の残差が有限値にならない";
    if (posErr > posTolM || dirErrDeg > dirTolDeg)
        return "終点が端点姿勢と許容内で一致しない(位置" + std::to_string(posErr * 1000.0) +
               "mm 向き" + std::to_string(dirErrDeg) + "度)";

    const double sTotal = static_cast<double>(last.s);
    if (sTotal > rs::core::kZeroLengthEps) {
        for (TrackNodeSample& smp : spline) {
            const float ratio = static_cast<float>(static_cast<double>(smp.s) / sTotal);
            smp.pos = smp.pos + residual * ratio;
        }
    }
    TrackNodeSample& tail = spline.back();
    tail.pos = end.pos;
    tail.tangent = endDir;
    return {};
}

// 緩和なし: 3D姿勢の再帰分割で作った curve をそのままサンプルする。
EdgeShapeBuild BuildWithoutTransition(const EdgeShapeInput& in, const Vec3& startDir) {
    const EdgeShapeResult generated = GenerateEdgeCurve(in.start, in.end, in.options);
    if (!generated.ok) return Fail(generated.error);
    EdgeShapeBuild result;
    result.curve = generated.curve;
    result.spline = SampleAlignment(result.curve, in.start.pos, startDir, in.start.up, in.cantA,
                                    in.cantB, in.sample, in.cantMid, in.cantTransitionRatio,
                                    in.cantTransitionShape, in.cantPoints);
    return result;
}

// 緩和あり: 水平形を3点フィレット(緩和曲線つき)で作り、縦を両端ピッチの3次エルミートで付ける。
EdgeShapeBuild BuildWithTransition(const EdgeShapeInput& in, const Vec3& startDir,
                                   const Vec3& endDir) {
    const float startHLen = std::hypot(startDir.x, startDir.z);
    const float endHLen = std::hypot(endDir.x, endDir.z);
    if (startHLen < kMinHorizontalDirLen || endHLen < kMinHorizontalDirLen)
        return Fail("端点の向きがほぼ鉛直");

    // 水平の問題として解く(高さをそろえる。縦はエルミートだけで付ける)。
    const Vec3 p0(in.start.pos.x, 0.0f, in.start.pos.z);
    const Vec3 p1(in.end.pos.x, 0.0f, in.end.pos.z);
    TransitionSpec spec;
    spec.shape = in.transitionShape;
    spec.length = in.transitionLength;
    ThreePointArcSolveResult sol;
    Vec3 startTangentH;
    if (!TryRefitThreePointArc(p0, startDir, 1.0f, p1, endDir, spec, sol, startTangentH))
        return Fail("緩和曲線つきの水平形を解けない");

    EdgeShapeBuild result;
    result.curve = sol.curve;
    result.effectiveTransitionLength = sol.transLen;
    std::vector<TrackNodeSample> horizontal = SampleAlignment(
        result.curve, p0, startTangentH, Vec3(0.0f, 1.0f, 0.0f), in.cantA, in.cantB, in.sample,
        in.cantMid, in.cantTransitionRatio, in.cantTransitionShape, in.cantPoints);
    if (horizontal.size() < 2) return Fail("水平形をサンプルできない");

    const double lengthH = static_cast<double>(horizontal.back().s);
    if (lengthH < rs::core::kTrackPositionEpsM) return Fail("水平弧長がゼロ");
    const double yA = in.start.pos.y, yB = in.end.pos.y;
    const double slopeA = static_cast<double>(startDir.y) / startHLen;
    const double slopeB = static_cast<double>(endDir.y) / endHLen;

    result.spline.reserve(horizontal.size());
    double s3d = 0.0;
    for (size_t i = 0; i < horizontal.size(); ++i) {
        const TrackNodeSample& h = horizontal[i];
        const double u = std::clamp(static_cast<double>(h.s) / lengthH, 0.0, 1.0);
        double h00, h10, h01, h11, d00, d10, d01, d11;
        HermiteBasis(u, h00, h10, h01, h11);
        HermiteBasisDeriv(u, d00, d10, d01, d11);
        const double y = yA * h00 + lengthH * slopeA * h10 + yB * h01 + lengthH * slopeB * h11;
        const double dyds =
            (yA * d00 + lengthH * slopeA * d10 + yB * d01 + lengthH * slopeB * d11) / lengthH;

        TrackNodeSample smp = h;
        smp.pos = Vec3(h.pos.x, static_cast<float>(y), h.pos.z);
        smp.tangent = Vec3(h.tangent.x, static_cast<float>(dyds), h.tangent.z).Normalized();
        smp.up = ComposeCantUp(smp.tangent, h.cant);
        if (i > 0) s3d += (smp.pos - result.spline.back().pos).Length();
        smp.s = static_cast<float>(s3d);
        result.spline.push_back(smp);
    }
    return result;
}


EdgeShapeBuild BuildEdgeShapeWithCant(const EdgeShapeInput& in) {
    if (!IsFiniteVec(in.start.pos) || !IsFiniteVec(in.start.dir) || !IsFiniteVec(in.end.pos) ||
        !IsFiniteVec(in.end.dir))
        return Fail("端点姿勢にNaN/無限大がある");
    const Vec3 startDir = in.start.dir.Normalized();
    const Vec3 endDir = in.end.dir.Normalized();
    if (startDir.Length() < 0.5f || endDir.Length() < 0.5f) return Fail("端点の向きがゼロベクトル");

    EdgeShapeInput normalized = in;
    normalized.start.dir = startDir;
    normalized.end.dir = endDir;
    if (normalized.start.up.Length() < rs::core::kDegenerateVectorEps) normalized.start.up = Vec3(0.0f, 1.0f, 0.0f);

    EdgeShapeBuild built = in.transitionLength > 0.0f
                               ? BuildWithTransition(normalized, startDir, endDir)
                               : BuildWithoutTransition(normalized, startDir);
    if (built.curve.empty() || built.spline.empty()) {
        if (built.error.empty()) built.error = "curve/splineを作れない";
        built.ok = false;
        built.curve.clear();
        built.spline.clear();
        return built;
    }
    const double posTolM = in.transitionLength > 0.0f ? in.options.transitionEndPositionToleranceM
                                                      : in.options.endPositionToleranceM;
    const std::string closeError =
        CloseEdgeEnd(built.spline, normalized.end, posTolM, in.options.endDirectionToleranceDeg);
    if (!closeError.empty()) return Fail(closeError);
    built.ok = true;
    return built;
}

}  // namespace

EdgeShapeBuild BuildEdgeShape(const EdgeShapeInput& in) {
    if (!in.cantAuto) return BuildEdgeShapeWithCant(in);
    //カントは curve を変えないので、カント0で一度作って曲がる向きを読み、自動値を決めて作り直す。
    EdgeShapeInput probe = in;
    probe.cantPoints.clear();
    probe.cantA = probe.cantB = probe.cantMid = probe.cantTransitionRatio = 0.0f;
    EdgeShapeBuild probed = BuildEdgeShapeWithCant(probe);
    if (!probed.ok) return probed;
    const AutoCantValues values = ResolveAutoCant(probed.curve, in.cantAutoMax,
                                                  in.autoNeighborPlateauA, in.autoNeighborPlateauB);
    EdgeShapeInput resolved = in;
    resolved.cantPoints.clear();  // 自動モードでは制御点を使わない
    resolved.cantA = values.cantA;
    resolved.cantB = values.cantB;
    resolved.cantMid = values.cantMid;
    resolved.cantTransitionRatio = values.cantTransitionRatio;
    return BuildEdgeShapeWithCant(resolved);
}


EdgeShapeInput MakeEdgeShapeInput(const TrackEdge& edge, const TrackNode& nodeA,
                                  const TrackNode& nodeB, const AlignmentSampleOptions& opt,
                                  const TrackGraph* graph) {
    EdgeShapeInput in;
    in.start = {nodeA.pos, (nodeA.tangent * edge.nodeATangentSign).Normalized(), nodeA.up};
    in.end = {nodeB.pos, (nodeB.tangent * edge.nodeBTangentSign).Normalized(), nodeB.up};
    in.cantA = edge.cantA;
    in.cantB = edge.cantB;
    in.cantMid = edge.cantMid;
    in.cantTransitionRatio = edge.cantTransitionRatio;
    in.cantTransitionShape = edge.cantTransitionShape;
    in.cantPoints = edge.cantPoints;
    //自動モードでは上の保存値は使われない(BuildEdgeShape が AutoCant で決める)。
    in.cantAuto = edge.cantAuto;
    in.cantAutoMax = edge.cantAutoMax;
    if (edge.cantAuto && graph != nullptr) {
        in.autoNeighborPlateauA = NeighborAutoPlateau(*graph, edge, 0);
        in.autoNeighborPlateauB = NeighborAutoPlateau(*graph, edge, 1);
    }
    // 緩和曲線の指定は refit が有効なedgeだけ(分割で無効にした半分は指定なしとして作る)。
    in.transitionShape = edge.refit.shape;
    in.transitionLength = edge.refit.available ? edge.refit.transitionLength : 0.0f;
    in.sample = opt;
    return in;
}

Vec3 PitchedTangent(const Vec3& horizontalTangent, float pitchRad) {
    const float cp = std::cos(pitchRad);
    return Vec3(horizontalTangent.x * cp, std::sin(pitchRad), horizontalTangent.z * cp);
}

float TangentPitchRad(const Vec3& tangent) {
    const float len = tangent.Length();
    if (len < rs::core::kTrackPositionEpsM) return 0.0f;
    return std::asin(std::clamp(tangent.y / len, -1.0f, 1.0f));
}

float ChordPitchRad(float deltaY, float horizontalLength) {
    if (horizontalLength <= 0.0f) return 0.0f;
    return std::atan2(deltaY, horizontalLength);
}

}  // namespace rs::world
