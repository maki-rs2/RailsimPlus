//
// CustomizerParsers.cpp - CustomizerParsers の実装(SchemaWalk.cppから分離)
//
#include "CustomizerParsers.h"

#include <array>

#include "SchemaInternal.h"

namespace rs::legacy_pi {

namespace {

// 1つの値を読んで out へ入れる。型が違えばfalse。ReadPair の convert に渡す。
bool ConvertNumber(const PiValue& value, double& out) {
    if (value.kind != PiValue::Kind::Number) return false;
    out = value.number;
    return true;
}

bool ConvertVec3(const PiValue& value, rs::core::Vec3& out) {
    if (value.kind != PiValue::Kind::Tuple || value.tuple.size() != 3) return false;
    for (const PiValue& element : value.tuple)
        if (element.kind != PiValue::Kind::Number) return false;
    value.AsVec3(out.x, out.y, out.z);
    return true;
}

bool ConvertColor(const PiValue& value, PiColor& out) {
    return value.kind == PiValue::Kind::HexColor && ParseArgbHex(value.text, out.r, out.g, out.b, out.a);
}

// 必須の「2つまで書ける値」を読む。2つ目を省くと1つ目と同じ値になる(RailSim II の Script.cpp の AsgnFloat 等の fill)。
// キーが無い・値が3つ以上・型が違うときはfalse。
template <typename T, typename Convert>
bool ReadPair(const PiBlock& block, const char* key, Convert convert, std::array<T, 2>& out) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr || assignment->values.empty() || assignment->values.size() > 2) return false;
    if (!convert(assignment->values[0], out[0])) return false;
    if (assignment->values.size() == 2) return convert(assignment->values[1], out[1]);
    out[1] = out[0];
    return true;
}

// 必須の値を1つ読む。
bool ReadRequiredNumber(const PiBlock& block, const char* key, double& out) {
    const PiAssignment* assignment = block.Find(key);
    return assignment != nullptr && assignment->First() != nullptr && ConvertNumber(*assignment->First(), out);
}

bool ReadRequiredVec3(const PiBlock& block, const char* key, rs::core::Vec3& out) {
    const PiAssignment* assignment = block.Find(key);
    return assignment != nullptr && assignment->First() != nullptr && ConvertVec3(*assignment->First(), out);
}

// 必須の文字列を読む。キーが無いか、文字列でなければfalse(空文字は許す)。
bool ReadRequiredString(const PiBlock& block, const char* key, std::string& out) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr || assignment->First() == nullptr ||
        assignment->First()->kind != PiValue::Kind::String)
        return false;
    out = assignment->First()->text;
    return true;
}

// 任意の数値。キーが無い・数値でないときは既定値のまま。
void ReadOptionalNumber(const PiBlock& block, const char* key, double& out) {
    ReadRequiredNumber(block, key, out);
}

} // namespace

void CollectIds(const PiAssignment& a, std::vector<int>& out) {
    for (const PiValue& v : a.values)
        if (v.kind == PiValue::Kind::Number) out.push_back(v.AsInt());
}

bool ParseChangeTexture(const PiAssignment& a, ChangeTextureRef& out) {
    if (a.values.empty() || a.values[0].kind != PiValue::Kind::Number) return false;
    // 非公式・非推奨の旧記法: ChangeTexture = matId, "path", normal; /
    // ChangeTexture = matId, "path", pbr; (第3引数がパスではなく無引用の種別
    // タグ)。現行の4引数形式(materialId, base, normalPath, pbrPath)と混同すると
    // 法線/PBR画像がベースカラーとして誤適用されるため、この記法は無効化する。
    if (a.values.size() == 3 && a.values[2].kind == PiValue::Kind::Ident &&
        (a.values[2].text == "normal" || a.values[2].text == "pbr")) {
        return false;
    }
    out.materialId = a.values[0].AsInt();
    if (a.values.size() > 1) out.base = a.values[1].AsString();
    if (a.values.size() > 2) out.normal = a.values[2].AsString();
    if (a.values.size() > 3) out.pbr = a.values[3].AsString();
    return !out.base.empty();
}

