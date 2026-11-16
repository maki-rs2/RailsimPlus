#include "Rs2NodeBuild.h"

#include <unordered_set>

namespace rs::legacy_layout {

using rs::world::TrackNode;

BuiltLinks BuildNodeLinks(const Rs2RailConnector& conn, const AddressMap& addrMap,
                          std::vector<std::string>& warnings) {
    BuiltLinks result;
    struct SlotRef { const Rs2LinkSlot* slot; int face; int indexInFace; };
    const SlotRef slots[4] = {{&conn.link00, 0, 0}, {&conn.link01, 0, 1},
                              {&conn.link10, 1, 0}, {&conn.link11, 1, 1}};
    for (const SlotRef& ref : slots) {
        if (!ref.slot->Populated()) continue;
        // Warpスロット(接続先がRailWayではなくWarp)はTrackNodeのlinkにしない。
        // これは警告を出さないためではなく走行系の要件: warp端点nodeは「linkが1本だけの終端node」
        // でなければならない。edge=NilGuidのダングリングlinkを残すとSelectNextLinkが
        // それを選び、edges.find失敗という別経路でatEndに至るため、
        // AdvanceAcrossScenes(WarpTraversal)がwarpリンクを引くべきnodeを見失う。
        if (ref.slot->warp) continue;
        TrackNode::Link link;
        link.edge = addrMap.ResolveWay(ref.slot->target);
        link.end = ref.slot->endIdx;
        link.side = ref.face;
        const bool dangling = rs::world::GuidIsNil(link.edge);
        if (dangling) {
            warnings.push_back("RailConnector " + conn.address +
                                ": Link先RailWay " + ref.slot->target + " が未解決(dangling)");
        }
        result.links.push_back(link);
        result.danglingSlot.push_back(dangling);
        result.slotFace.push_back(ref.face);
        result.slotInFace.push_back(ref.indexInFace);
        result.slotTarget.push_back(ref.slot->target);
    }
    return result;
}

// 面ごとの現在開通スロット(TrackNode::activeSlot)を TrailPoint から復元する。開通状態は PointList.Point(=編成別
// ルーチンの方針値)ではなく TrailPoint(=保存された現在状態)が正。
//
// TrailPoint[face] は「元スロット番号」(Link_x0 なら0 / Link_x1 なら1)なので、
// null スロットを詰めて構築した links の中での出現順(ordinal)へ変換する。
// 幹線側(throat)は「その面が1本しかない側」として走査時に自然に決まるため、
// ここで別途保持する必要は無い(fanout=4 は両面とも分岐なので throat が無い)。
void ResolvePointLinks(const Rs2RailConnector& conn, const BuiltLinks& built, TrackNode& node,
                       std::vector<std::string>& /*warnings*/) {
    for (int face = 0; face < 2; ++face) {
        const int savedSlot = conn.trailPoint[static_cast<size_t>(face)];
        int ordinal = 0;
        for (size_t i = 0; i < built.links.size(); ++i) {
            if (built.slotFace[i] != face) continue;
            if (built.slotInFace[i] == savedSlot) {
                node.activeSlot[static_cast<size_t>(face)] = ordinal;
                break;
            }
            ++ordinal;
        }
        // 該当スロットが populated でなければ既定(0=その面の最初の link)のまま。
    }
}

// RailSim II のPointList.Point値(1=左/2=右)を PointRoutineElement::hand(0=左/1=右)へ
// そのまま変換する。0はランダム要素にする。実際の面内スロット解決は行わない(通過するたびに
// rs::world::ResolveHandToOrdinal が出口面の幾何で都度解決するため)。
std::vector<rs::world::PointRoutineElement> BuildRoutineElements(
    const Rs2PointRoutine& routine, const Rs2RailConnector& conn,
    std::vector<std::string>& warnings) {
    std::vector<rs::world::PointRoutineElement> result;
    result.reserve(routine.points.size());
    for (const int value : routine.points) {
        if (value == 0) {
            result.push_back({true, 0, true});
            continue;
        }
        if (value != 1 && value != 2) {
            result.push_back({false, 0});
            warnings.push_back("RailConnector " + conn.address +
                               ": ポイントルーチンの値" + std::to_string(value) +
                               "が左右(0/1/2)でないため無効化");
            continue;
        }
        result.push_back({true, value - 1});
    }
    return result;
}

void ResolvePointRoutine(const Rs2RailConnector& conn, const BuiltLinks& /*built*/, TrackNode& node,
                         std::vector<std::string>& warnings,
                         std::vector<PendingPointTrainRoutine>* outPending) {
    if (!conn.pointInst.present || conn.pointInst.routines.empty()) return;
    if (!node.IsPoint()) return;

    std::unordered_set<Rs2Address> seenTrainGroups;
    bool hasDefaultRoutine = false;
    for (const Rs2PointRoutine& routine : conn.pointInst.routines) {
        const std::vector<rs::world::PointRoutineElement> elements =
            BuildRoutineElements(routine, conn, warnings);
        if (Rs2AddressIsNull(routine.trainGroup)) {
            if (!hasDefaultRoutine) {
                node.pointRoutine = elements;
                hasDefaultRoutine = true;
            }
            continue;
        }
        if (!seenTrainGroups.insert(routine.trainGroup).second) {
            warnings.push_back("RailConnector " + conn.address +
                               ": 同一TrainGroupのポイントルーチンが複数あるため後続を破棄");
            continue;
        }
        // UseDefault=yesのリストは要素があっても使われず、その列車は既定のリストで走る。
        if (routine.useDefault) continue;
        if (outPending != nullptr) {
            outPending->push_back({node.guid, routine.trainGroup, elements});
        }
    }
}

TrackNode BuildNode(const Rs2RailConnector& conn, const AddressMap& addrMap,
                    std::vector<std::string>& warnings,
                    std::vector<PendingPointTrainRoutine>* outPending) {
    TrackNode node;
    node.guid = addrMap.ResolveConnector(conn.address);
    node.pos = conn.splitter.pos;
    node.tangent = conn.splitter.dir.Normalized();
    node.up = conn.splitter.up.Normalized();
    node.cant = conn.cant;
    if (conn.pointInst.present) node.pointName = conn.pointInst.name;

    BuiltLinks built = BuildNodeLinks(conn, addrMap, warnings);
    node.links = built.links;
    ResolvePointLinks(conn, built, node, warnings);
    ResolvePointRoutine(conn, built, node, warnings, outPending);

    return node;
}

} // namespace rs::legacy_layout
