// «Ощущение удара» (S-41, game feel): процедурный слой поверх клипов — только визуал, ядро не трогается.
//
//  * FBoxerReactionRig — пружины реакции на попадание (порт web/src/ui/three/impact.ts ReactionRig):
//    голова (кивок/поворот/наклон), корпус (откид/крен/скрут), колени, отшатывание. Пинок по событию ядра
//    (Hit/Blocked/Miss) → пружины → аддитивные повороты костей ПО ВЕКТОРУ УДАРА.
//  * FBoxerFeelFrame — снимок для анимпотока на кадр: каналы реакции + наведение удара (точка
//    поверхности цели в мире, направление подхода кулака, огибающие фазы).
//  * FBoxerPoseFx — применяет кадр к локальной позе (FCompactPose): подшаг корпуса до цели, реакция,
//    наведение бьющей руки двухзвенной IK с «упором» кулака в поверхность (порт идеи web contact.ts).
//
// Физики здесь нет: компонентная физреакция (UPhysicalAnimationComponent) роняет движок
// (Docs/VERIFY_ON_PC.md, разд. 3), поэтому реакция — аддитивные пружины костей, как в вебе.
#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"
#include "FootPlant.h"

struct FCompactPose;
struct FBoneContainer;

// Каналы реакции (система ЗАЩИЩАЮЩЕГОСЯ, лицом к атакующему), как REACT_CHANNELS веба:
// *Pitch > 0 — назад (подбородок вверх / откид), < 0 — вперёд (сгиб); *Yaw > 0 — поворот влево от бойца;
// *Roll > 0 — макушка/плечи вправо; Knee — сгиб коленей (рад); Back — отшатывание назад (м); Side > 0 — влево (м).
enum class EBoxReactChannel : uint8
{
	HeadPitch,
	HeadYaw,
	HeadRoll,
	TorsoPitch,
	TorsoRoll,
	TorsoYaw,
	Knee,
	Back,
	Side,
	Num
};

constexpr int32 BOX_REACT_NUM = static_cast<int32>(EBoxReactChannel::Num);

// Пинок: желаемые ПИКИ отклонения по каналам (рад / м); пружина сама вернёт в ноль.
struct FBoxReactKick
{
	float V[BOX_REACT_NUM] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
	bool bValid = false;
	float& operator[](EBoxReactChannel C) { return V[static_cast<int32>(C)]; }
	float operator[](EBoxReactChannel C) const { return V[static_cast<int32>(C)]; }
};

// Вид удара для реакции (ядро: Jab/Cross — прямые, HookL/R — хук, UpperL/R — апперкот).
enum class EBoxFeelPunch : uint8
{
	Straight,
	Cross,
	Hook,
	Uppercut
};

enum class EBoxFeelEvent : uint8
{
	Land,
	Block,
	Miss
};

namespace BoxerFeel
{
	// Сила удара → множитель реакции (impactScale веба).
	float ImpactScale(float Mag);
	// Пинок реакции защищающегося (reactionKick веба). bRear — удар задней (правой) рукой.
	FBoxReactKick ReactionKick(EBoxFeelEvent Kind, EBoxFeelPunch Punch, bool bRear, bool bBody, float Mag, bool bSlipped);
	// Огибающие удара по фазе ядра: Aim — доворот руки на цель, Reach — «дотянуться до поверхности» (пик в контакте).
	void PunchEnvelopes(float Phase, float ContactFrac, float& OutAim, float& OutReach);

