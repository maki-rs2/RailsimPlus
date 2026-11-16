#include "TrackGraphSerialize.h"
#include "TrackGraphIntegrity.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <vector>

#include "BlockRegistry.h"
#include "Alignment.h"
#include "AutoCant.h"
#include "GuidJson.h"
#include "JsonReadValue.h"
#include "PlatformRegistry.h"
#include "PointName.h"
#include "PlatformStopInfoSerialize.h"
#include "SpeedSignalRegistry.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "track_graph_serialize";

nlohmann::json BlockToJson(const Block& block) {
    nlohmann::json edges = nlohmann::json::array();
    for (const Guid& edge : block.edges) {
        edges.push_back(GuidToJson(edge));
    }
    nlohmann::json result = {
        { "guid", GuidToJson(block.guid) },
        { "edges", edges },
    };
    if (!block.name.empty()) result["name"] = block.name;
    return result;
}

Block BlockFromJson(const nlohmann::json& json) {
    Block result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.guid = GuidFromJson(json["guid"]);
    }
    result.name = JsonReadValue(json, "name", result.name, kLogTag);
    if (json.contains("edges") && json["edges"].is_array()) {
        for (const nlohmann::json& item : json["edges"]) {
            result.edges.push_back(GuidFromJson(item));
        }
    }
    return result;
}

nlohmann::json SpeedSignalToJson(const SpeedSignal& signal) {
    nlohmann::json stages = nlohmann::json::array();
    for (const SignalAspectStage& stage : signal.stages) {
        stages.push_back({ { "speedKmh", stage.speedKmh }, { "sidecarIndex", stage.sidecarIndex } });
    }
    nlohmann::json result = {
        { "guid", GuidToJson(signal.guid) },
        { "edge", GuidToJson(signal.edge) },
        { "s", signal.s },
        { "direction", signal.direction },
        { "stages", std::move(stages) },
    };
    // キー名は"plugin"固定: PluginRootResolverがこのキーだけをプラグインルート基準の相対パスへ変換する。
    if (!signal.pluginFolderUtf8.empty()) result["plugin"] = signal.pluginFolderUtf8;
    if (!signal.switchOptions.empty()) result["switchOptions"] = signal.switchOptions;
    // 担当閉塞は主信号だけが持つ。
    if (rs::world::IsBlockBoundarySignal(signal.kind)) result["block"] = GuidToJson(signal.block);
    // 主信号は省略する(旧ファイルと同じ形)。
    switch (signal.kind) {
    case SpeedSignalKind::Main: break;
    case SpeedSignalKind::Relay: result["kind"] = "relay"; break;
    case SpeedSignalKind::Distant: result["kind"] = "distant"; break;
    case SpeedSignalKind::Shunting: result["kind"] = "shunting"; break;
    }
    return result;
}

SpeedSignal SpeedSignalFromJson(const nlohmann::json& json) {
    SpeedSignal result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.guid = GuidFromJson(json["guid"]);
    }
    if (json.contains("edge")) {
        result.edge = GuidFromJson(json["edge"]);
    }
    result.s = JsonReadValue(json, "s", result.s, kLogTag);
    result.direction = JsonReadValue(json, "direction", result.direction, kLogTag) < 0 ? -1 : +1;
    // 旧形式の単一の"speedKmh"は読まない(stagesが空=制限なしの信号として残る)。
    if (json.contains("stages") && json["stages"].is_array()) {
        for (const nlohmann::json& item : json["stages"]) {
            if (!item.is_object()) continue;
            SignalAspectStage stage;
            stage.speedKmh = std::max(0, JsonReadValue(item, "speedKmh", stage.speedKmh, kLogTag));
            stage.sidecarIndex = std::max(0, JsonReadValue(item, "sidecarIndex", stage.sidecarIndex, kLogTag));
            result.stages.push_back(stage);
        }
    }
    if (json.contains("plugin") && json["plugin"].is_string()) {
        result.pluginFolderUtf8 = json["plugin"].get<std::string>();
    }
    if (json.contains("switchOptions")) {
        result.switchOptions = JsonReadValue(json, "switchOptions", result.switchOptions, kLogTag);
    }
    if (json.contains("kind") && json["kind"].is_string()) {
        const std::string kind = json["kind"].get<std::string>();
        if (kind == "relay") result.kind = SpeedSignalKind::Relay;
        else if (kind == "distant") result.kind = SpeedSignalKind::Distant;
        else if (kind == "shunting") result.kind = SpeedSignalKind::Shunting;
    }
    if (rs::world::IsBlockBoundarySignal(result.kind) && json.contains("block")) {
        result.block = GuidFromJson(json["block"]);
    }
    return result;
}

}  // namespace

