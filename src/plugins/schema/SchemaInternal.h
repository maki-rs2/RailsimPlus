//
// SchemaInternal.h - plugin_schema の内部分割ファイル間で共有する宣言（非公開）
//
// BuildPluginDefinition（PluginSchema.cpp）が、パート走査（SchemaWalk.cpp）と
// 配置/ジョイント解決（SchemaPlacement.cpp）へ処理を委譲するための橋渡し。
// 公開 API ではないため rs::legacy_pi::schema_detail に閉じ込める。
//
#pragma once

#include <string>
#include <vector>

#include "PluginSchema.h"

namespace rs::legacy_pi::schema_detail {

// --- SchemaWalk.cpp（パート/カスタマイザ走査） ---

// トップレベル DefineAnimation "name"{...} を AnimationDef にパースする。
AnimationDef ParseDefineAnimation(const PiBlock& node);
PlatformDef ParsePlatformDef(const PiBlock& node);

// PrimaryAssembly やトップレベル container からパーツを収集し out へ追記する。
// 各パーツで walkPart（モデル参照/カスタマイザ）+ extractPlacement（配置情報）を行う。
// effectorLabels: エフェクタ(SoundEffect/Headlight/Particle)のAttachObjectが指すラベルの一覧
// (collectEffectorsで集めたものから呼び出し側が作って渡す)。モデル無しの位置アンカー
// （Joint3D同様、参照されているダミー)を誤って捨てないための参照集合に使う。
void CollectParts(const PiBlock& container, const std::vector<std::string>& effectorLabels,
                   std::vector<std::string>& warnings,
                   std::vector<ModelPart>& out);

// container 直下の FlagZY を読む(物理も描画もしない。値の保持だけ)。
void CollectFlagZys(const PiBlock& container, std::vector<FlagZyDef>& out);

// container 直下の StretchZY を読む(描画も追従もしない。値の保持だけ)。SchemaProfile.cpp。
void CollectStretchZys(const PiBlock& container, std::vector<StretchZyDef>& out);

// --- SchemaProfile.cpp（Profile / Wiredmesh / Line の読み取り） ---

// Profile{}/Wiredmesh{}共通のMaterial{ TexFileName(1〜3値); TexVPerMeter; UseTexture; BumpMap; EnvMap;
// AlphaZeroTest; CastShadow }読み取り。
void ParseProfileMaterial(const PiBlock& node, std::string& texFileName, float& texVPerMeter,
                          bool& useTexture, ProfileMaterialExtra& extra);
// Profile{}/Wiredmesh{}共通のFace{ Vertex{} }読み取り。Normal省略時は隣の辺から作る。
std::vector<RailProfileFace> ParseProfileFaces(const PiBlock& node);
// Profile{ MinRadius; MaxRadius; Material{}; Face{} } 1ブロックを読む。
RailProfile BuildRailProfile(const PiBlock& node);
// Wireframe{}/Wiredmesh{}/StretchZY{}共通のLine "名"{ Vertex{} }読み取り。
// 代表色は先頭に見つかったVertex.Diffuseを使う(使わない側は捨ててよい)。
void ParseWireLines(const PiBlock& node, std::vector<LineWirePolyline>& outLines, float& diffuseR,
                    float& diffuseG, float& diffuseB, float& diffuseA);

// JointZYX "取り付け先"{ AttachX; AttachCoord; LocalCoord } 1ブロック分(SchemaPartExtract.cpp)。
JointZYXRef ParseJointZYX(const PiBlock& j);

// Struct 最上位の LinePoint{ Coord; Dir } 1ブロックを読む。
LinePointDef ParseLinePoint(const PiBlock& node);

// PrimaryAssembly直下のエフェクタ(SoundEffect/Headlight/Particle)の収集結果。
struct EffectorCollection {
    std::vector<SoundEffectRef> soundEffects;
    std::vector<HeadlightDef>   headlights;
    std::vector<ParticleDef>    particles;
    std::vector<RailEffectorDef> railEffectors;
};

// PrimaryAssembly直下のエフェクタをIf/Else/ApplySwitchの発動文脈と共に収集する。
void CollectEffectors(const PiBlock& container, EffectorCollection& out);

// 収集済みエフェクタ3種類のAttachObjectのラベルをまとめて返す(重複あり)。collectPartsへ渡す。
std::vector<std::string> CollectEffectorLabels(const EffectorCollection& effectors);

// --- SchemaInfoBlocks.cpp（トップレベルの情報ブロックの読み取り） ---

// キーの値を文字列で返す。キーが無い/値が無いときは空文字列。
std::string StrAssign(const PiBlock& b, const char* key);

// 以下は node（ブロック名で振り分け済み）の値を def の該当フィールドへ書く。
void ReadSwitchDef(const PiBlock& node, PluginDefinition& def);
void ReadTrainInfo(const PiBlock& node, PluginDefinition& def);
// RailInfo/TieInfo/GirderInfo の3種を受ける。
void ReadRailInfo(const PiBlock& node, PluginDefinition& def);
void ReadSurfaceInfo(const PiBlock& node, PluginDefinition& def);
void ReadInterval(const PiBlock& node, PluginDefinition& def);
void ReadPierInfo(const PiBlock& node, PluginDefinition& def);
void ReadLineInfo(const PiBlock& node, PluginDefinition& def);
void ReadPoleInfo(const PiBlock& node, PluginDefinition& def);
void ReadSoundInfo(const PiBlock& node, PluginDefinition& def);
// Joint/Head/Base（node.nameで振り分ける）。
void ReadPierPart(const PiBlock& node, PluginDefinition& def);

// --- SchemaPartExtract.cpp（組立配置情報の抽出） ---

} // namespace rs::legacy_pi::schema_detail

