//
// FramePassTable.h - 1フレームの記録パスの順序・入出力・有効条件を1つの表にまとめたもの(GPUに依存しない)
//
// 記録の順は kFramePasses の並びだけが決める(App::Render が表の順に回す)。パスごとに、
//   reads        : 読む資源(これが今フレームに書かれていないと意味を持たないもの)
//   readsOptional: あれば読む資源(書かれる前に読まないよう、順序だけを縛る)
//   writes       : 書く資源
//   enabledWhen     : 走らせる条件(すべて立っているときだけ走る)
// を持つ。TAA・モーションブラー・フォグなど「前のパスの出力を読むパス」は、読む資源を書くパスの
// 条件を自分の条件にも含める(速度パスはプリパスが走るときだけ、TAA は速度パスが走るときだけ)。
//
// GpuContext の BeginFrame(バックバッファのバリア・クリア)と EndFrame(Present)は表の外。
// GPU計測の区間は段をまたぐ(不透明は RecordMeshFrame の中で閉じる)ので、表では扱わない。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace rs::app {

// パスが読み書きする資源。
enum class FrameResource : std::uint8_t {
    AtmosphereLut,   // 大気LUT(透過率・多重散乱・Sky-View)。フレームをまたいで残る
    AerialLut,       // 大気遠近のLUT。フレームをまたいで残る
    IblCubes,        // IBL(環境ソース・拡散・鏡面・露出)。フレームをまたいで残る
    SceneHdr,        // HDRのシーン色
    MainDepth,       // 本編の深度バッファ
    FrameConstants,  // 今フレームのメッシュ定数・視点定数(CPU側で組み立てる)
    PrepassDepth,    // 深度プリパスの出力
    Velocity,        // 速度バッファ
    AoResult,        // アンビエントオクルージョンの結果
    ShadowMap,       // シャドウマップ
    FogGrid,         // ボリュメトリックフォグの格子
    TaaResolved,     // TAA の解決後のシーン色
    DepthOfFieldOut, // 被写界深度後のシーン色
    MotionBlurOut,   // モーションブラー後のシーン色
    BackBuffer,      // バックバッファ(BeginFrame がクリアして渡す)
    Count,
};

using FrameResourceMask = std::uint32_t;

constexpr FrameResourceMask ResourceBit(FrameResource resource) {
    return FrameResourceMask{1} << static_cast<unsigned>(resource);
}

constexpr FrameResourceMask ResourceMaskOf(std::initializer_list<FrameResource> resources) {
    FrameResourceMask mask = 0;
    for (FrameResource resource : resources) mask |= ResourceBit(resource);
    return mask;
}

// フレームをまたいで残る資源(今フレームに書かれていなくても読んでよい)。
inline constexpr FrameResourceMask kPersistentResources =
    ResourceMaskOf({FrameResource::AtmosphereLut, FrameResource::AerialLut, FrameResource::IblCubes,
                    FrameResource::BackBuffer});

// パスの有効条件。
enum class FramePassFlag : std::uint8_t {
    HdrReady,            // HDRシーンターゲットが使える
    EnvironmentUpdate,   // 大気LUT・IBLを更新するフレーム
    EnvironmentRecorded, // 環境のパスが何か記録した(RTV・ビューポートを書き換えた)
    DrawSky,             // 空を描く
    MeshReady,           // 描くメッシュがあり、読込ジョブ中でない
    Prepass,             // 深度プリパスを描く
    Velocity,            // 速度バッファを描く
    AmbientOcclusion,    // アンビエントオクルージョンを掛ける
    Shadow,              // 影を描く
    VolumetricFog,       // ボリュメトリックフォグの格子を作る
    TemporalAa,          // TAA を解決する
    DepthOfField,        // 被写界深度を掛ける
    MotionBlur,          // モーションブラーを掛ける
    LensFlare,           // レンズフレアを足す
    LuminanceMeter,      // ISO オートのために、シーンの明るさを測る
    RmlUi,               // RmlUi を描く
    Count,
};

using FramePassFlagMask = std::uint32_t;

constexpr FramePassFlagMask FlagBit(FramePassFlag flag) { return FramePassFlagMask{1} << static_cast<unsigned>(flag); }

constexpr FramePassFlagMask FlagMaskOf(std::initializer_list<FramePassFlag> flags) {
    FramePassFlagMask mask = 0;
    for (FramePassFlag flag : flags) mask |= FlagBit(flag);
    return mask;
}

