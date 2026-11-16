//
// SchemaProfile.cpp - Profile / Wiredmesh / Line / StretchZY の読み取り
//
// Rail の Profile、架線の Wiredmesh、自由オブジェクトの StretchZY が同じ書式(Material・Face・Line)を
// 持つので、読み取りをここに集めて共用する。
//
#include <algorithm>
#include <cmath>

#include "../../core/Epsilon.h"
#include "SchemaInternal.h"

namespace rs::legacy_pi {

namespace {

bool ReadYesNo(const PiBlock& block, const char* key, bool def) {
    const PiAssignment* a = block.Find(key);
    if (a == nullptr || a->First() == nullptr) return def;
    return a->First()->IsYes();
}

// (dx,dy)を反時計回りに90度回転した(-dy,dx)を正規化して返す。断面辺の
// 押し出し方向(+Z)に対する外向き法線の既定値算出に使う(RailSim II の
// CProfileFace::Read、CProfilePlugin.cpp参照)。長さ0ならy軸正を返す。
void RotatedUnit(float dx, float dy, float& outX, float& outY) {
    float nx = -dy, ny = dx;
    float len = std::sqrt(nx * nx + ny * ny);
    if (len > rs::core::kDegenerateVectorEps) { outX = nx / len; outY = ny / len; }
    else { outX = 0.0f; outY = 1.0f; }
}

// Normal= が省略された頂点へ、RailSim II と同じ既定法線を書き込む。
// 先頭/末尾頂点は隣接1辺の直交ベクトル、中間頂点は前後2辺の直交ベクトルの和を使う
// (CProfilePlugin.cpp CProfileFace::Read)。既定(0,1)固定のままだと
// 垂直な断面辺(dx=0)で外向き法線が実際の向きと無関係な値になってしまう。
void FillDefaultFaceNormals(RailProfileFace& face) {
    const size_t n = face.verts.size();
    if (n < 2) return;
    for (size_t i = 0; i < n; ++i) {
        RailProfileVertex& v = face.verts[i];
        if (v.hasNormal) continue;
        if (i == 0) {
            const RailProfileVertex& next = face.verts[i + 1];
            RotatedUnit(next.coordX - v.coordX, next.coordY - v.coordY, v.normalX, v.normalY);
        } else if (i == n - 1) {
            const RailProfileVertex& prev = face.verts[i - 1];
            RotatedUnit(v.coordX - prev.coordX, v.coordY - prev.coordY, v.normalX, v.normalY);
        } else {
            const RailProfileVertex& prev = face.verts[i - 1];
            const RailProfileVertex& next = face.verts[i + 1];
            float n1x, n1y, n2x, n2y;
            RotatedUnit(v.coordX - prev.coordX, v.coordY - prev.coordY, n1x, n1y);
            RotatedUnit(next.coordX - v.coordX, next.coordY - v.coordY, n2x, n2y);
            float sx = n1x + n2x, sy = n1y + n2y;
            float len = std::sqrt(sx * sx + sy * sy);
            if (len > rs::core::kDegenerateVectorEps) { v.normalX = sx / len; v.normalY = sy / len; }
            else { v.normalX = n1x; v.normalY = n1y; }
        }
    }
}

// Dir/Upのベクトル値を正規化して読む。キーが無い・長さ0のときはfallbackのまま。
void ReadUnitVector(const PiBlock& vtx, const char* key, rs::core::Vec3& out) {
    const PiAssignment* a = vtx.Find(key);
    if (a == nullptr || a->First() == nullptr) return;
    rs::core::Vec3 v;
    a->First()->AsVec3(v.x, v.y, v.z);
    if (v.Length() > rs::core::kDegenerateVectorEps) out = v.Normalized();
}

} // namespace

namespace schema_detail {

// TexFileName は1〜3値(ベース・法線・PBR の順)。書かれた値だけ持ち、法線の自動導出はしない。
void ParseProfileMaterial(const PiBlock& node, std::string& texFileName, float& texVPerMeter,
                          bool& useTexture, ProfileMaterialExtra& extra) {
    const PiBlock* mat = node.FirstChild("Material");
    if (mat == nullptr) return;
    if (const PiAssignment* a = mat->Find("TexFileName")) {
        if (a->values.size() > 0) texFileName = a->values[0].AsString();
        if (a->values.size() > 1) extra.normalTexFileName = a->values[1].AsString();
        if (a->values.size() > 2) extra.pbrTexFileName = a->values[2].AsString();
    }
    if (const PiAssignment* v = mat->Find("TexVPerMeter"))
        if (v->First()) texVPerMeter = static_cast<float>(v->First()->AsFloat(1.0));
    useTexture = ReadYesNo(*mat, "UseTexture", useTexture);
    extra.bumpMap = ReadYesNo(*mat, "BumpMap", extra.bumpMap);
    extra.envMap = ReadYesNo(*mat, "EnvMap", extra.envMap);
    extra.alphaZeroTest = ReadYesNo(*mat, "AlphaZeroTest", extra.alphaZeroTest);
    extra.castShadow = ReadYesNo(*mat, "CastShadow", extra.castShadow);
}

// Profile{}/Wiredmesh{}共通のFace{ Vertex{ Coord=(x,y); Normal=(nx,ny); TexU } }読み取り
// (押し出し/スイープ用の2D断面)。
std::vector<RailProfileFace> ParseProfileFaces(const PiBlock& node) {
    std::vector<RailProfileFace> faces;
    for (const PiBlock* face : node.Children("Face")) {
        RailProfileFace f;
        for (const PiBlock* vtx : face->Children("Vertex")) {
            RailProfileVertex rv;
            if (const PiAssignment* c = vtx->Find("Coord"))
                if (const PiValue* v = c->First()) {
                    float z = 0;
                    v->AsVec3(rv.coordX, rv.coordY, z);
                }
            if (const PiAssignment* n = vtx->Find("Normal"))
                if (const PiValue* v = n->First()) {
                    float z = 0;
                    v->AsVec3(rv.normalX, rv.normalY, z);
                    rv.hasNormal = true;
                }
            if (const PiAssignment* u = vtx->Find("TexU"))
                if (u->First()) rv.texU = static_cast<float>(u->First()->AsFloat(0.0));
            if (const PiAssignment* ic = vtx->Find("IgnoreCant"))
                rv.ignoreCant = ic->First() && ic->First()->IsYes();
            // Diffuse は省略可(省略時・解析できないときは白・不透明のまま)。
            if (const PiAssignment* d = vtx->Find("Diffuse"))
                if (const PiValue* v = d->First(); v && v->kind == PiValue::Kind::HexColor) {
                    float r = 1, g = 1, b = 1, a = 1;
                    if (ParseArgbHex(v->text, r, g, b, a)) {
                        const float channels[4] = {r, g, b, a};
                        for (int i = 0; i < 4; ++i)
                            rv.color[i] = static_cast<uint8_t>(std::lround(std::clamp(channels[i], 0.0f, 1.0f) * 255.0f));
                    }
                }
            f.verts.push_back(rv);
        }
        if (f.verts.size() >= 2) {
            FillDefaultFaceNormals(f);
            faces.push_back(std::move(f));
        }
    }
    return faces;
}

// Wireframe{}/Wiredmesh{}/StretchZY{}共通のLine "名"{ Vertex{ Coord; Dir; Up; TexV; Diffuse } }読み取り。
// 代表色は先頭に見つかったVertex.Diffuseを使う。Dir/Up/TexVを描画に使うのはWiredmeshとStretchZYだけ。
void ParseWireLines(const PiBlock& node, std::vector<LineWirePolyline>& outLines, float& diffuseR,
                    float& diffuseG, float& diffuseB, float& diffuseA) {
    bool haveColor = false;
    for (const PiBlock* lineBlk : node.Children("Line")) {
        LineWirePolyline poly;
        poly.label = lineBlk->label;
        for (const PiBlock* vtx : lineBlk->Children("Vertex")) {
            LineWireVertex wv;
            wv.label = vtx->label; // Vertex "0.45"{...}のラベル(ハンガー取付点トラッキング用)
            if (const PiAssignment* c = vtx->Find("Coord"))
                if (const PiValue* v = c->First()) {
                    v->AsVec3(wv.x, wv.y, wv.z);
                }
            ReadUnitVector(*vtx, "Dir", wv.dir);
            ReadUnitVector(*vtx, "Up", wv.up);
            if (const PiAssignment* t = vtx->Find("TexV"))
                if (t->First()) wv.texV = static_cast<float>(t->First()->AsFloat(-1.0));
            poly.verts.push_back(wv);
            if (!haveColor) {
                if (const PiAssignment* d = vtx->Find("Diffuse"))
                    if (const PiValue* v = d->First(); v && v->kind == PiValue::Kind::HexColor) {
                        ParseArgbHex(v->text, diffuseR, diffuseG, diffuseB, diffuseA);
                        haveColor = true;
                    }
            }
        }
        if (poly.verts.size() >= 2) outLines.push_back(std::move(poly));
    }
}

RailProfile BuildRailProfile(const PiBlock& node) {
    RailProfile prof;
    if (const PiAssignment* a = node.Find("MinRadius"))
        if (a->First()) prof.minRadius = static_cast<float>(a->First()->AsFloat(-1.0));
    if (const PiAssignment* a = node.Find("MaxRadius"))
        if (a->First()) prof.maxRadius = static_cast<float>(a->First()->AsFloat(-1.0));
    ParseProfileMaterial(node, prof.texFileName, prof.texVPerMeter, prof.useTexture, prof.materialExtra);
    prof.faces = ParseProfileFaces(node);
    return prof;
}

// container 直下の StretchZY "名"{ Profile{}; Line*; JointZYX ×2 } を読む。
// Profile が無いブロックは空の断面で持つ。Line が1本も読めなければ Coord=(0,0,0) と (0,0,1) の既定の1本を入れる。
void CollectStretchZys(const PiBlock& container, std::vector<StretchZyDef>& out) {
    for (const PiBlock* block : container.Children("StretchZY")) {
        StretchZyDef def;
        def.label = block->label;
        if (const PiBlock* profile = block->FirstChild("Profile")) def.profile = BuildRailProfile(*profile);
        float unusedR = 0.0f, unusedG = 0.0f, unusedB = 0.0f, unusedA = 0.0f;
        ParseWireLines(*block, def.lines, unusedR, unusedG, unusedB, unusedA);
        if (def.lines.empty()) {
            LineWirePolyline defaultLine;
            defaultLine.verts.resize(2);
            defaultLine.verts[1].z = 1.0f;
            def.lines.push_back(std::move(defaultLine));
        }
        constexpr int kMaxJoints = 2;
        for (const PiBlock* joint : block->Children("JointZYX")) {
            if (def.jointCount >= kMaxJoints) break;
            def.joints[def.jointCount++] = ParseJointZYX(*joint);
        }
        out.push_back(std::move(def));
    }
}

} // namespace schema_detail

} // namespace rs::legacy_pi
