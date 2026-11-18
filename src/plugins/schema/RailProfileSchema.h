//
// RailProfileSchema.h - レール断面(Profile)と反復配置(Interval)
//
// 断面を押し出す側が、定義全体まで読まずに済むよう PluginSchema.h から分けてある。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rs::legacy_pi {

// Rail 断面の1頂点（断面は X(左右)-Y(上下) 平面。押し出しは Z 方向）
struct RailProfileVertex {
    float coordX = 0, coordY = 0;   // Coord = (x, y)
    float normalX = 0, normalY = 1; // Normal = (nx, ny)（未正規化で来る）
    bool  hasNormal = false;        // Normal= が明示されていたか(未指定時はビルダー側で
                                    // 断面辺の幾何法線へフォールバックする。既定(0,1)を
                                    // そのまま使うと垂直な断面辺で巻き順判定・法線の
                                    // 両方が誤る)
    float texU = 0;                 // TexU（断面まわりの U）
    bool  ignoreCant = false;       // IgnoreCant=yes: この頂点はカントで傾けない
                                    // (バラスト底辺など地面接地点)
    uint8_t color[4] = {255, 255, 255, 255}; // Diffuse（頂点色。R,G,B,A の順で sRGB のまま。省略時は白・不透明）
};

// 断面ポリライン1本（Face）。押し出すと (N-1) 枚の四角形になる。
struct RailProfileFace {
    std::vector<RailProfileVertex> verts;
};

// Profile/Wiredmesh の Material{} のうち、texFileName(ベース)・texVPerMeter・useTexture 以外の読み込み値。
// 描画に使うのは normalTexFileName・envMap・alphaZeroTest・castShadow(load/ProfileMaterial の ApplyProfileTextures)。pbrTexFileName・bumpMap は読み込みのままで、描画には使わない。
struct ProfileMaterialExtra {
    std::string normalTexFileName; // TexFileName の2値目。書かれていなければ空(空のときは反映側がベース画像の隣の _n を探す)
    std::string pbrTexFileName;    // TexFileName の3値目。書かれていなければ空
    bool bumpMap = false;          // BumpMap = yes の指定の有無。法線マップの反映には影響しない(あれば使う)
    bool envMap = false;
    bool alphaZeroTest = false;
    bool castShadow = true;
};

// Rail 用の断面（Material + ジオメトリ）。押し出しで1サブメッシュになる。
struct RailProfile {
    std::string                  texFileName;
    float                        texVPerMeter = 1.0f;
    bool                         useTexture = false;
    ProfileMaterialExtra         materialExtra;
    // MinRadius / MaxRadius: RailSim II の実行ファイルは読むだけで使っていない。負は指定なし。
    float                        minRadius = -1.0f;
    float                        maxRadius = -1.0f;
    std::vector<RailProfileFace> faces;
};

// Interval 反復配置(まくらぎ等をZ方向に等間隔で並べる)
struct RailInterval {
    std::string modelFile;         // 未解決相対パス
    float       modelScale = 1.0f;
    float       interval = 1.0f;   // Z方向の配置間隔[m]
    bool        ignoreCant = false; // IgnoreCant=yes: この反復配置はカントで傾けない
    // 複数の Interval ブロックを交互配置する際の、配置基準点(Z方向)の相対的な
    // ずれ[m]。デフォルト0.0(公式Help pi_sym_interval.html、実データでは橋桁用
    // 複数Intervalで0.6/1.2/1.8m等を指定して位相をずらす例を確認)。
    float       offset = 0.0f;
    // Line(架線)のハンガー(碍子)モデル選択キー(-k-build)。空ならRail/Tie/Girderの等間隔反復用。非空ならLineWireframe(ハンガー
    // 取付点トラッキング)内のラベル付きVertexのラベルと一致するIntervalがそのモデルを
    // 表す(実データ: WireTracking値=対応するmessengerのY高さ文字列、垂れ下がり長に
    // 応じたハンガーモデルを選ぶためのキー)。この場合interval/offset/ignoreCantは無視。
    std::string wireTracking;
};

} // namespace rs::legacy_pi
