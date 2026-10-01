// Порт ядра интерактивного боя: web/src/engine/interactive.ts + interactive/*.ts.
// Константы — те же числа, что в TS; у каждого блока — ссылка на исходный модуль.
// Комментарии механики сокращены — подробное «почему» живёт в TS-исходниках.
#include "BoxingFightCore.h"

namespace BoxingFightConst
{
	constexpr double PiD = 3.14159265358979323846;

	// ---------- ring.ts / ringSize.ts ----------
	constexpr double ROPE_HALF = 3.05;            // ringSize.ts: 6.1 м внутри канатов
	constexpr double DIST_MIN = 0.9;
	constexpr double DIST_MAX = 2.0;
	constexpr double DIST_START = 1.15;
	constexpr double RING_HALF = ROPE_HALF - 0.28;
	constexpr double ROPE_ZONE = 0.22;

	// ---------- state.ts ----------
	constexpr double ROUND_SECONDS_REF = 55.0;    // ROUND_SECONDS веба: под него калиброван баланс раунда

	// ---------- mirror.ts (зеркало simulate.ts) ----------
	constexpr double SIM_ROUND_SCALE = 1.1;
	constexpr double SIM_KD_SCALE = 1.05;
	constexpr double HEALTH_PER_WEAR = 0.9;
	constexpr double FLASH_SPREAD = 10.0;
	constexpr double GREEN_FADE_K = 7.0;
	constexpr double CLEAR_MARGIN = 4.5;

	// ---------- runtime.ts ----------
	constexpr double GAS_PENALTY = 0.35;
	constexpr double STAM_REGEN = 3.0;
	constexpr double BLOCK_REGEN = 0.5;

	// ---------- punches.ts (индекс — EPunchKind: jab, cross, hook, uppercut) ----------
	constexpr bool POWER_PUNCH[4] = {false, true, true, true};
	constexpr double PLAYER_PUNCH_DUR = 0.34;
	constexpr double AI_PUNCH_DUR[4] = {0.44, 0.56, 0.62, 0.66};
	constexpr double CONTACT_FRAC = 0.45;
	constexpr double REF_REACH = 183.0;
	struct FRangeZone
	{
		double BestLo, BestHi, Max, CloseMin;
	};
	constexpr FRangeZone RANGE[4] = {
		{1.1, 1.55, 1.85, 0.6},
		{1.05, 1.45, 1.75, 0.65},
		{DIST_MIN, 1.15, 1.4, 1.0},
		{DIST_MIN, 1.05, 1.28, 1.0},
	};
	constexpr double RANGE_FAR_MIN = 0.35;
	constexpr double BODY_ACC = 1.12;
	constexpr double BODY_HEALTH = 0.7;
	constexpr double BODY_STAM_BASE = 2.2;
	constexpr double BODY_STAM_PER_DMG = 1.4;
	constexpr double BODY_KD = 0.55;
	constexpr double BODY_COST = 0.3;
	constexpr double ARM_ACC[2] = {1.02, 0.97}; // ARM_MOD lead/rear
	constexpr double ARM_POW[2] = {0.95, 1.05};
	constexpr double TYPE_ACC[4] = {1.06, 0.96, 0.85, 0.76};
	constexpr double TYPE_POW[4] = {0.6, 1.15, 1.2, 1.35};
	constexpr double STAM_COST[4] = {1.6, 2.6, 2.8, 3.2};
	constexpr double PLAYER_STAM_K = 0.85;
	constexpr double STAM_HIT = 0.5;
	constexpr double EXH_SLOW = 0.7;
	constexpr double EMPTY_POW = 0.4;
	constexpr double EMPTY_ACC = 0.7;
	constexpr double EMPTY_SCORE = 0.3;

	// ---------- style.ts (индекс — EBoxStyle: technical, volume, puncher, pressure, counter, speed, balanced) ----------
	constexpr double SIM_STYLE_OUTPUT[7] = {47, 58, 46, 54, 45, 52, 50};
	constexpr double AI_MIX[7][4] = {
		{0.5, 0.28, 0.14, 0.08},  // technical
		{0.4, 0.3, 0.2, 0.1},     // volume
		{0.18, 0.34, 0.3, 0.18},  // puncher
		{0.3, 0.3, 0.25, 0.15},   // pressure
		{0.3, 0.34, 0.22, 0.14},  // counter
		{0.48, 0.3, 0.14, 0.08},  // speed
		{0.4, 0.3, 0.2, 0.1},     // balanced
	};
	constexpr double AI_PREF_DIST[7] = {1.4, 1.08, 1.02, 0.98, 1.34, 1.45, 1.2};
	constexpr double AI_BODY[7] = {0.1, 0.22, 0.18, 0.24, 0.1, 0.08, 0.14};
	constexpr double AI_GUARD_BONUS[7] = {0.12, -0.05, -0.02, -0.05, 0.14, 0.06, 0.0};
	constexpr int32 STYLE_BALANCED = 6;

	// ---------- defense.ts ----------
	constexpr double BLOCK_TIRE_START = 1.0;
	constexpr double BLOCK_TIRE_FULL = 3.5;
	constexpr double BLOCK_TIRE_LEAK = 1.4;
	constexpr double BLOCK_TIRE_DRAIN = 1.4;
	constexpr double GUARD_DECAY = 0.5;
	constexpr double GUARD_BREAK = 2.6;
	constexpr double GUARD_PRESSURE_LEAK = 0.12;
	constexpr double GUARD_PRESSURE_COST = 0.2;
	constexpr double GUARD_BREAK_LOCK = 0.6;
	constexpr double GUARD_BREAK_STAGGER = 0.22;
	constexpr double GUARD_BREAK_ACC = 0.12;
	constexpr double GUARD_OPEN = 0.22;
	constexpr double GUARD_OPEN_AFTER = 0.8;
	constexpr double EMPTY_BLOCK_LEAK = 1.5;
	constexpr double SLIP_LATE = 0.06;
	constexpr double SLIP_LATE_MUL = 0.75;
	constexpr double SLIP_PERFECT_LO = 0.1;
	constexpr double SLIP_PERFECT_HI = 0.24;
	constexpr double SLIP_PERFECT_MUL = 1.12;
	constexpr double SLIP_EARLY_MUL = 0.6;
	constexpr double SLIP_DODGE_CAP = 0.85;
	constexpr double BODY_VS_SLIP = 0.35;
	constexpr double SLIP_RECOVER = 0.24;
	constexpr double SLIP_CAUGHT_ACC = 0.12;
	constexpr double SLIP_CAUGHT_DMG = 1.3;
	constexpr double WHIFF_STAGGER = 0.18;
	constexpr double WHIFF_OPEN = 0.45;
	constexpr double WHIFF_OPEN_DEF = 0.85;
	constexpr double COUNTER_ACC = 0.15;
	constexpr double COUNTER_ACC_CAP = 0.95;
	constexpr double COUNTER_DMG = 1.25;
	constexpr double COUNTER_SCORE = 1.3;
	constexpr int32 AI_SHORT_AFTER = 2;
	constexpr double AI_SHORT_MEMORY = 5;
	constexpr double AI_SHORT_FOR = 4;
	constexpr double AI_SHORT_DUR = 0.72;
	constexpr double AI_TURTLE_AFTER = 0.8;
	constexpr double AI_TURTLE_INTERVAL = 0.42;

	// ---------- guard.ts ----------
	constexpr double SLIP_WINDOW = 0.36;
	constexpr double SLIP_COOLDOWN = 0.62;
	constexpr double SLIP_STAM = 1.0;
	constexpr double COUNTER_WINDOW = 0.62;
	constexpr double UPPERCUT_VS_SLIP = 0.7;
	constexpr double BLOCK_LEAK_STRAIGHT = 0.16;
	constexpr double BLOCK_LEAK_ROUND = 0.32;
	constexpr double STAM_BLOCK_POWER = 3.4;
	constexpr double STAM_BLOCK_JAB = 0.8;

	// ---------- attack.ts ----------
	constexpr double HURT_BASE = 0.5;
	constexpr double HURT_FORM = 0.9;

	// ---------- footwork.ts ----------
	constexpr double STEP_LEN = 0.2;
	constexpr double STEP_DUR = 0.24;
	constexpr double STEP_COOLDOWN = 0.34;
	constexpr double STEP_STAM = 0.7;
	constexpr double LAT_LEN = 0.24;
	constexpr double LAT_DUR = 0.26;
	constexpr double LAT_STAM = 0.8;
	constexpr double PIVOT_LEN = 0.55;
	constexpr double PIVOT_DUR = 0.22;
	constexpr double PIVOT_COOLDOWN = 0.6;
	constexpr double PIVOT_STAM = 1.3;
	constexpr double CUT_LEN = 0.26;
	constexpr double ANGLE_WINDOW = 0.4;
	constexpr double ANGLE_LAT = 0.6;
	constexpr double ANGLE_DEF_PEN = 0.15;
	constexpr double ANGLE_SLIP_PEN = 0.3;
	constexpr double ANGLE_GUARD_PEN = 0.4;
	constexpr double ROPE_SLIP_PEN = 0.2;
	constexpr double ROPE_DMG = 0.1;

	// ---------- knockdown.ts ----------
	constexpr double COUNT_STEP = 0.75;
	constexpr int32 RISE_LIMIT_COUNT = 10;
	constexpr double RISE_PER_TAP = 0.1;
	constexpr double RISE_KD_HARDER = 0.25;
	constexpr double RISE_LEAK = 0.2;

	// ---------- player.ts ----------
	constexpr double GASSED_FLASH_S = 0.45;

	// ---------- ai.ts ----------
	constexpr double HURT_AGG = 0.9;
	constexpr double PUNISH_COOLDOWN = 5.0;
	constexpr int32 READ_MEMORY = 5;
	constexpr double READ_STEP = 0.14;
	constexpr double READ_MAX = 0.56;
	constexpr double GASSED_AT = 0.35;
	constexpr double GASSED_PUSH = 0.4;
	constexpr double AI_PACE_AT = 0.45;
	constexpr double AI_PACE_K = 1.6;
	constexpr double AI_DIVER_JAB = 1.8;
	constexpr double AI_DIVER_BODY = 0.3;
	constexpr double AI_BLOCK_HOLD = 0.12;
	constexpr double AI_GUARD_CAP = 0.85;
	constexpr double DIST_START_TH = 0.12;
	constexpr double DIST_DONE_TH = 0.08;
	constexpr double SETTLE_PAUSE = 1.6;
	constexpr double REVERSE_GAP = 0.75;

	// ---------- hud.ts ----------
	constexpr double POSE_CONTACT = 0.5;
}

