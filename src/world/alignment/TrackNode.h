//
// TrackNode.h - 軌道のノード(RailSim II の RailConnector)
//
// ノードだけを使う側が、edge・信号・駅の定義まで読まずに済むよう TrackGraph.h から分けてある。
//
#pragma once

#include <array>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "Guid.h"
#include "PointRoutine.h"

namespace rs::world {

// = RailConnector
struct TrackNode {
    Guid guid = NilGuid();
    rs::core::Vec3 pos;
    rs::core::Vec3 tangent{0, 0, 1};  // 接線（正規化）。node は「向き」を持つ
    rs::core::Vec3 up{0, 1, 0};   // アップベクトル（カント面法線の基準）
    float cant = 0.0f;            // カント(tan比、RailSim II の CRailConnector::m_Cant と同値。無次元)

    // RailSim II の RailConnector と同じ「2面 × 各面最大2スロット」モデル。
    // 走行は必ず一方の面から他方の面へ抜ける。
    struct Link {
        Guid edge = NilGuid();
        int  end = 0;  // 0:nodeA / 1:nodeB
        int  side = 0; // 0/1: node のどちらの面に接続するか（RailSim II の S0面/S1面）
    };
    std::vector<Link> links;

    // 面ごとの現在開通スロット。links を出現順に見たとき、その面で何本目（0始まり）が
    // 開通しているか。その面の本数が1本以下なら選択の余地が無いので意味を持たない。
    // 分岐（ポイント）かどうかは「いずれかの面が2本以上あるか」で決まる（IsPoint()）。
    std::array<int, 2> activeSlot{0, 0};

    // 分岐の開通先ルーチン表。回転は実行時カーソルで表現し、この定義は変更しない。
    std::vector<PointRoutineElement> pointRoutine;
    // 編成別・タグ別・共通の優先順位で解決する分岐ルーチン表。
    std::vector<PointTrainRoutine> pointTrainRoutines;
    // 配置前の編成定義(Formation)に紐付くルーチン。編成が配置された時点で
    // pointTrainRoutinesへ複写される(走行系はこちらを直接読まない)。
    std::vector<PointFormationRoutine> pointFormationRoutines;
    std::vector<PointTagRoutine> pointTagRoutines;
    std::vector<PointCondition> pointConditions;
    std::vector<PointTrainCondition> pointTrainConditions;
    std::vector<PointTagCondition> pointTagConditions;
    // 編成定義(Formation)別の条件の層。pointFormationRoutinesと同じ位置づけで、
    // 列車ごとの層の次、タグの層の前に評価する。
    std::vector<PointFormationCondition> pointFormationConditions;

    // 分岐の名前(UTF-8)。空なら未命名。番線名と同じく、3D画面のラベルなどに出す。
    std::string pointName;
    // 所属駅。NilGuidは未所属。
    Guid pointStationId = NilGuid();

    // --- links の面/スロットを引くための問い合わせ（走行中に毎フレーム呼ばれるため
    //     いずれも links を1回走査するだけで、確保を伴わない） ---

    int LinkCountOnSide(int side) const {
        int count = 0;
        for (const Link& link : links)
            if (link.side == side) ++count;
        return count;
    }

    // その面の slot 番目（0始まり）の links index。無ければ -1。
    int LinkIndexOnSide(int side, int slot) const {
        int seen = 0;
        for (int i = 0; i < static_cast<int>(links.size()); ++i) {
            if (links[static_cast<size_t>(i)].side != side) continue;
            if (seen == slot) return i;
            ++seen;
        }
        return -1;
    }

    // その link が自分の面で何番目か（0始まり）。範囲外なら -1。
    int SlotOfLink(int linkIndex) const {
        if (linkIndex < 0 || linkIndex >= static_cast<int>(links.size())) return -1;
        const int side = links[static_cast<size_t>(linkIndex)].side;
        int seen = 0;
        for (int i = 0; i < linkIndex; ++i)
            if (links[static_cast<size_t>(i)].side == side) ++seen;
        return seen;
    }

    bool IsBranching(int side) const { return LinkCountOnSide(side) >= 2; }
    bool IsPoint() const { return IsBranching(0) || IsBranching(1); }

    // その link が現在開通しているか（分岐していない面の link は常に false:
    // 選択の余地が無く「開通先」という概念を持たないため）。
    bool IsLinkActive(int linkIndex) const {
        const int slot = SlotOfLink(linkIndex);
        if (slot < 0) return false;
        const int side = links[static_cast<size_t>(linkIndex)].side;
        if (!IsBranching(side)) return false;
        return activeSlot[static_cast<size_t>(side)] == slot;
    }
};

} // namespace rs::world
