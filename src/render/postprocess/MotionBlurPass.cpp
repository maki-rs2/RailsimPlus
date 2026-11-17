#include "MotionBlurPass.h"

#include <cstring>
#include <string>

#include <spdlog/spdlog.h>

#include "MotionBlurMath.h"
#include "MotionBlurShader.h"
#include "FullscreenPassPipelines.h"

namespace rs::render {

namespace {

// ルート定数(HLSL の MotionBlurConstants と並びを一致させる)。
struct MotionBlurShaderConstants {
    float invViewProj[16];
    float prevViewProj[16];
    float invSize[2];
    float size[2];
    float shutter;
    float maxRadius;
    std::uint32_t samples;
    std::uint32_t frame;
    std::uint32_t tileSize;
    std::uint32_t colorIdx;
    std::uint32_t depthIdx;
    std::uint32_t objectVelocityIdx;
    std::uint32_t velocityIdx;
    std::uint32_t partialIdx;
    std::uint32_t tileIdx;
    std::uint32_t neighborIdx;
};
constexpr std::uint32_t kConstantDwordCount = sizeof(MotionBlurShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(MotionBlurShaderConstants) == 192, "MotionBlurConstants は 48 DWORD(HLSL の cbuffer と同じ並び)");

// 枠の並び(SRV 番号は firstSrvIndex からの順)。
constexpr std::uint32_t kSlotVelocity = 0;
constexpr std::uint32_t kSlotPartial = 1;
constexpr std::uint32_t kSlotTile = 2;
constexpr std::uint32_t kSlotNeighbor = 3;
constexpr std::uint32_t kSlotOutput = 4;
constexpr std::uint32_t kSlotDepth = 5;
static_assert(kSlotDepth + 1 == MotionBlurPass::kSrvCount, "SRV 枠の数と並びが合わない");

// FullscreenPassPipelines に追加するピクセルシェーダの順(Draw の番号)。
enum PixelShader : std::size_t { kPsVelocity, kPsPartial, kPsTile, kPsNeighbor, kPsGather };

} // namespace

struct MotionBlurPass::Impl {
    FullscreenPassPipelines pipelines;
    SceneHdrTarget velocity;  // 合成した速度(xy=半径ベクトル[画素]、z=線形の深度)。画面と同じ大きさ
    SceneHdrTarget partial;   // タイルの途中結果(横だけタイル幅に縮めた)
    SceneHdrTarget tile;      // タイルごとの最大の速度
    SceneHdrTarget neighbor;  // 周囲 3x3 のタイルの最大の速度
    SceneHdrTarget output;
    std::uint32_t firstSrvIndex = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

MotionBlurPass::MotionBlurPass() = default;
MotionBlurPass::~MotionBlurPass() = default;

bool MotionBlurPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                          std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->firstSrvIndex = firstSrvIndex;

    // ルート署名は 32bit 定数 b0 と bindless テーブル。SRV は配列から番号で引き、画素は Load で読む。
    FullscreenPassPipelines& pipelines = impl->pipelines;
    if (!pipelines.Init(device, srvHeap, kMotionBlurShader, "motion_blur", kConstantDwordCount) ||
        !pipelines.AddPixelShader(L"PSVelocity", HdrTargetFormat::Half16) ||
        !pipelines.AddPixelShader(L"PSTilePartial", HdrTargetFormat::Rg16Float) ||
        !pipelines.AddPixelShader(L"PSTileMax", HdrTargetFormat::Rg16Float) ||
        !pipelines.AddPixelShader(L"PSNeighborMax", HdrTargetFormat::Rg16Float) ||
        !pipelines.AddPixelShader(L"PSGather", HdrTargetFormat::Half16))
        return false;

