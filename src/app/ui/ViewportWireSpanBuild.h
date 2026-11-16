//
// ViewportWireSpanBuild.h - 架線径間(ワイヤー+ハンガー)のメッシュ積み込み
//
// 敷設エディタビューポートのレール系メッシュ再構築が使う、1径間ぶんの積み込みと、
// 実体化済みPole個体から再構築する径間の積み込み。
//
#pragma once

#include <string>
#include <vector>

#include "EditorViewport.h"

namespace rs::render {
class MeshAssembler;
}

namespace rs::app {

// 1径間ぶんのワイヤーメッシュとハンガー(碍子)モデルを assembler へ積む。
void AppendWireAndHangers(const TrackPluginEntry& lineEntry, const std::wstring& lineBaseDir,
                          const rs::core::Vec3& baseA, const rs::core::Vec3& upA,
                          const rs::core::Vec3& baseB, const rs::core::Vec3& upB,
                          HangerModelCache& hangerModelCache, rs::render::MeshAssembler& assembler,
                          std::vector<std::string>& log, int& missing);

// 実体化済みPole個体のGuid参照から架線径間を再構築して積む。切断・選択用の
// ctx.wireEditCache の径間と、実体化済みの柱の接続点も追記する。
void AppendMaterializedWireSpans(const EditorViewportContext& ctx,
                                 HangerModelCache& hangerModelCache,
                                 rs::render::MeshAssembler& assembler);

} // namespace rs::app