using namespace BoxingFightConst;

namespace
{
	int32 KindIdx(EPunchKind K) { return static_cast<int32>(K); }
	int32 StyleIdx(EBoxStyle S) { return static_cast<int32>(S); }
	int32 ArmIdx(EPunchArm A) { return static_cast<int32>(A); }

	double ClampD(double V, double Lo, double Hi) { return FMath::Max(Lo, FMath::Min(Hi, V)); }
	double Hypot(double X, double Z) { return FMath::Sqrt(X * X + Z * Z); }
	double ClampRing(double V) { return ClampD(V, -RING_HALF, RING_HALF); }

	bool IsLateral(EStepKind K) { return K != EStepKind::Fwd && K != EStepKind::Back; }
	int32 SideOf(EStepKind K) { return (K == EStepKind::Left || K == EStepKind::PivotL) ? 1 : -1; }

	// punches.ts armFor: джеб/хук — lead, кросс/апперкот — rear.
	EPunchArm ArmFor(EPunchKind K)
	{
		return (K == EPunchKind::Cross || K == EPunchKind::Uppercut) ? EPunchArm::Rear : EPunchArm::Lead;
	}

	EPunchType ToPublicPunch(EPunchKind K, EPunchArm A)
	{
		switch (K)
		{
		case EPunchKind::Jab: return EPunchType::Jab;
		case EPunchKind::Cross: return EPunchType::Cross;
		case EPunchKind::Hook: return A == EPunchArm::Lead ? EPunchType::HookL : EPunchType::HookR;
		default: return A == EPunchArm::Lead ? EPunchType::UpperL : EPunchType::UpperR;
		}
	}

	// ---------- style.ts: интервалы и нормировки по набору ударов ----------
	double AiInterval(int32 S) { return 50.0 / SIM_STYLE_OUTPUT[S]; }
	double MixMeanAcc(int32 S)
	{
		double Acc = 0;
		for (int32 K = 0; K < 4; ++K) Acc += AI_MIX[S][K] * TYPE_ACC[K];
		return Acc;
	}
	double MixMeanAccPow(int32 S)
	{
		double Acc = 0;
		for (int32 K = 0; K < 4; ++K) Acc += AI_MIX[S][K] * TYPE_ACC[K] * TYPE_POW[K];
		return Acc;
	}
	double MixMeanCost(int32 S)
	{
		double Acc = 0;
		for (int32 K = 0; K < 4; ++K) Acc += AI_MIX[S][K] * STAM_COST[K];
		return Acc;
	}
	double AiAccNorm(int32 S) { return 1.0 / MixMeanAcc(S); }
	double AiPowNorm(int32 S) { return MixMeanAcc(S) / MixMeanAccPow(S); }
	double AiStamNorm(int32 S)
	{
		return ((MixMeanCost(STYLE_BALANCED) / MixMeanCost(S)) * AiInterval(S)) / AiInterval(STYLE_BALANCED);
	}
	double PlayerAccNorm() { return AiAccNorm(STYLE_BALANCED); }
	double PlayerPowNorm() { return AiPowNorm(STYLE_BALANCED); }

	// ---------- mirror.ts: формулы-зеркала simulate.ts ----------
	double LandProb(const FFighterSetup& Att, const FFighterSetup& Dfn, double FormAtt, double FormDef)
	{
		const double ReachEdge = (static_cast<double>(Att.ReachCm) - Dfn.ReachCm) * 0.25;
		const double Offense = (Att.Stats.Technique * 0.5 + Att.Stats.HandSpeed * 0.5) * FormAtt;
		const double Defense = (Dfn.Stats.Defense * 0.55 + Dfn.Stats.Footwork * 0.45) * FormDef;
		const double Raw = 34 + (Offense - Defense) * 0.55 + ReachEdge;
		return ClampD(Raw, 5, 70) / 100;
	}
	double DamagePerHit(const FFighterSetup& Att, const FFighterSetup& Dfn)
	{
		// massForPower = durabilityMass = WeightKg (проекции веса в ядре нет).
		const double PowerAbs = Att.Stats.Power * FMath::Sqrt(static_cast<double>(Att.WeightKg) / 70.0);
		const double DurabAbs = Dfn.Stats.Chin * FMath::Sqrt(static_cast<double>(Dfn.WeightKg) / 70.0);
		const double Ratio = PowerAbs / FMath::Max(20.0, DurabAbs);
		return FMath::Pow(Ratio, 1.4);
	}
	double KnockdownChanceF(double Pressure, double Chin)
	{
		const double X = (Pressure - Chin * 0.55) / 45;
		if (X <= 0) return 0;
		return FMath::Min(0.35, X * X * 0.6);
	}

	// ---------- defense.ts: чистые формулы блока/нырка ----------
	double ArmsTired(double HeldSec)
	{
		return ClampD((HeldSec - BLOCK_TIRE_START) / (BLOCK_TIRE_FULL - BLOCK_TIRE_START), 0, 1);
	}
	double BlockLeakMul(double HeldSec, double Pressure)
	{
		return (1 + BLOCK_TIRE_LEAK * ArmsTired(HeldSec)) * (1 + GUARD_PRESSURE_LEAK * Pressure);
	}
	double BlockCostMul(double HeldSec, double Pressure)
	{
		return (1 + ArmsTired(HeldSec)) * (1 + GUARD_PRESSURE_COST * Pressure);
	}
	double GuardPressureOf(EPunchKind K) { return K == EPunchKind::Jab ? 0.4 : 1; }
	bool GuardBreaks(EPunchKind K, double Pressure) { return K != EPunchKind::Jab && Pressure >= GUARD_BREAK; }
	double BlockRegenMul(double HeldSec, double BaseMul, double Regen)
	{
		const double K = ArmsTired(HeldSec);
		return BaseMul * (1 - K) - (BLOCK_TIRE_DRAIN / FMath::Max(0.01, Regen)) * K;
	}
	double GuardIntegrity(double HeldSec, double Pressure)
	{
		return ClampD(1 - FMath::Max(ArmsTired(HeldSec), Pressure / GUARD_BREAK), 0, 1);
	}
	double SlipTiming(double Lead)
	{
		if (Lead >= SLIP_PERFECT_LO && Lead <= SLIP_PERFECT_HI) return SLIP_PERFECT_MUL;
		if (Lead > SLIP_PERFECT_HI) return SLIP_EARLY_MUL;
		if (Lead < SLIP_LATE) return SLIP_LATE_MUL;
		return 1;
	}

	// ai.ts predictableReps: сколько раз подряд шёл этот удар (или связка из двух).
	int32 PredictableReps(const TArray<int32>& Recent, int32 Key)
	{
		TArray<int32> Seq = Recent;
		Seq.Add(Key);
		const int32 N = Seq.Num();
		int32 Same = 0;
		for (int32 K = N - 2; K >= 0 && Seq[K] == Key; --K) ++Same;
		int32 Period2 = 0;
		for (int32 K = N - 3; K >= 0 && Seq[K] == Seq[K + 2]; --K) ++Period2;
		return FMath::Max(Same, Period2 / 2);
	}
}

// ======================================================================
// FRuntime (runtime.ts)
// ======================================================================
double FBoxingFightCore::FRuntime::Fatigue() const
{
	return ClampD(Stamina / MaxStam, 0.45, 1);
}
double FBoxingFightCore::FRuntime::StamCap() const
{
	return MaxStam * ClampD(Tank / FMath::Max(1.0, static_cast<double>(Prof.Stats.Stamina)), 0.3, 1);
}
double FBoxingFightCore::FRuntime::FatigueForm() const
{
	return 0.7 + 0.3 * Fatigue();
}
double FBoxingFightCore::FRuntime::PlayerForm() const
{
	const double Cap = StamCap();
	const double Dist = 0.7 + 0.3 * FMath::Max(0.45, Cap / MaxStam);
	const double Gas = 1 - GAS_PENALTY * FMath::Max(0.0, 1 - Stamina / FMath::Max(1.0, 0.5 * Cap));
	return Dist * Gas;
}
double FBoxingFightCore::FRuntime::Health() const
{
	return ClampD(100 - Accumulated * HEALTH_PER_WEAR, 0, 100);
}

// ======================================================================
// Старт (state.ts FightState constructor)
// ======================================================================
void FBoxingFightCore::Init(const FFightConfig& Config)
{
	for (int32 I = 0; I < 2; ++I)
	{
		FRuntime Fresh;
		Fresh.Prof = Config.Fighters[I];
		Fresh.MaxStam = FMath::Max(30.0, static_cast<double>(Fresh.Prof.Stats.Stamina));
		Fresh.Stamina = Fresh.MaxStam;
		Fresh.Tank = Fresh.Prof.Stats.Stamina;
		Fresh.SlipDir = I == 0 ? 1 : -1; // state.ts slipDir = [1, -1]
		Rt[I] = Fresh;
		bAi[I] = Config.Fighters[I].bAiControlled;
	}
	Rng.Seed(Config.Seed != 0 ? Config.Seed : 1u); // `opts.seed >>> 0 || 1`
	T = 0;
	TotalRounds = FMath::Max(1, Config.Rounds);
	bAllowDraw = Config.bAllowDraw;
	RoundSeconds = FMath::Max(5.0, static_cast<double>(Config.RoundSeconds));
	BreakSeconds = FMath::Max(0.0, static_cast<double>(Config.BreakSeconds));
	bAutoProceed = Config.bAutoProceed;
	RoundK = ROUND_SECONDS_REF / RoundSeconds;
	Round = 1;
	TimeLeft = RoundSeconds;
	BreakLeft = 0;
	Phase = EFightPhase::Fighting;
	bHasDown = false;
	Down = FDownState();
	for (int32 K = 0; K < 3; ++K) JudgeCards[K] = FJudgeCard();
	PerRound.Reset();
	bRoundScored = false;
	for (int32 I = 0; I < 2; ++I)
	{
		RLanded[I] = 0;
		RDmgTaken[I] = 0;
		RKd[I] = 0;
		RPress[I] = 0;
		RKdSpent[I] = 0;
		RPressKd[I] = false;
		TotLanded[I] = 0;
		LastMissAt[I] = -1;
	}
	Events.Reset();
	Result = FFightResult();
	bHasResult = false;

	PlaceFighters();
	// Порядок бросков — как в state.ts: сначала синий (ИИ), затем красный (если ИИ), затем форма дня.
	if (bAi[1]) Rt[1].NextAiAt = 0.8 + Rng.Range(0, AiInterval(StyleIdx(Rt[1].Prof.Style)));
	if (bAi[0]) Rt[0].NextAiAt = 0.8 + Rng.Range(0, AiInterval(StyleIdx(Rt[0].Prof.Style)));
	Form[0] = 0.9 + 0.2 * Rng.Next();
	Form[1] = 0.9 + 0.2 * Rng.Next();
}

