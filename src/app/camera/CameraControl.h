//
// CameraControl.h - カメラ操作コントローラ
//
// カメラはビュー=Fly(マップエディタ流の常時WASD移動)の単一方式。CameraController は
// FlyCamera とマウス/WASD入力状態を束ね、Win32メッセージ・毎フレーム更新・ビュー構築を
// 受け持つ。ウィンドウ生成・キャプチャ(SetCapture等)は呼び出し側(WndProc)に残す。
//
#pragma once

#include <windows.h>
#include "../input/InputSource.h"

#include "../../core/Math.h"
#include "../../world/scene/SurfaceBounds.h"
#include "../../world/scene/TrainId.h"
#include "../gpu/CameraMath.h"
#include "FlyCamera.h"

namespace rs::app {

class CameraController {
public:
    FlyCamera fly;
    float viewMoveMultiplier = 1.0f; // WASD移動速度倍率(UIで0.1〜5.0)
    // 描画距離(遠クリップ面の上限)。UIの「描画距離」スライダが書き換える。
    // カメラ距離では押し上げない。
    float renderDistance = kDefaultRenderDistance;
    // true の間は Shift+ホイールで視野角を変えない(ファインダー中は撮影カメラの焦点距離が視野角を決める)。
    bool fovAdjustLocked = false;
    // 視点操作の設定と表示モードから、呼び出し側が毎フレーム入れる。偽の間は、その
    // ボタンのドラッグでは注目点を動かさない(Shift+左ドラッグの高さ調節はこれによらず使える)。
    bool middleDragMovesTarget = false;
    bool leftDragMovesTarget = false;

    // カメラをAABBへフィットさせる。
    void FitToAabb(const float mn[3], const float mx[3]);

    // カメラを初期状態(原点注視)へ戻す。
    void ResetActive();

    // 基準点(fly.target)を target へなめらかに移す。向き・距離・視野角は変えない。かかる時間は距離に応じて
    // 伸びる(下限と上限は CameraTuning)。target は呼び出し時点の値で、後から動いても追わない。
    // 移動中に視点操作(WASD・右ドラッグ・高さドラッグ・ホイール)・FitToAabb・ResetActive・SnapToTarget が
    // あると移動をやめ、その時点の fly.target から操作を続ける。
    void FlyTo(const rs::core::Vec3& target);

    // 列車 trainId の追跡を始める。基準点は毎フレーム UpdateFollow が渡す座標へ合わせる(最初はFlyToと同じ
    // 動きで寄り、寄っている間も目標を更新する)。向き・距離・視野角は変えない。
    // WASD・高さドラッグ・FitToAabb・ResetActive・SnapToTarget・StopFollow でやめる(回転とズームは続ける)。
    void BeginFollow(const rs::world::TrainId& trainId, const rs::core::Vec3& anchor);
    void StopFollow() { followTrainId_ = rs::world::NilGuid(); }
    bool IsFollowing() const { return !rs::world::GuidIsNil(followTrainId_); }
    const rs::world::TrainId& FollowedTrainId() const { return followTrainId_; }
    // 追跡中の毎フレーム、追跡先の座標を渡す。Ease より前に呼ぶ。
    void UpdateFollow(const rs::core::Vec3& anchor);
    // 基準点(カメラ中心)をSurfaceの範囲の中へ押し戻す。寄っている最中は、寄り先も押し戻す。
    void ClampTargetToSurface(const rs::world::SurfaceBounds& bounds);

    void Ease(float dt);
    // 移動(FlyTo)もやめて、描画用の値を fly へ揃える。
    void SnapToTarget();

