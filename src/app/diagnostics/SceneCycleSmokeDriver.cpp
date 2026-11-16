//
// SceneCycleSmokeDriver.cpp
//
#include "SceneCycleSmokeDriver.h"

#include <algorithm>
#include <chrono>

#include <spdlog/spdlog.h>

#include "../action/LayoutPersistenceAction.h"
#include "../plugin_session/LayoutSession.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {

namespace {

// 読込のあと、切替を始めるまでに待つフレーム数の下限。
constexpr int kWarmupFrames = 30;
// 切替と切替のあいだに空けるフレーム数。
constexpr int kIdleFrames = 10;
// 切替後、最低でもこのフレーム数は様子を見る(Syncの読込ジョブが次のフレームから動き出すため)。
constexpr int kMinSettleFrames = 3;
// 切替後、これだけ待っても落ち着かなければ失敗とする。
constexpr int kMaxSettleFrames = 3600;

} // namespace

void SceneCycleSmokeDriver::RecordSync(double syncMs) {
    if (phase_ != Phase::Settling) return;
    syncTotalMs_ += syncMs;
    syncMaxMs_ = std::max(syncMaxMs_, syncMs);
}

void SceneCycleSmokeDriver::Fail(const std::string& reason) {
    spdlog::error("[scene-cycle-smoke] FAILED: {} ({}回切替済み)", reason, completed_);
    phase_ = Phase::Done;
}

void SceneCycleSmokeDriver::BeginSwitch(LayoutPersistenceAction& action, const LayoutSession& session) {
    fromScene_ = session.activeIndex;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    action.SwitchScene(+1);
    callMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    toScene_ = session.activeIndex;
    // 読込ジョブの最中などで切り替わらなかったときは、数えずに次のフレームでやり直す。
    if (toScene_ == fromScene_) return;
    framesSinceSwitch_ = 0;
    syncTotalMs_ = 0.0;
    syncMaxMs_ = 0.0;
    phase_ = Phase::Settling;
}

void SceneCycleSmokeDriver::FinishSwitch() {
    ++completed_;
    allSyncTotalMs_ += syncTotalMs_;
    allSyncMaxMs_ = std::max(allSyncMaxMs_, syncMaxMs_);
    spdlog::info("[scene-cycle-smoke] 切替{}/{}: Scene{}→{} 呼び出し={:.2f}ms 落ち着くまで={}フレーム "
                 "Sync合計={:.2f}ms Sync最大={:.2f}ms",
                 completed_, switchCount_, fromScene_, toScene_, callMs_, framesSinceSwitch_, syncTotalMs_,
                 syncMaxMs_);
    if (completed_ >= switchCount_) {
        spdlog::info("[scene-cycle-smoke] OK: {}回切替 Sync合計の平均={:.2f}ms Sync最大={:.2f}ms", completed_,
                     allSyncTotalMs_ / completed_, allSyncMaxMs_);
        phase_ = Phase::Done;
        return;
    }
    idleFrames_ = 0;
    phase_ = Phase::Idle;
}

void SceneCycleSmokeDriver::MaybeSwitch(int frame, LayoutPersistenceAction& action, const LayoutSession& session,
                                        const SceneRenderer& renderer) {
    if (!Active() || phase_ == Phase::Done) return;
    const bool jobActive = renderer.SyncJobActive();

    if (phase_ == Phase::WaitLoad) {
        if (frame < kWarmupFrames || jobActive) return;
        if (session.scenes.size() < 2) {
            Fail("Sceneが1つしかない(--openlayoutで複数Sceneのレイアウトを開く)");
            return;
        }
        phase_ = Phase::Idle;
        idleFrames_ = kIdleFrames;
    }

    if (phase_ == Phase::Idle) {
        ++idleFrames_;
        if (idleFrames_ >= kIdleFrames && !jobActive) BeginSwitch(action, session);
        return;
    }

    // Settling
    ++framesSinceSwitch_;
    if (framesSinceSwitch_ >= kMinSettleFrames && !jobActive) {
        FinishSwitch();
    } else if (framesSinceSwitch_ > kMaxSettleFrames) {
        Fail("切替後のSyncが落ち着かない");
    }
}

} // namespace rs::app
