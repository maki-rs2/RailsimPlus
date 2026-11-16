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

namespace rs::legacy_pi {

enum class PluginType {
    Unknown, Train, Rail, Tie, Girder, Pier, Pole,
    Station, Struct, Surface, Line, Env, Skin
};

const char* PluginTypeName(PluginType t);
PluginType  PluginTypeFromString(const std::string& s);

struct PluginHeader {
    std::string railSimVersion;
    PluginType  type = PluginType::Unknown;
    std::string typeRaw;     // 元の PluginType 文字列
    std::string name;
    std::string author;
    std::string iconTexture; // 未解決の相対パス
    // IconRect = u, v, 幅, 高さ。IconTexture のうちアイコンに使う範囲(0〜1の比率)。省略時は全体。
    // 4値そろわないときは省略扱い(既定値のまま)。
    float       iconRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    std::string description;
};

// JointZY 接続: 子パーツ（Axle/Body、ラベル参照）への ZY 平面ジョイント。
// このパーツの配置は 子位置 + AttachCoord − R(LocalCoord) で決まる（R は Turn 回転）。
struct JointRef {
    std::string childLabel;         // 接続先パーツのラベル
    float attachZ = 0, attachY = 0; // 子フレーム上の接続点
    float localZ = 0, localY = 0;   // 自フレーム上の接続点
};

// SoundEffect{}: PI内に直接記述される固定音源の単発/ループ再生設定。
// AttachObjectはこの段階では未解決の文字列参照として保持する。
struct SoundEffectRef {
    std::string waveFile;      // WaveFileName（未解決の相対パス）
    std::string attachObject;  // AttachObject（同一アセンブリ内のObject3D名参照）
    rs::core::Vec3 sourceCoord{0, 0, 0}; // SourceCoord（AttachObjectのローカル座標）
    int volume = 0;            // Volume（1/100dB、既定0）
    bool loop = true;          // Loop（既定yes）
    TriggerInfo trig;          // 発動文脈（既定Always=空chain）
};

// StaticMove / StaticRotation / DynamicRotation / AnalogClock カスタマイザ(可動部)
struct StaticCustomizer {
    enum class Kind { Move, Rotate, DynamicRotate, AnalogClock };
    Kind  kind = Kind::Move;
    float dispX = 0, dispY = 0, dispZ = 0;   // Move: Displacement
    float axisX = 0, axisY = 0, axisZ = 1;   // Rotate/DynamicRotate: RotationAxis(既定Z)
    float angleDeg = 0;                      // Rotate: RotationAngle
    // DynamicRotation: 常時回転(車輪/ファン等)。RotationSpeed[rps]→deg/s に正規化して保持。
    // Acceleration/Deceleration[rps/s]も deg/s^2 に正規化(0=即時)。
    float rotationSpeedDegPerSec = 0;
    float accelDegPerSec2 = 0;
    float decelDegPerSec2 = 0;
    // AnalogClock: 針種別(RailSim II の CAnalogClock::m_HandType相当)。0=Hour/1=Minute/2=Second。
    int clockHandType = 0;
    CustomizerTiming timing;
    TriggerInfo trig; // 発動文脈（既定 Always=空chain）。ChangeTextureRef等と同形。
};

// ChangeModel = "path"[, scale]; の候補（発動文脈付き）。
// パーツ走査中は上書きせず候補として収集し、走査後にスイッチ初期値で
// Case 一致/Default 判定して有効な1件を modelFile へ反映する。
struct ChangeModelRef {
    std::string modelFile;
    float       modelScale = 1.0f;
    bool        hasScale = false; // 第2引数の有無（無指定は現在値を維持）
    TriggerInfo trig;
};

// -k-build の PBR: ChangeTexture = id, base[, normal, pbr];
struct ChangeTextureRef {
    int         materialId = 0;
    std::string base;    // 未解決相対パス
    std::string normal;  // 空可
    std::string pbr;     // 空可（ORM パック）
    TriggerInfo trig;    // 発動文脈（既定 Always）
};

// ChangeMaterial{ MaterialID=int(,int)*; Diffuse=f,f,f,f(opt); Ambient=f,f,f(opt);
//   Specular=f,f,f(opt); Emissive=f,f,f(opt); Power=f(opt); }
// 省略項目は .x のマテリアル値を使う（has* フラグで区別）。
struct ChangeMaterialCustomizer {
    std::vector<int> materialIds;
    bool  hasDiffuse = false;  float diffuse[4]  = {0.8f, 0.8f, 0.8f, 1.0f};
    bool  hasAmbient = false;  float ambient[3]  = {0.8f, 0.8f, 0.8f};
    bool  hasSpecular = false; float specular[3] = {0, 0, 0};
    bool  hasEmissive = false; float emissive[3] = {0, 0, 0};
    bool  hasPower = false;    float power = 0.0f;
    TriggerInfo trig;
};

// texture-transformer(Help pi_sym_texture_transformer.html): マテリアル単位の
// UV 2x3アフィン変換を即値で指定する4種。DefineAnimationを介さない静的版
// （AnimationFrame の SlideUV/TiledUV/RotationUV と同じ変換ファミリの、
//   時間依存しない1発版）。
//   ShiftTexture     = matId, du, dv;                          （平行移動のみ）
//   ScaleTexture     = matId, su, sv, du, dv;                  （スケール→平行移動）
//   RotateTexture    = matId, angleDeg(反時計回り), cu, cv;     （中心(cu,cv)回転）
//   TransformTexture = matId, a11, a12, a13, a21, a22, a23;    （一般2Dアフィン）
struct TextureTransformCustomizer {
    enum class Kind { Shift, Scale, Rotate, Transform } kind = Kind::Shift;
    int materialId = 0;
    // 種別ごとに意味が変わる（コメント参照）。Transform は p0..p5=a11,a12,a13,a21,a22,a23。
    float p[6] = {0, 0, 0, 0, 0, 0};
    TriggerInfo trig;
};

// DefineAnimation 内の1フレーム文。5種の frame 文を種別と共通フィールドで表す。
struct AnimationFrame {
    enum class Kind { Simple, Numbered, SlideUV, TiledUV, RotationUV } kind = Kind::Simple;
    std::string texturePath;   // Simple/Numbered は書式含む場合あり（連番用 %d 等）
    int durationFrames = 0;    // 表示時間（ゲーム内フレーム数。実時間への換算は
                               // AnimationRuntime.cpp の kAnimationFrameSeconds）
    int startNum = 0, endNum = 0; // Numbered: 開始/終了番号
    float p0 = 0, p1 = 0, p2 = 0, p3 = 0; // UV系パラメータ（種別により意味が変わる）
    int frameCount = 0;        // UV系のフレーム数
    // このフレームの直後に書かれた ShiftTexture/ScaleTexture/RotateTexture/
    // TransformTexture(材質番号なし)。このフレームを表示している間のUV変換になる。
    // 材質番号とtrigは使わない。
    bool hasTransform = false;
    TextureTransformCustomizer transform;
};

// DefineAnimation "name"{ frame文* } のトップレベル定義。
struct AnimationDef {
    std::string name;
    std::vector<AnimationFrame> frames;
};

// SetAnimation = materialId, "animName";  （DefineAnimation 名を参照）
struct SetAnimationCustomizer {
    int materialId = 0;
    std::string animationName;
    TriggerInfo trig;
};

// alpha-changer(Help pi_sym_alpha_changer.html): 材質のアルファ値を変更する。
// ChangeAlpha = matId, (float | DayAlpha | NightAlpha);
// alpha はフォールバック値で、実行時は isDayAlpha/isNightAlpha を見て
// PartMaterialRuntime::Apply が太陽高度から解決する。
struct ChangeAlphaCustomizer {
    int materialId = 0;
    float alpha = 1.0f;
    bool isDayAlpha = false;
    bool isNightAlpha = false;
    TriggerInfo trig;
};

// EnvMap / BumpMap / AlphaZeroTest / Transparent / NoCastShadow / NoReceiveShadow = id列; 1件分。
// 他のカスタマイザと同じく発動条件を持ち、条件が成り立つ間だけ列挙した材質のフラグが立つ(条件ごとに実行時に決まる)。
struct MaterialFlagCustomizer {
    std::vector<int> materialIds;
    TriggerInfo trig;
};

// customizers のうち materialId を含む項目が1件でもあるか(発動条件は見ない)。
// 法線/ORM画像を読み込むかの判定に使う。
inline bool MaterialFlagPossible(const std::vector<MaterialFlagCustomizer>& entries, int materialId) {
    for (const MaterialFlagCustomizer& e : entries)
        for (int id : e.materialIds)
            if (id == materialId) return true;
    return false;
}

// 発動条件を持たない(Always)項目だけで materialId が指定されているか。読込直後の初期値に使う。
inline bool MaterialFlagStatic(const std::vector<MaterialFlagCustomizer>& entries, int materialId) {
    for (const MaterialFlagCustomizer& e : entries) {
        if (!e.trig.chain.empty()) continue;
        for (int id : e.materialIds)
            if (id == materialId) return true;
    }
    return false;
}

// Joint3D "基準名" { AttachCoord=vec3; LocalCoord=vec3(既定0); DirLink="名"(既定=基準名);
//   AttachDir=vec3(既定+Z); UpLink="名"(既定=基準名); AttachUp=vec3(既定+Y); }
// 自オブジェクトの LocalCoord 点が基準フレームの AttachCoord 点に一致するよう配置し、
// 姿勢は AttachDir(自+Z がこれを向く)と AttachUp(自+Y)で決定する。
struct Joint3DRef {
    std::string target;               // 基準名（他オブジェクト名 or _LOCAL/_WORLD/_CAMERA/_LIGHT）
    std::string dirLink;              // 既定=target
    std::string upLink;               // 既定=target
    float attach[3] = {0, 0, 0};      // AttachCoord（基準フレーム上）
    float local[3]  = {0, 0, 0};      // LocalCoord（自フレーム上、既定0）
    float dir[3]    = {0, 0, 1};      // AttachDir（基準フレーム上、既定+Z）
    float up[3]     = {0, 1, 0};      // AttachUp（基準フレーム上、既定+Y）
};

// Body Tilt{ TiltRatio; MaxAngle; BaseAlt } （車体のみ, 振り子傾斜）
// TiltRatio=曲率×速度に対する振れ量、MaxAngle=最大振れ角[deg]、
// BaseAlt=Z平行の回転軸のY位置。応答速度は TrainInfo.tiltSpeed 側で持つ。
struct TiltInfo {
    bool  hasTilt = false;
    float tiltRatio = 0, maxAngle = 0, baseAlt = 0;
};

// object-joint-zyx（Help pi_sym_object_joint_zyx.html）: object-joint-zy と同じ
// 2D(ZY)アタッチだが、対象のローカルX座標(AttachX)も指定できる点が異なる。
// ZY系の可動部(ObjectZY/TriangleZY/PistonZY/CrankZY)が使う。
struct JointZYXRef {
    std::string target;                 // 固定先パーツ名
    float attachX = 0;                  // 固定先のローカルX座標
    float attachZ = 0, attachY = 0;     // 固定先のローカルZY座標（AttachCoord）
    float localZ = 0, localY = 0;       // 自身のローカルZY座標（LocalCoord、既定0）
};

// ZY系の可動部の種類。ObjectZY は単独、TriangleZY/PistonZY/CrankZY は2パーツで1組。
enum class ZyPartKind {
    ObjectZY,      // ObjectZY "名"{...}: 2つの固定先の間に置く単独パーツ
    TriangleLink,  // TriangleZY の Link
    CrankLink,     // CrankZY の Link
    CrankSlide,    // CrankZY の Slide
    PistonLink,    // PistonZY の Link
};

// ZY系の可動部1パーツ分の定義(RailSim II の CFreeObjectZY/CTriangleLinkZY/CCrankSlideZY)。
// 姿勢は ZyLinkageSolve.h の
// SolveZyGroup が解く。パーツ自身の Turn は ModelPart::turn を使う。
struct ZyPartInfo {
    ZyPartKind   kind = ZyPartKind::ObjectZY;
    std::string  partnerLabel;      // 組の相手のラベル(ObjectZY は空)
    int          memberIndex = 0;   // 組の中の並び(0/1)。Crank は Link=0・Slide=1、Triangle/Piston は出現順
    int          jointCount = 0;    // 有効な joints の数(ObjectZY=2、それ以外=1)
    JointZYXRef  joints[2];         // JointZYX(取り付け先)
    float linkCoordZ = 0, linkCoordY = 0; // Link 系のみ有効(LinkCoord)
    float directionZ = 0, directionY = 0; // CrankSlide のみ有効(Direction)
    float fixPosition = 0.5f;       // ObjectZY のみ: 位置の内分比
    float fixRight = 0.5f;          // ObjectZY のみ: 右方向の内分比(省略時は fixPosition と同じ)
};

// ChangeTexture / SetAnimation / ShiftTexture系 のどれか1件。ModelPart::textureSteps の要素で、
// kind が指す1つだけが有効。
struct TextureStep {
    enum class Kind { ChangeTexture, SetAnimation, Transform } kind = Kind::ChangeTexture;
    ChangeTextureRef           changeTexture;
    SetAnimationCustomizer     setAnimation;
    TextureTransformCustomizer transform;

