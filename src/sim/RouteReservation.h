#pragma once

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../world/alignment/PointClaims.h"
#include "../world/scene/TrainId.h"

namespace rs::sim {

using rs::world::TrainId;

// ポイントの分岐している面1つ。予約の単位。
struct NodeFaceKey {
    rs::world::Guid node = rs::world::NilGuid();
    int face = 0;
    bool operator==(const NodeFaceKey& other) const {
        return node == other.node && face == other.face;
    }
};

struct NodeFaceKeyHash {
    size_t operator()(const NodeFaceKey& key) const noexcept {
        return rs::world::GuidHash{}(key.node) * 31u + static_cast<size_t>(key.face);
    }
};

NodeFaceKey KeyOf(const rs::world::PointClaim& claim);

// 保持している面と、そのポイントがあるシーン。
struct HeldFace {
    NodeFaceKey key;
    size_t sceneIndex = 0;
};

// ポイントの予約台帳。列車が前方で通るポイントの進入線(PointClaim)をまとめて取り、
// 取れるまでは先着順の待ち行列に並ぶ。台帳は軌道を参照しない。開通先(activeSlot)の書き換えは
// 呼び出し側が、Acquireが成功した後に行う。
//
// - 同じ面に同じ線を求める列車どうしは同時に保持できる(続行)。違う線を求める列車がいれば、
//   先に保持している列車が手放すまで待つ。
// - 複数のポイントはまとめて取る。1つでも取れなければ、新しい要求は何も取らない(互いに待って
//   動けなくなるのを防ぐ)。すでに保持しているポイントは、取れなくても手放さない。
// - 待ち行列は、要求を始めた時刻の早い順。同時刻なら、いちばん近い要求が近い方を先にする。
//   先に並んだ列車の要求と違う線を求めるときは、先に並んだ列車に譲る。
class RouteReservation {
public:
    // claimsを全部取れたらtrue(以後は保持)。取れなければ待ち行列に並んでfalse。
    // claimsが空なら待ち行列から外してtrue。
    bool Acquire(TrainId trainId, const std::vector<rs::world::PointClaim>& claims,
                 double nowSeconds);

    // trainIdがclaimの面を、同じ線で保持しているか。
    bool Holds(TrainId trainId, const rs::world::PointClaim& claim) const;

    // trainIdが保持している面のうち、keepに入っていないものを手放す。
    void ReleaseExcept(TrainId trainId,
                       const std::unordered_set<NodeFaceKey, NodeFaceKeyHash>& keep);

    // trainIdが保持している面。
    std::vector<HeldFace> HeldFaces(TrainId trainId) const;

    // 列車を台帳から外す(保持も待ち行列も)。
    void RemoveTrain(TrainId trainId);
    // Sceneの並べ替え・削除(oldToNew[旧]=新)に合わせて、保持と待ち行列のScene番号を付け替える。
    // 対応先がkRemovedSceneの保持と要求は捨てる。
    void RemapSceneIndices(const std::vector<size_t>& oldToNew);
    void Clear();

private:
    struct Hold {
        int slot = 0;
        size_t sceneIndex = 0;
        std::vector<TrainId> trains;
    };
    struct Waiting {
        double requestedAtSeconds = 0.0;
        double nearestDistanceM = 0.0;
        std::vector<rs::world::PointClaim> claims;
    };

    static bool Earlier(const Waiting& a, const Waiting& b);
    bool ConflictsWithHolders(TrainId trainId, const rs::world::PointClaim& claim) const;
    bool MustYield(TrainId trainId, const Waiting& self, const rs::world::PointClaim& claim) const;

    std::unordered_map<NodeFaceKey, Hold, NodeFaceKeyHash> holds_;
    std::unordered_map<TrainId, Waiting, rs::world::GuidHash> waiting_;
};

} // namespace rs::sim