    // ---- Win32入力(WndProcから委譲。キャプチャ自体は呼び出し側が行う) ----
    void OnRightDown(int x, int y); // 右ドラッグ回転を開始
    void OnRightUp();
    // 左ドラッグ = 基準点の移動。Shift押下中は垂直(高さ調節)、Shiftなしは leftDragMovesTarget のときだけ水平。
    // 受け付けたら true を返す(呼び出し側は true のときだけ SetCapture する)。実際の移動は、
    // 押したまま数px動いてから始まる(動かさず離したクリックは何もしない)。
    bool OnLeftDown(int x, int y, bool shiftDown);
    // 左ボタンのドラッグ(待ちを含む)だったら true を返す(呼び出し側は ReleaseCapture の判断に使う)。
    bool OnLeftUp();
    // 中ドラッグ = 基準点の移動。middleDragMovesTarget のときだけ受け付け、Shift押下中は垂直に動かす。
    bool OnMiddleDown(int x, int y, bool shiftDown);
    bool OnMiddleUp();
    // 左・中ドラッグによる基準点の移動中か(エディタの左クリック処理との衝突回避に使う)。
    bool HeightDragging() const { return moveDrag_ == MoveDragPhase::Moving; }
    // fastDown は倍率キー(Ctrl)の押下。基準点の移動中の倍率に使う。
    void OnMouseMove(int x, int y, bool fastDown);
    void OnWheel(float delta, bool shiftDown);

    // ---- 毎フレーム ----
    // WASDで基準点を水平移動する(フォアグラウンド・キーボード非占有時)。
    void UpdateWasd(float dt, bool foreground, const rs::app::input::InputSource& input);

    // カメラ画面の間だけ呼び、傾きの押下継続とリセットを反映する。
    void UpdateRoll(double dtSeconds, bool foreground, const input::InputSource& input);
    // 直近の UpdateRoll で、傾きのキーを押して傾きを変えている最中か。
    bool IsRolling() const { return rolling_; }

    // 描画用ビューを組み立てる。
    CameraView BuildView() const;

    // 基準点の高さ(target[1])。敷設面追従に使う。
    float CenterHeight() const;

private:
    // FlyTo の移動を途中でやめる。fly.target は補間値のまま残す。
    // 基準点を動かす操作が呼ぶ。追跡もやめる。
    void CancelFlyTo() {
        flyToActive_ = false;
        StopFollow();
    }
    // 回転・ズームが呼ぶ。追跡中は、追跡先へ寄る動きを止めない。
    void CancelFlyToUnlessFollowing() {
        if (!IsFollowing()) flyToActive_ = false;
    }

    rs::world::TrainId followTrainId_ = rs::world::NilGuid();
    bool rolling_ = false;

    FlyCamera smoothed_;

    // FlyTo の移動(Ease が進める)。
    bool flyToActive_ = false;
    float flyToFrom_[3] = {0.0f, 0.0f, 0.0f};
    float flyToGoal_[3] = {0.0f, 0.0f, 0.0f};
    float flyToElapsedSeconds_ = 0.0f;
    float flyToDurationSeconds_ = 0.0f;

    // 左・中ボタンの移動ドラッグを始める。待ちの状態にして、しきい値を超えたら Moving へ進む。
    void BeginMoveDrag(rs::app::input::PointerButton button, int x, int y, bool vertical);
    // 押したボタンが button のときだけ移動ドラッグを終え、そのボタンが捕捉していたら true を返す。
    bool EndMoveDrag(rs::app::input::PointerButton button);
    // 移動ドラッグの1回分(dx, dy はpx)。
    void ApplyMoveDrag(float dx, float dy, bool fastDown);

    // ドラッグ操作の状態(右ドラッグ=回転 / 左・中ドラッグ=基準点の移動)
    enum class MoveDragPhase { None, Pending, Moving };
    bool rotating_ = false;
    // Pending は、ボタンを押して、まだドラッグのしきい値を超えていない間。
    MoveDragPhase moveDrag_ = MoveDragPhase::None;
    rs::app::input::PointerButton moveDragButton_ = rs::app::input::PointerButton::Left;
    // 垂直(高さ)に動かすか。水平なら false。押した時点で決まる。
    bool moveDragVertical_ = false;
    POINT lastDragPos_ = {0, 0};
};

} // namespace rs::app
