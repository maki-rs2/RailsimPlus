#include "IblGenerator.h"
#include "../RenderPrecision.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../resource/RenderTuning.h"
#include "AtmosphereGpuUtil.h"
#include "AtmosphereMath.h"
#include "BrdfLut.h"
#include "CubeTarget.h"
#include "ExposureTarget.h"
#include "IblBakeConstants.h"
#include "IblParams.h"
#include "IblShader.h"
#include "LandscapeParams.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

using namespace rs::render::ibl_bake;

namespace {

constexpr UINT kRootParamSrvTable = 0;
constexpr UINT kRootParamConstants = 1;
constexpr UINT kRootParamBakeLandscape = 2;
constexpr UINT kRootParamBakePhoto = 3;
constexpr UINT kRootParamBakeSkyMean = 4;
constexpr UINT kRootParamBakeMoon = 5;
constexpr UINT kRootParamBakeExposure = 6;

// 環境ソースの焼き込み(と大気の空だけの平均)はt0..t2(大気LUT)+追加テーブルのt3(標準の空の遠景キューブ)・
// t4(Envの鏡面キューブ=写真)・t5(大気の空だけの上半球平均)・t6(月用のSky-View LUT)・t7(暗順応の1x1)を引く。
// 追加テーブルは別々の場所のSRVなので1枠ずつ。
// 畳み込みはt0..t3(大気LUT+環境ソース)を引く。
constexpr UINT kBakeSrvCount = AtmosphereLuts::kSrvCount;
constexpr UINT kBakeExtraTableSrvCount = 1;
constexpr UINT kBakeExtraTableCount = 5; // 遠景・写真・上半球平均・月のSky-View・暗順応
constexpr UINT kFilterSrvCount = AtmosphereLuts::kSrvCount + 1;

} // namespace

struct IblGenerator::Impl {
    rs::rhi::SrvHeap srvHeap;
    CubeTarget envSource;
    CubeTarget diffuse;
    CubeTarget specular;
    CubeTarget skyMean; // 大気の空だけの上半球平均(1x1、+X面だけ使う)
    ExposureTarget exposure; // 暗順応の倍率と照度(1x1。トーンマップ・メッシュのシェーダが読む)
    BrdfLut brdfLut;

    ComPtr<ID3D12RootSignature> rootSigBake;
    ComPtr<ID3D12RootSignature> rootSigFilter;
    ComPtr<ID3D12PipelineState> psoBake;
    ComPtr<ID3D12PipelineState> psoSkyMean;
    ComPtr<ID3D12PipelineState> psoAdaptation;
    ComPtr<ID3D12PipelineState> psoDownsample;
    ComPtr<ID3D12PipelineState> psoDiffuse;
    ComPtr<ID3D12PipelineState> psoSpecular;

    std::uint32_t firstSrvIndex = 0; // 5枠の先頭の絶対番号
    std::uint32_t exposureSrvIndex = 0; // 暗順応の結果(1x1)のSRVの絶対番号
    bool hasGenerated = false;
    rs::core::Vec3 lastSun;
    // 前回の生成に使った調整値の版。版は解放されないので、アドレスが同じなら同じ版。
    const RenderTuning* lastTuning = nullptr;
    bool lastHadLandscape = false; // 前回の生成で遠景を焼いたか(変わったら作り直す)
    IblPhotoSky lastPhotoSky;      // 前回の生成で使った写真の空の層(層の有無・キューブ・平均色が変わったら作り直す)
    IblMoon lastMoon;              // 前回の生成で使った月明かり(方向・比が変わったら作り直す)