// ======================================================================
// Главный цикл (loop.ts)
// ======================================================================
void FBoxingFightCore::Tick(float DtRaw)
{
	if (Phase == EFightPhase::Over) return;
	const double Dt = ClampD(static_cast<double>(DtRaw), 0, 0.05);

	// Перерыв: в вебе ждёт proceed() от UI; здесь — таймер (или явный Proceed).
	if (Phase == EFightPhase::Between)
	{
		if (bAutoProceed)
		{
			BreakLeft -= Dt;
			if (BreakLeft <= 0) Proceed();
		}
		return;
	}
	T += Dt;

	if (Phase == EFightPhase::Down)
	{
		UpdateCount(Dt);
		return;
	}

	TimeLeft -= Dt;
	for (int32 I = 0; I < 2; ++I) UpdateFighter(I, Dt);
	for (int32 I = 0; I < 2; ++I)
	{
		if (bAi[I]) AiThink(I);
	}
	ResolvePunches();

	if (TimeLeft <= 0 && Phase == EFightPhase::Fighting) EndRound();
}

void FBoxingFightCore::UpdateFighter(int32 I, double Dt)
{
	FRuntime& R = Rt[I];
	if (R.bHasPunch && T >= R.Punch.End) R.bHasPunch = false;
	const double Held = UpdateGuard(I, Dt);
	RegenStamina(I, Held, Dt);
	UpdateStep(I, Dt);
}

// guard.ts updateGuard
double FBoxingFightCore::UpdateGuard(int32 I, double Dt)
{
	FRuntime& R = Rt[I];
	if (R.BlockUntil > 0 && T >= R.BlockUntil)
	{
		R.bBlocking = false;
		R.BlockUntil = 0;
	}
	if (!bAi[I] && R.bBlockWanted && !R.bBlocking) TryRaiseBlock(R);
	if (R.bBlocking && R.BlockSince < 0) R.BlockSince = T;
	if (!R.bBlocking && R.BlockSince >= 0)
	{
		if (T - R.BlockSince >= GUARD_OPEN_AFTER) R.BlockReadyAt = FMath::Max(R.BlockReadyAt, T + GUARD_OPEN);
		R.BlockSince = -1;
	}
	R.GuardPressure = FMath::Max(0.0, R.GuardPressure - GUARD_DECAY * Dt);
	return R.bBlocking ? T - R.BlockSince : 0;
}

// runtime.ts regenStamina — восстановление, когда не бьёт (в блоке — меньше, в долгом — таяние).
void FBoxingFightCore::RegenStamina(int32 I, double Held, double Dt)
{
	FRuntime& R = Rt[I];
	const double RegenMul = R.bBlocking ? BlockRegenMul(Held, BLOCK_REGEN, STAM_REGEN) : 1;
	if (!R.bHasPunch)
	{
		const double D = STAM_REGEN * RegenMul * Dt;
		if (D < 0) R.Stamina = FMath::Max(0.0, R.Stamina + D);
		else if (R.Stamina < R.StamCap()) R.Stamina = FMath::Min(R.StamCap(), R.Stamina + D);
	}
}

// ======================================================================
// Ввод (player.ts)
// ======================================================================
bool FBoxingFightCore::ApplyAction(int32 Fighter, EFightAction Action, EPunchTarget Target)
{
	if (Fighter < 0 || Fighter > 1) return false;
	if (Action == EFightAction::Proceed) return Proceed();
	if (bAi[Fighter]) return false;
	switch (Action)
	{
	case EFightAction::Jab: return HumanPunch(Fighter, EPunchKind::Jab, EPunchArm::Lead, Target);
	case EFightAction::Cross: return HumanPunch(Fighter, EPunchKind::Cross, EPunchArm::Rear, Target);
	case EFightAction::HookL: return HumanPunch(Fighter, EPunchKind::Hook, EPunchArm::Lead, Target);
	case EFightAction::HookR: return HumanPunch(Fighter, EPunchKind::Hook, EPunchArm::Rear, Target);
	case EFightAction::UpperL: return HumanPunch(Fighter, EPunchKind::Uppercut, EPunchArm::Lead, Target);
	case EFightAction::UpperR: return HumanPunch(Fighter, EPunchKind::Uppercut, EPunchArm::Rear, Target);
	case EFightAction::BlockStart: return HumanBlock(Fighter, true);
	case EFightAction::BlockEnd: return HumanBlock(Fighter, false);
	case EFightAction::SlipLeft: return HumanSlip(Fighter, -1);
	case EFightAction::SlipRight: return HumanSlip(Fighter, 1);
	case EFightAction::StepFwd: return Phase == EFightPhase::Fighting && StartStep(Fighter, EStepKind::Fwd);
	case EFightAction::StepBack: return Phase == EFightPhase::Fighting && StartStep(Fighter, EStepKind::Back);
	case EFightAction::StepLeft: return Phase == EFightPhase::Fighting && StartStep(Fighter, EStepKind::Left);
	case EFightAction::StepRight: return Phase == EFightPhase::Fighting && StartStep(Fighter, EStepKind::Right);
	case EFightAction::PivotL: return Phase == EFightPhase::Fighting && StartStep(Fighter, EStepKind::PivotL);
	case EFightAction::PivotR: return Phase == EFightPhase::Fighting && StartStep(Fighter, EStepKind::PivotR);
	case EFightAction::RiseTap: return HumanRiseTap(Fighter);
	default: return false;
	}
}

bool FBoxingFightCore::HumanPunch(int32 I, EPunchKind Kind, EPunchArm Arm, EPunchTarget Target)
{
	FRuntime& Me = Rt[I];
	if (Phase != EFightPhase::Fighting) return false;
	if (Me.bBlocking || Me.bHasPunch || T < Me.StaggerUntil) return false;
	// Пустой бак — удар не выходит («нет сил»); событие не чаще раза в GASSED_FLASH_S.
	if (Me.Stamina < STAM_COST[KindIdx(Kind)] * PLAYER_STAM_K)
	{
		if (T >= Me.GassedUntil) PushEvent(EFightEventKind::Gassed, I, -1, 0);
		Me.GassedUntil = T + GASSED_FLASH_S;
		return false;
	}
	StartPunch(I, Kind, true, Arm, Target);
	// Однообразие (тип+цель): ИИ «читает» повторы.
	const int32 Key = KindIdx(Kind) * 2 + static_cast<int32>(Target);
	const int32 Reps = PredictableReps(Me.Recent, Key);
	Me.Recent.Add(Key);
	if (Me.Recent.Num() > READ_MEMORY) Me.Recent.RemoveAt(0);
	const int32 Foe = 1 - I;
	if (bAi[Foe]) AiReactToPunch(Foe, I, Reps);
	return true;
}

bool FBoxingFightCore::HumanSlip(int32 I, int32 Dir)
{
	FRuntime& Me = Rt[I];
	if (Phase != EFightPhase::Fighting) return false;
	if (Me.bBlocking || Me.bHasPunch || T < Me.StaggerUntil || T < Me.SlipReadyAt) return false;
	Me.SlipUntil = T + SLIP_WINDOW;
	Me.SlipStart = T;
	Me.SlipReadyAt = T + SLIP_COOLDOWN;
	Me.SlipDir = Dir;
	Me.Stamina = FMath::Max(0.0, Me.Stamina - SLIP_STAM);
	Me.SlipTimes.Add(T);
	if (Me.SlipTimes.Num() > 4) Me.SlipTimes.RemoveAt(0);
	return true;
}

// Кнопка блока запоминается: поднимется сам, как только станет можно (guard.ts updateGuard).
bool FBoxingFightCore::HumanBlock(int32 I, bool bOn)
{
	FRuntime& Me = Rt[I];
	Me.bBlockWanted = bOn;
	if (bOn) TryRaiseBlock(Me);
	else Me.bBlocking = false;
	return true;
}

// knockdown.ts playerRiseTap
bool FBoxingFightCore::HumanRiseTap(int32 I)
{
	if (Phase != EFightPhase::Down || !bHasDown || Down.Who != I) return false;
	const double KdHarder = 1 + RISE_KD_HARDER * FMath::Max(0, Rt[I].Kd - 1);
	Down.RiseProgress = FMath::Min(1.0, Down.RiseProgress + (RISE_PER_TAP * (1 - 0.5 * Down.KoChance)) / KdHarder);
	if (Down.RiseProgress >= 1) RiseUp();
	return true;
}

// ======================================================================
// Атака (attack.ts)
// ======================================================================
void FBoxingFightCore::StartPunch(int32 I, EPunchKind Kind, bool bHasArm, EPunchArm Arm, EPunchTarget Target)
{
	FRuntime& R = Rt[I];
	const int32 K = KindIdx(Kind);
	// Усталость замедляет руки (против человека); ИИ против ИИ — без этого (паритет с simulate).
	const double Tired = (bAi[I] && bAi[1 - I]) ? 0 : 1 - FMath::Min(1.0, R.Stamina / R.MaxStam);
	const double Short = (bAi[I] && T < R.ShortUntil) ? AI_SHORT_DUR : 1;
	const double Dur = (bAi[I] ? AI_PUNCH_DUR[K] * Short : PLAYER_PUNCH_DUR) * (1 + EXH_SLOW * Tired * Tired);
	const double StamK = bAi[I] ? AiStamNorm(StyleIdx(R.Prof.Style)) : PLAYER_STAM_K;
	const double Cost = STAM_COST[K] * StamK + (Target == EPunchTarget::Body ? BODY_COST : 0);
	const bool bRound = Kind == EPunchKind::Hook || Kind == EPunchKind::Uppercut;
	FPunchAct P;
	P.Kind = Kind;
	P.Arm = bRound ? (bHasArm ? Arm : ArmFor(Kind)) : ArmFor(Kind);
	P.Target = Target;
	P.Start = T;
	P.Contact = T + Dur * CONTACT_FRAC;
	P.End = T + Dur;
	P.bResolved = false;
	P.bEmpty = !bAi[I] && R.Stamina < Cost;
	R.Punch = P;
	R.bHasPunch = true;
	R.Stamina = FMath::Max(0.0, R.Stamina - Cost);
	R.bBlocking = false;
	R.BlockUntil = 0;
}

void FBoxingFightCore::ResolvePunches()
{
	for (int32 I = 0; I < 2; ++I)
	{
		// Отличие от TS: после досрочки второй удар того же тика уже не резолвится.
		if (Phase == EFightPhase::Over) return;
		FRuntime& R = Rt[I];
		if (R.bHasPunch && !R.Punch.bResolved && T >= R.Punch.Contact)
		{
			R.Punch.bResolved = true;
			const FPunchAct Copy = R.Punch;
			ResolveContact(I, Copy);
		}
	}
}

