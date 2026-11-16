//
// MeshFrameRecord.h - 1フレームぶんのメッシュ描画の記録順(本編とサムネイルで共通)
//
// 不透明 → 後描き不透明 → 半透明の順と、フェード中の枠の扱いをここ1か所に持つ。
// 本編だけが持つ物(静的インスタンス・配置プレビュー・ハイライト・GPU計測)は、段階ごとの
// コールバックで差し込む。
//
#pragma once

#include <functional>
#include <span>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "MeshPipeline.h"

namespace rs::render {

class SceneHdrTarget;
class WboitPass;

// 描く枠1つ。メインのメッシュは fadeAlpha=1。スロットはフェードの不透明度(0は描かない)。
struct MeshDrawEntry {
    MeshPipeline* pipeline = nullptr;
    float fadeAlpha = 1.0f;
};

// 差し込み口の段階。各段階は、その段階の枠の描画を終えた直後に呼ばれる。
enum class MeshFrameStage {
    OpaqueEntries,      // 不透明とフェード中の不透明を描いた直後(静的インスタンスの不透明・配置プレビューを足す)
    BlendedEntries,     // 材質色Diffuse.a<1(窓ガラスなど)を描いた直後(静的インスタンスのBlendedを足す)
    LateOpaqueEntries,  // 後描き不透明を描いた直後(静的インスタンスの後描き・ハイライト・計測の区切りを足す)
    TransparentEntries, // 半透明を描いた直後(半透明のインスタンスを足す。WBOITの蓄積中)
};

struct MeshFrameTargets {
    // 半透明の合成先。wboit が null か Ready でなければ、ソート版(通常のブレンド)で描く。
    WboitPass* wboit = nullptr;
    rs::rhi::CpuDescriptor depthDsv{};
    rs::rhi::CpuDescriptor compositeRtv{};
    SceneHdrTarget* compositeTarget = nullptr;
};

// 描く枠を、不透明 → 後描き不透明 → 半透明の順に記録する。onStage は空でもよい。
void RecordMeshFrame(const rs::rhi::CommandList& commandList, const MeshConstants& constants, UINT frameIndex,
                     std::span<const MeshDrawEntry> entries, const MeshFrameTargets& targets,
                     const std::function<void(MeshFrameStage)>& onStage);

} // namespace rs::render