	// S-62: точка касания головы МИМО перчаток защиты (засчитанное попадание не приходит в перчатку).
	// Center/Radius — сфера головы, Start — откуда идёт кулак (плечо у прямых; у хука/апперкота путь — последние
	// PathCm по направлению подхода), Approach — исходный подход. Gloves — центры перчаток защищающегося, Clear —
	// радиус перчатки + кулака. Перебор точек на передней полусфере (сбоку/выше/ниже), цена — отклонение + проход
	// пути сквозь перчатки; Prev (если есть) — прошлый выбор (гистерезис, без дрожи). true — точка сдвинута.
	struct FGuardAim
	{
		FVector Surface = FVector::ZeroVector;
		FVector Approach = FVector::ForwardVector;
		FVector2D UV = FVector2D::ZeroVector; // сдвиг по сфере: U — вбок, V — вверх (доли радиуса)
		float ClearCm = 0.f;                  // запас пути до ближайшей перчатки (< 0 — сквозь)
	};
	bool AimAroundGuard(const FVector& Center, float Radius, const FVector& Start, const FVector& Approach, bool bStraight, float PathCm,
		const FVector Gloves[2], float Clear, const FVector2D* Prev, FGuardAim& Out);
	// Расстояние от точки до отрезка.
	float SegPointDist(const FVector& A, const FVector& B, const FVector& P);
}

struct FBoxerReactionRig
{
	float X[BOX_REACT_NUM] = {};
	float Vel[BOX_REACT_NUM] = {};

	// Добавить скорость (серия копится).
	void Kick(const FBoxReactKick& K);
	// Интегрировать (полунеявный Эйлер, подшаги ≤ 1/120 с); Dt ≤ 0 — поза стоит, длинный кадр режется до 1/30 с.
	void Update(float Dt);
	void Reset();
	bool IsActive() const;
	float Get(EBoxReactChannel C) const { return X[static_cast<int32>(C)]; }
};

// Кадр «ощущения» для анимпотока (всё в мире, см; копируется в прокси на игровом потоке).
struct FBoxerFeelFrame
{
	// --- реакция ---
	float React[BOX_REACT_NUM] = {};
	FVector Fwd = FVector::ForwardVector; // курс бойца (к сопернику)
	FVector Right = FVector::RightVector;

	// --- наведение удара ---
	bool bAim = false;
	bool bLeftArm = true;             // бьющая рука: левая (передняя у ортодокса) / правая
	FVector AimSurface = FVector::ZeroVector;  // точка поверхности цели, которой касается фронт кулака
	FVector AimApproach = FVector::ForwardVector; // направление хода кулака (единичный)
	float AimWeight = 0.f;            // доворот руки на цель 0..1
	float ReachWeight = 0.f;          // дотянуться до поверхности 0..1 (пик в кадре контакта)
	float FistReachCm = 10.f;         // кость кисти → фронт кулака/перчатки
	float MaxLungeCm = 30.f;          // подшаг корпуса к цели, если рука не достаёт
	bool bBentArm = false;            // хук/апперкот: локоть согнут, дистанцию добирает подшаг
	int32 PunchKind = 0;              // 0 — прямой, 1 — хук, 2 — апперкот (плоскость локтя в IK)

	// --- S-62: удар соперника идёт сквозь мой гард (не в блок) — перчатку отводит с его пути (мир, см) ---
	bool bThreat = false;
	FVector ThreatA = FVector::ZeroVector; // локоть бьющей руки соперника
	FVector ThreatB = FVector::ZeroVector; // фронт его кулака
	float ThreatClear = 16.f;              // радиус моей перчатки + его кулака
	float ThreatW = 0.f;                   // 0..1 — сила отвода (огибающая его удара)

	// Отладка (пишется анимпотоком, читается на игровом потоке с лагом в кадр).
	bool bActive = false;

	// --- S-70: ступни (FBoxerFootIk) ---
	bool bFeetOn = false;      // IK ног включён (не лежит, не встаёт, не сидит); false — плавно гаснет
	bool bFeetCalm = false;    // стоит спокойно (без удара/реакции/хода) — по этим кадрам учится стойка
	bool bFeetWalking = false; // ходьба постановки (выход из угла, в угол) — попеременный шаг
	int32 FeetStep = 0;        // вид шага ядра (EStepKind: 0 — не идёт)
	bool bFeetPunch = false;   // идёт удар (пивот опорной ступни «от ноги»)
	bool bFeetRearArm = false; // бьёт задняя рука
	int32 FeetPunchKind = 0;   // 0 — джеб, 1 — кросс, 2 — хук, 3 — апперкот
	float FeetPunchPhase = 0.f; // фаза удара (контакт = 0.5)
	float FeetScale = 1.f;     // масштаб облика (рост / эталон)
	bool bFeetSouthpaw = false; // левша: стойка зеркально (передняя — правая)
	bool bFeetPoseStance = false; // стойка — из позы (рефери): без боксёрской стойки, таза боком, приседа и пяток
};

