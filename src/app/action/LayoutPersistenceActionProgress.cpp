//
// LayoutPersistenceActionProgress.cpp - 読込ジョブの進み具合の取得
//
// 読込ジョブ本体(SceneSlotSynchronizer)に依存するので、LayoutPersistenceAction.cppとは別に置く
// (テストのターゲットはそちらだけを組み立てる)。
//
#include "LayoutPersistenceAction.h"

#include "../scene/SceneRenderer.h"

namespace rs::app {

LayoutLoadProgress LayoutPersistenceAction::LoadProgress() const {
    LayoutLoadProgress progress;
    progress.jobActive = refs_.sceneRenderer.SyncJobActive();
    if (!progress.jobActive) return progress;
    progress.phase = refs_.sceneRenderer.SyncJobPhase();
    progress.done = refs_.sceneRenderer.SyncJobPhaseDone();
    progress.total = refs_.sceneRenderer.SyncJobPhaseTotal();
    return progress;
}

} // namespace rs::app
