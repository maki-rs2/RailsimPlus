//
// ThumbnailRenderer.h - メッシュ1体を本編と同じマテリアル・シェーダで描き、RGBAを返す
//
// プラグインに依存せず、焼き込み済みの
// メッシュ・カメラ・光だけを受け取る。専用のコマンドキュー・RT・SRVヒープ・MeshPipeline・
// TextureManager を持つので、メインの描画とは独立してワーカースレッドから使える
// (Init・Render・破棄は同じスレッドで行うこと)。DX12の資源は実装側(.cpp)に閉じ込める。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../resource/TextureLoader.h"
#include "../rhi/Device.h"

namespace rs::render {

struct Mesh;
class MeshPipelineSharedState;
class TonemapPass;

// 1枚の撮影に渡すもの。メッシュはパーツ行列を焼き込んで全頂点を partIndex=0 にした
// ものを渡す(描くときのパーツ行列は単位行列1つ)。
struct ThumbnailScene {
    const Mesh* mesh = nullptr;
    float viewProj[16] = {};   // view*proj(行ベクトル規約、本編の ViewConstants::viewProj と同じ)
    float cameraPos[3] = {};
    // 光は ThumbnailLighting.h の太陽に固定。カメラは +Z 側から -Z 向きに見る前提で、見たい向きはモデルを回して合わせる。
};

struct ThumbnailRendererConfig {
    std::uint32_t width = 256;
    std::uint32_t height = 192;
    // SRVヒープの1レンジの内訳。本編のシーン常駐スロットと同じ配分
    // [テクスチャ maxTextures 個][半透明用バッファ transBufSrvCount 個]
    // [パーツ行列 partWorldSrvCount 個][個体差上書き]で、先頭0から並べる。
    // 値は app 層の定数(App.h)から渡す(render層は app 層の定数を参照しない)。
    std::uint32_t srvStride = 0;
    std::uint32_t maxTextures = 0;
    std::uint32_t transBufSrvCount = 0;
    std::uint32_t partWorldSrvCount = 0;
    std::uint32_t maxParts = 0;
};

class ThumbnailRenderer {
public:
    ThumbnailRenderer();
    ~ThumbnailRenderer();
    ThumbnailRenderer(const ThumbnailRenderer&) = delete;
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete;

    // shared・tonemap は撮影の間ずっと生きていること(本編が持つものを読み取り専用で共用する。
    // shared のPSOはHDR形式(kSceneHdrFormat)で作られたもの)。シーンは撮影解像度のHDRターゲットへ
    // 描き、tonemap(本編と同じPSO・モード・露出)でR8G8B8A8_UNORM_SRGBのRTへ変換してから読み戻す。
    // 失敗したら false(以後の Render も false)。
    bool Init(const rs::rhi::Device& device, const MeshPipelineSharedState& shared, const TonemapPass& tonemap,
              const ThumbnailRendererConfig& config);

    // out.ok=true のとき out は R8G8B8A8(sRGBのバイトのまま、width*height*4)。
    // メッシュが空・GPUの失敗・デバイスの削除は false(デバイスの削除後は何もしない)。
    bool Render(const ThumbnailScene& scene, ImageData& out);

    // メッシュを描かず、背景のグラデーションだけの画像を返す(out の形式は Render と同じ)。
    // GPUの失敗・デバイスの削除は false。
    bool RenderBackground(ImageData& out);

private:
    // scene が null なら背景だけ。Render と RenderBackground の共通処理。
    bool Capture(const ThumbnailScene* scene, ImageData& out);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
