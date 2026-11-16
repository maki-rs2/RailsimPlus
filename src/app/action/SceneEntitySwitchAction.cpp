#include "SceneEntitySwitchAction.h"

#include <utility>

#include "GroupCommonSwitchSync.h"
#include "SceneSignalAspectSource.h"
#include "SceneSignalSidecarSource.h"
#include "SignalBlockAction.h"
#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {

SceneEntitySwitchAction::SceneEntitySwitchAction(rs::world::Guid targetEntityId,
                                                 rs::world::SceneRegistry& sceneRegistry,
                                                 SceneRenderer& sceneRenderer,
                                                 rs::editor::Editor* editor)
    : targetEntityId_(std::move(targetEntityId)),
      sceneRegistry_(sceneRegistry),
      sceneRenderer_(sceneRenderer),
      editor_(editor) {}

bool SceneEntitySwitchAction::TargetExists(const rs::world::Guid& entityId,
                                           const rs::world::SceneRegistry& sceneRegistry,
                                           const SceneRenderer& sceneRenderer) {
    return sceneRegistry.FindEntity(entityId) != nullptr ||
           sceneRenderer.SignalModels().FindEntity(entityId) != nullptr;
}

std::optional<rs::world::Guid> SceneEntitySwitchAction::TargetSignalGuid() const {
    if (editor_ == nullptr || rs::world::GuidIsNil(targetEntityId_)) return std::nullopt;
    return sceneRenderer_.SignalModels().FindSignalGuid(targetEntityId_);
}

void SceneEntitySwitchAction::SetSwitch(std::size_t index, std::int32_t value) {
    if (rs::world::GuidIsNil(targetEntityId_)) {
        return;
    }
    const std::optional<rs::world::Guid> signalGuid = TargetSignalGuid();
    if (signalGuid.has_value()) {
        SceneSignalSidecarSource sidecarSource(sceneRenderer_);
        SceneSignalAspectSource aspectSource(sceneRenderer_);
        SignalBlockAction(*editor_, sidecarSource, aspectSource).SetSpeedSignalSwitch(*signalGuid, index, value);
        return;
    }
    const rs::world::SceneEntity* entity = sceneRegistry_.FindEntity(targetEntityId_);
    if (entity == nullptr) {
        return;
    }
    std::vector<std::string> log;
    const std::vector<rs::legacy_pi::SwitchDef>* switches =
        sceneRenderer_.TryGetSwitchDefs(entity->folderUtf8, log);
    if (switches != nullptr && index < switches->size()) {
        value = ClampSwitchValue((*switches)[index], value);
    }
    sceneRegistry_.SetSwitchOption(targetEntityId_, index, value);

    // 編成(同一trainId)内でGroupCommonを共有する他スイッチへ値を伝播する
    // (RailSim II 互換)。Car以外・GroupCommon未設定なら何もしない。
    SceneRenderer& sceneRenderer = sceneRenderer_;
    PropagateGroupCommonSwitch(
        sceneRegistry_,
        [&sceneRenderer](const std::string& folderUtf8) {
            std::vector<std::string> lookupLog;
            return sceneRenderer.TryGetSwitchDefs(folderUtf8, lookupLog);
        },
        targetEntityId_, index, value);
}

SceneEntitySwitchViewModel SceneEntitySwitchAction::ViewModel() const {
    SceneEntitySwitchViewModel viewModel;
    if (rs::world::GuidIsNil(targetEntityId_)) {
        return viewModel;
    }
    const std::optional<rs::world::Guid> signalGuid = TargetSignalGuid();
    if (signalGuid.has_value()) {
        const rs::world::SceneEntity* signalEntity = sceneRenderer_.SignalModels().FindEntity(targetEntityId_);
        viewModel.visible = true;
        viewModel.entityId = targetEntityId_;
        viewModel.folderUtf8 = signalEntity->folderUtf8;
        SceneSignalSidecarSource sidecarSource(sceneRenderer_);
        SceneSignalAspectSource aspectSource(sceneRenderer_);
        viewModel.rows = SignalBlockAction(*editor_, sidecarSource, aspectSource).SwitchRowsOfSignal(*signalGuid);
        return viewModel;
    }
    const rs::world::SceneEntity* entity = sceneRegistry_.FindEntity(targetEntityId_);
    if (entity == nullptr) {
        return viewModel;
    }
    viewModel.visible = true;
    viewModel.entityId = entity->guid;
    viewModel.folderUtf8 = entity->folderUtf8;
    std::vector<std::string> log;
    const std::vector<rs::legacy_pi::SwitchDef>* switches =
        sceneRenderer_.TryGetSwitchDefs(viewModel.folderUtf8, log);
    if (switches == nullptr) {
        return viewModel;
    }
    viewModel.rows = BuildSwitchRows(
        *switches, [entity](std::size_t index, const rs::legacy_pi::SwitchDef&) -> std::int32_t {
            return index < entity->switchOptions.size() ? entity->switchOptions[index] : 0;
        });
    return viewModel;
}

} // namespace rs::app
