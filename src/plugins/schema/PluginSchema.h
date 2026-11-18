//
// PluginSchema.h - PI プラグイン定義（AST）→ 型付き/汎用スキーマ
//
// 既存の PI トークナイザ/パーサ（lexer/parser）の上に載る薄いスキーマ層。
// 目的（モデル/テクスチャ参照の解決と表示）に必要な範囲を抽出する:
//   - PluginHeader（種別・名前・作者・アイコン等）
//   - Train: TrainInfo / DefineSwitch / DefineAnimation / PrimaryAssembly
//   - Rail : RailInfo / SoundInfo / Profile
//   - その他種別: 汎用ウォークで ModelFileName/ChangeModel と ChangeTexture/
//     EnvMap/BumpMap を抽出（未知種別でも最低限モデル+テクスチャを表示可能に）
//
// 要素の型は PluginHeader.h・SwitchDef.h・PartCustomizerSchema.h・RailProfileSchema.h・LineSchema.h にあり、
// ここでまとめて読み込む。定義全体を使わない側は、要素の型のヘッダだけを読む。
//
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "parser/Ast.h"
#include "SwitchExpr.h"
#include "TriggerInfo.h"
#include "EffectorSchema.h"
#include "WindCustomizerSchema.h"
#include "TrackingCustomizerSchema.h"
#include "CustomizerTiming.h"
#include "../../core/Math.h"
#include "LineSchema.h"
#include "PartCustomizerSchema.h"
#include "PluginHeader.h"
#include "RailProfileSchema.h"
#include "SwitchDef.h"

namespace rs::legacy_pi {

// 1 モデル部品（Axle/Body/Object3D/Struct の Main 等）
struct ModelPart {
    std::string kind;      // "Axle" / "Body" / "Object3D" / "Object" 等の元ブロック名
    std::string label;     // 部品名（Body "Bogie1" の "Bogie1"）
    std::string modelFile; // 未解決相対パス（ModelFileName または ChangeModel の第1引数）
    float       modelScale = 1.0f;
    // CastShadow = yes/no。書かれていなければ castShadowSpecified=false で、
    // BuildPluginDefinition が種別の既定(Surface のみ no)を castShadow に入れる。
    // false のパーツは全材質が影を落とさない。
    bool        castShadowSpecified = false;
    bool        castShadow = true;
    // resolveChangeModel() が modelFile/modelScale を書き換える直前の値
    // （= ModelFileName/ModelScale の素の値）。ChangeModel の実行時動的解決
    // （PartDynamicModel）が「候補どれもactiveでない」場合のベースとして使う。
    std::string baseModelFile;
    float       baseModelScale = 1.0f;
    float       diameter = 0.0f; // Axle のみ: 車輪直径[m]（Diameter, 0=非回転）
    int         symmetric = 1;   // Axle のみ: N角形近似の対称数（Symmetric, 逆回転見え防止, 既定1）
    bool        wheelSound = true; // Axle のみ: レールの継目で車輪音を鳴らすか（WheelSound, 省略時yes）
    TiltInfo    tilt;             // Body のみ: 振り子傾斜（Tilt ブロック）

    // このパーツに適用される PBR / テクスチャ差し替え情報
    std::vector<ChangeModelRef>   changeModels; // ChangeModel 候補（宣言順）
    // ChangeTexture / SetAnimation / ShiftTexture系を、種類をまたいで宣言順に並べた1本のリスト。
    // 同じ材質ではあとの宣言が勝つ。追加と走査は下の補助を使う。
    std::vector<TextureStep>      textureSteps;
    // EnvMap / BumpMap = id列;（-k-build: 法線/ORMを使う材質）。発動条件つきで宣言順に並べる。
    std::vector<MaterialFlagCustomizer> envMaps;
    std::vector<MaterialFlagCustomizer> bumpMaps;
    // AlphaZeroTest（二値抜き強制）/ Transparent（描画順を最後段へ強制。影も落とさず受けない）/
    // NoCastShadow・NoShadow（影を落とさない）/ NoReceiveShadow・NoShadow（影を受けない）= id列;。
    // 発動条件つきで宣言順に並べる。NoShadow は同じ条件で noCastShadows と noReceiveShadows の両方へ入る。
    std::vector<MaterialFlagCustomizer> alphaZeroTests;
    std::vector<MaterialFlagCustomizer> transparents;
    std::vector<MaterialFlagCustomizer> noCastShadows;
    std::vector<MaterialFlagCustomizer> noReceiveShadows;
    std::vector<ChangeMaterialCustomizer> changeMaterials;
    std::vector<ChangeAlphaCustomizer>      changeAlphas;

