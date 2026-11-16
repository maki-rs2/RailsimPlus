#include "Rs2Spike.h"
#include "../plugin_session/InstallRoot.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_map>

#include "../../platform/text/Encoding.h"
#include "../../plugins/schema/parser/Parser.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/layout/Rs2Schema.h"
#include "../../plugins/layout/PiValueAccessors.h"
#include "../../plugins/layout/Rs2Import.h"

namespace rs::app {

namespace {

using rs::legacy_pi::PiBlock;
using rs::legacy_pi::PiValue;

// PiValue を printf 用に読める文字列へ変換する(Number/String/HexAddr/HexColor/Ident)。
std::string DumpValue(const PiValue& v) {
    char buf[64];
    switch (v.kind) {
        case PiValue::Kind::Number:
            std::snprintf(buf, sizeof(buf), "%.6g", v.number);
            return buf;
        case PiValue::Kind::HexAddr:
            return "0x" + v.text;
        case PiValue::Kind::HexColor:
            return "#" + v.text;
        case PiValue::Kind::String:
            return "\"" + v.text + "\"";
        case PiValue::Kind::Ident:
            return v.text;
        case PiValue::Kind::Tuple: {
            std::string s = "(";
            for (size_t i = 0; i < v.tuple.size(); ++i) {
                if (i) s += ", ";
                s += DumpValue(v.tuple[i]);
            }
            return s + ")";
        }
    }
    return "?";
}

void DumpAssignment(const PiBlock& block, const char* key, const char* indent) {
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a) {
        std::printf("%s%s = <なし>\n", indent, key);
        return;
    }
    std::string s;
    for (size_t i = 0; i < a->values.size(); ++i) {
        if (i) s += ", ";
        s += DumpValue(a->values[i]);
    }
    std::printf("%s%s = %s\n", indent, key, s.c_str());
}

void DumpConnector(const PiBlock& conn, int idx) {
    std::printf("  --- RailConnector[%d] ---\n", idx);
    DumpAssignment(conn, "Address", "    ");
    if (const PiBlock* splitter = conn.FirstChild("Splitter")) {
        DumpAssignment(*splitter, "Pos", "    Splitter.");
        DumpAssignment(*splitter, "Dir", "    Splitter.");
        DumpAssignment(*splitter, "Up", "    Splitter.");
    } else {
        std::printf("    Splitter = <なし>\n");
    }
    DumpAssignment(conn, "Cant", "    ");
    DumpAssignment(conn, "Link00", "    ");
    DumpAssignment(conn, "Link01", "    ");
    DumpAssignment(conn, "Link10", "    ");
    DumpAssignment(conn, "Link11", "    ");
    DumpAssignment(conn, "Side", "    ");
    if (const PiBlock* pointInst = conn.FirstChild("PointInst")) {
        std::printf("    PointInst: あり\n");
        DumpAssignment(*pointInst, "Name", "      ");
        const std::vector<const PiBlock*> pointLists = pointInst->Children("PointList");
        for (size_t pointListIndex = 0; pointListIndex < pointLists.size(); ++pointListIndex) {
            std::printf("      PointList[%zu]\n", pointListIndex);
            DumpAssignment(*pointLists[pointListIndex], "TrainGroup", "        ");
            DumpAssignment(*pointLists[pointListIndex], "UseDefault", "        ");
            const std::vector<const rs::legacy_pi::PiAssignment*> points =
                rs::legacy_layout::FindAllAssignments(*pointLists[pointListIndex], "Point");
            for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
                const rs::legacy_pi::PiAssignment& point = *points[pointIndex];
                const int value = point.values.empty() ? 0 : point.values[0].AsInt(0);
                std::printf("        Point[%zu] = %d\n", pointIndex, value);
            }
        }
    } else {
        std::printf("    PointInst: なし\n");
    }
}

