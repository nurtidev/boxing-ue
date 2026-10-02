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
#include "FightStaging.h"

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
	// Старт боя: в своих углах и выход по гонгу (bCorners) или сразу по центру на DIST_START;
	// затем первые броски ГСЧ (темп ИИ, «форма дня») — их порядок от постановки не зависит.
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
	ERingStageKind GetStageKind() const { return Stage.Kind; }
	double GetFightTime() const { return T; }

	// Эффективность удара Kind на дистанции Dist (м) для размаха ReachCm: 0 — не достаёт,
	// 1 — идеальная дистанция (punches.ts rangeFactor). Публично — для подсказок UI.
	static double RangeFactor(EPunchKind Kind, double Dist, double ReachCm = 183.0);

	// Сдача (меню паузы UI, S-59): Loser проигрывает остановкой боя (RSC) в текущем раунде — как брошенный после
	// гонга бой веба. Раунд досчитывается судьями (ScoreRound), событие FightEnd. Бой уже окончен — false.
	bool Concede(int32 Loser)
	{
		if (bHasResult || Loser < 0 || Loser > 1) return false;
		Finish(1 - Loser, EFightMethod::RSC);
		return true;
	}

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
		// Постановка (corners.ts): идёт по стадии — скорость (0 — стоит), вектор хода; курс «по ходу».
		double WalkSpeed = 0, WalkVX = 0, WalkVZ = 0;
		bool bHasFaceYaw = false;
		double FaceYaw = 0;
		// Лежит: курс замёрз на миг падения (стоящий обходит его по пути в нейтральный угол — тело не крутится).
		bool bYawFrozen = false;
		double FrozenYaw = 0;
		// Отдых в углу (rounds.ts beginCornerRest/cornerRecover): итог считается по гонгу, набирается за перерыв.
		bool bHasRest = false;
		double RestStam0 = 0, RestStam1 = 0, RestWear0 = 0, RestWear1 = 0;
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

	// --- постановка раунда (corners.ts → FightStaging.cpp) ---
	struct FStageState
	{
		ERingStageKind Kind = ERingStageKind::None;
		double T = 0;
		bool bHasTarget[2] = {false, false};
		FVec2 Target[2];
		bool bArrived[2] = {true, true};
	};
	void PlaceInCorners();
	void BeginStage(ERingStageKind Kind, const FVec2* Target0, const FVec2* Target1);
	void BeginWalkout();
	void BeginRest();
	void BeginNeutral(int32 DownIdx);
	void BeginResume(int32 RoseIdx);
	void StartFighting();
	void EndStage();
	void UpdateStage(double Dt);
	void Arrive(int32 I);
	bool StageAllArrived() const;
	void BeginCornerRest();
	void CornerRecover(double Dt);

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
	// Давление «раунда веба» на длинном раунде (S-57). Шанс нокдауна/KO/RSC-H simulate выпуклый по урону раунда, а урон
	// 180-с раунда, сжатый RoundK, — сумма втрое большего числа попаданий: разброс втрое меньше, и нокдаунов выходило
	// вдвое меньше, чем в вебе. Поэтому при RoundK < 1 давление набирают «представители»: каждое чистое попадание идёт
	// в RPressRep с вероятностью RoundK и с полным весом — сумма того же числа попаданий, что за 55 с веба (среднее
	// и разброс как в вебе). При RoundK ≥ 1 (раунд ≤ 55 с) — прежняя формула RPress·RoundK, без лишних бросков ГСЧ.
	double RPressRep[2] = {0, 0};
	double PressOf(int32 I) const; // урон раунда в масштабе simulate (с SIM_KD_SCALE)
	double TotLanded[2] = {0, 0};
	// постановка (state.ts corners/stage/resumeGap/lyingAt/restT)
	bool bCorners = true;
	bool bGlassJaw = false; // dev: синий падает только на здоровье 0 и не встаёт (FFightConfig::bGlassJaw)
	FStageState Stage;
	double ResumeGap = 1.15;
	bool bHasLying = false;
	FVec2 Lying;
	double RestT = 0;
	double LastMissAt[2] = {-1, -1};
	double Form[2] = {1, 1};

	TArray<FFightEvent> Events;
	FFightResult Result;
	bool bHasResult = false;
};