    // --- PrimaryAssembly 内での組立配置 ---
    bool                  turn = false;      // Turn = yes（Y 軸 180° 回転）
    bool                  hasCoord = false;  // Coord = (z, y) を持つ（Axle）
    float                 coordZ = 0, coordY = 0;
    std::vector<JointRef> joints;            // JointZY 接続
    bool                  placeResolved = false; // 配置解決済みか
    float                 offsetZ = 0, offsetY = 0; // 解決済み絶対オフセット

    // ZY系の可動部（ObjectZY/TriangleZY/PistonZY/CrankZY のパーツ。Axle/Body/Object3D とは別扱い）。
    // isZyPart==true のときのみ zyInfo が有効。placeResolved3d を共用し
    // (resolveJoint3D/ResolveDynamicPartWorlds のどちらも同じフラグで完了判定する)、
    // joints3d は使わない(空のまま)。
    bool           isZyPart = false;
    ZyPartInfo     zyInfo;

    std::vector<Joint3DRef> joints3d;        // Joint3D 接続（Train の JointZY と共存）
    bool                     placeResolved3d = false; // Joint3D 配置解決済みか
    float                    worldMat[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                                              // 解決済みワールド行列（row-major, v'=v*M）
    // Joint3D の target/DirLink/UpLink が解釈できない構文を
    // 指しており、名前解決が恒等フォールバックになっている（真の姿勢が不明）か。
    // resolveJoint3D が解決前に一度だけ判定して立てる。true の場合、このパーツの
    // StaticRotation/StaticMove は適用しない（誤った基準姿勢に回転を上乗せして
    // 誤表示するより無回転の方が安全なため、DynamicJointPlacement.cpp 参照）。
    bool                     hasUnresolvedJointRef = false;

    // StaticMove / StaticRotation 可動部(パーツ直下 / ApplySwitch Case 配下 /
    // If 条件ブロック配下の3文脈すべてを宣言順に格納)
    std::vector<StaticCustomizer> customizers;

    // TrackWind / Windmill(読むだけ。可動部の実行時は参照しない)。customizers とは別に持つ。
    std::vector<WindTrackerCustomizer> windTrackers;
    std::vector<WindmillCustomizer>    windmills;

    // 追従の幾何条件。TrackingMove は customizers から添字で参照する。
    std::vector<TrackingMoveCustomizer>     trackingMoves;
    std::vector<TrackingRotationCustomizer> trackingRotations;

    void AddChangeTexture(ChangeTextureRef value);
    void AddSetAnimation(SetAnimationCustomizer value);
    void AddTextureTransform(TextureTransformCustomizer value);

    // textureSteps のうち ChangeTexture の数。平行配列(解決済みパス等)の長さに使う。
    size_t ChangeTextureCount() const;

    // 種類で絞って宣言順に走査する。ChangeTexture の index は「ChangeTexture だけを
    // 宣言順に数えた番号」で、平行配列の添字と一致する。
    // f の形: ForEachChangeTexture は f(size_t index, const ChangeTextureRef&)、
    // ForEachSetAnimation / ForEachTextureTransform は f(const T&)。
    template <class F> void ForEachChangeTexture(F&& f) const {
        size_t index = 0;
        for (const TextureStep& step : textureSteps) {
            if (step.kind == TextureStep::Kind::ChangeTexture) f(index++, step.changeTexture);
        }
    }
    template <class F> void ForEachSetAnimation(F&& f) const {
        for (const TextureStep& step : textureSteps) {
            if (step.kind == TextureStep::Kind::SetAnimation) f(step.setAnimation);
        }
    }
    template <class F> void ForEachTextureTransform(F&& f) const {
        for (const TextureStep& step : textureSteps) {
            if (step.kind == TextureStep::Kind::Transform) f(step.transform);
        }
    }
};


// プラグイン定義(ModelPart群のchangeModels/textureSteps/changeMaterials/
// changeAlphas/envMaps/bumpMaps/alphaZeroTests/transparents/noCastShadows/noReceiveShadows/customizers、および
// PluginDefinition::soundEffects[].trig)が持つ全トリガーを横断的に走査し、
// names のいずれかを参照するトリガーが1つでもあれば true を返す。
// PluginSession::IsFullyStatic()が「_CAMDIST等の高頻度変数に依存するプラグインを
// 誤って静的扱いする」問題を避けるための判定に使う。
struct PluginDefinition;
bool DefinitionReferencesAnySwitch(const PluginDefinition& def, const std::vector<std::string>& names);

// ModelPart::changeModels を発動文脈(SwitchResolver)で解決した結果。
struct ChangeModelResolveResult {
    int   variantIndex = -1;  // -1 = ModelFileName(baseModelFile)、i>=0 = changeModels[i]
    float modelScale = 1.0f;
};

// changeModels を宣言順に走査し、有効な1件（modelFile/modelScale の組）を
// 解決する。IsTriggerActive で各候補の trig を評価し、**最初にactiveだったもの**を
// 採用する（先勝ち。RailSim II の CCustomizerSwitchEntry::GetMesh /
// CCustomizerSwitchApplier::GetMeshCustomizer が候補を宣言順に見て
// `if(*asc>=0.0f) return tm;` と即returnする挙動に合わせる。ChangeModel は
// スケール引数必須＝activeなら必ず asc>=0 になるため、後続候補は評価されない)。
// これにより `If SW==0 { ChangeModel="",1.0; }` で消してから後続の ApplySwitch で
// 実モデルを指定する記法が、RailSim II と同様に「消える」側に確定する。
// resolve に「常に0を返す」関数を渡すと resolveChangeModel()（ロード時、
// 全スイッチ初期値0）と同じ結果になる（ロード時/実行時のロジック不一致による
// チラつきを防ぐため、両者はこの関数を共有する）。
ChangeModelResolveResult ResolveChangeModel(const ModelPart& part,
                                            const SwitchResolver& resolve);

// Joint3D の基準名が _LOCAL/_WORLD/_CAMERA/_LIGHT のいずれか(恒等行列扱いして
// よい特殊名)かどうか。パース時(resolveJoint3D)・実行時(毎フレームの動的階層
// 解決)の双方で同じ特殊名判定を使うための共有関数。
bool IsSystemJointTargetName(const std::string& name);

// Joint3D 1件分のワールド行列を、基準フレーム行列(dirFrame/upFrame/targetFrame、
// いずれも row-major row-vector 規約の 4x4 を16要素配列で表現)から計算する。
// dir=normalize(AttachDir), right=normalize(cross(up,dir)), up'=cross(dir,right)
// という正規直交基底を基準フレームのローカル軸として組み立てる。
// パース時(SchemaPlacement.cpp::resolveJoint3D、静的1回解決)と実行時
// (DynamicJointPlacement.cpp、可動部込みで毎フレーム再解決)の双方が同じ幾何
// 計算を共有するための公開関数(ロジックの重複/乖離防止)。
void ComposeJoint3DWorld(const Joint3DRef& j, const float dirFrame[16],
                         const float upFrame[16], const float targetFrame[16],
                         float outWorld[16]);

// TrainInfo（Train種別のみ）: 編成連結・走行諸元（Help pi_train）。
// 物理値は基準30FPSで実時間に対し見かけ通りになるよう正規化されている
// （シミュ時間軸の値ではない点に注意）。
struct TrainInfo {
    bool  hasTrainInfo = false;
    float frontLimit = 0, tailLimit = 0;             // 連結位置[m]（front=+Z, tail<front）
    float maxVelocity = 0;                           // [km/h]
    float maxAcceleration = 0, maxDeceleration = 0;   // [(km/h)/s]
    float tiltSpeed = 0;                              // 振り子応答速度[0..1]
    float doorClosingTime = 0;                        // [s]