    // 写真の空の層の入力が、前回の生成と変わったか(方位合わせの回転・太陽の方向は見ない)。
    bool PhotoSkyChanged(const IblPhotoSky& photoSky) const {
        const bool had = lastPhotoSky.specularCubeSrvGpuHandle != 0;
        const bool has = photoSky.specularCubeSrvGpuHandle != 0;
        if (had != has) return true;
        if (!has) return false;
        if (lastPhotoSky.fullSphere != photoSky.fullSphere) return true;
        if (lastPhotoSky.specularCubeSrvGpuHandle != photoSky.specularCubeSrvGpuHandle ||
            lastPhotoSky.cubeSerial != photoSky.cubeSerial)
            return true;
        for (int i = 0; i < 3; ++i) {
            if (lastPhotoSky.skyMean[i] != photoSky.skyMean[i]) return true;
        }
        return false;
    }

    // 焼き込み用ルート署名の追加テーブル5つ(遠景・写真・上半球平均・月のSky-View・暗順応)を設定する。
    static void SetBakeExtraTables(ID3D12GraphicsCommandList* list, std::uint64_t landscape, std::uint64_t photo,
                                   std::uint64_t skyMeanTable, std::uint64_t moonSkyViewTable,
                                   std::uint64_t exposureTable) {
        list->SetGraphicsRootDescriptorTable(kRootParamBakeLandscape, D3D12_GPU_DESCRIPTOR_HANDLE{landscape});
        list->SetGraphicsRootDescriptorTable(kRootParamBakePhoto, D3D12_GPU_DESCRIPTOR_HANDLE{photo});
        list->SetGraphicsRootDescriptorTable(kRootParamBakeSkyMean, D3D12_GPU_DESCRIPTOR_HANDLE{skyMeanTable});
        list->SetGraphicsRootDescriptorTable(kRootParamBakeMoon, D3D12_GPU_DESCRIPTOR_HANDLE{moonSkyViewTable});
        list->SetGraphicsRootDescriptorTable(kRootParamBakeExposure, D3D12_GPU_DESCRIPTOR_HANDLE{exposureTable});
    }

