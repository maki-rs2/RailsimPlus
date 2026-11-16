//
// LayoutSessionSceneDelete.cpp - Sceneの削除と取り消し(LayoutSession.cppのScene操作の続き)
//
#include "SceneDeletionBackup.h"

#include <algorithm>
#include <unordered_set>

#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneIndexRemap.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

std::vector<rs::world::TrainId> TrainsInScene(const rs::world::SceneRegistry& registry, size_t sceneIndex) {
    std::vector<rs::world::TrainId> trainIds;
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Car || !rs::world::IsUserPlacement(entity) ||
            rs::world::GuidIsNil(entity.trainId) || entity.placementSceneIndex != sceneIndex) continue;
        if (std::find(trainIds.begin(), trainIds.end(), entity.trainId) == trainIds.end()) {
            trainIds.push_back(entity.trainId);
        }
    }
    return trainIds;
}

std::vector<size_t> DeleteLayoutScene(size_t index, const std::vector<rs::world::TrainId>& trainIds,
                                      rs::editor::Editor& editor, rs::world::SceneRegistry& registry,
                                      CameraController& camera, std::vector<ImportedWireSpan>& wireSpans,
                                      LayoutSession& session, DeletedSceneBackup& backup) {
    EnsureLayoutScene(session, editor, registry, camera, wireSpans);
    const size_t count = session.scenes.size();
    if (count <= 1 || index >= count) return {};

    DeletedSceneBackup next;
    next.index = index;
    next.activeIndexBefore = session.activeIndex;
    next.sceneCountAfter = count - 1;
    next.trainIds = trainIds;

    // 消える車両を、SceneRegistry内の位置つきで控えてから消す(位置は消す前のもの)。
    const std::unordered_set<rs::world::TrainId, rs::world::GuidHash> doomed(trainIds.begin(), trainIds.end());
    std::vector<rs::world::Guid> carIds;
    const std::vector<rs::world::SceneEntity>& entities = registry.Entities();
    for (size_t position = 0; position < entities.size(); ++position) {
        const rs::world::SceneEntity& entity = entities[position];
        if (entity.category != rs::world::SceneEntityCategory::Car || !doomed.contains(entity.trainId)) continue;
        next.cars.emplace_back(position, entity);
        carIds.push_back(entity.guid);
    }
    for (const rs::world::Guid& guid : carIds) registry.RemoveEntity(guid);

    const std::vector<rs::world::WarpLink>& links = session.warpLinks.Links();
    for (size_t position = 0; position < links.size(); ++position) {
        if (links[position].a.sceneIndex == index || links[position].b.sceneIndex == index) {
            next.warps.emplace_back(position, links[position]);
        }
    }

    const bool deletingActive = session.activeIndex == index;
    if (deletingActive) {
        // 今のSceneのライブ状態(配置物を含む)をキャッシュへ戻し、ライブ側から外す。
        WriteBackActiveScene(session, editor, registry, camera, &wireSpans);
    } else {
        CopyBackActiveScene(session, editor, registry, camera, &wireSpans);
    }
    next.scene = std::move(session.scenes[index]);
    session.scenes.erase(session.scenes.begin() + index);

    std::vector<size_t> oldToNew(count);
    for (size_t scene = 0; scene < count; ++scene) {
        oldToNew[scene] = scene < index ? scene : (scene == index ? rs::world::kRemovedScene : scene - 1);
    }
    RemapSessionSceneIndices(session, registry, oldToNew);
    if (deletingActive) {
        // 消した位置に来る行へ(末尾なら1つ前へ)切り替える。
        session.activeIndex = std::min(index, session.scenes.size() - 1);
        ApplySceneCacheToLive(session.scenes[session.activeIndex], editor, registry, camera, wireSpans);
    } else {
        // 元に戻す履歴のコマンドはScene番号(ワープの端点)を持つので、番号が動いたあとは使えない。
        editor.stack.Clear();
    }
    backup = std::move(next);
    return oldToNew;
}

std::vector<size_t> RestoreLayoutScene(const DeletedSceneBackup& backup, rs::editor::Editor& editor,
                                       rs::world::SceneRegistry& registry, CameraController& camera,
                                       std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session) {
    const size_t count = session.scenes.size();
    if (count == 0 || count != backup.sceneCountAfter || backup.index > count ||
        session.activeIndex >= count || backup.activeIndexBefore > count) return {};

    CopyBackActiveScene(session, editor, registry, camera, &wireSpans);
    session.scenes.insert(session.scenes.begin() + backup.index, backup.scene);

    std::vector<size_t> oldToNew(count);
    for (size_t scene = 0; scene < count; ++scene) oldToNew[scene] = scene < backup.index ? scene : scene + 1;
    RemapSessionSceneIndices(session, registry, oldToNew);

    // ワープは、削除前の位置へ戻す(保存のたびに並びが変わらないようにする)。
    const std::vector<rs::world::WarpLink> kept = session.warpLinks.Links();
    session.warpLinks.Clear();
    size_t keptPosition = 0;
    size_t removedPosition = 0;
    const size_t total = kept.size() + backup.warps.size();
    for (size_t position = 0; position < total; ++position) {
        const bool isRemoved = removedPosition < backup.warps.size() && backup.warps[removedPosition].first == position;
        const rs::world::WarpLink& link = isRemoved ? backup.warps[removedPosition++].second : kept[keptPosition++];
        session.warpLinks.AddLink(link.a, link.b);
    }
    for (const std::pair<size_t, rs::world::SceneEntity>& car : backup.cars) {
        registry.RestoreEntityAt(car.first, car.second);
    }

    // 今のSceneを削除前に戻す(今のSceneを消していたなら、戻したSceneへ)。
    editor.stack.Clear();
    if (session.activeIndex != backup.activeIndexBefore) {
        SwitchLayoutSceneTo(backup.activeIndexBefore, editor, registry, camera, wireSpans, session);
    }
    return oldToNew;
}

} // namespace rs::app