void FBoxingFightCore::ResolveContact(int32 AttIdx, const FPunchAct& Punch)
{
	const int32 DefIdx = 1 - AttIdx;
	FRuntime& Att = Rt[AttIdx];
	FRuntime& Def = Rt[DefIdx];
	if (Def.Kd > 0 && Phase == EFightPhase::Down) return; // защищающийся падает

	const int32 K = KindIdx(Punch.Kind);
	const EPunchType Pub = ToPublicPunch(Punch.Kind, Punch.Arm);
	// Вне досягаемости — промах в воздух (без бросков ГСЧ).
	const double Rf = RangeFactor(Punch.Kind, Distance(), Att.Prof.ReachCm);
	if (Rf <= 0)
	{
		FFightEvent& E = PushEvent(EFightEventKind::Miss, AttIdx, DefIdx, 0);
		E.Punch = Pub;
		E.Arm = Punch.Arm;
		E.Target = Punch.Target;
		LastMissAt[AttIdx] = T;
		return;
	}
	const bool bRound = Punch.Kind == EPunchKind::Hook || Punch.Kind == EPunchKind::Uppercut;
	const double ArmAcc = bRound ? ARM_ACC[ArmIdx(Punch.Arm)] : 1;
	const double ArmPow = bRound ? ARM_POW[ArmIdx(Punch.Arm)] : 1;
	const bool bBody = Punch.Target == EPunchTarget::Body;

	const bool bCounter = T < Att.CounterUntil;
	const double FatigueAtt = bAi[AttIdx] ? 1 : Att.PlayerForm();
	const double FormAtt = Form[AttIdx] * FatigueAtt * (T < Att.HurtUntil ? HURT_FORM : 1) * (bCounter ? 1.15 : 1);
	const double Angle = AngleOf(AttIdx);
	const double Rope = RopePen(DefIdx);
	const bool bWhiffed = T < Def.WhiffOpenUntil;
	const double FormDef = Form[DefIdx] * (1 - ANGLE_DEF_PEN * Angle) * (bWhiffed ? WHIFF_OPEN_DEF : 1);
	const double AccNorm = bAi[AttIdx] ? AiAccNorm(StyleIdx(Att.Prof.Style)) : PlayerAccNorm();
	const double EmptyAcc = Punch.bEmpty ? EMPTY_ACC : 1;
	double P = LandProb(Att.Prof, Def.Prof, FormAtt, FormDef) * TYPE_ACC[K] * AccNorm * ArmAcc * Rf * (bBody ? BODY_ACC : 1) * EmptyAcc;
	if (bCounter) P = FMath::Min(COUNTER_ACC_CAP, P + COUNTER_ACC);
	// Нырок не по таймингу: окно кончилось, а боец ещё выходит из нырка — пойман.
	const bool bCaught = Def.SlipStart >= 0 && T >= Def.SlipUntil && T < Def.SlipUntil + SLIP_RECOVER;
	if (bCaught) P = FMath::Min(0.95, P + SLIP_CAUGHT_ACC);

	if (T < Def.SlipUntil && TrySlip(AttIdx, Punch.Kind, Punch.Arm, bBody, Angle, Rope)) return;

	const double BaseDmg = DamagePerHit(Att.Prof, Def.Prof) * TYPE_POW[K] *
		(bAi[AttIdx] ? AiPowNorm(StyleIdx(Att.Prof.Style)) : PlayerPowNorm()) * ArmPow * (0.6 + 0.4 * Rf) *
		(0.7 + 0.3 * FormAtt) * (bCounter ? COUNTER_DMG : 1) * (bCaught ? SLIP_CAUGHT_DMG : 1) *
		(Punch.bEmpty ? EMPTY_POW : 1) * (1 + ROPE_DMG * Rope);
	const double HpDmg = bBody ? BaseDmg * BODY_HEALTH : BaseDmg;

	bool bGuardBreak = false;
	if (Def.bBlocking)
	{
		if (BlockPunch(DefIdx, AttIdx, Punch, HpDmg)) return;
		bGuardBreak = true;
		P = FMath::Min(0.95, P + GUARD_BREAK_ACC);
	}

	if (Rng.Next() < P)
	{
		// Раундовые счётчики — в масштабе раунда веба (RoundK = 55 / RoundSeconds).
		Def.Accumulated += HpDmg * SIM_ROUND_SCALE * RoundK;
		RPress[DefIdx] += HpDmg;
		Def.Stamina = FMath::Max(0.0, Def.Stamina - STAM_HIT - (bBody ? BODY_STAM_BASE + BaseDmg * BODY_STAM_PER_DMG : 0));
		Def.HurtUntil = T + HURT_BASE + FMath::Min(0.5, HpDmg * 0.15);
		Def.HurtMag = ClampD(HpDmg / 2, 0.35, 1);
		Def.HurtTarget = Punch.Target;
		const double Clean = (Punch.bEmpty ? EMPTY_SCORE : 1) * (bCounter ? COUNTER_SCORE : 1);
		RLanded[AttIdx] += Clean;
		TotLanded[AttIdx] += Clean;
		RDmgTaken[DefIdx] += BaseDmg;
		FFightEvent& E = PushEvent(EFightEventKind::Hit, AttIdx, DefIdx, HpDmg);
		E.Punch = Pub;
		E.Arm = Punch.Arm;
		E.Target = Punch.Target;
		E.bCounter = bCounter;
		E.bGuardBreak = bGuardBreak;
		E.bCaught = bCaught;
		if (bCounter) Att.CounterUntil = 0;
		if (HpDmg > 2.2) Def.StaggerUntil = T + 0.28;
		TryKnockdown(AttIdx, DefIdx, Punch.Kind, FormAtt,
			(bBody ? BODY_KD : 1) * ArmPow * (0.6 + 0.4 * Rf) * (Punch.bEmpty ? EMPTY_POW : 1));
	}
	else
	{
		FFightEvent& E = PushEvent(EFightEventKind::Miss, AttIdx, DefIdx, 0);
		E.Punch = Pub;
		E.Arm = Punch.Arm;
		E.Target = Punch.Target;
		LastMissAt[AttIdx] = T;
	}
}

// ======================================================================
// Защита (guard.ts)
// ======================================================================
void FBoxingFightCore::TryRaiseBlock(FRuntime& R)
{
	if (Phase != EFightPhase::Fighting || R.bHasPunch || T < R.StaggerUntil || T < R.BlockReadyAt) return;
	R.bBlocking = true;
}

bool FBoxingFightCore::TrySlip(int32 AttIdx, EPunchKind Kind, EPunchArm Arm, bool bBody, double Angle, double Rope)
{
	const int32 DefIdx = 1 - AttIdx;
	FRuntime& Att = Rt[AttIdx];
	FRuntime& Def = Rt[DefIdx];
	double Dodge = ClampD(0.35 + (static_cast<double>(Def.Prof.Stats.Footwork) + Def.Prof.Stats.Defense) / 300, 0.3, 0.85);
	if (Kind == EPunchKind::Uppercut) Dodge *= UPPERCUT_VS_SLIP;
	if (bBody) Dodge *= BODY_VS_SLIP;
	Dodge *= (1 - ANGLE_SLIP_PEN * Angle) * (1 - ROPE_SLIP_PEN * Rope);
	// Тайминг нырка решает только у человека (ИИ ныряет реактивно).
	const double Timing = !bAi[DefIdx] ? SlipTiming(T - Def.SlipStart) : 1;
	Dodge = FMath::Min(SLIP_DODGE_CAP, Dodge * Timing);
	if (Rng.Next() < Dodge)
	{
		Def.CounterUntil = T + COUNTER_WINDOW;
		Def.SlipStart = -1;
		Att.Stamina = FMath::Max(0.0, Att.Stamina - 0.4);
		Att.StaggerUntil = FMath::Max(Att.StaggerUntil, T + WHIFF_STAGGER);
		Att.WhiffOpenUntil = T + WHIFF_OPEN;
		Att.bBlocking = false;
		const double Now = T;
		Att.WhiffTimes.RemoveAll([Now](const double& X) { return !(Now - X < AI_SHORT_MEMORY); });
		Att.WhiffTimes.Add(T);
		if (bAi[AttIdx] && Att.WhiffTimes.Num() >= AI_SHORT_AFTER) Att.ShortUntil = T + AI_SHORT_FOR;
		FFightEvent& E = PushEvent(EFightEventKind::Slipped, AttIdx, DefIdx, 0);
		E.Punch = ToPublicPunch(Kind, Arm);
		E.Arm = Arm;
		E.bPerfect = Timing > 1;
		LastMissAt[AttIdx] = T;
		return true;
	}
	return false;
}

bool FBoxingFightCore::BlockPunch(int32 DefIdx, int32 AttIdx, const FPunchAct& Punch, double HpDmg)
{
	FRuntime& Def = Rt[DefIdx];
	const int32 K = KindIdx(Punch.Kind);
	const double Held = T - FMath::Max(0.0, Def.BlockSince);
	const double Cost = (POWER_PUNCH[K] ? STAM_BLOCK_POWER : STAM_BLOCK_JAB) * BlockCostMul(Held, Def.GuardPressure);
	if (!GuardBreaks(Punch.Kind, Def.GuardPressure))
	{
		const double Weak = Def.Stamina < Cost ? EMPTY_BLOCK_LEAK : 1;
		const double Leak = (POWER_PUNCH[K] ? BLOCK_LEAK_ROUND : BLOCK_LEAK_STRAIGHT) * BlockLeakMul(Held, Def.GuardPressure) * Weak;
		const double Dmg = HpDmg * FMath::Min(0.9, Leak);
		Def.Stamina = FMath::Max(0.0, Def.Stamina - Cost);
		Def.GuardPressure += GuardPressureOf(Punch.Kind);
		Def.Accumulated += Dmg * SIM_ROUND_SCALE * RoundK;
		RDmgTaken[DefIdx] += Dmg;
		FFightEvent& E = PushEvent(EFightEventKind::Blocked, AttIdx, DefIdx, Dmg);
		E.Punch = ToPublicPunch(Punch.Kind, Punch.Arm);
		E.Arm = Punch.Arm;
		E.Target = Punch.Target;
		return true;
	}
	// Блок пробит: руки разведены, короткий стан, удар идёт в открытую цель.
	Def.bBlocking = false;
	Def.BlockUntil = 0;
	Def.GuardPressure = 0;
	Def.Stamina = FMath::Max(0.0, Def.Stamina - Cost * 0.5);
	Def.BlockReadyAt = T + GUARD_BREAK_LOCK;
	Def.StaggerUntil = FMath::Max(Def.StaggerUntil, T + GUARD_BREAK_STAGGER);
	return false;
}

