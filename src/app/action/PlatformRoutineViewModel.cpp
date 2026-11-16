//
// PlatformRoutineViewModel.cpp - 番線ルーチン表の表示用データを組み立てる（PlatformRoutineAction.cpp から分離）
//
#include <algorithm>
#include <utility>

#include "PlatformRoutineAction.h"
#include "../../world/alignment/PlatformRegistry.h"

namespace rs::app {
namespace {

RoutineLayerViewModel MakeLayerViewModel(const std::vector<rs::world::PlatformRoutineElement>& routine,
                                         const PlatformRoutineEnvironment& environment,
                                         const rs::world::Guid& platformId, int layerKey,
                                         rs::world::TrainId trainId = rs::world::NilGuid()) {
    RoutineLayerViewModel viewModel;
    if (environment.cursorIndex) {
        viewModel.nextDepartureIndex = environment.cursorIndex(platformId, routine, layerKey, trainId);
    }
    for (std::size_t index = 0; index < routine.size(); ++index) {
        const rs::world::PlatformRoutineElement& element = routine[index];
        RoutineElementViewModel row;
        row.enabled = element.enabled;
        row.action = element.action;
        row.timeType = element.timeType;
        row.dwellSeconds = element.dwellSeconds;
        row.departureTimes = element.departureTimes;
        row.switchChanges = element.switchChanges;
        row.canMoveUp = index > 0;
        row.canMoveDown = index + 1 < routine.size();
        viewModel.elements.push_back(std::move(row));
    }
    return viewModel;
}

} // namespace

PlatformRoutineViewModel PlatformRoutineAction::ViewModel() const {
    PlatformRoutineViewModel viewModel;
    rs::world::PlatformStopInfo* platform = rs::world::FindPlatformById(editor_.graph, platformId_);
    if (platform == nullptr) {
        return viewModel;
    }
    viewModel.visible = true;
    viewModel.common = MakeLayerViewModel(platform->routine, environment_, platformId_,
                                          rs::world::kCommonRoutineLayerKey);

    // 「編成ごと」は編成IDの昇順で並べる。ViewModelの添字はこの並び順で、
    // 元の配列添字への解決はAction側で行う。
    std::vector<std::size_t> indices(platform->trainRoutines.size());
    for (std::size_t index = 0; index < indices.size(); ++index) {
        indices[index] = index;
    }
    std::sort(indices.begin(), indices.end(), [platform](std::size_t left, std::size_t right) {
        return platform->trainRoutines[left].trainId <
               platform->trainRoutines[right].trainId;
    });
    for (std::size_t index : indices) {
        const rs::world::PlatformTrainRoutine& entry = platform->trainRoutines[index];
        TrainRoutineLayerViewModel trainLayer;
        trainLayer.trainId = entry.trainId;
        trainLayer.layer =
            MakeLayerViewModel(entry.routine, environment_, platformId_,
                               rs::world::kCommonRoutineLayerKey, entry.trainId);
        viewModel.trainLayers.push_back(std::move(trainLayer));
    }

    if (environment_.tagChoices) {
        viewModel.tagChoices = environment_.tagChoices();
    }
    for (std::size_t index = 0; index < platform->tagRoutines.size(); ++index) {
        const rs::world::PlatformTagRoutine& entry = platform->tagRoutines[index];
        TagRoutineLayerViewModel tagLayer;
        tagLayer.tagId = entry.tagId;
        tagLayer.canMoveUp = index > 0;
        tagLayer.canMoveDown = index + 1 < platform->tagRoutines.size();
        tagLayer.layer = MakeLayerViewModel(entry.routine, environment_, platformId_,
                                            rs::world::TagRoutineLayerKey(entry.tagId));
        for (const TagChoiceViewModel& choice : viewModel.tagChoices) {
            if (choice.id == entry.tagId) {
                tagLayer.hasTagDef = true;
                tagLayer.tagLabel = choice.label;
                break;
            }
        }
        viewModel.tagLayers.push_back(std::move(tagLayer));
    }
    return viewModel;
}

} // namespace rs::app
