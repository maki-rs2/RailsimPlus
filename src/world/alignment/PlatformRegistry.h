#pragma once

#include <cstddef>
#include <string>

#include "TrackGraph.h"

namespace rs::world {

// idに一致する番線を返す。見つからない場合はnullptrを返す。
const PlatformStopInfo* FindPlatformById(const TrackGraph& graph, const Guid& id);
PlatformStopInfo* FindPlatformById(TrackGraph& graph, const Guid& id);

// 駅を登録する。空でないsourceAddressが既存駅と一致するときはそのGuidを再利用する。
Guid RegisterStation(TrackGraph& graph, std::string name, std::string sourceAddress);

// 番線とedge索引を同時に登録し、索引更新の実装を1箇所に保つ。
size_t RegisterPlatform(TrackGraph& graph, PlatformStopInfo platform);

// 指定添字の番線を登録解除してgraph.platformsから取り除き、その内容を返す。
// edge索引(edgeToPlatform)も合わせて更新する。
PlatformStopInfo UnregisterPlatform(TrackGraph& graph, size_t index);

// 番線を指定添字位置へ挿入して登録する。削除のUndo(復元)専用。
// 末尾へappendすると、同一edgeを共有する番線が複数ある場合にedgeToPlatformの勝者
// (索引の対応関係)が変わってしまうため、必ず元の添字へ戻す。
void RegisterPlatformAt(TrackGraph& graph, size_t index, PlatformStopInfo platform);

// graph.platformsの内容からedgeToPlatform索引を作り直す。
// stoppableやrailEdgesをその場で書き換えた後など、索引の再構築が必要なときに呼ぶ。
void RebuildPlatformIndices(TrackGraph& graph);

} // namespace rs::world