nlohmann::json StationInfoToJson(const StationInfo& station) {
    nlohmann::json result = {
        { "guid", GuidToJson(station.id) },
        { "name", station.name },
        { "rs2Address", station.sourceAddress },
    };
    return result;
}

StationInfo StationInfoFromJson(const nlohmann::json& json) {
    StationInfo result;
    if (!json.is_object()) {
        return result;
    }
    if (json.contains("guid")) {
        result.id = GuidFromJson(json["guid"]);
    }
    result.name = JsonReadValue(json, "name", result.name, kLogTag);
    result.sourceAddress = JsonReadValue(json, "rs2Address", result.sourceAddress, kLogTag);
    return result;
}

namespace {
// 保存の並びをハッシュ表の並びに依存させない(同じグラフが毎回同じ出力になる)ため、Guid順に並べる。
template <typename Map>
std::vector<Guid> SortedKeys(const Map& map) {
    std::vector<Guid> keys;
    keys.reserve(map.size());
    for (const auto& pair : map) keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end());
    return keys;
}
}  // namespace

nlohmann::json TrackGraphToJson(const TrackGraph& graph) {
    nlohmann::json nodes = nlohmann::json::array();
    for (const Guid& guid : SortedKeys(graph.nodes)) {
        nodes.push_back(NodeToJson(graph.nodes.at(guid), &graph));
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const Guid& guid : SortedKeys(graph.edges)) {
        edges.push_back(EdgeToJson(graph.edges.at(guid)));
    }
    nlohmann::json stations = nlohmann::json::array();
    for (const StationInfo& station : graph.stations) {
        stations.push_back(StationInfoToJson(station));
    }
    nlohmann::json platforms = nlohmann::json::array();
    for (const PlatformStopInfo& platform : graph.platforms) {
        platforms.push_back(PlatformStopInfoToJson(platform));
    }
    nlohmann::json blocks = nlohmann::json::array();
    for (const Block& block : graph.blocks) {
        blocks.push_back(BlockToJson(block));
    }
    nlohmann::json speedSignals = nlohmann::json::array();
    for (const SpeedSignal& signal : graph.speedSignals) {
        speedSignals.push_back(SpeedSignalToJson(signal));
    }
    return {
        { "nodes", nodes },
        { "edges", edges },
        { "stations", stations },
        { "platforms", platforms },
        { "blocks", blocks },
        { "speedSignals", speedSignals },
    };
}

