//
// LoadProgressText.h - レイアウト読込の進捗表示に使う文言(ImGui版とRmlUi版で同じものを使う)
//
#pragma once

#include "../scene/SceneSlotSynchronizer.h"
#include "../locale/Localization.h"

namespace rs::app {

// 読込の予約から、読込ジョブが始まるまでの間に出す文言。
inline constexpr const char* kLoadPendingKey = "layout.loading.pending";

// 読込ジョブの段階ごとの文言。プリフェッチはワーカースレッドでのファイル読込、パス1は全個体の走査、
// パス2とファイナライズはロードとアップロードそのものなので、後ろの2つは同じ文言にする。
inline std::string LoadPhaseLabel(LoadJobPhase phase, const Localization& localization) {
    switch (phase) {
    case LoadJobPhase::Prefetch: return localization.Text("layout.loading.prefetch");
    case LoadJobPhase::Pass1: return localization.Text("layout.loading.checking");
    case LoadJobPhase::Pass2:
    case LoadJobPhase::Finalize: break;
    }
    return localization.Text("layout.loading.applying");
}

} // namespace rs::app
