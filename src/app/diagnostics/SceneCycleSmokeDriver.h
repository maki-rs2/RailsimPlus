//
// SceneCycleSmokeDriver.h - --scene-cycle-smoke: Scene切替の所要時間をヘッドレスで測る
//
// 読込が落ち着いたあと、Sceneを順に切り替えるのをN回くり返し、1回ごとに
// 「切替の呼び出し」と「切替後のSyncが落ち着くまで」の時間を出す。Scene切替は
// GUIのキー操作でしか通らず、通常の--smokeでは一度も実行されないため、
// 切替のたびにGPU資源の保管・復活がどれだけ働くかを数字で比べる手段にする。
//
#pragma once

#include <cstddef>
#include <string>

namespace rs::app {

class LayoutPersistenceAction;
class SceneRenderer;
struct LayoutSession;

class SceneCycleSmokeDriver {
public:
    // switchCount: 切り替える回数。0以下なら何もしない。
    explicit SceneCycleSmokeDriver(int switchCount) : switchCount_(switchCount) {}

    bool Active() const { return switchCount_ > 0; }

    // 規定回数の切替を終えた、または続けられなくなった。
    bool Finished() const { return phase_ == Phase::Done; }

    // Sync直後に呼ぶ。切替後のSyncにかかった時間を足す。
    void RecordSync(double syncMs);

    // Syncの後に毎フレーム呼ぶ。切替を起こし、切替後の落ち着きを見て結果を出す。
    void MaybeSwitch(int frame, LayoutPersistenceAction& action, const LayoutSession& session,
                     const SceneRenderer& renderer);

private:
    enum class Phase { WaitLoad, Idle, Settling, Done };

    void Fail(const std::string& reason);
    void BeginSwitch(LayoutPersistenceAction& action, const LayoutSession& session);
    void FinishSwitch();

    int switchCount_ = 0;
    Phase phase_ = Phase::WaitLoad;
    int completed_ = 0;
    int idleFrames_ = 0;

    // 直近の切替の計測。
    size_t fromScene_ = 0;
    size_t toScene_ = 0;
    double callMs_ = 0.0;
    int framesSinceSwitch_ = 0;
    double syncTotalMs_ = 0.0;
    double syncMaxMs_ = 0.0;

    // 全切替の集計。
    double allSyncTotalMs_ = 0.0;
    double allSyncMaxMs_ = 0.0;
};

} // namespace rs::app
