#include "Rs2Import.h"

#include "../../core/Epsilon.h"
#include "Rs2RailBlockImport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../platform/text/Encoding.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/EdgeShapeBuild.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/PointName.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/time/SolarModeNames.h"
#include "Rs2CurveFit.h"
#include "Rs2EdgeBuild.h"
#include "Rs2DiaImport.h"
#include "../../world/alignment/TrackGraphIntegrity.h"
#include "Rs2NodeBuild.h"

namespace rs::legacy_layout {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::ImportedPluginLink;
using rs::world::TrackEdge;
using rs::world::TrackGraph;
using rs::world::TrackNode;
using rs::world::TrackNodeSample;

std::string ResolveTrackPluginFolder(const std::string& installRootUtf8, const char* kindDir,
                                    const std::string& pluginName) {
    if (pluginName.empty()) return {};
    std::filesystem::path root(rs::platform_text::Utf8ToWide(installRootUtf8));
    std::filesystem::path full =
        root / rs::platform_text::Utf8ToWide(kindDir) / rs::platform_text::Utf8ToWide(pluginName);
    return rs::platform_text::WideToUtf8(full.wstring());
}

TrackGraph ImportTrackGraph(const Rs2Scene& scene, const std::string& installRootUtf8,
                            std::vector<std::string>& warnings, AddressMap* outAddresses,
                            Rs2PendingTrainRoutines* outPendingTrainRoutines) {
    TrackGraph graph;

    // パス1: 全 Address → Guid を採番。
    AddressMap addrMap;
    for (const Rs2RailConnector& c : scene.railConnectors) {
        if (Rs2AddressIsNull(c.address)) {
            warnings.push_back("RailConnector: Addressがnull、スキップ");
            continue;
        }
        if (!addrMap.connectorGuids.try_emplace(c.address, rs::world::GenerateGuid()).second) {
            warnings.push_back("RailConnector: Address " + c.address + " が重複、後の定義で上書き");
            addrMap.connectorGuids[c.address] = rs::world::GenerateGuid();
        }
    }
    for (const Rs2RailWay& w : scene.railWays) {
        if (Rs2AddressIsNull(w.address)) {
            warnings.push_back("RailWay: Addressがnull、スキップ");
            continue;
        }
        if (!addrMap.wayGuids.try_emplace(w.address, rs::world::GenerateGuid()).second) {
            warnings.push_back("RailWay: Address " + w.address + " が重複、後の定義で上書き");
            addrMap.wayGuids[w.address] = rs::world::GenerateGuid();
        }
    }

    // パス2a: ノード構築(edgeのcantA/cantB参照に先立って必要)。
    for (const Rs2RailConnector& c : scene.railConnectors) {
        if (Rs2AddressIsNull(c.address)) continue;
        std::vector<PendingPointTrainRoutine>* pendingPoints =
            outPendingTrainRoutines == nullptr ? nullptr : &outPendingTrainRoutines->points;
        TrackNode node = BuildNode(c, addrMap, warnings, pendingPoints);
        graph.nodes[node.guid] = node;
    }

    // パス2b: エッジ構築。PierLink/PoleLink解決用の索引を先に1度だけ構築する。
    const ImportedPluginIndex famIndex = BuildImportedPluginIndex(scene);

    for (const Rs2RailWay& w : scene.railWays) {
        if (Rs2AddressIsNull(w.address)) continue;
        TrackEdge edge = BuildEdge(w, addrMap, graph, installRootUtf8, famIndex, warnings);
        if (rs::world::GuidIsNil(edge.nodeA) || rs::world::GuidIsNil(edge.nodeB)) continue;
        graph.edges[edge.guid] = edge;
    }

    BuildRailBlocksFromNames(graph, scene.railWays, addrMap);

    // 駅停車統合: 番線表の構築。
    for (const Rs2PlacedInstance& placedInstance : scene.placements) {
        if (placedInstance.kind != "Station") continue;

        std::string stationName = placedInstance.diaInst.name;
        if (!placedInstance.diaInst.present || stationName.empty()) {
            // 駅名を持たない配置でも名無しにしないための仮名(UIは日本語表記)。
            stationName = "駅 " + placedInstance.address;
        }
        const ConvertedPlatformRoutines stationRoutines =
            ConvertDiaRoutineToPlatformRoutine(placedInstance.diaInst, warnings);
        const Guid stationId =
            rs::world::RegisterStation(graph, std::move(stationName), placedInstance.address);

        for (size_t platformOrder = 0; platformOrder < placedInstance.platforms.size(); ++platformOrder) {
            const Rs2PlatformInst& platformInst = placedInstance.platforms[platformOrder];
            if (platformInst.railList.empty()) continue;

            rs::world::PlatformStopInfo platformInfo;
            for (const Rs2Address& railAddress : platformInst.railList) {
                const rs::world::Guid edgeGuid = addrMap.ResolveWay(railAddress);
                if (!rs::world::GuidIsNil(edgeGuid)) platformInfo.railEdges.push_back(edgeGuid);
            }
            if (platformInfo.railEdges.empty()) {
                warnings.push_back("Platform: RailListが解決不能、スキップ");
                continue;
            }
            platformInfo.id = rs::world::GenerateGuid();
            platformInfo.stationId = stationId;
            // .rs2フォーマット自体に番線名の項目が無いため(Rs2PlatformInstにname無し)、
            // 駅内の出現順から仮名を振る(駅名の仮名フォールバックと同じ考え方)。
            platformInfo.name = std::to_string(platformOrder + 1) + "番線";
            platformInfo.stoppable = platformInst.stoppable;
            platformInfo.stationAddress = placedInstance.address;
            platformInfo.indexInStation = static_cast<int>(platformOrder);
            platformInfo.openDoor[0] = platformInst.openDoor[0];
            platformInfo.openDoor[1] = platformInst.openDoor[1];
            platformInfo.routine = stationRoutines.defaultRoutine;

            const rs::world::Guid platformId = platformInfo.id;
            rs::world::RegisterPlatform(graph, std::move(platformInfo));
            if (outPendingTrainRoutines != nullptr) {
                for (const DiaTrainGroupRoutine& entry :
                     stationRoutines.trainGroups) {
                    outPendingTrainRoutines->platforms.push_back({platformId, entry.trainGroup, entry.routine});
                }
            }
        }
    }

    const std::vector<std::string> repairs = rs::world::SanitizeTrackGraph(graph);
    warnings.insert(warnings.end(), repairs.begin(), repairs.end());
    // RailSim II の PointInst に名前が無い分岐へ既定名を振る(名前があるものはそのまま)。
    rs::world::AssignDefaultPointNames(graph);
    const std::vector<std::string> problems = rs::world::ValidateTrackGraph(graph);
    warnings.insert(warnings.end(), problems.begin(), problems.end());

    if (outAddresses) *outAddresses = addrMap;
    return graph;
}

std::vector<ImportedPlacement> ImportPlacements(const Rs2Scene& scene,
                                                const std::string& installRootUtf8,
                                                std::vector<std::string>& warnings) {
    std::vector<ImportedPlacement> out;
    out.reserve(scene.placements.size());
    for (const Rs2PlacedInstance& p : scene.placements) {
        ImportedPlacement ip;
        ip.kind = p.kind;
        ip.address = p.address;
        ip.pluginFolderUtf8 = ResolveTrackPluginFolder(installRootUtf8, p.kind.c_str(), p.pluginName);
        // PolePlugin="" の Pole は RailSim II でも正規のデータ(架線だけを敷いたときに RailSim II が作る、
        // メッシュを持たない架線の端点。RailSim II の CPole は描画せず、接続点は Pos そのもの)。
        // 描画しない扱いも架線の端点としての解決も RailSim II と同じなので、警告を出さない。
        const bool emptyPoleAnchor = p.kind == "Pole" && p.pluginName.empty();
        if (ip.pluginFolderUtf8.empty() && !emptyPoleAnchor) {
            warnings.push_back(p.kind + ": プラグイン未指定または未解決、プレースホルダ扱い");
        }
        const Vec3 up = p.hasUp ? p.up : Vec3(0.0f, 1.0f, 0.0f);
        // 架線柱(Upキー無し)は常に鉛直に立つ。OrigDirの勾配は姿勢へ反映せず、水平成分だけ使う。
        const Vec3 dir = p.hasUp ? p.dir : rs::world::HorizontalForward(p.dir);
        ip.placement = rs::world::DecomposeFreePlacement(p.pos, dir, up);
        ip.switchOptions = p.switchOptions;
        out.push_back(ip);
    }
    return out;
}

void ApplyRs2TimeToGameClock(const Rs2Time& time, rs::world::GameClock& clock,
                              rs::world::SolarOverrides& overrides,
                              std::vector<std::string>& warnings) {
    if (!time.present) return;

    const int realYear = time.year + 1;
    const int realMonth = time.month + 1;
    const int realDay = time.day + 1;
    const int64_t dayNumber = rs::world::FromDateParts({realYear, realMonth, realDay});

    // SumDaysは年初(1/1)からの通算日数のはず(CSaveFile.cppコンストラクタ参照)。
    // Dateを正としてdayNumberを決定し、SumDaysは整合性検証のみに使う
    // (不一致は保存データの想定外パターンとして警告するに留め、読込は続行する)。
    const int64_t jan1 = rs::world::FromDateParts({realYear, 1, 1});
    const int64_t expectedSumDays = dayNumber - jan1;
    if (expectedSumDays != time.sumDays) {
        warnings.push_back(
            "Time.SumDaysがDateから期待される値と不一致です(期待値=" +
            std::to_string(expectedSumDays) + ", 実値=" + std::to_string(time.sumDays) +
            ")。Dateを正として日時を設定します。");
    }

    const double secondsOfDay =
        time.hour * 3600.0 + time.minute * 60.0 + time.second + time.frame / 30.0;
    clock.SetDateTime(dayNumber, secondsOfDay);

    const int tsIdx = std::clamp(time.timeScaleIndex, 0, 9);
    const int spIdx = std::clamp(time.simulationSpeedIndex, 0, 4);
    // TimeScale(1tickあたりの進む量)×SimulationSpeed(倍速)の積が、
    // このプロジェクトのGameClock::TimeScale()(実秒あたりのゲーム内秒数)に
    // 対応する(RailSim II の CSaveFile::Simulate による)。
    clock.SetTimeScale(double(kTimeScaleTable[tsIdx]) * double(kSimSpeedTable[spIdx]));

    overrides.earthRotationMode = rs::world::EarthRotationModeFromIndex(time.earthRotationMode);
    overrides.earthRevolutionMode = rs::world::EarthRevolutionModeFromIndex(time.earthRevolutionMode);
}

} // namespace rs::legacy_layout
