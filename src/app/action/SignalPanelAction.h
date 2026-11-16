//
// SignalPanelAction.h - 信号モードの「信号の設定」パネルから呼ぶ、信号を変える操作の入口
//
// 強制R・向き・段の追加と削除・信号の削除は実際の信号(SignalBlockAction)につながっている。
// 向きの変更は、置けるときだけ変える。
//
#pragma once

#include <cstddef>
#include <string>

#include "../../world/alignment/TrackGraph.h"

namespace rs::world {
class WarpLinkTable;
} // namespace rs::world
namespace rs::app {
class SignalBlockAction;
class ToastQueue;
namespace signal_panel_action {
// 向きを順(forward)か逆(reverse)にする(Undo対象)。同じ向きの信号が閉塞に既にあるなどで置けないときは、
// 変えずに理由のトーストを出す。
void SetDirection(SignalBlockAction& action, ToastQueue& queue, const rs::world::Guid& signalGuid,
                  const std::string& direction, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex);
// 信号signalGuidの手動の強制Rを切り替える(実行時だけの状態で、Undo対象にも保存もしない)。
void SetForceRed(SignalBlockAction& action, const rs::world::Guid& signalGuid, bool forced);
// 速度speedKmhの段を足す・外す(Undo対象)。足せない速度・先頭の0の段は何もしない。
void AddStage(SignalBlockAction& action, const rs::world::Guid& signalGuid, int speedKmh);
void RemoveStage(SignalBlockAction& action, const rs::world::Guid& signalGuid, int speedKmh);
// 信号を削除する(Undo対象)。
void DeleteSignal(SignalBlockAction& action, const rs::world::Guid& signalGuid);
} // namespace signal_panel_action
} // namespace rs::app