void DumpRailWay(const PiBlock& way, int idx) {
    std::printf("  --- RailWay[%d] ---\n", idx);
    DumpAssignment(way, "Address", "    ");
    DumpAssignment(way, "Link0", "    ");
    DumpAssignment(way, "Link1", "    ");
    DumpAssignment(way, "SpeedLimit", "    ");
    DumpAssignment(way, "RailBlock", "    ");
    DumpAssignment(way, "RailPlugin", "    ");
    DumpAssignment(way, "TiePlugin", "    ");
    DumpAssignment(way, "GirderPlugin", "    ");
    DumpAssignment(way, "PierPlugin", "    ");
    DumpAssignment(way, "LinePlugin", "    ");
    DumpAssignment(way, "PolePlugin", "    ");
    const PiBlock* splitList = way.FirstChild("SplitList");
    if (!splitList) {
        std::printf("    SplitList: <なし>\n");
        return;
    }
    std::vector<const PiBlock*> splitters = splitList->Children("Splitter");
    std::printf("    SplitList: Splitter数=%zu\n", splitters.size());
    for (size_t i = 0; i < splitters.size() && i < 3; ++i) {
        std::printf("      [%zu] ", i);
        DumpAssignment(*splitters[i], "Pos", "");
        std::printf("          ");
        DumpAssignment(*splitters[i], "Dir", "");
        std::printf("          ");
        DumpAssignment(*splitters[i], "Up", "");
    }
    if (splitters.size() > 3) {
        std::printf("      [%zu] ", splitters.size() - 1);
        DumpAssignment(*splitters.back(), "Pos", "");
    }
}

void PrintParseDiagnostics(const rs::legacy_pi::PiDocument& doc,
                           const rs::legacy_pi::Diagnostics& diag) {
    std::printf("[rs2spike] top-level blocks=%zu diagnostics=%zu (errors=%d)\n",
                doc.nodes.size(), diag.Items().size(), diag.ErrorCount());
    if (diag.HasErrors()) {
        int shown = 0;
        for (const auto& it : diag.Items()) {
            if (shown++ >= 20) break;
            std::printf("  [ERR] %d:%d %s\n", it.line, it.column, it.message.c_str());
        }
    }
}

void DumpSceneOverview(const PiBlock& scene) {
    DumpAssignment(scene, "Name", "  Scene.");
    std::printf("  Scene children (blocks=%zu, assigns=%zu):\n", scene.blocks.size(),
                scene.assignments.size());
    for (const auto& b : scene.blocks)
        std::printf("    block '%s' (children=%zu, assigns=%zu)\n", b.name.c_str(),
                    b.blocks.size(), b.assignments.size());
}

// Rs2Schema でも同じファイルを型付け変換し、件数が一致するか確認
void ReportSchemaCounts(const rs::legacy_layout::Rs2Layout& layout,
                        const std::vector<std::string>& schemaWarnings) {
    std::printf("[rs2spike] Rs2Schema: scenes=%zu trainGroups=%zu unknownBlocks=%d warnings=%zu\n",
                layout.scenes.size(), layout.trainGroups.size(), layout.unknownBlockCount,
                schemaWarnings.size());
    for (size_t i = 0; i < layout.scenes.size(); ++i) {
        std::printf("  Rs2Schema scene[%zu] '%s': connectors=%zu ways=%zu\n", i,
                    layout.scenes[i].name.c_str(), layout.scenes[i].railConnectors.size(),
                    layout.scenes[i].railWays.size());
    }
    size_t diaInstCount = 0;
    size_t diaListCount = 0;
    size_t diaElementCount = 0;
    size_t pointInstCount = 0;
    size_t pointListCount = 0;
    size_t pointElementCount = 0;
    size_t defaultDiaListCount = 0;
    size_t defaultPointListCount = 0;
    for (const rs::legacy_layout::Rs2Scene& schemaScene : layout.scenes) {
        for (const rs::legacy_layout::Rs2PlacedInstance& placement : schemaScene.placements) {
            const rs::legacy_layout::Rs2DiaInst& diaInst = placement.diaInst;
            if (!diaInst.present) continue;
            ++diaInstCount;
            for (const rs::legacy_layout::Rs2DiaRoutine& routine : diaInst.routines) {
                ++diaListCount;
                if (rs::legacy_layout::Rs2AddressIsNull(routine.trainGroup)) ++defaultDiaListCount;
                diaElementCount += routine.elements.size();
            }
        }
        for (const rs::legacy_layout::Rs2RailConnector& connector : schemaScene.railConnectors) {
            const rs::legacy_layout::Rs2PointInst& pointInst = connector.pointInst;
            if (!pointInst.present) continue;
            ++pointInstCount;
            for (const rs::legacy_layout::Rs2PointRoutine& routine : pointInst.routines) {
                ++pointListCount;
                if (rs::legacy_layout::Rs2AddressIsNull(routine.trainGroup)) ++defaultPointListCount;
                pointElementCount += routine.points.size();
            }
        }
    }
    std::printf("[rs2spike] routines(all scenes): DiaInst=%zu DiaList=%zu DiaElement=%zu "
                "PointInst=%zu PointList=%zu Point=%zu defaultDiaList=%zu defaultPointList=%zu\n",
                diaInstCount, diaListCount, diaElementCount, pointInstCount, pointListCount,
                pointElementCount, defaultDiaListCount, defaultPointListCount);
    for (size_t i = 0; i < schemaWarnings.size() && i < 20; ++i)
        std::printf("  [warn] %s\n", schemaWarnings[i].c_str());
}

