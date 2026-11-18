//
// SignalPanelViewModel.h - 実際の信号から、信号モードの「信号の設定」カードの一覧を作る
//
#pragma once

#include <vector>

#include "../workspace/SignalPanelPresentation.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

class SignalBlockAction;

// graphの信号1本につきカード1枚を作る(選択と画面上の位置は空)。名前は並び順の「信号N」。
// 現示の文・手動の強制R・足せる速度は、SignalBlockAction::SignalRowの値をそのまま使う。
std::vector<workspace::SignalPanelSignal> BuildSignalPanelSignals(const rs::world::TrackGraph& graph,
                                                                  const SignalBlockAction& action);

} // namespace rs::app
