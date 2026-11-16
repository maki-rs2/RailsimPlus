//
// LayoutSession.cpp
//
#include "LayoutSession.h"

#include <algorithm>
#include <numeric>

#include "DefaultPluginCatalog.h"
#include "../../editor/tool/Editor.h"
#include "../scene/PlatformParentDriver.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/SceneIndexRemap.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {

// LayoutSceneCache 1件をEditor/SceneRegistry/CameraController/importedWireSpansへ反映する
// (Load時のアクティブSceneとSwitch時の遷移先Sceneで共有するコア処理)。
void ApplySceneCacheToLive(const LayoutSceneCache& cache, rs::editor::Editor& editor,
                           rs::world::SceneRegistry& sceneRegistry, CameraController& camera,
                           std::vector<ImportedWireSpan>& importedWireSpans) {
    // TrackGraph差し替え + Undo/Redoスタッククリア + revision更新
    // (EditorViewport.cppのneedRebuild検知をトリガー、Rs2SceneApplier.cppと同じ規約)。
    //
    // この差し替えにより
    // editor.graphのGuidは(GenerateGuidの再採番により)変わるが、走行中のtrain
    // (rs::sim::TrainRuntime)は「自分が今どのSceneに属しているか」をsceneIndexと
    // して持ち、そのSceneのgraphに対してtickする。ここでeditor.graphを差し替えても
    // Scene自体(session.scenes[i].graph)は破棄されないためそのSceneのGuidは生き
    // 続け、古いTrackPosition.edgeの再解決処理は不要になる。
    editor.graph = cache.graph;
    editor.stack.Clear();
    editor.manualOverlay = cache.overlay;

    sceneRegistry.RemoveByCategory(rs::world::SceneEntityCategory::Scenery);
    for (const rs::world::SceneEntity& entity : cache.manualScenery) {
        sceneRegistry.RestoreEntity(entity);
    }
    editor.NotifySceneReplaced();

    importedWireSpans = cache.wireSpans;

    // Cameraブロックが無いSceneへ切り替えた場合は現在のカメラ位置を変更しない
    // (カメラ情報が無ければ何もしない。ゼロ原点への意図しないジャンプを避ける)。
    if (cache.hasCamera) {
        camera.fly = cache.camera;
        camera.SnapToTarget();
    }
}

void WriteBackActiveScene(LayoutSession& session, const rs::editor::Editor& editor,
                          rs::world::SceneRegistry& sceneRegistry,
                          const CameraController& camera,
                          const std::vector<ImportedWireSpan>* importedWireSpans) {
    if (session.scenes.empty()) return;

    CopyBackActiveScene(session, editor, sceneRegistry, camera, importedWireSpans);
    LayoutSceneCache& cache = session.scenes[session.activeIndex];
    // 離れるSceneの駅は評価されなくなるので、親に追従するレールを最後の姿勢のまま残さない。
    ResetAllParentDeltas(cache.graph);
    // 駅の効果で付け替えたレール接続も、離れるSceneには持ち越さない。
    cache.graph.railLinkOverlay.Clear();
    for (const rs::world::SceneEntity& entity : cache.manualScenery) {
        sceneRegistry.RemoveEntity(entity.guid);
    }
}

void CopyBackActiveScene(LayoutSession& session, const rs::editor::Editor& editor,
                         const rs::world::SceneRegistry& sceneRegistry,
                         const CameraController& camera,
                         const std::vector<ImportedWireSpan>* importedWireSpans) {
    if (session.scenes.empty()) return;

    LayoutSceneCache& cache = session.scenes[session.activeIndex];
    cache.graph = editor.graph;
    cache.overlay = editor.manualOverlay;
    cache.camera = camera.fly;
    cache.hasCamera = true;
    if (importedWireSpans != nullptr) cache.wireSpans = *importedWireSpans;

    std::vector<rs::world::SceneEntity> manual;
    for (const rs::world::SceneEntity& entity : sceneRegistry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        if (!rs::world::IsUserPlacement(entity)) continue;
        // 運行に合わせた一時的なスイッチ値は持ち出さない。
        manual.push_back(entity);
        manual.back().switchOptions = sceneRegistry.SavedSwitchOptions(entity);
    }
    cache.manualScenery = std::move(manual);
}

