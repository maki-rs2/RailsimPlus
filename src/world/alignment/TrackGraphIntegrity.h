#pragma once

#include <unordered_set>
#include <string>
#include <vector>
#include "Guid.h"


namespace rs::world {

struct TrackGraph;

// 接続点と形の端点の比較には、取り込み時の丸め誤差の余裕を設ける。
inline constexpr double kLinkEndpointToleranceM = 1.0;

using TrackGraphGuids = std::unordered_set<Guid, GuidHash>;

// 配列をmapへ入れる前にも使い、重複した後続レコードの上書き消失を防ぐ。
void SanitizeTrackGraphGuid(Guid& guid, TrackGraphGuids& used, std::vector<std::string>& warnings);

std::vector<std::string> ValidateTrackGraph(const TrackGraph& graph);
std::vector<std::string> SanitizeTrackGraph(TrackGraph& graph);

} // namespace rs::world
