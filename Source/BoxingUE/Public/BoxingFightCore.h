// FBoxingFightCore — движок-агностичное ядро интерактивного (реалтайм) боя.
// Порт УПРОЩЁННОЙ версии web/src/engine/interactive.ts + interactive/*.ts (S-41).
//
// Только CoreMinimal (без Engine/UObject): UE-слой (Pawn/AnimBP/GameMode) владеет экземпляром,
// каждый кадр зовёт Tick(Dt), подаёт ввод через ApplyAction и читает GetSnapshot()/PollEvents().
//
// Внутренние величины — double (как number в JS): так формулы совпадают с TS-оригиналом без
// дрейфа float, а сидированный бой воспроизводим бит-в-бит на одной платформе/компиляторе.
// ГСЧ — mulberry32 (web/src/engine/rng.ts), uint32-арифметика.
//
// ИНВАРИАНТ СИДИРУЕМОСТИ: порядок бросков ГСЧ = порядок вызовов в Tick (как loop.ts).
// Не переставлять вызовы и не добавлять бросков в чтение (GetSnapshot/PollEvents их не делают).
#pragma once

#include "CoreMinimal.h"
#include "FightTypes.h"

// mulberry32 — побитово как web/src/engine/rng.ts.
struct FBoxingRng
{
	uint32 S = 1;

	void Seed(uint32 InSeed) { S = InSeed; }

	// [0, 1)
	double Next()
	{
		S = S + 0x6d2b79f5u;
		uint32 T = (S ^ (S >> 15)) * (1u | S);
		T = (T + ((T ^ (T >> 7)) * (61u | T))) ^ T;
		return static_cast<double>(T ^ (T >> 14)) / 4294967296.0;
	}

	double Range(double Min, double Max)
	{
		if (Max < Min)
		{
			const double Tmp = Min;
			Min = Max;
			Max = Tmp;
		}
		return Min + Next() * (Max - Min);
	}
};

class BOXINGUE_API FBoxingFightCore
{
public:
	// Старт боя: расстановка по центру на DIST_START, первые броски ГСЧ (темп ИИ, «форма дня»).
	void Init(const FFightConfig& Config);

	// Шаг времени. Как loop.ts: Dt зажимается в [0, 0.05] — при больших кадрах время боя
	// «теряется». UE-слою стоит тикать фиксированным шагом (аккумулятор, например 1/60 с).
	void Tick(float Dt);

	// Ввод бойца, которым управляет человек (bAiControlled == false). Для ИИ-бойца — игнор.
	// Target — цель для ударов (для остальных действий не важна). true — действие принято.
	bool ApplyAction(int32 Fighter, EFightAction Action, EPunchTarget Target = EPunchTarget::Head);

	FFightSnapshot GetSnapshot() const;
	// Забрать накопленные события (очередь очищается).
	TArray<FFightEvent> PollEvents();

	const FFightResult& GetResult() const { return Result; }
	bool IsOver() const { return Phase == EFightPhase::Over; }
	EFightPhase GetPhase() const { return Phase; }
	double GetFightTime() const { return T; }

	// Эффективность удара Kind на дистанции Dist (м) для размаха ReachCm: 0 — не достаёт,
	// 1 — идеальная дистанция (punches.ts rangeFactor). Публично — для подсказок UI.
	static double RangeFactor(EPunchKind Kind, double Dist, double ReachCm = 183.0);

private:
	struct FPunchAct
	{
		EPunchKind Kind = EPunchKind::Jab;
		EPunchArm Arm = EPunchArm::Lead;
		EPunchTarget Target = EPunchTarget::Head;
		double Start = 0, Contact = 0, End = 0;
		bool bResolved = false;
		bool bEmpty = false; // брошен «из пустого бака» — вялый
	};

	struct FDownState
	{
		int32 Who = -1;
		double CountT = 0;
		double RiseProgress = 0;
		bool bAiDecided = false;
		bool bAiWillRise = false;
		double AiRiseAt = 0;
		double KoChance = 0;
	};

	// Состояние одного бойца (runtime.ts Runtime).
	struct FRuntime
	{
		FFighterSetup Prof;
		double MaxStam = 70, Stamina = 70, Tank = 70;
		double Accumulated = 0; // износ в единицах simulate
		int32 Kd = 0;
		bool bHasPunch = false;
		FPunchAct Punch;
		bool bBlocking = false;
		double BlockUntil = 0;
		double BlockSince = -1;
		bool bBlockWanted = false;
		double GuardPressure = 0;
		double BlockReadyAt = 0;
		double SlipStart = -1;
		double WhiffOpenUntil = 0;
		TArray<double> WhiffTimes;
		double ShortUntil = 0;
		double SlipUntil = 0;
		double SlipReadyAt = 0;
		double CounterUntil = 0;
		double HurtUntil = 0;
		double HurtMag = 0;
		double StaggerUntil = 0;
		double NextAiAt = 0;
		double PunishReadyAt = 0;
		double X = 0, Z = 0;
		EStepKind Step = EStepKind::None;
		double StepSpeed = 0, StepUntil = 0, StepReadyAt = 0, NextStepAt = 0;
		int32 DistDir = 0;
		EStepKind LastStraight = EStepKind::None;
		double LastStraightAt = -9;
		int32 RopeSide = 0;
		double AngleUntil = 0, AngleMag = 0;
		bool bHasAim = false;
		double AimX = 0, AimZ = 0;
		EPunchTarget HurtTarget = EPunchTarget::Head;
		double GassedUntil = 0;
		int32 SlipDir = 1;
		TArray<int32> Recent;     // последние удары человека «вид*2+цель» (чтение ИИ)
		TArray<double> SlipTimes; // моменты последних уклонов человека

