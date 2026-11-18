#include "EditorViewport.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "../../sim/SignalAspectRuntime.h"
#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/BlockSpeedLimit.h"
#include "../../world/alignment/SignalSpeedColor.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "ViewportInput.h"
#include "imgui.h"

#include "../../core/DebugDraw.h"
#include "../gpu/App.h"
#include "../scene/SceneRenderer.h"
#include "../../editor/tool/Editor.h"

namespace rs::app {
namespace {

// 閉塞・信号のハイライトの線。範囲は地形や車両に隠れても追えるよう、深度を無視して手前に描く。
rs::core::DebugStyle BlockOverlayStyle(uint32_t color, float thickness) {
    rs::core::DebugStyle style;
    style.color = color;
    style.widthPx = thickness;
    style.depthMode = rs::core::DebugDepthMode::AlwaysOnTop;
    return style;
}

// edge1本をポリラインでハイライトする(選択中Blockの範囲表示用)。
void DrawEdge(const EditorViewportContext& ctx, const rs::world::Guid& id, uint32_t color,
              float thickness) {
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator it =
        ctx.editor.graph.edges.find(id);
    if (it == ctx.editor.graph.edges.end()) return;

    std::vector<rs::core::Vec3> points;
    points.reserve(it->second.spline.size());
    for (const rs::world::TrackNodeSample& sample : it->second.spline) points.push_back(sample.pos);
    const rs::core::DebugStyle style = BlockOverlayStyle(color, thickness);
    ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false, style);
}

// 一覧の並び順(index)から黄金角(137.508°)刻みで色相を割り当てる。guidハッシュ由来の
// 独立乱数だと少数のBlockでも色相が偶然近接/一致することがあるため、indexベースの
// 黄金角配色にしている。色はBlock一覧の
// 表示順(=B<N>ラベルの番号)に紐づき、Block追加/削除で他Blockの色がずれ得るが、
// 色は編集中の目視識別用であり永続化しないため許容する。
ImU32 BlockColorFromIndex(size_t index) {
    constexpr float kGoldenAngleDeg = 137.508f;
    // HSVからRGBへの変換で、色相の円(360°)を6つに分けた1区間の角度。
    constexpr float kHueSectorDeg = 60.0f;
    const float hue = std::fmod(static_cast<float>(index) * kGoldenAngleDeg, 360.0f) / kHueSectorDeg;
    const float chroma = OverlayColorTuning::Embedded().blockValue * OverlayColorTuning::Embedded().blockSaturation;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    if (hue < 1.0f) {
        red = chroma;
        green = x;
    } else if (hue < 2.0f) {
        red = x;
        green = chroma;
    } else if (hue < 3.0f) {
        green = chroma;
        blue = x;
    } else if (hue < 4.0f) {
        green = x;
        blue = chroma;
    } else if (hue < 5.0f) {
        red = x;
        blue = chroma;
    } else {
        red = chroma;
        blue = x;
    }
    const float match = OverlayColorTuning::Embedded().blockValue - chroma;
    return rs::core::PackRgba(static_cast<uint8_t>((red + match) * 255.0f),
                              static_cast<uint8_t>((green + match) * 255.0f),
                              static_cast<uint8_t>((blue + match) * 255.0f), 150);
}

// 信号の速度制限の色(SignalSpeedColor)をレール上のedge区間に重ねる。
uint32_t SignalLineColor(double speedKmh) {
    const rs::world::SignalColor color = rs::world::SignalSpeedColor(speedKmh);
    return rs::core::PackRgba(static_cast<uint8_t>(color.r * 255.0f),
                              static_cast<uint8_t>(color.g * 255.0f),
                              static_cast<uint8_t>(color.b * 255.0f), 255);
}

const rs::world::TrackEdge* FindEdge(const EditorViewportContext& ctx, const rs::world::Guid& id) {
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator it =
        ctx.editor.graph.edges.find(id);
    return it == ctx.editor.graph.edges.end() ? nullptr : &it->second;
}

// 信号の位置にマーカー(レールに直角の短い線と、進行方向を示す矢印)を描く。
void DrawSignalMarker(const EditorViewportContext& ctx, const rs::world::Guid& edgeGuid, double s,
                      int direction, uint32_t color, float thickness) {
    const rs::world::TrackEdge* edge = FindEdge(ctx, edgeGuid);
    if (edge == nullptr) return;
    const rs::world::TrackFrame frame = rs::world::EvaluateAt(*edge, s, direction);
    const rs::core::Vec3 origin = frame.pos + frame.up * edge->railTopLift;
    const rs::core::Vec3 right = rs::core::Vec3::Cross(frame.up, frame.tangent).Normalized();
    const rs::core::DebugStyle style = BlockOverlayStyle(color, thickness);
    rs::core::DebugDrawList& list = ctx.app.DebugDrawList();
    const rs::core::Vec3 bar[2] = {origin - right * OverlayLineTuning::Embedded().signalMarkerHalfWidthM,
                                   origin + right * OverlayLineTuning::Embedded().signalMarkerHalfWidthM};
    list.AddPolyline(bar, 2, false, style);
    const rs::core::Vec3 tip = origin + frame.tangent * OverlayLineTuning::Embedded().signalArrowLengthM;
    const rs::core::Vec3 head[3] = {tip - frame.tangent * OverlayLineTuning::Embedded().signalArrowHeadM - right * OverlayLineTuning::Embedded().signalArrowHeadM,
                                    tip,
                                    tip - frame.tangent * OverlayLineTuning::Embedded().signalArrowHeadM + right * OverlayLineTuning::Embedded().signalArrowHeadM};
    const rs::core::Vec3 shaft[2] = {origin, tip};
    list.AddPolyline(shaft, 2, false, style);
    list.AddPolyline(head, 3, false, style);
}

// edgeの上に、信号と同じ向きを指す矢じりを一定間隔で置く。位置はedgeの始点から測り、
// 向きによらず同じ基準にする(rankが違う信号どうしは位相を半周期ずらして、矢じりが交互に並ぶ)。
void DrawSignalChevrons(const EditorViewportContext& ctx, const rs::world::TrackEdge& edge,
                        int travelDir, int rank, uint32_t color, float thickness) {
    const rs::core::DebugStyle style = BlockOverlayStyle(color, thickness);
    const double length = rs::world::EdgeLength(edge);
    const double start = OverlayLineTuning::Embedded().signalChevronSpacingM * (0.25 + 0.5 * static_cast<double>(rank));
    for (double offset = start; offset < length; offset += OverlayLineTuning::Embedded().signalChevronSpacingM) {
        const rs::world::TrackFrame frame = rs::world::EvaluateAt(edge, offset, travelDir);
        const rs::core::Vec3 origin = frame.pos + frame.up * edge.railTopLift;
        const rs::core::Vec3 right = rs::core::Vec3::Cross(frame.up, frame.tangent).Normalized();
        const rs::core::Vec3 tip = origin + frame.tangent * (OverlayLineTuning::Embedded().signalArrowHeadM * 0.5f);
        const rs::core::Vec3 head[3] = {tip - frame.tangent * OverlayLineTuning::Embedded().signalArrowHeadM - right * OverlayLineTuning::Embedded().signalArrowHeadM,
                                        tip,
                                        tip - frame.tangent * OverlayLineTuning::Embedded().signalArrowHeadM + right * OverlayLineTuning::Embedded().signalArrowHeadM};
        ctx.app.DebugDrawList().AddPolyline(head, 3, false, style);
    }
}

// 閉塞のedge1本を、レールの真上の線と向きを示す矢じりで描く。
// 向きは、信号と同じ向きに進むときのedge上の向き(閉塞の中をノードと同じシーン内のワープでたどって決める)を使う。
// rankは閉塞内での信号の順(0始まり)。
void DrawSignalEdgeRange(const EditorViewportContext& ctx, const rs::world::SpeedSignal& signal,
                         const rs::world::Block& block, const rs::world::Guid& edgeGuid, int rank,
                         uint32_t color, float thickness) {
    const rs::world::TrackEdge* edge = FindEdge(ctx, edgeGuid);
    if (edge == nullptr) return;
    const double edgeLength = rs::world::EdgeLength(*edge);
    if (edgeLength <= 0.0) return;
    // 0は、信号からたどれない、または環で両方向に通るedge。線は+1で描き、矢じりは出さない。
    const int signalTravelDir = rs::world::SignalTravelDirectionOnEdge(
        ctx.editor.graph, signal, block, edgeGuid, &ctx.layoutSession.warpLinks,
        ctx.layoutSession.activeIndex);
    const int travelDir = signalTravelDir == 0 ? +1 : signalTravelDir;
    std::vector<double> stations;
    stations.push_back(0.0);
    for (const rs::world::TrackNodeSample& sample : edge->spline) {
        const double sampleS = static_cast<double>(sample.s);
        if (sampleS > 0.0 && sampleS < edgeLength) stations.push_back(sampleS);
    }
    stations.push_back(edgeLength);
    std::vector<rs::core::Vec3> points;
    points.reserve(stations.size());
    for (const double stationS : stations) {
        const rs::world::TrackFrame frame = rs::world::EvaluateAt(*edge, stationS, travelDir);
        points.push_back(frame.pos + frame.up * edge->railTopLift);
    }
    const rs::core::DebugStyle style = BlockOverlayStyle(color, thickness);
    ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false, style);
    if (signalTravelDir != 0) DrawSignalChevrons(ctx, *edge, travelDir, rank, color, thickness);
}

