//
// CabinViewCamera.cpp
//
#include "CabinViewCamera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "CameraTuning.h"
#include "../input/InputSource.h"
#include "../../core/Math.h"

namespace rs::app {

namespace {

// Shift+左ボタンを押してから、これだけ(px)動いたらクリックでなくドラッグとして高さ調節を始める。
constexpr int kHeightDragStartPx = 4;
constexpr float kCabinPosMaxMeters = 10.0f;  // RailSim II CABIN_POS_MAX(CTrainGroup.cpp:28)
// RailSim II CABIN_DIR_MAX(CTrainGroup.cpp:30)。dirY(縦方向、タンジェント値)のみへ適用する
// (dirX/横方向は角度そのものとして扱い、クランプせず360度自由に回転できる)。
constexpr float kCabinDirMax = 2.0f;

// 角度[rad]を(-π, π]へ正規化する。dirXは360度自由回転でクランプしないため、
// 長時間ドラッグし続けても値が際限なく増大して浮動小数点精度が落ちないようにする
// (OnMouseMoveから使う)。
float WrapAngle(float radians) {
    float wrapped = std::fmod(radians + rs::core::kPiF, 2.0f * rs::core::kPiF);
    if (wrapped < 0.0f) wrapped += 2.0f * rs::core::kPiF;
    return wrapped - rs::core::kPiF;
}

// pivotのright/forward(正規直交)を、up軸まわりにyawRadiansだけ回転した水平方向の
// 単位ベクトルを返す(タンジェント値ではなく角度そのものを使うため360度自由に回転
// できる)。ComputeEyeBasis(視線)とUpdateMovement(WASD移動方向)の両方から使う
// (重複させない)。
rs::core::Vec3 YawForward(const rs::core::Vec3& right, const rs::core::Vec3& forward, float yawRadians) {
    return forward * std::cos(yawRadians) + right * std::sin(yawRadians);
}

// pivotWorld(カブピボットのワールド剛体変換)+offset(CabinPos/CabinDir、nullptrなら
// 既定値0扱い)から、目の位置・視線方向(正規化済み)・up方向を求める共有計算。
// BuildViewとTryResolveEyeForwardの両方から使う(重複させない)。
struct ResolvedEyeBasis {
    rs::core::Vec3 eye;
    rs::core::Vec3 forward;
    rs::core::Vec3 up;
};

ResolvedEyeBasis ComputeEyeBasis(const rs::core::Mat4& pivotWorld, const CabinOffset* offset) {
    using rs::core::Vec3;

    const Vec3 origin(pivotWorld.m[12], pivotWorld.m[13], pivotWorld.m[14]);
    const Vec3 right(pivotWorld.m[0], pivotWorld.m[1], pivotWorld.m[2]);
    const Vec3 up(pivotWorld.m[4], pivotWorld.m[5], pivotWorld.m[6]);
    const Vec3 forward(pivotWorld.m[8], pivotWorld.m[9], pivotWorld.m[10]);

    const float posX = offset ? offset->posX : 0.0f;
    const float posY = offset ? offset->posY : 0.0f;
    const float posZ = offset ? offset->posZ : 0.0f;
    const float dirX = offset ? offset->dirX : 0.0f;  // 横方向: up軸まわりの回転角[rad]
    const float dirY = offset ? offset->dirY : 0.0f;  // 縦方向: タンジェント値

    // V3LocalToWorld相当: ローカル(x=右,y=上,z=前)オフセットを、pivotWorldの基底で
    // ワールド空間へ変換する(RailSim IIの CTrain::SetCabinView、CTrain.cpp:207-208)。
    const Vec3 eye = origin + right * posX + up * posY + forward * posZ;
    // CabinDir: 横方向(dirX)はforwardをup軸まわりにyaw回転、縦方向(dirY)はその上に
    // タンジェント値のピッチオフセットを乗せる。
    const Vec3 yawForward = YawForward(right, forward, dirX);
    const Vec3 dirWorld = (yawForward + up * dirY).Normalized();

    return ResolvedEyeBasis{eye, dirWorld, up};
}

} // namespace

CabinOffset& CabinViewCamera::Current() {
    std::array<CabinOffset, 2>& pair = offsets_[followedTrainId];
    return pair[followTailCabin ? 1 : 0];
}

const CabinOffset* CabinViewCamera::CurrentIfPresent() const {
    const std::unordered_map<rs::world::TrainId, std::array<CabinOffset, 2>,
                             rs::world::GuidHash>::const_iterator it = offsets_.find(followedTrainId);
    if (it == offsets_.end()) return nullptr;
    return &it->second[followTailCabin ? 1 : 0];
}

void CabinViewCamera::UpdateMovement(float dtSeconds, bool foreground,
                                     const rs::app::input::InputSource& input) {
    if (!active || rs::world::GuidIsNil(followedTrainId)) return;
    if (!input.AllowsKeyboard(rs::app::input::InputContext::Drive) || !foreground) return;

    using rs::core::Vec3;
    using rs::app::input::InputAction;

    const float speed = CameraTuning::Embedded().cabinMoveMetersPerSecond * dtSeconds;
    CabinOffset& offset = Current();

    const Vec3 pivotRight(lastPivotWorld_.m[0], lastPivotWorld_.m[1], lastPivotWorld_.m[2]);
    const Vec3 pivotUp(lastPivotWorld_.m[4], lastPivotWorld_.m[5], lastPivotWorld_.m[6]);
    const Vec3 pivotForward(lastPivotWorld_.m[8], lastPivotWorld_.m[9], lastPivotWorld_.m[10]);

    // 現在のCabinDir(視線)のヨー(左右)成分のみを反映した水平前方ベクトル。
    // ピッチ(dirY、見上げ/見下ろし)はWASDの移動方向に巻き込まない(見上げたまま前進
    // しても高さ=posYは変わらない、一般的なFPS風の「歩行」挙動)。
    const Vec3 forwardYaw = YawForward(pivotRight, pivotForward, offset.dirX);
    // 視点の右方向。pivotUp基準でforwardYawと直交させる(forwardYaw/rightViewは
    // どちらもpivotRight・pivotForwardの線形結合=pivotUpに直交する水平ベクトルになる
    // ため、これらで作るmoveWorldは常に水平、posYはUp/Downキー・Shift+左ドラッグ
    // 以外では変化しない)。
    const Vec3 rightView = Vec3::Cross(pivotUp, forwardYaw).Normalized();

    Vec3 moveWorld(0.0f, 0.0f, 0.0f);
    if (input.Held(InputAction::CabinMoveForward)) moveWorld += forwardYaw * speed;
    if (input.Held(InputAction::CabinMoveBack)) moveWorld += forwardYaw * -speed;
    if (input.Held(InputAction::CabinMoveRight)) moveWorld += rightView * speed;
    if (input.Held(InputAction::CabinMoveLeft)) moveWorld += rightView * -speed;

    // ワールド増分をpivotのローカル基底(right/up/forward)へ射影してposX/Y/Zへ加算する
    // (ComputeEyeBasisのeye = origin + right*posX + up*posY + forward*posZ と対応する
    // 正規直交基底のため、射影=内積で戻せる)。
    offset.posX += Vec3::Dot(moveWorld, pivotRight);
    offset.posY += Vec3::Dot(moveWorld, pivotUp);
    offset.posZ += Vec3::Dot(moveWorld, pivotForward);

    if (input.Held(InputAction::CabinMoveUp)) offset.posY += speed;
    if (input.Held(InputAction::CabinMoveDown)) offset.posY -= speed;
    offset.posX = std::clamp(offset.posX, -kCabinPosMaxMeters, kCabinPosMaxMeters);
    offset.posY = std::clamp(offset.posY, -kCabinPosMaxMeters, kCabinPosMaxMeters);
    offset.posZ = std::clamp(offset.posZ, -kCabinPosMaxMeters, kCabinPosMaxMeters);
}

void CabinViewCamera::OnRightDown(int x, int y) {
    if (!active) return;
    rotating_ = true;
    lastDragX_ = x;
    lastDragY_ = y;
}

void CabinViewCamera::OnRightUp() { rotating_ = false; }

bool CabinViewCamera::OnLeftDown(int x, int y, bool shiftDown) {
    // Shift非押下なら何もせずfalseを返す(エディタ操作等に譲る、
    // CameraController::OnLeftDownと同じ規約)。
    if (!active || !shiftDown) return false;
    // 押した直後は「待ち」にとどめ、しきい値を超えて動いたときに初めて高さ調節を始める
    // (CameraController::OnLeftDownと同じ。Shift+クリックで高さが動かないようにする)。
    heightPressed_ = true;
    heightDragging_ = false;
    lastDragX_ = x;
    lastDragY_ = y;
    return true;
}

void CabinViewCamera::OnLeftUp() {
    heightPressed_ = false;
    heightDragging_ = false;
}

void CabinViewCamera::OnMouseMove(int x, int y) {
    if (heightPressed_) {
        const int moveX = x - lastDragX_;
        const int moveY = y - lastDragY_;
        if (moveX * moveX + moveY * moveY < kHeightDragStartPx * kHeightDragStartPx) return;
        // 開始時に飛ばないよう、基準位置を今の位置へ取り直す。
        heightPressed_ = false;
        heightDragging_ = true;
        lastDragX_ = x;
        lastDragY_ = y;
        return;
    }
    if ((!rotating_ && !heightDragging_) || rs::world::GuidIsNil(followedTrainId)) return;
    const float dx = static_cast<float>(x - lastDragX_);
    const float dy = static_cast<float>(y - lastDragY_);
    lastDragX_ = x;
    lastDragY_ = y;

    CabinOffset& offset = Current();
    if (heightDragging_) {
        // 俯瞰カメラ(CameraController::OnMouseMove)のheightDragging_分岐と同じ符号・
        // 同じ感度定数を使う(上ドラッグ=dy負=上昇)。
        const float sensitivity = CameraTuning::Embedded().viewHeightSensitivity;
        offset.posY -= dy * sensitivity;
        offset.posY = std::clamp(offset.posY, -kCabinPosMaxMeters, kCabinPosMaxMeters);
        return;
    }

    const float sensitivity = CameraTuning::Embedded().cabinDirMouseSensitivity;
    // 横方向(dirX)は角度[rad]として扱い、クランプせず360度自由に見回せるようにする。
    // 際限なく増大しないよう(-π, π]へ正規化する。
    // 縦方向(dirY)はタンジェント値のまま、RailSim IIのCABIN_DIR_MAXでクランプする。
    offset.dirX = WrapAngle(offset.dirX + dx * sensitivity);
    offset.dirY -= dy * sensitivity;
    offset.dirY = std::clamp(offset.dirY, -kCabinDirMax, kCabinDirMax);
}

void CabinViewCamera::Reset() {
    if (rs::world::GuidIsNil(followedTrainId)) return;
    Current() = CabinOffset{};
}

void CabinViewCamera::RemoveTrain(rs::world::TrainId trainId) {
    offsets_.erase(trainId);
    if (followedTrainId == trainId) ResetPivotCache();
}

void CabinViewCamera::CachePivotIfResolved(bool resolved, const rs::core::Mat4& pivotWorld) {
    if (!resolved) return;
    lastPivotWorld_ = pivotWorld;
    hasPivot_ = true;
}

rs::app::CameraView CabinViewCamera::BuildView(const rs::core::Mat4& pivotWorld,
                                               float renderDistance, float fovYRadians) const {
    using rs::core::Vec3;

    const ResolvedEyeBasis basis = ComputeEyeBasis(pivotWorld, CurrentIfPresent());

    rs::app::CameraView view;
    view.eye[0] = basis.eye.x; view.eye[1] = basis.eye.y; view.eye[2] = basis.eye.z;
    const Vec3 target = basis.eye + basis.forward;
    view.target[0] = target.x; view.target[1] = target.y; view.target[2] = target.z;
    view.up[0] = basis.up.x; view.up[1] = basis.up.y; view.up[2] = basis.up.z;
    view.renderDistance = renderDistance;
    view.fovYRadians = fovYRadians;
    return view;
}

bool CabinViewCamera::TryResolveEyeForward(float outEye[3], float outForward[3], float outUp[3]) const {
    if (!active || !hasPivot_) return false;

    const ResolvedEyeBasis basis = ComputeEyeBasis(lastPivotWorld_, CurrentIfPresent());
    outEye[0] = basis.eye.x; outEye[1] = basis.eye.y; outEye[2] = basis.eye.z;
    outForward[0] = basis.forward.x; outForward[1] = basis.forward.y; outForward[2] = basis.forward.z;
    outUp[0] = basis.up.x; outUp[1] = basis.up.y; outUp[2] = basis.up.z;
    return true;
}

} // namespace rs::app