    // 1つのパス(ルート署名・PSO・SRVテーブルの設定)。以降、面ごとに DrawFace を呼ぶ。
    static void BeginPass(ID3D12GraphicsCommandList* list, ID3D12RootSignature* rootSig,
                          ID3D12PipelineState* pso, std::uint64_t tableGpuHandle) {
        list->SetGraphicsRootSignature(rootSig);
        list->SetPipelineState(pso);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 0, nullptr);
        list->IASetIndexBuffer(nullptr);
        const D3D12_GPU_DESCRIPTOR_HANDLE table = {tableGpuHandle};
        list->SetGraphicsRootDescriptorTable(kRootParamSrvTable, table);
    }

    static void DrawFace(const rs::rhi::CommandList& commandList, const CubeTarget& target, std::uint32_t face,
                         std::uint32_t mip, const void* constants, UINT constantDwords) {
        ID3D12GraphicsCommandList* list = commandList.Native();
        target.BindFaceMip(commandList, face, mip);
        list->SetGraphicsRoot32BitConstants(kRootParamConstants, constantDwords, constants, 0);
        list->DrawInstanced(3, 1, 0, 0);
    }

    // 環境ソースのミップ1以降を、1つ上のミップの2x2平均で作る(ミップ0の焼き込みが済んだあとに呼ぶ)。
    // 作るミップだけをRT、読み終えたミップをSRVにして順に進める。終わると全ミップがSRV状態。
    // 引く側(t3)のSRVは全ミップを指すが、読むのは1つ上のミップだけで、それは直前にSRVへ遷移済み。
    void BuildEnvSourceMips(const rs::rhi::CommandList& commandList, std::uint64_t tableGpuHandle) {
        ID3D12GraphicsCommandList* list = commandList.Native();
        BeginPass(list, rootSigFilter.Get(), psoDownsample.Get(), tableGpuHandle);
        for (std::uint32_t mip = 1; mip < envSource.MipCount(); ++mip) {
            envSource.TransitionMip(commandList, mip - 1, HdrTargetState::ShaderResource);
            for (std::uint32_t face = 0; face < ibl::kIblCubeFaceCount; ++face) {
                const FilterConstants constants = {face, 0.0f, static_cast<float>(mip - 1)};
                DrawFace(commandList, envSource, face, mip, &constants, kFilterConstantDwords);
            }
        }
        envSource.TransitionMip(commandList, envSource.MipCount() - 1, HdrTargetState::ShaderResource);
        envSource.MarkAllMips(HdrTargetState::ShaderResource);
    }

    // 大気の空の側の焼き込み定数(太陽・月・暗順応)。暗順応の毎フレームの更新と、キューブの作り直しで共通。
    static EnvBakeConstants MakeSkyBakeConstants(const rs::core::Vec3& sun, const RenderTuning& tuning,
                                                 const IblMoon& moon) {
        EnvBakeConstants constants;
        constants.sunToSun[0] = sun.x;
        constants.sunToSun[1] = sun.y;
        constants.sunToSun[2] = sun.z;
        constants.radianceScale = tuning.skyRadianceScale;
        constants.moonRatio = moon.ratio;
        constants.moonToMoon[0] = moon.directionToMoon.x;
        constants.moonToMoon[1] = moon.directionToMoon.y;
        constants.moonToMoon[2] = moon.directionToMoon.z;
        constants.adaptStart = tuning.nightAdaptationStart;
        constants.adaptExponent = tuning.nightAdaptationExponent;
        constants.adaptMaxGain = tuning.nightAdaptationMaxGain;
        constants.adaptDayStart = tuning.dayAdaptationStart;
        constants.adaptDayExponent = tuning.dayAdaptationExponent;
        constants.adaptBoost = ToGpuFloat(
            atmosphere::NightAdaptationBoost(atmosphere::DirectionElevationDeg(sun), tuning.nightAdaptationBoost));
        return constants;
    }

    // 大気の空の上半球平均と、暗順応の倍率・照度を、今の太陽・月で作る(毎フレーム。1画素ずつの軽い処理)。
    // 倍率は画素0(今)へ描く。writeBaked が真なら、画素1(IBL焼き込み時)にも同じ値を描く(キューブを作り直すフレーム)。
    // 環境ソースの焼き込みより先に呼ぶこと(焼き込みが上半球平均と画素0を読む)。終わると両方SRV状態。
    void UpdateExposure(const rs::rhi::CommandList& commandList, std::uint64_t tableGpuHandle,
                        const EnvBakeConstants& bakeConstants, std::uint64_t moonSkyViewTable, bool writeBaked) {
        ID3D12GraphicsCommandList* list = commandList.Native();
        // 追加テーブルは使わないものを拡散キューブで代用する(引かれない。上半球平均の描画先はこの間RT状態)。
        const std::uint64_t substitute = diffuse.SrvGpuHandle();

        // 大気の空だけの上半球平均(写真の空の層の明るさを合わせる基準)。
        skyMean.Transition(commandList, HdrTargetState::RenderTarget);
        BeginPass(list, rootSigBake.Get(), psoSkyMean.Get(), tableGpuHandle);
        SetBakeExtraTables(list, substitute, substitute, substitute, moonSkyViewTable, tableGpuHandle);
        DrawFace(commandList, skyMean, 0, 0, &bakeConstants, kBakeConstantDwords);
        skyMean.Transition(commandList, HdrTargetState::ShaderResource);

        // 暗順応の倍率と照度。上半球平均(SRV状態になった skyMean)を読んで描く。
        exposure.Transition(commandList, HdrTargetState::RenderTarget);
        BeginPass(list, rootSigBake.Get(), psoAdaptation.Get(), tableGpuHandle);
        SetBakeExtraTables(list, substitute, substitute, skyMean.SrvGpuHandle(), moonSkyViewTable, tableGpuHandle);
        list->SetGraphicsRoot32BitConstants(kRootParamConstants, kBakeConstantDwords, &bakeConstants, 0);
        exposure.Bind(commandList, ExposureTarget::kCurrentPixel);
        list->DrawInstanced(3, 1, 0, 0);
        if (writeBaked) {
            exposure.Bind(commandList, ExposureTarget::kBakedPixel);
            list->DrawInstanced(3, 1, 0, 0);
        }
        exposure.Transition(commandList, HdrTargetState::ShaderResource);
    }

    // 環境ソース → 拡散 → 鏡面の順に作る(後ろの2つは環境ソースを引く)。tableGpuHandle は
    // 大気LUTの先頭のSRV(続く4枠目が環境ソース)。landscapeInput.srvGpuHandle が0以外なら、環境ソースへ
    // 標準の空の遠景を重ねる。photoSky の鏡面キューブが有効なら、地平線より上の空を写真の空の層にする。
    // moon: 月明かりの空(moonSkyViewTable は月用のSky-View LUTのSRV。常に有効なSRVを渡す)。
    void GenerateCubes(const rs::rhi::CommandList& commandList, std::uint64_t tableGpuHandle,
                       const rs::core::Vec3& sun, const RenderTuning& tuning, const IblLandscape& landscapeInput,
                       const IblPhotoSky& photoSky, const IblMoon& moon, std::uint64_t moonSkyViewTable) {
        ID3D12GraphicsCommandList* list = commandList.Native();
        const bool useLandscape = landscapeInput.srvGpuHandle != 0;
        const bool usePhoto = photoSky.specularCubeSrvGpuHandle != 0;
        const LandscapeLightingParams landscape = MakeLandscapeLightingParams(tuning);

        EnvBakeConstants bakeConstants = MakeSkyBakeConstants(sun, tuning, moon);
        bakeConstants.useLandscape = useLandscape ? 1.0f : 0.0f;
        bakeConstants.landscapeYawRad = landscape.yawRad;
        bakeConstants.landscapeBrightness = landscape.brightness;
        for (int i = 0; i < 3; ++i) bakeConstants.landscapeTint[i] = landscape.tint[i];
        bakeConstants.landscapeSaturation = landscape.saturation;
        bakeConstants.landscapeContrast = landscape.contrast;
        bakeConstants.landscapeContrastPivot = landscapeInput.contrast.pivotLuminance;
        bakeConstants.landscapeContrastScale = landscapeInput.contrast.meanScale;
        // 0=写真なし、1=地平線より上の空の層、2=全方向(既定)。シェーダの EnvBakePS と同じ番号。
        bakeConstants.usePhoto = !usePhoto ? 0.0f : (photoSky.fullSphere ? 2.0f : 1.0f);
        bakeConstants.photoSunFound = photoSky.sunFound ? 1.0f : 0.0f;
        for (int i = 0; i < 3; ++i) {
            bakeConstants.photoSkyMean[i] = photoSky.skyMean[i];
            bakeConstants.photoSunDirection[i] = photoSky.sunDirection[i];
        }
        bakeConstants.photoYawCos = photoSky.yaw.cos;
        bakeConstants.photoYawSin = photoSky.yaw.sin;

        // 上半球平均と暗順応は Update が先に済ませてある(UpdateExposure)。追加テーブルは使わないときも埋め、
        // その間SRV状態の拡散キューブで代用する(引かれない。環境ソース自身の描画先はこの間RT状態なのでSRVにできない)。
        const std::uint64_t substitute = diffuse.SrvGpuHandle();
        exposure.RecordReadback(commandList); // 開発用(RS_DEBUG_ADAPTATION=1 のときだけ)

        envSource.Transition(commandList, HdrTargetState::RenderTarget);
        BeginPass(list, rootSigBake.Get(), psoBake.Get(), tableGpuHandle);
        SetBakeExtraTables(list, useLandscape ? landscapeInput.srvGpuHandle : substitute,
                           usePhoto ? photoSky.specularCubeSrvGpuHandle : substitute, skyMean.SrvGpuHandle(),
                           moonSkyViewTable, exposure.SrvGpuHandle());
        for (std::uint32_t face = 0; face < ibl::kIblCubeFaceCount; ++face) {
            bakeConstants.face = face;
            DrawFace(commandList, envSource, face, 0, &bakeConstants, kBakeConstantDwords);
        }
        BuildEnvSourceMips(commandList, tableGpuHandle);

        diffuse.Transition(commandList, HdrTargetState::RenderTarget);
        BeginPass(list, rootSigFilter.Get(), psoDiffuse.Get(), tableGpuHandle);
        for (std::uint32_t face = 0; face < ibl::kIblCubeFaceCount; ++face) {
            const FilterConstants constants = {face, 0.0f, 0.0f};
            DrawFace(commandList, diffuse, face, 0, &constants, kFilterConstantDwords);
        }
        diffuse.Transition(commandList, HdrTargetState::ShaderResource);

        specular.Transition(commandList, HdrTargetState::RenderTarget);
        BeginPass(list, rootSigFilter.Get(), psoSpecular.Get(), tableGpuHandle);
        for (std::uint32_t mip = 0; mip < specular.MipCount(); ++mip) {
            const float roughness = ToGpuFloat(
                atmosphere::IblSpecularMipRoughness(static_cast<int>(mip), static_cast<int>(specular.MipCount())));
            for (std::uint32_t face = 0; face < ibl::kIblCubeFaceCount; ++face) {
                const FilterConstants constants = {face, roughness, 0.0f};
                DrawFace(commandList, specular, face, mip, &constants, kFilterConstantDwords);
            }
        }
        specular.Transition(commandList, HdrTargetState::ShaderResource);
    }
};