namespace {

bool IsTextureTransformKey(const std::string& key) {
    return key == "ShiftTexture" || key == "ScaleTexture" || key == "RotateTexture" ||
           key == "TransformTexture";
}

// 変換の種別と係数を values[firstParam..] から読む。材質番号付きの文
// (ShiftTexture = matId, du, dv;)と、DefineAnimation 内の材質番号なしの文
// (ShiftTexture = du, dv;)で共通。
bool ReadTransformParams(const std::string& key, const PiAssignment& a, size_t firstParam,
                         TextureTransformCustomizer& out) {
    size_t need = 0;
    if (key == "ShiftTexture") { out.kind = TextureTransformCustomizer::Kind::Shift; need = 2; }
    else if (key == "ScaleTexture") { out.kind = TextureTransformCustomizer::Kind::Scale; need = 4; }
    else if (key == "RotateTexture") { out.kind = TextureTransformCustomizer::Kind::Rotate; need = 3; }
    else if (key == "TransformTexture") { out.kind = TextureTransformCustomizer::Kind::Transform; need = 6; }
    else return false;
    if (a.values.size() < firstParam + need) return false;
    for (size_t i = 0; i < need; ++i)
        out.p[i] = static_cast<float>(a.values[firstParam + i].AsFloat(0.0));
    return true;
}

} // namespace

bool ParseTextureTransform(const std::string& key, const PiAssignment& a,
                           TextureTransformCustomizer& out) {
    if (a.values.empty() || a.values[0].kind != PiValue::Kind::Number) return false;
    out.materialId = a.values[0].AsInt();
    return ReadTransformParams(key, a, 1, out);
}

// ChangeAlpha = matId, (float | DayAlpha | NightAlpha);
// alpha はフォールバック値で、実行時は isDayAlpha/isNightAlpha を見て
// PartMaterialRuntime::Apply が太陽高度から解決する。
bool ParseChangeAlpha(const PiAssignment& a, ChangeAlphaCustomizer& out) {
    if (a.values.size() < 2 || a.values[0].kind != PiValue::Kind::Number) return false;
    out.materialId = a.values[0].AsInt();
    const PiValue& v = a.values[1];
    if (v.kind == PiValue::Kind::Number) {
        out.alpha = static_cast<float>(v.AsFloat(1.0));
    } else if (v.kind == PiValue::Kind::Ident && v.text == "DayAlpha") {
        out.isDayAlpha = true;
        out.alpha = 1.0f;
    } else if (v.kind == PiValue::Kind::Ident && v.text == "NightAlpha") {
        out.isNightAlpha = true;
        out.alpha = 0.0f;
    } else {
        return false;
    }
    return true;
}

void ParseTiming(const PiBlock& b, CustomizerTiming& t) {
    if (const PiAssignment* a = b.Find("PreAnimationDelay"))
        if (a->First()) t.preDelay = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = b.Find("AnimationTime"))
        if (a->First()) t.animTime = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = b.Find("PostAnimationDelay"))
        if (a->First()) t.postDelay = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = b.Find("PreReverseDelay"))
        if (a->First()) t.preRevDelay = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = b.Find("ReverseTime"))
        if (a->First()) t.revTime = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = b.Find("PostReverseDelay"))
        if (a->First()) t.postRevDelay = static_cast<float>(a->First()->AsFloat(0.0));
    if (const PiAssignment* a = b.Find("AnimationEase"))
        if (a->values.size() >= 2) {
            t.easeStart = static_cast<float>(a->values[0].AsFloat(0.0));
            t.easeEnd = static_cast<float>(a->values[1].AsFloat(0.0));
        }
    if (const PiAssignment* a = b.Find("ReverseEase"))
        if (a->values.size() >= 2) {
            t.revEaseStart = static_cast<float>(a->values[0].AsFloat(0.0));
            t.revEaseEnd = static_cast<float>(a->values[1].AsFloat(0.0));
            t.hasReverseEase = true;
        }
}