namespace BoxerFeel
{
	// S-62: куда отвести перчатку (центр Glove) с пути удара A→B: сдвиг перпендикулярно пути до Clear, не больше MaxCm.
	// Ноль — путь и так мимо.
	FVector GuardPush(const FVector& Glove, const FVector& A, const FVector& B, float Clear, float MaxCm);
}

// Отладочный вывод анимпотока (последний кадр с наведением).
struct FBoxerFeelDebug
{
	float FistGapCm = -1.f;   // зазор кисть → её цель вдоль подхода (≈ фронт кулака → поверхность); −1 — нет наведения
	FVector FistFront = FVector::ZeroVector; // фронт кулака в мире (кадр анимпотока)
	FVector Elbow = FVector::ZeroVector;     // S-62: локоть бьющей руки в мире (путь удара для гарда соперника)
	float GuardPushCm = 0.f;                 // S-62: насколько отведена моя перчатка с пути удара соперника
	float LungeCm = 0.f;
	float AimW = 0.f;
	float ReachW = 0.f;
	// Позиции целей ДО процедурного слоя (мир, см): клип как есть, без подшага/реакции — по ним наводится соперник.
	bool bRaw = false;
	FVector RawHead = FVector::ZeroVector;
	FVector RawChest = FVector::ZeroVector;
	FVector RawHandL = FVector::ZeroVector;
	FVector RawHandR = FVector::ZeroVector;
	// S-70: ступни (FBoxerFootIk) — [0] левая, [1] правая.
	bool bFeet = false;               // планировщик ступней работал в этом кадре
	float FeetW = 0.f;                // вес IK ног
	float FootU[2] = {-1.f, -1.f};    // фаза переноса (−1 — стоит в опоре)
	bool bFootDrag[2] = {false, false}; // перенос — подтяг волоком
	int32 FootSwings = 0;             // всего переносов (накопительно)
	int32 FootDrags = 0;              // из них волоком
	float HipDropCm = 0.f;            // таз опущен, чтобы нога дотянулась (см, мир)
	int32 LeadSide = 0;               // передняя ступня: 0 — левая (правша), 1 — правая (левша)
	// Замер: подушечки в мире в этой оценке позы (после IK; и с выключенной фиксацией).
	bool bBallW = false;
	FVector BallW[2] = {FVector::ZeroVector, FVector::ZeroVector};
	int32 EvalSeq = 0;
	float EvalDt = 0.f;
	int32 FootWhy[6] = {0, 0, 0, 0, 0, 0}; // причины переносов: нога на пределе, подтяг, порог, разворот, шаг ядра, ходьба
	int32 FootCtx[5] = {0, 0, 0, 0, 0};    // контекст: удар, выпад, шаг ядра, разворот, ход корпуса
	float FootHeel[2] = {0.f, 0.f};       // подъём пятки (рад), по стороне
	float FootStretch[2] = {0.f, 0.f};    // вытянутость ноги
	float FootLungeCm[2] = {0.f, 0.f};    // выпад таза от стойки (вперёд, вбок), см
	float FootErrCm[2] = {0.f, 0.f};      // щиколотка после IK → цель (см): недотянулась
	// S-70: пик выноса таза в текущем ударе (FBoxerPoseFx → FBoxerFootIk в той же оценке позы), пространство компонента.
	bool bLungePeak = false;
	FVector LungePeakCS = FVector::ZeroVector;
	FVector2D StanceCm[2] = {FVector2D::ZeroVector, FVector2D::ZeroVector}; // стойка: щиколотка от корпуса (вперёд, вправо), см
	float StanceYawDeg[2] = {0.f, 0.f};   // курс ступни стойки от курса бойца (град)
};

