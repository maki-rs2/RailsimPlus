#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "SceneSlotPool.h"
#include "../../sim/audio/PlacedSoundSourcePool.h"

namespace rs::app {

struct PluginSession;

// 1つのセッションの SoundEffect を、鳴らす要求として out へ追記する。発動条件が偽・音声ファイルが
// 解決できない・音源のワールド位置をまだ求めていないもの(hasWorldPos が偽)は含めない。
// index/generation は PlacedSoundKey になる(スロットの番号と、スロットの中身が入れ替わるたびに進む世代)。
// dumpThisFrame が真なら、RS_DUMP_PLACED_SOUNDS の診断をこのセッション分だけログへ出す
// (dumpLabel は見出し行に入れる出どころの説明)。
void AppendSessionSoundRequests(const PluginSession& session, std::size_t index, std::uint64_t generation,
                                bool isTrainCar, bool dumpThisFrame, const std::string& dumpLabel,
                                std::vector<rs::sim::PlacedSoundRequest>& out);

// シーンの全スロットから音を集める。
std::vector<rs::sim::PlacedSoundRequest> CollectPlacedSoundRequests(
    const std::vector<SceneSlotPool::Slot>& slots, int& dumpFrameCounter);

} // namespace rs::app
