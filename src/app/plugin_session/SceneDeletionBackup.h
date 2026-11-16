//
// SceneDeletionBackup.h - Sceneの削除と、直近1回分の取り消し(復元)
//
// 控えはレイアウト全体で1つだけで、.rslへは保存しない(持ち主は LayoutPersistenceAction)。
// Sceneのキャッシュ・そのSceneを端に持つワープ・そのSceneにいた編成の車両を控え、削除前の番号で戻す。
// 削除のあとにほかの操作が入って番号がずれると戻せないので、控えの有効判定は IsCurrent に集める。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "LayoutSession.h"
#include "../../sim/TrainRuntime.h"

namespace rs::app {

struct DeletedSceneBackup {
    // 消えたSceneの削除前の位置と、削除前の今のScene。
    size_t index = 0;
    size_t activeIndexBefore = 0;
    // 削除したあとのScene数(復元はこの数のレイアウトにだけ戻せる)。
    size_t sceneCountAfter = 0;
    LayoutSceneCache scene;
    // 消えたワープと、削除前のワープ表での位置。
    std::vector<std::pair<size_t, rs::world::WarpLink>> warps;
    // 消えた編成の車両と、削除前のSceneRegistry内での位置(昇順)。
    std::vector<std::pair<size_t, rs::world::SceneEntity>> cars;
    std::vector<rs::world::TrainId> trainIds;
    // 消えた編成の走行状態(TrainRuntime側。ダイヤ・タグ・運行情報は戻らない)。
    std::vector<rs::sim::TrainRuntimeSnapshot> trainRuntime;
    // 削除が済んだ時点の、変更あり判定の材料。1つでも動いていれば、控えは古い。
    uint64_t editorRevision = 0;
    uint64_t sceneRegistryRevision = 0;
    uint64_t markedEdits = 0;

    // 削除のあとに編集・Scene操作が入っていないか(復元してよいか)。
    bool IsCurrent(uint64_t currentEditorRevision, uint64_t currentRegistryRevision, uint64_t currentMarkedEdits,
                   size_t currentSceneCount) const {
        return editorRevision == currentEditorRevision && sceneRegistryRevision == currentRegistryRevision &&
               markedEdits == currentMarkedEdits && sceneCountAfter == currentSceneCount;
    }
};

// 編成のどれかの車両がsceneIndexのSceneにいる編成のid(数える・消す対象)。
std::vector<rs::world::TrainId> TrainsInScene(const rs::world::SceneRegistry& registry, size_t sceneIndex);

// indexのSceneを消す。そのSceneを端に持つワープと、trainIdsの編成の車両(SceneRegistryから)も消し、控えをbackupへ入れる。
// 今のSceneを消すときは隣(消した位置に来る行、末尾なら1つ前)へ切り替える。今のSceneでなければそのまま。
// 戻り値は付け替え表(oldToNew[旧]=新、消えたSceneはkRemovedScene)。Sceneが1つ以下・範囲外なら空で、何も変えない。
// 走行系(TrainRuntime・予約・ワープ作成中)の付け替えと、編成の走行状態の後始末は呼び出し側が行う。
std::vector<size_t> DeleteLayoutScene(size_t index, const std::vector<rs::world::TrainId>& trainIds,
                                      rs::editor::Editor& editor, rs::world::SceneRegistry& registry,
                                      CameraController& camera, std::vector<ImportedWireSpan>& wireSpans,
                                      LayoutSession& session, DeletedSceneBackup& backup);

// backupのSceneを削除前の位置へ戻し、ワープと車両も戻して、今のSceneを削除前に戻す。
// 戻り値は挿入の付け替え表(今ある番号→戻したあとの番号)。整合が取れないときは空で、何も変えない。
// 走行系の付け替えと編成の走行状態の復元は呼び出し側が行う(付け替えてから復元する)。
std::vector<size_t> RestoreLayoutScene(const DeletedSceneBackup& backup, rs::editor::Editor& editor,
                                       rs::world::SceneRegistry& registry, CameraController& camera,
                                       std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session);

} // namespace rs::app
