//
// DiaCardCandidates.h - ダイヤモードで3D画面に出すカード(番線・分岐)のうち、どれを出すかを選ぶ(RmlUi非依存)
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "AnchoredElementPlacement.h"
#include "../../world/alignment/Guid.h"

namespace rs::app::workspace {

enum class DiaCardKind { Platform, Point };

struct DiaCardCandidate {
    DiaCardKind kind = DiaCardKind::Platform;
    rs::world::Guid id = rs::world::NilGuid();
    std::string name;
    bool selected = false;
    ClipPoint clip; // 追従する点の、カメラ変換後の位置
};

// カメラの後ろと画面の外のものを捨て、番線と分岐を合わせて、画面の中心に近い順に maxCount 件まで残す。
// 中心からの距離が同じものは、渡された順を保つ。
std::vector<DiaCardCandidate> ChooseVisibleDiaCards(std::vector<DiaCardCandidate> candidates, std::size_t maxCount);

} // namespace rs::app::workspace