ChangeMaterialCustomizer ParseChangeMaterial(const PiBlock& b, const TriggerInfo& trig) {
    ChangeMaterialCustomizer cm;
    if (const PiAssignment* a = b.Find("MaterialID"))
        for (const PiValue& v : a->values)
            if (v.kind == PiValue::Kind::Number) cm.materialIds.push_back(v.AsInt());
    if (const PiAssignment* a = b.Find("Diffuse")) {
        cm.hasDiffuse = true;
        for (size_t i = 0; i < a->values.size() && i < 4; ++i)
            cm.diffuse[i] = static_cast<float>(a->values[i].AsFloat(cm.diffuse[i]));
    }
    if (const PiAssignment* a = b.Find("Ambient")) {
        cm.hasAmbient = true;
        for (size_t i = 0; i < a->values.size() && i < 3; ++i)
            cm.ambient[i] = static_cast<float>(a->values[i].AsFloat(cm.ambient[i]));
    }
    // Ambient 省略時は Diffuse から合成せず、ベース材質(.x の Ambient=Diffuse)の
    // 値を維持する。RailSim II は指定された項目だけを上書きする(CCustomizerMisc.cpp:447)。
    if (const PiAssignment* a = b.Find("Specular")) {
        cm.hasSpecular = true;
        for (size_t i = 0; i < a->values.size() && i < 3; ++i)
            cm.specular[i] = static_cast<float>(a->values[i].AsFloat(cm.specular[i]));
    }
    if (const PiAssignment* a = b.Find("Emissive")) {
        cm.hasEmissive = true;
        for (size_t i = 0; i < a->values.size() && i < 3; ++i)
            cm.emissive[i] = static_cast<float>(a->values[i].AsFloat(cm.emissive[i]));
    }
    if (const PiAssignment* a = b.Find("Power")) {
        cm.hasPower = true;
        if (a->First()) cm.power = static_cast<float>(a->First()->AsFloat(0.0));
    }
    cm.trig = trig;
    return cm;
}

bool ParseSoundEffect(const PiBlock& b, const TriggerInfo& trig, SoundEffectRef& out) {
    SoundEffectRef soundEffect;
    if (const PiAssignment* assignment = b.Find("WaveFileName"); assignment != nullptr && assignment->First() != nullptr)
        soundEffect.waveFile = assignment->First()->AsString();
    if (const PiAssignment* assignment = b.Find("AttachObject"); assignment != nullptr && assignment->First() != nullptr)
        soundEffect.attachObject = assignment->First()->AsString();
    if (soundEffect.waveFile.empty() || soundEffect.attachObject.empty()) return false;

    if (const PiAssignment* assignment = b.Find("SourceCoord"); assignment != nullptr && assignment->First() != nullptr)
        assignment->First()->AsVec3(soundEffect.sourceCoord.x, soundEffect.sourceCoord.y,
                                    soundEffect.sourceCoord.z);
    if (const PiAssignment* assignment = b.Find("Volume"); assignment != nullptr && assignment->First() != nullptr)
        soundEffect.volume = assignment->First()->AsInt(0);
    if (const PiAssignment* assignment = b.Find("Loop"); assignment != nullptr && assignment->First() != nullptr)
        soundEffect.loop = assignment->First()->IsYes();
    soundEffect.trig = trig;
    out = std::move(soundEffect);
    return true;
}

