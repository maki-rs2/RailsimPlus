#include "DepthOfFieldPass.h"

#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

#include "DepthOfFieldMath.h"
#include "DepthOfFieldShader.h"
#include "FullscreenPassPipelines.h"

namespace rs::render {

namespace {

// ルート定数(HLSL の DepthOfFieldConstants と並びを一致させる。float2 は 16 バイトの境界をまたがない)。
struct DepthOfFieldShaderConstants {
    float invViewProj[16];
    float size[2];
    float invSize[2];
    float halfSize[2];
    float focalMm;
    float aperture;
    float focusMm;
    float mmToPx;
    float maxRadius;
    std::uint32_t samples;
    std::uint32_t frame;
    std::uint32_t tileSize;
    std::uint32_t colorIdx;
    std::uint32_t depthIdx;
    std::uint32_t prefilterIdx;
    std::uint32_t partialIdx;
    std::uint32_t tileIdx;
    std::uint32_t neighborIdx;
    std::uint32_t farIdx;
    std::uint32_t nearIdx;
    std::uint32_t pad[2];
};
constexpr std::uint32_t kConstantDwordCount = sizeof(DepthOfFieldShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(DepthOfFieldShaderConstants) == 160, "DepthOfFieldConstants は 40 DWORD(HLSL の cbuffer と同じ並び)");

// 枠の並び(SRV 番号は firstSrvIndex からの順)。
constexpr std::uint32_t kSlotPrefilter = 0;
constexpr std::uint32_t kSlotPartial = 1;
constexpr std::uint32_t kSlotTile = 2;
constexpr std::uint32_t kSlotNeighbor = 3;
constexpr std::uint32_t kSlotFar = 4;
constexpr std::uint32_t kSlotNear = 5;
constexpr std::uint32_t kSlotOutput = 6;
constexpr std::uint32_t kSlotDepth = 7;
static_assert(kSlotDepth + 1 == DepthOfFieldPass::kSrvCount, "SRV 枠の数と並びが合わない");

// FullscreenPassPipelines に追加するピクセルシェーダの順(Draw の番号)。
enum PixelShader : std::size_t { kPsPrefilter, kPsPartial, kPsTile, kPsNeighbor, kPsFar, kPsNear, kPsComposite };

std::uint32_t HalfSize(std::uint32_t pixels) { return (pixels + 1) / 2; }

} // namespace

struct DepthOfFieldPass::Impl {
    FullscreenPassPipelines pipelines;
    SceneHdrTarget prefilter; // 半解像度。rgb=色、a=符号付きのぼけの半径[フル解像度の画素]
    SceneHdrTarget partial;   // タイルの途中結果(横だけタイル幅に縮めた)
    SceneHdrTarget tile;      // タイルごとの近景のぼけの最大[半解像度の画素]
    SceneHdrTarget neighbor;  // 周囲 3x3 のタイルの最大
    SceneHdrTarget farField;  // 半解像度。遠景のギャザー
    SceneHdrTarget nearField; // 半解像度。rgb=近景の色、a=カバー率
    SceneHdrTarget output;
    std::uint32_t firstSrvIndex = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

DepthOfFieldPass::DepthOfFieldPass() = default;
DepthOfFieldPass::~DepthOfFieldPass() = default;

bool DepthOfFieldPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                            std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->firstSrvIndex = firstSrvIndex;

    // ルート署名は 32bit 定数 b0 だけ。SRV は ResourceDescriptorHeap[] から番号で引き、画素は Load で読む。
    FullscreenPassPipelines& pipelines = impl->pipelines;
    if (!pipelines.Init(device, srvHeap, kDepthOfFieldShader, "depth_of_field", kConstantDwordCount) ||
        !pipelines.AddPixelShader(L"PSPrefilter", HdrTargetFormat::Half16) ||
        !pipelines.AddPixelShader(L"PSTilePartial", HdrTargetFormat::Rg16Float) ||
        !pipelines.AddPixelShader(L"PSTileMax", HdrTargetFormat::Rg16Float) ||
        !pipelines.AddPixelShader(L"PSNeighborMax", HdrTargetFormat::Rg16Float) ||
        !pipelines.AddPixelShader(L"PSFar", HdrTargetFormat::Half16) ||
        !pipelines.AddPixelShader(L"PSNear", HdrTargetFormat::Half16) ||
        !pipelines.AddPixelShader(L"PSComposite", HdrTargetFormat::Half16))
        return false;

