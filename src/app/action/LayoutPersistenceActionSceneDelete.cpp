//
// LayoutPersistenceActionSceneDelete.cpp - LayoutPersistenceActionのSceneの削除と、削除の取り消し
//
// データの入れ替えは LayoutSessionSceneDelete.cpp、ここは走行系・編成パネル・文書の状態の後始末をまとめる。
//
#include "LayoutPersistenceAction.h"

#include <algorithm>

#include "FormationAction.h"
#include "../ui/FormationPanel.h"
#include "../../editor/tool/Editor.h"
#include "../../editor/tool/WarpLinkAuthoring.h"
#include "../../world/scene/SceneRegistry.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {

bool LayoutPersistenceAction::DeleteScene(size_t index) {
    if (!PrepareSceneOperation()) return false;
    const std::vector<rs::world::TrainId> trainIds = TrainsInScene(refs_.sceneRegistry, index);
    DeletedSceneBackup backup;
    const std::vector<size_t> oldToNew =
        DeleteLayoutScene(index, trainIds, refs_.editor, refs_.sceneRegistry, refs_.camera,
                          refs_.importedWireSpans, refs_.layoutSession, backup);
    if (oldToNew.empty()) return false;
    // 走行状態は、編成を消す前に控える。編成の撤去は、編成パネルの撤去と同じ経路を通す。
    backup.trainRuntime = refs_.sceneRenderer.TrainRuntime().SnapshotOf(trainIds);
    FormationAction formation({refs_.selector, refs_.sceneRegistry, refs_.sceneRenderer, refs_.trainPlacementUi,
                               refs_.document, refs_.cabinCamera, refs_.camera});
    for (const rs::world::TrainId& trainId : trainIds) formation.RemoveGroup(trainId);
    refs_.sceneRenderer.RemapTrainSceneIndices(oldToNew);
    refs_.warpAuthoring.RemapScenes(oldToNew);
    FinishSceneSwitch(true);
    backup.editorRevision = refs_.editor.DocumentRevision();
    backup.sceneRegistryRevision = refs_.sceneRegistry.PersistentRevision();
    backup.markedEdits = refs_.document.markedEdits;
    deletedScene_ = std::move(backup);
    return true;
}

bool LayoutPersistenceAction::HasRestorableScene() {
    if (!deletedScene_) return false;
    if (!deletedScene_->IsCurrent(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision(),
                                  refs_.document.markedEdits, refs_.layoutSession.scenes.size())) {
        deletedScene_.reset();
        return false;
    }
    return true;
}

std::string LayoutPersistenceAction::DeletedSceneName() const {
    return deletedScene_ ? deletedScene_->scene.name : std::string();
}

bool LayoutPersistenceAction::RestoreDeletedScene() {
    if (!PrepareSceneOperation() || !HasRestorableScene()) return false;
    const DeletedSceneBackup backup = std::move(*deletedScene_);
    deletedScene_.reset();
    const std::vector<size_t> oldToNew =
        RestoreLayoutScene(backup, refs_.editor, refs_.sceneRegistry, refs_.camera, refs_.importedWireSpans,
                           refs_.layoutSession);
    if (oldToNew.empty()) return false;
    // 今ある編成の番号を戻してから、消していた編成の走行状態(削除前の番号のまま)を足す。
    refs_.sceneRenderer.RemapTrainSceneIndices(oldToNew);
    refs_.warpAuthoring.RemapScenes(oldToNew);
    refs_.sceneRenderer.TrainRuntime().AddSnapshots(backup.trainRuntime);
    std::vector<rs::world::TrainId>& placed = refs_.trainPlacementUi.placedTrainIds;
    for (const rs::world::TrainId& trainId : backup.trainIds) {
        if (std::find(placed.begin(), placed.end(), trainId) == placed.end()) placed.push_back(trainId);
    }
    FinishSceneSwitch(true);
    return true;
}

}  // namespace rs::app