		double Fatigue() const;
		double StamCap() const;
		double FatigueForm() const;
		double PlayerForm() const;
		double Health() const;
	};

	struct FVec2
	{
		double X = 0, Z = 0;
	};

	// --- цикл (loop.ts) ---
	void UpdateFighter(int32 I, double Dt);
	double UpdateGuard(int32 I, double Dt);
	void RegenStamina(int32 I, double Held, double Dt);

	// --- ввод человека (player.ts) ---
	bool HumanPunch(int32 I, EPunchKind Kind, EPunchArm Arm, EPunchTarget Target);
	bool HumanSlip(int32 I, int32 Dir);
	bool HumanBlock(int32 I, bool bOn);
	bool HumanRiseTap(int32 I);
	bool Proceed();

	// --- атака (attack.ts) ---
	void StartPunch(int32 I, EPunchKind Kind, bool bHasArm, EPunchArm Arm, EPunchTarget Target);
	void ResolvePunches();
	void ResolveContact(int32 Att, const FPunchAct& Punch);

	// --- защита (guard.ts) ---
	void TryRaiseBlock(FRuntime& R);
	bool TrySlip(int32 Att, EPunchKind Kind, EPunchArm Arm, bool bBody, double Angle, double Rope);
	bool BlockPunch(int32 Def, int32 Att, const FPunchAct& Punch, double HpDmg);

	// --- нокдауны (knockdown.ts) ---
	void TryKnockdown(int32 Att, int32 Def, EPunchKind Kind, double FormAtt, double ShotMul);
	void Knockdown(int32 Att, int32 Def);
	void UpdateCount(double Dt);
	void RiseUp();

	// --- раунды и судьи (rounds.ts) ---
	void EndRound();
	void ScoreRound();
	void DecideByCards();
	void Finish(int32 Winner, EFightMethod Method);
	void BuildResult(int32 WinnerIndex, EFightMethod Method, EDecisionKind Decision, int32 StoppedRound);

	// --- ноги (footwork.ts) ---
	void PlaceFighters();
	FVec2 Axis(int32 I) const;
	FVec2 Tangent(int32 I, int32 Side) const;
	FVec2 StepTarget(int32 I, EStepKind Kind, double Amt) const;
	void MoveStep(int32 I, EStepKind Kind, double Amt);
	void MoveTo(int32 I, double NX, double NZ);
	bool CanRetreat(int32 I) const;
	int32 RopeLevel(int32 I) const;
	double RopePen(int32 I) const;
	double AngleOf(int32 I) const;
	bool StartStep(int32 I, EStepKind Kind, const FVec2* Aim = nullptr);
	double Distance() const;
	double YawOf(int32 I) const;
	void UpdateStep(int32 I, double Dt);

	// --- ИИ (ai.ts) ---
	void AiThink(int32 Me);
	bool IsDiver(int32 Who) const;
	EPunchKind AiPickType(int32 Me);
	void AiFootwork(int32 Me);
	void AiReactToPunch(int32 AiIdx, int32 HumanIdx, int32 Reps);

	FFightEvent& PushEvent(EFightEventKind Kind, int32 Attacker, int32 Defender, double Mag);

	// --- состояние (state.ts FightState) ---
	FRuntime Rt[2];
	bool bAi[2] = {false, true};
	FBoxingRng Rng;
	double T = 0;
	int32 TotalRounds = 3;
	bool bAllowDraw = false;
	double RoundSeconds = 180;
	double BreakSeconds = 60;
	bool bAutoProceed = true;
	double RoundK = 1;  // 55 / RoundSeconds — перевод раундовых счётчиков в масштаб веба

	int32 Round = 1;
	double TimeLeft = 180;
	double BreakLeft = 0;
	EFightPhase Phase = EFightPhase::Fighting;
	bool bHasDown = false;
	FDownState Down;

	FJudgeCard JudgeCards[3];
	TArray<FRoundResult> PerRound;
	bool bRoundScored = false;
	double RLanded[2] = {0, 0};
	double RDmgTaken[2] = {0, 0};
	int32 RKd[2] = {0, 0};
	double RPress[2] = {0, 0};
	double RKdSpent[2] = {0, 0};
	bool RPressKd[2] = {false, false};
	double TotLanded[2] = {0, 0};
	double LastMissAt[2] = {-1, -1};
	double Form[2] = {1, 1};

	TArray<FFightEvent> Events;
	FFightResult Result;
	bool bHasResult = false;
};
