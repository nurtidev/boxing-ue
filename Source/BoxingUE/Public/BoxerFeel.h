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
