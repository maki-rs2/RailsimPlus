#include "PlatformRegistry.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace rs::world {

const PlatformStopInfo* FindPlatformById(const TrackGraph& graph, const Guid& id) {
    // 走行中は列車ごと・番線ごとに毎フレーム呼ばれるので索引を先に引く。platformsは
    // 直接書き換えられることもあるため、索引の指す先が違う・載っていないときは順に探す。
    const std::unordered_map<Guid, size_t, GuidHash>::const_iterator indexIt =
        graph.platformIndexById.find(id);
    if (indexIt != graph.platformIndexById.end() && indexIt->second < graph.platforms.size() &&
        graph.platforms[indexIt->second].id == id) {
        return &graph.platforms[indexIt->second];
    }
    const std::vector<PlatformStopInfo>::const_iterator platformIt = std::find_if(
        graph.platforms.begin(), graph.platforms.end(),
        [&id](const PlatformStopInfo& platform) { return platform.id == id; });
    return platformIt == graph.platforms.end() ? nullptr : &*platformIt;
}

PlatformStopInfo* FindPlatformById(TrackGraph& graph, const Guid& id) {
    return const_cast<PlatformStopInfo*>(FindPlatformById(std::as_const(graph), id));
}

Guid RegisterStation(TrackGraph& graph, std::string name, std::string sourceAddress) {
    if (!sourceAddress.empty()) {
        for (const StationInfo& station : graph.stations) {
            if (station.sourceAddress == sourceAddress) return station.id;
        }
    }

    StationInfo station;
    station.id = GenerateGuid();
    station.name = std::move(name);
    station.sourceAddress = std::move(sourceAddress);
    graph.stations.push_back(std::move(station));
    return graph.stations.back().id;
}

size_t RegisterPlatform(TrackGraph& graph, PlatformStopInfo platform) {
    graph.platforms.push_back(std::move(platform));
    const size_t platformIndex = graph.platforms.size() - 1;
    RebuildPlatformIndices(graph);
    return platformIndex;
}

PlatformStopInfo UnregisterPlatform(TrackGraph& graph, size_t index) {
    if (index >= graph.platforms.size()) return PlatformStopInfo{};
    PlatformStopInfo platform = std::move(graph.platforms[index]);
    graph.platforms.erase(graph.platforms.begin() + static_cast<std::ptrdiff_t>(index));
    RebuildPlatformIndices(graph);
    return platform;
}

void RegisterPlatformAt(TrackGraph& graph, size_t index, PlatformStopInfo platform) {
    const size_t insertIndex = std::min(index, graph.platforms.size());
    graph.platforms.insert(graph.platforms.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                           std::move(platform));
    RebuildPlatformIndices(graph);
}

void RebuildPlatformIndices(TrackGraph& graph) {
    graph.edgeToPlatform.clear();
    graph.edgeToPlatforms.clear();
    graph.platformIndexById.clear();
    for (size_t platformIndex = 0; platformIndex < graph.platforms.size(); ++platformIndex) {
        const PlatformStopInfo& registeredPlatform = graph.platforms[platformIndex];
        graph.platformIndexById.try_emplace(registeredPlatform.id, platformIndex);
        for (const Guid& edgeGuid : registeredPlatform.railEdges) {
            // edgeToPlatform(単数)は停車目標の解決専用なので、停車可能な番線だけを載せる。
            // 停車不可の番線までここへ入れると、同じedgeを共有する場合に後勝ちで停車不可側が
            // 引かれ、停車目標番線(=ドア定義の参照先)が取り違えられる。
            if (registeredPlatform.stoppable) graph.edgeToPlatform[edgeGuid] = registeredPlatform.id;
            // edgeToPlatforms(複数)は接近判定用なので、停車可否にかかわらず全番線を保持する。
            graph.edgeToPlatforms[edgeGuid].push_back(registeredPlatform.id);
        }
    }
}

} // namespace rs::world
