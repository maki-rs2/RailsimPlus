//
// PointHandLinks.h - 分岐の面ごとに、左・右の行き先(links の添字)と、今どちらが開通しているかを求める。
//
#pragma once

#include <vector>

namespace rs::world {
struct TrackGraph;
struct TrackNode;

// 分岐している面1つぶん。左右は、列車がその面へ出ていくときの向き(ルーチンの hand と同じ)。
struct PointSideLinks {
    int side = 0;
    int leftLinkIndex = 0;
    int rightLinkIndex = 0;
    bool leftActive = false;
    bool rightActive = false;
};

// node の分岐している面(S0面・S1面)ごとに並べて返す。分岐でなければ空。
std::vector<PointSideLinks> BuildPointSideLinks(const TrackGraph& graph, const TrackNode& node);

} // namespace rs::world
