#include "VolumetricFogPass.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "../resource/ComputePipeline.h"
#include "../resource/VolumeTexture.h"
#include "VolumetricFogMath.h"
#include "VolumetricFogShader.h"

namespace rs::render {

namespace {

// ルート定数(HLSL の VolumetricFogConstants と並びを一致させる)。
struct VolumetricFogConstants {
    float cameraAndDistance[4];
    float right[4];
    float up[4];
    float forward[4];
    float toSun[4];
    float fog[4];
    float ambientScale;
    std::uint32_t grid[3];
    std::uint32_t adaptation[4];
    std::uint32_t diffuseCubeIdx;
    std::uint32_t transmittanceIdx;
    std::uint32_t scatterSrvIdx;
    std::uint32_t scatterUavIdx;
    std::uint32_t integratedUavIdx;
    std::uint32_t historySrvIdx;
    float historyWeight;
    float padding;
    float jitter[4];
    float prevViewProj[16];
};
constexpr std::uint32_t kConstantDwordCount = sizeof(VolumetricFogConstants) / sizeof(std::uint32_t);
static_assert(kConstantDwordCount == 60, "VolumetricFogConstants は HLSL の cbuffer と同じ並び");

constexpr std::uint32_t kInjectGroupSize = 4;    // CSInject の numthreads
constexpr std::uint32_t kIntegrateGroupSize = 8; // CSIntegrate の numthreads(奥行きは1スレッドが全スライスを進む)

void CopyVec3(float (&dst)[4], const float (&src)[3], float w) {
    std::memcpy(dst, src, sizeof(src));
    dst[3] = w;
}

} // namespace

struct VolumetricFogPass::Impl {
    rs::rhi::SrvHeap srvHeap;
    ComputePipeline injectPipeline;
    ComputePipeline integratePipeline;
    VolumeTexture scatter[2]; // 今フレームに書く側と、前フレームに書いた側を交互に入れ替える
    VolumeTexture integrated;
    std::uint32_t current = 0; // 最後に書いた scatter の番号
    float prevViewProj[16] = {};
    std::uint64_t lastFrameNumber = 0;
    std::uint32_t jitterIndex = 0;
    bool historyValid = false;
    bool historyUsed = false;
};

VolumetricFogPass::VolumetricFogPass() = default;
VolumetricFogPass::~VolumetricFogPass() = default;

bool VolumetricFogPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                             std::uint32_t firstSrvIndex) {
    impl_.reset();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->srvHeap = srvHeap;
    const std::string source = BuildVolumetricFogShader();
    if (!impl->injectPipeline.Init(device, source, L"CSInject", kConstantDwordCount, "volumetric_fog", true) ||
        !impl->integratePipeline.Init(device, source, L"CSIntegrate", kConstantDwordCount, "volumetric_fog", true) ||
        !impl->scatter[0].Init(device, srvHeap, firstSrvIndex) ||
        !impl->scatter[1].Init(device, srvHeap, firstSrvIndex + VolumeTexture::kSrvCount) ||
        !impl->integrated.Init(device, srvHeap, firstSrvIndex + VolumeTexture::kSrvCount * 2))
        return false;
    impl_ = std::move(impl);
    return true;
}

bool VolumetricFogPass::Resize(std::uint32_t gridWidth, std::uint32_t gridHeight, std::uint32_t sliceCount) {
    if (!impl_) return false;
    impl_->historyValid = false;
    return impl_->scatter[0].Resize(gridWidth, gridHeight, sliceCount) &&
           impl_->scatter[1].Resize(gridWidth, gridHeight, sliceCount) &&
           impl_->integrated.Resize(gridWidth, gridHeight, sliceCount);
}

bool VolumetricFogPass::Ready(std::uint32_t gridWidth, std::uint32_t gridHeight, std::uint32_t sliceCount) const {
    return impl_ && impl_->integrated.Ready() && impl_->integrated.Width() == gridWidth &&
           impl_->integrated.Height() == gridHeight && impl_->integrated.Depth() == sliceCount;
}

