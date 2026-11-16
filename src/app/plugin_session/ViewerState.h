//
// ViewerState.h - プラグインビューアーモード専用の状態(ライト・太陽の上書き・システムスイッチの初期値)
//
// RailSim IIのビューアーに合わせて、日付・時刻のシステムスイッチは
// 読込時に固定の値を入れ、毎フレームは書き換えない。太陽の上書きはここで持ち、
// レイアウトに保存される globals.solarOverrides には書かない。
//
#pragma once

#include <cstdint>

#include "../../world/time/SolarPosition.h"

namespace rs::app {

struct PluginSession;

struct ViewerState {
    // ライトON=昼、OFF=夜。変更は SetViewerLight を通す(上書きと _NIGHT を同時に更新するため)。
    bool lightOn = true;
    rs::world::SolarOverrides solarOverrides;

    ViewerState();
};

// ライトを切り替える。太陽の上書きを変え、プラグインが _NIGHT を参照していればその値も書き直す。
void SetViewerLight(ViewerState& viewer, PluginSession& session, bool lightOn);

// プラグイン読込直後に、参照されている日付・時刻・季節・_NIGHT へビューアーの初期値を入れる。
void ApplyViewerInitialSwitches(const ViewerState& viewer, PluginSession& session);

} // namespace rs::app