// 今フレームの有効条件の状態。
struct FramePassFlags {
    FramePassFlagMask bits = 0;

    constexpr void Set(FramePassFlag flag, bool value) {
        if (value) {
            bits |= FlagBit(flag);
        } else {
            bits &= ~FlagBit(flag);
        }
    }
    constexpr bool Test(FramePassFlag flag) const { return (bits & FlagBit(flag)) != 0; }
};

enum class FramePassId : std::uint8_t {
    AtmosphereLut,
    AerialPerspective,
    Ibl,
    EnvironmentViewportReset,
    SceneBegin,
    FramePrepare,
    Sky,
    Prepass,
    Velocity,
    AmbientOcclusion,
    MainTargetReset,
    Shadow,
    VolumetricFog,
    ComputeSelfTest,
    MeshScene,
    TemporalAa,
    DepthOfField,
    MotionBlur,
    LensFlare,
    LuminanceMeter,
    PostProcess,
    SceneCapture,
    Overlay2D,
    RmlUi,
    Imgui,
    Count,
};

struct FramePassDesc {
    FramePassId id;
    const char* name;
    FrameResourceMask reads;
    FrameResourceMask readsOptional;
    FrameResourceMask writes;
    FramePassFlagMask enabledWhen;
    // CPU側の準備だけで、コマンドは記録しない。
    bool cpuOnly = false;
};

namespace frame_pass_detail {
using R = FrameResource;
using F = FramePassFlag;
constexpr FrameResourceMask Res(std::initializer_list<FrameResource> resources) { return ResourceMaskOf(resources); }
constexpr FramePassFlagMask Req(std::initializer_list<FramePassFlag> flags) { return FlagMaskOf(flags); }
} // namespace frame_pass_detail

