// MeshFrameRecord.cpp - RecordMeshFrame(不透明 → 後描き不透明 → 半透明の記録順を1か所に持つ)。
#include "MeshFrameRecord.h"

#include "../postprocess/SceneHdrTarget.h"
#include "../wboit/WboitPass.h"

namespace rs::render {

namespace {

UINT PartCount(const MeshPipeline& pipeline) {
    return static_cast<UINT>(pipeline.PartXforms().size());
}

void Notify(const std::function<void(MeshFrameStage)>& onStage, MeshFrameStage stage) {
    if (onStage) onStage(stage);
}

// フェード中の枠の定数。PSがgMaterial.wを掛けるので、半透明側も一緒にフェードする。
MeshConstants FadeConstants(const MeshConstants& base, float fadeAlpha) {
    MeshConstants fade = base;
    fade.material[3] = fadeAlpha;
    return fade;
}

} // namespace

void RecordMeshFrame(const rs::rhi::CommandList& commandList, const MeshConstants& constants, UINT frameIndex,
                     std::span<const MeshDrawEntry> entries, const MeshFrameTargets& targets,
                     const std::function<void(MeshFrameStage)>& onStage) {
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    // 不透明。フェード中の枠は、不透明パス側で深度EQUALのブレンドで済ませる。
    for (const MeshDrawEntry& entry : entries)
        if (entry.fadeAlpha >= 1.0f)
            entry.pipeline->RecordInstancedMultiPart(cmdList, constants, frameIndex, 1, PartCount(*entry.pipeline));
    for (const MeshDrawEntry& entry : entries)
        if (entry.fadeAlpha > 0.0f && entry.fadeAlpha < 1.0f)
            entry.pipeline->RecordFadeInstancedMultiPart(cmdList, FadeConstants(constants, entry.fadeAlpha),
                                                         frameIndex, 1, PartCount(*entry.pipeline));
    Notify(onStage, MeshFrameStage::OpaqueEntries);

    // 材質色Diffuse.a<1の材質(窓ガラスなど)は深度を書くので、全枠の不透明の後にまとめて描く。
    // 途中で描くと、後から描く物(窓越しの景色)が深度で落ちる。
    for (const MeshDrawEntry& entry : entries)
        entry.pipeline->RecordBlendedInstancedMultiPart(cmdList, constants, frameIndex, 1,
                                                        PartCount(*entry.pipeline));
    Notify(onStage, MeshFrameStage::BlendedEntries);

    // 後描き不透明(PIのTransparent指定)は、全枠の不透明の後にまとめて描く。
    for (const MeshDrawEntry& entry : entries)
        entry.pipeline->RecordLateOpaqueInstancedMultiPart(cmdList, constants, frameIndex, 1,
                                                           PartCount(*entry.pipeline));
    Notify(onStage, MeshFrameStage::LateOpaqueEntries);

    // 半透明。フェード中の枠は、元から半透明のsubmesh(Transparent)だけをここで描く
    // (alpha==0はWriteMeshConstantsが未設定扱いに読み替えるので、その手前で描画ごと落とす)。
    const bool wboit = targets.wboit != nullptr && targets.wboit->Ready() && targets.compositeTarget != nullptr;
    if (wboit) targets.wboit->Begin(commandList, targets.depthDsv);
    for (const MeshDrawEntry& entry : entries) {
        if (entry.fadeAlpha >= 1.0f) {
            entry.pipeline->RecordTransparentInstancedMultiPart(cmdList, constants, frameIndex, 1,
                                                               PartCount(*entry.pipeline), wboit);
        } else if (entry.fadeAlpha > 0.0f) {
            entry.pipeline->RecordTransparentInstancedMultiPart(cmdList, FadeConstants(constants, entry.fadeAlpha),
                                                               frameIndex, 1, PartCount(*entry.pipeline), wboit);
        }
    }
    Notify(onStage, MeshFrameStage::TransparentEntries);
    if (wboit) targets.wboit->Composite(commandList, targets.compositeRtv, *targets.compositeTarget);
}

} // namespace rs::render
