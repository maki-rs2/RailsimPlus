//
// PartCustomizerSchema.h - パーツの接続・可動部・材質とテクスチャのカスタマイザ
//
// 実行時の反映側がカスタマイザの型だけを読めるよう、PluginSchema.h から分けてある。
//
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "CustomizerTiming.h"
#include "SwitchExpr.h"
#include "TriggerInfo.h"
#include "../../core/Math.h"

namespace rs::legacy_pi {

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

// StaticMove / StaticRotation / DynamicRotation / AnalogClock / TrackingMove カスタマイザ(可動部)
struct StaticCustomizer {
    enum class Kind { Move, Rotate, DynamicRotate, AnalogClock, TrackingMove };
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
    size_t trackingIndex = 0;                // ModelPart::trackingMoves の添字
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

} // namespace rs::legacy_pi