// 記録の順。
inline constexpr std::array<FramePassDesc, static_cast<std::size_t>(FramePassId::Count)> kFramePasses = []() {
    using namespace frame_pass_detail;
    return std::array<FramePassDesc, static_cast<std::size_t>(FramePassId::Count)>{{
        // 環境(大気LUT → 大気遠近 → IBL)。RTV・ビューポートを書き換えるので、シーンの開始より前に済ませる。
        {FramePassId::AtmosphereLut, "AtmosphereLut", Res({}), Res({}), Res({R::AtmosphereLut}),
         Req({F::HdrReady, F::EnvironmentUpdate})},
        {FramePassId::AerialPerspective, "AerialPerspective", Res({R::AtmosphereLut}), Res({}), Res({R::AerialLut}),
         Req({F::HdrReady, F::EnvironmentUpdate})},
        {FramePassId::Ibl, "Ibl", Res({R::AtmosphereLut}), Res({}), Res({R::IblCubes}),
         Req({F::HdrReady, F::EnvironmentUpdate})},
        {FramePassId::EnvironmentViewportReset, "EnvironmentViewportReset", Res({}), Res({}), Res({}),
         Req({F::HdrReady, F::EnvironmentUpdate, F::EnvironmentRecorded})},
        // シーンの描画先(HDR+主深度)を張る。
        {FramePassId::SceneBegin, "SceneBegin", Res({}), Res({}), Res({R::SceneHdr, R::MainDepth}), Req({F::HdrReady})},
        // 環境を組み立て、カリング・定数・影の準備をして、以降の有効条件を決める(CPUのみ)。
        {FramePassId::FramePrepare, "FramePrepare", Res({}), Res({R::AtmosphereLut, R::IblCubes, R::AerialLut}),
         Res({R::FrameConstants}), Req({F::HdrReady}), true},
        {FramePassId::Sky, "Sky", Res({R::AtmosphereLut}), Res({R::IblCubes}), Res({R::SceneHdr}),
         Req({F::HdrReady, F::DrawSky})},
        // 主視点の深度プリパス → 速度(プリパスの深度に沿って書く)→ AO(プリパスの深度を読む)。
        {FramePassId::Prepass, "Prepass", Res({R::FrameConstants}), Res({}), Res({R::PrepassDepth}),
         Req({F::HdrReady, F::MeshReady, F::Prepass})},
        {FramePassId::Velocity, "Velocity", Res({R::FrameConstants, R::PrepassDepth}), Res({}), Res({R::Velocity}),
         Req({F::HdrReady, F::MeshReady, F::Prepass, F::Velocity})},
        {FramePassId::AmbientOcclusion, "AmbientOcclusion", Res({R::PrepassDepth}), Res({}), Res({R::AoResult}),
         Req({F::HdrReady, F::MeshReady, F::Prepass, F::AmbientOcclusion})},
        // プリパス系が出力先とビューポートを書き換えたので、本描画の分へ戻す。
        {FramePassId::MainTargetReset, "MainTargetReset", Res({}), Res({}), Res({}),
         Req({F::HdrReady, F::MeshReady, F::Prepass})},
        {FramePassId::Shadow, "Shadow", Res({R::FrameConstants}), Res({}), Res({R::ShadowMap}),
         Req({F::HdrReady, F::MeshReady, F::Shadow})},
        // フォグの格子は、影のマップを引いて作る。
        {FramePassId::VolumetricFog, "VolumetricFog", Res({R::FrameConstants, R::ShadowMap}),
         Res({R::IblCubes, R::AtmosphereLut}), Res({R::FogGrid}),
         Req({F::HdrReady, F::MeshReady, F::Shadow, F::VolumetricFog})},
        {FramePassId::ComputeSelfTest, "ComputeSelfTest", Res({}), Res({}), Res({}), Req({F::HdrReady, F::MeshReady})},
        // 不透明・半透明(WBOIT)・後段不透明・選択ハイライト。
        {FramePassId::MeshScene, "MeshScene", Res({R::FrameConstants}),
         Res({R::AerialLut, R::IblCubes, R::AoResult, R::ShadowMap, R::FogGrid}), Res({R::SceneHdr, R::MainDepth}),
         Req({F::HdrReady, F::MeshReady})},
        // TAA・モーションブラーは、速度とプリパスの深度が今フレームに書かれたときだけ走る。
        {FramePassId::TemporalAa, "TemporalAa", Res({R::SceneHdr, R::Velocity, R::PrepassDepth}), Res({}),
         Res({R::TaaResolved}), Req({F::HdrReady, F::MeshReady, F::Prepass, F::Velocity, F::TemporalAa})},
        // 被写界深度は、本編の深度だけを読む(速度・プリパスは要らない)。TAA の解決の後・モーションブラーの前。
        {FramePassId::DepthOfField, "DepthOfField", Res({R::SceneHdr, R::MainDepth}), Res({R::TaaResolved}),
         Res({R::DepthOfFieldOut}), Req({F::HdrReady, F::MeshReady, F::DepthOfField})},
        {FramePassId::MotionBlur, "MotionBlur", Res({R::SceneHdr, R::Velocity, R::PrepassDepth}),
         Res({R::TaaResolved, R::DepthOfFieldOut}), Res({R::MotionBlurOut}),
         Req({F::HdrReady, F::MeshReady, F::Prepass, F::Velocity, F::MotionBlur})},
        {FramePassId::LensFlare, "LensFlare", Res({R::SceneHdr}), Res({R::TaaResolved, R::DepthOfFieldOut, R::MotionBlurOut, R::MainDepth}),
         Res({R::SceneHdr}), Req({F::HdrReady, F::LensFlare})},
        // ISO オートの測光。撮影カメラの露出を掛ける前のシーン(レンズフレアまで足したあと)を測る。何も書かない。
        {FramePassId::LuminanceMeter, "LuminanceMeter", Res({R::SceneHdr}),
         Res({R::TaaResolved, R::DepthOfFieldOut, R::MotionBlurOut}), Res({}), Req({F::HdrReady, F::LuminanceMeter})},
        // ブルーム → トーンマップ → FXAA。バックバッファへ書く。
        {FramePassId::PostProcess, "PostProcess", Res({R::SceneHdr}), Res({R::TaaResolved, R::DepthOfFieldOut, R::MotionBlurOut}),
         Res({R::BackBuffer}), Req({F::HdrReady})},
        {FramePassId::SceneCapture, "SceneCapture", Res({R::BackBuffer}), Res({}), Res({}), Req({})},
        {FramePassId::Overlay2D, "Overlay2D", Res({}), Res({R::MainDepth}), Res({R::BackBuffer}), Req({})},
        {FramePassId::RmlUi, "RmlUi", Res({}), Res({}), Res({R::BackBuffer}), Req({F::RmlUi})},
        {FramePassId::Imgui, "Imgui", Res({}), Res({}), Res({R::BackBuffer}), Req({})},
    }};
}();

// 条件がすべて立っているか。
constexpr bool PassEnabled(const FramePassDesc& pass, FramePassFlags flags) {
    return (flags.bits & pass.enabledWhen) == pass.enabledWhen;
}

} // namespace rs::app
