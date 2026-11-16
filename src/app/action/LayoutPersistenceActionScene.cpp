//
// LayoutPersistenceActionScene.cpp - LayoutPersistenceActionのScene操作(切替・名前・並べ替え・一覧)
//
// 読込中の確認と、ライブ状態を入れ替えたあとの後始末は、どの操作もここの2関数を通す。
//
#include "LayoutPersistenceAction.h"

#include "../../editor/tool/Editor.h"
#include "../../editor/tool/WarpLinkAuthoring.h"
#include "../../world/scene/SceneRegistry.h"
#include "../plugin_session/ActiveEnv.h"
#include "../plugin_session/PluginRootResolver.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {

bool LayoutPersistenceAction::PrepareSceneOperation() const {
    // 読込ジョブ進行中はScene操作も一時停止する(registryを変更する操作の一つ)。
    return !IsLoading();
}

void LayoutPersistenceAction::FinishSceneSwitch(bool markEdited) {
    refs_.sceneRenderer.ResetTrainWarpFade();
    // 切替はライブ状態を入れ替えるので、revisionが動いた分は基準へ取り込む。
    refs_.document.Rebase(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision());
    if (markEdited) refs_.document.MarkEdited();
}

void LayoutPersistenceAction::SwitchScene(int delta) {
    if (!PrepareSceneOperation()) return;
    // 切替そのものは編集ではないが、切替前の未保存の変更は残っているので、未保存の状態を引き継ぐ。
    const bool wasDirty = DocumentViewModel().dirty;
    SwitchLayoutScene(delta, refs_.editor, refs_.sceneRegistry, refs_.camera, refs_.importedWireSpans,
                      refs_.layoutSession);
    FinishSceneSwitch(wasDirty);
}

void LayoutPersistenceAction::SwitchSceneTo(size_t index) {
    if (!PrepareSceneOperation()) return;
    const bool wasDirty = DocumentViewModel().dirty;
    SwitchLayoutSceneTo(index, refs_.editor, refs_.sceneRegistry, refs_.camera, refs_.importedWireSpans,
                        refs_.layoutSession);
    FinishSceneSwitch(wasDirty);
}

void LayoutPersistenceAction::RenameScene(size_t index, const std::string& name) {
    if (!PrepareSceneOperation()) return;
    if (RenameLayoutScene(index, name, refs_.editor, refs_.sceneRegistry, refs_.camera,
                          refs_.importedWireSpans, refs_.layoutSession)) {
        deletedScene_.reset();
        refs_.document.MarkEdited();
    }
}

void LayoutPersistenceAction::ReorderScene(size_t source, size_t destination) {
    if (!PrepareSceneOperation()) return;
    const std::vector<size_t> oldToNew =
        ReorderLayoutScene(source, destination, refs_.editor, refs_.sceneRegistry, refs_.camera,
                           refs_.importedWireSpans, refs_.layoutSession);
    if (oldToNew.empty()) return;
    deletedScene_.reset();
    // LayoutSessionの外でScene番号を持つ状態(走行系・ワープ端点の保留)も付け替える。
    refs_.sceneRenderer.RemapTrainSceneIndices(oldToNew);
    refs_.warpAuthoring.RemapScenes(oldToNew);
    FinishSceneSwitch(true);
}

bool LayoutPersistenceAction::SetSceneEnvironment(size_t index, const std::string& envPlugin) {
    if (!PrepareSceneOperation()) return false;
    // フォルダは、.rs2/.rslの読込と同じ解決(ResolveEnvPluginFolder)で決める。
    const std::string folderUtf8 = ResolveEnvPluginFolder(PluginRootCandidates(), envPlugin);
    if (!SetLayoutSceneEnv(index, envPlugin, folderUtf8, refs_.editor, refs_.sceneRegistry, refs_.camera,
                           refs_.importedWireSpans, refs_.layoutSession)) {
        return false;
    }
    deletedScene_.reset();
    refs_.document.MarkEdited();
    return true;
}

void LayoutPersistenceAction::RescanSceneChoices() { scenePanelSource_.Rescan(PluginRootCandidates()); }

bool LayoutPersistenceAction::FillScenePanel(workspace::ScenePanelPresentation& panel) {
    // 走査はフォルダを読むので、画面を開くまで(起動直後は)しない。
    if (panel.active_ && !scenePanelSource_.Scanned()) RescanSceneChoices();
    return FillScenePanelFromSession(panel, refs_.layoutSession, refs_.sceneRegistry, scenePanelSource_);
}

}  // namespace rs::app
