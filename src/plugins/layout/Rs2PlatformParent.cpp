#include "Rs2PlatformParent.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ScenePlacement.h"

namespace rs::legacy_layout {

using rs::core::DVec3;
using rs::core::Vec3;

namespace {

DVec3 ToDouble(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 ToFloat(const DVec3& v) {
    return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

// 親ローカルの分割点を休止ワールドへ動かす。float と double の変換はここに閉じる。
Rs2Splitter ToRestWorld(const rs::world::RigidPose& pose, const Rs2Splitter& splitter) {
    Rs2Splitter out;
    out.pos = ToFloat(rs::world::ApplyToPoint(pose, ToDouble(splitter.pos)));
    out.dir = ToFloat(rs::world::ApplyToDirection(pose, ToDouble(splitter.dir)));
    out.up = ToFloat(rs::world::ApplyToDirection(pose, ToDouble(splitter.up)));
    return out;
}

std::array<Rs2LinkSlot*, 4> SlotsOf(Rs2RailConnector& connector) {
    return {&connector.link00, &connector.link01, &connector.link10, &connector.link11};
}

std::array<const Rs2LinkSlot*, 4> SlotsOf(const Rs2RailConnector& connector) {
    return {&connector.link00, &connector.link01, &connector.link10, &connector.link11};
}

// スロット番号 0,1 = S0面、2,3 = S1面。
int FaceOfSlot(size_t slotIndex) { return static_cast<int>(slotIndex / 2); }

std::string GroupKey(const ParentedRailWay& parented) {
    return parented.stationAddress + "|" + std::to_string(parented.partIndex);
}

// 保存形式の16桁hexのうち、シーン内の既存の Address と重ならないものを順に返す。
class AddressIssuer {
public:
    explicit AddressIssuer(const Rs2Scene& scene) {
        for (const Rs2RailConnector& c : scene.railConnectors) used_.insert(c.address);
        for (const Rs2RailWay& w : scene.railWays) used_.insert(w.address);
        for (const Rs2PlacedInstance& p : scene.placements) used_.insert(p.address);
    }

    Rs2Address Next() {
        for (;;) {
            char buffer[17];
            std::snprintf(buffer, sizeof(buffer), "%016llx",
                          static_cast<unsigned long long>(kBase + counter_++));
            if (used_.insert(buffer).second) return buffer;
        }
    }

private:
    // 実在の保存アドレス(ヒープのポインタ値)が取らない上位ビットを使う。
    static constexpr unsigned long long kBase = 0xF000000000000000ULL;
    std::unordered_set<Rs2Address> used_;
    unsigned long long counter_ = 0;
};

} // namespace

std::vector<ParentedRailWay> FindParentedRailWays(const Rs2Scene& scene,
                                                  const StationParentLookup& lookup,
                                                  std::vector<std::string>& warnings) {
    std::vector<ParentedRailWay> result;
    if (!lookup) return result;

    std::unordered_map<Rs2Address, const Rs2RailWay*> wayByAddress;
    for (const Rs2RailWay& w : scene.railWays) wayByAddress.emplace(w.address, &w);
    std::unordered_set<Rs2Address> connectorAddresses;
    for (const Rs2RailConnector& c : scene.railConnectors) connectorAddresses.insert(c.address);
    std::unordered_set<Rs2Address> claimed;

    const rs::world::TrackGraph emptyGraph;
    for (const Rs2PlacedInstance& station : scene.placements) {
        if (station.kind != "Station" || station.platforms.empty()) continue;
        const std::optional<StationParentInfo> info = lookup(station);
        if (!info) continue;

        // 取り込み側(ImportPlacements)と同じ分解で、駅の配置の行列を求める。
        const Vec3 up = station.hasUp ? station.up : Vec3(0.0f, 1.0f, 0.0f);
        const rs::core::Mat4 stationWorld = rs::world::ResolveWorldMatrix(
            rs::world::DecomposeFreePlacement(station.pos, station.dir, up), emptyGraph);

        const size_t platformCount = std::min(station.platforms.size(), info->platforms.size());
        for (size_t platformOrder = 0; platformOrder < platformCount; ++platformOrder) {
            const StationPlatformParent& parent = info->platforms[platformOrder];
            const std::string label =
                "Station " + station.address + " Platform #" + std::to_string(platformOrder);
            if (parent.undefined) {
                warnings.push_back(label + ": ParentObjectが未定義のため、通常のレールとして取り込み");
                continue;
            }
            if (parent.partIndex < 0) continue;
            if (static_cast<size_t>(parent.partIndex) >= info->partRestMatrices.size()) {
                warnings.push_back(label + ": 親の部品の休止行列が無いため、通常のレールとして取り込み");
                continue;
            }
            const rs::world::RigidPose restPose = rs::world::NormalizedRigidPose(rs::core::Mat4::Mul(
                info->partRestMatrices[static_cast<size_t>(parent.partIndex)], stationWorld));

            for (const Rs2Address& wayAddress : station.platforms[platformOrder].railList) {
                const auto wayIt = wayByAddress.find(wayAddress);
                if (wayIt == wayByAddress.end()) {
                    warnings.push_back(label + ": RailList " + wayAddress + " が見つからず、親に追従させない");
                    continue;
                }
                const Rs2RailWay& way = *wayIt->second;
                if (way.splitList.size() < 2) {
                    warnings.push_back(label + ": RailWay " + wayAddress +
                                       " のSplitListが2点未満で、親に追従させない");
                    continue;
                }
                if (connectorAddresses.count(way.link0.connector) == 0 ||
                    connectorAddresses.count(way.link1.connector) == 0) {
                    warnings.push_back(label + ": RailWay " + wayAddress +
                                       " の端のコネクタが見つからず、親に追従させない");
                    continue;
                }
                if (!claimed.insert(wayAddress).second) continue;
                result.push_back({wayAddress, station.address, parent.partIndex, restPose});
            }
        }
    }
    return result;
}

namespace {

// 1つのコネクタに刺さるスロットの分類。
struct ConnectorUse {
    struct Entry {
        size_t slotIndex = 0;
        const ParentedRailWay* parented = nullptr;  // nullptr=親つきでない(Warp・未解決を含む)
    };
    std::vector<Entry> entries;
    std::vector<std::string> groupOrder;  // 親つきスロットの群(出現順)
    bool hasStatic = false;
};

ConnectorUse ClassifySlots(const Rs2RailConnector& connector,
                           const std::unordered_map<Rs2Address, const ParentedRailWay*>& parentedByWay) {
    ConnectorUse use;
    const std::array<const Rs2LinkSlot*, 4> slots = SlotsOf(connector);
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i]->Populated()) continue;
        const ParentedRailWay* parented = nullptr;
        if (!slots[i]->warp) {
            const auto it = parentedByWay.find(slots[i]->target);
            if (it != parentedByWay.end()) parented = it->second;
        }
        use.entries.push_back({i, parented});
        if (parented == nullptr) {
            use.hasStatic = true;
            continue;
        }
        const std::string key = GroupKey(*parented);
        if (std::find(use.groupOrder.begin(), use.groupOrder.end(), key) == use.groupOrder.end())
            use.groupOrder.push_back(key);
    }
    return use;
}

} // namespace