// ======================================================================
// Нокдауны (knockdown.ts)
// ======================================================================
void FBoxingFightCore::TryKnockdown(int32 AttIdx, int32 DefIdx, EPunchKind Kind, double FormAtt, double ShotMul)
{
	const FRuntime& Att = Rt[AttIdx];
	const FRuntime& Def = Rt[DefIdx];
	// (1) от давления раунда: шанс simulate «за раунд» выдаётся по ходу раунда.
	if (!RPressKd[DefIdx])
	{
		const double Pressure = RPress[DefIdx] * SIM_KD_SCALE * RoundK * (1 + Def.Accumulated / 110);
		const double F = KnockdownChanceF(Pressure, Def.Prof.Stats.Chin);
		const double Spent = RKdSpent[DefIdx];
		if (F > Spent)
		{
			RKdSpent[DefIdx] = F;
			if (Rng.Next() < (F - Spent) / (1 - Spent))
			{
				RPressKd[DefIdx] = true;
				Knockdown(AttIdx, DefIdx);
				return;
			}
		}
	}
	// (2) flash-нокдаун панчера — только от силовых.
	if (!POWER_PUNCH[KindIdx(Kind)]) return;
	const double Flash = FMath::Max(0.0, Att.Prof.Stats.Power * FormAtt - 84) * 0.0035 * FMath::Min(1.0, ShotMul);
	if (Flash <= 0) return;
	const double ChinResist = ClampD(1 - (static_cast<double>(Def.Prof.Stats.Chin) - 75) / 45, 0.2, 1.4);
	// Шанс «за раунд» размазан на FLASH_SPREAD попаданий (раунд длиннее — попаданий больше).
	if (Rng.Next() < (Flash * ChinResist) / (FLASH_SPREAD / RoundK)) Knockdown(AttIdx, DefIdx);
}

void FBoxingFightCore::Knockdown(int32 AttIdx, int32 DefIdx)
{
	FRuntime& Def = Rt[DefIdx];
	Def.Kd += 1;
	RKd[DefIdx] += 1;
	Def.Accumulated += 12;
	Def.bHasPunch = false;
	Def.bBlocking = false;
	Def.Step = EStepKind::None;
	Rt[AttIdx].Step = EStepKind::None;
	PushEvent(EFightEventKind::Knockdown, AttIdx, DefIdx, 1);
	// Три нокдауна за бой — остановка (RSC).
	if (Def.Kd >= 3)
	{
		Finish(AttIdx, EFightMethod::RSC);
		return;
	}
	// Шанс «не встать» — KO-проверка simulate.
	const double Severity = (RPress[DefIdx] * SIM_KD_SCALE * RoundK + Def.Accumulated * 0.12) /
		FMath::Max(20.0, static_cast<double>(Def.Prof.Stats.Chin));
	const double KoChance = FMath::Min(0.6, FMath::Max(0.0, Severity - 0.9) * 0.5 + (RKd[DefIdx] >= 2 ? 0.2 : 0));
	Phase = EFightPhase::Down;
	bHasDown = true;
	Down = FDownState();
	Down.Who = DefIdx;
	Down.KoChance = KoChance;
}

void FBoxingFightCore::UpdateCount(double Dt)
{
	if (!bHasDown) return;
	FDownState& D = Down;
	D.CountT += Dt;
	const int32 Count = static_cast<int32>(D.CountT / COUNT_STEP) + 1; // CountT ≥ 0 → усечение = floor
	if (!bAi[D.Who])
	{
		// Человек набивает подъём тапами (RiseTap); прогресс медленно утекает.
		if (D.RiseProgress >= 1)
		{
			RiseUp();
			return;
		}
		D.RiseProgress = FMath::Max(0.0, D.RiseProgress - Dt * RISE_LEAK);
		if (Count > RISE_LIMIT_COUNT) Finish(1 - D.Who, EFightMethod::KO);
	}
	else
	{
		if (!D.bAiDecided)
		{
			D.bAiDecided = true;
			D.bAiWillRise = Rng.Next() >= D.KoChance;
			D.AiRiseAt = Rng.Range(2.4, 7.2);
		}
		if (D.bAiWillRise && D.CountT >= D.AiRiseAt)
		{
			RiseUp();
			return;
		}
		if (!D.bAiWillRise && Count > RISE_LIMIT_COUNT)
		{
			Finish(1 - D.Who, EFightMethod::KO);
			return;
		}
		if (D.bAiWillRise && Count > RISE_LIMIT_COUNT) RiseUp(); // страховка
	}
}

void FBoxingFightCore::RiseUp()
{
	if (!bHasDown) return;
	FRuntime& R = Rt[Down.Who];
	R.HurtUntil = T + 0.9;
	R.HurtMag = 0.8;
	R.StaggerUntil = T + 0.7;
	R.Stamina = FMath::Max(0.0, R.Stamina - R.MaxStam * 0.12);
	bHasDown = false;
	Down = FDownState();
	Phase = EFightPhase::Fighting;
	for (int32 I = 0; I < 2; ++I)
	{
		if (bAi[I]) Rt[I].NextAiAt = T + 0.6;
	}
}

// ======================================================================
// Раунды и судьи (rounds.ts)
// ======================================================================
bool FBoxingFightCore::Proceed()
{
	if (Phase != EFightPhase::Between) return false;
	Round += 1;
	TimeLeft = RoundSeconds;
	bRoundScored = false;
	for (int32 I = 0; I < 2; ++I)
	{
		RLanded[I] = 0;
		RDmgTaken[I] = 0;
		RKd[I] = 0;
		RPress[I] = 0;
		RKdSpent[I] = 0;
		RPressKd[I] = false;
		FRuntime& R = Rt[I];
		// Угол: часть износа спадает, стамина назад (не выше потолка «бака»).
		R.Accumulated *= 0.72;
		R.Stamina = FMath::Min(R.StamCap(), R.Stamina + R.MaxStam * 0.28);
		R.bHasPunch = false;
		R.bBlocking = false;
		R.SlipUntil = 0;
		R.SlipReadyAt = 0;
		R.SlipStart = -1;
		R.CounterUntil = 0;
		R.BlockSince = -1;
		R.GuardPressure = 0;
		R.BlockReadyAt = 0;
		R.WhiffOpenUntil = 0;
		R.HurtUntil = 0;
		R.StaggerUntil = 0;
		R.Step = EStepKind::None;
		R.StepUntil = 0;
		R.StepReadyAt = 0;
		R.AngleUntil = 0;
	}
	PlaceFighters(); // постановки углов нет — сразу на стартовую дистанцию
	Phase = EFightPhase::Fighting;
	if (bAi[1]) Rt[1].NextAiAt = T + 0.7 + Rng.Range(0, 0.6);
	if (bAi[0]) Rt[0].NextAiAt = T + 0.7 + Rng.Range(0, 0.6);
	return true;
}

void FBoxingFightCore::EndRound()
{
	ScoreRound();
	PushEvent(EFightEventKind::RoundEnd, -1, -1, 0);
	// Любители: RSC-H — вчистую перебит по попаданиям и принял тяжёлый раунд.
	if (!bAllowDraw)
	{
		for (int32 I = 0; I < 2; ++I)
		{
			const int32 J = 1 - I;
			const double OneSided = (RLanded[J] - RLanded[I]) * SIM_ROUND_SCALE * RoundK;
			const double Beating = (RPress[I] * SIM_KD_SCALE * RoundK) / FMath::Max(20.0, static_cast<double>(Rt[I].Prof.Stats.Chin));
			if (OneSided > 10 && Beating > 0.4)
			{
				const double StopChance = FMath::Min(0.5, (OneSided - 10) * 0.02 + (Beating - 0.4) * 0.55);
				if (Rng.Next() < StopChance)
				{
					Finish(J, EFightMethod::RSC);
					return;
				}
			}
		}
	}
	if (Round >= TotalRounds)
	{
		DecideByCards();
	}
	else
	{
		Phase = EFightPhase::Between;
		BreakLeft = BreakSeconds;
	}
}

void FBoxingFightCore::ScoreRound()
{
	if (bRoundScored) return;
	bRoundScored = true;
	const double Landed[2] = {RLanded[0], RLanded[1]};
	const double DmgTaken[2] = {RDmgTaken[0], RDmgTaken[1]};
	const int32 RoundKd[2] = {RKd[0], RKd[1]};
	const double S = SIM_ROUND_SCALE * RoundK;
	// Расход «бака» на дистанцию — формула simulate (+ налог необстрелянности).
	for (int32 I = 0; I < 2; ++I)
	{
		FRuntime& R = Rt[I];
		const double GreenFade = (1 - static_cast<double>(R.Prof.Seasoning)) * (static_cast<double>(Round) / TotalRounds) * GREEN_FADE_K;
		R.Tank -= 6 + Landed[I] * S * 0.08 + DmgTaken[I] * S * 0.2 + GreenFade;
		R.Tank = FMath::Max(5.0, R.Tank);
	}
	const double ScoreI = (Landed[0] * 1.0 + DmgTaken[1] * 0.6) * S;
	const double ScoreJ = (Landed[1] * 1.0 + DmgTaken[0] * 0.6) * S;
	const double Margin = ScoreI - ScoreJ;
	const double SeasonBias = (static_cast<double>(Rt[0].Prof.Seasoning) - Rt[1].Prof.Seasoning) * 0.6;

	FRoundResult RR;
	RR.Round = Round;
	for (int32 Jd = 0; Jd < 3; ++Jd)
	{
		int32 Ca = 10;
		int32 Cb = 10;
		if (RoundKd[0] || RoundKd[1])
		{
			if (RoundKd[1]) Cb = 8 - (RoundKd[1] - 1);
			if (RoundKd[0]) Ca = 8 - (RoundKd[0] - 1);
			if (Ca == Cb)
			{
				if (Margin > 0.5) Cb -= 1;
				else if (Margin < -0.5) Ca -= 1;
			}
		}
		else if (FMath::Abs(Margin) >= CLEAR_MARGIN)
		{
			if (Margin > 0) Cb = 9;
			else Ca = 9;
		}
		else
		{
			// Близкий раунд — каждый судья видит чуть по-своему.
			const double Perceived = Margin + Rng.Range(-1.6, 1.6) + SeasonBias;
			if (FMath::Abs(Perceived) < 0.5)
			{
				// ровно — 10-10
			}
			else if (Perceived > 0) Cb = 9;
			else Ca = 9;
		}
		RR.JudgeCards[Jd].Red = Ca;
		RR.JudgeCards[Jd].Blue = Cb;
		JudgeCards[Jd].Red += Ca;
		JudgeCards[Jd].Blue += Cb;
	}
	for (int32 I = 0; I < 2; ++I)
	{
		RR.Landed[I] = static_cast<float>(Landed[I]);
		RR.Stamina[I] = static_cast<int32>(FMath::RoundToDouble(Rt[I].Stamina));
		RR.Knockdowns[I] = RoundKd[I];
		RR.Damage[I] = static_cast<int32>(FMath::RoundToDouble(DmgTaken[I]));
	}
	PerRound.Add(RR);
}