    const float black[3] = {0.0f, 0.0f, 0.0f};
    const auto slot = [&](std::uint32_t s) { return firstSrvIndex + s; };
    if (!impl->prefilter.Init(device, srvHeap, slot(kSlotPrefilter), black) ||
        !impl->partial.Init(device, srvHeap, slot(kSlotPartial), black, HdrTargetFormat::Rg16Float) ||
        !impl->tile.Init(device, srvHeap, slot(kSlotTile), black, HdrTargetFormat::Rg16Float) ||
        !impl->neighbor.Init(device, srvHeap, slot(kSlotNeighbor), black, HdrTargetFormat::Rg16Float) ||
        !impl->farField.Init(device, srvHeap, slot(kSlotFar), black) ||
        !impl->nearField.Init(device, srvHeap, slot(kSlotNear), black) ||
        !impl->output.Init(device, srvHeap, slot(kSlotOutput), black))
        return false;
    impl_ = std::move(impl);
    return true;
}

bool DepthOfFieldPass::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_ || width == 0 || height == 0) return false;
    Impl& impl = *impl_;
    impl.width = 0;
    impl.height = 0;
    const std::uint32_t halfWidth = HalfSize(width);
    const std::uint32_t halfHeight = HalfSize(height);
    const std::uint32_t tilesX = DepthOfFieldTileCount(halfWidth);
    const std::uint32_t tilesY = DepthOfFieldTileCount(halfHeight);
    if (!impl.prefilter.Resize(halfWidth, halfHeight) || !impl.partial.Resize(tilesX, halfHeight) ||
        !impl.tile.Resize(tilesX, tilesY) || !impl.neighbor.Resize(tilesX, tilesY) ||
        !impl.farField.Resize(halfWidth, halfHeight) || !impl.nearField.Resize(halfWidth, halfHeight) ||
        !impl.output.Resize(width, height)) {
        spdlog::error("[depth_of_field] 資源を作れませんでした {}x{}", width, height);
        return false;
    }
    impl.width = width;
    impl.height = height;
    return true;
}

bool DepthOfFieldPass::Ready(std::uint32_t width, std::uint32_t height) const {
    return impl_ && impl_->width == width && impl_->height == height && impl_->prefilter.Ready() &&
           impl_->output.Ready();
}

std::uint32_t DepthOfFieldPass::DepthSrvIndex() const { return impl_ ? impl_->firstSrvIndex + kSlotDepth : 0; }

SceneHdrTarget& DepthOfFieldPass::Output() { return impl_->output; }

void DepthOfFieldPass::Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& input,
                              const DepthOfFieldFrameParams& params) {
    if (!impl_ || !commandList.Valid() || !input.Ready() || !impl_->output.Ready() || input.Width() != impl_->width ||
        input.Height() != impl_->height)
        return;
    Impl& impl = *impl_;

    DepthOfFieldShaderConstants constants = {};
    std::memcpy(constants.invViewProj, params.invViewProj, sizeof(constants.invViewProj));
    constants.size[0] = static_cast<float>(impl.width);
    constants.size[1] = static_cast<float>(impl.height);
    constants.invSize[0] = 1.0f / constants.size[0];
    constants.invSize[1] = 1.0f / constants.size[1];
    constants.halfSize[0] = static_cast<float>(impl.prefilter.Width());
    constants.halfSize[1] = static_cast<float>(impl.prefilter.Height());
    constants.focalMm = params.focalLengthMm;
    constants.aperture = params.aperture;
    constants.focusMm = params.focusDistanceM * 1000.0f;
    constants.mmToPx = CocPixels(1.0, params.frameWidthMm, impl.width); // センサー上の 1mm あたりの画素数
    constants.maxRadius = DepthOfFieldMaxRadiusPixels(impl.width, params.maxRadiusFraction);
    constants.samples = ClampDepthOfFieldSamples(params.samples);
    constants.frame = params.frameIndex;
    constants.tileSize = kDepthOfFieldTileSize;
    constants.colorIdx = input.SrvIndex();
    constants.depthIdx = DepthSrvIndex();
    constants.prefilterIdx = impl.prefilter.SrvIndex();
    constants.partialIdx = impl.partial.SrvIndex();
    constants.tileIdx = impl.tile.SrvIndex();
    constants.neighborIdx = impl.neighbor.SrvIndex();
    constants.farIdx = impl.farField.SrvIndex();
    constants.nearIdx = impl.nearField.SrvIndex();

    input.Transition(commandList, HdrTargetState::ShaderResource);
    impl.pipelines.Bind(commandList, &constants);

    // 描いた RT は、次のパスが読めるよう ShaderResource へ戻す。出力だけ RenderTarget 状態のまま返す。
    const auto pass = [&](SceneHdrTarget& target, PixelShader ps) {
        impl.pipelines.Draw(commandList, target, ps);
        if (&target != &impl.output) target.Transition(commandList, HdrTargetState::ShaderResource);
    };
    pass(impl.prefilter, kPsPrefilter);
    pass(impl.partial, kPsPartial);
    pass(impl.tile, kPsTile);
    pass(impl.neighbor, kPsNeighbor);
    pass(impl.farField, kPsFar);
    pass(impl.nearField, kPsNear);
    pass(impl.output, kPsComposite);
}

} // namespace rs::render
