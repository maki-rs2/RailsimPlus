//
// ProfileMaterial.h - 断面(Profile / Wiredmesh)の材質へテクスチャを入れる共通処理
//
// Rail・Tie・Girder・Pier・Line の断面メッシュ生成が同じ規則でベース画像と法線マップを解決する。
//
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "../schema/PluginSchema.h"
#include "../../render/mesh/Mesh.h"

namespace rs::legacy_plugin {

// 断面の頂点色を、メッシュの頂点へ写す。
void CopyProfileVertexColor(rs::render::MeshVertex& meshVertex, const rs::legacy_pi::RailProfileVertex& profileVertex);

// 1つの断面(1材質)の四角形を、不透明なものと透けるもの(両端の頂点のアルファがどちらかでも255未満)に振り分けて集める。
// 不透明の四角形 → 透ける四角形の順に、連続した2範囲の submesh として出す(透ける四角形が無ければ submesh は1つ)。
class ProfileQuadSink {
public:
    // a・b は四角形の断面上の両端の頂点。indices は四角形の三角形ぶんの頂点番号。
    void AddQuad(const rs::legacy_pi::RailProfileVertex& a, const rs::legacy_pi::RailProfileVertex& b,
                 std::initializer_list<uint32_t> indices);
    // 集めた index を mesh.indices の末尾へ移し、材質 materialIndex の submesh を mesh.submeshes へ足す。
    void Flush(rs::render::Mesh& mesh, uint32_t materialIndex);

private:
    std::vector<uint32_t> opaque_;
    std::vector<uint32_t> translucent_;
};

// 断面の材質 mat にベース画像と法線マップのパスを入れる(拡散色などは呼び出し側が先に入れておく)。
// 断面の頂点色は材質の Diffuse と Ambient の両方を置き換えるので、ここで ambient も白(1.0)にする(色を書かない断面は白になる)。
// - ベース: useTexture が真で texFileName が空でなければ解決する。無ければ警告を log に積み、missing を増やす。
// - 法線: ベースが決まった材質だけ。TexFileName の2値目(extra.normalTexFileName)があればそれを解決し、
//   無ければ警告と missing(ベースと同じ扱い)。書かれていなければ、ベース画像の隣の「<画像名>_n<拡張子>」を探し、
//   あれば使い、無ければ何もしない。
// - 法線を入れたら useBumpMap も真にする。BumpMap = yes の指定が無くても使うのは、作者が同梱した法線マップを
//   未指定のときに補うエンジン独自の動き(RailSim II -k-build は BumpMap = yes の材質にだけ法線を表示する)。
// - EnvMap・AlphaZeroTest・CastShadow は、画像の有無に関係なく材質の useEnvMap・forceAlphaTest・castShadow へ写す。
//   PBR の画像(extra.pbrTexFileName)は、RailSim II -k-build の断面の経路が PBR のシェーダを選ばないので使わない。
// label は警告文の種別名("rail"・"pier"・"line wire")。
void ApplyProfileTextures(rs::render::Material& mat, const std::string& texFileName, bool useTexture,
                          const rs::legacy_pi::ProfileMaterialExtra& extra, const std::wstring& baseDir,
                          const char* label, std::vector<std::string>& log, int& missing);

} // namespace rs::legacy_plugin