// 全信号のマーカーと、影響する範囲(担当閉塞の範囲)を信号の色で描く。
// 閉塞に信号が2つあるときは、順が先の信号の線を太く先に描き、あとの信号を細く上に重ねる。
void DrawSpeedSignals(const EditorViewportContext& ctx, float thickness) {
    const rs::world::TrackGraph& graph = ctx.editor.graph;
    // pass 0で閉塞内の先頭の信号、pass 1でそれ以外を描く(太い線が細い線を隠さないよう、描く順を分ける)。
    for (int pass = 0; pass < 2; ++pass) {
        for (const rs::world::SpeedSignal& signal : graph.speedSignals) {
            // 走行中は現示の段の速度、状態が無ければ最上段の速度。段が無い信号は制限なしなので、
            // 色の表の上端(最も速い色)にする。
            const double colorSpeedKmh = static_cast<double>(
                rs::sim::ResolveSignalDisplay(&ctx.sceneRenderer.SignalAspects(), signal)
                    .speedKmh.value_or(std::numeric_limits<int>::max()));
            const uint32_t color = SignalLineColor(colorSpeedKmh);
            const rs::world::Block* block = rs::world::OwnerBlock(graph, signal);
            int rank = 0;
            size_t signalCount = 1;
            if (block != nullptr) {
                const std::vector<const rs::world::SpeedSignal*> inBlock =
                    rs::world::SpeedSignalsOfBlock(graph, *block);
                signalCount = inBlock.size();
                for (size_t index = 0; index < inBlock.size(); ++index) {
                    if (inBlock[index] == &signal) rank = static_cast<int>(index);
                }
            }
            if ((rank == 0 ? 0 : 1) != pass) continue;
            DrawSignalMarker(ctx, signal.edge, signal.s, signal.direction, color, thickness);
            // 表示専用の信号は閉塞の範囲を持たない。
            if (block == nullptr || !rs::world::IsBlockBoundarySignal(signal.kind)) continue;
            const float lineWidth =
                (rank == 0 && signalCount > 1) ? thickness + OverlayLineTuning::Embedded().signalRangeUnderlayExtraPx : thickness;
            for (const rs::world::Guid& blockEdge : block->edges) {
                DrawSignalEdgeRange(ctx, signal, *block, blockEdge, rank, color, lineWidth);
            }
        }
    }
}