TrackGraph TrackGraphFromJson(const nlohmann::json& json) {
    TrackGraph result;
    if (!json.is_object()) {
        return result;
    }
    TrackGraphGuids used;
    std::vector<std::string> warnings;
    // edgeEnd が書かれていないリンク(node の guid → node.links の添字)。edge が出そろってから決める。
    std::vector<std::pair<Guid, std::vector<size_t>>> linksWithoutEnd;
    if (json.contains("nodes") && json["nodes"].is_array()) {
        for (const nlohmann::json& item : json["nodes"]) {
            std::vector<size_t> withoutEnd;
            TrackNode node = NodeFromJson(item, &withoutEnd);
            SanitizeTrackGraphGuid(node.guid, used, warnings);
            if (!withoutEnd.empty()) linksWithoutEnd.emplace_back(node.guid, std::move(withoutEnd));
            result.nodes[node.guid] = std::move(node);
        }
    }
    if (json.contains("edges") && json["edges"].is_array()) {
        for (const nlohmann::json& item : json["edges"]) {
            TrackEdge edge = EdgeFromJson(item);
            SanitizeTrackGraphGuid(edge.guid, used, warnings);
            result.edges[edge.guid] = std::move(edge);
        }
    }
    // 端を書かないリンクは、edge の接続側ノードがこのノードと一致する端にする。
    // 一致する端が決まらない(edge が無い・両端が同じ)リンクは0のままにして、整合の修復に任せる。
    for (const auto& [nodeGuid, indices] : linksWithoutEnd) {
        TrackNode& node = result.nodes.at(nodeGuid);
        for (const size_t index : indices) {
            TrackNode::Link& link = node.links[index];
            const auto edge = result.edges.find(link.edge);
            if (edge == result.edges.end()) continue;
            link.end = EdgeEndAtNode(edge->second, nodeGuid).value_or(0);
        }
    }
    // 派生する形を作れない場合も、元のedge定義は残して検証の警告にする。
    for (std::pair<const Guid, TrackEdge>& pair : result.edges) {
        const EdgeRebuildMethod method = RebuildEdgeSplineWithFallback(result, pair.second);
        if (method == EdgeRebuildMethod::Relaxed) {
            spdlog::warn("[{}] edge {} は許容をゆるめて作りました", kLogTag, GuidToHex(pair.first));
        } else if (method == EdgeRebuildMethod::Straight) {
            spdlog::warn("[{}] edge {} は姿勢から作れず、両端を結ぶ直線で代用しました(端の向きがノードと合いません)",
                         kLogTag, GuidToHex(pair.first));
        } else if (method == EdgeRebuildMethod::Failed) {
            warnings.push_back("TrackGraph " + GuidToHex(pair.first) + ": edgeの形を作れませんでした。定義を保持します");
        }
    }
    //自動カントの edge は、隣の curve が出そろってからつなぎ目の値を決め直す。
    std::vector<Guid> allEdges;
    for (const std::pair<const Guid, TrackEdge>& pair : result.edges) allEdges.push_back(pair.first);
    RebuildAutoCantAround(result, allEdges);
    if (json.contains("stations") && json["stations"].is_array()) {
        for (const nlohmann::json& item : json["stations"]) {
            result.stations.push_back(StationInfoFromJson(item));
        }
    }
    if (json.contains("platforms") && json["platforms"].is_array()) {
        for (const nlohmann::json& item : json["platforms"]) {
            result.platforms.push_back(PlatformStopInfoFromJson(item));
        }
    }
    if (json.contains("blocks") && json["blocks"].is_array()) {
        for (const nlohmann::json& item : json["blocks"]) {
            result.blocks.push_back(BlockFromJson(item));
        }
    }
    // 古い"signals"キーとBlockのsignalSpeedKmhは読み捨てる(変換しない)。
    if (json.contains("speedSignals") && json["speedSignals"].is_array()) {
        for (const nlohmann::json& item : json["speedSignals"]) {
            SpeedSignal signal = SpeedSignalFromJson(item);
            result.speedSignals.push_back(signal);
        }
    }
    const std::vector<std::string> repairs = SanitizeTrackGraph(result);
    warnings.insert(warnings.end(), repairs.begin(), repairs.end());
    // 名前を持たない古い保存データの分岐に、既定名を振る。
    AssignDefaultPointNames(result);
    const std::vector<std::string> problems = ValidateTrackGraph(result);
    warnings.insert(warnings.end(), problems.begin(), problems.end());
    for (const std::string& warning : warnings) spdlog::warn("[{}] {}", kLogTag, warning);
    return result;
}

}  // namespace rs::world
