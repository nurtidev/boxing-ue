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
	Miss,
	Whiff // S-75: МОЙ удар ушёл в нырок соперника — «провалился»: корпус вперёд по инерции, доворот за рукой
};

namespace BoxerFeel
{
	// Сила удара → множитель реакции (impactScale веба).
	float ImpactScale(float Mag);
	// Пинок реакции защищающегося (reactionKick веба). bRear — удар задней (правой) рукой.
	// Whiff — пинок АТАКУЮЩЕГО (в его осях): провал вперёд за ударом.
	FBoxReactKick ReactionKick(EBoxFeelEvent Kind, EBoxFeelPunch Punch, bool bRear, bool bBody, float Mag, bool bSlipped);
	// Огибающие удара по фазе ядра: Aim — доворот руки на цель, Reach — «дотянуться до поверхности» (пик в контакте).
	// S-75: bSnap — контр-удар (после удачного уклона): рука встаёт на линию раньше — удар читается быстрее и прямее.
	void PunchEnvelopes(float Phase, float ContactFrac, float& OutAim, float& OutReach, bool bSnap = false);

	// S-75: нырок процедурно (поверх клипа AM_SlipL/R) — голова и корпус уходят с линии вбок и вниз, колени подсели.
	// Slip — −1..1 (синус окна уклона ядра, < 0 — влево от бойца). Выход: крен корпуса (рад, > 0 — макушка вправо, как
	// TorsoRoll), наклон вперёд (рад), сдвиг таза вбок (см, > 0 — влево) и сгиб коленей (рад, как канал Knee — таз ниже).
	struct FSlipPose
	{
		float Roll = 0.f;
		float Bend = 0.f;
		float SideCm = 0.f;
		float Knee = 0.f;
	};
	FSlipPose SlipPose(float Slip);

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

// S-75: блок «читается» — состояние рук защиты на игровом потоке (чистая логика, тест BoxingUE.BoxerDefense).
//  * W — вес плотного блока: перчатки у лица/подбородка, локти к корпусу, плечи вверх. Поднимается за ~0.08 с, опускается
//    за ~0.2 с (web: опустил после долгого удержания — руки идут вниз 0.22 с).
//  * Sag — руки устают (GuardIntegrity ядра < 0.6): перчатки ниже.
//  * Push — удар в блок: перчатки вдавливает к лицу (см, пружина без отскока), корпус чуть сжимается (реакция Block).
//  * BreakW — блок пробит (Hit.bGuardBreak): руки разлетаются 0.6 с (в ядре 0.6 с не закрыться).
struct FBoxerGuardState
{
	float W = 0.f;
	float Sag = 0.f;
	float Push = 0.f;
	float PushV = 0.f;
	float BreakT = -1.f; // с с момента пробития (−1 — нет)

	static constexpr float RAISE_S = 0.08f;
	static constexpr float LOWER_S = 0.2f;
	static constexpr float PUSH_MAX_CM = 7.f;
	static constexpr float BREAK_S = 0.6f;

	void Update(float Dt, bool bGuardUp, float Integrity);
	void OnBlocked(float Mag);
	void OnGuardBreak();
	float BreakW() const;
	void Reset() { *this = FBoxerGuardState(); }
};

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

	// --- S-75: защита (FBoxerGuardState, нырок ядра) ---
	float GuardW = 0.f;      // плотный блок 0..1
	float GuardSag = 0.f;    // руки устают 0..1 (перчатки ниже)
	float GuardPushCm = 0.f; // удар в блок — перчатки вдавлены к лицу (см)
	float GuardBreakW = 0.f; // блок пробит — руки разлетаются 0..1
	float Slip = 0.f;        // −1..1 нырок (< 0 — влево)
	// --- S-76/S-75: клинч — поза сцепки «рука сверху / рука снизу» (мир, см) ---
	float ClinchW = 0.f;
	FVector ClinchOver = FVector::ZeroVector;  // левая — за его правое плечо (сверху)
	FVector ClinchUnder = FVector::ZeroVector; // правая — под его левую руку, на спину
	// --- S-78: тела не проходят друг в друга — тело соперника (его прошлый кадр анимпотока с упреждением, мир, см) ---
	bool bBody = false;                        // тело соперника известно (FBoxerBodyTrack)
	FVector OppHeadC = FVector::ZeroVector;    // центр его головы — итог кадра (после его раздвижки)
	FVector OppHeadBone = FVector::ZeroVector; // его кость head (затылок/шея) — итог
	FVector OppHeadPre = FVector::ZeroVector;  // центр его головы ДО раздвижки (раздвижку делим поровну, без обратной связи)
	FVector OppHeadBonePre = FVector::ZeroVector;
	FVector OppPelvis = FVector::ZeroVector;   // ось его корпуса: таз → грудь (spine_05)
	FVector OppChest = FVector::ZeroVector;
	float OppHeadR = 10.f; // радиус его головы
	float OppBodyR = 13.f; // радиус его корпуса (до оси)
	float HeadR = 10.f;    // мой радиус головы
	float HeadUpCm = 8.f;  // центр головы над костью head
	float SepW = 0.f;      // раздвижка голов 0..1
	float HandPushW = 0.f; // упор моих кистей в его голову/корпус 0..1 (клинч — 0: руки на нём)

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