void ApplyParentedRailWays(Rs2Scene& scene, const std::vector<ParentedRailWay>& parented) {
    if (parented.empty()) return;

    std::unordered_map<Rs2Address, size_t> wayIndex;
    for (size_t i = 0; i < scene.railWays.size(); ++i) wayIndex.emplace(scene.railWays[i].address, i);
    std::unordered_map<Rs2Address, size_t> connectorIndex;
    for (size_t i = 0; i < scene.railConnectors.size(); ++i)
        connectorIndex.emplace(scene.railConnectors[i].address, i);

    // SplitList を休止ワールドへ変換し、変換後の両端([0]=先頭、[1]=末尾)を控える。
    std::unordered_map<Rs2Address, const ParentedRailWay*> parentedByWay;
    std::unordered_map<Rs2Address, std::array<Rs2Splitter, 2>> endSplitters;
    std::vector<Rs2Address> connectorOrder;
    std::unordered_set<Rs2Address> connectorSeen;
    for (const ParentedRailWay& entry : parented) {
        const auto wayIt = wayIndex.find(entry.wayAddress);
        if (wayIt == wayIndex.end()) continue;
        Rs2RailWay& way = scene.railWays[wayIt->second];
        if (way.splitList.size() < 2) continue;
        for (Rs2Splitter& splitter : way.splitList) splitter = ToRestWorld(entry.restPose, splitter);
        parentedByWay[entry.wayAddress] = &entry;
        endSplitters[entry.wayAddress] = {way.splitList.front(), way.splitList.back()};
        for (const Rs2Address& connector : {way.link0.connector, way.link1.connector})
            if (connectorSeen.insert(connector).second) connectorOrder.push_back(connector);
    }

    AddressIssuer issuer(scene);
    std::vector<Rs2RailConnector> detached;
    for (const Rs2Address& connectorAddress : connectorOrder) {
        const auto connectorIt = connectorIndex.find(connectorAddress);
        if (connectorIt == connectorIndex.end()) continue;
        Rs2RailConnector& connector = scene.railConnectors[connectorIt->second];
        const ConnectorUse use = ClassifySlots(connector, parentedByWay);
        if (use.groupOrder.empty()) continue;

        const std::array<Rs2LinkSlot*, 4> slots = SlotsOf(connector);
        for (size_t groupIndex = 0; groupIndex < use.groupOrder.size(); ++groupIndex) {
            const std::string& group = use.groupOrder[groupIndex];
            // 親つきレールだけが刺さるコネクタは、最初の群をそのまま使う。
            const bool keepInPlace = groupIndex == 0 && !use.hasStatic;

            Rs2RailConnector fresh;
            Rs2RailConnector* target = &connector;
            std::array<int, 2> countInFace{0, 0};
            if (!keepInPlace) {
                fresh.address = issuer.Next();
                fresh.cant = connector.cant;
                target = &fresh;
            }
            bool splitterSet = false;
            for (const ConnectorUse::Entry& entry : use.entries) {
                if (entry.parented == nullptr || GroupKey(*entry.parented) != group) continue;
                Rs2LinkSlot& slot = *slots[entry.slotIndex];
                const int end = slot.endIdx == 0 ? 0 : 1;
                if (!splitterSet) {
                    target->splitter = endSplitters.at(entry.parented->wayAddress)[static_cast<size_t>(end)];
                    splitterSet = true;
                }
                if (keepInPlace) continue;

                // 面(S0/S1)は保ち、面内の番号は新しいコネクタの空きへ詰める。
                const int face = FaceOfSlot(entry.slotIndex);
                const int slotInFace = countInFace[static_cast<size_t>(face)]++;
                Rs2LinkSlot& moved = *SlotsOf(fresh)[static_cast<size_t>(face * 2 + slotInFace)];
                moved.target = slot.target;
                moved.endIdx = slot.endIdx;
                Rs2RailWay& way = scene.railWays[wayIndex.at(entry.parented->wayAddress)];
                Rs2WayEndLink& link = end == 0 ? way.link0 : way.link1;
                link.connector = fresh.address;
                link.branchIdx = slotInFace;
                slot = Rs2LinkSlot{};
            }
            if (!keepInPlace) detached.push_back(std::move(fresh));
        }
    }
    for (Rs2RailConnector& connector : detached) scene.railConnectors.push_back(std::move(connector));
}

} // namespace rs::legacy_layout
