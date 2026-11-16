//
// DiaPanelPresentationBuilder.cpp
//
#include "DiaPanelPresentationBuilder.h"

#include <algorithm>
#include <cstdio>

#include "../workspace/DiaPanelFormat.h"
#include "../../world/alignment/PlatformRoutine.h"

namespace rs::app {
namespace {

using namespace workspace;

std::string FormationName(const DiaPanelBuildInput& input, const rs::world::Guid& id) {
    for (const DiaPanelFormationName& formation : input.formations) {
        if (formation.id == id) return formation.name;
    }
    return "(削除された編成)";
}

std::string TagLabel(const DiaPanelBuildInput& input, int tagId) {
    for (const DiaPanelTagName& tag : input.tags) {
        if (tag.id == tagId) return "タグ:" + tag.label;
    }
    return "タグ:#" + std::to_string(tagId);
}

std::string ElementSummary(const rs::world::PlatformRoutineElement& element) {
    std::string summary = DiaActionLabel(element.action);
    if (element.action != rs::world::DiaAction::Pass) {
        if (element.timeType == rs::world::DiaTimeType::ElapsedFromArrival) {
            summary += " " + FormatDiaSeconds(element.dwellSeconds) + "秒";
        } else if (element.departureTimes.empty()) {
            summary += " 時刻未設定";
        } else {
            summary += " " + FormatDiaTime(element.departureTimes.front());
            if (element.departureTimes.size() > 1) summary += " 他" + std::to_string(element.departureTimes.size() - 1);
        }
    }
    if (!element.enabled) summary += " (無効)";
    return summary;
}

std::string StopPositionText(const rs::world::PlatformRoutineElement& element) {
    if (!element.stopOffsetNormalized.has_value()) return "番線の既定";
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "要素で指定 %+.2f", *element.stopOffsetNormalized);
    return buffer;
}

DiaPanelElement MakeElement(const rs::world::PlatformRoutineElement& source) {
    DiaPanelElement element;
    element.summary_ = ElementSummary(source);
    element.overwrite_ = !source.runInfoChanges.empty();
    element.disabled_ = !source.enabled;
    element.operation_ = DiaActionKey(source.action);
    element.departure_ = source.timeType == rs::world::DiaTimeType::ElapsedFromArrival ? "seconds" : "time";
    element.seconds_ = FormatDiaSeconds(source.dwellSeconds);
    for (const rs::world::DiaTimeOfDay& time : source.departureTimes) {
        element.times_.push_back({"", FormatDiaTime(time), {}});
    }
    element.stopPosition_ = StopPositionText(source);
    element.stopCustom_ = source.stopOffsetNormalized.has_value();
    element.stopOffset_ = source.stopOffsetNormalized.value_or(0.0);
    for (const rs::world::RunInfoChange& change : source.runInfoChanges) {
        DiaPanelField field;
        field.label_ = DiaRunInfoFieldLabel(change.field);
        field.draft_ = change.value;
        field.key_ = static_cast<int>(change.field);
        element.overwrites_.push_back(std::move(field));
    }
    return element;
}

struct LayerSpec {
    std::string name;
    std::string kind;
    bool tag = false;
    bool deletable = false;
    int routineKey = rs::world::kCommonRoutineLayerKey;
    rs::world::Guid formationId = rs::world::NilGuid();
};

DiaPanelLayer MakeLayer(LayerSpec spec, const std::vector<rs::world::PlatformRoutineElement>& routine) {
    DiaPanelLayer layer;
    layer.name_ = std::move(spec.name);
    layer.kind_ = std::move(spec.kind);
    layer.deletable_ = spec.deletable;
    layer.reorderable_ = spec.tag;
    layer.routineKey_ = spec.routineKey;
    layer.formationId_ = spec.formationId;
    bool nextFound = false;
    for (const rs::world::PlatformRoutineElement& source : routine) {
        DiaPanelElement element = MakeElement(source);
        if (source.enabled && !nextFound) {
            element.next_ = nextFound = true;
        }
        layer.elements_.push_back(std::move(element));
    }
    return layer;
}

DiaPanelTarget MakeTarget(const DiaPanelBuildInput& input, const rs::world::PlatformStopInfo& platform,
                          std::string breadcrumb, int sceneIndex) {
    DiaPanelTarget target;
    target.breadcrumb_ = std::move(breadcrumb);
    target.doorLeft_ = platform.openDoor[0];
    target.doorRight_ = platform.openDoor[1];
    target.platformId_ = platform.id;
    target.sceneIndex_ = sceneIndex;
    for (const rs::world::PlatformFormationRoutine& entry : platform.formationRoutines) {
        target.layers_.push_back(MakeLayer({FormationName(input, entry.formationId), "編成", false, true,
                                            kDiaFormationLayerKey, entry.formationId},
                                           entry.routine));
    }
    for (const rs::world::PlatformTagRoutine& entry : platform.tagRoutines) {
        target.layers_.push_back(MakeLayer({TagLabel(input, entry.tagId), "タグ", true, true,
                                            rs::world::TagRoutineLayerKey(entry.tagId), rs::world::NilGuid()},
                                           entry.routine));
    }
    target.layers_.push_back(MakeLayer({"全列車共通", "全列車共通", false, false, rs::world::kCommonRoutineLayerKey,
                                        rs::world::NilGuid()},
                                       platform.routine));
    return target;
}

// 駅の番線のうちレールを持つものについて、最初のレールの中点を求め、その平均を返す。
bool StationFlyTarget(const rs::world::TrackGraph& graph, const rs::world::Guid& stationId, DiaPanelListRow& row) {
    float sum[3] = {0.0f, 0.0f, 0.0f};
    int count = 0;
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (platform.stationId != stationId) continue;
        for (const rs::world::Guid& edgeId : platform.railEdges) {
            const auto it = graph.edges.find(edgeId);
            if (it == graph.edges.end() || it->second.spline.empty()) continue;
            const rs::core::Vec3& mid = it->second.spline[it->second.spline.size() / 2].pos;
            sum[0] += mid.x;
            sum[1] += mid.y;
            sum[2] += mid.z;
            ++count;
            break;
        }
    }
    if (count == 0) return false;
    row.flyX_ = sum[0] / static_cast<float>(count);
    row.flyY_ = sum[1] / static_cast<float>(count);
    row.flyZ_ = sum[2] / static_cast<float>(count);
    return true;
}