bool ParseHeadlight(const PiBlock& b, const TriggerInfo& trig,
                    const std::optional<LensFlareDef>& previousLensFlare, HeadlightDef& out) {
    HeadlightDef headlight;
    if (!ReadRequiredString(b, "AttachObject", headlight.attachObject) || headlight.attachObject.empty())
        return false;
    if (!ReadRequiredVec3(b, "SourceCoord", headlight.sourceCoord)) return false;
    if (!ReadRequiredVec3(b, "Direction", headlight.direction)) return false;
    if (!ReadRequiredNumber(b, "MaxDistance", headlight.maxDistance)) return false;
    if (const PiBlock* lensFlare = b.FirstChild("LensFlare")) {
        Diagnostics ignored;
        headlight.lensFlare = ReadLensFlare(*lensFlare, ignored);
    } else {
        headlight.lensFlare = previousLensFlare;
    }
    headlight.trig = trig;
    out = std::move(headlight);
    return true;
}

bool ParseParticle(const PiBlock& b, const TriggerInfo& trig, ParticleDef& out) {
    ParticleDef particle;
    if (!ReadRequiredString(b, "TextureFileName", particle.textureFileName)) return false;
    if (!ReadRequiredString(b, "AttachObject", particle.attachObject) || particle.attachObject.empty())
        return false;
    if (!ReadRequiredVec3(b, "SourceCoord", particle.sourceCoord)) return false;
    if (!ReadRequiredNumber(b, "MinQty", particle.minQty)) return false;
    particle.maxQty = particle.minQty;  // MaxQty 省略時は MinQty と同じ
    ReadOptionalNumber(b, "MaxQty", particle.maxQty);
    ReadOptionalNumber(b, "VelocityRel", particle.velocityRel);
    ReadOptionalNumber(b, "AccelerationRel", particle.accelerationRel);
    ReadOptionalNumber(b, "DecelerationRel", particle.decelerationRel);

    if (!ReadPair(b, "Lifetime", ConvertNumber, particle.lifetime)) return false;
    if (!ReadPair(b, "Direction", ConvertVec3, particle.direction)) return false;
    if (!ReadPair(b, "InitialRadius", ConvertNumber, particle.initialRadius)) return false;
    if (!ReadPair(b, "FinalRadius", ConvertNumber, particle.finalRadius)) return false;
    if (!ReadPair(b, "Color", ConvertColor, particle.color)) return false;

    // BlendMode は識別子。Alpha / Add 以外は RailSim II でも文法エラー。
    const PiAssignment* blend = b.Find("BlendMode");
    if (blend == nullptr || blend->First() == nullptr || blend->First()->kind != PiValue::Kind::Ident) return false;
    if (blend->First()->text == "Alpha") particle.blendMode = ParticleBlendMode::Alpha;
    else if (blend->First()->text == "Add") particle.blendMode = ParticleBlendMode::Add;
    else return false;

    ReadOptionalNumber(b, "AirResistance", particle.airResistance);
    ReadOptionalNumber(b, "Gravity", particle.gravity);
    ReadOptionalNumber(b, "Turbulence", particle.turbulence);
    particle.trig = trig;
    out = std::move(particle);
    return true;
}