	// S-74: плоскость сгиба колена в IK ног (порт legIk.ts + защита от выворота). Возвращает единичное направление «куда
	// колено» (⟂ линии бедро → цель): доля колена клипа (ClipShare 0..1, как clipKnee веба), остальное — курс ступни
	// (колено над носком). bGuard: колено клипа, смотрящее назад от носка (клип ходьбы GASP заносит ногу назад, а
	// ступня стоит впереди), не учитывается, и итог — не дальше KneeMaxDevRad от курса ступни (ни назад, ни внутрь).
	// Inward (необязательно) — горизонталь от этого бедра к другому: внутрь колено уходит не дальше KneeMaxInRad
	// («колени внутрь» у малорослых обликов, QA S-78 №11), наружу — до KneeMaxDevRad.
	FVector KneeBendDir(const FVector& Hip, const FVector& ClipKnee, const FVector& Goal, const FVector& FootFwd, float L1, float L2,
		float ClipShare, bool bGuard, float KneeMaxDevRad = 0.61f, const FVector& Inward = FVector::ZeroVector, float KneeMaxInRad = 0.2f);
	// Знаковый угол (рад) направления сгиба Bend от курса ступни FootFwd вокруг оси Axis (оба проецируются ⟂ Axis);
	// > 0 — против часовой вокруг Axis. bValid = false — вырождено (одно из направлений почти вдоль оси).
	float BendAngle(const FVector& Axis, const FVector& Bend, const FVector& FootFwd, bool& bValid);
	// Куда сгибается двухзвенник A → B → C: отступ B от линии A → C (единичный; ноль — почти прямой, отступ < MinCm).
	FVector BendOf(const FVector& A, const FVector& B, const FVector& C, float MinCm = 1.5f);
}

// S-74: метрика «колено/локоть вывернулись» (отладка -BoxFootLog): угол сгиба от курса ступни / от сгиба клипа.
struct FBoxerJointStat
{
	int32 Frames = 0; // кадров с оценкой (сустав согнут)
	int32 Over45 = 0; // |угол| > 45°
	int32 Over90 = 0; // > 90° — колено назад / локоть вывернут
	int32 Flips = 0;  // входов в > 90° (переворотов)
	float MaxDeg = 0.f;
	double SumDeg = 0.0;
	bool bWasFlip = false;
	void Add(float Deg)
	{
		const float A = FMath::Abs(Deg);
		++Frames;
		SumDeg += A;
		MaxDeg = FMath::Max(MaxDeg, A);
		Over45 += A > 45.f ? 1 : 0;
		Over90 += A > 90.f ? 1 : 0;
		Flips += (A > 90.f && !bWasFlip) ? 1 : 0;
		bWasFlip = A > 90.f;
	}
};

// S-74: «угол не оценивался» (сустав почти прямой).
constexpr float KNEE_DEG_NONE = -999.f;

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
	// S-74: колени (по стороне) — угол сгиба от курса ступни после IK / у клипа до IK; чашечка от плоскости сгиба (град,
	// KNEE_DEG_NONE — нога почти прямая, не оценивается).
	float KneeDeg[2] = {KNEE_DEG_NONE, KNEE_DEG_NONE};
	float ClipKneeDeg[2] = {KNEE_DEG_NONE, KNEE_DEG_NONE};
	float KneecapDeg[2] = {KNEE_DEG_NONE, KNEE_DEG_NONE};
	// S-74: локоть бьющей руки после наведения (IK) — угол сгиба от сгиба клипа (град) и вверх ли смотрит локоть.
	float ElbowDeg = KNEE_DEG_NONE;
	float ElbowUp = 0.f; // проекция направления локтя на вертикаль (1 — локоть вверх)
	float ElbowOut = 0.f; // проекция на «наружу» от корпуса (−1 — локоть внутрь, к другой руке)
	int32 ElbowKind = 0;  // вид удара наведения: 0 — прямой, 1 — хук, 2 — апперкот
	float ElbowJumpDeg = KNEE_DEG_NONE; // скачок направления сгиба локтя за кадр (град)
	// S-78: тело (мир, см) — по нему соперник раздвигает головы и упирает кулаки (FBoxerBodyTrack).
	bool bBody = false;
	FVector BodyHeadC = FVector::ZeroVector;     // центр головы — итог
	FVector BodyHeadBone = FVector::ZeroVector;  // кость head — итог
	FVector BodyHeadPre = FVector::ZeroVector;   // центр головы до раздвижки
	FVector BodyHeadBonePre = FVector::ZeroVector;
	FVector BodyPelvis = FVector::ZeroVector;
	FVector BodyChest = FVector::ZeroVector;     // spine_05
	float SepCm = 0.f;      // моя голова отведена от его (этот кадр)
	float HandPushCm = 0.f; // кисть вытолкнута из его головы/корпуса (макс. по рукам)
};