struct SceneRowContext {
    const DiaPanelBuildInput& input;
    DiaPanelPresentation& panel;
    const DiaPanelSceneSource& scene;
    int sceneIndex = 0;
};

void AddPlatformRow(SceneRowContext& context, const rs::world::PlatformStopInfo& platform,
                    const std::string& parentName, int depth) {
    DiaPanelPresentation& panel = context.panel;
    DiaPanelListRow row;
    row.kind_ = "target";
    row.name_ = platform.name;
    row.target_ = static_cast<int>(panel.targets_.size());
    row.gray_ = !platform.stoppable;
    row.draggable_ = true;
    row.depth_ = depth;
    row.id_ = platform.id;
    row.sceneIndex_ = context.sceneIndex;
    panel.rows_.push_back(std::move(row));
    panel.targets_.push_back(MakeTarget(context.input, platform,
                                        parentName.empty() ? platform.name : parentName + " > " + platform.name,
                                        context.sceneIndex));
}

// 停車できて、レールを持つ番線が1つでもあるか。
bool HasStoppablePlatformWithRail(const rs::world::TrackGraph& graph, const rs::world::Guid& stationId) {
    return std::any_of(graph.platforms.begin(), graph.platforms.end(), [&stationId](const rs::world::PlatformStopInfo& platform) {
        return platform.stationId == stationId && platform.stoppable && !platform.railEdges.empty();
    });
}

void AddScene(SceneRowContext& context, bool showSceneRow) {
    DiaPanelPresentation& panel = context.panel;
    const rs::world::TrackGraph& graph = *context.scene.graph;
    if (showSceneRow) {
        DiaPanelListRow row;
        row.kind_ = "scene";
        row.name_ = context.scene.name;
        row.sceneIndex_ = context.sceneIndex;
        panel.rows_.push_back(std::move(row));
    }
    const int stationDepth = showSceneRow ? 1 : 0;
    for (const rs::world::StationInfo& station : graph.stations) {
        // 取り込んだ駅(sourceAddressを持つ)のうち、停車できる番線にレールが無いものは出さない。
        // 手動で作った駅(sourceAddressが空)は、番線が空でも出す。
        if (!station.sourceAddress.empty() && !HasStoppablePlatformWithRail(graph, station.id)) continue;
        DiaPanelListRow row;
        row.kind_ = "station";
        row.name_ = station.name;
        row.depth_ = stationDepth;
        row.id_ = station.id;
        row.sceneIndex_ = context.sceneIndex;
        row.hasFlyTarget_ = StationFlyTarget(graph, station.id, row);
        panel.rows_.push_back(std::move(row));
        for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
            if (platform.stationId == station.id) AddPlatformRow(context, platform, station.name, stationDepth + 1);
        }
    }
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        const bool known = !rs::world::GuidIsNil(platform.stationId) &&
                           std::any_of(graph.stations.begin(), graph.stations.end(),
                                       [&platform](const rs::world::StationInfo& station) { return station.id == platform.stationId; });
        if (!known) AddPlatformRow(context, platform, showSceneRow ? context.scene.name : std::string(), stationDepth);
    }
}

