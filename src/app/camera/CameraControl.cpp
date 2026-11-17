//
// CameraControl.cpp - CameraController の実装
//
#include "CameraControl.h"

#include "CameraTuning.h"

#include <algorithm>
#include <cmath>

namespace rs::app {

namespace {
// 左・中ボタンを押してから、これだけ(px)動いたらクリックでなくドラッグとして基準点の移動を始める。
constexpr int kMoveDragStartPx = 4;

// 水平面内の画面の前方と右。移動はピッチに影響しない(マップエディタ流)。
// eyePos は target + distance*(sinYaw,·,cosYaw) でカメラが+yaw側に立つため、
// 画面の前方(視線方向)はその逆向き -(sin,cos) になる。
struct HorizontalAxes {
    float forward[3];
    float right[3];
};

HorizontalAxes HorizontalAxesFromYaw(float yaw) {
    HorizontalAxes axes;
    axes.forward[0] = -std::sin(yaw);
    axes.forward[1] = 0.0f;
    axes.forward[2] = -std::cos(yaw);
    axes.right[0] = axes.forward[2];
    axes.right[1] = 0.0f;
    axes.right[2] = -axes.forward[0];
    return axes;
}
}  // namespace

void CameraController::FitToAabb(const float mn[3], const float mx[3]) {
    CancelFlyTo();
    fly.FrameAabb(mn, mx);
}

void CameraController::ResetActive() {
    CancelFlyTo();
    fly = FlyCamera{};
}

void CameraController::FlyTo(const rs::core::Vec3& target) {
    const CameraTuning& tuning = CameraTuning::Embedded();
    flyToFrom_[0] = fly.target[0];
    flyToFrom_[1] = fly.target[1];
    flyToFrom_[2] = fly.target[2];
    flyToGoal_[0] = target.x;
    flyToGoal_[1] = target.y;
    flyToGoal_[2] = target.z;
    const double dx = static_cast<double>(flyToGoal_[0]) - static_cast<double>(flyToFrom_[0]);
    const double dy = static_cast<double>(flyToGoal_[1]) - static_cast<double>(flyToFrom_[1]);
    const double dz = static_cast<double>(flyToGoal_[2]) - static_cast<double>(flyToFrom_[2]);
    const double distanceMeters = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double seconds = static_cast<double>(tuning.flyToMinSeconds) +
                           distanceMeters / static_cast<double>(tuning.flyToMetersPerSecond);
    flyToDurationSeconds_ = static_cast<float>(
        std::clamp(seconds, static_cast<double>(tuning.flyToMinSeconds), static_cast<double>(tuning.flyToMaxSeconds)));
    flyToElapsedSeconds_ = 0.0f;
    flyToActive_ = true;
}

void CameraController::BeginFollow(const rs::world::TrainId& trainId, const rs::core::Vec3& anchor) {
    FlyTo(anchor);
    followTrainId_ = trainId;
}

void CameraController::UpdateFollow(const rs::core::Vec3& anchor) {
    if (!IsFollowing()) return;
    if (flyToActive_) {
        // 寄っている間は、動き続ける追跡先へ向けて目標を取り直す。
        flyToGoal_[0] = anchor.x;
        flyToGoal_[1] = anchor.y;
        flyToGoal_[2] = anchor.z;
        return;
    }
    fly.target[0] = anchor.x;
    fly.target[1] = anchor.y;
    fly.target[2] = anchor.z;
}

void CameraController::ClampTargetToSurface(const rs::world::SurfaceBounds& bounds) {
    const auto clampPoint = [&bounds](float* point) {
        rs::core::Vec3 pos(point[0], point[1], point[2]);
        rs::world::ClipToSurfaceBounds(bounds, pos);
        point[0] = pos.x;
        point[2] = pos.z;
    };
    clampPoint(fly.target);
    if (flyToActive_) clampPoint(flyToGoal_);
}

void CameraController::Ease(float dt) {
    dt = std::min(dt, 0.1f);
    const CameraTuning& tuning = CameraTuning::Embedded();
    auto easeFactor = [](float rate, float dtClamped) {
        return 1.0f - std::exp(-rate * dtClamped);
    };
    const float rotF = easeFactor(tuning.rotationRate, dt);
    const float zoomF = easeFactor(tuning.zoomRate, dt);
    const float moveF = easeFactor(tuning.moveRate, dt);
    const float fovF = easeFactor(tuning.fovRate, dt);

    // FlyTo の移動は、fly.target を補間値へ進める(描画側の追従は下の指数のまま通す)。
    if (flyToActive_) {
        flyToElapsedSeconds_ += dt;
        const float progress = std::min(flyToElapsedSeconds_ / flyToDurationSeconds_, 1.0f);
        const float eased = progress * progress * (3.0f - 2.0f * progress); // smoothstep
        for (int i = 0; i < 3; ++i) {
            fly.target[i] = std::lerp(flyToFrom_[i], flyToGoal_[i], eased);
        }
        if (progress >= 1.0f) flyToActive_ = false;
    }

    smoothed_.yaw += (fly.yaw - smoothed_.yaw) * rotF;
    smoothed_.pitch += (fly.pitch - smoothed_.pitch) * rotF;
    smoothed_.roll = fly.roll;
    smoothed_.distance += (fly.distance - smoothed_.distance) * zoomF;
    for (int i = 0; i < 3; ++i) {
        smoothed_.target[i] += (fly.target[i] - smoothed_.target[i]) * moveF;
    }
    smoothed_.fieldOfView += (fly.fieldOfView - smoothed_.fieldOfView) * fovF;
}

void CameraController::SnapToTarget() {
    CancelFlyTo();
    smoothed_ = fly;
}

void CameraController::OnRightDown(int x, int y) {
    // 右ドラッグ = 視点回転。
    rotating_ = true;
    lastDragPos_.x = x;
    lastDragPos_.y = y;
}

void CameraController::OnRightUp() { rotating_ = false; }

void CameraController::BeginMoveDrag(rs::app::input::PointerButton button, int x, int y, bool vertical) {
    // 押した直後は「待ち」にとどめ、しきい値を超えて動いたときに初めて移動を始める
    // (敷設ツールのShift+クリックでカメラの高さが動かないようにするため)。
    moveDrag_ = MoveDragPhase::Pending;
    moveDragButton_ = button;
    moveDragVertical_ = vertical;
    lastDragPos_.x = x;
    lastDragPos_.y = y;
}

bool CameraController::EndMoveDrag(rs::app::input::PointerButton button) {
    // 待ちのまま離した(クリック)場合も、SetCaptureしているのでtrueを返す。
    if (moveDrag_ == MoveDragPhase::None || moveDragButton_ != button) return false;
    moveDrag_ = MoveDragPhase::None;
    return true;
}

bool CameraController::OnLeftDown(int x, int y, bool shiftDown) {
    // Shift+左ドラッグ = 垂直(高さ調節)。Shift非押下は、水平の移動に使う設定のときだけ受け付ける。
    // どちらでもない左クリックはエディタ操作等に譲るため、ここでは何も始めない。
    if (!shiftDown && !leftDragMovesTarget) return false;
    BeginMoveDrag(rs::app::input::PointerButton::Left, x, y, shiftDown);
    return true;
}

bool CameraController::OnLeftUp() { return EndMoveDrag(rs::app::input::PointerButton::Left); }

bool CameraController::OnMiddleDown(int x, int y, bool shiftDown) {
    if (!middleDragMovesTarget) return false;
    BeginMoveDrag(rs::app::input::PointerButton::Middle, x, y, shiftDown);
    return true;
}

bool CameraController::OnMiddleUp() { return EndMoveDrag(rs::app::input::PointerButton::Middle); }

void CameraController::ApplyMoveDrag(float dx, float dy, bool fastDown) {
    const CameraTuning& tuning = CameraTuning::Embedded();
    const float scale = std::max(fly.distance, tuning.viewDistMoveMinMeters) * tuning.viewDragPerPixel *
                        (fastDown ? tuning.viewDragFastMultiplier : 1.0f);
    if (moveDragVertical_) {
        // dxは無視し、上ドラッグで上昇する。
        fly.target[1] -= dy * scale;
        return;
    }
    // 右へドラッグすると注目点が画面の右へ、上へドラッグすると前へ動く。
    const HorizontalAxes axes = HorizontalAxesFromYaw(fly.yaw);
    fly.target[0] += (axes.right[0] * dx - axes.forward[0] * dy) * scale;
    fly.target[2] += (axes.right[2] * dx - axes.forward[2] * dy) * scale;
}

void CameraController::OnMouseMove(int x, int y, bool fastDown) {
    if (moveDrag_ == MoveDragPhase::Pending) {
        const int moveX = x - lastDragPos_.x;
        const int moveY = y - lastDragPos_.y;
        if (moveX * moveX + moveY * moveY < kMoveDragStartPx * kMoveDragStartPx) return;
        // 開始時に飛ばないよう、基準位置を今の位置へ取り直す。
        moveDrag_ = MoveDragPhase::Moving;
        lastDragPos_.x = x;
        lastDragPos_.y = y;
        return;
    }
    const bool moving = moveDrag_ == MoveDragPhase::Moving;
    if (!rotating_ && !moving) return;
    if (moving) {
        CancelFlyTo();
    } else {
        CancelFlyToUnlessFollowing();
    }
    float dx = float(x - lastDragPos_.x);
    float dy = float(y - lastDragPos_.y);
    const CameraTuning& tuning = CameraTuning::Embedded();
    if (moving) {
        // ドラッグ開始後にShiftを離しても、ボタンを離すまでは押した時点の向き(水平/垂直)で続ける。
        ApplyMoveDrag(dx, dy, fastDown);
    } else {
        // 右ドラッグ = 基準点(target)を中心とした回転。ピボットは常にtargetなので
        // 特別な捕捉処理は不要で、yaw/pitchを更新するだけでよい。
        fly.yaw += dx * tuning.flyMouseSensitivity;
        fly.pitch += dy * tuning.flyMouseSensitivity;
        if (fly.pitch > tuning.flyPitchLimitRadians) fly.pitch = tuning.flyPitchLimitRadians;
        if (fly.pitch < -tuning.flyPitchLimitRadians) fly.pitch = -tuning.flyPitchLimitRadians;
    }
    lastDragPos_.x = x;
    lastDragPos_.y = y;
}

void CameraController::OnWheel(float delta, bool shiftDown) {
    CancelFlyToUnlessFollowing();
    const CameraTuning& tuning = CameraTuning::Embedded();
    if (shiftDown) {
        // Shift+ホイール = 視野角調整。配置プレビュー中の高さ調整
        // (AdjustSceneryPlacementHeight)はCtrl+ホイールを使うため競合しない。
        if (!fovAdjustLocked) {
            fly.fieldOfView = std::clamp(fly.fieldOfView - delta * tuning.fovYWheelStepRadians,
                                         tuning.fovYMinRadians, tuning.fovYMaxRadians);
        }
        return;
    }

    // ホイール = distance(基準点からの距離)を乗算調整するズーム。
    // 近いほど1ノッチの実移動量が小さくなる(Cities風の減速感)。
    float factor = (delta > 0.0f) ? tuning.viewZoomStepIn : tuning.viewZoomStepOut;
    fly.distance *= std::pow(factor, std::abs(delta));
    fly.distance = std::clamp(fly.distance, tuning.viewDistanceMinMeters, tuning.viewDistanceMaxMeters);
}

void CameraController::UpdateWasd(float dt, bool foreground, const rs::app::input::InputSource& input) {
    // キー状態は入力Action層から取得する(デバイス境界はImGuiInputBridgeのみ)。
    // バックエンドはフォーカス時のみキーを受け取るためバックグラウンドでは反応しないが、
    // 念のため foreground ガードも残す。UIのキーボード捕捉はテキスト入力中の衝突回避。
    if (!input.AllowsKeyboard(rs::app::input::InputContext::View) || !foreground) return;

    const bool moving = input.Held(rs::app::input::InputAction::CameraMoveForward) ||
                        input.Held(rs::app::input::InputAction::CameraMoveBack) ||
                        input.Held(rs::app::input::InputAction::CameraMoveRight) ||
                        input.Held(rs::app::input::InputAction::CameraMoveLeft);
    if (moving) CancelFlyTo();

    const HorizontalAxes axes = HorizontalAxesFromYaw(fly.yaw);
    const float* const fwdHoriz = axes.forward;
    const float* const right = axes.right;

    // 移動速度 = 基準倍率 * 距離係数(distance) * dt。
    // 基準点から離れているほど速く動く(Cities:Skylines流)。
    const CameraTuning& tuning = CameraTuning::Embedded();
    float dist = viewMoveMultiplier * std::max(fly.distance, tuning.viewDistMoveMinMeters) * dt;
    // Ctrl押下中はダッシュ。
    if (input.MatchesPointer(rs::app::input::PointerAction::CameraDash)) dist *= tuning.viewDashMultiplier;

    if (input.Held(rs::app::input::InputAction::CameraMoveForward)) {
        fly.target[0] += fwdHoriz[0] * dist;
        fly.target[2] += fwdHoriz[2] * dist;
    }
    if (input.Held(rs::app::input::InputAction::CameraMoveBack)) {
        fly.target[0] -= fwdHoriz[0] * dist;
        fly.target[2] -= fwdHoriz[2] * dist;
    }
    if (input.Held(rs::app::input::InputAction::CameraMoveRight)) {
        fly.target[0] += right[0] * dist;
        fly.target[2] += right[2] * dist;
    }
    if (input.Held(rs::app::input::InputAction::CameraMoveLeft)) {
        fly.target[0] -= right[0] * dist;
        fly.target[2] -= right[2] * dist;
    }
}

void CameraController::UpdateRoll(double dtSeconds, bool foreground, const input::InputSource& input) {
    rolling_ = false;
    if (!foreground || !input.AllowsKeyboard(input::InputContext::View)) return;
    if (input.Triggered(input::InputAction::CameraRollReset)) {
        fly.SetRoll(0.0);
        return;
    }
    const int direction = static_cast<int>(input.Held(input::InputAction::CameraRollLeft)) -
                          static_cast<int>(input.Held(input::InputAction::CameraRollRight));
    rolling_ = direction != 0;
    const double deltaRadians = static_cast<double>(direction) * CameraTuning::Embedded().rollRadiansPerSecond * dtSeconds;
    fly.SetRoll(static_cast<double>(fly.roll) + deltaRadians);
}

CameraView CameraController::BuildView() const {
    CameraView view;
    smoothed_.EyePos(view.eye);
    view.target[0] = smoothed_.target[0];
    view.target[1] = smoothed_.target[1];
    view.target[2] = smoothed_.target[2];
    view.renderDistance = renderDistance;
    view.fovYRadians = smoothed_.fieldOfView;
    // world-upを視線まわりに回す。視線に平行な成分はLookAtで除かれる。
    const double pitch = smoothed_.pitch;
    const double yaw = smoothed_.yaw;
    const rs::core::DVec3 forward{-std::cos(pitch) * std::sin(yaw), -std::sin(pitch),
                                -std::cos(pitch) * std::cos(yaw)};
    const rs::core::DVec3 worldUp{0.0, 1.0, 0.0};
    const double sine = std::sin(static_cast<double>(smoothed_.roll));
    const double cosine = std::cos(static_cast<double>(smoothed_.roll));
    const rs::core::DVec3 rolledUp = rs::core::Add(
        rs::core::Add(rs::core::Scale(worldUp, cosine), rs::core::Scale(rs::core::Cross(forward, worldUp), sine)),
        rs::core::Scale(forward, rs::core::Dot(forward, worldUp) * (1.0 - cosine)));
    view.up[0] = static_cast<float>(rolledUp.x);
    view.up[1] = static_cast<float>(rolledUp.y);
    view.up[2] = static_cast<float>(rolledUp.z);
    return view;
}

float CameraController::CenterHeight() const {
    return smoothed_.target[1];
}

} // namespace rs::app