std::string MakeNewSceneName(const std::vector<LayoutSceneCache>& scenes) {
    size_t number = scenes.size() + 1;
    for (;;) {
        const std::string name = "シーン(" + std::to_string(number) + ")";
        bool used = false;
        for (const LayoutSceneCache& scene : scenes) {
            if (scene.name == name) { used = true; break; }
        }
        if (!used) return name;
        ++number;
    }
}

void EnsureLayoutScene(LayoutSession& session, const rs::editor::Editor& editor,
                       const rs::world::SceneRegistry& registry, const CameraController& camera,
                       const std::vector<ImportedWireSpan>& wireSpans) {
    if (!session.scenes.empty()) return;
    LayoutSceneCache cache;
    if (rs::world::GuidIsNil(session.liveSceneGuid)) session.liveSceneGuid = rs::world::GenerateGuid();
    cache.guid = session.liveSceneGuid;
    cache.name = MakeNewSceneName(session.scenes);
    session.scenes.push_back(std::move(cache));
    session.activeIndex = 0;
    CopyBackActiveScene(session, editor, registry, camera, &wireSpans);
}

size_t SwitchLayoutScene(int direction, rs::editor::Editor& editor, rs::world::SceneRegistry& sceneRegistry,
                         CameraController& camera, std::vector<ImportedWireSpan>& importedWireSpans,
                         LayoutSession& session) {
    EnsureLayoutScene(session, editor, sceneRegistry, camera, importedWireSpans);

    const long long n = static_cast<long long>(session.scenes.size());
    long long next = static_cast<long long>(session.activeIndex) + direction;
    next = ((next % n) + n) % n;  // ラップアラウンド(C++の%は負値を返しうるため補正)
    return SwitchLayoutSceneTo(static_cast<size_t>(next), editor, sceneRegistry, camera,
                               importedWireSpans, session);
}

size_t SwitchLayoutSceneTo(size_t nextIndex, rs::editor::Editor& editor, rs::world::SceneRegistry& sceneRegistry,
                           CameraController& camera, std::vector<ImportedWireSpan>& importedWireSpans,
                           LayoutSession& session) {
    EnsureLayoutScene(session, editor, sceneRegistry, camera, importedWireSpans);
    if (nextIndex >= session.scenes.size() || nextIndex == session.activeIndex) return session.activeIndex;

    WriteBackActiveScene(session, editor, sceneRegistry, camera, &importedWireSpans);

    session.activeIndex = nextIndex;
    ApplySceneCacheToLive(session.scenes[nextIndex], editor, sceneRegistry, camera,
                          importedWireSpans);
    return session.activeIndex;
}

void RemapSessionSceneIndices(LayoutSession& session, rs::world::SceneRegistry& registry,
                              const std::vector<size_t>& oldToNew) {
    // 今のSceneが消える場合は動かさない(呼び出し側が、切り替え先を決めて入れる)。
    const size_t active = rs::world::RemapSceneIndex(oldToNew, session.activeIndex);
    if (!rs::world::IsRemovedScene(active)) session.activeIndex = active;
    const std::vector<rs::world::WarpLink> links = session.warpLinks.Links();
    session.warpLinks.Clear();
    for (const rs::world::WarpLink& link : links) {
        const size_t sceneA = rs::world::RemapSceneIndex(oldToNew, link.a.sceneIndex);
        const size_t sceneB = rs::world::RemapSceneIndex(oldToNew, link.b.sceneIndex);
        if (rs::world::IsRemovedScene(sceneA) || rs::world::IsRemovedScene(sceneB)) continue;
        session.warpLinks.AddLink({sceneA, link.a.node}, {sceneB, link.b.node});
    }
    for (const rs::world::SceneEntity& entity : registry.Entities()) {
        if (entity.category != rs::world::SceneEntityCategory::Car) continue;
        registry.UpdatePlacementWithScene(entity.guid, entity.placement,
                                          rs::world::RemapSceneIndex(oldToNew, entity.placementSceneIndex));
    }
}

