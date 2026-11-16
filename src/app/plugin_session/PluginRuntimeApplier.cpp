//
// PluginRuntimeApplier.cpp - PluginRuntimeApplier の実装
//
#include "PluginRuntimeApplier.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../../plugins/schema/SwitchExpr.h"
#include "../../plugins/load/runtime/MaterialRuntime.h"
#include "../../plugins/load/runtime/AnimationRuntime.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../render/pipeline/MeshPipeline.h"
#include "../gpu/App.h"
#include "PluginSession.h"

namespace rs::app {

namespace {

void ApplyMaterialResult(App& app, rs::render::MeshPipeline& pipeline,
                          uint32_t matIdx, const rs::legacy_plugin::MaterialApplyResult& r) {
    pipeline.UpdateMaterialColor(matIdx, r.color);
    if (r.hasTextureOverride && !r.base.empty()) {
        rs::render::PreloadedTexInfo info;
        bool ok = pipeline.FindPreloadedSlot(r.base, r.normal, r.pbr, info);
        if (!ok && !pipeline.TexKnownFailed(r.base) &&
            !pipeline.TexBudgetExhausted(r.base, r.normal, r.pbr)) {
            // このスロットのプールに無いとき(モデル切替の直後など)はここで登録する。
            // 走行中に呼ばれるのでGPUの完了は待たない。
            // 失敗が確定済み(TexKnownFailed=読込失敗、
            // TexBudgetExhausted=このパイプラインのテクスチャ枠切れ)なら
            // RunUploadCommands 自体を呼ばない。呼ぶと毎フレーム・
            // 失敗テクスチャの数だけキューへの提出が走る。
            app.RunUploadCommands(
                [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
                    ok = pipeline.PreloadTexture(device, upload, r.base, r.normal,
                                                r.pbr, info);
                },
                UploadCompletion::NoWait);
        }
        if (ok) {
            // hasNormal/hasOrm はプール側(info)から導出する(読込に失敗した
            // 法線/ORMを、r.normal/r.pbrの非空だけで有りとみなさないため)。
            // 法線/ORMはChangeTextureが組み立てた画像なので、今のEnvMap/BumpMap指定が
            // 成り立つ材質だけで使う。
            pipeline.UpdateMaterialTexture(matIdx, true, info.texSlotOffset,
                                           info.hasNormal && r.AllowsNormal(),
                                           info.hasOrm && r.AllowsOrm());
        }
    } else if (r.hasTextureOverride) {
        // base="" が active（通常起きないが安全のためテクスチャ無し扱い）
        pipeline.UpdateMaterialTexture(matIdx, false, 0, false, false);
    } else {
        // hasTextureOverride==false: どの ChangeTexture Case/条件も
        // active でない。毎フレーム明示的に真の .x ベーステクスチャへ
        // 戻す(触らないと、直前に active だった Case のテクスチャに
        // 止まったまま戻らない)。
        pipeline.RevertMaterialTexture(matIdx);
    }
}

void ApplyAnimationResult(App& app, rs::render::MeshPipeline& pipeline,
                           uint32_t matIdx, const rs::legacy_plugin::AnimationApplyResult& r,
                           bool materialRuntimeManagesTextures) {
    if (r.hasTexture && !r.texturePath.empty()) {
        rs::render::PreloadedTexInfo info;
        bool ok = pipeline.FindPreloadedSlot(r.texturePath, "", "", info);
        if (!ok && !pipeline.TexKnownFailed(r.texturePath) &&
            !pipeline.TexBudgetExhausted(r.texturePath, "", "")) {
            // 事前ロードに漏れていた場合のフォールバック。恒久失敗済み
            // (読込失敗/テクスチャ枠切れ)なら RunUploadCommands 自体を
            // 呼ばない(理由は上のChangeTexture側コメント参照)。
            app.RunUploadCommands(
                [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
                    ok = pipeline.PreloadTexture(device, upload, r.texturePath, "",
                                                "", info);
                },
                UploadCompletion::NoWait);
        }
        // このアニメ経路は常に normal/pbr を渡さない(base のみ)ため、
        // hasNormal/hasOrm はプール側(info)由来でも常に false になる。
        if (ok) pipeline.UpdateMaterialTexture(matIdx, true, info.texSlotOffset,
                                               info.hasNormal, info.hasOrm);
    } else if (r.textureCapable && !materialRuntimeManagesTextures) {
        // Simple(画像差し替え)系SetAnimationの対象materialIdだが
        // 今フレームは非active(または一致するframe文が無い)。
        // ChangeTextureを1つも持たないパーツ(=MaterialRuntime側の
        // RevertMaterialTexture経路を通らないmaterialId)では他に
        // 誰も戻さないため、ここで明示的にベースのテクスチャへ戻す。
        // MaterialRuntimeがこのパーツを管理している場合は、既に
        // 今フレームの正しいテクスチャ(ChangeTexture active分含む)を
        // 書き込み済みなので、ここで巻き戻さない(上のコメント参照)。
        // textureCapable==falseのmaterialId(UV系のみ関与)は
        // 一切触れない。
        pipeline.RevertMaterialTexture(matIdx);
    }
    float uv6[6] = {r.uv.a, r.uv.b, r.uv.c, r.uv.d, r.uv.tx, r.uv.ty};
    pipeline.UpdateMaterialUvTransform(matIdx, uv6);
}

} // namespace

bool ApplySceneRuntime(App& app, rs::render::MeshPipeline& pipeline,
                        PluginSession& session, uint64_t& appliedGeneration, bool force,
                        SessionRuntimeTimings* timings) {
    SessionRuntimeSink sink;
    sink.material = [&](uint32_t index, const rs::legacy_plugin::MaterialApplyResult& result) {
        ApplyMaterialResult(app, pipeline, index, result);
    };
    sink.animation = [&](uint32_t index, const rs::legacy_plugin::AnimationApplyResult& result,
                          bool managed) {
        ApplyAnimationResult(app, pipeline, index, result, managed);
    };
    return ApplySessionMaterialAndAnimation(session, appliedGeneration, sink, force, timings);
}

} // namespace rs::app
