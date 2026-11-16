#include "Rs2RailSchema.h"

#include "PiValueAccessors.h"
#include "Rs2DiaSchema.h"

namespace rs::legacy_layout {

using rs::legacy_pi::PiAssignment;
using rs::legacy_pi::PiBlock;

namespace {

Rs2Splitter ParseSplitter(const PiBlock& splitter) {
    Rs2Splitter s;
    s.pos = GetVec3(splitter, "Pos");
    s.dir = GetVec3(splitter, "Dir");
    s.up = GetVec3(splitter, "Up");
    return s;
}

// Link00/01/10/11 = <endIdx>, <ConnectorAddress|RailWayAddress>;
Rs2LinkSlot ParseLinkSlot(const PiBlock& block, const char* key) {
    Rs2LinkSlot slot;
    const PiAssignment* a = block.Find(key);
    if (!a || a->values.empty()) return slot;
    slot.endIdx = a->values[0].AsInt(0);
    if (a->values.size() > 1) slot.target = AsAddress(a->values[1]);
    return slot;
}

// PierList.PierLink = <s>, <Address>; (RailWay埋め込み、edge上の実Pier厳密参照)
Rs2PierLink ParsePierLink(const PiAssignment& a) {
    Rs2PierLink link;
    if (!a.values.empty()) link.s = static_cast<float>(a.values[0].AsFloat(0.0f));
    if (a.values.size() > 1) link.address = AsAddress(a.values[1]);
    return link;
}

// PoleList.PoleLink = <s>, <branchIdx>, <yes/no>, <Address>;
Rs2PoleLink ParsePoleLink(const PiAssignment& a) {
    Rs2PoleLink link;
    if (!a.values.empty()) link.s = static_cast<float>(a.values[0].AsFloat(0.0f));
    if (a.values.size() > 1) link.branchIdx = a.values[1].AsInt(0);
    if (a.values.size() > 2) link.populated = a.values[2].IsYes();
    if (a.values.size() > 3) link.address = AsAddress(a.values[3]);
    return link;
}

} // namespace

Rs2RailConnector ParseRailConnector(const PiBlock& conn) {
    Rs2RailConnector c;
    c.address = GetAddress(conn, "Address");
    if (const PiBlock* splitter = conn.FirstChild("Splitter")) c.splitter = ParseSplitter(*splitter);
    c.cant = GetFloat(conn, "Cant", 0.0f);
    c.link00 = ParseLinkSlot(conn, "Link00");
    c.link01 = ParseLinkSlot(conn, "Link01");
    c.link10 = ParseLinkSlot(conn, "Link10");
    c.link11 = ParseLinkSlot(conn, "Link11");
    // TrailPoint = <S0面の現在スロット>, <S1面の現在スロット>;
    if (const PiAssignment* trail = conn.Find("TrailPoint"); trail && trail->values.size() >= 2) {
        c.trailPoint[0] = trail->values[0].AsInt(0);
        c.trailPoint[1] = trail->values[1].AsInt(0);
    }
    c.pointInst = ParsePointInst(conn);
    return c;
}

// Link0/Link1 = <endIdx>, <branchIdx>, <ConnectorAddress>;
Rs2WayEndLink ParseWayEndLink(const PiBlock& way, const char* key) {
    Rs2WayEndLink link;
    const PiAssignment* a = way.Find(key);
    if (!a || a->values.empty()) return link;
    link.endIdx = a->values[0].AsInt(0);
    if (a->values.size() > 1) link.branchIdx = a->values[1].AsInt(0);
    if (a->values.size() > 2) link.connector = AsAddress(a->values[2]);
    return link;
}

Rs2RailWay ParseRailWay(const PiBlock& way) {
    Rs2RailWay w;
    w.address = GetAddress(way, "Address");
    w.link0 = ParseWayEndLink(way, "Link0");
    w.link1 = ParseWayEndLink(way, "Link1");
    const PiAssignment* speedLimit = way.Find("SpeedLimit");
    w.speedLimit = speedLimit && !speedLimit->values.empty()
                       ? static_cast<float>(speedLimit->values[0].AsFloat(-1.0f))
                       : -1.0f;
    w.railBlock = GetString(way, "RailBlock");
    w.railPlugin = GetString(way, "RailPlugin");
    w.tiePlugin = GetString(way, "TiePlugin");
    w.girderPlugin = GetString(way, "GirderPlugin");
    w.pierPlugin = GetString(way, "PierPlugin");
    w.linePlugin = GetString(way, "LinePlugin");
    w.polePlugin = GetString(way, "PolePlugin");
    if (const PiBlock* splitList = way.FirstChild("SplitList")) {
        for (const PiBlock* sp : splitList->Children("Splitter"))
            w.splitList.push_back(ParseSplitter(*sp));
    }
    w.centerlineOnly = GetYesNo(way, "MultiTrackDummy", false);
    if (w.centerlineOnly) {
        w.trackCount = GetInt(way, "DummyTrackNum", 0);
        w.trackSpacing = GetFloat(way, "DummyTrackInterval", 0.0f);
    }
    if (const PiBlock* pierList = way.FirstChild("PierList")) {
        for (const PiAssignment* a : FindAllAssignments(*pierList, "PierLink"))
            w.pierLinks.push_back(ParsePierLink(*a));
    }
    if (const PiBlock* poleList = way.FirstChild("PoleList")) {
        for (const PiAssignment* a : FindAllAssignments(*poleList, "PoleLink"))
            w.poleLinks.push_back(ParsePoleLink(*a));
    }
    return w;
}

} // namespace rs::legacy_layout