IblGenerator::IblGenerator() = default;
IblGenerator::~IblGenerator() = default;

bool IblGenerator::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                        std::uint32_t firstSrvIndex, std::uint32_t exposureSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->srvHeap = srvHeap;
    impl->firstSrvIndex = firstSrvIndex;
    impl->exposureSrvIndex = exposureSrvIndex;

    // 枠の並び: 環境ソース・拡散・鏡面・BRDF LUT・上半球平均。
    if (!impl->envSource.Init(device, srvHeap, firstSrvIndex + kEnvSourceSrvOffset, ibl::kIblEnvSourceSize,
                              ibl::kIblEnvSourceMipCount) ||
        !impl->diffuse.Init(device, srvHeap, firstSrvIndex + kDiffuseSrvOffset, ibl::kIblDiffuseSize, 1) ||
        !impl->specular.Init(device, srvHeap, firstSrvIndex + kSpecularSrvOffset, ibl::kIblSpecularSize,
                             ibl::kIblSpecularMipCount) ||
        !impl->brdfLut.Init(device, srvHeap, firstSrvIndex + kBrdfLutSrvOffset) ||
        !impl->skyMean.Init(device, srvHeap, firstSrvIndex + kSkyMeanSrvOffset, 1, 1, CubeFormat::Float32) ||
        !impl->exposure.Init(device, srvHeap, exposureSrvIndex)) {
        spdlog::error("[ibl] 資源の作成に失敗しました");
        return false;
    }

    const std::vector<UINT> bakeExtraTables(kBakeExtraTableCount, kBakeExtraTableSrvCount);
    impl->rootSigBake = CreateAtmosphereRootSignatureTables(native, kBakeSrvCount, kBakeConstantDwords,
                                                            bakeExtraTables);
    impl->rootSigFilter = CreateAtmosphereRootSignature(native, kFilterSrvCount, kFilterConstantDwords);
    if (!impl->rootSigBake || !impl->rootSigFilter) return false;

    const std::string bakeSource = BuildIblEnvBakeShader();
    const std::string filterSource = BuildIblFilterShader();
    ComPtr<ID3DBlob> vs = CompileAtmosphereShader(bakeSource, "VSMain", "vs_5_0", false);
    ComPtr<ID3DBlob> bakePs = CompileAtmosphereShader(bakeSource, "EnvBakePS", "ps_5_0", false);
    ComPtr<ID3DBlob> skyMeanPs = CompileAtmosphereShader(bakeSource, "SkyMeanPS", "ps_5_0", false);
    ComPtr<ID3DBlob> adaptationPs = CompileAtmosphereShader(bakeSource, "AdaptationPS", "ps_5_0", false);
    ComPtr<ID3DBlob> downsamplePs = CompileAtmosphereShader(filterSource, "DownsamplePS", "ps_5_0", false);
    ComPtr<ID3DBlob> diffusePs = CompileAtmosphereShader(filterSource, "DiffusePS", "ps_5_0", false);
    ComPtr<ID3DBlob> specularPs = CompileAtmosphereShader(filterSource, "SpecularPS", "ps_5_0", false);
    if (!vs || !bakePs || !skyMeanPs || !adaptationPs || !downsamplePs || !diffusePs || !specularPs) return false;

    impl->psoBake = CreateAtmosphereFullscreenPso(native, impl->rootSigBake.Get(), vs.Get(), bakePs.Get(),
                                                  kSceneHdrFormat);
    impl->psoSkyMean = CreateAtmosphereFullscreenPso(native, impl->rootSigBake.Get(), vs.Get(), skyMeanPs.Get(),
                                                     DXGI_FORMAT_R32G32B32A32_FLOAT);
    impl->psoAdaptation = CreateAtmosphereFullscreenPso(native, impl->rootSigBake.Get(), vs.Get(), adaptationPs.Get(),
                                                        DXGI_FORMAT_R32G32B32A32_FLOAT);
    impl->psoDownsample = CreateAtmosphereFullscreenPso(native, impl->rootSigFilter.Get(), vs.Get(),
                                                        downsamplePs.Get(), kSceneHdrFormat);
    impl->psoDiffuse = CreateAtmosphereFullscreenPso(native, impl->rootSigFilter.Get(), vs.Get(),
                                                     diffusePs.Get(), kSceneHdrFormat);
    impl->psoSpecular = CreateAtmosphereFullscreenPso(native, impl->rootSigFilter.Get(), vs.Get(),
                                                      specularPs.Get(), kSceneHdrFormat);
    if (!impl->psoBake || !impl->psoSkyMean || !impl->psoAdaptation || !impl->psoDownsample || !impl->psoDiffuse || !impl->psoSpecular) {
        spdlog::error("[ibl] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool IblGenerator::Ready() const { return impl_ != nullptr; }

bool IblGenerator::Update(const rs::rhi::CommandList& commandList, const AtmosphereLuts& luts,
                          const rs::core::Vec3& sunDirectionToSun, const RenderTuning& tuning,
                          const IblLandscape& landscape, const IblPhotoSky& photoSky, const IblMoon& moon) {
    if (!Ready() || !commandList.Valid() || !luts.Ready()) return false;
    Impl& impl = *impl_;
    const rs::core::Vec3 sun = sunDirectionToSun.Normalized();
    float adaptationGain = 0.0f;
    float adaptationLuminance = 0.0f;
    if (impl.exposure.PollReadback(adaptationGain, adaptationLuminance)) {
        spdlog::info("[adaptation] E={:.4e} gain={:.4g} (太陽高度 {:.2f} 度、月の比 {:.3e})", adaptationLuminance,
                     adaptationGain, std::asin(std::clamp(static_cast<double>(impl.lastSun.y), -1.0, 1.0)) * 180.0 / std::numbers::pi,
                     impl.lastMoon.ratio);
    }
    const bool hasLandscape = landscape.srvGpuHandle != 0;
    // 太陽が動かなくても、遠景の有無が前回と変わったら作り直す(遠景の焼き込みが初回より遅れたとき、
    // プラグインの空から標準の空へ戻したときに、遠景のない環境光が残らないようにする)。
    // 写真の空の層も同じ考え方で、層の有無・Envの切り替え(鏡面キューブの識別)・平均色の変化で作り直す。
    // 調整値が読み直されたときも、焼き込んだ明るさが古いまま残らないように作り直す。
    const bool needCubes = atmosphere::ShouldRegenerateSkyView(impl.hasGenerated, impl.lastSun, sun,
                                                               static_cast<double>(tuning.iblRegenerateThresholdDeg)) ||
                           (impl.hasGenerated && (&tuning != impl.lastTuning || hasLandscape != impl.lastHadLandscape ||
                                                  impl.PhotoSkyChanged(photoSky) ||
                                                  atmosphere::ShouldRegenerateIblForMoon(
                                                      impl.lastMoon.directionToMoon, impl.lastMoon.ratio,
                                                      moon.directionToMoon, moon.ratio,
                                                      static_cast<double>(tuning.iblRegenerateThresholdDeg))));

    // 環境ソースが大気LUTの3枠のすぐ後ろにあること(畳み込みが大気LUTの先頭から4枠のテーブルで
    // 環境ソースをt3として引くため)。
    const std::uint64_t lutTable = luts.SrvTableGpuHandle();
    const std::uint64_t expectedEnvHandle =
        lutTable + static_cast<std::uint64_t>(AtmosphereLuts::kSrvCount) * impl.srvHeap.Stride();
    if (impl.envSource.SrvGpuHandle() != expectedEnvHandle) {
        spdlog::error("[ibl] 環境ソースのSRV枠が大気LUTの直後にありません");
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = {impl.srvHeap.Native()};
    commandList.Native()->SetDescriptorHeaps(1, heaps);
    impl.brdfLut.Generate(commandList);
    // 暗順応は毎フレーム今の太陽・月で作り直す(キューブは太陽が閾値だけ動くまで古いままだが、倍率が階段にならないように)。
    impl.UpdateExposure(commandList, lutTable, Impl::MakeSkyBakeConstants(sun, tuning, moon),
                        luts.MoonSkyViewSrvGpuHandle(), needCubes);
    if (needCubes) {
        impl.GenerateCubes(commandList, lutTable, sun, tuning, landscape, photoSky, moon,
                           luts.MoonSkyViewSrvGpuHandle());
        impl.lastSun = sun;
        impl.lastTuning = &tuning;
        impl.lastMoon = moon;
        impl.lastHadLandscape = hasLandscape;
        impl.lastPhotoSky = photoSky;
        impl.hasGenerated = true;
        spdlog::debug("[ibl] 環境光を生成しました (太陽高度 {:.2f} 度)",
                      std::asin(std::clamp(static_cast<double>(sun.y), -1.0, 1.0)) * 180.0 / std::numbers::pi);
    }
    return true;
}

std::uint64_t IblGenerator::SrvTableGpuHandle() const { return impl_ ? impl_->envSource.SrvGpuHandle() : 0; }

std::uint64_t IblGenerator::ExposureSrvGpuHandle() const { return impl_ ? impl_->exposure.SrvGpuHandle() : 0; }

std::uint32_t IblGenerator::ExposureSrvIndex() const { return impl_ ? impl_->exposureSrvIndex : 0; }
std::uint32_t IblGenerator::DiffuseSrvIndex() const { return impl_ ? impl_->firstSrvIndex + kDiffuseSrvOffset : 0; }
std::uint32_t IblGenerator::SpecularSrvIndex() const { return impl_ ? impl_->firstSrvIndex + kSpecularSrvOffset : 0; }
std::uint32_t IblGenerator::BrdfLutSrvIndex() const { return impl_ ? impl_->firstSrvIndex + kBrdfLutSrvOffset : 0; }

} // namespace rs::render
