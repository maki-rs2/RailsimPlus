// MeshPipelineClassify.cpp - MeshPipeline::ClassifySubmeshTransparency(材質とテクスチャから submesh の描画分類・影を落とすかを決める)。
#include "MeshPipeline.h"
#include "../../platform/EnvironmentVariable.h"
#include "SubmeshAlphaClassify.h"

#include <algorithm>
#include <cstdlib>

#include <spdlog/spdlog.h>

namespace rs::render {

void MeshPipeline::ClassifySubmeshTransparency() {
    // 分類根拠のダンプ可否。submesh毎にgetenvを
    // 引くとロード時間に影響するため、ループ外で1回だけ読む。
    const bool dumpClass = rs::platform::ReadEnvironmentVariable("RS_DUMP_ALPHA_CLASS") != nullptr;
    const bool kBuildMode = AlphaZeroKBuildEnabled();
    // 半透明のtexelが多い穴あき材質は、同じ index 範囲の半透明側 submesh を後から足す。
    std::vector<SubMesh> semiParts;
    // 半透明フラグをロード時に確定する。
    for (SubMesh& sm : geometry_.Submeshes()) {
        const Material* material = materialSet_.MaterialAt(sm.materialIndex);
        const GpuTexture* texture = materialSet_.TextureAt(sm.materialIndex);
        bool matAlpha = material && material->diffuse[3] < 1.0f;
        // ChangeAlpha 対象は base alpha=1.0(不透明)でも強制的にブレンド対象へ
        // 含める。不透明パスにはアルファブレンドが無いため、そうしないと
        // 実行時のアルファ変更が画面に一切反映されない
        // (たとえば、base alpha=1.0の車両の室内灯)。
        bool alphaOverride = material && material->hasAlphaOverride;
        // Transparent 指定マテリアル(RailSim II の shadow-inhibitor)。
        // RailSim II での意味は「他の全ポリゴンより後に描画する」のみで、深度書込
        // 無効化は含意しない(pi_sym_shadow_inhibitor.html)。実アルファを伴う
        // 場合のみブレンドパス(深度書込OFF)へ、伴わない場合は不透明パスと同じ
        // 深度書込ONのまま描画順だけ最後に回す(lateOpaque)。実アルファの無い
        // 屋根系マテリアルにTransparentが使われることがあり、ブレンドパスへ混ぜると
        // 深度非書込のため車内ジオメトリを正しく遮蔽できず、突き破って見える。
        bool forceTransparent = material && material->forceTransparent;
        // AlphaZeroTest 指定マテリアルはテクスチャの中間 alpha を無視して
        // 二値抜き(不透明パス+アルファテスト)に固定する(RailSim II 互換)。
        // RailSim II -k-build 方式(既定。RS_ALPHAZERO_KBUILD=0で無効)なら切り抜きにせず、描画側でフィルタ・ミップを切る。
        bool forceCut = material && AlphaZeroForcesCut(material->forceAlphaTest, kBuildMode);
        // 実行時にテクスチャが差し替わりうる材質(ChangeTexture/画像切替アニメの
        // 対象)。ベースが不透明でも透明テクスチャ(non2.png等)へ切り替わりうるため、
        // 半透明でない限りカットアウトへ昇格させ、透明フレームを clip で捨てる。
        bool swappable = material && material->swappableTexture;
        bool texValid = texture && texture->valid;
        // RailSim II(lib/mesh.cpp CMesh::RenderCustom + graphic.cpp InitRenderState)は
        // 全材質を blend ON かつ Z-write ON で1パス描画し、テクスチャ alpha は
        // per-texel でブレンドに使う(255=不透明/中間=半透明)。当エンジンも不透明/
        // cutout パスをブレンド有効(深度書込ON、mesh_pipeline.cpp CreatePipeline)で
        // 描くため、「中間 alpha だから深度書込OFFの半透明パスへ」という分類はしない。
        // - 真の半透明(深度書込OFF・ソート/WBOIT)へ回すのは材質色 Diffuse.a<1 か
        //   ChangeAlpha のみ(RailSim II の Diffuse.a 駆動に相当)。
        // - テクスチャに alpha==0 texel(穴/透明背景)がある材質だけ cutout(alpha
        //   test で穴をクリップ)。中間 alpha しか無い不透明テクスチャ(車体の陰影
        //   デカール等)は穴が無いので cutout 不要で、不透明パスのブレンドで濃淡が
        //   per-texel に出る。
        bool texHole = texValid && texture->hasZeroAlpha;
        // AlphaZeroTest(forceCut)が明示指定された材質は、ChangeAlpha対象であっても
        // カットアウトとして扱う。不透明/cutout用PSOはブレンド有効
        // (CreateBasePipelineDesc、RailSim II のblend ON/Z-write ON準拠)なので、
        // cutout経路でも実行時のアルファ変更はそのまま反映される。alphaOverrideを
        // 理由に半透明パスへ送ると、作者がカットアウトと明示した材質(葉や柵の
        // 抜きテクスチャ)が深度書込OFFのブレンド経路へ奪われ、全体が半透明に
        // 見えるうえWBOIT無効時には描画されなくなる。
        // 材質色Diffuse.a<1の材質は、テクスチャに穴(alpha==0のtexel)があっても切り抜かない。
        // 穴の周りだけ切り抜くと、窓ガラスのように外周だけ透明で面が半透明のテクスチャが、
        // 奥の物(座席など)の形に切り抜かれて見える。
        // 頂点色のアルファが1未満の頂点を含む submesh(断面の透ける面)も、材質色のアルファと同じく実アルファとして扱う。
        bool matAlphaReal = matAlpha || sm.vertexAlpha;
        // alphaOverride(ChangeAlpha)だけで見せ消ちする材質(地の色Diffuse.a=1.0、
        // AlphaZeroTest非指定)。
        // AlphaZeroTestを宣言した材質で穴のあるテクスチャのものは、RailSim II -k-build 方式で切り抜きにしなくても
        // 作者が抜きテクスチャと明示したものなので、1パスのブレンドへ送らず切り抜きで描く
        // (葉や木が半透明に見えるのを防ぐ)。
        const bool declaredCutWithHole = material && material->forceAlphaTest && texHole;
        bool changeAlphaOnly = alphaOverride && !forceCut && !matAlpha && !declaredCutWithHole;
        // matAlphaReal(材質色Diffuse.a<1由来の実アルファ)と、穴のあるchangeAlphaOnlyは、
        // クリップなし・深度書込ON・ブレンド有効の1パス(Blended)で、submesh順
        // (=Diffuse.aの降順)に描く。RailSim II の描き方と同じで、窓ガラスは先に描いた
        // ブラインドや網の上にブレンドされる。穴のないchangeAlphaOnlyは不透明系の2段描画
        // (深度専用パス→カラーパス)で、消灯中は深度も色も残さない。
        // Transparent指定の材質だけは、実アルファがあれば半透明パス(WBOIT)、なければ遅延不透明へ送る。
        const AlphaClassInputs alphaInputs = {matAlpha, alphaOverride, forceTransparent, forceCut,
                                              swappable, texValid, texHole, texValid && texture->hasAlpha,
                                              matAlphaReal, changeAlphaOnly};
        sm.alphaClass = ClassifyAlphaClass(alphaInputs);
        sm.castShadow = material ? material->castShadow : true;
        // 穴あきテクスチャ、AlphaZeroTest指定、または実行時差し替え候補(透明フレームへ
        // 切り替わりうる)を持つ材質を cutout(深度書込ON+穴クリップ+共面デカール用に
        // GREATER_EQUAL)にする。差し替え候補が不透明のみなら clip は発火せず無害。
        // Blended(クリップなしの1パス)と半透明パスの材質は、穴を捨てずにテクスチャalphaで混ぜる。
        sm.alphaTestClip = (texHole || forceCut || swappable) && !sm.IsBlended() && !sm.IsBlendedInPlace();
        // 穴あきテクスチャの半透明のtexel(窓ガラスの色など)は、深度を書かずに全不透明の後でブレンドする。
        // 深度を書くと、後から描く窓越しの景色が深度で落ちる。不透明側は上限以上のtexelだけを残す。
        sm.alphaSplit = AlphaSplit::None;
        if (texHole && NeedsSemiAlphaSplit(sm.alphaClass, forceCut, swappable, texture->semiAlphaFraction)) {
            sm.alphaSplit = AlphaSplit::OpaquePart;
            SubMesh semi = sm;
            semi.alphaClass = SubmeshAlphaClass::Blended;
            semi.alphaSplit = AlphaSplit::SemiPart;
            semi.castShadow = false;
            semiParts.push_back(semi);
        }
        // lateOpaque: Transparent指定(forceTransparent)だが実アルファを持たない
        // 材質のみ(不透明パスと同じ深度書込ONのまま描画順だけ最後に回す)。
        // Transparentバケツと排他にする必要がある。
        // 両方trueのままだとRecordInstancedMultiPartの
        // LateOpaqueループがTransparentを除外しないため2回描画され、UploadMesh側の
        // 容量見積り(Transparentを先に除外してtwoPassSubmeshCountを数える)と
        // ズレてcbCapacityが枠切れになる。
        // 半透明バケツへ入らなかった材質のうち、テクスチャに中間alphaを持つもの。
        // これらは2段描画(深度専用パス→カラーパス)の対象になる。中間alphaの
        // 領域だけが深度を書かなくなり、その奥のジオメトリが後から描かれても
        // 深度で落ちない。alphaが閾値以上の領域は深度を書くため、
        // 大半のテクスチャ(alphaが全域で閾値以上)に対して実質no-op。

        // 全submeshの最終分類を無条件で出力する(バケツで絞ると「そもそもどの
        // バケツに落ちたか」が分からないため、絞り込み無しでダンプする)。
        if (dumpClass && material) {
            const Material& mm = *material;
            const bool legacyOpaqueHasAlphaTex =
                !sm.IsLateOpaque() && !sm.IsTransparent() && texValid && texture->hasAlpha;
            spdlog::info("[alphaclass] mat={} diffuseA={:.3f} matAlpha={} texHole={} "
                         "hasAlpha={} swappable={} forceCut={} alphaOverride={} "
                         "-> transparent={} cutout={} lateOpaque={} opaqueHasAlphaTex={} "
                         "tex='{}' class={}",
                         sm.materialIndex, mm.diffuse[3], matAlpha, texHole,
                         texValid && texture->hasAlpha, swappable, forceCut, alphaOverride,
                         sm.IsTransparent(), sm.alphaTestClip && !sm.IsTransparent(), sm.IsLateOpaque(),
                         legacyOpaqueHasAlphaTex, mm.texturePath, SubmeshAlphaClassName(sm.alphaClass));
        }
    }
    geometry_.Submeshes().insert(geometry_.Submeshes().end(), semiParts.begin(), semiParts.end());
    // 描画順は材質色のアルファの降順(同じ値は元の順)。RailSim II は材質を読込時にこの順へ並べ、
    // 全材質を1パスで描く。重ね貼りの層(LEDの網など、アルファの低い材質を一番手前に置くもの)は、
    // 後から描かれてはじめて下の層の上に乗る。材質番号の順のままだと、下の層が後から上書きする。
    std::stable_sort(geometry_.Submeshes().begin(), geometry_.Submeshes().end(),
                     [this](const SubMesh& a, const SubMesh& b) {
                         const Material* materialA = materialSet_.MaterialAt(a.materialIndex);
                         const Material* materialB = materialSet_.MaterialAt(b.materialIndex);
                         const float alphaA = materialA ? materialA->diffuse[3] : 1.0f;
                         const float alphaB = materialB ? materialB->diffuse[3] : 1.0f;
                         return alphaA > alphaB;
                     });
}

} // namespace rs::render