bool SameRow(const DiaPanelListRow& left, const DiaPanelListRow& right) {
    return left.kind_ == right.kind_ && left.sceneIndex_ == right.sceneIndex_ && left.id_ == right.id_;
}

int FindTarget(const DiaPanelPresentation& panel, const DiaPanelTarget& wanted) {
    for (std::size_t index = 0; index < panel.targets_.size(); ++index) {
        if (panel.targets_[index].platformId_ == wanted.platformId_ && panel.targets_[index].sceneIndex_ == wanted.sceneIndex_) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int FindLayer(const DiaPanelTarget& target, const DiaPanelLayer& wanted) {
    for (std::size_t index = 0; index < target.layers_.size(); ++index) {
        if (target.layers_[index].routineKey_ == wanted.routineKey_ && target.layers_[index].formationId_ == wanted.formationId_) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

template <typename T>
const T* At(const std::vector<T>& values, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < values.size() ? &values[static_cast<std::size_t>(index)] : nullptr;
}

// 開いていた番線の、要素ごとの発車時刻の開閉を戻す(層のキーで探す)。
void CarryTimesOpen(const DiaPanelTarget& oldTarget, DiaPanelTarget& newTarget) {
    for (const DiaPanelLayer& oldLayer : oldTarget.layers_) {
        const int layerIndex = FindLayer(newTarget, oldLayer);
        if (layerIndex < 0) continue;
        DiaPanelLayer& layer = newTarget.layers_[static_cast<std::size_t>(layerIndex)];
        for (std::size_t element = 0; element < layer.elements_.size() && element < oldLayer.elements_.size(); ++element) {
            layer.elements_[element].timesOpen_ = oldLayer.elements_[element].timesOpen_;
        }
    }
}

} // namespace

DiaPanelPresentation BuildDiaPanelPresentation(const DiaPanelBuildInput& input) {
    DiaPanelPresentation panel;
    panel.sceneCount_ = static_cast<int>(input.scenes.size());
    panel.activeScene_ = input.activeScene;
    const bool showSceneRow = input.scenes.size() > 1;
    for (std::size_t index = 0; index < input.scenes.size(); ++index) {
        if (input.scenes[index].graph == nullptr) continue;
        SceneRowContext context{input, panel, input.scenes[index], static_cast<int>(index)};
        AddScene(context, showSceneRow);
    }
    for (const DiaPanelFormationName& formation : input.formations) {
        panel.layerCatalog_.push_back({formation.name, false, kDiaFormationLayerKey, formation.id});
    }
    for (const DiaPanelTagName& tag : input.tags) {
        panel.layerCatalog_.push_back({"タグ:" + tag.label, false, rs::world::TagRoutineLayerKey(tag.id), {}});
    }
    panel.RefreshDerived();
    return panel;
}

void CarryOverDiaPanelState(const DiaPanelPresentation& previous, DiaPanelPresentation& next) {
    next.active_ = previous.active_;
    if (!previous.active_) return;
    for (DiaPanelListRow& row : next.rows_) {
        for (const DiaPanelListRow& old : previous.rows_) {
            if (!SameRow(old, row)) continue;
            row.collapsed_ = old.collapsed_;
            break;
        }
    }
    next.RecomputeRowVisibility();
    const DiaPanelTarget* const oldTarget = At(previous.targets_, previous.targetIndex_);
    if (previous.screen_ >= 1 && oldTarget != nullptr) {
        const int targetIndex = FindTarget(next, *oldTarget);
        if (targetIndex >= 0 && next.OpenTarget(targetIndex)) {
            CarryTimesOpen(*oldTarget, next.targets_[static_cast<std::size_t>(targetIndex)]);
            const DiaPanelLayer* const oldLayer = At(oldTarget->layers_, previous.layerIndex_);
            const int layerIndex = oldLayer == nullptr ? -1 : FindLayer(next.targets_[static_cast<std::size_t>(targetIndex)], *oldLayer);
            if (layerIndex >= 0 && next.OpenLayer(layerIndex) && previous.screen_ == 2) {
                next.OpenElement(previous.elementIndex_);
            }
            next.addingLayer_ = previous.addingLayer_;
            next.addingOverwrite_ = previous.addingOverwrite_ && next.screen_ == 2;
        }
    }
    for (DiaPanelListRow& row : next.rows_) {
        for (const DiaPanelListRow& old : previous.rows_) {
            if (!SameRow(old, row) || !old.editing_) continue;
            row.editing_ = true;
            row.draft_ = old.draft_;
            break;
        }
    }
}

} // namespace rs::app
