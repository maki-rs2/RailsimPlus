//
// SpeedSignal.h - 閉塞と速度制限の信号
//
// 閉塞・信号だけを使う側が、軌道の形の定義まで読まずに済むよう TrackGraph.h から分けてある。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Guid.h"

namespace rs::world {

// 閉塞。範囲はレール1本単位(edge全長)で、edgeの一覧が正典。信号位置からの自動生成はしない。
// 1本のedgeは複数の閉塞のedgesに入れる(重なる)。nameが空なら名前なし。
// 在線状態は実行時派生データであり、ここ(Undo対象の静的データ)には
// 置かない(rs::sim::ComputeBlockOccupancy参照)。
struct Block {
    Guid guid = NilGuid();
    std::string name;
    std::vector<Guid> edges;
};

// 信号の段1つ。sidecarIndexは信号プラグインのstation.jsonの`signal`配列(正規化前)の要素番号で、
// 段のスイッチ割当を引くのに使う。
struct SignalAspectStage {
    int speedKmh = 0;
    int sidecarIndex = 0;
};

// 信号の種別。Main以外は表示専用で、閉塞を持たず列車の速度にも前方探索にも載らない。
// Relay(中継)は前方の主信号の現示を写し、Distant(遠方)は前方の主信号の1段手前の現示を出し、
// Shunting(入換)は最上段を出して強制Rで段0になる。
enum class SpeedSignalKind { Main, Relay, Distant, Shunting };

// 速度制限の信号。edge上の位置(edge, s)に自由に置き、担当閉塞
// (block)に制限速度をかける。directionはedge自身のs軸に対する±1で、同じ向きに走る列車にだけかかる。
// 担当閉塞はblockに保存する(Main以外はNil)。担当閉塞の引き方はOwnerBlock(SpeedSignalRegistry.h)。
// stagesは現示の段(速度の昇順・先頭0・狭義単調増加。SignalAspectRuntimeのIsValidAspectTable)。
// 列車にかかる制限速度は、その列車に見える現示の段の速度(SignalAspectRuntime)で、edgeの
// speedLimitKmhと重なるときは小さい方を使う(EffectiveSpeedLimitKmh参照)。
// 最上段の速度(SignalTopSpeedKmh)は表示と配置の検証用。stagesが空の信号は制限をかけない。
// pluginFolderUtf8は信号プラグイン(station.jsonに`signal`の速度表を持つStation)のフォルダ。
// 置く操作では必ず入る。読込時に保存データへ無かった信号だけ空になり、速度制限とマーカーは働く。
struct SpeedSignal {
    Guid guid = NilGuid();
    Guid edge = NilGuid();
    double s = 0.0;
    int direction = +1;
    std::vector<SignalAspectStage> stages;
    std::string pluginFolderUtf8;
    // 信号プラグインのDefineSwitch宣言順のスイッチ値。空なら全スイッチ既定0。
    // 現示で決まるスイッチ(station.jsonのsignal表が書くもの)は、描画時にこの値を現示の値で上書きする。
    std::vector<int32_t> switchOptions;
    SpeedSignalKind kind = SpeedSignalKind::Main;
    // 担当閉塞のguid。主信号は、この閉塞がedgeを含むときだけ働く。表示専用の信号はNil。
    Guid block = NilGuid();
};

} // namespace rs::world
