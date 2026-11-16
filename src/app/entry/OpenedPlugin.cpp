#include "OpenedPlugin.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <memory>

#include <spdlog/spdlog.h>

#include "AppRuntime.h"
#include "../plugin_session/StartupAssetLoad.h"
#include "../plugin_session/ViewerState.h"
#include "../../platform/text/Encoding.h"
#include "../plugin_browser/PluginThumbnailRailMesh.h"
#include "../../world/scene/ScenePlacement.h"

namespace rs::app {

namespace {

// 一時的な配置物を外す(外から渡した表示メッシュの登録も外す)。
void RemoveOpenedEntity(AppRuntimeState& state) {
    OpenedPluginState& opened = state.openedPlugin;
    if (!rs::world::GuidIsNil(opened.entityId)) {
        state.sceneRenderer.EraseExternalMesh(opened.entityId);
        state.sceneRegistry.RemoveEntity(opened.entityId);
    }
    opened.entityId = rs::world::NilGuid();
}

// 定義ファイルの指定(フルパス)から、フォルダを含まないファイル名を取り出す。指定が無ければ空。
std::string DefinitionFileNameOf(const std::string& forceDefFileUtf8) {
    if (forceDefFileUtf8.empty()) return std::string();
    const std::filesystem::path path(rs::platform_text::Utf8ToWide(forceDefFileUtf8));
    return rs::platform_text::WideToUtf8(path.filename().wstring());
}

// アセンブリを持たないプラグイン(押し出し表示・パーツだけのプラグイン)の表示メッシュを作る。
// アセンブリを持つ・読めない・出せるものが無いときは false。読込はスロットの読込と同じキャッシュを使う。
bool BuildDisplayMeshWithoutAssembly(AppRuntimeState& state, const PluginSource& source,
                                     plugin_browser::ViewerDisplayMesh& out) {
    const std::shared_ptr<const rs::legacy_plugin::LoadedPlugin> parsed =
        state.sceneRenderer.EnsureParsedPlugin(source);
    if (!parsed->ok || parsed->hasAssembly) return false;
    return plugin_browser::BuildViewerDisplayMesh(*parsed, out);
}

// スロットが決まって最初の1回だけの取り付け。--switch指定とビューアーの初期スイッチを書き、
// 稀な漢字を含むフォントアトラスの組み直しを要求する。
void AttachOpenedSession(AppRuntimeState& state, PluginSession& session) {
    ApplyCliSwitchOverrides(session, state.cli);
    if (state.viewerMode) ApplyViewerInitialSwitches(state.viewer, session);
    state.openedPlugin.fontRebuildNeeded = true;
}

} // namespace

void OpenPlugin(AppRuntimeState& state, const std::string& folderUtf8) {
    RemoveOpenedEntity(state);
    OpenedPluginState& opened = state.openedPlugin;
    opened = OpenedPluginState{};
    opened.folderUtf8 = folderUtf8;
    // ビューアーの日付・時刻・季節は設定の値のまま保つので、スロットのTickに書かせない。
    if (state.viewerMode) state.sceneRenderer.SetWriteCalendarForTemporary(false);

    rs::world::SceneEntity entity;
    entity.folderUtf8 = folderUtf8;
    entity.definitionFile = DefinitionFileNameOf(state.forceDefFile);
    entity.category = rs::world::SceneEntityCategory::Scenery;
    entity.isTemporary = true;
    entity.placement = rs::world::FreePlacement{rs::core::Vec3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f};

    plugin_browser::ViewerDisplayMesh display;
    const bool hasDisplayMesh =
        BuildDisplayMeshWithoutAssembly(state, PluginSource(folderUtf8, entity.definitionFile), display);

    opened.entityId = state.sceneRegistry.AddEntity(std::move(entity));
    opened.mode = OpenedPluginState::Mode::Slot;
    if (!hasDisplayMesh) return;
    if (display.extruded) {
        opened.hasFocusBounds = true;
        std::copy(std::begin(display.focusMin), std::end(display.focusMin), std::begin(opened.focusMin));
        std::copy(std::begin(display.focusMax), std::end(display.focusMax), std::begin(opened.focusMax));
    }
    state.sceneRenderer.SetExternalMesh(opened.entityId,
                                        std::make_shared<const rs::render::Mesh>(std::move(display.mesh)));
}

PluginSession* FindOpenedPluginSession(AppRuntimeState& state) {
    const OpenedPluginState& opened = state.openedPlugin;
    switch (opened.mode) {
    case OpenedPluginState::Mode::Slot:
        return state.sceneRenderer.FindSessionByEntityId(opened.entityId);
    case OpenedPluginState::Mode::None:
        break;
    }
    return nullptr;
}

void TickOpenedPlugin(AppRuntimeGlobals& globals, AppRuntimeState& state) {
    OpenedPluginState& opened = state.openedPlugin;
    if (opened.mode != OpenedPluginState::Mode::Slot) return;

    SceneSlotPool::Slot* slot = state.sceneRenderer.FindSlotForEntity(opened.entityId);
    if (slot == nullptr || !slot->session.plugin.ok) {
        // 読込ジョブが終わっても載らなかった(読込失敗・出せるメッシュが無い等)なら、失敗として警告を
        // 1回だけ出す。画面は空のまま(他の配置物の失敗と同じ扱い)。
        if (!opened.failureReported && !state.sceneRenderer.SyncJobActive() &&
            state.sceneRenderer.IsEntityPlacementFailed(opened.entityId)) {
            spdlog::warn("[plugin] 開いたプラグインを情景スロットに載せられませんでした: {}", opened.folderUtf8);
            opened.failureReported = true;
        }
        return;
    }
    PluginSession& session = slot->session;

    if (opened.attachedEntityId != opened.entityId || opened.attachedGeneration != slot->residency.generation) {
        AttachOpenedSession(state, session);
        opened.attachedEntityId = opened.entityId;
        opened.attachedGeneration = slot->residency.generation;
        opened.cameraFitted = false;
    }

    // 当たり判定の箱は最初のTickで決まる。決まったら、プラグイン全体が収まる位置へカメラを合わせる。
    // 押し出し表示は、直線全体ではなく中央の短い区間に合わせる。
    if (!opened.cameraFitted && slot->sceneryPick.sceneryPickHasAabb) {
        if (opened.hasFocusBounds) {
            globals.camera.FitToAabb(opened.focusMin, opened.focusMax);
        } else {
            globals.camera.FitToAabb(slot->sceneryPick.sceneryPickAabbMin, slot->sceneryPick.sceneryPickAabbMax);
        }
        globals.camera.SnapToTarget();
        opened.cameraFitted = true;
    }
}

} // namespace rs::app
