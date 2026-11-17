#include "RmlUiPluginBrowserView.h"

#include <optional>
#include <format>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Traits.h>

#include <spdlog/spdlog.h>

#include "RmlUiBrowserGridDriver.h"
#include "RmlUiFieldCommit.h"
#include "RmlUiBrowserHoverText.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiPluginBrowserInternal.h"
#include "RmlUiPluginBrowserModel.h"
#include "RmlUiUpdateVariable.h"
#include "RmlUiNumberParse.h"
#include "../action/PluginBrowserCardSizeAction.h"
#include "../action/PluginFavoriteAction.h"
#include "../action/SceneryTransformValues.h"
#include "../action/SceneSignalAspectSource.h"
#include "../action/SceneSignalSidecarSource.h"
#include "../action/SceneryPlacementAction.h"
#include "../action/SignalBlockAction.h"
#include "../action/ToastQueue.h"
#include "../action/TrackPluginSetAction.h"
#include "../action/TrackPluginSetText.h"
#include "../action/TrackLayingAction.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_session/PluginSelector.h"
#include "../settings/UserSettings.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../editor/tool/Editor.h"

namespace rs::app::rmlui {

using rs::app::plugin_browser::BrowserCardAction;
using rs::app::plugin_browser::EntryId;

// 敷設プラグインのブラウザで開いている種別に合わせて、配置物(橋脚・架線柱)の追加/移動モードと
// 架線の接続/切断モードを切り替える。橋脚・架線柱の種別なら前者、架線なら後者、それ以外は両方オフ。
// 同時にはどちらか一方だけ(柱を扱う間は架線の追加・削除はできず、架線を扱う間は柱を選べない)。
void RmlUiPluginBrowserView::EnterTrackPlacementMode(std::optional<plugin_browser::BrowserKind> kind) {
    if (editor_ == nullptr) return;
    const bool pillar = kind == plugin_browser::BrowserKind::Pier || kind == plugin_browser::BrowserKind::Pole;
    const bool wire = kind == plugin_browser::BrowserKind::Line;
    if (pillar || wire) editor_->sectionSplitMode = false;
    if (!pillar) {
        editor_->placementAddMode = false;
        editor_->ClearSelectedPlacement();
        editor_->hoveredPlacementEdge = rs::world::NilGuid();
        editor_->hoveredPlacementFolder.clear();
    }
    if (!wire) {
        editor_->wireEditMode = false;
        editor_->wireFirstPoint.reset();
        editor_->ClearWireSpanSelection();
    }
    if (pillar) editor_->placementAddMode = true;
    if (wire && !editor_->wireEditMode) editor_->wireEditMode = true;
}

// タブを開いたとき。敷設プラグインのブラウザだけ、開いた種別に合わせてモードを切り替える。
// 種別を変えたら、前の種別で選んでいたノードの選択も外す。
void RmlUiPluginBrowserView::ApplyTabPlacementMode(const plugin_browser::BrowserTab& tab) {
    if (plugin_browser::GetBrowserProfile(service_->State().Profile()).cardAction !=
        BrowserCardAction::ToggleSet) {
        return;
    }
    if (editor_ != nullptr) editor_->ClearSelectedNode();
    EnterTrackPlacementMode(tab.favorites ? std::nullopt : std::optional(tab.kind));
}

void RmlUiPluginBrowserView::OnPlace(const Rml::VariantList& arguments) {
    if (service_ == nullptr) return;
    // 列車ではカードのドラッグで編成へ落とすので、クリックでは何もしない。
    const BrowserCardAction cardAction = plugin_browser::GetBrowserProfile(service_->State().Profile()).cardAction;
    if (cardAction == BrowserCardAction::DragToFormation) return;
    const std::optional<EntryId> id = ReadEntryId(arguments, *service_);
    if (cardAction == BrowserCardAction::ToggleSet && id.has_value()) {
        const plugin_browser::PluginCatalogEntry& entry = service_->Catalog().Entries()[*id];
        TrackLayingAction action(*editor_, *preview_);
        if (entry.kind == plugin_browser::BrowserKind::Pier || entry.kind == plugin_browser::BrowserKind::Pole) {
            action.SetPlacementAddFolder(entry.folderUtf8);
            EnterTrackPlacementMode(entry.kind);
        } else if (entry.kind == plugin_browser::BrowserKind::Line) {
            // 選んだ架線を保持する。以降の接続はこの架線で張る。
            editor_->wireLineFolder = entry.folderUtf8;
            EnterTrackPlacementMode(entry.kind);
        } else {
            // レール・枕木・桁は、カード本体を押しても敷設プラグインセットへ足す・外す。
            ToggleSetMembership(*id);
        }
        return;
    }
    if (cardAction != BrowserCardAction::Place && cardAction != BrowserCardAction::SelectSignal) return;
    if (!id.has_value()) return;
    operationError_.clear();
    const std::string& folder = service_->Catalog().Entries()[*id].folderUtf8;
    if (cardAction == BrowserCardAction::SelectSignal) {
        SceneSignalSidecarSource sidecarSource(*sceneRenderer_);
        SceneSignalAspectSource aspectSource(*sceneRenderer_);
        SignalBlockAction action(*editor_, sidecarSource, aspectSource);
        // 配置待ち中のカードをもう一度押すと、配置待ちをやめる(情景と同じ)。
        if (editor_->signalBlockArmedAction == rs::editor::SignalBlockArmedAction::PlaceSpeedSignal &&
            editor_->signalPluginFolder == folder) {
            action.Disarm();
            return;
        }
        operationError_ = action.SelectSignalPlugin(folder);
        if (!operationError_.empty()) return;
        // 閉塞の境界に立つ信号は担当の閉塞が無いと置けない。配置待ちの間はレールのクリックが配置に回り、
        // 閉塞を作るためのレール選択ができなくなるので、閉塞が1つも無いうちは配置待ちにせず案内だけ出す。
        if (editor_->graph.blocks.empty() && rs::world::IsBlockBoundarySignal(editor_->signalKindToPlace)) {
            if (toastQueue_ != nullptr)
                toastQueue_->Post(ToastSeverity::Info, "先にレールを選んで閉塞を作ってください");
            return;
        }
        action.ArmPlaceSpeedSignal();
        return;
    }
    // 配置プレビュー中のカードをもう一度押すと、選択を解除する(配置を終える)。
    SceneryPlacementAction placement(*editor_);
    if (placement.IsPlacing(folder)) {
        placement.End();
        return;
    }
    SceneryPlacementOptions options;
    options.continuous = continuousPlacement_ != nullptr && *continuousPlacement_;
    placement.Begin(folder, options);
}

void RmlUiPluginBrowserView::RefreshSceneryTransform() {
    if (sceneryTransformInputFocused_ || editor_ == nullptr || sceneRegistry_ == nullptr) return;
    std::vector<rs::world::SceneEntity> selected;
    for (const rs::world::Guid& id : editor_->SelectedSceneEntities()) {
        const rs::world::SceneEntity* entity = sceneRegistry_->FindEntity(id);
        if (entity != nullptr) selected.push_back(*entity);
    }
    const rs::app::SceneryTransformValues fields = rs::app::CollectSceneryTransformValues(selected);
    if (!fields.hasValue) return;
    const std::array<const char*, 6> keys{kSceneryXKey, kSceneryYKey, kSceneryZKey,
                                         kSceneryYawKey, kSceneryPitchKey, kSceneryRollKey};
    const std::array<std::string*, 6> texts{&model_->sceneryX, &model_->sceneryY, &model_->sceneryZ,
                                           &model_->sceneryYaw, &model_->sceneryPitch, &model_->sceneryRoll};
    for (size_t i = 0; i < keys.size(); ++i) {
        UpdateVariable(modelHandle_, keys[i], *texts[i],
                       fields.mixed[i] ? kMixedValue : std::format("{:.3f}", fields.values[i]));
    }
}

void RmlUiPluginBrowserView::OnCommitSceneryTransform(Rml::Event& event, const Rml::VariantList& arguments) {
    if (editor_ == nullptr || sceneRegistry_ == nullptr) return;
    sceneryTransformInputFocused_ = false;
    Rml::ElementFormControl* const control = rmlui_dynamic_cast<Rml::ElementFormControl*>(event.GetCurrentElement());
    if (control == nullptr) return;
    // Esc で取り消した欄は確定せず、表示を今の値に戻す。
    if (ConsumeFieldCancel(control)) {
        RefreshSceneryTransform();
        return;
    }
    float value = 0.0f;
    if (!ParseFloatText(control->GetValue(), value)) {
        RefreshSceneryTransform();
        return;
    }
    const Rml::String field = FirstEventArgument<Rml::String>(arguments, Rml::String());
    std::vector<rs::world::SceneEntity> before;
    for (const rs::world::Guid& id : editor_->SelectedSceneEntities()) {
        const rs::world::SceneEntity* entity = sceneRegistry_->FindEntity(id);
        if (entity == nullptr || entity->category != rs::world::SceneEntityCategory::Scenery ||
            std::get_if<rs::world::FreePlacement>(&entity->placement) == nullptr) continue;
        before.push_back(*entity);
        rs::world::FreePlacement placement = std::get<rs::world::FreePlacement>(entity->placement);
        if (field == "x") placement.position.x = value;
        else if (field == "y") placement.position.y = value;
        else if (field == "z") placement.position.z = value;
        else if (field == "yaw") placement.yawDeg = value;
        else if (field == "pitch") placement.pitchDeg = value;
        else if (field == "roll") placement.rollDeg = value;
        else continue;
        sceneRegistry_->CommitPlacement(id, placement);
    }
    editor_->CommitSceneEntityTransform(*sceneRegistry_, before);
    RefreshSceneryTransform();
}

} // namespace rs::app::rmlui