// 未知の frame 種別はエラーにせず無視する。
void ParseAnimationFrame(const std::string& key, const PiAssignment& a,
                         std::vector<AnimationFrame>& out) {
    if (IsTextureTransformKey(key)) {
        // 直前のフレームに付くUV変換(材質番号なし)。直前にフレームが無ければ無視する。
        if (out.empty()) return;
        TextureTransformCustomizer transform;
        if (ReadTransformParams(key, a, 0, transform)) {
            out.back().hasTransform = true;
            out.back().transform = transform;
        }
        return;
    }
    AnimationFrame f;
    if (key == "Frame") {
        // Frame = filename, duration;
        f.kind = AnimationFrame::Kind::Simple;
        if (a.values.size() > 0) f.texturePath = a.values[0].AsString();
        if (a.values.size() > 1) f.durationFrames = a.values[1].AsInt();
    } else if (key == "NumberedFrame") {
        // NumberedFrame = filenameFmt, start, end, duration;
        f.kind = AnimationFrame::Kind::Numbered;
        if (a.values.size() > 0) f.texturePath = a.values[0].AsString();
        if (a.values.size() > 1) f.startNum = a.values[1].AsInt();
        if (a.values.size() > 2) f.endNum = a.values[2].AsInt();
        if (a.values.size() > 3) f.durationFrames = a.values[3].AsInt();
    } else if (key == "SlideUVFrame" || key == "TiledUVFrame" || key == "RotationUVFrame") {
        // <Kind>UVFrame = filename, p0,p1,p2,p3, frameCount, duration;
        f.kind = key == "SlideUVFrame"    ? AnimationFrame::Kind::SlideUV
                : key == "TiledUVFrame"   ? AnimationFrame::Kind::TiledUV
                                          : AnimationFrame::Kind::RotationUV;
        if (a.values.size() > 0) f.texturePath = a.values[0].AsString();
        if (a.values.size() > 1) f.p0 = static_cast<float>(a.values[1].AsFloat(0.0));
        if (a.values.size() > 2) f.p1 = static_cast<float>(a.values[2].AsFloat(0.0));
        if (a.values.size() > 3) f.p2 = static_cast<float>(a.values[3].AsFloat(0.0));
        if (a.values.size() > 4) f.p3 = static_cast<float>(a.values[4].AsFloat(0.0));
        if (a.values.size() > 5) f.frameCount = a.values[5].AsInt();
        if (a.values.size() > 6) f.durationFrames = a.values[6].AsInt();
    } else {
        return; // 未知の frame 種別は無視
    }
    out.push_back(std::move(f));
}

StaticCustomizer ParseCustomizerBlock(const PiBlock& c, const TriggerInfo& trig) {
    StaticCustomizer sc;
    if (c.name == "StaticRotation") sc.kind = StaticCustomizer::Kind::Rotate;
    else if (c.name == "DynamicRotation") sc.kind = StaticCustomizer::Kind::DynamicRotate;
    else sc.kind = StaticCustomizer::Kind::Move;
    if (sc.kind == StaticCustomizer::Kind::Move) {
        if (const PiAssignment* a = c.Find("Displacement"))
            if (const PiValue* v = a->First()) v->AsVec3(sc.dispX, sc.dispY, sc.dispZ);
    } else if (sc.kind == StaticCustomizer::Kind::Rotate) {
        // RotationAxis 省略時は Z 軸(0,0,1)が既定
        sc.axisX = 0; sc.axisY = 0; sc.axisZ = 1;
        if (const PiAssignment* a = c.Find("RotationAxis"))
            if (const PiValue* v = a->First()) v->AsVec3(sc.axisX, sc.axisY, sc.axisZ);
        if (const PiAssignment* a = c.Find("RotationAngle"))
            if (a->First()) sc.angleDeg = static_cast<float>(a->First()->AsFloat(0.0));
    } else {
        // DynamicRotation: RotationAxis(既定Z)、RotationSpeed[rps]→deg/s、
        // Acceleration/Deceleration[rps/s]→deg/s^2（負値は許容しない仕様）。
        sc.axisX = 0; sc.axisY = 0; sc.axisZ = 1;
        if (const PiAssignment* a = c.Find("RotationAxis"))
            if (const PiValue* v = a->First()) v->AsVec3(sc.axisX, sc.axisY, sc.axisZ);
        if (const PiAssignment* a = c.Find("RotationSpeed"))
            if (a->First()) sc.rotationSpeedDegPerSec =
                static_cast<float>(a->First()->AsFloat(0.0)) * 360.0f;
        if (const PiAssignment* a = c.Find("Acceleration"))
            if (a->First()) sc.accelDegPerSec2 =
                static_cast<float>(a->First()->AsFloat(0.0)) * 360.0f;
        if (const PiAssignment* a = c.Find("Deceleration"))
            if (a->First()) sc.decelDegPerSec2 =
                static_cast<float>(a->First()->AsFloat(0.0)) * 360.0f;
    }
    ParseTiming(c, sc.timing);
    sc.trig = trig;
    return sc;
}

} // namespace rs::legacy_pi