// Применение кадра к позе. Индексы костей кэшируются по серийному номеру контейнера костей.
class FBoxerPoseFx
{
public:
	// CompToWorld — трансформ компонента меша (прокси: GetComponentTransform()).
	void Apply(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug = nullptr);

private:
	void Resolve(const FBoneContainer& Bones);

	uint16 Serial = MAX_uint16;
	const void* ContainerPtr = nullptr;
	FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Spine[3] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Neck = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Head = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Thigh[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Calf[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Foot[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	// [0] — левая, [1] — правая.
	FCompactPoseBoneIndex UpperArm[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex LowerArm[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Hand[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
};

// S-70: ступни видимого меша — планировщик BoxFoot (FootPlant.h, порт footPlant.ts) + двухзвенная IK ног (порт legIk.ts и
// plantFeet из SkinnedFighter.tsx). Применяется ПОСЛЕДНИМ (после ретаргета и FBoxerPoseFx): опорная ступня стоит в мире,
// шаг — перенос одной ступни, таз — перенос веса/«пружина»/опускание ≤ 3.5 см, пятки и пивот на подушечке.
// Стойка (щиколотки, курс и поворот ступней, таз) учится на лету по «спокойным» кадрам позы GASP (bFeetCalm) в
// пространстве компонента — левша (зеркало позы) получается сам: передняя ступня — та, что дальше по курсу.
class FBoxerFootIk
{
public:
	// Dt — шаг анимации этого меша (с; хит-стоп ≈ 0). CompToWorld — трансформ компонента.
	void Apply(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, float Dt, FBoxerFeelDebug* OutDebug = nullptr);
	void Reset();

private:
	void Resolve(const FBoneContainer& Bones);
	void ApplyInner(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, float Dt, FBoxerFeelDebug* OutDebug);

	int32 Seq = 0;
	uint16 Serial = MAX_uint16;
	const void* ContainerPtr = nullptr;
	FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
	// [0] — левая, [1] — правая.
	FCompactPoseBoneIndex Thigh[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Calf[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Foot[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Ball[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Spine1 = FCompactPoseBoneIndex(INDEX_NONE); // корпус над тазом (таз боком — корпус на месте)

	// Стойка (пространство компонента, см): щиколотки, подушечки, поворот ступней, таз.
	bool bStance = false;
	float CalmTime = 0.f;
	FVector StAnkle[2];
	FVector StBall[2];
	FQuat StFootQ[2];
	FVector StPelvis = FVector::ZeroVector;

	// Состояние (планировщик — в мире, м; индекс планировщика: 0 — передняя, 1 — задняя).
	BoxFoot::FGait Gait;
	BoxFoot::FStepTrack Track;
	BoxFoot::FLungeHold Hold[2];
	float IkW = 0.f;
	float WalkMix = 0.f;
	bool bPrevBody = false;
	FVector2D PrevBody = FVector2D::ZeroVector;
	float VX = 0.f, VY = 0.f;
	float WsX = 0.f, WsY = 0.f; // перенос веса (мир, м)
	float Dip = 0.f;            // «пружина» на шаге (м)
	float HipDrop = 0.f;        // опускание таза (м)
	float ReachHeel[2] = {0.f, 0.f};
	float Twist[2] = {0.f, 0.f};
	int32 Ctx[5] = {0, 0, 0, 0, 0}; // отладка: контекст переносов (см. FBoxerFeelDebug::FootCtx)
};

// Маска «верх тела» для слоя ударов: кость BlendRoot и всё под ней.
struct FBoxerUpperMask
{
	void Resolve(const FBoneContainer& Bones, FName BlendRoot);
	bool IsValid() const { return Root.GetInt() != INDEX_NONE; }

	uint16 Serial = MAX_uint16;
	const void* ContainerPtr = nullptr;
	FCompactPoseBoneIndex Root = FCompactPoseBoneIndex(INDEX_NONE);
	// 0 — низ, 1 — корпус/шея/голова, 2 — руки (ключица и ниже).
	TArray<uint8> Kind;
};