    // FrontCabin{ Joint3D "対象" {...} } / TailCabin{ Joint3D "対象" {...} }
    // (RailSim II の CTrainPlugin::m_FrontCabin/
    // m_TailCabin と同形式、Joint3DRefを再利用する)。未指定(has*Cabin==false)の場合、
    // フォールバック値は ResolveCabinFallback が埋める。
    bool       hasFrontCabin = false, hasTailCabin = false;
    Joint3DRef frontCabin, tailCabin;
};


// Pier(橋脚)の Joint/Head/Base 各パーツ参照。
// localOffsetY は "*ToPierLocal"/"*ToHeadLocal" 等のY成分で、
// 積み上げ方向のオフセット(実データの観察による解釈)。
struct PierPartRef {
    std::string modelFile;
    float       modelScale = 1.0f;
    float       localOffsetY = 0.0f;
};

// Pier 固有定義（あれば）
struct PierDef {
    bool        hasJoint = false, hasHead = false, hasBase = false;
    PierPartRef joint, head, base;
    float       interval = 20.0f;    // 橋脚間隔[m]（単体表示では未使用）
    float       buildMinAlt = 0.0f;
    int         trackNum = 1;  // TrackNum: この橋脚が跨ぐ並走軌道数
    // TrackInterval[m]: 並走軌道間隔。形や位置には使わず、敷設設定との食い違いの警告と説明文にだけ使う。
    float       trackInterval = 0.0f;
    // Direction=Up: 断面の始点が地面側・終点が上端。既定(Down)は始点が上端・終点が地面側。
    bool        directionUp = false;
    // Offset[m]: 端点から最初の橋脚までの距離。
    float       offset = 0.0f;
    // TaperX/Y/Z: 橋脚の長さ1mあたりの、地面側の断面(X・Y)と間隔(Z)の広がり。負の値は0にする。
    float       taperX = 0.0f, taperY = 0.0f, taperZ = 0.0f;
};


// Rail の SoundInfo{ WheelSoundFile; JointInterval; }。車軸が継目を通るときの車輪音の定義。
struct RailSoundDef {
    bool hasSoundInfo = false;
    std::string wheelSoundFile; // 未解決相対パス（空なら鳴らさない）
    float jointInterval = 0.0f; // 継目の間隔[m]（0以下は鳴らさない）
};


// FlagZY{ ... }(旗の布シミュレーション用の自由オブジェクト)。物理も描画も行わず、
// 読み込んだ値を保持するだけ。Gravity は読んだ値に 1/60 を掛けた値。
struct FlagZyDef {
    std::string label;
    float stiffness = 1.0f;
    float airResistance = 0.75f;
    float gravity = 0.0f;
};

// StretchZY "名"{ Profile{}; Line*; JointZYX "n"{} ×2 }(2つの物体の間に断面を引き伸ばす自由オブジェクト)。
// 読み込んだ値を保持するだけで、描画も追従もしない。Line が1本も無いときは、
// Coord=(0,0,0) と (0,0,1) の2点の Line を1本入れる。
struct StretchZyDef {
    std::string                   label;
    RailProfile                   profile;
    std::vector<LineWirePolyline> lines;
    JointZYXRef                   joints[2];
    int                           jointCount = 0; // 有効な joints の数(最大2)
};

struct PlatformDef {
    int trackNum = 1;
    bool stoppable = true;
    bool openDoor[2] = {false, false};
    std::string railPlugin;
    std::string tiePlugin;
    std::string girderPlugin;
    std::string pierPlugin;
    std::string linePlugin;
    std::string polePlugin;
    bool liftRailSurface = true;
    bool enableCant = true;
    std::string parentObject; // ParentObject(省略時は空)
    // parentObject を FindPlatformParentPart で解決した PluginDefinition::parts の添字。
    // 省略時と未解決は -1。BuildPluginDefinition が1回だけ解決する。
    int parentPartIndex = -1;
    // parentObject が指定されているのに、駅プラグイン内に該当するパーツが無い(RailSim II では UndefinedObject)。
    // 真の番線はレールを作らない。
    bool parentUndefined = false;
    std::vector<rs::core::Vec3> coords;
};

struct PluginDefinition {
    PluginHeader           header;
    std::vector<SwitchDef> switches;
    std::vector<ModelPart> parts; // 表示対象モデル部品（全種別共通）
    std::vector<SoundEffectRef> soundEffects; // PrimaryAssembly直下のSoundEffect（全種別共通）
    std::vector<HeadlightDef> headlights;     // 同・Headlight(灯具)
    std::vector<ParticleDef>  particles;      // 同・Particle
    std::vector<RailEffectorDef> railEffectors; // 同・ConnectRail/BranchRail/DisconnectRail(読むだけ)
    std::vector<AnimationDef> animations; // トップレベル DefineAnimation 定義

