#pragma once

#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "TrackGraph.h"

namespace rs::world {

// edge の端のうち、接続側ノードが nodeGuid と一致する端(0/1)。両端が一致する(自己ループ)ときと、
// どちらとも一致しないときは nullopt。リンクの edgeEnd を保存せずに求め直せるかの判定に使う。
std::optional<int> EdgeEndAtNode(const TrackEdge& edge, const Guid& nodeGuid);
// graph を渡すと、edge から求め直せるリンクの edgeEnd を書かない。nullptr なら全リンクに書く。
nlohmann::json NodeToJson(const TrackNode& node, const TrackGraph* graph = nullptr);
// linksWithoutEnd を渡すと、edgeEnd が無かったリンクの添字(node.links の添字)を入れる。
// そのリンクの end は未決定(0のまま)で、呼び出し側が edge から決める。
TrackNode NodeFromJson(const nlohmann::json& json, std::vector<size_t>* linksWithoutEnd = nullptr);
nlohmann::json EdgeToJson(const TrackEdge& edge);
TrackEdge EdgeFromJson(const nlohmann::json& json);
nlohmann::json StationInfoToJson(const StationInfo& station);
StationInfo StationInfoFromJson(const nlohmann::json& json);
nlohmann::json TrackGraphToJson(const TrackGraph& graph);
TrackGraph TrackGraphFromJson(const nlohmann::json& json);

}  // namespace rs::world
