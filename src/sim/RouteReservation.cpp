#include "RouteReservation.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "../world/scene/SceneIndexRemap.h"

namespace rs::sim {

NodeFaceKey KeyOf(const rs::world::PointClaim& claim) { return NodeFaceKey{claim.node, claim.face}; }

bool RouteReservation::Earlier(const Waiting& a, const Waiting& b) {
    if (a.requestedAtSeconds != b.requestedAtSeconds) {
        return a.requestedAtSeconds < b.requestedAtSeconds;
    }
    return a.nearestDistanceM < b.nearestDistanceM;
}

bool RouteReservation::ConflictsWithHolders(TrainId trainId,
                                            const rs::world::PointClaim& claim) const {
    // 切り替えが要るのに切り替えられない面は、保持の有無によらず取れない。
    if (claim.blockedByOccupancy) return true;
    const std::unordered_map<NodeFaceKey, Hold, NodeFaceKeyHash>::const_iterator it =
        holds_.find(KeyOf(claim));
    if (it == holds_.end()) return false;
    const Hold& hold = it->second;
    const bool selfHolds =
        std::find(hold.trains.begin(), hold.trains.end(), trainId) != hold.trains.end();
    if (hold.slot == claim.slot) return false;
    // 違う線を求める。自分だけが保持しているなら、切り替えられるときに限って変えられる。
    const bool onlySelf = selfHolds && hold.trains.size() == 1;
    return !onlySelf;
}

bool RouteReservation::MustYield(TrainId trainId, const Waiting& self,
                                 const rs::world::PointClaim& claim) const {
    for (const auto& [otherId, other] : waiting_) {
        if (otherId == trainId || !Earlier(other, self)) continue;
        for (const rs::world::PointClaim& otherClaim : other.claims) {
            if (KeyOf(otherClaim) == KeyOf(claim) && otherClaim.slot != claim.slot) return true;
        }
    }
    return false;
}

bool RouteReservation::Acquire(TrainId trainId, const std::vector<rs::world::PointClaim>& claims,
                               double nowSeconds) {
    if (claims.empty()) {
        waiting_.erase(trainId);
        return true;
    }
    Waiting self;
    self.requestedAtSeconds = nowSeconds;
    self.nearestDistanceM = std::numeric_limits<double>::max();
    self.claims = claims;
    for (const rs::world::PointClaim& claim : claims) {
        self.nearestDistanceM = std::min(self.nearestDistanceM, claim.distanceM);
    }
    const std::unordered_map<TrainId, Waiting, rs::world::GuidHash>::const_iterator waitingIt =
        waiting_.find(trainId);
    if (waitingIt != waiting_.end()) self.requestedAtSeconds = waitingIt->second.requestedAtSeconds;

    bool allGranted = true;
    for (const rs::world::PointClaim& claim : claims) {
        if (ConflictsWithHolders(trainId, claim) || MustYield(trainId, self, claim)) {
            allGranted = false;
            break;
        }
    }
    if (!allGranted) {
        waiting_[trainId] = std::move(self);
        return false;
    }
    for (const rs::world::PointClaim& claim : claims) {
        Hold& hold = holds_[KeyOf(claim)];
        hold.sceneIndex = claim.sceneIndex;
        if (hold.trains.empty() || hold.slot != claim.slot) {
            // 空の面、または自分だけが保持していて線を変える面。
            hold.slot = claim.slot;
            hold.trains.clear();
        }
        if (std::find(hold.trains.begin(), hold.trains.end(), trainId) == hold.trains.end()) {
            hold.trains.push_back(trainId);
        }
    }
    waiting_.erase(trainId);
    return true;
}

bool RouteReservation::Holds(TrainId trainId, const rs::world::PointClaim& claim) const {
    const std::unordered_map<NodeFaceKey, Hold, NodeFaceKeyHash>::const_iterator it =
        holds_.find(KeyOf(claim));
    if (it == holds_.end() || it->second.slot != claim.slot) return false;
    return std::find(it->second.trains.begin(), it->second.trains.end(), trainId) !=
           it->second.trains.end();
}

void RouteReservation::ReleaseExcept(
    TrainId trainId, const std::unordered_set<NodeFaceKey, NodeFaceKeyHash>& keep) {
    for (auto it = holds_.begin(); it != holds_.end();) {
        std::vector<TrainId>& trains = it->second.trains;
        if (!keep.contains(it->first)) {
            trains.erase(std::remove(trains.begin(), trains.end(), trainId), trains.end());
        }
        it = trains.empty() ? holds_.erase(it) : std::next(it);
    }
}

std::vector<HeldFace> RouteReservation::HeldFaces(TrainId trainId) const {
    std::vector<HeldFace> faces;
    for (const auto& [key, hold] : holds_) {
        if (std::find(hold.trains.begin(), hold.trains.end(), trainId) != hold.trains.end()) {
            faces.push_back(HeldFace{key, hold.sceneIndex});
        }
    }
    return faces;
}

void RouteReservation::RemoveTrain(TrainId trainId) {
    ReleaseExcept(trainId, {});
    waiting_.erase(trainId);
}

void RouteReservation::RemapSceneIndices(const std::vector<size_t>& oldToNew) {
    // 消えるSceneのポイントの保持と要求は捨てる。台帳は軌道を参照しないので、保持していた列車は何も手放さない。
    for (std::unordered_map<NodeFaceKey, Hold, NodeFaceKeyHash>::iterator it = holds_.begin(); it != holds_.end();) {
        it->second.sceneIndex = rs::world::RemapSceneIndex(oldToNew, it->second.sceneIndex);
        it = rs::world::IsRemovedScene(it->second.sceneIndex) ? holds_.erase(it) : std::next(it);
    }
    for (std::unordered_map<TrainId, Waiting, rs::world::GuidHash>::iterator it = waiting_.begin(); it != waiting_.end();) {
        std::vector<rs::world::PointClaim>& claims = it->second.claims;
        for (rs::world::PointClaim& claim : claims) claim.sceneIndex = rs::world::RemapSceneIndex(oldToNew, claim.sceneIndex);
        std::erase_if(claims, [](const rs::world::PointClaim& claim) { return rs::world::IsRemovedScene(claim.sceneIndex); });
        it = claims.empty() ? waiting_.erase(it) : std::next(it);
    }
}

void RouteReservation::Clear() {
    holds_.clear();
    waiting_.clear();
}

} // namespace rs::sim
