//
// TrackLayingText.cpp - 敷設設定・敷設プレビューの表示用の文と表
//
#include "TrackLayingText.h"

#include <cstdio>

namespace rs::app {
namespace {

// snprintfで書式化してstd::stringにする。文はどれも数行・数十文字なので固定バッファで足りる。
template <typename... Args>
std::string Format(const char* format, Args... args) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    return std::string(buffer);
}

std::array<const char*, kTransitionChoiceCount> MakeTransitionLabels() {
    std::array<const char*, kTransitionChoiceCount> labels{};
    for (std::size_t index = 0; index < kTransitionChoices.size(); ++index) {
        labels[index] = kTransitionChoices[index].label;
    }
    return labels;
}

} // namespace

const std::array<const char*, kTransitionChoiceCount> kTransitionLabels = MakeTransitionLabels();

const TransitionChoice& TransitionChoiceAt(int comboIndex) {
    if (comboIndex < 0 || comboIndex >= kTransitionChoiceCount) return kTransitionChoices[0];
    return kTransitionChoices[static_cast<std::size_t>(comboIndex)];
}

int TransitionComboIndexOf(bool enabled, rs::world::AlignmentSegment::TransitionShape shape) {
    if (!enabled) return 0;
    for (int index = 1; index < kTransitionChoiceCount; ++index) {
        if (kTransitionChoices[static_cast<std::size_t>(index)].shape == shape) return index;
    }
    return 0;
}

std::string FormatFixed(float value, int decimals) {
    return Format("%.*f", decimals, static_cast<double>(value));
}

const char* ArcPreviewModeLabel(ArcPreviewMode mode) {
    switch (mode) {
    case ArcPreviewMode::SCurve: return "S字接続(双円弧)";
    case ArcPreviewMode::EndNodeSnap: return "終点ノード接続(厳密)";
    case ArcPreviewMode::AngleSnap: return "角度スナップ";
    case ArcPreviewMode::Free: return "自由";
    }
    return "自由";
}

std::string FormatPlaceHeight(const TrackLayingViewModel& viewModel) {
    return Format("(敷設高 Y=%.*fm)", kPlaceHeightDecimals, static_cast<double>(viewModel.placeHeight));
}

std::vector<TextLine> TransitionNoticeLines(const TrackLayingViewModel& viewModel) {
    std::vector<TextLine> lines;
    if (!viewModel.transitionEnabled) return lines;
    // 3点フィレットの幾何拘束(緩和長≤R×旋回角)で指定値が入りきらない場合、
    // プレビューの実効緩和長(=この形状での上限)を明示する。
    if (viewModel.transitionShortened) {
        lines.push_back({Format("この形状では最大 %.1fm まで（指定 %.1fm は自動短縮）",
                                static_cast<double>(viewModel.effectiveTransitionLength),
                                static_cast<double>(viewModel.transitionLength)),
                         TextSeverity::Warning});
    }
    return lines;
}

std::vector<TextLine> LayingStatusLines(const TrackLayingViewModel& viewModel) {
    std::vector<TextLine> lines;
    if (viewModel.sideRejected) {
        lines.push_back({"確定不可: " + viewModel.sideRejectReason, TextSeverity::Error});
    }
    if (!viewModel.commitRejectReason.empty()) {
        lines.push_back({"敷設できません: " + viewModel.commitRejectReason, TextSeverity::Error});
    }
    if (viewModel.selectedStartNodeCount != 0) {
        lines.push_back({Format("開始ノード選択: %zu 本", viewModel.selectedStartNodeCount), TextSeverity::Normal});
    }
    if (viewModel.startSelectionRejected) {
        lines.push_back({viewModel.startSelectionRejectReason ? viewModel.startSelectionRejectReason
                                                              : "選択が不正です",
                         TextSeverity::Error});
    }
    if (viewModel.showEndNodeProgress) {
        lines.push_back({Format("S字複線接続: 終点ノード %zu/%d 選択", viewModel.selectedEndNodeCount,
                                viewModel.activeTrackCount),
                         TextSeverity::Normal});
    }
    return lines;
}

std::vector<TextLine> ArcPreviewLines(const ArcPreviewViewModel& preview) {
    std::vector<TextLine> lines;
    switch (preview.kind) {
    case ArcPreviewKind::AwaitingMidpoint:
        lines.push_back({"円弧敷設: 中間点を決めるクリックをしてください", TextSeverity::Normal});
        break;
    case ArcPreviewKind::EndSnapUnreachable:
        lines.push_back({"接続先の角度に合わせられません(2接線の交点が求まりません)", TextSeverity::Error});
        break;
    case ArcPreviewKind::Solved: {
        const char* mode = ArcPreviewModeLabel(preview.mode);
        if (preview.isStraightFallback) {
            lines.push_back({Format("%s  直線フォールバック  全長=%.2fm", mode, static_cast<double>(preview.totalLength)),
                             TextSeverity::Normal});
        } else if (preview.isSCurve) {
            lines.push_back({Format("%s  旋回角=%.2fdeg  R1=%.2fm  R2=%.2fm", mode,
                                    static_cast<double>(preview.turnAngleDeg), static_cast<double>(preview.radius1),
                                    static_cast<double>(preview.radius2)),
                             TextSeverity::Normal});
        } else {
            lines.push_back({Format("%s  旋回角=%.2fdeg  R=%.2fm", mode, static_cast<double>(preview.turnAngleDeg),
                                    static_cast<double>(preview.radius)),
                             TextSeverity::Normal});
            if (preview.transitionLength > 0.0f) {
                lines.push_back({Format("前直線=%.2fm  緩和=%.1fm  円弧長=%.2fm  緩和=%.1fm  後直線=%.2fm",
                                        static_cast<double>(preview.preLineLength),
                                        static_cast<double>(preview.transitionLength),
                                        static_cast<double>(preview.arcLength),
                                        static_cast<double>(preview.transitionLength),
                                        static_cast<double>(preview.postLineLength)),
                                 TextSeverity::Normal});
            } else {
                lines.push_back({Format("前直線=%.2fm  円弧長=%.2fm  後直線=%.2fm",
                                        static_cast<double>(preview.preLineLength),
                                        static_cast<double>(preview.arcLength),
                                        static_cast<double>(preview.postLineLength)),
                                 TextSeverity::Normal});
            }
        }
        break;
    }
    case ArcPreviewKind::Rejected:
        lines.push_back({Format("確定不可: %s", preview.rejectReason ? preview.rejectReason : "unknown"),
                         TextSeverity::Error});
        break;
    case ArcPreviewKind::None:
        break;
    }
    return lines;
}

} // namespace rs::app
