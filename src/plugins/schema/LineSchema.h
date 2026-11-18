//
// LineSchema.h - 架線(Line)・架線柱(Pole)・架線の支持点(LinePoint)
//
// 架線を組み立てる側が、定義全体まで読まずに済むよう PluginSchema.h から分けてある。
//
#pragma once

#include <string>
#include <vector>

#include "RailProfileSchema.h"
#include "../../core/Math.h"

namespace rs::legacy_pi {

// Wireframe{}内のLine{}1本ぶんの頂点(RailSim II のPI仕様)。Coord=(x,y,z)のxは架線では使わない(常に0)。
// StretchZY の Line だけが、x を断面の横ずらしに使う。
// zは[0,1]で径間長に比例スケール、yは絶対値[m](スケールしない)。
struct LineWireVertex {
    float y = 0.0f;
    float z = 0.0f;
    // Vertex "0.45"{...}のようなラベル(ハンガー取付点トラッキング用、空なら通常頂点)。
    // RailInterval::wireTrackingと突き合わせてハンガーモデルを選ぶ。
    std::string label;
    // Dir/Up: 断面の向きを決める局所の方向(径間の向き・上向きを基準に変換する)。正規化して持つ。
    rs::core::Vec3 dir{0.0f, 0.0f, 1.0f};
    rs::core::Vec3 up{0.0f, 1.0f, 0.0f};
    // TexV: 指定した頂点のV座標の調整値。負は指定なし。
    float texV = -1.0f;
    // Coord の x。架線では使わない(常に0)。StretchZY の Line が断面の横ずらしに使う。
    float x = 0.0f;
};

struct LineWirePolyline {
    std::string label; // Line "名"{} の名前。追従先の指定に使う側が読む。無ければ空
    std::vector<LineWireVertex> verts;
};

// Wireframe{ MinInterval; MaxInterval; Line{...} ... } 1ブロックぶん。径間長が
// [minInterval, maxInterval)に収まるときこのプロファイルを使う(実データでは
// ~10m/10~20m/20m~の3段階、径間が長いほど頂点が多く精緻なたわみ表現になる)。
struct LineWireframe {
    float minInterval = 0.0f;
    float maxInterval = 1.0e9f;
    std::vector<LineWirePolyline> lines; // トロリ線本体・たわみ架線・吊り金具等
    // 代表色(先頭のVertex.Diffuseから取得。実データでは同一Wireframe内は単色が
    // 通例のため、ブロック単位の単一マテリアルとして扱う簡略化)。
    float diffuseR = 0.25f, diffuseG = 0.25f, diffuseB = 0.25f, diffuseA = 1.0f;
};

// Wiredmesh{ MinInterval; MaxInterval; Material{...}; Face{...}; Line{...} } 1ブロックぶん。
// -k-build (*2k.txt)のみに存在し、実描画対象の架線本体を表す(RailSim II の CLine::Render()の
// 描画元)。Line{}のスキーマはWireframeと同一だが、Wireframeは同じ -k-build のファイル内では
// 別の意味(ハンガー/碍子モデルの取付点トラッキング用ラベル付き頂点、Diffuseを持たない)
// に使われるため、両者を混同してはいけない。
// Material/Face(2D断面)はRailProfile/RailProfileFaceと同一スキーマなのでそのまま再利用し、
// Line{}沿いにこの断面をスイープしたテクスチャ付きメッシュとして描画する。
struct LineWiredmesh {
    float minInterval = 0.0f;
    float maxInterval = 1.0e9f;
    std::vector<LineWirePolyline> lines;
    float diffuseR = 1.0f, diffuseG = 1.0f, diffuseB = 1.0f, diffuseA = 1.0f;
    std::string texFileName;
    float texVPerMeter = 1.0f;
    bool useTexture = false;
    ProfileMaterialExtra materialExtra;
    std::vector<RailProfileFace> faces; // 空ならFace未定義(Diffuseリボンへフォールバック)
};

// Line(架線) 固有定義（あれば）。
// Pole(架線柱)の配置間隔はPole側ではなくこちら(LineInfo)が持つ。
struct LineDef {
    bool  hasLineInfo = false;
    float trolleyAlt = 0.0f;    // TrolleyAlt: レール面-トロリ線間高さ[m]
    float height = 0.0f;        // Height: トロリ線-支持点(電柱取付点)間高さ[m]
    float maxInterval = 40.0f;  // MaxInterval: 最大径間長[m]（直線区間での上限）
    float maxDeflection = 0.5f; // MaxDeflection: パンタグラフ中心線からの最大偏位[m]
    float offset = 0.0f;        // Offset: 端点から最初の架線柱までの距離[m]
    // 描画元は wiredmeshes があればそちらを使う(-k-build、Trolley常時+径間長別の
    // messenger帯を同時描画=複数一致)。無ければ wireframes を単一プロファイル
    // 選択で使う(旧形式フォールバック)。
    std::vector<LineWiredmesh> wiredmeshes;
    std::vector<LineWireframe> wireframes;
};

// Pole(架線柱) 固有定義（あれば）。径間長(Pole間隔)パラメータは持たない
// (Line側が決める)。trackIntervalは並走軌道間の実測間隔とは別に
// PI側が明示する架線接続点オフセット算出用の値(trackNum>1の複線一括Poleのみ意味を持つ)。
struct PoleDef {
    bool hasPoleInfo = false;
    int   trackNum = 1;       // TrackNum: このPoleが対応する並走軌道数
    float trackInterval = 0.0f; // TrackInterval[m]: 並走軌道間隔(架線接続点オフセット算出用)
};

// Struct 最上位の LinePoint{ Coord; Dir }。Struct を置いたとき、架線をつなぐ見えない支持点を
// Struct のローカル座標・向きで指す。Struct の行列で変換する側は配置処理が受け持つ。
struct LinePointDef {
    rs::core::Vec3 coord{0, 0, 0};
    rs::core::Vec3 dir{0, 0, 1};
};

} // namespace rs::legacy_pi