// Rs2Import で最初のシーンを TrackGraph へ変換し、ノード/エッジ数・
// 総延長・AABB・trackPlugins解決を報告する。
void ReportImportedGraphStats(const rs::world::TrackGraph& graph,
                              const std::vector<std::string>& importWarnings) {
    double totalLength = 0.0;
    rs::core::Vec3 aabbMin(1e9f, 1e9f, 1e9f), aabbMax(-1e9f, -1e9f, -1e9f);
    for (const auto& [guid, edge] : graph.edges) {
        (void)guid;
        if (!edge.spline.empty()) totalLength += edge.spline.back().s;
        for (const auto& s : edge.spline) {
            aabbMin.x = std::min(aabbMin.x, s.pos.x); aabbMax.x = std::max(aabbMax.x, s.pos.x);
            aabbMin.y = std::min(aabbMin.y, s.pos.y); aabbMax.y = std::max(aabbMax.y, s.pos.y);
            aabbMin.z = std::min(aabbMin.z, s.pos.z); aabbMax.z = std::max(aabbMax.z, s.pos.z);
        }
    }
    int pointNodes = 0;
    for (const auto& [guid, node] : graph.nodes) {
        (void)guid;
        if (node.IsPoint()) ++pointNodes;
    }
    std::printf("[rs2spike] Rs2Import: nodes=%zu edges=%zu pointNodes=%d totalLength=%.1fm "
                "importWarnings=%zu\n",
                graph.nodes.size(), graph.edges.size(), pointNodes, totalLength,
                importWarnings.size());
    std::printf("  aabbMin=(%.1f,%.1f,%.1f) aabbMax=(%.1f,%.1f,%.1f)\n", aabbMin.x, aabbMin.y,
                aabbMin.z, aabbMax.x, aabbMax.y, aabbMax.z);
    for (size_t i = 0; i < importWarnings.size() && i < 20; ++i)
        std::printf("  [import-warn] %s\n", importWarnings[i].c_str());
}

        // trackPlugins に積まれた全プラグインフォルダを重複排除し、
        // 実際にロードできるか(かつRail種別はメッシュ生成まで通るか)をヘッドレスで確認する。
void ReportTrackPluginLoadability(const rs::world::TrackGraph& graph) {
    std::vector<std::string> uniqueFolders;
    for (const auto& [guid, edge] : graph.edges) {
        (void)guid;
        for (const std::string& folder : rs::world::ActiveFolders(edge))
            if (std::find(uniqueFolders.begin(), uniqueFolders.end(), folder) ==
                uniqueFolders.end())
                uniqueFolders.push_back(folder);
    }
    int loadOk = 0, loadFail = 0, railMeshOk = 0;
    for (const auto& folder : uniqueFolders) {
        std::wstring folderW = rs::platform_text::Utf8ToWide(folder);
        rs::legacy_plugin::LoadOptions opt;
        rs::legacy_plugin::LoadedPlugin lp = rs::legacy_plugin::LoadPluginFolder(folderW, opt);
        if (!lp.ok) { ++loadFail; continue; }
        ++loadOk;
        if (lp.def.header.type == rs::legacy_pi::PluginType::Rail &&
            !lp.def.railProfiles.empty()) {
            // 実際にこのフォルダを参照している最初のedgeのsplineで押し出しを試す。
            for (const auto& [eguid, edge] : graph.edges) {
                (void)eguid;
                const bool refs = rs::world::EdgeHasFolder(edge, folder);
                if (!refs || edge.spline.size() < 2) continue;
                std::vector<std::string> meshLog;
                int missing = 0;
                rs::render::Mesh mesh = rs::legacy_plugin::BuildRailMeshAlongSpline(
                    lp.def, folderW, edge.spline, meshLog, missing);
                if (!mesh.vertices.empty()) ++railMeshOk;
                break;
            }
        }
    }
    std::printf("[rs2spike] Rs2Import trackPlugins: uniqueFolders=%zu loadOk=%d loadFail=%d "
                "railMeshOk=%d\n",
                uniqueFolders.size(), loadOk, loadFail, railMeshOk);
}

        // Struct/Pole/Pier/Stationの配置物をImportedPlacementへ変換し、
        // プラグイン解決成功率をヘッドレスで確認する。
