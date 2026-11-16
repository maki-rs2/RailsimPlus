//
// ShadowPass.h - シャドウマップへ影を落とすものを深度だけで描くパスと、影定数(b1)のフレームごとのバッファ
//
// 描き方は通常の描画と同じ MeshPipeline::RecordShadowInstancedMultiPart を呼ぶ。影専用の別実装は持たない。
// 影を落とすものの一覧と、パスを描くか・どのカメラで描くかの判断は呼び出し側(アプリ層)が持つ。
//
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "../../core/Math.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "ShadowConstants.h"
#include "ShadowMap.h"

namespace rs::render {

class MeshPipeline;
struct MeshConstants;

// シャドウパスへ描かせるもの1つ(メッシュ1体ぶんの MeshPipeline と、そのパーツ数)。
struct ShadowCaster {
    MeshPipeline* pipeline = nullptr;
    std::uint32_t partCount = 0;
    bool includeThin = true; // 細い部品(SubMesh::thinShadow)も描くか
};

// casters の後に呼ぶ追加の記録。ライト用の定数(視点の定数をカスケードのものにした MeshConstants)を渡す。
// MeshPipeline を直接持たない描画物(インスタンスバッチ等)を、アプリ層の型に依存せず描かせるための口。
using ShadowExtraRecord = std::function<void(const MeshConstants& lightConstants)>;

class ShadowPass {
public:
    ShadowPass();
    ~ShadowPass();
    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    // 影定数のフレームごとのバッファを作る。
    bool Init(const rs::rhi::Device& device);
    bool Ready() const;

    // frameIndex 番目の影定数バッファへ constants を書き、その GPU アドレスを返す(失敗時は0)。
    // 返したアドレスを MeshConstants::shadowConstantsAddress へ入れると、全ての描画経路が b1 に張る。
    std::uint64_t WriteConstants(std::uint32_t frameIndex, const ShadowConstants& constants);

    // slice 枚目へ casters を深度だけで描く。ShadowMap を DepthWrite にして描き、描き終えたら
    // ShaderResource へ戻す。出力先(RTV・DSV)とビューポートは書き換えるので、呼び出し側が元へ戻す。
    // lightBase は描画の共通定数。viewConstantsAddress は、そのカスケードのワールド→ライトのクリップ座標を
    // 入れた視点の定数を指すこと(呼び出し側がカスケードごとに書く)。
    void Record(const rs::rhi::CommandList& commandList, ShadowMap& shadowMap, std::uint32_t slice,
                const MeshConstants& lightBase, const std::vector<ShadowCaster>& casters, const ShadowExtraRecord& extraRecord,
                std::uint32_t frameIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
