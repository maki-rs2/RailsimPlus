//
// SwitchFlipSmokeDriver.h - --switch-flip-smoke: 「スイッチ変更→スロット再利用経路」の回帰検証
//
// 60フレーム目以降、20フレームおきに計5回スロット経路のSceneryエンティティの
// スイッチ値を変更する(SceneRenderer::Syncパス2のparkedスロット再利用を反復して
// 踏ませる)。この経路はGUIでスイッチを変更したときにしか通らず、通常の--smokeでは
// 一度も実行されないため、ヘッドレスで踏ませる手段が無いと回帰を検出できない。
// この経路はGPUリソースの解放漏れがデバイス喪失につながるため、
// ずっと残す回帰テストにする(GUI自動操作の代わりにCLIで再現する)。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../../world/alignment/Guid.h"

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class SceneRenderer;

class SwitchFlipSmokeDriver {
public:
    // active: state.cli.switchFlipSmokeFrames > 0 (呼び出し側で判定して渡す)。
    explicit SwitchFlipSmokeDriver(bool active) : active_(active) {}

    bool Active() const { return active_; }

    int FlipCount() const { return switchFlipNextIndex_; }

    // スイッチ反映Syncの所要時間(=ユーザーが体感する引っかかり)を、変更した次の
    // フレームでだけ実測して出す。改善の可否を目視の体感ではなく数値で判定するため
    // (切替は変更フレームのSyncの後に行うので反映は次フレーム)。
    void MaybeLogMeasuredSync(int frame, double syncMs);

    // スロット経路Sceneryのスイッチ値をGUI操作を模して変更する。
    void MaybeFlip(int frame, rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer,
                  std::vector<std::string>& log);

private:
    bool active_ = false;
    int switchFlipNextIndex_ = 0;
    rs::world::Guid switchFlipEntityId_ = rs::world::NilGuid();  // 選定済みターゲットのSceneEntity::guid
    std::string switchFlipFolderUtf8_;
    std::vector<size_t> switchFlipSwitchIndices_; // entries.size()>=2 のスイッチのindex一覧
    int switchFlipMeasureFrame_ = -1;             // 切替反映Syncを実測するフレーム(-1=なし)
};

} // namespace rs::app
