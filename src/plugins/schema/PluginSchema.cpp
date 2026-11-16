#include "PluginSchema.h"
#include "PluginTypeTraits.h"

#include <cstdlib>

#include <magic_enum/magic_enum.hpp>

#include "SchemaInternal.h"

namespace rs::legacy_pi {

const char* PluginTypeName(PluginType t) {
    const std::string_view sv = magic_enum::enum_name(t);
    return sv.empty() ? "Unknown" : sv.data();
}

PluginType PluginTypeFromString(const std::string& s) {
    return magic_enum::enum_cast<PluginType>(s).value_or(PluginType::Unknown);
}

// "RRGGBB"/"AARRGGBB"(6/8桁hex、'#'除く)をRGBA float[0,1]へ変換する
// (Wireframe{ Line{ Vertex{ Diffuse=#ff404040; } } }用。
// Envの色指定も同じ読み方を使う)。6桁ならアルファ=1.0とみなす。
// 桁数が不正なら暗い灰色(アルファ1)を返し、falseを返す。
bool ParseArgbHex(const std::string& hex, float& r, float& g, float& b, float& a) {
    if (hex.size() != 6 && hex.size() != 8) {
        r = g = b = 0.25f;
        a = 1.0f;
        return false;
    }
    const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
    const bool hasAlpha = hex.size() == 8;
    const unsigned aByte = hasAlpha ? (v >> 24) & 0xFF : 0xFF;
    const unsigned rByte = (v >> 16) & 0xFF;
    const unsigned gByte = (v >> 8) & 0xFF;
    const unsigned bByte = v & 0xFF;
    r = static_cast<float>(rByte) / 255.0f;
    g = static_cast<float>(gByte) / 255.0f;
    b = static_cast<float>(bByte) / 255.0f;
    a = static_cast<float>(aByte) / 255.0f;
    return true;
}

namespace {

using schema_detail::StrAssign;

} // namespace

PluginHeader BuildPluginHeader(const PiBlock& node) {
    PluginHeader h;
    h.railSimVersion = StrAssign(node, "RailSimVersion");
    h.typeRaw = StrAssign(node, "PluginType");
    h.type = PluginTypeFromString(h.typeRaw);
    h.name = StrAssign(node, "PluginName");
    h.author = StrAssign(node, "PluginAuthor");
    h.iconTexture = StrAssign(node, "IconTexture");
    // IconRect = u, v, 幅, 高さ。4値そろったときだけ採用し、不足は省略扱い(全体)にする。
    if (const PiAssignment* rect = node.Find("IconRect")) {
        if (rect->values.size() >= 4) {
            for (size_t i = 0; i < 4; ++i)
                h.iconRect[i] = static_cast<float>(rect->values[i].AsFloat(h.iconRect[i]));
        }
    }
    h.description = StrAssign(node, "Description");
    return h;
}

namespace {

// 旧形式(非 -k-build)の径間長帯ごとの架線ジオメトリ。
// -k-build のファイルではWireframeはハンガー取付点トラッキング
// (Diffuseなし)に転用されるため、そちらはこの分岐では拾わずwiredmeshes優先で
// 描画側が無視する(BuildLineWireMesh側のゲート判定)。
LineWireframe BuildLineWireframe(const PiBlock& node) {
    LineWireframe wf;
    if (const PiAssignment* a = node.Find("MinInterval"))
        if (a->First()) wf.minInterval = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = node.Find("MaxInterval"))
        if (a->First()) wf.maxInterval = static_cast<float>(a->First()->AsFloat(1.0e9));
    schema_detail::ParseWireLines(node, wf.lines, wf.diffuseR, wf.diffuseG, wf.diffuseB, wf.diffuseA);
    return wf;
}

// -k-build(*2k.txt)の実描画対象の架線本体。
// 区間が重複しうる(トロリ線は区間指定なし=常時、messengerは径間長
// 帯別)ため、描画側は複数一致を許容する。Material/Faceの2D断面はProfileと
// 同一スキーマなのでschema_detail::ParseProfileMaterial/ParseProfileFacesを共用する。
LineWiredmesh BuildLineWiredmesh(const PiBlock& node) {
    LineWiredmesh wm;
    if (const PiAssignment* a = node.Find("MinInterval"))
        if (a->First()) wm.minInterval = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = node.Find("MaxInterval"))
        if (a->First()) wm.maxInterval = static_cast<float>(a->First()->AsFloat(1.0e9));
    schema_detail::ParseProfileMaterial(node, wm.texFileName, wm.texVPerMeter, wm.useTexture, wm.materialExtra);
    wm.faces = schema_detail::ParseProfileFaces(node);
    schema_detail::ParseWireLines(node, wm.lines, wm.diffuseR, wm.diffuseG, wm.diffuseB, wm.diffuseA);
    return wm;
}

// containerからエフェクタ(SoundEffect/Headlight/Particle)とパーツを集めて def へ足す。
// パーツ収集には、エフェクタのAttachObjectが指すラベルを渡す(位置アンカーのダミーを捨てないため)。
void CollectEffectorsAndParts(const PiBlock& container, PluginDefinition& def) {
    schema_detail::EffectorCollection effectors;
    schema_detail::CollectEffectors(container, effectors);
    schema_detail::CollectParts(container, schema_detail::CollectEffectorLabels(effectors),
                                def.joint3dWarnings, def.parts);
    for (SoundEffectRef& e : effectors.soundEffects) def.soundEffects.push_back(std::move(e));
    for (HeadlightDef& e : effectors.headlights) def.headlights.push_back(std::move(e));
    for (ParticleDef& e : effectors.particles) def.particles.push_back(std::move(e));
    for (RailEffectorDef& e : effectors.railEffectors) def.railEffectors.push_back(std::move(e));
    schema_detail::CollectFlagZys(container, def.flagZys);
    schema_detail::CollectStretchZys(container, def.stretchZys);
}

} // namespace