bool RenameLayoutScene(size_t index, const std::string& name, rs::editor::Editor& editor,
                       rs::world::SceneRegistry& registry, CameraController& camera,
                       std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session) {
    EnsureLayoutScene(session, editor, registry, camera, wireSpans);
    if (name.empty() || index >= session.scenes.size()) return false;
    if (session.scenes[index].name == name) return false;
    session.scenes[index].name = name;
    return true;
}

bool AddLayoutScene(const std::string& surfaceFolderUtf8, rs::editor::Editor& editor,
                    rs::world::SceneRegistry& registry, CameraController& camera,
                    std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session) {
    if (surfaceFolderUtf8.empty()) return false;
    // ライブ状態だけのセッションは、まずライブのSceneをキャッシュにして残す。
    EnsureLayoutScene(session, editor, registry, camera, wireSpans);
    // 現在のSceneのライブ状態をキャッシュへ戻してから末尾へ足す(切替側で二重に書き戻さない)。
    WriteBackActiveScene(session, editor, registry, camera, &wireSpans);
    LayoutSceneCache newScene;
    newScene.guid = rs::world::GenerateGuid();
    newScene.name = MakeNewSceneName(session.scenes);
    // 配置物のguidをシーンごとに一意にして、他のSceneの配置物と衝突させない。
    newScene.manualScenery = MakeSurfaceEntitiesFromFolder(surfaceFolderUtf8, "Surface",
                                                           rs::world::GuidToHex(newScene.guid));
    session.scenes.push_back(std::move(newScene));
    session.activeIndex = session.scenes.size() - 1;
    ApplySceneCacheToLive(session.scenes.back(), editor, registry, camera, wireSpans);
    return true;
}

bool SetLayoutSceneEnv(size_t index, const std::string& envPlugin, const std::string& envFolderUtf8,
                       rs::editor::Editor& editor, rs::world::SceneRegistry& registry, CameraController& camera,
                       std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session) {
    EnsureLayoutScene(session, editor, registry, camera, wireSpans);
    if (index >= session.scenes.size()) return false;
    LayoutSceneCache& scene = session.scenes[index];
    if (scene.envPlugin == envPlugin) return false;
    scene.envPlugin = envPlugin;
    scene.envFolderUtf8 = envFolderUtf8;
    return true;
}

std::vector<size_t> ReorderLayoutScene(size_t source, size_t destination, rs::editor::Editor& editor,
                                      rs::world::SceneRegistry& registry, CameraController& camera,
                                      std::vector<ImportedWireSpan>& wireSpans, LayoutSession& session) {
    EnsureLayoutScene(session, editor, registry, camera, wireSpans);
    const size_t count = session.scenes.size();
    if (source >= count || destination >= count || source == destination) return {};
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), size_t{0});
    order.erase(order.begin() + source);
    order.insert(order.begin() + destination, source);
    std::vector<size_t> oldToNew(count);
    for (size_t index = 0; index < count; ++index) oldToNew[order[index]] = index;

    // 今のシーンは変わらないので、ライブ状態は入れ直さず、保存用にキャッシュへ複写するだけにする(カメラを保つ)。
    CopyBackActiveScene(session, editor, registry, camera, &wireSpans);
    LayoutSceneCache moved = std::move(session.scenes[source]);
    session.scenes.erase(session.scenes.begin() + source);
    session.scenes.insert(session.scenes.begin() + destination, std::move(moved));
    RemapSessionSceneIndices(session, registry, oldToNew);
    // 元に戻す履歴のワープのコマンドはScene番号(端点)を持つので、番号が動いたあとは使えない。
    editor.stack.Clear();
    return oldToNew;
}

} // namespace rs::app
