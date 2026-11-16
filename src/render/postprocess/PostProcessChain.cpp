#include "PostProcessChain.h"

#include <spdlog/spdlog.h>

namespace rs::render {

bool PostProcessChain::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                            std::uint32_t firstSrvIndex) {
    const bool bloomOk = bloom_.Init(device, srvHeap, firstSrvIndex);
    if (!bloomOk) spdlog::warn("[postprocess] ブルームを初期化できませんでした。ブルームは掛かりません");
    const float clear[3] = {0.0f, 0.0f, 0.0f};
    const bool fxaaOk = fxaa_.Init(device) &&
                        ldr_.Init(device, srvHeap, firstSrvIndex + BloomPass::kSrvCount, clear,
                                  HdrTargetFormat::Srgb8);
    if (!fxaaOk) spdlog::warn("[postprocess] FXAAを初期化できませんでした。FXAAは掛かりません");
    return bloomOk || fxaaOk;
}

bool PostProcessChain::Resize(std::uint32_t width, std::uint32_t height) {
    const bool bloomOk = bloom_.Resize(width, height);
    if (!bloomOk) spdlog::warn("[postprocess] ブルームの作業用RTを作れませんでした。ブルームは掛かりません");
    const bool fxaaOk = ldr_.Resize(width, height);
    if (!fxaaOk) spdlog::warn("[postprocess] FXAAの作業用RTを作れませんでした。FXAAは掛かりません");
    return bloomOk && fxaaOk;
}

void PostProcessChain::Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& scene,
                              const TonemapPass& tonemap, const TonemapParams& tonemapParams,
                              rs::rhi::CpuDescriptor outputRtv, PostProcessTimer* timer, const OutputRect* finalRect) {
    const PostProcessPlan plan = PlanStages(settings_, tuning_, tonemapParams);
    if (plan.bloom && bloom_.Ready()) {
        if (timer) timer->Begin(PostProcessStage::Bloom);
        BloomParams params;
        params.strength = tuning_.bloomStrength;
        params.threshold = tuning_.bloomThreshold;
        params.scatter = tuning_.bloomScatter;
        params.exposure = tonemapParams.exposure;
        bloom_.Record(commandList, scene, params);
        if (timer) timer->End(PostProcessStage::Bloom);
    }
    const bool runFxaa = plan.fxaa && fxaa_.Ready() && ldr_.Ready() && ldr_.Width() == scene.Width() &&
                         ldr_.Height() == scene.Height();
    if (timer) timer->Begin(PostProcessStage::Tonemap);
    scene.Transition(commandList, HdrTargetState::ShaderResource);
    if (runFxaa) ldr_.Transition(commandList, HdrTargetState::RenderTarget);
    // 最後の段が出力へ書くときだけ矩形へ縮める。トーンマップが最後なら今、FXAAが最後ならトーンマップの後。
    const auto setFinalRect = [&] {
        if (finalRect != nullptr) {
            commandList.SetViewportAndScissor(finalRect->x, finalRect->y, finalRect->width, finalRect->height);
        }
    };
    if (!runFxaa) setFinalRect();
    tonemap.Record(commandList, scene, runFxaa ? ldr_.Rtv() : outputRtv, tonemapParams,
                   runFxaa ? nullptr : finalRect);
    if (timer) timer->End(PostProcessStage::Tonemap);
    if (runFxaa) {
        if (timer) timer->Begin(PostProcessStage::Fxaa);
        ldr_.Transition(commandList, HdrTargetState::ShaderResource);
        setFinalRect();
        fxaa_.Record(commandList, ldr_, outputRtv, finalRect);
        if (timer) timer->End(PostProcessStage::Fxaa);
    }
    if (finalRect != nullptr) {
        commandList.SetViewportAndScissor(0.0f, 0.0f, static_cast<float>(scene.Width()),
                                          static_cast<float>(scene.Height()));
    }
}

} // namespace rs::render
