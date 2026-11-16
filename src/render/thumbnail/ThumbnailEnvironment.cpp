//
// ThumbnailEnvironment.cpp - ThumbnailEnvironment.h の実装
//
#include "ThumbnailEnvironment.h"

#include "../../core/Math.h"
#include "../resource/RenderTuning.h"
#include "../sky/AtmosphereLuts.h"
#include "../sky/IblGenerator.h"
#include "ThumbnailLighting.h"

namespace rs::render {

namespace {

} // namespace

struct ThumbnailEnvironment::Impl {
    AtmosphereLuts luts;
    IblGenerator ibl;
    bool generated = false;
};

std::uint32_t ThumbnailEnvironment::SrvCount() {
    // 大気LUT・IBLの連続した枠の後ろに、月用のSky-View LUTの1枠(サムネイルは月を使わないが、シェーダが引く枠は要る)と、
    // 暗順応の結果(1x1)の1枠(サムネイルは本編のトーンマップの倍率を掛けないが、メッシュのシェーダが引く枠は要る)。
    return AtmosphereLuts::kSrvCount + IblGenerator::kSrvCount + 2;
}

ThumbnailEnvironment::ThumbnailEnvironment() = default;
ThumbnailEnvironment::~ThumbnailEnvironment() = default;

bool ThumbnailEnvironment::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                std::uint32_t firstSrvIndex) {
    impl_.reset();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    // IBLの先頭は、大気LUTの3枠のすぐ後ろ(IblGenerator.h の取り決め)。
    const std::uint32_t moonSrvIndex = firstSrvIndex + AtmosphereLuts::kSrvCount + IblGenerator::kSrvCount;
    if (!impl->luts.Init(device, srvHeap, firstSrvIndex, moonSrvIndex) ||
        !impl->ibl.Init(device, srvHeap, firstSrvIndex + AtmosphereLuts::kSrvCount, moonSrvIndex + 1))
        return false;
    impl_ = std::move(impl);
    return true;
}

void ThumbnailEnvironment::RecordGeneration(const rs::rhi::CommandList& commandList) {
    if (!impl_) return;
    // 直射光と同じ太陽(ThumbnailLighting.h)。
    const rs::core::Vec3 sunDirectionToSun = ThumbnailSunDirectionToSun();
    impl_->luts.Update(commandList, sunDirectionToSun);
    impl_->ibl.Update(commandList, impl_->luts, sunDirectionToSun, RenderTuning::Embedded());
    impl_->generated = true;
}

bool ThumbnailEnvironment::Ready() const {
    return impl_ && impl_->generated && impl_->luts.Ready() && impl_->ibl.Ready();
}

EnvironmentSrvIndices ThumbnailEnvironment::SrvIndices() const {
    EnvironmentSrvIndices srv;
    if (!impl_) return srv;
    srv.diffuse = impl_->ibl.DiffuseSrvIndex();
    srv.specular = impl_->ibl.SpecularSrvIndex();
    srv.brdfLut = impl_->ibl.BrdfLutSrvIndex();
    srv.exposure = impl_->ibl.ExposureSrvIndex();
    srv.transmittance = impl_->luts.TransmittanceSrvIndex();
    return srv;
}

} // namespace rs::render
