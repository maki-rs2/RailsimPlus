//
// CabinViewCamera.h - 運転台視点(カブビュー)カメラ状態
//
// 選択編成のカブピボット(FrontCabin/TailCabin、PluginSchema.hのJoint3DRef)から
// のオフセット(CabinPos/CabinDir)を編成ごと・前後カブ別に保持する。既存の俯瞰
// カメラ(FlyCamera/CameraController)とは別系統で、CameraController::Ease()の
// 平滑化パイプラインを通さない(入力に対する応答が1フレーム遅延なしで
// 反映される)。
//
#pragma once

#include <unordered_map>
#include <array>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"
#include "../../world/scene/TrainId.h"
#include "CameraView.h"

namespace rs::app::input {
class InputSource;
}

namespace rs::app {

// CabinPos/CabinDir 1組(前カブ/後カブいずれか1つぶん)。
struct CabinOffset {
    // CabinPos: カブピボットのローカル座標系での位置オフセット[m]
    // (x=左右, y=上下, z=前後)。各軸独立に±10.0mでクランプする。
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    // CabinDir: 横方向(dirX)はピボットのup軸まわりの回転角[rad]で、クランプせず
    // 360度自由に回転できる(意図的なRailSim II非互換)。縦方向(dirY)は
    // ローカル座標系での視線オフセット(タンジェント値、オイラー角ではない)を
    // 保持し、±2.0でクランプする。
    float dirX = 0.0f, dirY = 0.0f;
};

class CabinViewCamera {
public:
    // カブ視点ON/OFF(FormationPanelのトグルが書き換える)。
    bool active = false;
    // 追従中の編成。NilGuidなら追従対象なし。
    rs::world::TrainId followedTrainId = rs::world::NilGuid();
    // 現在使っているカブ(true=後カブ/TailCabin)。呼び出し側が
    // TrainRuntime::GroupTravelReversed等から毎フレーム再計算して書き込む。
    bool followTailCabin = false;

    // WASD+上下矢印キーでCabinPosを直接更新する(イージング無し)。
    // 毎フレーム呼ぶこと。activeでない、またはfollowedTrainIdが未設定なら何もしない。
    void UpdateMovement(float dtSeconds, bool foreground, const rs::app::input::InputSource& input);

    // 右ドラッグ(MainWindowのWM_RBUTTONDOWN/WM_MOUSEMOVE/WM_RBUTTONUPから委譲)。
    // 既存の俯瞰カメラの右ドラッグ回転とボタン割当を揃える。
    void OnRightDown(int x, int y);
    void OnRightUp();
    // Shift+左ドラッグ = CabinPosの高さ(Y)調節(MainWindowのWM_LBUTTONDOWN/
    // WM_MOUSEMOVE/WM_LBUTTONUPから委譲)。俯瞰カメラ(CameraController::OnLeftDown)と
    // 同じ操作方法・同じ規約。shiftDownがfalseなら何もせずfalseを返す
    // (エディタ操作等に譲る)。
    bool OnLeftDown(int x, int y, bool shiftDown);
    void OnLeftUp();
    void OnMouseMove(int x, int y);
    bool Dragging() const { return rotating_; }
    bool HeightDragging() const { return heightDragging_; }

    // 現在のfollowedTrainId+followTailCabinの組のCabinPos/CabinDirを初期値へ戻す。
    void Reset();

    // 編成削除時にエントリを破棄する。
    void RemoveTrain(rs::world::TrainId trainId);

    // pivotWorld(ComposeJoint3DWorldが返すカブピボットのワールド剛体変換、
    // row-major row-vector: 行0=right/行1=up/行2=forward/行3=origin)+現在の
    // CabinPos/CabinDirオフセットからCameraViewを組み立てる。
    rs::app::CameraView BuildView(const rs::core::Mat4& pivotWorld,
                                  float renderDistance, float fovYRadians) const;

    // カブ視点が有効かつ直近のピボット解決に成功していれば、その目の位置・視線方向・
    // up方向をoutへ書き込みtrueを返す。_CAMDIST・オーディオリスナー等、俯瞰カメラと
    // カブ視点カメラのどちらがアクティブかを意識せず「現在の実効カメラ視点」だけを
    // 必要とする呼び出し元向け。
    // 未解決/非アクティブならfalseを返す(呼び出し側は俯瞰カメラの値を使うこと)。
    bool TryResolveEyeForward(float outEye[3], float outForward[3], float outUp[3]) const;

    // 直近成功したResolveCabinPivot結果をキャッシュする。resolved==falseの場合は
    // 何もしない(呼び出し側は前フレームのピボットを使い続ける。SceneTrainController.h
    // ResolveCabinPivotのコメント参照。Scene切替直後の一部フレームは非常駐で解決
    // できないため、identityへフォールバックしない)。
    void CachePivotIfResolved(bool resolved, const rs::core::Mat4& pivotWorld);
    bool HasCachedPivot() const { return hasPivot_; }
    const rs::core::Mat4& CachedPivot() const { return lastPivotWorld_; }
    // 追従対象編成を切り替える際、別編成の古いピボットを使い続けないよう呼ぶこと。
    void ResetPivotCache() { hasPivot_ = false; }

    // 現在のfollowedTrainId+followTailCabinの組のCabinPos/CabinDirを読む(HUD表示・
    // テスト用の読み取り専用アクセサ。未エントリならnullptr、そのときは既定値
    // (CabinOffset{})相当として扱ってよい)。
    const CabinOffset* CurrentIfPresent() const;

private:
    CabinOffset& Current();

    bool rotating_ = false;
    bool heightDragging_ = false;
    // Shift+左を押してから、しきい値(px)動くまでの待ち状態。
    bool heightPressed_ = false;
    int lastDragX_ = 0, lastDragY_ = 0;
    // [0]=FrontCabin, [1]=TailCabin。編成ごと・前後カブ別に2組保持する。
    std::unordered_map<rs::world::TrainId, std::array<CabinOffset, 2>, rs::world::GuidHash> offsets_;
    rs::core::Mat4 lastPivotWorld_ = rs::core::Mat4::Identity();
    bool hasPivot_ = false;
};

} // namespace rs::app
