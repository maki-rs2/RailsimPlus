//
// MaterialOverwrite.cpp - 材質の上書きファイル(.mat)の読込と適用
//
#include "MaterialOverwrite.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/schema/parser/Parser.h"

namespace rs::legacy_x {

namespace {

namespace fs = std::filesystem;

// 画像の枠。color と albedo、normal、pbr は材質の枠へ入る。環境の球・キューブと rma は
// 現行の描画に対応する枠が無いので、読んで捨てる。
enum class TextureSlot { Color, Normal, Pbr, Ignored };

// 7種類以外の Type は color と同じ枠として扱う。
TextureSlot SlotOfType(const std::string& type) {
    const std::string t = rs::platform_text::ToAsciiLower(type);
    if (t == "normal") return TextureSlot::Normal;
    if (t == "pbr") return TextureSlot::Pbr;
    if (t == "sphere" || t == "cube" || t == "rma") return TextureSlot::Ignored;
    return TextureSlot::Color;
}

std::vector<int> ReadIds(const rs::legacy_pi::PiBlock& block) {
    std::vector<int> ids;
    const rs::legacy_pi::PiAssignment* a = block.Find("MaterialID");
    if (a == nullptr) return ids;
    for (const rs::legacy_pi::PiValue& v : a->values)
        if (v.kind == rs::legacy_pi::PiValue::Kind::Number) ids.push_back(v.AsInt());
    return ids;
}

// 数値だけが並ぶ代入を読む。数値以外が混ざっていれば空を返す。
std::vector<float> ReadFloats(const rs::legacy_pi::PiBlock& block, const char* key) {
    std::vector<float> out;
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (a == nullptr) return out;
    for (const rs::legacy_pi::PiValue& v : a->values) {
        if (v.kind != rs::legacy_pi::PiValue::Kind::Number) return {};
        out.push_back(static_cast<float>(v.number));
    }
    return out;
}

// 4値目(アルファ)が負の項目は適用を飛ばす。3値のときの4値目は 1.0。
// 戻り値は適用してよいか。rgb に先頭3値を入れる。
bool ReadColor3(const rs::legacy_pi::PiBlock& block, const char* key, float rgb[3]) {
    const std::vector<float> v = ReadFloats(block, key);
    if (v.size() != 3 && v.size() != 4) return false;
    const float alpha = v.size() == 4 ? v[3] : 1.0f;
    if (alpha < 0.0f) return false;
    rgb[0] = v[0];
    rgb[1] = v[1];
    rgb[2] = v[2];
    return true;
}

void ApplyMaterialBlock(const rs::legacy_pi::PiBlock& block, rs::render::Mesh& mesh) {
    const int count = static_cast<int>(mesh.materials.size());
    const std::vector<float> diffuse = ReadFloats(block, "Diffuse");
    const std::vector<float> power = ReadFloats(block, "Power");
    for (int id : ReadIds(block)) {
        if (id < 0 || id >= count) continue; // 範囲外の材質は飛ばす
        rs::render::Material& m = mesh.materials[static_cast<size_t>(id)];
        if (diffuse.size() == 4 && diffuse[3] >= 0.0f)
            for (int i = 0; i < 4; ++i) m.diffuse[i] = diffuse[static_cast<size_t>(i)];
        ReadColor3(block, "Ambient", m.ambient);
        ReadColor3(block, "Specular", m.specular);
        ReadColor3(block, "Emissive", m.emissive);
        if (power.size() == 1 && power[0] >= 0.0f) m.power = power[0];
    }
}

// File の相対パスは .mat のあるフォルダが基準。無い画像は空(その枠を空にする)。
std::string ResolveImage(const std::string& matDirUtf8, const std::string& file,
                         std::vector<std::string>& log) {
    if (file.empty()) return {};
    const fs::path joined = fs::path(rs::platform_text::Utf8ToWide(matDirUtf8)) /
                            fs::path(rs::platform_text::Utf8ToWide(file));
    const fs::path normalized = joined.lexically_normal();
    std::error_code ec;
    if (!fs::exists(normalized, ec)) {
        log.push_back("[mat][warn] missing texture: " + file);
        return {};
    }
    return rs::platform_text::WideToUtf8(normalized.wstring());
}

void ApplyTextureBlock(const rs::legacy_pi::PiBlock& block, const std::string& matDirUtf8,
                       rs::render::Mesh& mesh, std::vector<std::string>& log) {
    if (mesh.materials.empty()) return;
    const rs::legacy_pi::PiAssignment* type = block.Find("Type");
    const std::string typeName = (type && type->First()) ? type->First()->AsString() : "";
    const TextureSlot slot = SlotOfType(typeName);
    if (slot == TextureSlot::Ignored) return;
    const rs::legacy_pi::PiAssignment* file = block.Find("File");
    const std::string fileName = (file && file->First()) ? file->First()->AsString() : "";
    const std::string resolved = ResolveImage(matDirUtf8, fileName, log);
    const int count = static_cast<int>(mesh.materials.size());
    for (int id : ReadIds(block)) {
        // 範囲外の材質番号は最後の材質として扱う(色の上書きとは違う)。
        rs::render::Material& m = mesh.materials[static_cast<size_t>(id < 0 || id >= count ? count - 1 : id)];
        switch (slot) {
        case TextureSlot::Color: m.texturePath = resolved; break;
        case TextureSlot::Normal: m.normalPath = resolved; break;
        case TextureSlot::Pbr: m.ormPath = resolved; break;
        case TextureSlot::Ignored: break;
        }
    }
}

} // namespace

std::wstring FindMaterialOverwriteFile(const std::wstring& xPath) {
    fs::path base(xPath);
    base.replace_extension();
    for (const wchar_t* ext : {L".Mat", L".mat", L".MAT"}) {
        fs::path candidate = base;
        candidate += ext;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate.wstring();
    }
    return {};
}

void ApplyMaterialOverwriteText(const std::string& utf8, const std::string& matDirUtf8,
                                rs::render::Mesh& mesh, std::vector<std::string>& log) {
    rs::legacy_pi::Diagnostics diag;
    const rs::legacy_pi::PiDocument doc = rs::legacy_pi::Parse(utf8, diag);
    for (const rs::legacy_pi::Diagnostic& d : diag.Items())
        log.push_back("[mat][warn] line " + std::to_string(d.line) + ": " + d.message);
    for (const rs::legacy_pi::PiBlock& block : doc.nodes) {
        if (block.name == "Material") ApplyMaterialBlock(block, mesh);
        else if (block.name == "Texture") ApplyTextureBlock(block, matDirUtf8, mesh, log);
    }
}

void ApplyMaterialOverwriteFor(const std::wstring& xPath, rs::render::Mesh& mesh,
                               std::vector<std::string>& log) {
    const std::wstring matPath = FindMaterialOverwriteFile(xPath);
    if (matPath.empty()) return;
    bool ok = false;
    const std::string text = rs::platform_text::ReadFileAsUtf8(matPath, ok);
    if (!ok) {
        log.push_back("[mat][warn] cannot read .mat");
        return;
    }
    const std::string dir =
        rs::platform_text::WideToUtf8(fs::path(matPath).parent_path().wstring());
    ApplyMaterialOverwriteText(text, dir, mesh, log);
    log.push_back("[mat] applied material overwrite");
}

} // namespace rs::legacy_x
