//
// ThreePointArcSolver.cpp - RailSim2流3点円弧（接線-接線フィレット）ソルバと
// 角度スナップ。双円弧(biarc)はBiarcSolver.cpp、複線オフセット/2接線交点は
// TangentOffsetGeometry.cppにある。共通宣言はGeometrySolvers.h。
//
#include "GeometrySolvers.h"

#include "../../core/Epsilon.h"
#include "EditorGeometryTuning.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rs::world {

using rs::core::Vec3;

namespace {
constexpr float kEditorPi = std::numbers::pi_v<float>;
// SolveLtForPhi: 上限を探す倍率の拡大回数の上限と、その倍率、二分法の反復回数。
constexpr int kBracketMaxSteps = 40;
constexpr float kBracketGrowth = 1.5f;
constexpr int kBisectionSteps = 40;
constexpr float kMinLegLen = 0.05f; // [m]
} // namespace

// ============================== 角度スナップ ==============================

float SnapThetaToStep(float thetaRad, float stepDeg) {
    float step = std::clamp(stepDeg, 1.0f, 90.0f) * kEditorPi / 180.0f;
    if (step < rs::core::kLineAngleEpsRad) return thetaRad;
    return std::round(thetaRad / step) * step;
}

namespace {

// 緩和曲線1本(曲率0→1/R, 長さLt)を挿入したときの偏角φt・移程量shift・前進量Xmと、
// 頂点Cからの接線長 T=(R+shift)·tan(|θ|/2)+Xm を返す。
struct TransFit {
    float phiT;
    float shift;
    float xm;
    float tangent;
};
TransFit EvalTransFit(rs::world::AlignmentSegment::TransitionShape shape, float radiusAbs,
                      float lt, float tanAbs) {
    rs::world::TransitionEndPose pose =
        rs::world::EvalTransitionEnd(shape, 1.0f / radiusAbs, lt);
    TransFit f;
    f.phiT = pose.theta;
    f.shift = pose.x - radiusAbs * (1.0f - std::cos(pose.theta));
    f.xm = pose.z - radiusAbs * std::sin(pose.theta);
    f.tangent = (radiusAbs + f.shift) * tanAbs + f.xm;
    return f;
}

// 固定半径Rで緩和曲線の偏角φtがtargetPhiになる緩和曲線長Ltを二分法で求める
// (φtはLtに対し単調増加)。
float SolveLtForPhi(rs::world::AlignmentSegment::TransitionShape shape, float radiusAbs,
                    float targetPhi) {
    float lo = 0.0f, hi = 2.0f * radiusAbs * targetPhi + 1.0f;
    for (int k = 0; k < kBracketMaxSteps; ++k) {
        if (rs::world::EvalTransitionEnd(shape, 1.0f / radiusAbs, hi).theta >= targetPhi) break;
        hi *= kBracketGrowth;
    }
    for (int i = 0; i < kBisectionSteps; ++i) {
        float mid = 0.5f * (lo + hi);
        float p = rs::world::EvalTransitionEnd(shape, 1.0f / radiusAbs, mid).theta;
        if (p < targetPhi) lo = mid; else hi = mid;
    }
    return 0.5f * (lo + hi);
}

// 緩和曲線の長さ・半径・偏角の解。
struct TransitionPlan {
    float radiusAbs;
    float phiT;
    float ltEff;
};

// 要求緩和長 ltReq から、実際に挿入する緩和曲線の半径・偏角・長さを決める。
TransitionPlan PlanTransition(const TransitionSpec& trans, const EditorGeometryTuning& tuning,
                              float d, float tanAbs, float absTheta, float ltReq) {
    float radiusAbs = d / tanAbs; // 緩和なし純円弧の半径(以降で緩和ありに更新)
    float phiT = 0.0f;            // 緩和曲線1本の偏角[rad]
    float ltEff = 0.0f;           // 実際に挿入する緩和曲線長(片側)

    if (ltReq >= kMinLegLen) {
        // 緩和曲線あり。まず「緩和曲線+0m円弧+緩和曲線」(左右対称スパイラルが旋回角を
        // 使い切り、円弧が消える)の解 (r0,lt0) を求める。これがこの3点形状で入る緩和
        // 曲線の幾何学的な最大長 lt0 になる。接線長 T = (R+ΔR)·tan(|θ|/2)+Xm はRに
        // 単調増加なので、arc=0条件 2φt=θ の下で T(R)=d となるRを二分法で解く。
        const float targetPhi = 0.5f * absTheta; // 各スパイラルが旋回角の半分を担う
        const float rPure = d / tanAbs;          // 純円弧の半径(=Rの上限)
        float r0 = 0.0f, lt0 = 0.0f;
        bool arcZeroOk = false;
        {
            float lo = tuning.minTransitionRadiusMeters;
            float hi = std::max(rPure, tuning.minTransitionRadiusMeters);
            float ltLo = SolveLtForPhi(trans.shape, lo, targetPhi);
            float tLo = EvalTransFit(trans.shape, lo, ltLo, tanAbs).tangent;
            if (tLo <= d) { // 最小半径でも接線長がd以内 → arc=0解が存在
                for (int i = 0; i < 40; ++i) {
                    float mid = 0.5f * (lo + hi);
                    float ltMid = SolveLtForPhi(trans.shape, mid, targetPhi);
                    float tMid = EvalTransFit(trans.shape, mid, ltMid, tanAbs).tangent;
                    if (tMid > d) hi = mid; else lo = mid;
                }
                r0 = 0.5f * (lo + hi);
                lt0 = SolveLtForPhi(trans.shape, r0, targetPhi);
                arcZeroOk = (r0 >= tuning.minTransitionRadiusMeters);
            }
        }

        if (arcZeroOk && ltReq >= lt0) {
            // 要求長が最大長以上: 円弧を残さず、緩和長を最大 lt0 に頭打ちする
            // (=緩和曲線+0m円弧+緩和曲線)。実効長 r.transLen で返す。
            radiusAbs = r0;
            ltEff = lt0;
            phiT = targetPhi;
        } else {
            // 要求長に従う: 固定 Lt=LtReq で接線長 T(solvedRadius)=d となる solvedRadius を不動点反復で解く
            // (円弧が正の長さで残る領域)。ΔR=移程量, Xm=前進量。収束判定は接線長残差
            // |T(solvedRadius)-d|(半径差分はfloat揺らぎが1/tan倍に増幅され小旋回角で収束しない)。
            float solvedRadius = d / tanAbs;
            bool converged = false;
            for (int it = 0; it < 24; ++it) {
                if (solvedRadius < tuning.minTransitionRadiusMeters) break;
                TransFit f = EvalTransFit(trans.shape, solvedRadius, ltReq, tanAbs);
                phiT = f.phiT;
                if (std::fabs(f.tangent - d) < tuning.tangentResidualToleranceMeters) {
                    converged = true;
                    break;
                }
                solvedRadius = (d - f.xm) / tanAbs - f.shift;
            }
            if (converged && solvedRadius >= tuning.minTransitionRadiusMeters && 2.0f * phiT <= absTheta) {
                radiusAbs = solvedRadius;
                ltEff = ltReq;
            } else if (arcZeroOk) {
                // 不動点が解けない/円弧が負になる → arc=0解へフォールバック。
                radiusAbs = r0;
                ltEff = lt0;
                phiT = targetPhi;
            } else {
                // 緩和曲線を入れる余地なし → 単円フィレットにフォールバック。
                radiusAbs = d / tanAbs;
                ltEff = 0.0f;
                phiT = 0.0f;
            }
        }
    }
    return TransitionPlan{radiusAbs, phiT, ltEff};
}

// 解いた寸法から Line/Transition/Arc/Transition/Line の並びを組み立てる。
void AppendCurveSegments(ThreePointArcSolveResult& r, const TransitionSpec& trans, bool useTrans,
                         float ltEff, float radius, float arcLen) {
    if (r.preLineLen >= kMinLegLen) {
        rs::world::AlignmentSegment seg;
        seg.kind = rs::world::AlignmentSegment::Kind::Line;
        seg.length = r.preLineLen;
        r.curve.push_back(seg);
    }
    if (useTrans) {
        rs::world::AlignmentSegment seg;
        seg.kind = rs::world::AlignmentSegment::Kind::Transition;
        seg.shape = trans.shape;
        seg.length = ltEff;
        seg.radius = 0.0f;       // 曲率0(直線側)から
        seg.radiusEnd = radius;  // 円弧曲率へ
        r.curve.push_back(seg);
    }
    if (arcLen >= kMinLegLen) {
        // arc=0(緩和+0円弧+緩和)のときは円弧セグメントを省く。前後の緩和曲線は
        // 曲率1/Rで直接接続し(0→1/R と 1/R→0)、円弧を挟まずG1連続になる。
        rs::world::AlignmentSegment seg;
        seg.kind = rs::world::AlignmentSegment::Kind::Arc;
        seg.radius = radius;
        seg.length = arcLen;
        r.curve.push_back(seg);
    }
    if (useTrans) {
        rs::world::AlignmentSegment seg;
        seg.kind = rs::world::AlignmentSegment::Kind::Transition;
        seg.shape = trans.shape;
        seg.length = ltEff;
        seg.radius = radius;     // 円弧曲率から
        seg.radiusEnd = 0.0f;    // 曲率0(直線側)へ
        r.curve.push_back(seg);
    }
    if (r.postLineLen >= kMinLegLen) {
        rs::world::AlignmentSegment seg;
        seg.kind = rs::world::AlignmentSegment::Kind::Line;
        seg.length = r.postLineLen;
        r.curve.push_back(seg);
    }
}

} // namespace

