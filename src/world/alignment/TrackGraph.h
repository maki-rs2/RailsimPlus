//
// TrackGraph.h - 軌道グラフ 世界モデル
//
// TrackGraph(全体コンテナ)を定義し、要素の型(AlignmentSegment.h・TrackNode.h・TrackEdge.h・
// PlatformStopInfo.h・SpeedSignal.h)をまとめて読み込む。グラフ全体を使わない側は、要素の型の
// ヘッダだけを読む。内部表現のみ（永続 JSON とは別レイヤ）。
//
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "AlignmentSegment.h"
#include "Guid.h"
#include "IntervalList.h"
#include "PlatformRoutine.h"
#include "PlatformStopInfo.h"
#include "PointRoutine.h"
#include "RailLinkOverlay.h"
#include "RigidPose.h"
#include "SpeedSignal.h"
#include "TrackEdge.h"
#include "TrackNode.h"

namespace rs::world {

struct TrackGraph {
    std::unordered_map<Guid, TrackNode, GuidHash> nodes;
    std::unordered_map<Guid, TrackEdge, GuidHash> edges;
    RailLinkOverlay railLinkOverlay;

    // 駅停車統合。
    std::vector<StationInfo> stations;
    std::vector<PlatformStopInfo> platforms;
    // platform Guid → platformsのindex。FindPlatformById専用で、引いた先のIDを確かめてから使う。
    std::unordered_map<Guid, size_t, GuidHash> platformIndexById;
    std::unordered_map<Guid, Guid, GuidHash> edgeToPlatform;  // edge Guid → platform Guid
    // 接近判定用。同一edgeを複数の番線/駅舎が共有する場合も全て保持する。
    std::unordered_map<Guid, std::vector<Guid>, GuidHash> edgeToPlatforms;
    // 閉塞。edgeToBlocksはplatformsと同型の索引(edge Guid → blocksのindex配列)。
    std::vector<Block> blocks;
    std::unordered_map<Guid, std::vector<size_t>, GuidHash> edgeToBlocks;
    // block Guid → blocksのindex。FindBlock専用で、引いた先のIDを確かめてから使う。
    std::unordered_map<Guid, size_t, GuidHash> blockIndexById;
    // 速度制限の信号。edgeToSpeedSignalsはblocksと同型の索引(edge Guid → speedSignalsのindex配列)。
    std::vector<SpeedSignal> speedSignals;
    std::unordered_map<Guid, std::vector<size_t>, GuidHash> edgeToSpeedSignals;
    // 担当閉塞のguid → speedSignalsのindex配列(主信号だけ)。信号だけから決まるので、閉塞の編集では作り直さない。
    std::unordered_map<Guid, std::vector<size_t>, GuidHash> blockToSpeedSignals;
};

} // namespace rs::world