namespace BoxerFeel
{
	// S-78: вытолкнуть шар (центр P, радиус Rp) из шара (C, R) — сдвиг P (ноль — не пересекаются). Fallback — куда, если
	// центры совпали.
	FVector PushOutSphere(const FVector& P, float Rp, const FVector& C, float R, const FVector& Fallback);
	// То же из капсулы «отрезок A → B, радиус R».
	FVector PushOutCapsule(const FVector& P, float Rp, const FVector& A, const FVector& B, float R, const FVector& Fallback);
	// Моя доля (Share) горизонтального сдвига головы Mine от Theirs, чтобы расстояние стало ≥ Want (вертикаль не меняем —
	// раздвигаем наклоном корпуса). Fallback — направление, если головы одна над другой. Ноль — уже не ближе Want.
	FVector HeadSeparation(const FVector& Mine, const FVector& Theirs, float Want, float Share, const FVector& Fallback);
}

// S-78: тело соперника для раздвижки и упора (игровой поток): его прошлый кадр анимпотока + упреждение на кадр (скорость по
// двум последним РАЗНЫМ кадрам, не дальше PREDICT_MAX_CM), вес включения плавный (RATE в с⁻¹).
struct FBoxerBodyTrack
{
	static constexpr int32 N = 6; // центр головы, кость head, таз, грудь, центр головы до раздвижки, кость head до раздвижки
	static constexpr float RATE = 5.f;
	static constexpr float PREDICT_MAX_CM = 12.f;
	float W = 0.f;
	bool bHave = false;
	FVector Cur[N];
	FVector Prev[N];

	void Update(const FBoxerFeelDebug& Opp, bool bOn, float Dt, FBoxerFeelFrame& Out);
	void Reset() { *this = FBoxerBodyTrack(); }
};

// Применение кадра к позе. Индексы костей кэшируются по серийному номеру контейнера костей.
class FBoxerPoseFx
{
public:
	// CompToWorld — трансформ компонента меша (прокси: GetComponentTransform()).
	void Apply(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug = nullptr);
	// S-78: итоговое тело (голова, таз, грудь) — сопернику; звать ПОСЛЕ всех слоёв позы (ступни, посадка).
	void PublishBody(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug);

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
	FVector PrevElbowBend[2] = {FVector::ZeroVector, FVector::ZeroVector}; // S-74: отладка — сгиб локтя прошлого кадра наведения
	FCompactPoseBoneIndex Clav[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)}; // S-75: плечи в блоке

	// S-75: плотный блок / пробитый блок — руки двухзвенной IK к лицу (или в стороны), до записи «сырых» перчаток.
	void ApplyGuard(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld);
	// S-76/S-75: клинч — таз к сопернику, корпус навалился, голова вбок (щека к щеке), руки IK на его плечо/спину.
	void ApplyClinch(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld);
	// S-78 (BoxerContact.cpp): головы не входят друг в друга — моя половина раздвижки наклоном корпуса (остаток — тазом);
	// лицо не входит в его корпус. До наведения руки.
	void ApplySeparation(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug);
	// S-78: упор кулаков — перчатка (любая рука, в ударе и вне его) не глубже поверхности его головы/шеи/корпуса;
	// публикует итоговое тело для соперника. Последний шаг FBoxerPoseFx.
	void ApplyHandStop(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug);
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
	// S-74: ось коленной чашечки в осях бедра (поза привязки) — бедро доворачивается, чтобы чашечка была в плоскости сгиба.
	bool bKneeAxis = false;
	FVector KneeAxisLocal[2] = {FVector::ForwardVector, FVector::ForwardVector};

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
