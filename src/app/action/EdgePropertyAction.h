//
// EdgePropertyAction.h - 選択edgeプロパティのアプリサービス
//
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "../../editor/tool/Editor.h"
#include "../../world/alignment/EdgeSplitMerge.h"

namespace rs::app {

struct LayoutSession;
class PluginSelector;

// 軌間が分からないときの既定値[mm](狭軌)。
inline constexpr float kDefaultGaugeMm = 1067.0f;

struct EdgePropertyViewModel {
    bool visible = false;
    rs::world::Guid edgeGuid = rs::world::NilGuid();
    bool isCenterline = false;
    int trackCount = 0;
    std::size_t memberEdgeCount = 0;
    bool hasTransitionPlan = false;
    int transitionComboIndex = 0;
    float transitionLength = 0.0f;
    // 選択中のレールの本数。2本以上のとき、下の値は全レール(複線中心線を除く)をまとめた値で、
    // レールごとに違う欄は *Mixed が真になる(値は最後に選んだレールのもの)。
    // 手動のカント値・緩和曲線・配置の再配置は1本のときだけ使う。
    std::size_t selectedCount = 0;
    bool speedLimitMixed = false;
    bool cantAutoMixed = false;     // カント自動のレールとそうでないレールが混ざっている
    bool cantAutoMaxMixed = false;
    float speedLimit = -1.0f;
    float cantA = 0.0f;
    float cantB = 0.0f;
    bool hasCantMid = false;
    float cantMid = 0.0f;
    float cantTransitionRatio = 0.0f;
    bool cantAuto = false;
    float cantAutoMax = 0.0f;
    float gaugeMm = kDefaultGaugeMm;
    double edgeLength = 0.0;
    int pendingPlacementEdits = 0;

    // 軌間が不明でも、表示と入力で同じ軌間を使ってカントのtan比を換算する。
    double CantToMm(double cant) const {
        return cant * (gaugeMm > 0.0f ? gaugeMm : kDefaultGaugeMm);
    }
    double CantFromMm(double cantMm) const {
        return cantMm / CantToMm(1.0);
    }
};

class EdgePropertyAction {
public:
    EdgePropertyAction(rs::editor::Editor& editor, LayoutSession& layoutSession,
                       const PluginSelector& selector);

    void SetSpeedLimit(float speedLimitKmh);
    void SetCant(float cantA, float cantB, std::optional<float> cantMid,
                 std::optional<float> cantTransitionRatio);
    // 選択中の全レールのカント自動を切り替える(最大カントは変えない)。
    void SetCantAuto(bool enabled);
    // 選択中の全レールの最大カント(tan比)を設定する。
    void SetCantAutoMax(float cantAutoMax);
    // 選択中の全レールのカント自動をONにする。最大カントの初期値はレールごとに、そのレールの
    // 今のカント(絶対値の最大)。すでに自動のレールは変えない。
    void EnableCantAuto();
    void SetTransition(int comboIndex, float length);
    // 選択中のレール全部を1回のUndoで削除する。
    void RemoveSelectedEdge();
    void ClearPlacementEdits();
    EdgePropertyViewModel ViewModel() const;

private:
    rs::editor::Editor& editor_;
    LayoutSession& layoutSession_;
    const PluginSelector& selector_;
};

} // namespace rs::app