// 信号の配置待ち中のホバー位置にマーカーを仮表示する。置けない位置は赤。
void DrawSpeedSignalPreview(const EditorViewportContext& ctx) {
    const rs::editor::SpeedSignalHover& hover = ctx.editor.speedSignalHover;
    if (!hover.hasTarget) return;
    const bool placeable = hover.error == rs::world::SpeedSignalPlacementError::None;
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    const uint32_t color = placeable ? colors.edgeHovered : colors.invalid;
    DrawSignalMarker(ctx, hover.edge, hover.s, hover.direction, color, 3.0f);
}

} // namespace

// 閉塞のビューポート目視確認オーバーレイ。
// 信号・閉塞編集モードだけで描く(ほかのモードでは何も描かない)。閉塞ごとの色の線、
// 選択中の閉塞の強調の上に、信号のマーカーと影響する範囲(担当閉塞の範囲)を信号の色で重ねる。
void DrawSignalBlockOverlay(const EditorViewportContext& ctx, const rs::app::ViewProjResult&, rs::app::input::PointerPos mouse) {
    if (!GetViewModeTraits(ctx.viewMode).signalBlock) return;

    for (size_t index = 0; index < ctx.editor.graph.blocks.size(); ++index) {
        const ImU32 color = BlockColorFromIndex(index);
        for (const rs::world::Guid& blockEdge : ctx.editor.graph.blocks[index].edges) {
            DrawEdge(ctx, blockEdge, color, OverlayLineTuning::Embedded().minWidthPx);
        }
    }
    for (const rs::world::Block& block : ctx.editor.graph.blocks) {
        if (block.guid != ctx.editor.selectedBlock) continue;
        for (const rs::world::Guid& blockEdge : block.edges) {
            DrawEdge(ctx, blockEdge, OverlayColorTuning::Embedded().signalExtent, 5.0f);
        }
    }
    // 選択の強調の上に、信号のマーカーと影響する範囲を信号の色で重ねる。
    DrawSpeedSignals(ctx, 3.0f);
    if (ctx.editor.signalBlockArmedAction == rs::editor::SignalBlockArmedAction::PlaceSpeedSignal &&
        ctx.input.AllowsPointer(rs::app::input::InputContext::Edit) && IsMouseInViewport(mouse, ctx.width, ctx.height)) {
        DrawSpeedSignalPreview(ctx);
    }

    // レールを選んでいる間(閉塞を作る前)は、選択中のレールを強調する。
    for (const rs::world::Guid& selected : ctx.editor.SelectedEdges()) {
        DrawEdge(ctx, selected, OverlayColorTuning::Embedded().edgeSelected, 5.0f);
    }

    // 信号の配置待ちでないときは、次のクリックで選択・トグルされるレールを白の細線で仮表示する
    // (対象のレールを分かりやすくするため)。
    if (ctx.editor.signalBlockArmedAction != rs::editor::SignalBlockArmedAction::PlaceSpeedSignal &&
        ctx.input.AllowsPointer(rs::app::input::InputContext::Edit) && IsMouseInViewport(mouse, ctx.width, ctx.height)) {
        if (ctx.signalBlockLink != nullptr && ctx.signalBlockLink->hoveredRail.has_value()) {
            DrawEdge(ctx, ctx.signalBlockLink->hoveredRail->edge, OverlayColorTuning::Embedded().edgeHovered,
                     OverlayLineTuning::Embedded().minWidthPx);
        }
    }
}

} // namespace rs::app
