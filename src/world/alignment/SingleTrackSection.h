#pragma once

#include <cstddef>
#include <optional>
#include <unordered_set>

#include "TravelDirectionFollow.h"
#include "WarpLinkTable.h"
#include "Guid.h"

namespace rs::world {

struct TrackGraph;

// 上下どちらにも信号がある閉塞のつながり(単線区間)。分岐しないノードでつながる閉塞を1つの区間とみなす。
// 反対向きの列車が同時に入ってはならない範囲で、向きを予約の単位にする。
struct SingleTrackSection {
    // 区間を表す識別子。区間の閉塞のうち最小のguid(どの閉塞から求めても同じ値になる)。
    Guid id = NilGuid();
    std::unordered_set<Guid, GuidHash> edges;
    // 基準の信号(区間の信号のうち最小のguid)の進む向きを、区間のedge全体へ伝えた印。
    TravelDirectionMasks masks;
    // 区間の閉塞の範囲の合計[m]。区間の長さの上限として使う。
    double lengthM = 0.0;
};

// edgeを含む閉塞から、区間を求める。区間が上下どちらの向きの信号も持たない(一方向だけ)、または
// edgeが閉塞に入っていないときはnullopt。ワープをまたぐ区間は扱わない。
// chainEdgesを渡すと、nulloptのときも、分岐しない継ぎ目でつながる閉塞のedgeを返す(同じつながりの
// edgeを繰り返し調べないため)。
std::optional<SingleTrackSection> FindSingleTrackSection(
    const TrackGraph& graph, const WarpLinkTable* warpTable, size_t sceneIndex, const Guid& edge,
    std::unordered_set<Guid, GuidHash>* chainEdges = nullptr);

// 単線区間の向きの予約に使う面の番号(ポイントの面は0と1)。予約の対象はnode=区間のid、face=この値、
// slot=向きの番号(SectionTravelSlot)。
constexpr int kSectionClaimFace = 2;

// 区間の中で、列車がedgeをalongEdgeDirに進むときの向きの番号。0=基準の信号と同じ向き、1=逆向き。
// 両方の向きの印が立つedge(環)と、区間に無いedgeは0。
int SectionTravelSlot(const SingleTrackSection& section, const Guid& edge, bool alongEdgeDir);

} // namespace rs::world