PluginDefinition BuildPluginDefinition(PiDocument doc) {
    PluginDefinition def;

    for (const auto& node : doc.nodes) {
        if (node.name == "PluginHeader") {
            def.header = BuildPluginHeader(node);
        } else if (node.name == "DefineAnimation") {
            def.animations.push_back(schema_detail::ParseDefineAnimation(node));
        } else if (node.name == "Platform") {
            def.platforms.push_back(schema_detail::ParsePlatformDef(node));
        } else if (node.name == "DefineSwitch") {
            schema_detail::ReadSwitchDef(node, def);
        } else if (node.name == "TrainInfo") {
            schema_detail::ReadTrainInfo(node, def);
        } else if (node.name == "FrontCabin") {
            // トップレベル直下に書かれた場合の救済(通常はPrimaryAssembly直下、下記参照)。
            def.trainInfo.hasFrontCabin = ParseCabinBlock(node, def.trainInfo.frontCabin);
        } else if (node.name == "TailCabin") {
            def.trainInfo.hasTailCabin = ParseCabinBlock(node, def.trainInfo.tailCabin);
        } else if (node.name == "RailInfo" || node.name == "TieInfo" || node.name == "GirderInfo") {
            schema_detail::ReadRailInfo(node, def);
        } else if (node.name == "SurfaceInfo") {
            schema_detail::ReadSurfaceInfo(node, def);
        } else if (node.name == "Profile") {
            def.railProfiles.push_back(schema_detail::BuildRailProfile(node));
        } else if (node.name == "Interval") {
            schema_detail::ReadInterval(node, def);
        } else if (node.name == "PierInfo") {
            schema_detail::ReadPierInfo(node, def);
        } else if (node.name == "LineInfo") {
            schema_detail::ReadLineInfo(node, def);
        } else if (node.name == "PoleInfo") {
            schema_detail::ReadPoleInfo(node, def);
        } else if (node.name == "SoundInfo") {
            schema_detail::ReadSoundInfo(node, def);
        } else if (node.name == "LinePoint") {
            def.linePoints.push_back(schema_detail::ParseLinePoint(node));
        } else if (node.name == "Wireframe") {
            def.line.wireframes.push_back(BuildLineWireframe(node));
        } else if (node.name == "Wiredmesh") {
            def.line.wiredmeshes.push_back(BuildLineWiredmesh(node));
        } else if (node.name == "Joint" || node.name == "Head" || node.name == "Base") {
            schema_detail::ReadPierPart(node, def);
        } else if (node.name == "PrimaryAssembly") {
            CollectEffectorsAndParts(node, def);
            // FrontCabin/TailCabinはPrimaryAssembly直下の子ブロック(RailSim II の
            // CTrainPlugin.cpp:589-598と同じネスト。トップレベル直下ではない)。
            if (!def.trainInfo.hasFrontCabin) {
                if (const PiBlock* cabin = node.FirstChild("FrontCabin"))
                    def.trainInfo.hasFrontCabin = ParseCabinBlock(*cabin, def.trainInfo.frontCabin);
            }
            if (!def.trainInfo.hasTailCabin) {
                if (const PiBlock* cabin = node.FirstChild("TailCabin"))
                    def.trainInfo.hasTailCabin = ParseCabinBlock(*cabin, def.trainInfo.tailCabin);
            }
        }
    }

    // PrimaryAssembly が無い/空でもトップレベルにパーツがある種別に備え、
    // トップレベルからも汎用収集する（未知種別のフォールバック）。
    if (def.parts.empty()) {
        PiBlock top;
        top.blocks = doc.nodes; // 走査用の一時ラッパ
        for (size_t index = 0; index < top.blocks.size(); ++index) {
            PiMember member;
            member.kind = PiMember::Kind::Block;
            member.index = index;
            top.members.push_back(std::move(member));
        }
        CollectEffectorsAndParts(top, def);
    }

    // CastShadow を書かなかったパーツは種別の既定に従う(Surface だけ影を落とさない)。
    for (ModelPart& part : def.parts) {
        if (!part.castShadowSpecified) part.castShadow = GetPluginTypeTraits(def.header.type).defaultCastShadow;
    }

    // PrimaryAssembly の組立座標（offsetZ/offsetY/turn）を解決する。
    // Train 等の全パーツ同時表示（パーツ単位ワールド行列）で使用する。
    schema_detail::ResolvePlacements(def.parts);

    // Joint3D 配置（Station 等の施設パーツで使用）を反復解決する。
    schema_detail::ResolveJoint3D(def.parts, def.joint3dWarnings);

    // Platform の ParentObject を、パーツのラベルから一度だけ解決する。
    for (size_t i = 0; i < def.platforms.size(); ++i) {
        PlatformDef& platform = def.platforms[i];
        if (platform.parentObject.empty()) continue;
        platform.parentPartIndex = FindPlatformParentPart(def.parts, platform.parentObject);
        if (platform.parentPartIndex >= 0) continue;
        platform.parentUndefined = true;
        def.joint3dWarnings.push_back("Platform #" + std::to_string(i) + ": UndefinedObject: \"" +
                                      platform.parentObject + "\"");
    }

    // FrontCabin/TailCabin省略時のフォールバック合成。
    ResolveCabinFallback(def.trainInfo, def.parts);

    def.doc = std::move(doc);
    return def;
}

} // namespace rs::legacy_pi