void FBoxingFightCore::DecideByCards()
{
	int32 RedJ = 0;
	int32 BlueJ = 0;
	for (int32 Jd = 0; Jd < 3; ++Jd)
	{
		if (JudgeCards[Jd].Red > JudgeCards[Jd].Blue) ++RedJ;
		else if (JudgeCards[Jd].Blue > JudgeCards[Jd].Red) ++BlueJ;
	}
	const int32 EvenJ = 3 - RedJ - BlueJ;

	if (RedJ >= 2 || BlueJ >= 2)
	{
		const int32 Winner = RedJ >= 2 ? 0 : 1;
		const int32 WJ = Winner == 0 ? RedJ : BlueJ;
		const int32 LJ = Winner == 0 ? BlueJ : RedJ;
		const EDecisionKind Dk = WJ == 3 ? EDecisionKind::Unanimous : (LJ == 0 ? EDecisionKind::Majority : EDecisionKind::Split);
		BuildResult(Winner, EFightMethod::Decision, Dk, 0);
	}
	else if (bAllowDraw)
	{
		const EDecisionKind Dk = EvenJ == 3 ? EDecisionKind::DrawUnanimous
			: (RedJ == BlueJ ? EDecisionKind::DrawSplit : EDecisionKind::DrawMajority);
		BuildResult(-1, EFightMethod::Draw, Dk, 0);
	}
	else
	{
		// Любители: ничьей нет — по суммам карт, затем по попаданиям.
		int32 TotRed = 0;
		int32 TotBlue = 0;
		for (int32 Jd = 0; Jd < 3; ++Jd)
		{
			TotRed += JudgeCards[Jd].Red;
			TotBlue += JudgeCards[Jd].Blue;
		}
		int32 Winner;
		if (TotRed != TotBlue) Winner = TotRed > TotBlue ? 0 : 1;
		else Winner = TotLanded[0] >= TotLanded[1] ? 0 : 1;
		const int32 WJ = Winner == 0 ? RedJ : BlueJ;
		const int32 LJ = Winner == 0 ? BlueJ : RedJ;
		const EDecisionKind Dk = WJ == 3 ? EDecisionKind::Unanimous
			: (WJ > LJ ? (LJ == 0 ? EDecisionKind::Majority : EDecisionKind::Split) : EDecisionKind::TieBreak);
		BuildResult(Winner, EFightMethod::Decision, Dk, 0);
	}
}

// Досрочка (KO/RSC): досчитать неполный раунд, чтобы итог был валиден.
void FBoxingFightCore::Finish(int32 Winner, EFightMethod Method)
{
	ScoreRound();
	bHasDown = false;
	Down = FDownState();
	BuildResult(Winner, Method, EDecisionKind::None, Round);
}

void FBoxingFightCore::BuildResult(int32 WinnerIndex, EFightMethod Method, EDecisionKind Decision, int32 StoppedRound)
{
	Result = FFightResult();
	Result.WinnerIndex = WinnerIndex;
	Result.Method = Method;
	Result.Decision = Decision;
	Result.StoppedRound = StoppedRound;
	for (int32 Jd = 0; Jd < 3; ++Jd) Result.JudgeTotals[Jd] = JudgeCards[Jd];
	for (int32 I = 0; I < 2; ++I)
	{
		Result.Knockdowns[I] = Rt[I].Kd;
		Result.Form[I] = static_cast<float>(Form[I]);
	}
	Result.Rounds = PerRound;
	bHasResult = true;
	Phase = EFightPhase::Over;
	PushEvent(EFightEventKind::FightEnd, WinnerIndex, WinnerIndex < 0 ? -1 : 1 - WinnerIndex, 0);
}

// ======================================================================
// Ноги (footwork.ts)
// ======================================================================
void FBoxingFightCore::PlaceFighters()
{
	Rt[0].X = -DIST_START / 2;
	Rt[1].X = DIST_START / 2;
	Rt[0].Z = 0;
	Rt[1].Z = 0;
}

FBoxingFightCore::FVec2 FBoxingFightCore::Axis(int32 I) const
{
	const FRuntime& Me = Rt[I];
	const FRuntime& Op = Rt[1 - I];
	const double Dx = Op.X - Me.X;
	const double Dz = Op.Z - Me.Z;
	double D = Hypot(Dx, Dz);
	if (D == 0) D = 1;
	FVec2 U;
	U.X = Dx / D;
	U.Z = Dz / D;
	return U;
}

// Касательная «влево» (Side=1) / «вправо» (−1) от лица бойца: лево = (u.z, −u.x).
FBoxingFightCore::FVec2 FBoxingFightCore::Tangent(int32 I, int32 Side) const
{
	const FVec2 U = Axis(I);
	FVec2 Tg;
	Tg.X = U.Z * Side;
	Tg.Z = -U.X * Side;
	return Tg;
}

FBoxingFightCore::FVec2 FBoxingFightCore::StepTarget(int32 I, EStepKind Kind, double Amt) const
{
	const FRuntime& Me = Rt[I];
	const FRuntime& Op = Rt[1 - I];
	FVec2 Out;
	if (!IsLateral(Kind))
	{
		const FVec2 U = Axis(I);
		const double S = Kind == EStepKind::Fwd ? Amt : -Amt;
		Out.X = Me.X + U.X * S;
		Out.Z = Me.Z + U.Z * S;
		return Out;
	}
	// Дуга: сдвиг по касательной и возврат на прежний радиус вокруг соперника.
	const double D = Distance();
	const FVec2 Tg = Tangent(I, SideOf(Kind));
	const double Px = Me.X + Tg.X * Amt - Op.X;
	const double Pz = Me.Z + Tg.Z * Amt - Op.Z;
	double L = Hypot(Px, Pz);
	if (L == 0) L = 1;
	Out.X = Op.X + (Px / L) * D;
	Out.Z = Op.Z + (Pz / L) * D;
	return Out;
}

void FBoxingFightCore::MoveStep(int32 I, EStepKind Kind, double Amt)
{
	FRuntime& R = Rt[I];
	if (R.bHasAim)
	{
		// «Срез» ринга: прямо к точке отсечки (по диагонали), не по дуге.
		const double Dx = R.AimX - R.X;
		const double Dz = R.AimZ - R.Z;
		const double L = Hypot(Dx, Dz);
		if (L < 1e-3) return;
		const double S = FMath::Min(Amt, L);
		MoveTo(I, R.X + (Dx / L) * S, R.Z + (Dz / L) * S);
		return;
	}
	const FVec2 P = StepTarget(I, Kind, Amt);
	MoveTo(I, P.X, P.Z);
}

// Канаты (квадрат ±RING_HALF) и дистанция [DIST_MIN, DIST_MAX]; не уложился — стоит на месте.
void FBoxingFightCore::MoveTo(int32 I, double NX, double NZ)
{
	FRuntime& Me = Rt[I];
	const FRuntime& Op = Rt[1 - I];
	double X = ClampRing(NX);
	double Z = ClampRing(NZ);
	const double Dx = X - Op.X;
	const double Dz = Z - Op.Z;
	double D = Hypot(Dx, Dz);
	if (D == 0) D = 1e-6;
	const double Want = ClampD(D, DIST_MIN, DIST_MAX);
	if (Want != D)
	{
		X = ClampRing(Op.X + (Dx / D) * Want);
		Z = ClampRing(Op.Z + (Dz / D) * Want);
	}
	const double D2 = Hypot(X - Op.X, Z - Op.Z);
	if (D2 < DIST_MIN - 1e-6 || D2 > DIST_MAX + 1e-6) return;
	Me.X = X;
	Me.Z = Z;
}

bool FBoxingFightCore::CanRetreat(int32 I) const
{
	const FRuntime& Me = Rt[I];
	const FVec2 P = StepTarget(I, EStepKind::Back, STEP_LEN);
	const FVec2 U = Axis(I);
	const double Progress = -((ClampRing(P.X) - Me.X) * U.X + (ClampRing(P.Z) - Me.Z) * U.Z);
	return Progress > STEP_LEN * 0.35;
}

int32 FBoxingFightCore::RopeLevel(int32 I) const
{
	const FRuntime& R = Rt[I];
	return (FMath::Abs(R.X) > RING_HALF - ROPE_ZONE ? 1 : 0) + (FMath::Abs(R.Z) > RING_HALF - ROPE_ZONE ? 1 : 0);
}

double FBoxingFightCore::RopePen(int32 I) const
{
	const int32 L = RopeLevel(I);
	return L == 0 ? 0 : (L == 1 ? 0.6 : 1);
}

double FBoxingFightCore::AngleOf(int32 I) const
{
	const FRuntime& R = Rt[I];
	return T < R.AngleUntil ? R.AngleMag : 0;
}

bool FBoxingFightCore::StartStep(int32 I, EStepKind Kind, const FVec2* Aim)
{
	FRuntime& R = Rt[I];
	if (Kind == EStepKind::None) return false;
	if (T < R.StaggerUntil || T < R.StepReadyAt) return false;
	if (Kind == EStepKind::Back && !Aim && !CanRetreat(I)) return false; // спиной в канаты — «пойман»
	R.bHasAim = Aim != nullptr;
	if (Aim)
	{
		R.AimX = Aim->X;
		R.AimZ = Aim->Z;
	}
	const bool bStraight = !IsLateral(Kind);
	const bool bPivot = Kind == EStepKind::PivotL || Kind == EStepKind::PivotR;
	const double Len = Aim ? CUT_LEN : (bStraight ? STEP_LEN : (bPivot ? PIVOT_LEN : LAT_LEN));
	const double Dur = bStraight ? STEP_DUR : (bPivot ? PIVOT_DUR : LAT_DUR);
	R.Step = Kind;
	R.StepSpeed = Len / Dur;
	R.StepUntil = T + Dur;
	R.StepReadyAt = T + (bPivot ? PIVOT_COOLDOWN : FMath::Max(STEP_COOLDOWN, Dur + 0.08));
	R.Stamina = FMath::Max(0.0, R.Stamina - (bStraight ? STEP_STAM : (bPivot ? PIVOT_STAM : LAT_STAM)));
	if (!bStraight)
	{
		// Ушёл с линии — открыл угол; соперник, сам шагнувший вбок, гасит наш.
		R.AngleUntil = R.StepUntil + ANGLE_WINDOW;
		R.AngleMag = bPivot ? 1 : ANGLE_LAT;
		Rt[1 - I].AngleUntil = 0;
	}
	return true;
}