    const TriggerInfo& Trig() const;
};

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

    // TrackingMove / TrackingRotation(読むだけ。可動部の実行時は参照しない)。
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

// TriggerInfo(発動文脈のchain)が現在activeかどうかを判定する共通関数。
// chain の全ステップをAND評価する: SwitchCase は resolve(switchName) が
// caseValues に含まれるか(Default時はdeclaredCaseValuesに含まれないか)、
// Condition は EvalSwitchExpr の結果(negateCondition で反転)。
// chain が空(Always)なら常にtrue。
// ChangeModel(SchemaWalk.cpp)/MaterialRuntime/AnimationRuntime/MoverRuntime の
// leaf active判定はすべてこの関数を共有する（判定ロジックの重複/乖離防止）。
bool IsTriggerActive(const TriggerInfo& trig, const SwitchResolver& resolve);

// TriggerInfo(発動文脈のchain)が names のいずれかのスイッチ/変数を参照しているか。
// SwitchCase段は switchName が names に含まれるかで判定し、Condition段は
// ExprReferencesSwitch で式内参照を判定する(names の1つでも参照していればtrue)。
bool TriggerReferencesAnySwitch(const TriggerInfo& trig, const std::vector<std::string>& names);

// candidates のうち trig が参照しているスイッチ名を referenced へ追加する
// (判定は TriggerReferencesAnySwitch と同じ)。
void AddReferencedSwitches(const TriggerInfo& trig, const std::vector<std::string>& candidates,
                           std::set<std::string>& referenced);

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

struct SwitchDef {
    std::string name;
    std::string groupCommon;
    std::vector<std::string> entries;
};

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

// Rail の SoundInfo{ WheelSoundFile; JointInterval; }。車軸が継目を通るときの車輪音の定義。
struct RailSoundDef {
    bool hasSoundInfo = false;
    std::string wheelSoundFile; // 未解決相対パス（空なら鳴らさない）
    float jointInterval = 0.0f; // 継目の間隔[m]（0以下は鳴らさない）
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