void ReportPlacementResolution(const rs::legacy_layout::Rs2Scene& firstScene,
                               const std::string& installRootUtf8) {
    std::vector<std::string> placementWarnings;
    std::vector<rs::legacy_layout::ImportedPlacement> placements =
        rs::legacy_layout::ImportPlacements(firstScene, installRootUtf8, placementWarnings);
    int placeOk = 0, placeMissing = 0, placeLoadOk = 0, placeLoadFail = 0;
    std::unordered_map<std::string, int> kindCount;
    for (const auto& p : placements) {
        ++kindCount[p.kind];
        if (p.pluginFolderUtf8.empty()) { ++placeMissing; continue; }
        ++placeOk;
        rs::legacy_plugin::LoadOptions opt;
        rs::legacy_plugin::LoadedPlugin lp = rs::legacy_plugin::LoadPluginFolder(
            rs::platform_text::Utf8ToWide(p.pluginFolderUtf8), opt);
        if (lp.ok) ++placeLoadOk; else ++placeLoadFail;
    }
    std::printf("[rs2spike] Rs2Import placements: total=%zu resolved=%d missing=%d "
                "loadOk=%d loadFail=%d\n",
                placements.size(), placeOk, placeMissing, placeLoadOk, placeLoadFail);
    for (const auto& [kind, count] : kindCount)
        std::printf("  kind=%s count=%d\n", kind.c_str(), count);
    // デバッグ: 未解決(pluginFolderUtf8が空)の最初の5件のkindとRs2Layout側の生pluginNameを出す
    int shownMissing = 0;
    for (size_t i = 0; i < firstScene.placements.size() && shownMissing < 5; ++i) {
        const rs::legacy_layout::Rs2PlacedInstance& raw = firstScene.placements[i];
        if (!raw.pluginName.empty()) continue;
        std::printf("  [missing-debug] kind=%s pluginName='%s' pos=(%.2f,%.2f,%.2f)\n",
                    raw.kind.c_str(), raw.pluginName.c_str(), raw.pos.x, raw.pos.y, raw.pos.z);
        ++shownMissing;
    }
}

} // namespace

int RunRs2Spike(const std::string& utf8Path) {
    std::wstring w = rs::platform_text::Utf8ToWide(utf8Path);
    rs::legacy_pi::Diagnostics diag;
    rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(w, diag);
    PrintParseDiagnostics(doc, diag);

    const PiBlock* sceneList = nullptr;
    for (const auto& n : doc.nodes)
        if (n.name == "SceneList") sceneList = &n;
    if (!sceneList) {
        std::printf("[rs2spike] SceneList not found\n");
        return 1;
    }
    std::vector<const PiBlock*> scenes = sceneList->Children("Scene");
    std::printf("[rs2spike] Scene count=%zu\n", scenes.size());
    if (scenes.empty()) return 1;
    const PiBlock& scene = *scenes[0];
    DumpSceneOverview(scene);

    const PiBlock* connList = scene.FirstChild("RailConnectorList");
    const PiBlock* wayList = scene.FirstChild("RailWayList");
    std::vector<const PiBlock*> conns =
        connList ? connList->Children("RailConnector") : std::vector<const PiBlock*>{};
    std::vector<const PiBlock*> ways =
        wayList ? wayList->Children("RailWay") : std::vector<const PiBlock*>{};
    std::printf("[rs2spike] RailConnector count=%zu, RailWay count=%zu\n", conns.size(),
                ways.size());

    // 先頭数件をダンプ
    for (size_t i = 0; i < conns.size() && i < 3; ++i) DumpConnector(*conns[i], (int)i);
    for (size_t i = 0; i < ways.size() && i < 3; ++i) DumpRailWay(*ways[i], (int)i);

    std::vector<std::string> schemaWarnings;
    rs::legacy_layout::Rs2Layout layout = rs::legacy_layout::ParseRs2Layout(doc, schemaWarnings);
    ReportSchemaCounts(layout, schemaWarnings);

    const std::filesystem::path installRoot = ResolveInstallRootFromFilePath(std::filesystem::path(w));
    const std::string installRootUtf8 = rs::platform_text::WideToUtf8(installRoot.wstring());
    if (!layout.scenes.empty()) {
        std::vector<std::string> importWarnings;
        rs::world::TrackGraph graph = rs::legacy_layout::ImportTrackGraph(
            layout.scenes[0], installRootUtf8, importWarnings, nullptr, nullptr);
        ReportImportedGraphStats(graph, importWarnings);
        ReportTrackPluginLoadability(graph);
        ReportPlacementResolution(layout.scenes[0], installRootUtf8);
    }

    return 0;
}

} // namespace rs::app