namespace rs::legacy_pi {

// 色リテラル("RRGGBB"/"AARRGGBB"、'#'除く)をRGBA float[0,1]へ変換する(PluginSchema.cpp)。
// 桁数が不正ならfalse(出力は暗い灰色)。Wireframe/Wiredmeshの頂点色とEnvの色指定が共用する。
bool ParseArgbHex(const std::string& hex, float& r, float& g, float& b, float& a);

// パーツ直下の組立配置情報（Coord/Turn/Diameter/Symmetric/Tilt/JointZY/Joint3D）を
// 抽出する。collectParts（SchemaWalk.cpp、schema_detail 名前空間）から呼ばれるため
// schema_detail の外（rs::legacy_pi 直下）で宣言する。
void ExtractPlacement(const PiBlock& b, ModelPart& p);

// ZY系のパーツ(ObjectZY / TriangleZY・PistonZY・CrankZY の Link・Slide)のブロック b から
// JointZYX・LinkCoord・Direction・FixPosition・FixRight・Turn を抽出する。
// memberIndex は組の中の並び(0/1)。collectParts（SchemaParts.cpp）から呼ばれる。
void ExtractZyPartInfo(const PiBlock& b, ZyPartKind kind, int memberIndex, ModelPart& p);

// Joint3D "基準名" {...} 1ブロック分をJoint3DRefへパースする（extractPlacementの
// パーツ直下Joint3D抽出と、トップレベルFrontCabin/TailCabin{ Joint3D{...} }の
// 双方が共有する。PluginSchema.cpp参照）。
void ParseJoint3DBlock(const PiBlock& c, Joint3DRef& out);

// FrontCabin{ Joint3D "対象" {...} } / TailCabin{ Joint3D "対象" {...} } を
// パースする。ネストしたJoint3Dブロックが無ければoutは変更せずfalseを返す
// （呼び出し側はhasFrontCabin/hasTailCabinをfalseのままにする）。
bool ParseCabinBlock(const PiBlock& node, Joint3DRef& out);

// FrontCabin/TailCabinブロックが省略されているとき、既定値相当の
// Joint3DRefを合成してhasFrontCabin/hasTailCabinを立てる。既に明示されているcabinは
// 変更しない。partsから最初のBodyパーツをアタッチ先に使う（見つからなければ
// 何もしない=has*Cabinはfalseのまま）。BuildPluginDefinitionの末尾、parts収集後に呼ぶ。
void ResolveCabinFallback(TrainInfo& trainInfo, const std::vector<ModelPart>& parts);

// パート（Axle/Body/Object3D 等）とその配下を再帰的に走査し、モデル参照と
// テクスチャ差し替えを収集する薄いラッパ（発動文脈はAlways=空chainから開始）。
// collectParts/collectZyGroup（SchemaParts.cpp）から呼ばれる。
void WalkPart(const PiBlock& b, ModelPart& part);

} // namespace rs::legacy_pi

namespace rs::legacy_pi::schema_detail {

// --- SchemaPlacement.cpp（配置/ジョイント解決） ---

// JointZY/Coord 系の組立座標（offsetZ/offsetY/turn）を反復解決する。
void ResolvePlacements(std::vector<ModelPart>& parts);

// Joint3D 配置（ワールド行列 worldMat）を反復解決する。resolvePlacements の後に呼ぶ。
void ResolveJoint3D(std::vector<ModelPart>& parts, std::vector<std::string>& warnings);

} // namespace rs::legacy_pi::schema_detail
