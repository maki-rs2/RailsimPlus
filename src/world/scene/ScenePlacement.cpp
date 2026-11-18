//
// ScenePlacement.cpp - ScenePlacement -> ワールド行列 解決
//
#include "ScenePlacement.h"
#include "../../core/Epsilon.h"
#include "../alignment/Alignment.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "../alignment/TrackGraph.h"
#include "../alignment/TrackTraversal.h"

namespace rs::world {

namespace {

using rs::core::Mat4;
using rs::core::Vec3;

// 剛体基底(right,up,forward)+原点からワールド行列を組む(row-major, v'=v*M)。
// src/plugins/load/TrainAssembly.cppのComposeRigidと同じ規約(このファイル用に複製)。
Mat4 ComposeRigid(const Vec3& origin, const Vec3& forward, const Vec3& up) {
    const Vec3 right = Vec3::Cross(up, forward);
    Mat4 m = Mat4::Identity();
    m.m[0] = right.x;
    m.m[1] = right.y;
    m.m[2] = right.z;
    m.m[4] = up.x;
    m.m[5] = up.y;
    m.m[6] = up.z;
    m.m[8] = forward.x;
    m.m[9] = forward.y;
    m.m[10] = forward.z;
    m.m[12] = origin.x;
    m.m[13] = origin.y;
    m.m[14] = origin.z;
    return m;
}

} // namespace

Vec3 HorizontalForward(const Vec3& v) {
    const float length = v.Length();
    if (length < rs::core::kDegenerateVectorEps) return {0.0f, 0.0f, 1.0f};
    const Vec3 horizontal{v.x / length, 0.0f, v.z / length};
    if (horizontal.Length() < rs::core::kHorizontalDirectionEps) return {0.0f, 0.0f, 1.0f};
    return horizontal.Normalized();
}

float HeadingDeg(const Vec3& v) {
    const Vec3 forward = HorizontalForward(v);
    return std::atan2(forward.x, forward.z) * 180.0f / std::numbers::pi_v<float>;
}

UprightFrame ResolveUprightFrame(const Vec3& tangent, float yawOffsetDeg) {
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const float yawRad = yawOffsetDeg * std::numbers::pi_v<float> / 180.0f;
    UprightFrame frame;
    frame.up = up;
    frame.forward = RotateAroundAxis(HorizontalForward(tangent), up, yawRad).Normalized();
    frame.right = Vec3::Cross(up, frame.forward).Normalized();
    return frame;
}

Mat4 ResolveWorldMatrix(const ScenePlacement& placement, const TrackGraph& graph) {
    if (const TrackPlacement* track = std::get_if<TrackPlacement>(&placement)) {
        const auto it = graph.edges.find(track->position.edge);
        if (it == graph.edges.end()) return Mat4::Identity();

        const TrackFrame frame = EvaluateAt(it->second, track->position.s, track->position.direction);
        const float yawRad = track->yawOffsetDeg * std::numbers::pi_v<float> / 180.0f;
        const Vec3 forward = RotateAroundAxis(frame.tangent, frame.up, yawRad).Normalized();
        // 横オフセットは軌道の右方向で測る(取り込み・ドラッグの射影と同じ基準)。yawは
        // モデルの姿勢だけを回し、原点を軌道の反対側へ鏡映させない。
        const Vec3 trackRight = Vec3::Cross(frame.up, frame.tangent).Normalized();
        const Vec3 origin =
            frame.pos + trackRight * track->lateralOffsetM + frame.up * track->verticalOffsetM;

        if (track->keepUpright) {
            // 位置(origin)はカント適用済みframe.upで計算済み。姿勢は水平な前とワールドの
            // 上だけで組み、勾配やカントのロール・ピッチをモデルへ伝えない。
            const UprightFrame upright = ResolveUprightFrame(frame.tangent, track->yawOffsetDeg);
            return ComposeRigid(origin, upright.forward, upright.up);
        }

        return ComposeRigid(origin, forward, frame.up);
    }

    const FreePlacement& free = std::get<FreePlacement>(placement);
    // 内因性yaw->pitch->rollでright/up/forwardの基底全体を段階的に回転する
    // (FreePlacementのコメント参照)。upを固定するとpitch/rollを正しく反映できない。
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};

    const float yawRad = free.yawDeg * std::numbers::pi_v<float> / 180.0f;
    forward = RotateAroundAxis(forward, up, yawRad).Normalized();
    right = RotateAroundAxis(right, up, yawRad).Normalized();

    const float pitchRad = free.pitchDeg * std::numbers::pi_v<float> / 180.0f;
    forward = RotateAroundAxis(forward, right, pitchRad).Normalized();
    up = RotateAroundAxis(up, right, pitchRad).Normalized();

    const float rollRad = free.rollDeg * std::numbers::pi_v<float> / 180.0f;
    up = RotateAroundAxis(up, forward, rollRad).Normalized();
    right = RotateAroundAxis(right, forward, rollRad).Normalized();

    return ComposeRigid(free.position, forward, up);
}

FreePlacement DecomposeFreePlacement(const Vec3& pos, const Vec3& dir, const Vec3& up) {
    const Vec3 forward = dir.Normalized();
    // Gram-Schmidt: forwardに直交するupへ補正する(入力が厳密な直交でなくてもよい)。
    Vec3 upTarget = (up - forward * Vec3::Dot(forward, up)).Normalized();
    if (upTarget.Length() < rs::core::kDegenerateVectorEps) upTarget = {0.0f, 1.0f, 0.0f};  // 縮退時の既定

    // yaw: forwardの水平(XZ平面)方位角。ResolveWorldMatrixのyaw合成手順で
    // forward1=(sin(yaw),0,cos(yaw))となることの逆算。dirがワールドupと
    // ほぼ平行(真上/真下向き)なら forward.x/forward.z が共に0に近く、atan2(0,0)=0の
    // 既定によりyaw=0とみなす(縮退時は任意のyawで等価)。
    const float yawRad = std::atan2(forward.x, forward.z);

    // pitch: forward2.y = -sin(pitch) (閉形式)。
    const float pitchRad = std::asin(std::clamp(-forward.y, -1.0f, 1.0f));

    // yaw,pitch適用後のup(up2)を閉形式で再現し、up2→upTargetへ回すrollを逆算する。
    const float sy = std::sin(yawRad), cy = std::cos(yawRad);
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);
    const Vec3 up2(sy * sp, cp, cy * sp);

    const float cosRoll = std::clamp(Vec3::Dot(up2, upTarget), -1.0f, 1.0f);
    const float sinRoll = Vec3::Dot(Vec3::Cross(up2, upTarget), forward);
    const float rollRad = std::atan2(sinRoll, cosRoll);

    FreePlacement result;
    result.position = pos;
    result.yawDeg = yawRad * 180.0f / std::numbers::pi_v<float>;
    result.pitchDeg = pitchRad * 180.0f / std::numbers::pi_v<float>;
    result.rollDeg = rollRad * 180.0f / std::numbers::pi_v<float>;
    return result;
}

} // namespace rs::world