double FBoxingFightCore::Distance() const
{
	return Hypot(Rt[1].X - Rt[0].X, Rt[1].Z - Rt[0].Z);
}

double FBoxingFightCore::YawOf(int32 I) const
{
	const FVec2 U = Axis(I);
	return FMath::Atan2(-U.Z, U.X);
}

void FBoxingFightCore::UpdateStep(int32 I, double Dt)
{
	FRuntime& R = Rt[I];
	if (R.Step != EStepKind::None)
	{
		if (T >= R.StepUntil) R.Step = EStepKind::None;
		else MoveStep(I, R.Step, R.StepSpeed * Dt);
	}
}

// static
double FBoxingFightCore::RangeFactor(EPunchKind Kind, double Dist, double ReachCm)
{
	const FRangeZone& Rz = RANGE[KindIdx(Kind)];
	const double D = Dist / ClampD(ReachCm / REF_REACH, 0.85, 1.15);
	if (D > Rz.Max) return 0;
	if (D > Rz.BestHi) return 1 - (1 - RANGE_FAR_MIN) * ((D - Rz.BestHi) / (Rz.Max - Rz.BestHi));
	if (D >= Rz.BestLo) return 1;
	const double Span = FMath::Max(0.01, Rz.BestLo - DIST_MIN);
	return FMath::Max(Rz.CloseMin, 1 - (1 - Rz.CloseMin) * ((Rz.BestLo - D) / Span));
}

// ======================================================================
// ИИ (ai.ts)
// ======================================================================
void FBoxingFightCore::AiThink(int32 Me)
{
	FRuntime& Ai = Rt[Me];
	const int32 Foe = 1 - Me;
	if (T < Ai.StaggerUntil || Ai.bHasPunch) return;
	AiFootwork(Me);
	if (Ai.bBlocking) return;
	const int32 Style = StyleIdx(Ai.Prof.Style);
	// Контратакёр/панчер ловят промах соперника встречным (сдвиг удара, не лишний удар).
	const double LastMiss = LastMissAt[Foe];
	const bool bPunish = (Ai.Prof.Style == EBoxStyle::Counter || Ai.Prof.Style == EBoxStyle::Puncher) &&
		LastMiss >= 0 && T - LastMiss < 0.5 && T >= Ai.PunishReadyAt;
	if (!bPunish && T < Ai.NextAiAt) return;
	if (bPunish) Ai.PunishReadyAt = T + PUNISH_COOLDOWN;
	const double Base = AiInterval(Style);
	const double HealthF = 0.9 + 0.1 * (Ai.Health() / 100);
	const double HurtF = T < Ai.HurtUntil ? HURT_AGG : 1;
	const double Agg = Form[Me] * Ai.FatigueForm() * HealthF * HurtF;
	const FRuntime& Op = Rt[Foe];
	const bool bTurtling = Op.bBlocking && Op.BlockSince >= 0 && T - Op.BlockSince > AI_TURTLE_AFTER;
	EPunchKind Kind = AiPickType(Me);
	if (bTurtling && Kind == EPunchKind::Jab && Rng.Next() < 0.7) Kind = Distance() < 1.15 ? EPunchKind::Hook : EPunchKind::Cross;
	const bool bRound = Kind == EPunchKind::Hook || Kind == EPunchKind::Uppercut;
	EPunchArm Arm = ArmFor(Kind);
	if (bRound) Arm = Rng.Next() < 0.5 ? EPunchArm::Lead : EPunchArm::Rear;
	const bool bDiver = IsDiver(Foe);
	const double BodyP = AI_BODY[Style] + (bDiver ? AI_DIVER_BODY : 0);
	const EPunchTarget Target = Rng.Next() < BodyP ? EPunchTarget::Body : EPunchTarget::Head;
	StartPunch(Me, Kind, bRound, Arm, Target);
	const double Jitter = 0.75 + Rng.Range(0, 0.6);
	double Interval = (Base * Jitter) / FMath::Max(0.4, Agg);
	// Против ЧЕЛОВЕКА: давит выдохшегося, бережёт дыхание, ломает «черепаху».
	if (!bAi[Foe])
	{
		const double OpSt = Op.Stamina / Op.MaxStam;
		if (OpSt < GASSED_AT) Interval *= 1 - GASSED_PUSH * (1 - OpSt / GASSED_AT);
		const double MySt = Ai.Stamina / Ai.MaxStam;
		const bool bFinishing = T < Op.HurtUntil || Op.Health() < 30;
		if (MySt < AI_PACE_AT && !bFinishing) Interval *= 1 + AI_PACE_K * (1 - MySt / AI_PACE_AT);
		if (bTurtling) Interval = FMath::Min(Interval, AI_TURTLE_INTERVAL * (0.8 + Rng.Range(0, 0.4)));
	}
	const double From = bPunish ? FMath::Max(T, Ai.NextAiAt) : T;
	Ai.NextAiAt = From + Interval;
}

// Человек часто ныряет (2+ уклона за 2.5 с)?
bool FBoxingFightCore::IsDiver(int32 Who) const
{
	if (bAi[Who]) return false;
	int32 N = 0;
	for (int32 K = 0; K < Rt[Who].SlipTimes.Num(); ++K)
	{
		if (T - Rt[Who].SlipTimes[K] < 2.5) ++N;
	}
	return N >= 2;
}

// Микс стиля × эффективность на текущей дистанции (+ ловля ныряльщика).
EPunchKind FBoxingFightCore::AiPickType(int32 Me)
{
	const int32 Style = StyleIdx(Rt[Me].Prof.Style);
	const double Dist = Distance();
	const double Reach = Rt[Me].Prof.ReachCm;
	const bool bDiver = IsDiver(1 - Me);
	double W[4];
	double Sum = 0;
	for (int32 K = 0; K < 4; ++K)
	{
		const double DiverMul = !bDiver ? 1 : (K == 3 ? 3 : (K == 0 ? AI_DIVER_JAB : 1));
		W[K] = AI_MIX[Style][K] * RangeFactor(static_cast<EPunchKind>(K), Dist, Reach) * DiverMul;
		Sum += W[K];
	}
	const double Rv = Rng.Next() * Sum; // бросок — до проверки суммы (порядок ГСЧ как в TS)
	if (Sum <= 0) return EPunchKind::Jab;
	double Acc = 0;
	for (int32 K = 0; K < 4; ++K)
	{
		Acc += W[K];
		if (Rv < Acc) return static_cast<EPunchKind>(K);
	}
	return EPunchKind::Cross;
}

void FBoxingFightCore::AiFootwork(int32 Me)
{
	FRuntime& Ai = Rt[Me];
	const FRuntime& Op = Rt[1 - Me];
	if (T < Ai.StaggerUntil || T < Ai.NextStepAt || T < Ai.StepReadyAt) return;
	const double Fw = Ai.Prof.Stats.Footwork;
	const double Think = (0.62 - ClampD((Fw - 50) / 150, 0, 0.3)) * (0.8 + Rng.Range(0, 0.4));
	Ai.NextStepAt = T + Think;
	if (Ai.Stamina < STEP_STAM * 2) return;
	const EBoxStyle Style = Ai.Prof.Style;
	const bool bCutter = Style == EBoxStyle::Pressure || Style == EBoxStyle::Puncher || Style == EBoxStyle::Volume;
	const bool bMover = Style == EBoxStyle::Technical || Style == EBoxStyle::Counter || Style == EBoxStyle::Speed;
	const bool bHurt = T < Ai.HurtUntil || Ai.Health() < 30;
	double Pref = AI_PREF_DIST[StyleIdx(Style)];
	if (bHurt) Pref = FMath::Max(Pref, 1.5);
	if (Ai.Fatigue() < 0.55) Pref += 0.1;
	const double D = Distance();
	const int32 Rope = RopeLevel(Me);

	// Близость к канатам: чем ближе к центру (и меньше «съел» канат), тем лучше.
	auto RopeScore = [](const FVec2& P) {
		return FMath::Max(FMath::Abs(ClampRing(P.X)), FMath::Abs(ClampRing(P.Z))) +
			Hypot(P.X - ClampRing(P.X), P.Z - ClampRing(P.Z)) * 3;
	};
	auto BestSideRope = [this, Me, &RopeScore](bool bPivot) {
		const EStepKind L = bPivot ? EStepKind::PivotL : EStepKind::Left;
		const EStepKind R = bPivot ? EStepKind::PivotR : EStepKind::Right;
		const double Len = bPivot ? PIVOT_LEN : LAT_LEN;
		return RopeScore(StepTarget(Me, L, Len)) <= RopeScore(StepTarget(Me, R, Len)) ? L : R;
	};
	// Прямой шаг с памятью направления: разворот вперёд↔назад — не раньше REVERSE_GAP.
	auto Straight = [this, Me](EStepKind Kind, bool bForce) {
		FRuntime& Self = Rt[Me];
		const bool bRev = Self.LastStraight != EStepKind::None && Self.LastStraight != Kind && T - Self.LastStraightAt < REVERSE_GAP;
		if (bRev && !bForce) return false;
		if (!StartStep(Me, Kind)) return false;
		Self.LastStraight = Kind;
		Self.LastStraightAt = T;
		return true;
	};

	// 1) Уйти с канатов / из угла (давящий — только встряхнутым), в одну выбранную сторону.
	if (Rope == 0) Ai.RopeSide = 0;
	if (Rope > 0 && (!bCutter || bHurt))
	{
		const bool bPivot = Rope == 2 && Ai.Stamina > PIVOT_STAM * 3 && Rng.Next() < 0.6;
		const EStepKind Best = BestSideRope(bPivot);
		if (Ai.RopeSide == 0) Ai.RopeSide = (Best == EStepKind::Left || Best == EStepKind::PivotL) ? 1 : -1;
		const EStepKind Kind = Ai.RopeSide > 0 ? (bPivot ? EStepKind::PivotL : EStepKind::Left)
			: (bPivot ? EStepKind::PivotR : EStepKind::Right);
		const double Len = bPivot ? PIVOT_LEN : LAT_LEN;
		auto Clipped = [this, Me, Len](EStepKind K) {
			const FVec2 P = StepTarget(Me, K, Len);
			return Hypot(P.X - ClampRing(P.X), P.Z - ClampRing(P.Z)) > 0.02;
		};
		const EStepKind Other = Kind == EStepKind::Left ? EStepKind::Right
			: (Kind == EStepKind::Right ? EStepKind::Left : (Kind == EStepKind::PivotL ? EStepKind::PivotR : EStepKind::PivotL));
		const EStepKind Go = (Clipped(Kind) && !Clipped(Other)) ? Other : Kind;
		if (Go != Kind) Ai.RopeSide = Ai.RopeSide > 0 ? -1 : 1;
		StartStep(Me, Go);
		return;
	}
	// 2) Давящий режет ринг: по диагонали в точку между соперником и центром.
	const double OpR = Hypot(Op.X, Op.Z);
	if (bCutter && !bHurt && OpR > 0.45)
	{
		FVec2 Aim;
		Aim.X = ClampRing(Op.X - (Op.X / OpR) * Pref);
		Aim.Z = ClampRing(Op.Z - (Op.Z / OpR) * Pref);
		const double Off = Hypot(Aim.X - Ai.X, Aim.Z - Ai.Z);
		if (Off > 0.12)
		{
			const FVec2 U = Axis(Me);
			const double Mx = (Aim.X - Ai.X) / Off;
			const double Mz = (Aim.Z - Ai.Z) / Off;
			const double Fwd = Mx * U.X + Mz * U.Z;
			const double Side = Mx * U.Z - Mz * U.X; // >0 — влево
			const EStepKind Kind = FMath::Abs(Fwd) >= FMath::Abs(Side) ? (Fwd > 0 ? EStepKind::Fwd : EStepKind::Back)
				: (Side > 0 ? EStepKind::Left : EStepKind::Right);
			StartStep(Me, Kind, &Aim);
			return;
		}
	}
	// 3) Дистанция стиля — серией с гистерезисом.
	const double Err = D - Pref;
	if (Ai.DistDir != 0 && Ai.DistDir * Err <= DIST_DONE_TH)
	{
		Ai.DistDir = 0;
		Ai.NextStepAt = T + Think * SETTLE_PAUSE;
		return;
	}
	if (Ai.DistDir == 0 && FMath::Abs(Err) >= DIST_START_TH) Ai.DistDir = Err > 0 ? 1 : -1;
	if (Ai.DistDir != 0)
	{
		if (Ai.DistDir > 0) Straight(EStepKind::Fwd, bHurt);
		else if (CanRetreat(Me)) Straight(EStepKind::Back, bHurt);
		else StartStep(Me, BestSideRope(false));
		return;
	}
	// 4) На своей дистанции — кружить.
	const double Circle = bMover ? 0.55 : (Style == EBoxStyle::Balanced ? 0.2 : 0);
	if (Rng.Next() < Circle)
	{
		EStepKind Kind;
		if (bMover)
		{
			// Влево — прочь от правой руки соперника; если слева канаты — вправо.
			const double Left = RopeScore(StepTarget(Me, EStepKind::Left, LAT_LEN));
			const double Right = RopeScore(StepTarget(Me, EStepKind::Right, LAT_LEN));
			Kind = (Left > RING_HALF - ROPE_ZONE && Right < Left) ? EStepKind::Right : EStepKind::Left;
		}
		else
		{
			Kind = Rng.Next() < 0.5 ? EStepKind::Left : EStepKind::Right;
		}
		StartStep(Me, Kind);
	}
}

