//
// TrackLayingText.h - 敷設設定・敷設プレビューの表示用の文と表(ImGui版・RmlUi版で共用)
//
// どちらのUIにも依存しない。文はstd::stringで返し、色付けはTextSeverityだけを返して
// 各UIが自分の色へ引く。TrackLayingViewModel(TrackLayingAction.h)から文を作るだけで、
// 状態は持たない。
//
#pragma once

#include <array>
#include <string>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "TrackLayingAction.h"
#include "../../world/alignment/AlignmentSegment.h"

namespace rs::app {

// 緩和曲線コンボの選択肢1つ分。添字0=「なし」、1以降=緩和曲線あり+形状。
// 「コンボの添字」と「(緩和の有無, TransitionShape)」の対応はこの表だけが持つ。
struct TransitionChoice {
    const char* label;
    bool enabled;
    rs::world::AlignmentSegment::TransitionShape shape;
};

inline constexpr int kTransitionChoiceCount = 4;
inline constexpr std::array<TransitionChoice, kTransitionChoiceCount> kTransitionChoices = {{
    {"なし", false, rs::world::AlignmentSegment::TransitionShape::Clothoid},
    {"クロソイド", true, rs::world::AlignmentSegment::TransitionShape::Clothoid},
    {"3次放物線", true, rs::world::AlignmentSegment::TransitionShape::CubicParabola},
    {"サイン半波長", true, rs::world::AlignmentSegment::TransitionShape::SineHalf},
}};
// 「なし」+形状ごとに1行。形状を足したら表にも行を足す。
static_assert(kTransitionChoiceCount ==
              static_cast<int>(magic_enum::enum_count<rs::world::AlignmentSegment::TransitionShape>()) + 1);

// 範囲外の添字は「なし」として扱う。
const TransitionChoice& TransitionChoiceAt(int comboIndex);
// (緩和の有無, 形状)から添字を引く。enabled==falseなら形状によらず0。
int TransitionComboIndexOf(bool enabled, rs::world::AlignmentSegment::TransitionShape shape);
// ImGui::Comboなど、ラベルのconst char*配列が欲しい所向け(kTransitionChoicesと同じ並び)。
extern const std::array<const char*, kTransitionChoiceCount> kTransitionLabels;

enum class TextSeverity { Normal, Warning, Error };

struct TextLine {
    std::string text;
    TextSeverity severity = TextSeverity::Normal;
    bool operator==(const TextLine&) const = default;
};

// プレビュー線の凡例1つ分(色はTrackLayingPreviewColor.hのPreviewColor(kind)から引く)。
struct PreviewLegendEntry {
    rs::world::AlignmentSegment::Kind kind;
    const char* label;
};
inline constexpr std::array<PreviewLegendEntry, 3> kPreviewLegend = {{
    {rs::world::AlignmentSegment::Kind::Line, "直線"},
    {rs::world::AlignmentSegment::Kind::Transition, "緩和曲線"},
    {rs::world::AlignmentSegment::Kind::Arc, "円弧"},
}};

// 数値入力欄の小数点以下の桁数(ImGui版・RmlUi版で同じにする)。
inline constexpr int kAngleSnapDecimals = 1;
inline constexpr int kTrackSpacingDecimals = 2;
inline constexpr int kTransitionLengthDecimals = 1;
inline constexpr int kPlaceHeightDecimals = 2;

// valueを小数点以下decimals桁で書式化する("%.Nf")。
std::string FormatFixed(float value, int decimals);

const char* ArcPreviewModeLabel(ArcPreviewMode mode);

// 「カメラ中心高で敷設」の横に出す現在の敷設高さ。
std::string FormatPlaceHeight(const TrackLayingViewModel& viewModel);

// 緩和曲線が有効なときだけ出る注意(自動短縮)。
std::vector<TextLine> TransitionNoticeLines(const TrackLayingViewModel& viewModel);
// 複線の拒否・開始ノード選択数と拒否理由・S字複線の終点選択の進み具合。
std::vector<TextLine> LayingStatusLines(const TrackLayingViewModel& viewModel);
// プレビューの解の内訳(表示の判定はTrackLayingAction::ViewModelが済ませている)。
std::vector<TextLine> ArcPreviewLines(const ArcPreviewViewModel& preview);

} // namespace rs::app
