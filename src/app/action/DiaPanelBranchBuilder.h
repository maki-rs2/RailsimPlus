//
// DiaPanelBranchBuilder.h - ダイヤの左タブの表示値のうち、分岐(ポイント)の分を作る(DiaPanelPresentationBuilder の内部用)
//
#pragma once

#include <string>
#include <vector>

#include "DiaPanelPresentationBuilder.h"

namespace rs::world {
struct TrackNode;
} // namespace rs::world

namespace rs::app::dia_builder {

// 層の見出しの名前。編成が定義に無ければ「(削除された編成)」、タグが定義に無ければ「タグ:#番号」。
std::string FormationName(const DiaPanelBuildInput& input, const rs::world::Guid& id);
std::string TagLabel(const DiaPanelBuildInput& input, int tagId);

struct PointEntry {
    rs::world::Guid id;
    const rs::world::TrackNode* node = nullptr;
};

// グラフの分岐を、位置の x、z の順に並べる(読込のたびに内部の並びが変わるので、表示用に固定する)。
std::vector<PointEntry> SortedPoints(const rs::world::TrackGraph& graph);

// 分岐の名前。空なら「分岐」。
std::string PointDisplayName(const rs::world::TrackNode& node);

// 有効な要素を持つ層も、有効な条件も1つもないか。
bool PointIsUnset(const rs::world::TrackNode& node);

// 分岐1つぶんの表示値。breadcrumb は「駅名 > 分岐名」の形で呼び出し側が決める。
workspace::DiaPanelTarget MakeBranchTarget(const DiaPanelBuildInput& input, const rs::world::TrackGraph& graph,
                                           const PointEntry& point, std::string breadcrumb, int sceneIndex);

} // namespace rs::app::dia_builder
