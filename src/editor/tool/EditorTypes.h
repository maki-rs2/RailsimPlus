//
// EditorTypes.h - 敷設エディタの公開データ型（ツールモード・敷設プレビュー）
//
// Editor.h から分離。app/ui 側のオーバーレイ描画やパネルは、Editor クラス本体を
// 見ずにこれらの型だけを必要とすることが多いため、別ヘッダーに置いてある。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/geometry/GeometrySolvers.h"

namespace rs::editor {

enum class ToolMode { Select, PlaceArc };

// edge内区間分割の編集対象レイヤー。区間表を持つのはプラグインだけ。
enum class SectionLayer { PluginSet };


// スクリーン投影結果・レイキャスト用の型。実体はrs::core::Math.hへ集約済み
// (editor層がapp層へ依存しないよう、汎用カメラ幾何をcoreへ置いた)。
// editor層のコードからは引き続き非修飾の ProjectedPoint / Ray で参照できる。
using ProjectedPoint = rs::core::ProjectedPoint;
using Ray = rs::core::Ray;

// 現在の敷設プレビュー用データ（app/ui のオーバーレイ描画に使う）。
struct PlacePreview {
    bool active = false;               // 始点確定済みか
    rs::core::Vec3 startPos;
    std::vector<rs::core::Vec3> polyline; // 始点→現在地面点を1mおきにサンプリングした点列
    // polyline と同数。各点が属する線形区間の種別（直線/緩和曲線/円弧の色分け用）。
    // サンプリング由来のポリライン(awaitingEndpoint確定形)でのみ埋まる。補助線
    // (awaitingMidpoint等)では空で、その場合は呼び出し側が単色で描く。
    std::vector<rs::world::AlignmentSegment::Kind> polylineKind;
    // polyline と同数。各点のアップベクトル(カント適用済み)。呼び出し側が
    // 敷設プレビュー時の軌道基準線(レール頭頂面 = polyline+polylineUp*baseLift)を
    // 描くために使う(polylineKindと同様、サンプリング由来のポリラインでのみ埋まる)。
    std::vector<rs::core::Vec3> polylineUp;

    // ---- 円弧ツール(3点指定)の追加情報 ----
    bool awaitingMidpoint = false; // true: まだ中間点未確定（中間点決めクリック待ち）。
                                   // polylineは始点→中間点候補の補助線（赤）。
    bool awaitingEndpoint = false; // true: 中間点確定済み、終点待ち。
    rs::core::Vec3 midPos;         // 中間点候補（awaitingEndpoint==trueのとき有効）

    bool haveSolve = false;         // SolveThreePointArcの結果が有効か（awaitingEndpoint時）
    rs::world::ThreePointArcSolveResult solve; // haveSolve==true のときの計算結果
    bool endSnappedToNode = false;  // 終点が既存nodeにスナップされ、2接線交点で解いたか
    bool endSnapUnreachable = false; // 終点ノードスナップだが交点が求まらない/後方(確定拒否)

    // 円弧セグメント本体の弦(前後直線を除いた弧の両端)。R表示とセットで複数弧に対応。
    // 緩和曲線適用時は直線境界の弦(緩和曲線区間を含む全体スパン、kind=TransitionSpan)
    // と、実際の円弧のみの区間を結ぶ弦(kind=Arc)の2本を積む。呼び出し側(描画)は
    // kindで色を変える。showRadiusText: 緩和曲線適用時は外側
    // (TransitionSpan)のテキストのみ表示し、内側(Arc)は文字が重なるため非表示にする。
    struct ArcChord {
        enum class Kind { Arc, TransitionSpan };
        rs::core::Vec3 start;
        rs::core::Vec3 end;
        rs::core::Vec3 mid;
        float radius = 0.0f;
        Kind kind = Kind::Arc;
        bool showRadiusText = true;
    };
    std::vector<ArcChord> arcChords; // 空なら弧弦非表示(直線フォールバック等)

    // ---- 複線(平行N本)プレビュー ----
    std::vector<std::vector<rs::core::Vec3>> sidePolylines; // 基準curve以外のtrack(オフセット≠0)のサンプル点列
    std::vector<std::vector<rs::world::AlignmentSegment::Kind>> sidePolylineKinds; // sidePolylinesと同じ並び・点数。区間の種類(色分け用)
    bool referenceIsGuide = false; // 基準curveに対応するtrackが無い(偶数本)。描画側は基準curveを描かない
    bool sideRejected = false; // いずれかの平行線がoffsetで確定不可
    std::string sideRejectReason; // sideRejectedのとき、どの線がなぜ作れないか
};

} // namespace rs::editor