// ============================== RailSim2流3点円弧ソルバ ==============================
//
// P0(始点)/C(中間点=頂点)/P1(終点) から、始点側補助線 P0-C・終点側補助線
// C-P1 を2辺とする、短い方の辺長 d=min(a,b) を等辺とした二等辺三角形の
// フィレット円弧（+前後の直線）を解く。
//   t0=normalize(C-P0), t1=normalize(P1-C)
//   theta = t0からt1への符号付き角(旋回角)
//   R = d/tan(|theta|/2) (符号はthetaの向きに一致: 既存規約でlat>0側(+right)
//       へ曲がるときradius>0)
//   curve: [Line(a-d)] + [Arc(R,|R*theta|)] + [Line(b-d)] （前後直線は
//   a-d/b-d が0.05m以上のときのみ挿入。|theta|<0.5度は全長直線1本）
ThreePointArcSolveResult SolveThreePointArc(const Vec3& p0, const Vec3& c, const Vec3& p1,
                                            const TransitionSpec& trans) {
    const EditorGeometryTuning& tuning = EditorGeometryTuning::Embedded();
    ThreePointArcSolveResult r;

    Vec3 up(0, 1, 0);

    Vec3 aVec(c.x - p0.x, 0.0f, c.z - p0.z);
    float aLen = aVec.Length();
    Vec3 bVec(p1.x - c.x, 0.0f, p1.z - c.z);
    float bLen = bVec.Length();

    r.aLen = aLen;
    r.bLen = bLen;

    if (aLen < kMinLegLen || bLen < kMinLegLen) {
        r.rejectReason = (aLen < kMinLegLen) ? "start leg too short" : "end leg too short";
        return r;
    }

    Vec3 t0 = aVec * (1.0f / aLen);
    Vec3 t1 = bVec * (1.0f / bLen);

    Vec3 right0 = Vec3::Cross(up, t0).Normalized();
    float theta = std::atan2(Vec3::Dot(t1, right0), Vec3::Dot(t1, t0));

    r.valid = true;
    r.theta = theta;

    float d = std::min(aLen, bLen);
    r.d = d;
    r.preLineLen = std::max(aLen - d, 0.0f);
    r.postLineLen = std::max(bLen - d, 0.0f);

    constexpr float kMinThetaRad = 0.5f * kEditorPi / 180.0f; // 約0.5度
    if (std::fabs(theta) < kMinThetaRad) {
        // ほぼ直線: 全長(P0->P1、水平)を直線1本とする。
        r.isStraightFallback = true;
        r.radius = 0.0f;
        Vec3 chord(p1.x - p0.x, 0.0f, p1.z - p0.z);
        float totalLen = chord.Length();
        r.endTangent = t0;
        if (totalLen < tuning.minAcceptableLengthMeters) {
            r.rejectReason = "length<0.5m";
            return r;
        }
        rs::world::AlignmentSegment seg;
        seg.kind = rs::world::AlignmentSegment::Kind::Line;
        seg.length = totalLen;
        r.curve.push_back(seg);
        r.acceptable = true;
        return r;
    }

    if (std::fabs(theta) >= tuning.maxTurnAngleRadians) {
        r.rejectReason = "theta>=175deg";
        return r;
    }

    float halfTheta = theta * 0.5f;
    float tanHalf = std::tan(halfTheta);
    if (std::fabs(tanHalf) < rs::core::kDirectionParallelEps) {
        r.rejectReason = "degenerate theta";
        return r;
    }
    const float sgn = (theta >= 0.0f) ? 1.0f : -1.0f;
    const float tanAbs = std::fabs(tanHalf);
    const float absTheta = std::fabs(theta);
    const float ltReq = std::max(trans.length, 0.0f); // 要求された緩和曲線長(片側)

    const TransitionPlan plan = PlanTransition(trans, tuning, d, tanAbs, absTheta, ltReq);
    const float radiusAbs = plan.radiusAbs;
    const float phiT = plan.phiT;
    const float ltEff = plan.ltEff;

    const bool useTrans = (ltEff >= kMinLegLen);
    r.transLen = useTrans ? ltEff : 0.0f;

    float radius = sgn * radiusAbs;
    r.radius = radius;

    const float minRadius = useTrans ? tuning.minTransitionRadiusMeters : tuning.minArcRadiusMeters;
    if (radiusAbs < minRadius) {
        r.rejectReason = "radius<minArcRadius";
        return r;
    }

    float arcLen = radiusAbs * (absTheta - 2.0f * phiT);
    if (arcLen < 0.0f) arcLen = 0.0f; // arc=0(緩和+0円弧+緩和)のとき数値誤差で負になることがある
    float totalLen = r.preLineLen + 2.0f * (useTrans ? ltEff : 0.0f) + arcLen + r.postLineLen;
    if (totalLen < tuning.minAcceptableLengthMeters) {
        r.rejectReason = "length<0.5m";
        return r;
    }

    r.endTangent = t1;

    AppendCurveSegments(r, trans, useTrans, ltEff, radius, arcLen);

    r.acceptable = true;
    return r;
}

} // namespace rs::world
