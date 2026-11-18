//
// EditorCenterlineSplit.h - 複線中心線グループのedge分割トポロジ追従
//
// member実レールedge(または中心線edge自身)の分割を、同じグループの全N本の実レール+
// 中心線edgeへ対応する位置で同期させ、1つのUndo単位(CompositeCommand)にまとめる。
//
#pragma once
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world


namespace rs::editor {

class Editor;

// edgeGuidが属する複線中心線グループのguidを返す(centerlineOnly edge自身ならその
// guid、実レールメンバーならそれをtrackEdgesに含む中心線のguid)。いずれでも無ければ
// NilGuid。
rs::world::Guid FindOwningCenterlineGroup(const rs::world::TrackGraph& graph,
                                          const rs::world::Guid& edgeGuid);

// centerlineGuidのグループ(中心線edge自身+trackEdges全member)を、中心線側の
// 弧長位置sCenterに対応する位置で同期分割する。各memberの対応sは弧長比の比例配分
// (OffsetCurve既存近似と同じ精度規約)。
// 中心線+全memberのいずれか1つでも分割不能(CheckEdgeSplitがNone以外)なら、
// 何も変更せずfalseを返す(部分適用を避ける)。成功時は1つのCompositeCommandを
// ExecuteCommandし、trueを返す。
bool SplitCenterlineGroupAt(Editor& ed, const rs::world::Guid& centerlineGuid, double sCenter);

} // namespace rs::editor
