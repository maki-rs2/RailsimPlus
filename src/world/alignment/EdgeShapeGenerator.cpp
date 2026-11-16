#include "EdgeShapeGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../../core/Epsilon.h"
#include "../../core/Math.h"
#include "Alignment.h"

namespace rs::world {

using rs::core::Vec3;

namespace {

constexpr float kChordEpsM = rs::core::kTrackPositionEpsM;    // 重複点を読み飛ばす弦長

// ---- CalcSplit 用の double ベクトル演算(型と基本演算は core にある) ----
using rs::core::DVec3;

DVec3 ToD3(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 ToVec3(const DVec3& v) {
    return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}
DVec3 operator+(const DVec3& a, const DVec3& b) { return rs::core::Add(a, b); }
DVec3 operator-(const DVec3& a, const DVec3& b) { return rs::core::Sub(a, b); }
DVec3 operator-(const DVec3& a) { return {-a.x, -a.y, -a.z}; }
DVec3 operator*(const DVec3& a, double s) { return rs::core::Scale(a, s); }
// 長さが極小(1e-300以下)のときは零ベクトルを返す(core::Normalizeは0以下だけ)。
DVec3 Norm(const DVec3& a) {
    const double l = rs::core::Length(a);
    return l > rs::core::kDirectionUnderflowEps ? a * (1.0 / l) : DVec3{};
}
bool IsFinite(const DVec3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// 2直線(点p+方向n)の最近点。ほぼ平行なら true(RailSim II の LineLineNearest と同じ判定)。
bool LineLineNearest(DVec3& m1, DVec3& m2, const DVec3& p1, const DVec3& n1, const DVec3& p2, const DVec3& n2) {
    const DVec3 r = p2 - p1;
    const double n12 = Dot(n1, n2);
    const double t = 1.0 - n12 * n12;
    if (t < rs::core::kDirectionParallelEps) return true;
    m1 = p1 + n1 * (Dot(n1, r - n2 * Dot(n2, r)) / t);
    m2 = p2 + n2 * (Dot(n2, n1 * Dot(n1, r) - r) / t);
    return false;
}

// CalcSplit 1回分の結果。leaf=true なら p1→p2 を1本の円弧にしてよい。
// leaf=false なら splitPos/splitDir で2つに分けて再帰する。valid=false は解けない。
struct SplitStep {
    bool valid = true;
    bool leaf = false;
    DVec3 splitPos;
    DVec3 splitDir;
};

// 補正分割(両端の接線が同じ側へ開いていて円弧が作れない場合)の分割点と向き。
SplitStep CompensatedSplit(const DVec3& pos1, const DVec3& dir1, const DVec3& pos2, const DVec3& dir2,
                           double segLen, const DVec3& chordDir) {
    SplitStep step;
    DVec3 to1, to2;
    const double dot1 = Dot(chordDir, dir1);
    const double dot2 = Dot(chordDir, dir2);
    if (std::fabs(dot1) < std::fabs(dot2)) {
        to1 = Norm(chordDir - dir1 * dot1);
        to2 = Norm(dir1 - dir2 * Dot(dir1, dir2));
        if (Dot(dir2, chordDir) < 0.0) to2 = -to2;
    } else {
        to2 = Norm(dir2 * dot2 - chordDir);
        to1 = Norm(dir1 * Dot(dir1, dir2) - dir2);
        if (Dot(to1, chordDir) < 0.0) to1 = -to1;
    }
    // 両端から to1/to2 方向へ同じ距離 r だけ進んだ2点の距離が 2r になる r を二分法で探す。
    constexpr double kBisectionTolM = 0.001;  // RailSim II と同じ収束判定
    constexpr int kMaxIterations = 200;
    double r = 0.25 * segLen, lowerBound = r, upperBound = r;
    bool converged = false;
    for (int i = 0; i < kMaxIterations; ++i) {
        const double dif = rs::core::Length((pos1 + to1 * r) - (pos2 + to2 * r)) - 2.0 * r;
        if (std::fabs(dif) < kBisectionTolM) {
            converged = true;
            break;
        }
        if (dif > 0.0) {
            if (r == upperBound) {
                r *= 2.0;
                upperBound = r;
            } else {
                lowerBound = r;
                r = 0.5 * (lowerBound + upperBound);
            }
        } else {
            upperBound = r;
            r = 0.5 * (upperBound + lowerBound);
        }
    }
    if (!converged) {
        step.valid = false;
        return step;
    }
    const DVec3 m1 = pos1 + to1 * r;
    const DVec3 m2 = pos2 + to2 * r;
    step.splitPos = (m1 + m2) * 0.5;
    const DVec3 out1 = step.splitPos - m1;
    const DVec3 out2 = step.splitPos - m2;
    const DVec3 s1 = Norm(Cross(Cross(dir1, to1), out1));
    const DVec3 s2 = Norm(Cross(Cross(dir2, to2), out2));
    step.splitDir = Norm(s1 + s2);
    return step;
}

// pos1→pos2 を、弦の長さを保つ左右対称の1本の円弧(両端の向きの二等分線が弦の向きになる)で
// 結んだときの終点と、実際の終点pos2との距離[m]。BuildCurveFromPoseChainが置く円弧と同じ形で、
// 向きの差が lineAngleEpsRad 未満なら直線(始点の向きに弦の長さだけ進む)として測る。
double SingleArcClosureError(const DVec3& pos1, const DVec3& dir1, const DVec3& pos2, const DVec3& dir2,
                             double segLen, double lineAngleEpsRad) {
    const double crossLen = rs::core::Length(Cross(dir1, dir2));
    const DVec3 reachDir = crossLen < lineAngleEpsRad ? dir1 : Norm(dir1 + dir2);
    return rs::core::Length(pos1 + reachDir * segLen - pos2);
}

// CRailCurve::CalcSplit の移植(double)。
SplitStep CalcSplit(const DVec3& pos1, const DVec3& dir1, const DVec3& pos2, const DVec3& dir2,
                    const EdgeShapeOptions& opt) {
    SplitStep step;
    DVec3 chordDir = pos2 - pos1;
    const double segLen = rs::core::Length(chordDir);
    if (!(segLen > rs::core::kZeroLengthEps)) {
        step.valid = false;  // 同一点: 向きが定まらず分割も進まない
        return step;
    }
    chordDir = chordDir * (1.0 / segLen);

    // 葉: 両端接線を弦の長さだけ伸ばした点が終点に十分近い。
    if (rs::core::Length(pos1 + dir1 * segLen - pos2) + rs::core::Length(pos1 + dir2 * segLen - pos2) < opt.leafToleranceM) {
        step.splitPos = (pos1 + pos2) * 0.5;
        step.splitDir = chordDir;
        // 左右対称の1本の円弧で結ぶと終点がずれる姿勢(片側だけ向きが弦からずれている)は、
        // 弦の中点で分けて短い区間にする。区間を半分にするたびにずれは小さくなる。
        step.leaf = segLen <= opt.maxSegmentLengthM &&
                    SingleArcClosureError(pos1, dir1, pos2, dir2, segLen, opt.lineAngleEpsRad) <
                        opt.leafClosureToleranceM;
        return step;
    }

    DVec3 m1, m2;
    double dif = 0.0;
    bool lineComp = false;  // RailSim II の LINECOMP へ進む(直線区間の切り出し判定)
    if (LineLineNearest(m1, m2, pos1, dir1, pos2, dir2)) {
        if (Dot(dir1, dir2) > 0.001) {
            const DVec3 mid = (pos1 + pos2) * 0.5;
            const double dot1 = Dot(mid - pos1, dir1);
            const double dot2 = Dot(mid - pos2, dir2);
            if (dot1 * dot2 <= 0.0) {
                const DVec3 bisector = Norm(dir1 + dir2);
                step.splitPos = mid;
                step.splitDir = Norm(-bisector + chordDir * (2.0 * Dot(bisector, chordDir)));
                return step;
            }
        } else {
            dif = Dot(dir1, pos2 - pos1);
            lineComp = true;
        }
    } else {
        const DVec3 cc = (m1 + m2) * 0.5;
        const DVec3 to1 = cc - pos1;
        const DVec3 to2 = cc - pos2;
        const double dot1 = Dot(to1, dir1);
        const double dot2 = Dot(to2, dir2);
        const double tl1 = rs::core::Length(to1);
        const double tl2 = rs::core::Length(to2);
        dif = tl1 - tl2;
        if (tl1 < 0.001 || tl2 < 0.001 || dot1 * dot2 >= 0.0)
            return CompensatedSplit(pos1, dir1, pos2, dir2, segLen, chordDir);
        if (dot1 < 0.0) dif = -dif;
        lineComp = true;
    }
    if (lineComp) {
        // 片側に直線が余っている: その直線分を先に切り出す。
        if (dif > 1.0) {
            step.splitPos = pos1 + dir1 * dif;
            step.splitDir = dir1;
            return step;
        }
        if (dif < -1.0) {
            step.splitPos = pos2 + dir2 * dif;
            step.splitDir = dir2;
            return step;
        }
    }

    // 対称な円弧: 弦と両端接線から分割点(円弧の中点)と向きを求める。
    const DVec3 mid = (pos1 + pos2) * 0.5;
    const double cosTheta = std::clamp(Dot(dir1, dir2), -1.0, 1.0);
    const double theta = std::acos(cosTheta);
    const double sinHalf = std::sin(0.5 * theta);
    if (!(std::fabs(sinHalf) > rs::core::kDirectionDivisionEps)) {
        step.valid = false;
        return step;
    }
    const double radius = 0.5 * segLen / sinHalf;
    DVec3 out = Norm(dir1 - dir2);
    const DVec3 chord = pos2 - pos1;
    if (Dot(chord, dir2) < 0.0) {
        step.splitPos = out * ((1.0 + std::cos(0.5 * theta)) * radius) + mid;
        out = Norm(-dir1 - dir2);
        const DVec3 vert = chord - out * Dot(out, chord);
        step.splitDir = out * (radius * (2.0 * std::numbers::pi - theta)) + vert * 2.0;
    } else {
        step.splitPos = out * ((1.0 - std::cos(0.5 * theta)) * radius) + mid;
        out = Norm(dir1 + dir2);
        const DVec3 vert = chord - out * Dot(out, chord);
        step.splitDir = out * (radius * theta) + vert * 2.0;
    }
    step.splitDir = Norm(step.splitDir);
    return step;
}

struct SplitContext {
    const EdgeShapeOptions& opt;
    std::vector<EdgeEndPose>& chain;  // 葉の終点姿勢を順に積む
    Vec3 up;
    std::string error;
};

bool SplitRecursive(SplitContext& ctx, const DVec3& pos1, const DVec3& dir1, const DVec3& pos2,
                    const DVec3& dir2, int depth) {
    if (depth > ctx.opt.maxDepth) {
        ctx.error = "分割の深さが上限を超えた";
        return false;
    }
    if (static_cast<int>(ctx.chain.size()) > ctx.opt.maxSegmentCount) {
        ctx.error = "区間数が上限を超えた";
        return false;
    }
    const SplitStep step = CalcSplit(pos1, dir1, pos2, dir2, ctx.opt);
    if (!step.valid) {
        ctx.error = "分割が解けない(同一点、または二分法が収束しない)";
        return false;
    }
    if (!IsFinite(step.splitPos) || !IsFinite(step.splitDir)) {
        ctx.error = "分割点が有限値にならない";
        return false;
    }
    if (step.leaf) {
        ctx.chain.push_back({ToVec3(pos2), ToVec3(dir2), ctx.up});
        return true;
    }
    return SplitRecursive(ctx, pos1, dir1, step.splitPos, step.splitDir, depth + 1) &&
           SplitRecursive(ctx, step.splitPos, step.splitDir, pos2, dir2, depth + 1);
}

bool IsFinitePose(const EdgeEndPose& p) {
    return IsFinite(ToD3(p.pos)) && IsFinite(ToD3(p.dir)) && IsFinite(ToD3(p.up));
}

EdgeShapeResult Fail(const std::string& message) {
    EdgeShapeResult result;
    result.ok = false;
    result.error = message;
    return result;
}

} // namespace

std::vector<AlignmentSegment> BuildCurveFromPoseChain(const std::vector<EdgeEndPose>& poses,
                                                          float lineAngleEpsRad) {
    std::vector<AlignmentSegment> out;
    if (poses.size() < 2) return out;

    Vec3 currentDir = poses[0].dir.Normalized();
    if (currentDir.Length() < rs::core::kDegenerateVectorEps) currentDir = Vec3(0, 0, 1);
    // 始点フレームは再構築時と同じ先頭姿勢の up から始める。world Y を仮定すると
    // axisTiltRad 算出の基底(right0/up0)が SampleAlignment 側と食い違い、円弧を跨ぐたびに
    // ずれが蓄積する(RailSim II 実データで最悪57cm)。
    Vec3 currentUp = poses[0].up.Normalized();
    if (currentUp.Length() < rs::core::kDegenerateVectorEps) currentUp = Vec3(0.0f, 1.0f, 0.0f);

    for (size_t i = 0; i + 1 < poses.size(); ++i) {
        const Vec3 p0 = poses[i].pos;
        const Vec3 p1 = poses[i + 1].pos;
        const float chordLen = (p1 - p0).Length();
        if (chordLen < kChordEpsM) continue;  // 重複点は読み飛ばす

        const Vec3 t0 = currentDir;
        Vec3 t1 = poses[i + 1].dir;
        if (t1.Length() < rs::core::kDegenerateVectorEps) t1 = t0;
        t1 = t1.Normalized();

        const Vec3 cross01 = Vec3::Cross(t0, t1);
        const float crossLen = cross01.Length();
        if (crossLen < lineAngleEpsRad) {
            // 直線: currentDir/currentUp は変わらない。
            AlignmentSegment seg;
            seg.kind = AlignmentSegment::Kind::Line;
            seg.length = chordLen;
            out.push_back(seg);
            continue;
        }

        const Vec3 axis = cross01.Normalized();
        const float theta = std::atan2(crossLen, Vec3::Dot(t0, t1));  // (0, pi)
        const float sinHalf = std::sin(0.5f * theta);
        if (std::fabs(sinHalf) < rs::core::kDirectionParallelEps) {
            // 直線の判定(lineAngleEpsRad)を僅かに超えるだけの偏角は、円弧の半径が定まらない。
            // 飛ばすと区間ごと消えるので、直線として弦の長さを残す。
            AlignmentSegment seg;
            seg.kind = AlignmentSegment::Kind::Line;
            seg.length = chordLen;
            out.push_back(seg);
            continue;
        }
        const float radius = chordLen / (2.0f * sinHalf);
        const float length = radius * theta;
        if (length < kChordEpsM) continue;

        const Vec3 right0 = ComputeRightVector(t0, currentUp);
        // SampleAlignment の基底規約(dir×right)に合わせる。
        const Vec3 up0 = Vec3::Cross(t0, right0).Normalized();
        const float axisTiltRad = std::atan2(Vec3::Dot(axis, right0), Vec3::Dot(axis, up0));

        AlignmentSegment seg;
        seg.kind = AlignmentSegment::Kind::Arc;
        seg.radius = radius;
        seg.length = length;
        seg.axisTiltRad = axisTiltRad;
        out.push_back(seg);

        // currentDir は入力の接線へそろえて誤差の蓄積を避け、currentUp は区間開始時の
        // 幾何アップを回転軸まわりに theta だけ回して引き継ぐ。
        currentDir = t1;
        currentUp = RotateAroundAxis(up0, axis, theta);
    }
    return out;
}

EdgeShapeResult GenerateEdgeCurve(const EdgeEndPose& start, const EdgeEndPose& end,
                                  const EdgeShapeOptions& opt) {
    if (!IsFinitePose(start) || !IsFinitePose(end)) return Fail("端点姿勢にNaN/無限大がある");
    const DVec3 startDir = Norm(ToD3(start.dir));
    const DVec3 endDir = Norm(ToD3(end.dir));
    if (rs::core::Length(startDir) < 0.5 || rs::core::Length(endDir) < 0.5) return Fail("端点の向きがゼロベクトル");

    EdgeEndPose startPose = start;
    startPose.dir = ToVec3(startDir);
    EdgeEndPose endPose = end;
    endPose.dir = ToVec3(endDir);
    if (startPose.up.Length() < rs::core::kDegenerateVectorEps) startPose.up = Vec3(0.0f, 1.0f, 0.0f);

    std::vector<EdgeEndPose> chain;
    chain.push_back(startPose);
    SplitContext ctx{opt, chain, startPose.up, {}};
    if (!SplitRecursive(ctx, ToD3(startPose.pos), ToD3(startPose.dir), ToD3(endPose.pos),
                        ToD3(endPose.dir), 0))
        return Fail(ctx.error);

    EdgeShapeResult result;
    result.curve = BuildCurveFromPoseChain(chain, opt.lineAngleEpsRad);
    if (result.curve.empty()) return Fail("生成した曲線が空");

    // 生成した curve を始点フレームから実際にサンプルし、終点が end に合うか確かめる。
    AlignmentSampleOptions sampleOpt;
    sampleOpt.sampleStep = 1.0e6f;  // 区間境界と終端だけで足りる
    const std::vector<TrackNodeSample> samples =
        SampleAlignment(result.curve, startPose.pos, startPose.dir, startPose.up, 0.0f, 0.0f, sampleOpt);
    if (samples.empty()) return Fail("生成した曲線をサンプルできない");
    const TrackNodeSample& last = samples.back();
    result.endPositionErrorM = rs::core::Length(ToD3(last.pos) - ToD3(endPose.pos));
    const double cosAngle = std::clamp(Dot(Norm(ToD3(last.tangent)), endDir), -1.0, 1.0);
    result.endDirectionErrorDeg = std::acos(cosAngle) * 180.0 / std::numbers::pi;

    const bool within = result.endPositionErrorM <= opt.endPositionToleranceM &&
                        result.endDirectionErrorDeg <= opt.endDirectionToleranceDeg;
    if (!std::isfinite(result.endPositionErrorM) || !within) {
        EdgeShapeResult failed = Fail("終点が端点姿勢と許容内で一致しない");
        failed.endPositionErrorM = result.endPositionErrorM;
        failed.endDirectionErrorDeg = result.endDirectionErrorDeg;
        return failed;
    }
    result.ok = true;
    return result;
}

} // namespace rs::world
