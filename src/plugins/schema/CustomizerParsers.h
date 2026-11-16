//
// CustomizerParsers.h - PIカスタマイザ文の構文パース(SchemaWalk.cppから分離)
//
// ChangeTexture/ChangeAlpha/ChangeMaterial/StaticMove系/DefineAnimationのframe文
// といった、個々のカスタマイザ文をAST(PiAssignment/PiBlock)からCustomizer/Ref構造体
// へ変換する処理を集約する。木構造の走査(walkPart等)には関与しない片方向の
// 依存(SchemaWalk.cpp → ここ)のみを持つ。
//
#pragma once

#include <string>
#include <vector>

#include "PluginSchema.h"

namespace rs::legacy_pi {

// EnvMap = id,id,...; / BumpMap = id,id,...; のような ID リストを収集する。
void CollectIds(const PiAssignment& a, std::vector<int>& out);

// ChangeTexture = id, "base"[, "normal", "pbr"];
bool ParseChangeTexture(const PiAssignment& a, ChangeTextureRef& out);

// texture-transformer: ShiftTexture(3値)/ScaleTexture(5値)/RotateTexture(4値)/
// TransformTexture(7値)。いずれも values[0]=matId(Number)、以降は float 列。
bool ParseTextureTransform(const std::string& key, const PiAssignment& a,
                           TextureTransformCustomizer& out);

// ChangeAlpha = matId, (float | DayAlpha | NightAlpha);
bool ParseChangeAlpha(const PiAssignment& a, ChangeAlphaCustomizer& out);

// StaticMove/StaticRotation 共通のタイミング項目を読む。
void ParseTiming(const PiBlock& b, CustomizerTiming& t);

// ChangeMaterial{ MaterialID=int(,int)*; Diffuse=f,f,f,f(opt); Ambient=f,f,f(opt);
//   Specular=f,f,f(opt); Emissive=f,f,f(opt); Power=f(opt); } をパースする。
// trig は呼び出し側(SchemaWalk.cpp)が現在の発動文脈から組み立てて渡す。
ChangeMaterialCustomizer ParseChangeMaterial(const PiBlock& b, const TriggerInfo& trig);

// SoundEffect{ WaveFileName=...; AttachObject=...; SourceCoord=(x,y,z);
//   Volume=int(opt); Loop=yes/no(opt); } をパースする。
// WaveFileName/AttachObjectが空または無い場合はfalseを返す。
bool ParseSoundEffect(const PiBlock& b, const TriggerInfo& trig, SoundEffectRef& out);

// Headlight{ AttachObject; SourceCoord; Direction; MaxDistance; LensFlare{}(任意) } をパースする。
// 必須の項目(AttachObject/SourceCoord/Direction/MaxDistance)が欠けているか型が違うときはfalse。
// LensFlare内の値の警告は呼び出し元へ返さない(PluginDefinitionに診断の出口が無いため)。
// LensFlare{}を省くと、同じプラグインで直前に読んだ灯具のLensFlareを引き継ぐ(previousLensFlare。
// 直前が無ければ空のまま)。
bool ParseHeadlight(const PiBlock& b, const TriggerInfo& trig,
                    const std::optional<LensFlareDef>& previousLensFlare, HeadlightDef& out);

// Particle{ ... } をパースする(項目は EffectorSchema.h の ParticleDef)。
// 必須の項目が欠けているか型が違うとき、BlendModeがAlpha/Add以外のときはfalse。
// 値は書かれた単位のまま持つ。組の値は2つ目を省くと1つ目と同じになる。
bool ParseParticle(const PiBlock& b, const TriggerInfo& trig, ParticleDef& out);

// DefineAnimation "name"{ frame文* } の各 frame 文を AnimationFrame にパースする。
// 未知の frame 種別はエラーにせず無視する。ShiftTexture/ScaleTexture/RotateTexture/
// TransformTexture(材質番号なし)は直前のフレームのUV変換になる(直前が無ければ無視)。
void ParseAnimationFrame(const std::string& key, const PiAssignment& a,
                         std::vector<AnimationFrame>& out);

// TrackWind{ TrackSpeed; FixAxis(任意) } をパースする。読むだけで実行時には使わない。
// TrackSpeed が無いか型が違う、FixAxis が3値でないときはfalse。FixAxis は長さ1に正規化して持つ。
bool ParseWindTracker(const PiBlock& b, const TriggerInfo& trig, WindTrackerCustomizer& out);

// Windmill{ Directional(任意); RotationAxis(任意); RotationSpeed; Symmetric } をパースする。読むだけで実行時には使わない。
// RotationSpeed・Symmetric が無いか型が違う、Directional が yes/no でない、RotationAxis が3値でないときはfalse。
// Symmetric は 1 未満を 1 に直す。
bool ParseWindmill(const PiBlock& b, const TriggerInfo& trig, WindmillCustomizer& out);

// TrackingMove{ TrackingID; AscentRange; SliderRange(任意); Displacement; タイミング } をパースする。読むだけで実行時には使わない。
// TrackingID(文字列)・AscentRange・Displacement(3値)が無いか型が違う、SliderRange が実数でないときはfalse。
// SliderRange は半分にして持つ(省略は -1)。
bool ParseTrackingMove(const PiBlock& b, const TriggerInfo& trig, TrackingMoveCustomizer& out);

// TrackingRotation{ TrackingID; RotationRadius; SliderRange; RotationAxis; RotationAngle; タイミング } をパースする。
// TrackingID が無いか文字列でない、ほかの項目が書かれていて型が違うときはfalse。TrackingID 以外は省略可(既定値)。
// RotationAngle は度で書かれた値をラジアンに直して持つ。
bool ParseTrackingRotation(const PiBlock& b, const TriggerInfo& trig, TrackingRotationCustomizer& out);

// StaticMove/StaticRotation/DynamicRotation ブロック単体を StaticCustomizer にパースする。
StaticCustomizer ParseCustomizerBlock(const PiBlock& c, const TriggerInfo& trig);

} // namespace rs::legacy_pi
