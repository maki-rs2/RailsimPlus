//
// mesh.h - 内部メッシュ型
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rs::render {

struct MeshVertex {
    float pos[3] = {0, 0, 0};
    float normal[3] = {0, 1, 0};
    float uv[2] = {0, 0};
    // パーツ単位ワールド行列（gPartWorld）を引くための添字。単一メッシュ表示
    // では常に 0（gPartWorld[0]=恒等行列）で動く。
    uint32_t partIndex = 0;
    // 頂点色(sRGB の RGBA)。断面のメッシュだけが白以外を入れる。
    uint8_t color[4] = {255, 255, 255, 255};
};

struct Material {
    float       diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    float       ambient[3] = {0.8f, 0.8f, 0.8f};
    float       specular[3] = {0, 0, 0};
    float       power = 0.0f;
    std::string texturePath; // UTF-8。解決済みの絶対 or 相対パス（空=テクスチャ無し）
    std::string name;        // 参考: 元テクスチャファイル名など
    float       emissive[3] = {0, 0, 0};

    // --- RailSim II -k-build PBR 拡張 ---
    // ChangeTexture = id, base, normal, pbr; で解決済みの絶対パスが入る。
    // pbr は ORM パック (R=AO, G=Roughness, B=Metallic)。空なら未指定。
    std::string normalPath;  // 法線マップ（タンジェント空間）
    std::string ormPath;     // ORM パック
    // BumpMap / EnvMap = id列; で指定されたマテリアル(法線/ORMはこのマテリアルにだけ使われる)。
    // 発動条件つきの指定は実行時に決まる。読込直後は条件のない指定だけを反映した値。
    bool        useBumpMap = false;
    bool        useEnvMap = false;

    // SetAnimation(UV型アニメ): uv' = uv*[[a,c],[b,d]] + [tx,ty]。
    // 既定は恒等変換(a=d=1,他0)で、SetAnimation非対象マテリアルは無変更。
    float uvTransform[6] = {1, 0, 0, 1, 0, 0}; // a,b,c,d,tx,ty

    // ChangeAlpha(alpha-changer)がこのマテリアルを対象にしているか。
    // base .x の diffuse.a が 1.0(不透明)でも、実行時にアルファが変わることがある
    // マテリアルは強制的に半透明パス(ブレンド有効)へ回さないと、値を更新
    // しても不透明パスがブレンド無しで描くため見た目に一切反映されない。
    bool hasAlphaOverride = false;

    // 実行時にテクスチャが差し替わることがあるマテリアル（ChangeTexture の対象、または
    // SetAnimation の画像切替(Simple/Numbered)フレームの対象）。ベース .x の
    // テクスチャが不透明でも、差し替え先が透明/抜き（LED 方向幕など）に
    // なることがある。不透明パス固定だと透明フレームでも RGB を不透明描画し深度を
    // 書いて背後の同種レイヤーを潰す（方向幕のような差替テクスチャが「3枚重なると
    // 奥が消える」症状）。そこで半透明でない限りカットアウト（アルファテスト+
    // 深度書込ON）へ昇格させ、透明フレームは clip で捨てる。差し替え先が
    // 不透明同士なら clip は発火せず不透明描画と等価で無害。分類は
    // mesh_upload.cpp の UploadMesh 側で行う。
    bool swappableTexture = false;

    // AlphaZeroTest = id列; で指定されたマテリアル。テクスチャに中間 alpha が
    // 含まれていてもブレンド(半透明パス)へ回さず、不透明パス+アルファテストで
    // 描く(RailSim II 互換。古いプラグインはドット絵縮小等で意図せず中間 alpha を含む
    // テクスチャをこの指定で二値抜き扱いにしている)。
    bool forceAlphaTest = false;

    // Transparent = id列; で指定されたマテリアル。RailSim II の意味は「他の全
    // ポリゴンより後に描画する」のみで、深度書き込み無効化は含意しない
    // (pi_sym_shadow_inhibitor.html)。不透明パスと同じ深度書込ONのまま
    // 描画順だけ最後に回す「遅延不透明バケツ」へ回す。
    // なお材質色Diffuse.a<1由来の実アルファ(matAlphaReal)を持つマテリアルは、
    // このforceTransparent指定の有無に関わらずTransparentバケツへ回る(下記参照)。
    bool forceTransparent = false;

    // 影の扱い(NoCastShadow / NoReceiveShadow / NoShadow / Transparent)。偽なら、それぞれ影を落とさない・影を受けない。
    bool castShadow = true;
    bool receiveShadow = true;

    // 全フィールドの一致を見る(defaultなので、フィールドを足しても比較から漏れない)。
    bool operator==(const Material&) const = default;
};

// 1 サブメッシュ = 1 マテリアルに属する連続インデックス範囲
enum class SubmeshAlphaClass : uint8_t {
    Opaque,
    OpaqueAlphaTex,
    Cutout,
    LateOpaque,
    // 材質色Diffuse.a<1の材質(窓ガラスなど)。RailSim II と同じく、クリップなし・深度書込ON・
    // ブレンド有効の1パスで、submesh順(Diffuse.aの降順)に描く。深度を書くので、窓越しの景色が
    // 落ちないよう、全パイプラインの不透明描画の後にまとめて描く。
    Blended,
    // ChangeAlphaで見え消えする穴あきの材質(ブラインド・点灯レンズの光など)。Blendedと同じ描き方を、
    // 下地と同じ面に重ねるため、パイプライン内の不透明と同じループでsubmesh順に描く。
    BlendedInPlace,
    Transparent,
};

// 1枚の穴あきテクスチャを「不透明(上限以上)」と「半透明(下限以上、上限未満)」に分けて描くための印。
// 半透明のtexelが多い穴あき材質は、同じ index 範囲を2つの submesh に分ける。
enum class AlphaSplit : uint8_t {
    None,
    OpaquePart, // 穴あきの描画。上限未満のtexelを捨て、深度も上限以上だけ書く
    SemiPart,   // Blendedの段で描く。下限以上、上限未満のtexelだけ残す(深度は書かない)
};

struct SubMesh {
    uint32_t materialIndex = 0;
    uint32_t indexOffset = 0; // indices 配列内の開始
    uint32_t indexCount = 0;

    // 描画バケツの排他的な分類（ロード時に計算してキャッシュ）。
    SubmeshAlphaClass alphaClass = SubmeshAlphaClass::Opaque;
    AlphaSplit alphaSplit = AlphaSplit::None;
    // AlphaZeroTest、穴あきテクスチャ、実行時テクスチャ差替えによるalpha testの要否。
    bool alphaTestClip = false;
    // 細い部品(架線・ハンガーなど)。影の品質が低いときは影パスで描かない(影への寄与が小さい割に頂点が多い)。
    bool thinShadow = false;
    // 頂点色のアルファが1未満の頂点を含む。形状の性質なので、材質ではなく submesh が持つ。
    bool vertexAlpha = false;
    // 影パスで描くか(材質の castShadow から決める)。偽なら影を落とさない。
    bool castShadow = true;

    bool IsTransparent() const { return alphaClass == SubmeshAlphaClass::Transparent; }
    bool IsLateOpaque() const { return alphaClass == SubmeshAlphaClass::LateOpaque; }
    bool IsCutoutPass() const { return alphaClass == SubmeshAlphaClass::Cutout; }
    bool IsOpaqueAlphaTex() const { return alphaClass == SubmeshAlphaClass::OpaqueAlphaTex; }
    bool IsBlended() const { return alphaClass == SubmeshAlphaClass::Blended; }
    bool IsBlendedInPlace() const { return alphaClass == SubmeshAlphaClass::BlendedInPlace; }
    bool NeedsTwoPassDraw() const {
        return alphaClass != SubmeshAlphaClass::Transparent &&
               alphaClass != SubmeshAlphaClass::Opaque &&
               alphaClass != SubmeshAlphaClass::Blended &&
               alphaClass != SubmeshAlphaClass::BlendedInPlace;
    }
    float center[3] = {0, 0, 0};   // ローカル空間の近似中心（頂点平均）
};

struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t>   indices;
    std::vector<SubMesh>    submeshes;
    std::vector<Material>   materials;

    // 毎フレーム書き換える頂点の範囲。頂点配列の末尾の連続した範囲(first + count == vertices.size())に限る。
    // count == 0 が通常(書き換えない)。0 でないときだけ、専用の書き換え可能な頂点バッファへ上げる。
    uint32_t dynamicVertexFirst = 0;
    uint32_t dynamicVertexCount = 0;

    // AABB（自動フィット用）
    float aabbMin[3] = {0, 0, 0};
    float aabbMax[3] = {0, 0, 0};

    void ComputeAabb() {
        if (vertices.empty()) {
            for (int i = 0; i < 3; ++i) aabbMin[i] = aabbMax[i] = 0.0f;
            return;
        }
        for (int i = 0; i < 3; ++i) {
            aabbMin[i] = aabbMax[i] = vertices[0].pos[i];
        }
        for (const auto& v : vertices) {
            for (int i = 0; i < 3; ++i) {
                if (v.pos[i] < aabbMin[i]) aabbMin[i] = v.pos[i];
                if (v.pos[i] > aabbMax[i]) aabbMax[i] = v.pos[i];
            }
        }
    }

    bool Empty() const { return vertices.empty() || indices.empty(); }
};

} // namespace rs::render