void VolumetricFogPass::Record(const rs::rhi::CommandList& commandList, const VolumetricFogFrameParams& params) {
    if (!impl_ || !commandList.Valid() || !impl_->scatter[0].Ready() || !impl_->scatter[1].Ready() ||
        !impl_->integrated.Ready())
        return;
    Impl& impl = *impl_;
    const std::uint32_t previousIndex = impl.current;
    impl.current = 1u - impl.current;
    VolumeTexture& scatter = impl.scatter[impl.current];
    VolumeTexture& previous = impl.scatter[previousIndex];
    const bool useHistory = impl.historyValid && !params.discardHistory && params.historyWeight > 0.0f &&
                            params.frameNumber == impl.lastFrameNumber + 1;
    impl.historyUsed = useHistory;

    VolumetricFogConstants constants = {};
    CopyVec3(constants.cameraAndDistance, params.cameraPos, params.maxDistance);
    CopyVec3(constants.right, params.camera.right, params.camera.tanHalfFovX);
    CopyVec3(constants.up, params.camera.up, params.camera.tanHalfFovY);
    CopyVec3(constants.forward, params.camera.forward, params.anisotropy);
    CopyVec3(constants.toSun, params.toSun, params.sunIntensity);
    constants.fog[0] = params.fogDensity;
    constants.fog[1] = params.fogHeightScale;
    constants.fog[2] = params.fogBaseHeight;
    constants.fog[3] = params.fogAlbedo;
    constants.ambientScale = params.ambientScale;
    const std::uint32_t gridX = scatter.Width();
    const std::uint32_t gridY = scatter.Height();
    const std::uint32_t gridZ = scatter.Depth();
    constants.grid[0] = gridX;
    constants.grid[1] = gridY;
    constants.grid[2] = gridZ;
    constants.adaptation[0] = params.exposureSrvIndex;
    constants.adaptation[1] = params.adaptationEnabled ? 1u : 0u;
    constants.diffuseCubeIdx = params.diffuseCubeSrvIndex;
    constants.transmittanceIdx = params.transmittanceSrvIndex;
    constants.scatterSrvIdx = scatter.SrvIndex();
    constants.scatterUavIdx = scatter.UavIndex();
    constants.integratedUavIdx = impl.integrated.UavIndex();
    constants.historySrvIdx = previous.SrvIndex();
    constants.historyWeight = useHistory ? params.historyWeight : 0.0f;
    const volumetric_fog::CellJitter jitter = volumetric_fog::CellJitterForFrame(impl.jitterIndex++);
    constants.jitter[0] = jitter.x;
    constants.jitter[1] = jitter.y;
    constants.jitter[2] = jitter.z;
    std::memcpy(constants.prevViewProj, impl.prevViewProj, sizeof(constants.prevViewProj));

    scatter.Transition(commandList, VolumeTextureState::UnorderedAccess);
    impl.injectPipeline.Bind(commandList, impl.srvHeap);
    impl.injectPipeline.SetConstants(commandList, &constants);
    impl.injectPipeline.SetRootCbv(commandList, params.shadowConstantsAddress);
    impl.injectPipeline.Dispatch(commandList, volumetric_fog::GridCellCount(gridX, kInjectGroupSize),
                                 volumetric_fog::GridCellCount(gridY, kInjectGroupSize),
                                 volumetric_fog::GridCellCount(gridZ, kInjectGroupSize));

    scatter.Transition(commandList, VolumeTextureState::ShaderResource);
    impl.integrated.Transition(commandList, VolumeTextureState::UnorderedAccess);
    impl.integratePipeline.Bind(commandList, impl.srvHeap);
    impl.integratePipeline.SetConstants(commandList, &constants);
    impl.integratePipeline.SetRootCbv(commandList, params.shadowConstantsAddress);
    impl.integratePipeline.Dispatch(commandList, volumetric_fog::GridCellCount(gridX, kIntegrateGroupSize),
                                    volumetric_fog::GridCellCount(gridY, kIntegrateGroupSize), 1);
    impl.integrated.Transition(commandList, VolumeTextureState::ShaderResource);

    std::memcpy(impl.prevViewProj, params.viewProj, sizeof(impl.prevViewProj));
    impl.lastFrameNumber = params.frameNumber;
    impl.historyValid = true;
}

std::uint32_t VolumetricFogPass::IntegratedSrvIndex() const { return impl_ ? impl_->integrated.SrvIndex() : 0; }
VolumeTexture& VolumetricFogPass::Scatter() { return impl_->scatter[impl_->current]; }
bool VolumetricFogPass::HistoryUsed() const { return impl_ && impl_->historyUsed; }
VolumeTexture& VolumetricFogPass::Integrated() { return impl_->integrated; }

} // namespace rs::render
