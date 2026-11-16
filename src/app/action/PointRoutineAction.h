//
// PointRoutineAction.h - 分岐(ポイント)のルーチン表・条件・所属駅のアプリサービス
//
// 番線の PlatformRoutineAction の分岐版。どの操作も SetPointDiaCommand 1回(Undo 1回)で実行する。
//
#pragma once

#include <cstddef>
#include <string>

#include "../../editor/tool/Editor.h"
#include "../../world/alignment/PointRoutine.h"

namespace rs::app {

// 分岐の層の持ち主。共通は1つ、編成はGuid、タグはタグ番号で見分ける。
struct PointLayerRef {
    enum class Kind { Common, Formation, Tag };
    Kind kind = Kind::Common;
    rs::world::Guid formationId = rs::world::NilGuid();
    int tagId = -1;
};

class PointRoutineAction {
public:
    PointRoutineAction(rs::editor::Editor& editor, rs::world::Guid nodeId);

    // 層。ルーチンも条件も持たない層は無いので、追加ではルーチンの項目を作り(要素を1つ入れる)、削除では両方を消す。
    bool AddFormationLayer(const rs::world::Guid& formationId);
    bool AddTagLayer(int tagId);
    void RemoveLayer(const PointLayerRef& layer);
    // タグの層の優先順を、fromTagId の層を toTagId の層の位置へ動かして変える。ルーチンと条件の両方を同じ順に直す。
    void MoveTagLayerTo(int fromTagId, int toTagId);

    // 要素。AddElement は既定値(左・有効)の要素を末尾へ足す。
    void AddElement(const PointLayerRef& layer);
    void RemoveElement(const PointLayerRef& layer, std::size_t elementIndex);
    void SetElementEnabled(const PointLayerRef& layer, std::size_t elementIndex, bool enabled);
    void MoveElementTo(const PointLayerRef& layer, std::size_t fromIndex, std::size_t toIndex);
    // hand は 0=左 / 1=右。random が true なら hand は使われない。
    void SetElementHand(const PointLayerRef& layer, std::size_t elementIndex, int hand, bool random);

    // 条件。AddCondition は種類ごとの既定値(時刻範囲は 07:00〜09:00、閉塞の在線は閉塞なし・いずれか、開通先は左)で末尾へ足す。
    void AddCondition(const PointLayerRef& layer, rs::world::PointCondition::Kind kind);
    void RemoveCondition(const PointLayerRef& layer, std::size_t conditionIndex);
    void SetConditionEnabled(const PointLayerRef& layer, std::size_t conditionIndex, bool enabled);
    void SetConditionTime(const PointLayerRef& layer, std::size_t conditionIndex, bool isStart,
                          const rs::world::DiaTimeOfDay& time);
    void SetConditionBlock(const PointLayerRef& layer, std::size_t conditionIndex, const rs::world::Guid& block,
                           bool selected);
    void SetConditionRequireAll(const PointLayerRef& layer, std::size_t conditionIndex, bool requireAll);
    void SetConditionHand(const PointLayerRef& layer, std::size_t conditionIndex, int hand);

    // 分岐の名前を変える。空の名前は受け付けない(名前なしにはできない)。
    void SetName(std::string name);
    // 所属駅を変える。NilGuidは未所属。
    void SetStation(const rs::world::Guid& stationId);

private:
    rs::editor::Editor& editor_;
    rs::world::Guid nodeId_;
};

} // namespace rs::app
