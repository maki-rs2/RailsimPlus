//
// PostProcessChain.h - シーンを描き終えたHDRから、バックバッファへ出すまでのポストプロセスの枠
//
// 本編の「HDR → (ブルーム) → トーンマップ → (FXAA) → バックバッファ」を1か所で記録する。
// 今ある段はブルーム・トーンマップ・FXAAで、GTAO・TAAは段を足していく。
// FXAAを走らせるときだけ、トーンマップはLDR用の中間RTへ書き、FXAAがバックバッファへ書く。
// どの段を走らせるかは PlanStages(PostProcessPlan.h)が決める。
// トーンマップはサムネイルと共用するので、TonemapPassは呼び出し側が持ち、Recordの引数で受け取る。
// サムネイルはこのクラスを通らない(本編にある効果のうち、撮り済みの画像と食い違うものを入れないため)。
//
#pragma once

#include <cstdint>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "BloomPass.h"
#include "FxaaPass.h"
#include "PostProcessPlan.h"
#include "OutputRect.h"
#include "SceneHdrTarget.h"
#include "TonemapMath.h"
#include "TonemapPass.h"

namespace rs::render {

enum class PostProcessStage {
    Bloom,
    Tonemap,
    Fxaa,
};

// 段ごとのGPU計測の区間。render層は計測器(app層)に依存しないので、呼び出し側が実装して渡す。
class PostProcessTimer {
public:
    virtual ~PostProcessTimer() = default;
    virtual void Begin(PostProcessStage stage) = 0;
    virtual void End(PostProcessStage stage) = 0;
};

class PostProcessChain {
public:
    // 作業用RTが使うSRVの枠数(Init の firstSrvIndex から連続)。
    // ブルームの各段 + FXAAの入力(LDR)で1枠。
    static constexpr std::uint32_t kSrvCount = BloomPass::kSrvCount + 1;

    // 作業用RTとPSOを作る。失敗しても Record は動く(その段が走らないだけ)。
    // 少なくとも1つの段を作れたら true。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 作業用RTを作り直す。呼ぶ前にGPUがこのRT群を使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    void SetSettings(const PostProcessSettings& settings) { settings_ = settings; }
    void SetTuning(const PostProcessTuning& tuning) { tuning_ = tuning; }

    // 計画された段を記録し、scene をShaderResource状態へ遷移させて、tonemap(と、走らせるなら FXAA)で
    // outputRtv(kTonemapOutputFormat)へ書く。
    // ビューポート・シザーは、呼び出し前に呼び出し側が設定済みの前提(ブルームを走らせた場合は、
    // scene の大きさに張り直した状態で戻る)。timer は省略できる。
    // finalRect を渡すと、最終の出力(トーンマップかFXAAのうち最後の段)をその矩形へ縮めて書く
    // (中間の段は scene の大きさのまま)。戻るときは scene の大きさのビューポートに戻す。
    void Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& scene, const TonemapPass& tonemap,
                const TonemapParams& tonemapParams, rs::rhi::CpuDescriptor outputRtv,
                PostProcessTimer* timer = nullptr, const OutputRect* finalRect = nullptr);

private:
    PostProcessSettings settings_;
    PostProcessTuning tuning_;
    BloomPass bloom_;
    FxaaPass fxaa_;
    // トーンマップ後のLDR。FXAAの入力。画面と同じ大きさで常に確保しておく(切替でGPUの待ちを入れないため)。
    SceneHdrTarget ldr_;
};

} // namespace rs::render
