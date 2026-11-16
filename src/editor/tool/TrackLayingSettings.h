//
// TrackLayingSettings.h - 敷設設定(app/uiのパネルから変更されるUI設定値)
//
// Editor::layingSettings として保持される。グラフに属さないエディタ状態で
// あり、Undo対象外。入力範囲と、フィールドから導出する補助関数を共有する。
//
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/geometry/GeometrySolvers.h"

namespace rs::editor {

struct TrackLayingSettings {
    // 敷設入力の範囲。設定の変更と保存ファイルの読込で同じ制約を使う。
    static constexpr float kMinAngleSnapDegrees = 1.0f;
    static constexpr float kMaxAngleSnapDegrees = 90.0f;
    static constexpr int kMinParallelTrackCount = 1;
    static constexpr float kMinTrackSpacingM = 0.0f;
    static constexpr float kMinTransitionLengthM = 0.0f;
    // 複線一括敷設の最大本数。trackCount/activeTrackCount_はこれへclampして使う。
    static constexpr int kMaxParallelTrackCount = 8;

    // ---- 円弧敷設: 角度スナップ設定（app/uiのパネルから変更される） ----
    // 常時スナップするか(既定OFF)。通常はShift押下中だけ angleSnapHeld で有効にする。
    bool angleSnapEnabled = false;
    float angleSnapDeg = 15.0f; // 1〜90度にclampして使う
    // Shiftを押している間だけtrue(ビューポート入力が毎フレーム書く一時状態、保存しない)。
    bool angleSnapHeld = false;
    // スナップ解除キーを押している間だけtrue(同上の一時状態)。ノード・並走位置・edge上の
    // スナップをすべて切り、クリックは敷設面の点として扱う。
    bool snapDisabledHeld = false;
    // 角度スナップの判定はここ1か所(ソルバ・プレビュー・UIの表示が共有する)。
    bool AngleSnapActive() const { return angleSnapEnabled || angleSnapHeld; }

    // ---- 複線(平行N本)一括敷設設定（app/uiのパネルから変更される） ----
    int trackCount = 1;        // 敷設本数(1=単線)。1〜8にclamp
    float trackSpacing = 4.0f; // 線路間隔[m](進行方向右手方向)

    // ---- 緩和曲線 敷設設定（app/uiのパネルから変更される） ----
    // transEnabled==false または transLen<=0 で緩和曲線なし（単円フィレット）。
    // 複線(trackCount>1)+緩和曲線も OffsetCurve が対応済み(端点半径オフセット+
    // 長さ補正の近似)。中心線も緩和曲線 refit.available で生成されノードドラッグ可能。
    bool transEnabled = false;
    rs::world::AlignmentSegment::TransitionShape transShape =
        rs::world::AlignmentSegment::TransitionShape::Clothoid;
    float transLen = 20.0f;    // 片側の緩和曲線長[m]
    static float ClampAngleSnapDegrees(float degrees) {
        return std::clamp(degrees, kMinAngleSnapDegrees, kMaxAngleSnapDegrees);
    }
    static int ClampTrackCount(int count) {
        return std::clamp(count, kMinParallelTrackCount, kMaxParallelTrackCount);
    }
    // 緩和曲線長は負にできないため、確定する経路で共用する。
    static float ClampTransitionLength(float length) { return std::max(length, kMinTransitionLengthM); }
    // 線路間隔の有効範囲(0以上)。保存ファイルの読込が共有する。
    static float ClampTrackSpacing(float spacing) { return std::max(spacing, kMinTrackSpacingM); }

    // 新規点・edge分岐から始める複線の横オフセット[m]。全本数の中心が0になる
    // (奇数本なら中央が厳密に0、偶数本なら中央2本の中間が0)。
    static std::vector<float> CenteredTrackOffsets(int count, float spacing) {
        const float center = float(count - 1) * 0.5f * spacing;
        std::vector<float> offsets;
        offsets.reserve(size_t(std::max(count, 0)));
        for (int i = 0; i < count; ++i) offsets.push_back(float(i) * spacing - center);
        return offsets;
    }

    // 現在の敷設設定から TransitionSpec を作る。
    rs::world::TransitionSpec CurrentTransitionSpec() const {
        rs::world::TransitionSpec spec;
        spec.shape = transShape;
        spec.length = transEnabled ? ClampTransitionLength(transLen) : 0.0f;
        return spec;
    }

    // ---- 敷設テンプレート ----
    // 「今から敷設するedgeに適用するプラグイン」のfolderUtf8リスト。UI
    // (DrawTrackPluginListPanel)がedge未選択時に直接編集する(グラフに属さない
    // エディタ状態のためUndo対象外、trackCount等の敷設設定と同じ扱い)。
    // 敷設確定(CommitArc/CommitMultiTrackNodes)時、新規edge.pluginSectionsへ
    // そのままコピーされる(「敷設した瞬間にリストの内容を適用」)。
    std::vector<std::string> defaultTrackPlugins;

    // ---- 敷設面の高さ ----
    // 新規ノードの標高(pos.y)[m]。= 水平位置を決める基準面の高さ + placeHeightOffset。
    // 既存ノードへのスナップ時はそのノードの実標高が優先される。
    float placeHeight = 0.0f;
    // 標高の手動調整分[m]。app/ui が placeHeight を「基準面の高さ + placeHeightOffset」として
    // 毎フレーム合成する。PlacementHeightActionが0.2m刻みで動かし、敷設ツールの出入りで0へ戻す。
    float placeHeightOffset = 0.0f;
    // 敷設テンプレートのレールの天端の、地面からの持ち上げ量[m](app/uiが毎フレーム書く一時状態)。
    // 見えているレールの天端で視線を受けるために、基準面をこの分だけ上げる。
    float placeRailTopLift = 0.0f;

    // マウスの視線から、新規ノードを置く点を求める。水平位置は基準面
    // (Y = placeHeight - placeHeightOffset + placeRailTopLift)との交点で決め、高さだけ
    // placeHeight にする。高さを変えても水平位置が視線に沿ってずれない(レールの長さが
    // 変わらない)ための形。基準面を天端の高さにするのは、レール上のカーソルの視線が
    // 地面まで抜けて奥へずれ、並走位置の判定が上側で外れるのを防ぐため。
    bool RayHitPlacementPoint(const rs::core::Ray& ray, rs::core::Vec3& outHit) const {
        if (!rs::core::RayHitPlaneY(ray, placeHeight - placeHeightOffset + placeRailTopLift, outHit)) {
            return false;
        }
        outHit.y = placeHeight;
        return true;
    }
};

}  // namespace rs::editor
