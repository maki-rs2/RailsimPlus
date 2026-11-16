#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "TrackGraph.h"
#include "TrackTraversal.h"
#include "WarpForwardScan.h"

namespace rs::world {

// 前方の信号を探す距離[m]。信号の現示は次の信号の現示を見て決めるので、これより離れた
// 信号は連鎖に入れない。信号が疎な区間でも次の信号まで届く長さにしてある。
constexpr double kSpeedSignalForwardSearchM = 5000.0;

struct SpeedSignalAhead {
    size_t sceneIndex = 0;
    Guid signal = NilGuid();
    double distanceM = 0.0;
};

// scanStartから進行方向へmaxSearchM以内で、最も近い信号を返す。シーン間ワープを跨ぐ。
// 向きは走査ごとに `candidate.direction == relativeSign * 走査中のedge内の進行方向` で比べ、
// 逆向きの信号を除く(relativeSign=+1: scanStartと同じ向きの信号)。
// 担当閉塞を持たない信号(OwnerBlockが引けない。候補のあるシーンのgraphで引く)と
// excludeGuidの信号は数えない。ポイントの開通先はgraphの共有のactiveSlotで辿る(slotOverrideが
// nullptrのとき)。見つからなければnullopt。
// scanStartからその信号までの線路に、閉塞に入っていない区間があれば、その先の信号は数えない
// (閉塞の外は何も無い扱いで、見つからないのと同じnullopt)。
std::optional<SpeedSignalAhead> FindNearestSpeedSignalAlongAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const NodeSlotOverride* slotOverride = nullptr);
// 走査済みのspanを使い回す版。spanがmaxSearchMに届いていなければ自分で走査し直す。
std::optional<SpeedSignalAhead> FindNearestSpeedSignalAlongAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const ForwardScanSpan& span, const NodeSlotOverride* slotOverride = nullptr);

// spanの進路の前方にある信号を、近い順に全部返す。向き・除外・担当閉塞の有無・距離の上限の判定は
// FindNearest...と同じ(閉塞に入っていない区間があっても、その先の信号は返す)。
std::vector<SpeedSignalAhead> CollectSignalsAhead(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const ForwardScanSpan& span,
    int relativeSign, const Guid& excludeGuid, double maxSearchM);

// spanの走査開始位置から、閉塞が途切れずに続く距離[m]。閉塞に覆われない最初のedgeの手前まで
// (edgeの途中までの覆いは数えない)。最後まで続くときはspan.scannedM。
double ContiguousBlockCoveredM(const std::function<const TrackGraph*(size_t)>& graphForScene,
                               const ForwardScanSpan& span);

// 前方の探索結果。aheadは最も近い前方の信号(FindNearest...と同じ)。
// endsAtDeadEnd=trueは、前方に信号が無く、閉塞が途切れずに行き止まりまで続くとき。
// 行き止まりは前方に赤がある扱いにするので、呼び出し側が停止の速度として使う。
// ワープ先のシーンが無い終端(SceneBoundary)と、探索の範囲を使い切った場合はfalse。
struct SpeedSignalForwardResult {
    std::optional<SpeedSignalAhead> ahead;
    bool endsAtDeadEnd = false;
    // 信号から前方の信号(無ければ終端)までの間に、合流で入るポイントがあり、入ってくる線に
    // 開通していない。ポイントの開通先は共有のactiveSlotで見る。
    bool routeNotAligned = false;
    // 走査開始位置から、閉塞が途切れずに続く距離[m](ContiguousBlockCoveredM)。
    double coveredToM = 0.0;
};

// FindNearest...と同じ探索で、行き止まりまで続くかも合わせて返す。
SpeedSignalForwardResult SearchForwardSpeedSignalAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const NodeSlotOverride* slotOverride = nullptr);
SpeedSignalForwardResult SearchForwardSpeedSignalAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition scanStart, int relativeSign, const Guid& excludeGuid,
    double maxSearchM, const ForwardScanSpan& span, const NodeSlotOverride* slotOverride = nullptr);

} // namespace rs::world