    // Train 固有（あれば）
    TrainInfo trainInfo;

    // Rail/Tie/Girder 共通（あれば。PI上のブロック名はRailInfo/TieInfo/GirderInfoと
    // 種別ごとに異なるが、意味的には同じ押し出し情報ブロックとして扱う）
    bool                                  hasRailInfo = false;
    float                                 gauge = 1.435f;
    float                                 cantRatio = 0.0f;   // [deg・m] カント比例定数
    float                                 maxCant = 0.0f;     // [deg] カント最大角
    bool                                  flattenCant = false;// 下層へカント傾斜を伝えない
    float                                 railHeight = 0.0f;  // Height
    float                                 surfaceAlt = 0.0f;  // SurfaceAlt
    std::vector<RailProfile>              railProfiles;
    std::vector<RailInterval>             intervals; // Tie 等: Interval 反復配置
    int                                   trackNum = 1;  // TrackNum: Girder(桁)が跨ぐ並走軌道数
    float                                 trackInterval = 0.0f; // TrackInterval[m]: Girderの並走軌道間隔(警告と説明文だけに使う)

    // Surface 固有（あれば）: SurfaceInfo{ SizeX; SizeZ; } [m]。原点中心のXZ長方形の全幅。
    bool  hasSurfaceInfo = false;
    float surfaceSizeX = 0.0f;
    float surfaceSizeZ = 0.0f;

