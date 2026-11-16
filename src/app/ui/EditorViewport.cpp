#include "EditorViewport.h"

#include <iterator>

#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneRegistry.h"
#include "../action/EditToolScope.h"
#include "../action/TrackLayingAction.h"
#include "../gpu/CameraMath.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/RailTieHeights.h"
#include "../scene/SceneRenderer.h"
#include "../scene/ParsedPluginCache.h"
#include "../scene/StructLinePoleTracker.h"
#include "../scene/SignalModelSet.h"
#include "../../sim/SignalAspectRuntime.h"
#include "TrainPlacementInput.h"
#include "ViewportPluginSettingsTarget.h"
#include "ViewportInput.h"
#include "ViewportRailMeshBuild.h"
#include "ViewportOverlay.h"
#include "ViewportPlacementInstances.h"
#include "ViewportSceneryInstances.h"

namespace rs::app {
void HandleStationDiaInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                           rs::app::input::PointerPos mouse, bool enabled);
void DrawStationDiaOverlay(const EditorViewportContext& ctx);
void HandleSignalBlockInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse, bool enabled);
void DrawSignalBlockOverlay(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr, rs::app::input::PointerPos mouse);
}

namespace rs::app {

namespace {

// 信号プラグインのモデルが引くもの(station.json・スイッチ定義・現示)を、SceneRendererから結ぶ。
SignalModelSources MakeSignalModelSources(const EditorViewportContext& ctx) {
    SignalModelSources sources;
    sources.sidecar = [&ctx](const std::string& folderUtf8) -> const rs::legacy_pi::sidecar::StationSidecar& {
        return ctx.sceneRenderer.TryGetStationSidecar(folderUtf8);
    };
    sources.switchDefs = [&ctx](const std::string& folderUtf8) {
        return ctx.sceneRenderer.TryGetSwitchDefs(folderUtf8, ctx.log);
    };
    sources.displayStage = [&ctx](const rs::world::SpeedSignal& signal) {
        return rs::sim::ResolveSignalDisplay(&ctx.sceneRenderer.SignalAspects(), signal).stage;
    };
    return sources;
}

void RebuildViewportMeshes(const EditorViewportContext& ctx) {
    rs::editor::Editor& gEditor = ctx.editor;
    // --- メッシュ再構築(g_editor.revision変化 または レール系リストの
    //     entryRevision変化のいずれかで再構築する。プラグインリストに
    //     追加された瞬間に読み込み、敷設時に無条件でリスト内の全プラグインを
    //     重ねて配置する) ---
    const bool pluginsChanged = ctx.sceneRenderer.TakePluginCacheChange();
    const uint64_t curEntryRevision = ctx.selector.EntryRevision();
    const uint64_t curSceneRegistryRevision = ctx.sceneRegistry.Revision();
    // Structの架線接続点の柱は、情景の構成が変わったときだけ導出し直す。柱が実際に変わったときだけ
    // 架線メッシュを作り直す(情景の変更のたびに全再構築しない)。
    StructLinePoleTracker& linePoles = ctx.sceneRenderer.StructLinePoles();
    linePoles.Update(ctx.sceneRegistry, gEditor.graph);
    const bool linePolesChanged = linePoles.TakeChanged();
    const bool railDirty = pluginsChanged || gEditor.revision != ctx.meshRevision ||
                           curEntryRevision != ctx.lastEntryRevision || linePolesChanged;
    // シーンエンティティ側の変更(配置の増減・スイッチ確定値の変更等)はレール押し出しに
    // 影響しないので、レール再構築のトリガーから分離する。まとめて再構築すると
    // エンティティを1件触るたびに全edgeの押し出しが走る。
    const bool sceneDirty = curSceneRegistryRevision != ctx.lastSceneRegistryRevision;
    if (railDirty) {
        ctx.meshRevision = gEditor.revision;
        ctx.lastEntryRevision = curEntryRevision;
        RebuildExtrudedRailMesh(ctx);
    }
    // 信号プラグインのモデル。信号の追加・削除・移動、edgeの分割・結合、Scene切替、レイアウト読込は
    // いずれもg_editor.revisionが進むので、railDirtyのときに作り直す(直後に全再構築が走る)。
    // 現示だけが変わったときはrevisionを進めず、変わった信号のスイッチ変更だけを差分更新へ渡す。
    const SignalModelSources signalSources = MakeSignalModelSources(ctx);
    SignalModelSet& signalModels = ctx.sceneRenderer.SignalModels();
    if (railDirty) {
        signalModels.Rebuild(gEditor.graph, signalSources);
    } else {
        signalModels.UpdateAspects(signalSources);
    }
    std::vector<rs::world::SceneRegistry::SwitchChange> signalChanges =
        signalModels.TakePendingSwitchChanges();
    if (railDirty || sceneDirty || !signalChanges.empty()) {
        // 構造(配置の増減・置換)かレールが変わったときは全再構築。スイッチ値だけが変わった
        // ときは、変わった個体が属するバッチだけを差分で積み直す。差分更新が受け付けない
        // 条件(バンド管理のバリアント等)に当たったら全再構築へ戻す。
        const uint64_t curStructureRevision = ctx.sceneRegistry.StructureRevision();
        const bool structureChanged =
            railDirty || curStructureRevision != ctx.lastSceneStructureRevision;
        std::vector<rs::world::SceneRegistry::SwitchChange> switchChanges =
            ctx.sceneRegistry.TakePendingSwitchChanges();
        const std::vector<rs::world::SceneRegistry::PlacementChange> placementChanges =
            ctx.sceneRegistry.TakePendingPlacementChanges();
        switchChanges.insert(switchChanges.end(), std::make_move_iterator(signalChanges.begin()),
                             std::make_move_iterator(signalChanges.end()));
        ctx.lastSceneRegistryRevision = curSceneRegistryRevision;
        ctx.lastSceneStructureRevision = curStructureRevision;
        if (structureChanged || !ApplySwitchChangesToSceneryInstances(ctx, switchChanges) ||
            !ApplyPlacementChangesToSceneryInstances(ctx, placementChanges)) {
            RebuildDiscretePlacementInstances(ctx);
        }
    }
}

} // namespace

void TickEditorViewport(const EditorViewportContext& ctx) {
    rs::editor::Editor& gEditor = ctx.editor;
    gEditor.BindSceneRegistry(ctx.sceneRegistry);
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;

    rs::app::ViewProjResult vpr = rs::app::BuildViewProj(ctx.view, gCurWidth, gCurHeight);
    // マウス座標はウィンドウ外に出ても取得できてしまう(多ビューポート環境等)ため、
    // ここでビューポート内にクランプしてから各ホバー/ピック処理へ渡す。ここが
    // 唯一のmouse生成箇所で以降は全てこの値を使い回すため、他のクランプは不要。
    const rs::app::input::PointerPos mouse = ctx.input.Pointer();
    // 新規ノードの標高 = 基準面(Y=0)+手動調整分。
    // 敷設面の高さ: 基準(カメラ中心高(現モードのtarget高)に追従 or 0)+手動調整分。
    {
        const rs::editor::TrackLayingSettings& laying = gEditor.layingSettings;
        // 水平位置を決める基準面は常にY=0(カメラ中心高には追従させない)。
        gEditor.layingSettings.placeHeight = laying.placeHeightOffset;
        gEditor.layingSettings.placeRailTopLift =
            ComputeRailTieHeights(laying.defaultTrackPlugins, ctx.selector).TopLift();
    }
    // Surfaceの範囲(あれば、新規ノードと情景の配置位置をこの内側へ押し戻す)。
    gEditor.surfaceBounds = ctx.sceneRenderer.CurrentSurfaceBounds(ctx.sceneRegistry);

    // 敷設ツールはRailモード専用。他の表示モードへ移ったら必ず解除する。
    // 残すと、情景の配置プレビュー中もPlaceArcの入力が先に評価されて、情景でなくレールが置かれる。
    // ツール切替の後始末はSelectToolに集約している(経路を分けない)。
    if (!GetViewModeTraits(ctx.viewMode).trackEditing) {
        TrackLayingAction(gEditor, ctx.lastArcPreview).SelectTool(false);
    }

    // 敷設操作(クリック配置・選択・Undo/Redo・削除)はRailモードでのみ有効。
    // 他モード(Train等)では軌道の編集自体を行わせない。
    const bool editingEnabled = IsTrackEditingEnabled(ctx.viewMode, gEditor);

    // 表示モード・編集サブモードの切替でホバー/選択状態を一括解除する。
    // 個々の切替経路(ボタン押下・Escape等)
    // それぞれにクリア処理を書くと漏れが出てハイライトが残るため、ここで「表示モード+排他的な編集サブモード」
    // をまとめた署名を毎フレーム比較し、何であれ変化したら必ずこの1箇所で
    // クリアする(切替元/切替先の個別実装に依存しない経路にする)。
    const EditorModeSignature curModeSignature{
        ctx.viewMode,          gEditor.mode,
        gEditor.placementAddMode, gEditor.stationAnchorMode,
        gEditor.sectionSplitMode, gEditor.wireEditMode,
        gEditor.sceneryPlacementMode,
    };
    ctx.state.Synchronize(gEditor.SceneGeneration(), GetParsedPluginCache().Generation());
    if (ctx.state.UpdateMode(curModeSignature)) {
        gEditor.ClearSelectionAndHover();
    }

    // 前フレーム以降の変更を反映してから、描画と同じ一覧で選択する。
    RebuildViewportMeshes(ctx);
    HandleEditorInput(ctx, vpr, mouse, editingEnabled);
    // Undo/Redoは敷設・配置系と信号・閉塞のモードで共通(受け付けるモードはIsUndoRedoEnabledの1か所)。
    if (IsUndoRedoEnabled(ctx.viewMode, gEditor)) {
        if (ctx.input.Triggered(rs::app::input::InputAction::EditorUndo)) gEditor.Undo();
        if (ctx.input.Triggered(rs::app::input::InputAction::EditorRedo)) gEditor.Redo();
    }
    HandleStationDiaInput(ctx, vpr, mouse, GetViewModeTraits(ctx.viewMode).stationDia);
    HandleSignalBlockInput(ctx, vpr, mouse, IsSignalBlockEditingEnabled(ctx.viewMode));
    // Scenery(駅舎/施設等)選択とTrain(編成車両)ホバーハイライトはプラグイン種別で
    // 表示モードを分離し、Railモードのnode/edge/配置物選択とは
    // 排他的に扱う。
    HandleSceneEntitySelectionInput(
        ctx, vpr, mouse, GetViewModeTraits(ctx.viewMode).scenery && !gEditor.sceneryPlacementMode);
    const bool trainHovered =
        HandleTrainHoverInput(ctx, vpr, mouse, GetViewModeTraits(ctx.viewMode).train);
    HandleTrainPlacementInput(ctx, vpr, mouse, GetViewModeTraits(ctx.viewMode).train, trainHovered);
    SyncTrainPlacementPreview(ctx);
    RebuildViewportMeshes(ctx);
    DrawEditorOverlay(ctx, vpr, mouse.x, mouse.y);
    DrawStationDiaOverlay(ctx);
    DrawSignalBlockOverlay(ctx, vpr, mouse);
    DrawSceneEntityOverlay(ctx, vpr);
    // プラグイン設定パネルの受け渡し。有効かどうかはctx.pluginSettingsLinkの値だけで決まる。
    HandlePluginSettingsClick(ctx, vpr, mouse);
    DrawPluginSettingsTargetBox(ctx);

}

} // namespace rs::app
