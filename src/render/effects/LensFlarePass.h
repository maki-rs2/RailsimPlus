//
// LensFlarePass.h - レンズフレアの要素と白飛びを、リニアHDRのシーンターゲットへ足すパス
//
// RailSim II の CLensFlare::Render / CWhiteout::Render に当たる。トーンマップの前(リニアHDR)で、
// 加算(SrcAlpha×Src + One×Dest)で要素を描き、白飛びはアルファブレンドで塗る。
// 定義の色はsRGBの値なので、シェーダでリニアへ変換してから HDR の倍率を掛ける。
// 要素は深度テストをしない。遮蔽は光源の位置の周りの深度を読み、遮られていない割合を要素全体に掛ける。
// 入力は光源の一覧と、光源を指す要素の一覧(太陽も灯具も同じ形)。
// SRVは共有ヒープの連続 kSrvCount 枠(先頭がシーンの深度、続いてテクスチャ kMaxFlareTextures 枠)。
// 深度のSRVは呼び出し側が先頭の枠へ書く(MeshPipeline::WriteDepthSrv)。
//
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "../../core/Math.h"
#include "../resource/TextureLoader.h"
#include "../rhi/CommandList.h"
#include "../rhi/CommandQueue.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "LensFlareMath.h"

namespace rs::render {

// 1フレームに描く光源・要素の上限。超えた分は捨てる(警告は1回だけ)。
constexpr std::uint32_t kMaxFlareLights = 16;
constexpr std::uint32_t kMaxFlareInstances = 128;
// テクスチャ要素に使える画像の枚数(スロット番号は 0〜kMaxFlareTextures-1)。
// シェーダの gTex0〜gTex(N-1) の数と合わせる(LensFlareShader.h の SampleSlot)。
constexpr std::uint32_t kMaxFlareTextures = 8;

// 光源の可視度を求める円盤の半径[ピクセル]。フレアの中心の要素の大きさに合わせた調整値。
constexpr float kFlareVisibilitySampleRadiusPx = 12.0f;
// 無限遠でない光源の深度の許容幅。光源をカメラ側へ、カメラまでの距離のこの割合だけ寄せた点と比べる
// (灯具が自分のメッシュに遮られないように)。
constexpr float kFlareDepthToleranceRel = 0.02f;

struct FlareLight {
    rs::core::Vec3 position;
    // 太陽のように無限遠とみなすか。真なら、シーンの深度が遠面でない点を遮蔽とみなす。
    bool atInfinity = false;
};

// 描く要素1件。instance.element は呼び出しの間だけ生きていればよい。
struct FlareDraw {
    FlareInstance instance;
    std::uint32_t lightIndex = 0;  // LensFlareFrame::lights の添字
};

// 白飛び。color のアルファは、Whiteout の色のアルファに強さ(ComputeWhiteoutAlpha)を掛けた値。
struct WhiteoutDraw {
    bool enabled = false;
    std::uint32_t lightIndex = 0;
    FlareColor color;
};

struct LensFlareFrame {
    rs::core::Mat4 view = rs::core::Mat4::Identity();
    rs::core::Mat4 viewProj = rs::core::Mat4::Identity();
    rs::core::Vec3 cameraPos;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameIndex = 0;  // 実行中のフレームと重ならない側のバッファを選ぶ
    std::vector<FlareLight> lights;
    std::vector<FlareDraw> draws;
    WhiteoutDraw whiteout;
    // リニアHDRへ足すときの倍率(RenderTuning の flareHdrIntensity・whiteoutHdrLevel)。
    float flareHdrIntensity = 1.0f;
    float whiteoutHdrLevel = 1.0f;
    // 太陽に大気の透過率LUT(共有ヒープの絶対番号 transmittanceSrvIndex)の色と強さを掛けるか。
    bool sunAtmosphere = false;
    std::uint32_t transmittanceSrvIndex = 0;

    bool HasWork() const { return !draws.empty() || whiteout.enabled; }
};

class LensFlarePass {
public:
    static constexpr std::uint32_t kSrvCount = 1 + kMaxFlareTextures;
    // フレームごとの書き込み先の数。スワップチェーンのバッファ数と同じにする。
    static constexpr std::uint32_t kRingSize = 2;

    LensFlarePass();
    ~LensFlarePass();
    LensFlarePass(const LensFlarePass&) = delete;
    LensFlarePass& operator=(const LensFlarePass&) = delete;

    // device・srvHeap・queue は使い終わるまで生きていること。firstSrvIndex から kSrvCount 枠を使う。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, const rs::rhi::CommandQueue& queue,
              std::uint32_t firstSrvIndex);
    bool Ready() const;

    // 深度のSRVを置く枠の番号(WriteDepthSrv に渡す)。
    std::uint32_t DepthSrvIndex() const;

    // テクスチャ要素の画像を1枚、スロットへ上げる(完了まで待つ)。まだ画像の無いスロットにだけ使う
    // (割り当て済みの台帳から新しく渡されたスロット)。SRVの範囲は実行中のフレームが束縛していても、
    // 記述子テーブルは VOLATILE なので、別のスロットの書き込みは待たなくてよい。
    // スロットが範囲外・画像が読めていない(ok でない)・アップロードに失敗なら false。
    bool UploadTexture(std::uint32_t slot, const ImageData& image);
    // スロットに画像が上がっているか。
    bool HasTexture(std::uint32_t slot) const;

    // rtv(リニアHDRのシーンターゲット。R16G16B16A16_FLOAT)へ記録する。深度は読み取り用の状態になっていること(呼び出し側が遷移させる)。
    // 描くものが無ければ何もしない。ビューポート・シザーは frame の幅・高さの全面へ設定する。
    void Record(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                const LensFlareFrame& frame) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
