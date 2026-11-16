//
// FormationAction.cpp - 編成パネルのアプリケーションサービス
//
#include "FormationAction.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/LayoutDocumentState.h"
#include "../plugin_session/TrainGroupTemplateApplier.h"
#include "../camera/CabinViewCamera.h"
#include "../camera/CameraControl.h"
#include "../scene/SceneRenderer.h"
#include "../ui/FormationPanel.h"
#include "../workspace/FormationCarSlots.h"
#include "../workspace/TrainInfoCardCatalog.h"

namespace rs::app {

FormationAction::FormationAction(const FormationRefs& refs) : refs_(refs) {}

void FormationAction::AddFormation() {
    refs_.selector.AddFormation("");
    refs_.document.MarkEdited();
}

void FormationAction::SelectFormation(std::size_t defIndex) {
    // 置いた列車がある編成も選べる(同じ編成を置き直せないことは、配置ツールが見る)。選んだ編成の列車の編成情報カードも広げる。
    const std::vector<Formation>& formations = refs_.selector.Formations();
    if (defIndex < formations.size()) {
        const std::vector<rs::world::TrainId> trains =
            refs_.sceneRenderer.DiaRuntime().TagRegistry().TrainsOfFormation(formations[defIndex].guid);
        if (!trains.empty()) refs_.uiState.focusedTrainId = trains.front();
    }
    refs_.selector.SelectFormation(defIndex);
}

void FormationAction::RemoveFormation(std::size_t defIndex) {
    const std::vector<Formation>& formations = refs_.selector.Formations();
    if (defIndex >= formations.size()) return;
    // 編成の登録(タグと、配置済み列車との対応)も一緒に落とす。
    refs_.sceneRenderer.DiaRuntime().TagRegistry().RemoveFormation(formations[defIndex].guid);
    refs_.selector.RemoveFormation(defIndex);
    refs_.document.MarkEdited();
}

void FormationAction::SetSelectedFormationName(std::string name) {
    refs_.selector.SetFormationName(refs_.selector.SelectedFormationIndex(), name);
    refs_.document.MarkEdited();
}

void FormationAction::RenameFormation(std::size_t defIndex, std::string name) {
    refs_.selector.SetFormationName(defIndex, name);
    refs_.document.MarkEdited();
}

void FormationAction::DuplicateFormation(std::size_t defIndex) {
    const std::vector<Formation>& formations = refs_.selector.Formations();
    if (defIndex >= formations.size()) return;
    const rs::world::Guid sourceId = formations[defIndex].guid;
    const std::optional<std::size_t> copyIndex = refs_.selector.DuplicateFormation(defIndex);
    if (!copyIndex.has_value()) return;
    refs_.sceneRenderer.DiaRuntime().TagRegistry().CopyFormationTags(sourceId, formations[*copyIndex].guid);
    refs_.document.MarkEdited();
}

std::optional<std::size_t> FormationAction::FindFormationIndex(const rs::world::Guid& formationId) const {
    const std::vector<Formation>& formations = refs_.selector.Formations();
    for (std::size_t defIndex = 0; defIndex < formations.size(); ++defIndex) {
        if (formations[defIndex].guid == formationId) return defIndex;
    }
    return std::nullopt;
}

std::size_t FormationAction::PlacedTrainCount(const rs::world::Guid& formationId) const {
    return refs_.sceneRenderer.DiaRuntime().TagRegistry().TrainsOfFormation(formationId).size();
}

bool FormationAction::ApplyTrainGroupTemplate(const std::string& path) {
    const TrainGroupTemplateApplyResult result = ApplyTrainGroupTemplateFile(path, refs_.selector);
    if (result.ok) refs_.document.MarkEdited();
    return result.ok;
}

void FormationAction::SetCarReversed(std::size_t carIndex, bool reversed) {
    SetCarReversed(refs_.selector.SelectedFormationIndex(), carIndex, reversed);
}

void FormationAction::SetCarReversed(std::size_t defIndex, std::size_t carIndex, bool reversed) {
    refs_.selector.SetFormationCarReversed(defIndex, carIndex, reversed);
    refs_.document.MarkEdited();
}

void FormationAction::MoveCar(std::size_t carIndex, int delta) {
    refs_.selector.MoveFormationCar(refs_.selector.SelectedFormationIndex(), carIndex, delta);
    refs_.document.MarkEdited();
}

void FormationAction::RemoveCar(std::size_t carIndex) {
    RemoveCar(refs_.selector.SelectedFormationIndex(), carIndex);
}

void FormationAction::RemoveCar(std::size_t defIndex, std::size_t carIndex) {
    refs_.selector.RemoveFormationCar(defIndex, carIndex);
    refs_.document.MarkEdited();
}

void FormationAction::MoveCarToSlot(std::size_t defIndex, std::size_t carIndex, std::size_t slot) {
    const std::vector<Formation>& formations = refs_.selector.Formations();
    if (defIndex >= formations.size()) return;
    const std::optional<std::size_t> target =
        workspace::ResolveCarMoveTarget(formations[defIndex].cars.size(), carIndex, slot);
    if (!target.has_value()) return;
    refs_.selector.MoveFormationCarTo(defIndex, carIndex, *target);
    refs_.document.MarkEdited();
}

std::string FormationAction::InsertCar(std::size_t defIndex, std::size_t slot, const std::string& folderUtf8) {
    const std::vector<Formation>& formations = refs_.selector.Formations();
    // 自動の名前のまま最初の車両を入れたら、一覧で見分けられるよう先頭車の名前を付ける。
    const bool renameFromHead = defIndex < formations.size() && formations[defIndex].cars.empty() &&
                                FormationCatalog::IsAutoFormationName(formations[defIndex].name);
    std::string error;
    refs_.selector.InsertFormationCar(defIndex, slot, folderUtf8, error);
    if (!error.empty()) return error;
    if (renameFromHead && !formations[defIndex].cars.empty() && !formations[defIndex].cars.front().displayName.empty()) {
        refs_.selector.SetFormationName(defIndex, formations[defIndex].cars.front().displayName);
    }
    refs_.document.MarkEdited();
    return error;
}

void FormationAction::SetPlacementReversedHead(bool reversed) {
    refs_.uiState.reversedHead = reversed;
}

int FormationAction::AddTrainTag(const std::string& label) {
    const int tagId = refs_.sceneRenderer.AddTrainTag(label);
    refs_.document.MarkEdited();
    return tagId;
}

void FormationAction::RemoveTrainTag(int tagId) {
    refs_.sceneRenderer.RemoveTrainTag(tagId);
    refs_.document.MarkEdited();
}

void FormationAction::SetFormationTagAssigned(const rs::world::Guid& formationId, int tagId, bool assigned) {
    rs::world::TrainTagRegistry& tagRegistry = refs_.sceneRenderer.DiaRuntime().TagRegistry();
    std::vector<int> updatedTags = tagRegistry.FormationTags(formationId);
    const std::vector<int>::iterator iterator =
        std::find(updatedTags.begin(), updatedTags.end(), tagId);
    if (assigned && iterator == updatedTags.end()) {
        updatedTags.push_back(tagId);
    }
    if (!assigned && iterator != updatedTags.end()) {
        updatedTags.erase(iterator);
    }
    tagRegistry.SetFormationTags(formationId, std::move(updatedTags));
    refs_.document.MarkEdited();
}

void FormationAction::SetGroupTargetSpeedKmh(rs::world::TrainId trainId, float targetKmh) {
    refs_.sceneRenderer.SetGroupTargetSpeed(trainId, static_cast<double>(targetKmh));
}

void FormationAction::StepGroupTargetSpeedKmh(rs::world::TrainId trainId, int steps) {
    const float currentKmh = static_cast<float>(refs_.sceneRenderer.GroupTargetSpeedKmh(trainId));
    SetGroupTargetSpeedKmh(trainId, workspace::StepTargetSpeedKmh(currentKmh, steps));
}

void FormationAction::SetGroupStopDistance(rs::world::TrainId trainId, float distanceM) {
    refs_.sceneRenderer.SetGroupStopDistance(trainId, static_cast<double>(distanceM));
}

void FormationAction::ClearGroupStopTarget(rs::world::TrainId trainId) {
    refs_.sceneRenderer.ClearGroupStopTarget(trainId);
}

void FormationAction::ForceStopGroup(rs::world::TrainId trainId) {
    refs_.sceneRenderer.SetGroupTargetSpeed(trainId, 0.0);
    refs_.sceneRenderer.SetGroupCurrentSpeed(trainId, 0.0);
    refs_.sceneRenderer.ClearGroupStopTarget(trainId);
}

void FormationAction::RemoveGroup(rs::world::TrainId trainId) {
    refs_.sceneRenderer.RemoveGroup(trainId, refs_.sceneRegistry);
    std::erase(refs_.uiState.placedTrainIds, trainId);
    refs_.cabinCamera.RemoveTrain(trainId);
}

void FormationAction::SetCabinViewActive(rs::world::TrainId trainId, bool active) {
    if (active) {
        refs_.camera.StopFollow();
        if (refs_.cabinCamera.followedTrainId != trainId) refs_.cabinCamera.ResetPivotCache();
        refs_.cabinCamera.followedTrainId = trainId;
        refs_.cabinCamera.active = true;
    } else if (refs_.cabinCamera.followedTrainId == trainId) {
        refs_.cabinCamera.active = false;
    }
}

void FormationAction::LeaveCabinView() {
    SetCabinViewActive(refs_.cabinCamera.followedTrainId, false);
}

CabinHudViewModel FormationAction::CabinHud() const {
    CabinHudViewModel out;
    const rs::world::TrainId trainId = refs_.cabinCamera.followedTrainId;
    if (!refs_.cabinCamera.active || rs::world::GuidIsNil(trainId)) return out;
    out.active = true;
    const rs::world::TrainRunInfo* runInfo = refs_.sceneRenderer.DiaRuntime().RunInfoRegistry().RunInfo(trainId);
    if (runInfo != nullptr) {
        out.trainType = runInfo->type;
        out.destination = runInfo->destination;
    }
    const SceneTrainController::StopLabels& stops = refs_.sceneRenderer.GroupStopLabels(trainId);
    out.previousStation = stops.previousStation;
    out.nextStation = stops.nextStation;
    out.nextPlatform = stops.nextPlatform;
    out.speedKmh = static_cast<int>(std::lround(std::abs(refs_.sceneRenderer.GroupCurrentSpeedKmh(trainId))));
    return out;
}

void FormationAction::FollowTrain(rs::world::TrainId trainId) {
    rs::core::Vec3 anchor;
    if (ResolveLeadCarAnchor(trainId, anchor)) refs_.camera.BeginFollow(trainId, anchor);
}

void FormationAction::FollowFormation(const rs::world::Guid& formationId) {
    const std::vector<rs::world::TrainId> trains = refs_.sceneRenderer.DiaRuntime().TagRegistry().TrainsOfFormation(formationId);
    if (!trains.empty()) FollowTrain(trains.front());
}

bool FormationAction::ResolveLeadCarAnchor(rs::world::TrainId trainId, rs::core::Vec3& outAnchor) const {
    const bool travelReversed = refs_.sceneRenderer.TrainRuntime().GroupTravelReversed(trainId);
    return refs_.sceneRenderer.ResolveLeadCarAnchor(refs_.sceneRegistry, trainId, travelReversed, outAnchor);
}

FormationViewModel FormationAction::ViewModel() const {
    FormationViewModel viewModel;
    const std::vector<Formation>& defs = refs_.selector.Formations();
    const std::size_t selectedDefIndex = refs_.selector.SelectedFormationIndex();

    const std::vector<rs::world::TrainTagDef>& tagDefs = refs_.sceneRenderer.TrainTagDefs();
    const rs::world::TrainTagRegistry& tagRegistry = refs_.sceneRenderer.DiaRuntime().TagRegistry();
    viewModel.defs.reserve(defs.size());
    for (std::size_t defIndex = 0; defIndex < defs.size(); ++defIndex) {
        const Formation& def = defs[defIndex];
        FormationRowViewModel row{def.name, def.cars.size(), defIndex == selectedDefIndex, {}, def.guid,
                                  refs_.sceneRenderer.DiaRuntime().TagRegistry().HasTrainOfFormation(def.guid)};
        row.cars.reserve(def.cars.size());
        for (const FormationCar& car : def.cars) {
            row.cars.push_back({car.displayName, car.folderUtf8, car.reversed});
        }
        const std::vector<int>& formationTags = tagRegistry.FormationTags(def.guid);
        for (const rs::world::TrainTagDef& tagDef : tagDefs) {
            if (std::find(formationTags.begin(), formationTags.end(), tagDef.id) != formationTags.end()) {
                row.tags.push_back({tagDef.id, tagDef.label});
            }
        }
        viewModel.defs.push_back(std::move(row));
    }

    if (selectedDefIndex < defs.size()) {
        viewModel.hasSelectedDef = true;
        viewModel.selectedDefName = defs[selectedDefIndex].name;
        const std::vector<FormationCar>& cars = refs_.selector.SelectedFormationCars();
        viewModel.selectedCars.reserve(cars.size());
        for (const FormationCar& car : cars) {
            viewModel.selectedCars.push_back({car.displayName, car.folderUtf8, car.reversed});
        }
    }

    viewModel.placementReversedHead = refs_.uiState.reversedHead;
    viewModel.cabinViewActive = refs_.cabinCamera.active;

    viewModel.tags.reserve(tagDefs.size());
    for (const rs::world::TrainTagDef& tagDef : tagDefs) {
        viewModel.tags.push_back({tagDef.id, tagDef.label});
    }

    viewModel.placedGroups.reserve(refs_.uiState.placedTrainIds.size());
    for (const rs::world::TrainId& trainId : refs_.uiState.placedTrainIds) {
        PlacedGroupViewModel group;
        group.trainId = trainId;
        group.sourceFormationId = refs_.sceneRenderer.DiaRuntime().TagRegistry().FormationOfTrain(trainId);
        const std::vector<int>& assignedTags = refs_.sceneRenderer.TrainTagsOf(trainId);
        group.tagAssigned.reserve(tagDefs.size());
        for (const rs::world::TrainTagDef& tagDef : tagDefs) {
            const bool assigned =
                std::find(assignedTags.begin(), assignedTags.end(), tagDef.id) != assignedTags.end();
            group.tagAssigned.push_back(assigned);
        }
        group.targetSpeedKmh = static_cast<float>(refs_.sceneRenderer.GroupTargetSpeedKmh(trainId));
        group.currentSpeedKmh = static_cast<float>(refs_.sceneRenderer.GroupCurrentSpeedKmh(trainId));
        // 運行状態はダイヤ状態機械(rs::sim::DiaRuntime)が唯一の情報源。
        group.diaState = refs_.sceneRenderer.GroupDiaState(trainId);
        double departureAbsSeconds = 0.0;
        group.hasDeparture =
            refs_.sceneRenderer.GroupDepartureAbsSeconds(trainId, departureAbsSeconds);
        if (group.hasDeparture) {
            group.departureSecondsOfDay = static_cast<int>(std::fmod(departureAbsSeconds, 86400.0));
        }
        group.cabinViewActive =
            refs_.cabinCamera.active && refs_.cabinCamera.followedTrainId == trainId;
        const rs::world::TrainRunInfo* runInfo =
            refs_.sceneRenderer.DiaRuntime().RunInfoRegistry().RunInfo(trainId);
        if (runInfo != nullptr) {
            group.trainType = runInfo->type;
            group.destination = runInfo->destination;
            group.trainNumber = runInfo->trainNumber;
        }
        // 走り出した編成は走行系の持つSceneが正。置いたまま一度も走っていない編成は走行系に載っていないので、
        // 車両の配置時のSceneを使う。
        group.sceneIndex = refs_.sceneRenderer.TrainRuntime().GroupSceneIndex(trainId);
        if (!group.sceneIndex.has_value()) {
            for (const rs::world::SceneEntity& entity : refs_.sceneRegistry.Entities()) {
                if (entity.category != rs::world::SceneEntityCategory::Car || entity.trainId != trainId) continue;
                group.sceneIndex = entity.placementSceneIndex;
                break;
            }
        }
        viewModel.placedGroups.push_back(std::move(group));
    }
    return viewModel;
}

} // namespace rs::app
