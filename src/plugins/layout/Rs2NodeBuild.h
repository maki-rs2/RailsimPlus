//
// Rs2NodeBuild.h - Rs2RailConnector(生データ) → rs::world::TrackNode(内部表現) 変換。
// Address解決マップ(AddressMap)と分岐(ポイント)のthroat/active解決を担う
// (RailSim II の RailConnectorの「2面×各面最大2スロット」モデルに基づくPointList.Point/TrailPoint解釈)。
//
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Rs2Types.h"
#include "../../world/alignment/Guid.h"
#include "../../world/alignment/PointRoutine.h"
#include "../../world/alignment/TrackNode.h"

namespace rs::legacy_layout {

// Address(hex文字列) → 内部Guid の解決マップ(2パス方式)。
struct AddressMap {
    std::unordered_map<Rs2Address, rs::world::Guid> connectorGuids;
    std::unordered_map<Rs2Address, rs::world::Guid> wayGuids;

    rs::world::Guid ResolveConnector(const Rs2Address& addr) const {
        if (Rs2AddressIsNull(addr)) return rs::world::NilGuid();
        auto it = connectorGuids.find(addr);
        return it != connectorGuids.end() ? it->second : rs::world::NilGuid();
    }
    rs::world::Guid ResolveWay(const Rs2Address& addr) const {
        if (Rs2AddressIsNull(addr)) return rs::world::NilGuid();
        auto it = wayGuids.find(addr);
        return it != wayGuids.end() ? it->second : rs::world::NilGuid();
    }
};

// links を Link00→Link01→Link10→Link11 の順で null 以外のスロットのみ詰めて構築する。
// 戻り値の各要素の bool は「元スロットのAddressは非null(populated)だが
// 解決できなかった(dangling)」を示す(スロット位置との対応を保つため、未解決でも
// populated ならスロット自体は保持し、edge=NilGuidのまま links へ積む。未解決分は
// 呼び出し側で warnings に出す)。
struct BuiltLinks {
    std::vector<rs::world::TrackNode::Link> links;
    std::vector<bool> danglingSlot;   // links と同じ添字。true=Address有りだが解決不能
    std::vector<int> slotFace;        // links と同じ添字。0=S0面(Link00/01) / 1=S1面(Link10/11)
    std::vector<int> slotInFace;      // links と同じ添字。面内スロット番号(0=Link_x0 / 1=Link_x1)
    std::vector<Rs2Address> slotTarget;  // links と同じ添字。接続先RailWayのAddress
};

// TrainGroup別ポイントルーチンの未解決エントリ。TrainIdは編成配置時(app層)に採番される
// ため、取り込み時点ではRs2Addressのまま持ち越し、配置後に解決してTrackNodeへ書き込む。
struct PendingPointTrainRoutine {
    rs::world::Guid node = rs::world::NilGuid();
    Rs2Address trainGroup;
    std::vector<rs::world::PointRoutineElement> routine;
};

BuiltLinks BuildNodeLinks(const Rs2RailConnector& conn, const AddressMap& addrMap,
                          std::vector<std::string>& warnings);

// 分岐ノードの throat(幹線側) と現在の開通先を解決する。
// RailSim II のRailConnectorは2面構成で走行は必ず一方の面から他方の面へ抜けるため、
// 分岐している(スロット2個の)面が1つだけなら、その面が分岐側・反対面が throat に確定する。
// 開通先は PointList.Point(=編成別ルーチンの方針値)ではなく TrailPoint(=保存された
// 現在スロット)から取る。
void ResolvePointLinks(const Rs2RailConnector& conn, const BuiltLinks& built,
                       rs::world::TrackNode& node, std::vector<std::string>& warnings);

// PointList.Point(ポイントルーチンの各要素、1=左/2=右)を PointRoutineElement::hand
// (0=左/1=右)へ変換して node.pointRoutine 等へ積む。面ごとの解決は行わない
// (実際にどちらのリンクへ解決されるかは、通過するたびに出口面の幾何で決まる。
// rs::world::ResolveHandToOrdinal、TrackTraversal.h参照)。
void ResolvePointRoutine(const Rs2RailConnector& conn, const BuiltLinks& built,
                         rs::world::TrackNode& node, std::vector<std::string>& warnings,
                         std::vector<PendingPointTrainRoutine>* outPending = nullptr);

rs::world::TrackNode BuildNode(const Rs2RailConnector& conn, const AddressMap& addrMap,
                               std::vector<std::string>& warnings,
                               std::vector<PendingPointTrainRoutine>* outPending = nullptr);

} // namespace rs::legacy_layout