    const float black[3] = {0.0f, 0.0f, 0.0f};
    const auto slot = [&](std::uint32_t s) { return firstSrvIndex + s; };
    if (!impl->velocity.Init(device, srvHeap, slot(kSlotVelocity), black) ||
        !impl->partial.Init(device, srvHeap, slot(kSlotPartial), black, HdrTargetFormat::Rg16Float) ||
        !impl->tile.Init(device, srvHeap, slot(kSlotTile), black, HdrTargetFormat::Rg16Float) ||
        !impl->neighbor.Init(device, srvHeap, slot(kSlotNeighbor), black, HdrTargetFormat::Rg16Float) ||
        !impl->output.Init(device, srvHeap, slot(kSlotOutput), black))
        return false;
    impl_ = std::move(impl);
    return true;
}

bool MotionBlurPass::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_ || width == 0 || height == 0) return false;
    Impl& impl = *impl_;
    impl.width = 0;
    impl.height = 0;
    const std::uint32_t tilesX = MotionBlurTileCount(width);
    const std::uint32_t tilesY = MotionBlurTileCount(height);
    if (!impl.velocity.Resize(width, height) || !impl.partial.Resize(tilesX, height) ||
        !impl.tile.Resize(tilesX, tilesY) || !impl.neighbor.Resize(tilesX, tilesY) ||
        !impl.output.Resize(width, height)) {
        spdlog::error("[motion_blur] 資源を作れませんでした {}x{}", width, height);
        return false;
    }
    impl.width = width;
    impl.height = height;
    return true;
}

bool MotionBlurPass::Ready(std::uint32_t width, std::uint32_t height) const {
    return impl_ && impl_->width == width && impl_->height == height && impl_->velocity.Ready() &&
           impl_->output.Ready();
}

std::uint32_t MotionBlurPass::DepthSrvIndex() const { return impl_ ? impl_->firstSrvIndex + kSlotDepth : 0; }

SceneHdrTarget& MotionBlurPass::Output() { return impl_->output; }

void MotionBlurPass::Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& input,
                            const MotionBlurFrameParams& params) {
    if (!impl_ || !commandList.Valid() || !input.Ready() || !impl_->output.Ready() || input.Width() != impl_->width ||
        input.Height() != impl_->height)
        return;
    Impl& impl = *impl_;

    MotionBlurShaderConstants constants = {};
    std::memcpy(constants.invViewProj, params.invViewProj, sizeof(constants.invViewProj));
    std::memcpy(constants.prevViewProj, params.prevViewProj, sizeof(constants.prevViewProj));
    constants.invSize[0] = 1.0f / static_cast<float>(impl.width);
    constants.invSize[1] = 1.0f / static_cast<float>(impl.height);
    constants.size[0] = static_cast<float>(impl.width);
    constants.size[1] = static_cast<float>(impl.height);
    constants.shutter = params.shutter;
    constants.maxRadius = MotionBlurMaxRadiusPixels(impl.height, params.maxRadiusFraction);
    constants.samples = ClampMotionBlurSamples(params.samples);
    constants.frame = params.frameIndex;
    constants.tileSize = kMotionBlurTileSize;
    constants.colorIdx = input.SrvIndex();
    constants.depthIdx = DepthSrvIndex();
    constants.objectVelocityIdx = params.objectVelocitySrvIndex;
    constants.velocityIdx = impl.velocity.SrvIndex();
    constants.partialIdx = impl.partial.SrvIndex();
    constants.tileIdx = impl.tile.SrvIndex();
    constants.neighborIdx = impl.neighbor.SrvIndex();

    input.Transition(commandList, HdrTargetState::ShaderResource);
    impl.pipelines.Bind(commandList, &constants);

    // 書き込み先をバインドして(クリアする)、その大きさの全画面の三角形を1回描く。
    const auto draw = [&](SceneHdrTarget& target, PixelShader ps) { impl.pipelines.Draw(commandList, target, ps); };
    draw(impl.velocity, kPsVelocity);
    impl.velocity.Transition(commandList, HdrTargetState::ShaderResource);
    draw(impl.partial, kPsPartial);
    impl.partial.Transition(commandList, HdrTargetState::ShaderResource);
    draw(impl.tile, kPsTile);
    impl.tile.Transition(commandList, HdrTargetState::ShaderResource);
    draw(impl.neighbor, kPsNeighbor);
    impl.neighbor.Transition(commandList, HdrTargetState::ShaderResource);
    draw(impl.output, kPsGather); // 出力は RenderTarget 状態のまま返す
}

} // namespace rs::render