// ИИ защищается от удара человека в момент старта удара: блок или уклон (без читов).
void FBoxingFightCore::AiReactToPunch(int32 AiIdx, int32 HumanIdx, int32 Reps)
{
	FRuntime& Ai = Rt[AiIdx];
	const FRuntime& Hu = Rt[HumanIdx];
	if (T < Ai.StaggerUntil || Ai.bHasPunch || !Hu.bHasPunch) return;
	if (T < Ai.WhiffOpenUntil) return;
	const double Read = FMath::Min(READ_MAX, Reps * READ_STEP);
	const double Base = FMath::Min(0.6,
		(static_cast<double>(Ai.Prof.Stats.Defense) * 0.6 + Ai.Prof.Stats.Footwork * 0.4) / 220 + AI_GUARD_BONUS[StyleIdx(Ai.Prof.Style)]);
	const double Guard = FMath::Max(0.08, FMath::Min(AI_GUARD_CAP, Base + Read));
	if (Rng.Next() >= Guard * (1 - ANGLE_GUARD_PEN * AngleOf(HumanIdx))) return;
	const double SlipP = FMath::Min(0.75, (Ai.Prof.Stats.Footwork > 70 ? 0.4 : 0) + Read);
	const bool bCanBlock = T >= Ai.BlockReadyAt;
	if (Rng.Next() < SlipP || !bCanBlock)
	{
		Ai.SlipUntil = T + SLIP_WINDOW;
		Ai.SlipStart = T;
	}
	else
	{
		Ai.bBlocking = true;
		Ai.BlockUntil = Hu.Punch.Contact + AI_BLOCK_HOLD;
	}
	// Прочитал удар — отвечает встречным сразу после контакта (с бонусом контры).
	if (Read >= READ_STEP * 2)
	{
		const double At = Hu.Punch.Contact + AI_BLOCK_HOLD + 0.02;
		Ai.NextAiAt = FMath::Min(Ai.NextAiAt, At);
		Ai.CounterUntil = FMath::Max(Ai.CounterUntil, At + COUNTER_WINDOW);
	}
}

// ======================================================================
// Чтение (hud.ts) — без ГСЧ и без мутаций механики
// ======================================================================
FFightEvent& FBoxingFightCore::PushEvent(EFightEventKind Kind, int32 Attacker, int32 Defender, double Mag)
{
	FFightEvent E;
	E.Kind = Kind;
	E.Attacker = Attacker;
	E.Defender = Defender;
	E.Magnitude = static_cast<float>(Mag);
	E.Round = Round;
	E.Time = static_cast<float>(T);
	const int32 Idx = Events.Add(E);
	return Events[Idx];
}

TArray<FFightEvent> FBoxingFightCore::PollEvents()
{
	TArray<FFightEvent> Out = Events;
	Events.Reset();
	return Out;
}

FFightSnapshot FBoxingFightCore::GetSnapshot() const
{
	FFightSnapshot S;
	S.Phase = Phase;
	S.Round = Round;
	S.TotalRounds = TotalRounds;
	S.TimeLeft = static_cast<float>(FMath::Max(0.0, TimeLeft));
	S.BreakLeft = static_cast<float>(Phase == EFightPhase::Between ? FMath::Max(0.0, BreakLeft) : 0.0);
	S.Distance = static_cast<float>(Distance());
	for (int32 Jd = 0; Jd < 3; ++Jd) S.JudgeTotals[Jd] = JudgeCards[Jd];
	S.bHasResult = bHasResult;
	if (bHasDown)
	{
		S.DownWho = Down.Who;
		S.DownCount = FMath::Min(RISE_LIMIT_COUNT, static_cast<int32>(Down.CountT / COUNT_STEP) + 1);
		S.RiseProgress = static_cast<float>(Down.RiseProgress);
	}
	for (int32 I = 0; I < 2; ++I)
	{
		const FRuntime& R = Rt[I];
		FFighterState& F = S.Fighters[I];
		F.X = static_cast<float>(R.X);
		F.Z = static_cast<float>(R.Z);
		const double Yaw = YawOf(I);
		F.Yaw = static_cast<float>(Yaw);
		F.YawDegUE = static_cast<float>(-Yaw * 180.0 / PiD);
		F.Health = static_cast<float>(R.Health());
		F.StaminaPct = static_cast<float>(R.Stamina / R.MaxStam * 100.0);
		F.Knockdowns = R.Kd;
		F.RopeLevel = RopeLevel(I);
		F.bAngle = AngleOf(I) > 0;
		F.bGassed = T < R.GassedUntil;
		F.Step = R.Step;

		const bool bDownNow = bHasDown && Down.Who == I;
		const bool bLostEarly = bHasResult && Result.WinnerIndex >= 0 && Result.WinnerIndex != I &&
			(Result.Method == EFightMethod::KO || Result.Method == EFightMethod::RSC);
		F.bDown = bDownNow || bLostEarly;
		F.bKO = bLostEarly;
		if (bHasResult)
		{
			if (Result.WinnerIndex == I) F.Victory = 1.f;
			else if (Result.WinnerIndex < 0) F.Victory = 0.5f;
			else F.bDefeated = true;
		}
		if (F.bDown) continue; // лежит — остальная поза не нужна (как poseOf)

		if (R.bHasPunch)
		{
			const double Dur = R.Punch.End - R.Punch.Start;
			const double U = ClampD((T - R.Punch.Start) / Dur, 0, 1);
			F.bPunching = true;
			F.Punch = ToPublicPunch(R.Punch.Kind, R.Punch.Arm);
			F.PunchTarget = R.Punch.Target;
			F.PunchPhase = static_cast<float>(U);
			// Кусочно-линейно: контакт (CONTACT_FRAC цикла) ↔ пик клипа (POSE_CONTACT).
			const double Anim = U < CONTACT_FRAC ? (U / CONTACT_FRAC) * POSE_CONTACT
				: POSE_CONTACT + ((U - CONTACT_FRAC) / (1 - CONTACT_FRAC)) * (1 - POSE_CONTACT);
			F.PunchPhaseAnim = static_cast<float>(Anim);
			F.PunchDuration = static_cast<float>(Dur);
			F.PunchTimeToContact = static_cast<float>(R.Punch.Contact - T);
		}
		F.bStaggered = T < R.StaggerUntil;
		F.bBlocking = R.bBlocking;
		F.GuardIntegrity = R.bBlocking ? static_cast<float>(GuardIntegrity(T - FMath::Max(0.0, R.BlockSince), R.GuardPressure)) : 1.f;
		if (T < R.SlipUntil)
		{
			const double U = 1 - (R.SlipUntil - T) / SLIP_WINDOW;
			F.Slip = static_cast<float>(FMath::Sin(ClampD(U, 0, 1) * PiD) * R.SlipDir);
			F.SlipPhase = static_cast<float>(ClampD(U, 0, 1));
		}
		if (T < R.HurtUntil)
		{
			const double Total = R.HurtUntil - FMath::Max(T - 0.6, 0.0);
			const double U = 1 - (R.HurtUntil - T) / FMath::Max(0.3, Total);
			F.Hurt = static_cast<float>(FMath::Sin(ClampD(U, 0, 1) * PiD) * R.HurtMag);
			F.HurtTarget = R.HurtTarget;
		}
	}
	return S;
}
