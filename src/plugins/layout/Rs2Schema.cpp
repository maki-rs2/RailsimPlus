#include "Rs2Schema.h"

#include <unordered_set>

#include "PiValueAccessors.h"
#include "Rs2DiaSchema.h"
#include "Rs2RailSchema.h"

namespace rs::legacy_layout {

namespace {

using rs::legacy_pi::PiAssignment;
using rs::legacy_pi::PiBlock;
using rs::legacy_pi::PiValue;

// TrainGroup 直下の走行状態が持つ DiaElement(実行中の1要素のスナップショット)。
// 駅の DiaInst 配下の要素と構文が同じなので Rs2DiaSchema のパーサを共用する。
Rs2DiaElement ParseDiaElement(const PiBlock& block) {
    const PiBlock* element = block.FirstChild("DiaElement");
    if (!element) return {};
    return ParseDiaElementBlock(*element);
}

Rs2Train ParseTrain(const PiBlock& train) {
    Rs2Train t;
    t.trainPlugin = GetString(train, "TrainPlugin");
    t.pos = GetVec3(train, "Pos");
    t.dir = GetVec3(train, "Dir");
    t.up = GetVec3(train, "Up");
    t.reverse = GetYesNo(train, "Reverse", false);
    t.switchOptions = GetIntArray(train, "SwitchOption");
    return t;
}

// GroupEnd0/GroupEnd1/Seeker { Address = ...; Location = <side>, <offset>, <RailWayAddress>; }
// (RailSim II の CGroupEndLocator 相当)。offsetは経路累積値で位置復元には
// 使えないため解釈せずそのまま保持する(Rs2GroupEndLocatorコメント参照)。
Rs2GroupEndLocator ParseGroupEndLocator(const PiBlock& group, const char* key) {
    Rs2GroupEndLocator loc;
    const PiBlock* end = group.FirstChild(key);
    if (!end) return loc;
    loc.present = true;
    loc.address = GetAddress(*end, "Address");
    const PiAssignment* location = end->Find("Location");
    if (location && location->values.size() >= 3) {
        loc.side = location->values[0].AsInt(0);
        loc.offset = location->values[1].AsFloat(0.0);
        loc.railAddress = AsAddress(location->values[2]);
    }
    return loc;
}

// PointList/SeekList = <RailConnector Address>, ...; (出現しないことがある)
std::vector<Rs2Address> ParseAddressList(const PiBlock& group, const char* key) {
    std::vector<Rs2Address> out;
    const PiAssignment* a = group.Find(key);
    if (!a) return out;
    out.reserve(a->values.size());
    for (const PiValue& v : a->values) out.push_back(AsAddress(v));
    return out;
}

// PlatformList > Platform { Address; Stoppable; OpenDoor; RailList; }
Rs2PlatformInst ParsePlatformInst(const PiBlock& platform) {
    Rs2PlatformInst p;
    p.address = GetAddress(platform, "Address");
    const PiAssignment* stoppable = platform.Find("Stoppable");
    p.stoppable = !stoppable || stoppable->values.empty() || stoppable->values[0].IsYes();
    const PiAssignment* openDoor = platform.Find("OpenDoor");
    if (openDoor && openDoor->values.size() >= 2) {
        p.openDoor[0] = openDoor->values[0].IsYes();
        p.openDoor[1] = openDoor->values[1].IsYes();
    }
    p.railList = ParseAddressList(platform, "RailList");
    return p;
}

// Enabled=yes のときのみ現れる走行状態一式。パースのみでランタイム未反映。
Rs2TrainGroupRunState ParseTrainGroupRunState(const PiBlock& group) {
    Rs2TrainGroupRunState rs;
    rs.present = true;
    rs.state = GetInt(group, "State", 0);
    rs.targetSpeed = GetDouble(group, "TargetSpeed", 0.0);
    rs.currentSpeed = GetDouble(group, "CurrentSpeed", 0.0);
    rs.stopTarget = GetDouble(group, "StopTarget", 0.0);
    if (const PiAssignment* dep = group.Find("DepartureTime"); dep && dep->values.size() >= 2) {
        rs.departureTimeRaw[0] = dep->values[0].AsString();
        rs.departureTimeRaw[1] = dep->values[1].AsString();
    }
    rs.doorWait = GetInt(group, "DoorWait", 0);
    if (const PiAssignment* openDoor = group.Find("OpenDoor"); openDoor && openDoor->values.size() >= 2) {
        rs.openDoor[0] = openDoor->values[0].IsYes();
        rs.openDoor[1] = openDoor->values[1].IsYes();
    }
    rs.reverse = GetYesNo(group, "Reverse", false);
    rs.groupEnd[0] = ParseGroupEndLocator(group, "GroupEnd0");
    rs.groupEnd[1] = ParseGroupEndLocator(group, "GroupEnd1");
    rs.seeker = ParseGroupEndLocator(group, "Seeker");
    rs.pointList = ParseAddressList(group, "PointList");
    rs.seekList = ParseAddressList(group, "SeekList");
    rs.dia = ParseDiaElement(group);
    rs.platform = GetAddress(group, "Platform");
    return rs;
}

Rs2TrainGroup ParseTrainGroup(const PiBlock& group) {
    Rs2TrainGroup g;
    g.address = GetAddress(group, "Address");
    g.name = GetString(group, "Name");
    g.enabled = GetYesNo(group, "Enabled", false);
    for (const PiBlock* train : group.Children("Train")) g.trains.push_back(ParseTrain(*train));
    if (g.enabled) g.runState = ParseTrainGroupRunState(group);
    return g;
}

// トップレベル/Scene直下で「読んで保持するだけ・無視してよい」既知ブロック
// 未知ブロック警告の対象から除外するためのリスト。
// "Time" はParseTimeで個別にパースするため、ここには含めない
// (ParseRs2Layoutのif/else if連鎖でTimeが先に処理されるため実害はないが、
// 「もう無視していない」ことを明示するため除外する)。
bool IsKnownIgnoredTopLevelBlock(const std::string& name) {
    // "RailBlockList" は閉塞名→占有中の編成という実行時の占有スナップショットで
    // あり、レイアウトデータではないため無視したままでよい(RailWay.RailBlockの
    // 閉塞名自体はParseRailWayで別途読む)。
    return name == "LayoutInfo" || name == "Simulation" ||
           name == "Wind" || name == "RailBlockList" ||
           name == "Window";
}

// WarpList.Warp{ Address; Link0/Link1; }(Rs2Warp参照)。
Rs2Warp ParseWarp(const PiBlock& warp) {
    Rs2Warp w;
    w.address = GetAddress(warp, "Address");
    w.link0 = ParseWayEndLink(warp, "Link0");
    w.link1 = ParseWayEndLink(warp, "Link1");
    return w;
}

// warpAddresses に含まれるAddressを指すRailConnectorスロットへwarp=trueを立てる。
// 全Sceneパース後にParseRs2Layoutから呼ぶ
// (WarpListがSceneListより後に出現する保存順に依存しないため)。
void MarkWarpSlots(Rs2Layout& layout, const std::unordered_set<Rs2Address>& warpAddresses) {
    if (warpAddresses.empty()) return;
    for (Rs2Scene& scene : layout.scenes) {
        for (Rs2RailConnector& conn : scene.railConnectors) {
            for (Rs2LinkSlot* slot : {&conn.link00, &conn.link01, &conn.link10, &conn.link11}) {
                if (slot->Populated() && warpAddresses.count(slot->target) > 0) slot->warp = true;
            }
        }
    }
}

// Time ブロック(Rs2Time参照)。
Rs2Time ParseTime(const PiBlock& block) {
    Rs2Time t;
    t.present = true;
    const std::vector<int32_t> date = GetIntArray(block, "Date");
    if (date.size() > 0) t.year = date[0];
    if (date.size() > 1) t.month = date[1];
    if (date.size() > 2) t.day = date[2];
    if (date.size() > 3) t.dayOfWeek = date[3];
    const std::vector<int32_t> time = GetIntArray(block, "Time");
    if (time.size() > 0) t.hour = time[0];
    if (time.size() > 1) t.minute = time[1];
    if (time.size() > 2) t.second = time[2];
    if (time.size() > 3) t.frame = time[3];
    t.sumDays = GetInt(block, "SumDays", 0);
    t.timeScaleIndex = GetInt(block, "TimeScale", t.timeScaleIndex);
    t.simulationSpeedIndex = GetInt(block, "SimulationSpeed", t.simulationSpeedIndex);
    t.earthRotationMode = GetInt(block, "EarthRotation", 0);
    t.earthRevolutionMode = GetInt(block, "EarthRevolution", 0);
    return t;
}

// Scene直下 Camera{ Head; Pitch; Dist; FieldOfView; Focus=(x,y,z); }(Rs2Camera参照)。
Rs2Camera ParseCamera(const PiBlock& block) {
    Rs2Camera c;
    c.present = true;
    c.head = GetFloat(block, "Head", 0.0f);
    c.pitch = GetFloat(block, "Pitch", 0.0f);
    c.dist = GetFloat(block, "Dist", 30.0f);
    c.fieldOfView = GetFloat(block, "FieldOfView", 0.0f);
    c.focus = GetVec3(block, "Focus");
    return c;
}

// Scene直下 LineList{Line{...}}(架線スパン)。
Rs2Line ParseLine(const PiBlock& block) {
    Rs2Line l;
    l.address = GetAddress(block, "Address");
    l.linePluginName = GetString(block, "LinePlugin");
    l.right = GetVec3(block, "Right");
    l.dir = GetVec3(block, "Dir");
    l.link0 = ParseWayEndLink(block, "Link0");
    l.link1 = ParseWayEndLink(block, "Link1");
    return l;
}

// Struct/Station: Pos/Dir/Up + プラグイン参照(キー名は種別ごとに指定)。Addressは駅舎の
// 番線表(PlatformStopInfo::stationAddress)と配置エンティティ(SceneEntity::sourceAddress)を
// 突き合わせる鍵になるため必ず読む(欠けると駅舎の_APPROACHスイッチが永久に0のままになる)。
Rs2PlacedInstance ParsePosDirUpInstance(const PiBlock& block, const char* kind,
                                       const char* pluginKey) {
    Rs2PlacedInstance inst;
    inst.kind = kind;
    inst.address = GetAddress(block, "Address");
    inst.pluginName = GetString(block, pluginKey);
    inst.pos = GetVec3(block, "Pos");
    inst.dir = GetVec3(block, "Dir");
    inst.up = GetVec3(block, "Up");
    inst.hasUp = true;
    inst.switchOptions = GetIntArray(block, "SwitchOption");
    return inst;
}

// Pole: Pos + OrigDir(Upキーが無いためhasUp=falseにする)。
Rs2PlacedInstance ParsePoleInstance(const PiBlock& block) {
    Rs2PlacedInstance inst;
    inst.kind = "Pole";
    inst.address = GetAddress(block, "Address");
    inst.pluginName = GetString(block, "PolePlugin");
    inst.pos = GetVec3(block, "Pos");
    inst.dir = GetVec3(block, "OrigDir");
    inst.hasUp = false;
    inst.switchOptions = GetIntArray(block, "SwitchOption");
    return inst;
}

// Pier: JointPos/JointDir/JointUp + プラグイン参照。Addressは、RailWay埋め込み
// PierList.PierLinkからの厳密紐付けに必要なため読む。
Rs2PlacedInstance ParsePierInstance(const PiBlock& block) {
    Rs2PlacedInstance inst;
    inst.kind = "Pier";
    inst.address = GetAddress(block, "Address");
    inst.pluginName = GetString(block, "PierPlugin");
    inst.pos = GetVec3(block, "JointPos");
    inst.dir = GetVec3(block, "JointDir");
    inst.up = GetVec3(block, "JointUp");
    inst.hasUp = true;
    inst.switchOptions = GetIntArray(block, "SwitchOption");
    return inst;
}

Rs2Scene ParseScene(const PiBlock& scene) {
    Rs2Scene s;
    s.address = GetAddress(scene, "Address");
    s.name = GetString(scene, "Name");
    s.envPlugin = GetString(scene, "EnvPlugin");
    if (const PiBlock* camera = scene.FirstChild("Camera")) s.camera = ParseCamera(*camera);
    if (const PiBlock* connList = scene.FirstChild("RailConnectorList")) {
        for (const PiBlock* c : connList->Children("RailConnector"))
            s.railConnectors.push_back(ParseRailConnector(*c));
    }
    if (const PiBlock* wayList = scene.FirstChild("RailWayList")) {
        for (const PiBlock* w : wayList->Children("RailWay")) s.railWays.push_back(ParseRailWay(*w));
    }
    // Scene直下 SurfacePlugin(地形)。
    // Struct等と異なりScene1個につき1枚のみで、Pos/Dir/Upを持たない(地形プラグイン
    // 自体の原点をシーン原点に置く。ハイトマップ等の地形編集は扱わない)。既存のStruct/Pole/Pier/Station経路へ合流させる
    // ため、ここでもRs2PlacedInstanceとしてplacementsへ積む。
    const std::string surfacePlugin = GetString(scene, "SurfacePlugin");
    if (!surfacePlugin.empty()) s.placements.push_back(MakeSurfaceInstance(surfacePlugin));
    if (const PiBlock* structList = scene.FirstChild("StructList")) {
        for (const PiBlock* b : structList->Children("Struct"))
            s.placements.push_back(ParsePosDirUpInstance(*b, "Struct", "StructPlugin"));
    }
    if (const PiBlock* stationList = scene.FirstChild("StationList")) {
        for (const PiBlock* b : stationList->Children("Station")) {
            Rs2PlacedInstance inst = ParsePosDirUpInstance(*b, "Station", "StationPlugin");
            if (const PiBlock* platformList = b->FirstChild("PlatformList")) {
                for (const PiBlock* p : platformList->Children("Platform"))
                    inst.platforms.push_back(ParsePlatformInst(*p));
            }
            inst.diaInst = ParseDiaInst(*b);
            s.placements.push_back(std::move(inst));
        }
    }
    if (const PiBlock* poleList = scene.FirstChild("PoleList")) {
        for (const PiBlock* b : poleList->Children("Pole"))
            s.placements.push_back(ParsePoleInstance(*b));
    }
    if (const PiBlock* pierList = scene.FirstChild("PierList")) {
        for (const PiBlock* b : pierList->Children("Pier"))
            s.placements.push_back(ParsePierInstance(*b));
    }
    if (const PiBlock* lineList = scene.FirstChild("LineList")) {
        for (const PiBlock* b : lineList->Children("Line")) s.lines.push_back(ParseLine(*b));
    }
    return s;
}

} // namespace

Rs2PlacedInstance MakeSurfaceInstance(const std::string& pluginName) {
    Rs2PlacedInstance inst;
    inst.kind = "Surface";
    inst.pluginName = pluginName;
    inst.pos = rs::core::Vec3(0.0f, 0.0f, 0.0f);
    inst.dir = rs::core::Vec3(0.0f, 0.0f, 1.0f);
    inst.up = rs::core::Vec3(0.0f, 1.0f, 0.0f);
    inst.hasUp = true;
    return inst;
}

Rs2Layout ParseRs2Layout(const rs::legacy_pi::PiDocument& doc, std::vector<std::string>& warnings) {
    Rs2Layout layout;

    const PiBlock* header = nullptr;
    const PiBlock* sceneList = nullptr;
    const PiBlock* trainGroupList = nullptr;
    const PiBlock* warpList = nullptr;
    for (const auto& n : doc.nodes) {
        if (n.name == "DatafileHeader") {
            header = &n;
        } else if (n.name == "SceneList") {
            sceneList = &n;
        } else if (n.name == "TrainGroupList") {
            trainGroupList = &n;
        } else if (n.name == "WarpList") {
            warpList = &n;
        } else if (n.name == "Time") {
            layout.time = ParseTime(n);
        } else if (!IsKnownIgnoredTopLevelBlock(n.name)) {
            ++layout.unknownBlockCount;
            warnings.push_back("未知の top-level ブロック: " + n.name);
        }
    }

    // WarpListはSceneをパースする前に構築する(ファイル内でのブロック順に依存しないため。
    // MarkWarpSlotsはこの後Sceneパース後に呼ぶ)。
    if (warpList) {
        for (const PiBlock* w : warpList->Children("Warp")) layout.warps.push_back(ParseWarp(*w));
    }

    if (header) {
        layout.railSimVersion = GetString(*header, "RailSimVersion");
        layout.datafileType = GetString(*header, "DatafileType");
    } else {
        warnings.push_back("DatafileHeader が見つかりません");
    }

    if (sceneList) {
        layout.currentScene = GetAddress(*sceneList, "CurrentScene");
        for (const PiBlock* scene : sceneList->Children("Scene")) {
            layout.scenes.push_back(ParseScene(*scene));
            for (const auto& b : scene->blocks) {
                if (b.name != "RailConnectorList" && b.name != "RailWayList" &&
                    b.name != "StructList" && b.name != "StationList" &&
                    b.name != "PoleList" && b.name != "PierList" &&
                    b.name != "LineList" && b.name != "Camera") {
                    ++layout.unknownBlockCount;
                    warnings.push_back("未知の Scene 内ブロック: " + b.name);
                }
            }
        }
    } else {
        warnings.push_back("SceneList が見つかりません");
    }

    // 全Sceneパース後に、WarpのAddress集合を使ってRailConnectorスロットへwarp印を付ける
    // (Rs2NodeBuild.cppがこの印でwarpスロットをTrackNode::linksから除外する)。
    if (!layout.warps.empty()) {
        std::unordered_set<Rs2Address> warpAddresses;
        for (const Rs2Warp& w : layout.warps) warpAddresses.insert(w.address);
        MarkWarpSlots(layout, warpAddresses);
    }

    if (trainGroupList) {
        for (const PiBlock* group : trainGroupList->Children("TrainGroup"))
            layout.trainGroups.push_back(ParseTrainGroup(*group));
    }

    return layout;
}

} // namespace rs::legacy_layout