    // Pier 固有（あれば）
    PierDef pier;

    // Line/Pole 固有（あれば）
    LineDef line;
    PoleDef pole;
    // Rail 固有（あれば）
    RailSoundDef railSound;
    std::vector<PlatformDef> platforms;

    // Struct 固有（あれば）
    std::vector<LinePointDef> linePoints;
    // 自由オブジェクトの FlagZY / StretchZY（読み込むだけ）
    std::vector<FlagZyDef> flagZys;
    std::vector<StretchZyDef> stretchZys;

    // パーツ収集（collectParts）・Joint3D 配置解決（resolveJoint3D）で出た警告
    // （重複ラベル/未知参照/循環参照等）と、Platform の ParentObject が未定義の警告
    std::vector<std::string> joint3dWarnings;

    // 元 AST（詳細アクセス用に保持）
    PiDocument doc;
};

// Platform の ParentObject の名前に一致するラベルを持つパーツの添字(定義順で最初の一致)。
// 名前が空、または一致するパーツが無いときは -1。
int FindPlatformParentPart(const std::vector<ModelPart>& parts, const std::string& parentObject);

// PluginHeaderブロック1つからヘッダの6項目を読む(ヘッダの読み方はここ1か所)。
PluginHeader BuildPluginHeader(const PiBlock& node);

// AST から PluginDefinition を構築する。未知種別でも parts を汎用抽出する。
PluginDefinition BuildPluginDefinition(PiDocument doc);

} // namespace rs::legacy_pi
