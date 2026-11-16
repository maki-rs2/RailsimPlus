//
// PlatformRoutineAction.h - 番線ルーチン表のアプリサービス
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::app {

using rs::world::TrainId;

struct RoutineLayerRef {
    // Formation の entryIndex は PlatformStopInfo::formationRoutines の添字(Train のような並べ替えはしない)。
    enum class Kind { Common, Train, Tag, Formation };
    Kind kind = Kind::Common;
    std::size_t entryIndex = 0;
};

struct RoutineElementViewModel {
    bool enabled = true;
    rs::world::DiaAction action = rs::world::DiaAction::Stop;
    rs::world::DiaTimeType timeType = rs::world::DiaTimeType::ElapsedFromArrival;
    double dwellSeconds = 0.0;
    std::vector<rs::world::DiaTimeOfDay> departureTimes;
    std::vector<rs::world::SwitchChange> switchChanges;
    bool canMoveUp = false;
    bool canMoveDown = false;
};

struct RoutineLayerViewModel {
    std::vector<RoutineElementViewModel> elements;
    int nextDepartureIndex = -1;
};

struct TrainRoutineLayerViewModel {
    TrainId trainId = rs::world::NilGuid();
    RoutineLayerViewModel layer;
};

struct TagRoutineLayerViewModel {
    int tagId = 0;
    bool hasTagDef = false;
    std::string tagLabel;
    bool canMoveUp = false;
    bool canMoveDown = false;
    RoutineLayerViewModel layer;
};

struct TagChoiceViewModel {
    int id = 0;
    std::string label;
};

struct PlatformRoutineViewModel {
    bool visible = false;
    RoutineLayerViewModel common;
    std::vector<TrainRoutineLayerViewModel> trainLayers;
    std::vector<TagRoutineLayerViewModel> tagLayers;
    std::vector<TagChoiceViewModel> tagChoices;
};

// 環境の注入。「次発車」の解決と利用可能なタグ定義は実行時の走行状態を持つ描画側しか
// 答えられないが、Action層は描画層へリンクしないため関数で受け取る。
struct PlatformRoutineEnvironment {
    std::function<int(const rs::world::Guid&, const std::vector<rs::world::PlatformRoutineElement>&, int,
                      rs::world::TrainId)>
        cursorIndex;
    std::function<std::vector<TagChoiceViewModel>()> tagChoices;
};

class PlatformRoutineAction {
public:
    PlatformRoutineAction(rs::editor::Editor& editor, rs::world::Guid platformId,
                          PlatformRoutineEnvironment environment);
    void SetElementEnabled(const RoutineLayerRef& layer, std::size_t elementIndex, bool enabled);
    void SetElementAction(const RoutineLayerRef& layer, std::size_t elementIndex, rs::world::DiaAction action);
    void SetElementTimeType(const RoutineLayerRef& layer, std::size_t elementIndex, rs::world::DiaTimeType timeType);
    void SetElementDwellSeconds(const RoutineLayerRef& layer, std::size_t elementIndex, double dwellSeconds);
    void AddDepartureTime(const RoutineLayerRef& layer, std::size_t elementIndex);
    void RemoveDepartureTime(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t timeIndex);
    void SetDepartureTime(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t timeIndex, int hour, int minute, int second);
    // 発車時刻を書いたあと、早い順に並べ直す(1回のUndoで戻る)。時刻の追加も同じ。
    void SetDepartureTimeSorted(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t timeIndex, int hour, int minute, int second);
    void AddDepartureTimeSorted(const RoutineLayerRef& layer, std::size_t elementIndex);
    // 要素の停止位置を、番線の既定へ戻す(要素の指定を外す)。
    void ResetElementStopOffset(const RoutineLayerRef& layer, std::size_t elementIndex);
    // 運行情報の書換。同じ項目は1つだけ持つので、すでにあれば追加しない。
    void AddRunInfoChange(const RoutineLayerRef& layer, std::size_t elementIndex, rs::world::RunInfoChange::Field field);
    void SetRunInfoChange(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t changeIndex, std::string value);
    void RemoveRunInfoChange(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t changeIndex);
    void AddSwitchChange(const RoutineLayerRef& layer, std::size_t elementIndex);
    void RemoveSwitchChange(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t switchIndex);
    void SetSwitchChange(const RoutineLayerRef& layer, std::size_t elementIndex, std::size_t switchIndex, std::string switchName, int value);
    void AddElement(const RoutineLayerRef& layer);
    void RemoveElement(const RoutineLayerRef& layer, std::size_t elementIndex);
    void MoveElement(const RoutineLayerRef& layer, std::size_t elementIndex, int delta);
    // 要素を取り出して、並べ替えたあとに toIndex の位置へ来るように入れる(ドラッグの並べ替え用)。
    void MoveElementTo(const RoutineLayerRef& layer, std::size_t fromIndex, std::size_t toIndex);
    bool AddTrainRoutine(TrainId trainId);
    void RemoveTrainRoutine(std::size_t entryIndex);
    bool AddTagRoutine(int tagId);
    void RemoveTagRoutine(std::size_t entryIndex);
    void MoveTagRoutine(std::size_t entryIndex, int delta);
    void MoveTagRoutineTo(std::size_t fromIndex, std::size_t toIndex);
    bool AddFormationRoutine(const rs::world::Guid& formationId);
    void RemoveFormationRoutine(std::size_t entryIndex);
    PlatformRoutineViewModel ViewModel() const;

private:
    rs::editor::Editor& editor_;
    rs::world::Guid platformId_;
    PlatformRoutineEnvironment environment_;
};

} // namespace rs::app
