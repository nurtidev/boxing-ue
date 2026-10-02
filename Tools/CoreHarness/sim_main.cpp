// Харнесс ядра боя вне UE: детерминизм, 200 сидов, паритет с TS, постановка раунда (S-53).
#include "BoxingFightCore.h"
extern "C" int printf(const char*, ...);

namespace
{
	double AbsD(double V) { return V < 0 ? -V : V; }
	double HypotD(double X, double Z) { return FMath::Sqrt(X * X + Z * Z); }

	// Пары: AB — эталон паритета (технарь vs прессинг), PG — панчер vs «стеклянный» объёмник (нокдауны → нейтральный угол).
	FFightConfig MakeConfig(uint32 Seed, float RoundSec, float BreakSec, bool bCorners, bool bKdPair)
	{
		FFightConfig C;
		C.Seed = Seed;
		C.Rounds = 3;
		C.RoundSeconds = RoundSec;
		C.BreakSeconds = BreakSec;
		C.bCorners = bCorners;
		FFighterSetup& A = C.Fighters[0];
		FFighterSetup& B = C.Fighters[1];
		if (!bKdPair)
		{
			A.Stats = {78, 80, 76, 74, 75, 82, 79};
			A.ReachCm = 185; A.WeightKg = 71; A.Style = EBoxStyle::Technical;
			B.Stats = {84, 74, 70, 78, 77, 74, 72};
			B.ReachCm = 180; B.WeightKg = 71; B.Style = EBoxStyle::Pressure;
		}
		else
		{
			A.Stats = {92, 76, 72, 76, 74, 74, 70};
			A.ReachCm = 183; A.WeightKg = 75; A.Style = EBoxStyle::Puncher;
			B.Stats = {72, 74, 72, 70, 58, 72, 68};
			B.ReachCm = 180; B.WeightKg = 75; B.Style = EBoxStyle::Volume;
		}
		A.bAiControlled = true;
		B.bAiControlled = true;
		return C;
	}

	uint32 Mix(uint32 H, uint32 V) { return (H ^ V) * 16777619u; }
	uint32 Bits(float F)
	{
		union { float F; uint32 U; } X;
		X.F = F;
		return X.U;
	}

	struct FRunSummary
	{
		uint32 Hash = 2166136261u;      // прежний хеш харнесса (позиция/здоровье каждый тик)
		uint32 FightHash = 2166136261u; // только бой: все поля событий + снимки тиков, где шло боевое время
		int32 Winner = -1, Method = 0, Stopped = 0, Ticks = 0;
		int32 Hits[2] = {0, 0}, Kds[2] = {0, 0};
		int32 FirstFightTick = -1;
		// постановка
		int32 StageSeen[5] = {0, 0, 0, 0, 0}; // по ERingStageKind: сколько раз стадия начиналась
		int32 Violations = 0;
		double MaxStageStep = 0;   // макс. смещение бойца за тик на постановке, м
		double MaxFightStep = 0;   // … в бою
		double MinBodyClear = 1e9; // мин. запас «идущий в нейтральный угол — тело лежащего» сверх допуска
		int32 MaxStageTicks = 0;   // самая долгая ходьба Out/Rest/Resume (тиков)
	};

	const char* PhaseName(EFightPhase P)
	{
		switch (P)
		{
		case EFightPhase::Fighting: return "fighting";
		case EFightPhase::Down: return "down";
		case EFightPhase::Between: return "between";
		case EFightPhase::Over: return "over";
		default: return "walkout";
		}
	}
	const char* StageName(ERingStageKind K)
	{
		static const char* N[] = {"none", "out", "rest", "neutral", "resume"};
		return N[static_cast<int32>(K)];
	}

	int32 G_Fail = 0;
	void Bad(FRunSummary& S, uint32 Seed, const char* What, double A = 0, double B = 0)
	{
		if (S.Violations++ < 3) printf("  ! seed %u: %s (%.6f, %.6f)\n", Seed, What, A, B);
	}

	FRunSummary RunFight(uint32 Seed, bool bVerbose, float RoundSec, float BreakSec, bool bCorners, bool bKdPair = false, bool bTrace = false)
	{
		FBoxingFightCore Core;
		Core.Init(MakeConfig(Seed, RoundSec, BreakSec, bCorners, bKdPair));
		FRunSummary S;
		const float Dt = 1.f / 60.f;
		const double WalkStep = BoxingStaging::WALK_SPEED * static_cast<double>(Dt) + 1e-6;
		FFightSnapshot Prev = Core.GetSnapshot();
		double PrevT = Core.GetFightTime();
		ERingStageKind PrevKind = ERingStageKind::None;
		int32 StageStartTick = 0;
		double NeutralStartClear = 0;
		int32 PrevKey = -1;
		// Старт с углами: оба в своих углах, стадия Out.
		if (bCorners)
		{
			const BoxingStaging::FRingPoint C0 = BoxingStaging::CornerOf(0), C1 = BoxingStaging::CornerOf(1);
			if (Prev.Phase != EFightPhase::Walkout || Prev.Stage.Kind != ERingStageKind::Out || AbsD(Prev.Fighters[0].X - C0.X) > 1e-5 ||
				AbsD(Prev.Fighters[1].Z - C1.Z) > 1e-5)
			{
				Bad(S, Seed, "старт не в углах");
			}
			PrevKind = Prev.Stage.Kind;
			++S.StageSeen[static_cast<int32>(PrevKind)];
		}
		while (!Core.IsOver() && S.Ticks < 60 * 60 * 30)
		{
			Core.Tick(Dt);
			++S.Ticks;
			const double NowT = Core.GetFightTime();
			const bool bFightTime = NowT != PrevT;
			TArray<FFightEvent> Ev = Core.PollEvents();
			for (int32 K = 0; K < Ev.Num(); ++K)
			{
				const FFightEvent& E = Ev[K];
				S.Hash = (S.Hash ^ (uint32(E.Kind) * 131u + uint32(E.Attacker + 1) * 7u + uint32(E.Magnitude * 1000.f))) * 16777619u;
				uint32 H = S.FightHash;
				H = Mix(H, uint32(E.Kind)); H = Mix(H, uint32(E.Attacker + 1)); H = Mix(H, uint32(E.Defender + 1));
				H = Mix(H, Bits(E.Magnitude)); H = Mix(H, uint32(E.Punch)); H = Mix(H, uint32(E.Target));
				H = Mix(H, uint32(E.bCounter) | uint32(E.bPerfect) << 1 | uint32(E.bGuardBreak) << 2 | uint32(E.bCaught) << 3);
				H = Mix(H, uint32(E.Round)); H = Mix(H, Bits(E.Time));
				S.FightHash = H;
				if (E.Kind == EFightEventKind::Hit) ++S.Hits[E.Attacker];
				if (E.Kind == EFightEventKind::Knockdown) ++S.Kds[E.Defender];
				if (bVerbose && (E.Kind == EFightEventKind::Knockdown || E.Kind == EFightEventKind::RoundEnd))
					printf("  t=%.2f r%d %s\n", E.Time, E.Round, E.Kind == EFightEventKind::Knockdown ? "KNOCKDOWN" : "round end");
			}
			const FFightSnapshot Sn = Core.GetSnapshot();
			S.Hash = (S.Hash ^ uint32(Sn.Fighters[0].X * 1e4f + 50000.f) ^ (uint32(Sn.Fighters[1].Health * 100.f) << 8)) * 16777619u;
			if (bFightTime && Sn.Phase == EFightPhase::Fighting)
			{
				uint32 H = S.FightHash;
				for (int32 I = 0; I < 2; ++I)
				{
					const FFighterState& F = Sn.Fighters[I];
					H = Mix(H, Bits(F.X)); H = Mix(H, Bits(F.Z)); H = Mix(H, Bits(F.Health)); H = Mix(H, Bits(F.StaminaPct));
					H = Mix(H, uint32(F.bPunching) | uint32(F.bBlocking) << 1 | uint32(F.Step) << 2);
				}
				S.FightHash = Mix(H, Bits(Sn.TimeLeft));
			}
			if (S.FirstFightTick < 0 && Sn.Phase == EFightPhase::Fighting) S.FirstFightTick = S.Ticks;

			// --- инварианты постановки ---
			const bool bStageTick = Prev.Stage.Kind != ERingStageKind::None || Sn.Stage.Kind != ERingStageKind::None;
			for (int32 I = 0; I < 2; ++I)
			{
				const double Step = HypotD(Sn.Fighters[I].X - Prev.Fighters[I].X, Sn.Fighters[I].Z - Prev.Fighters[I].Z);
				if (bStageTick && !bFightTime) S.MaxStageStep = FMath::Max(S.MaxStageStep, Step);
				else if (!bStageTick) S.MaxFightStep = FMath::Max(S.MaxFightStep, Step);
				if (bCorners && bStageTick && !bFightTime && Step > WalkStep) Bad(S, Seed, "телепорт на постановке", Step, WalkStep);
				if (Step > 0.2 && (bCorners || Sn.Round == Prev.Round)) Bad(S, Seed, "телепорт", Step);
				if (bCorners && Sn.Stage.Kind == ERingStageKind::Neutral && Sn.Stage.bHasTarget[I] && !Sn.Stage.bArrived[I])
				{
					// Тело лежащего — не ближе допуска (или стартового зазора, если начал ближе), с точностью до шага.
					const double Body = HypotD(Sn.Fighters[I].X - Sn.LyingX, Sn.Fighters[I].Z - Sn.LyingZ);
					const double Need = FMath::Min(BoxingStaging::PASS_CLEAR, NeutralStartClear) - WalkStep;
					S.MinBodyClear = FMath::Min(S.MinBodyClear, Body - Need);
					if (Body < Need) Bad(S, Seed, "прошёл сквозь лежащего", Body, Need);
				}
			}
			// Часы раунда честно: на выходе/перерыве/возврате не идут ни раунд, ни боевое время; на счёте раунд стоит.
			if (Prev.Phase == EFightPhase::Walkout || Prev.Phase == EFightPhase::Between)
			{
				if (bFightTime) Bad(S, Seed, "боевое время шло на постановке");
				if (Sn.Round == Prev.Round && Sn.TimeLeft != Prev.TimeLeft) Bad(S, Seed, "часы раунда шли на постановке");
			}
			if (Prev.Phase == EFightPhase::Down && Sn.TimeLeft != Prev.TimeLeft) Bad(S, Seed, "часы раунда шли на счёте");
			if (Sn.Phase == EFightPhase::Walkout)
				for (int32 I = 0; I < 2; ++I)
					// Удар бившего в момент нокдауна остаётся в снимке «досмотренным» (фаза 1) — живой удар на выходе запрещён.
					if (Sn.Fighters[I].Step != EStepKind::None || (Sn.Fighters[I].bPunching && Sn.Fighters[I].PunchPhase < 1.f))
						Bad(S, Seed, "шаг/удар на постановке");
			// Перерыв: дошедший стоит ровно в своём углу.
			if (Sn.Stage.Kind == ERingStageKind::Rest)
				for (int32 I = 0; I < 2; ++I)
				{
					const BoxingStaging::FRingPoint C = BoxingStaging::CornerOf(I);
					if (Sn.Stage.bArrived[I] && (AbsD(Sn.Fighters[I].X - C.X) > 1e-5 || AbsD(Sn.Fighters[I].Z - C.Z) > 1e-5)) Bad(S, Seed, "дошёл не в свой угол");
				}
			// Смена стадии: проверка конца предыдущей и начала новой.
			if (Sn.Stage.Kind != PrevKind || (Sn.Stage.Kind != ERingStageKind::None && Sn.Stage.T < Prev.Stage.T))
			{
				const int32 Len = S.Ticks - StageStartTick;
				if (PrevKind == ERingStageKind::Out || PrevKind == ERingStageKind::Resume || PrevKind == ERingStageKind::Rest)
					S.MaxStageTicks = FMath::Max(S.MaxStageTicks, Len);
				if (PrevKind == ERingStageKind::Out && Sn.Phase == EFightPhase::Fighting)
					for (int32 I = 0; I < 2; ++I)
					{
						const BoxingStaging::FRingPoint M = BoxingStaging::MeetPoint(I);
						if (AbsD(Sn.Fighters[I].X - M.X) > 1e-6 || AbsD(Sn.Fighters[I].Z - M.Z) > 1e-6) Bad(S, Seed, "не на точке встречи");
					}
				// Новый выход после перерыва (авто-переход ждёт, пока оба дойдут) — из своих углов.
				if (PrevKind == ERingStageKind::Rest && Sn.Stage.Kind == ERingStageKind::Out)
					for (int32 I = 0; I < 2; ++I)
					{
						const BoxingStaging::FRingPoint C = BoxingStaging::CornerOf(I);
						const double Moved = HypotD(Sn.Fighters[I].X - C.X, Sn.Fighters[I].Z - C.Z);
						if (Moved > 1e-5) Bad(S, Seed, "выход не из своего угла", Moved);
					}
				if (Sn.Stage.Kind != ERingStageKind::None) ++S.StageSeen[static_cast<int32>(Sn.Stage.Kind)];
				if (Sn.Stage.Kind == ERingStageKind::Neutral)
				{
					const int32 W = Sn.Stage.bHasTarget[0] ? 0 : 1;
					NeutralStartClear = HypotD(Prev.Fighters[W].X - Sn.LyingX, Prev.Fighters[W].Z - Sn.LyingZ);
					const double Tx = Sn.Stage.TargetX[W], Tz = Sn.Stage.TargetZ[W];
					const bool bNeutralCorner = AbsD(AbsD(Tx) - BoxingStaging::CORNER_SPOT) < 1e-5 && AbsD(AbsD(Tz) - BoxingStaging::CORNER_SPOT) < 1e-5 && Tx * Tz < 0;
					if (!bNeutralCorner) Bad(S, Seed, "цель — не нейтральный угол", Tx, Tz);
					if (!Sn.Stage.bArrived[1 - W] || Sn.Stage.bHasTarget[1 - W]) Bad(S, Seed, "лежащий не на месте");
				}
				PrevKind = Sn.Stage.Kind;
				StageStartTick = S.Ticks;
			}
			if (bTrace)
			{
				// Формат — как у web-эталона (.s53tmp/ref.ts): тик фаза стадия дошли x0 z0 x1 z1 t.
				const int32 A0 = Sn.Stage.Kind == ERingStageKind::None ? 1 : int32(Sn.Stage.bArrived[0]);
				const int32 A1 = Sn.Stage.Kind == ERingStageKind::None ? 1 : int32(Sn.Stage.bArrived[1]);
				const int32 Key = int32(Sn.Phase) * 100 + int32(Sn.Stage.Kind) * 10 + A0 * 2 + A1;
				if (Key != PrevKey)
				{
					printf("  %d %s %s %d%d %.6f %.6f %.6f %.6f t=%.6f\n", S.Ticks, PhaseName(Sn.Phase), StageName(Sn.Stage.Kind), A0, A1,
						Sn.Fighters[0].X, Sn.Fighters[0].Z, Sn.Fighters[1].X, Sn.Fighters[1].Z, NowT);
					PrevKey = Key;
				}
			}
			Prev = Sn;
			PrevT = NowT;
		}
		const FFightResult& R = Core.GetResult();
		S.Winner = R.WinnerIndex; S.Method = int32(R.Method); S.Stopped = R.StoppedRound;
		if (bVerbose)
		{
			static const char* M[] = {"None", "DEC", "KO", "RSC", "DRAW"};
			printf("seed %u%s: winner=%d method=%s stopped=%d ticks=%d (%.1f s game time)\n", Seed, bCorners ? " [углы]" : "", R.WinnerIndex, M[S.Method],
				R.StoppedRound, S.Ticks, S.Ticks / 60.0);
			for (int32 J = 0; J < 3; ++J) printf("  judge %d: %d-%d\n", J + 1, R.JudgeTotals[J].Red, R.JudgeTotals[J].Blue);
			for (int32 K = 0; K < R.Rounds.Num(); ++K)
				printf("  r%d landed %.1f-%.1f kd %d-%d dmg %d-%d stam %d-%d\n", R.Rounds[K].Round, R.Rounds[K].Landed[0], R.Rounds[K].Landed[1],
					R.Rounds[K].Knockdowns[0], R.Rounds[K].Knockdowns[1], R.Rounds[K].Damage[0], R.Rounds[K].Damage[1], R.Rounds[K].Stamina[0], R.Rounds[K].Stamina[1]);
			printf("  hits %d-%d, hash %08x, fight-hash %08x\n", S.Hits[0], S.Hits[1], S.Hash, S.FightHash);
		}
		if (S.Violations) ++G_Fail;
		return S;
	}

	bool Near(double A, double B) { return AbsD(A - B) < 1e-6; }

	// Геометрия углов против web corners.ts (значения сняты vite-node: neutralFor/lyingBody/farNeutral/neutralPathClear).
	int32 CheckGeometry()
	{
		struct FCase { double Dx, Dz, Sx, Sz, Nx, Nz, Bx, Bz, Fx, Fz, Clear; };
		const FCase Cases[] = {
			{1.593596, 0, 0.4785, 0, -2.63, 2.63, 2.193596000, 0.000000000, -2.63, 2.63, 0.315096000},
			{0, 0, -1, 0, -2.63, 2.63, 0.600000000, 0.000000000, 2.63, -2.63, 0.200000000}, // дальний перекрыт телом
			{2.2, -2.2, 1.4, -1.4, -2.63, 2.63, 2.624264069, -2.624264069, -2.63, 2.63, 0.331370850},
			{-2, 2, -1, 1, 2.63, -2.63, -2.424264069, 2.424264069, 2.63, -2.63, 0.614213562},
			{0.5, 0.5, -0.5, -0.5, 2.63, -2.63, 0.924264069, 0.924264069, 2.63, -2.63, 0.589328913},
			{-2.6, -2.6, -1.6, -1.6, 2.63, -2.63, -3.024264069, -3.024264069, 2.63, -2.63, 0.614213562},
			{1, -1, 0, 0, -2.63, 2.63, 1.424264069, -1.424264069, -2.63, 2.63, 0.614213562},
			{-0.3, 2.5, 0.6, 2, 2.63, -2.63, -0.824494366, 2.791385759, 2.63, -2.63, 0.229563014},
		};
		int32 Fails = 0;
		for (const FCase& C : Cases)
		{
			const BoxingStaging::FRingPoint D = {C.Dx, C.Dz}, St = {C.Sx, C.Sz};
			const BoxingStaging::FRingPoint N = BoxingStaging::NeutralFor(D, St);
			const BoxingStaging::FRingPoint B = BoxingStaging::LyingBody(D, St);
			const BoxingStaging::FRingPoint F = BoxingStaging::FarNeutral(D);
			const double Cl = BoxingStaging::NeutralPathClear(D, St, N);
			if (!Near(N.X, C.Nx) || !Near(N.Z, C.Nz) || !Near(B.X, C.Bx) || !Near(B.Z, C.Bz) || !Near(F.X, C.Fx) || !Near(F.Z, C.Fz) || !Near(Cl, C.Clear))
			{
				++Fails;
				printf("  ! геометрия: down(%.3f,%.3f) stand(%.3f,%.3f) -> (%.3f,%.3f), ожидалось (%.3f,%.3f)\n", C.Dx, C.Dz, C.Sx, C.Sz, N.X, N.Z, C.Nx, C.Nz);
			}
		}
		const BoxingStaging::FRingPoint R = BoxingStaging::CornerOf(0), Bl = BoxingStaging::CornerOf(1);
		if (!Near(R.X, -2.63) || !Near(R.Z, -2.63) || !Near(Bl.X, 2.63) || !Near(Bl.Z, 2.63)) ++Fails;
		printf("геометрия углов vs web corners.ts (%d случаев): %s\n", int32(sizeof(Cases) / sizeof(Cases[0])), Fails ? "MISMATCH" : "OK");
		return Fails;
	}
}

// sim.exe findkd <раунд, с> — сиды пары AB (пресеты GameMode) с нокдауном: для скриншотов нейтрального угла в UE.
static int FindKd(float RoundSec)
{
	int32 Found = 0;
	for (uint32 Seed = 1; Seed <= 3000 && Found < 12; ++Seed)
	{
		FBoxingFightCore Core;
		Core.Init(MakeConfig(Seed, RoundSec, 10.f, true, false));
		int32 Ticks = 0;
		while (!Core.IsOver() && Ticks < 60 * 60 * 30)
		{
			Core.Tick(1.f / 60.f);
			++Ticks;
			bool bKd = false;
			TArray<FFightEvent> Ev = Core.PollEvents();
			for (int32 K = 0; K < Ev.Num(); ++K)
				if (Ev[K].Kind == EFightEventKind::Knockdown)
				{
					printf("seed %u: нокдаун r%d, упал %d, реальное время %.1f с\n", Seed, Ev[K].Round, Ev[K].Defender, Ticks / 60.0);
					bKd = true;
				}
			if (bKd)
			{
				++Found;
				break;
			}
		}
	}
	return 0;
}

int main(int Argc, char** Argv)
{
	if (Argc >= 3 && Argv[1][0] == 'f')
	{
		int32 Sec = 0; // целые секунды (без atof: заглушка CoreMinimal без std-заголовков)
		for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) Sec = Sec * 10 + (*C - '0');
		return FindKd(static_cast<float>(Sec));
	}
	// mulberry32: эталон из web/src/engine/rng.ts (node) для seed 12345.
	FBoxingRng Rng;
	Rng.Seed(12345);
	const double R1 = Rng.Next(), R2 = Rng.Next(), R3 = Rng.Next();
	printf("mulberry32(12345): %.17g %.17g %.17g\n", R1, R2, R3);
	printf("mulberry32 expected: 0.9797282677609473 0.3067522644996643 0.484205421525985\n");
	int32 Fails = 0;

	// --- 1. Детерминизм и 200 сидов (раунд 180 с, перерыв 60 с, углы — по умолчанию) ---
	const FRunSummary A = RunFight(42, true, 180.f, 60.f, true);
	const FRunSummary B = RunFight(42, false, 180.f, 60.f, true);
	const bool bDet = A.Hash == B.Hash && A.FightHash == B.FightHash && A.Ticks == B.Ticks;
	printf("determinism seed 42: %s\n", bDet ? "OK" : "MISMATCH");
	Fails += bDet ? 0 : 1;

	for (int32 Mode = 1; Mode >= 0; --Mode)
	{
		int32 Wins[2] = {0, 0}, Draws = 0, Early = 0, Unfinished = 0, Kds = 0;
		for (uint32 Seed = 1; Seed <= 200; ++Seed)
		{
			const FRunSummary S = RunFight(Seed, false, 180.f, 60.f, Mode == 1);
			if (S.Method == 0) ++Unfinished;
			else if (S.Winner < 0) ++Draws;
			else ++Wins[S.Winner];
			if (S.Method == 2 || S.Method == 3) ++Early;
			Kds += S.Kds[0] + S.Kds[1];
		}
		printf("200 seeds [%s, 180 с]: red %d, blue %d, draws %d, stoppages %d, knockdowns %d, unfinished %d\n", Mode ? "углы" : "без углов",
			Wins[0], Wins[1], Draws, Early, Kds, Unfinished);
		Fails += Unfinished ? 1 : 0;
	}

	// --- 2. Паритет с TS (web: corners:false, autopilot, roundSeconds 55, dt=fround(1/60)) — эталон снят vite-node:
	//   seed 42:   winner=0 DEC judges 30-27 30-27 30-27 hits=50-26 kd=0-0
	//   seed 7:    winner=0 DEC judges 30-27 30-27 30-27 hits=62-39 kd=0-0
	//   seed 1001: winner=0 DEC judges 28-29 29-28 29-29 hits=41-39 kd=0-0
	// Без углов — ещё и прежние хеши харнесса (до S-53) бит-в-бит; с углами — те же события/снимки боя и итог.
	struct FRef { uint32 Seed; int32 Hits0, Hits1; uint32 LegacyHash; };
	const FRef Refs[3] = {{42, 50, 26, 0x417ec113u}, {7, 62, 39, 0xaf91feedu}, {1001, 41, 39, 0xe7ad2b05u}};
	for (int32 K = 0; K < 3; ++K)
	{
		const FRunSummary P = RunFight(Refs[K].Seed, K == 0, 55.f, 0.f, false);
		const FRunSummary Pc = RunFight(Refs[K].Seed, false, 55.f, 0.f, true);
		const bool bOk = P.Hits[0] == Refs[K].Hits0 && P.Hits[1] == Refs[K].Hits1 && P.Winner == 0 && P.Hash == Refs[K].LegacyHash;
		const bool bOkC = Pc.Hits[0] == Refs[K].Hits0 && Pc.Hits[1] == Refs[K].Hits1 && Pc.Winner == 0 && Pc.FightHash == P.FightHash;
		printf("parity seed %u: без углов %s (hash %08x), с углами %s (бой с тика %d, всего тиков %d против %d)\n", Refs[K].Seed, bOk ? "OK" : "MISMATCH",
			P.Hash, bOkC ? "OK" : "MISMATCH", Pc.FirstFightTick, Pc.Ticks, P.Ticks);
		Fails += (bOk ? 0 : 1) + (bOkC ? 0 : 1);
	}

	// --- 3. Бой без нокдаунов с углами = бой без углов бит-в-бит (события + снимки боевого времени + итог) ---
	{
		int32 Compared = 0, Same = 0, WithKd = 0;
		for (int32 Pair = 0; Pair < 2; ++Pair)
			for (int32 Len = 0; Len < 2; ++Len)
				for (uint32 Seed = 1; Seed <= 100; ++Seed)
				{
					const float RoundSec = Len ? 180.f : 55.f;
					const FRunSummary N = RunFight(Seed, false, RoundSec, 0.f, false, Pair == 1);
					const FRunSummary C = RunFight(Seed, false, RoundSec, 0.f, true, Pair == 1);
					if (N.Kds[0] + N.Kds[1] + C.Kds[0] + C.Kds[1] > 0)
					{
						++WithKd;
						continue;
					}
					++Compared;
					if (N.FightHash == C.FightHash && N.Winner == C.Winner && N.Method == C.Method && N.Hits[0] == C.Hits[0] && N.Hits[1] == C.Hits[1]) ++Same;
				}
		printf("бой без нокдаунов: с углами == без углов бит-в-бит %d из %d (боёв с нокдаунами %d — там постановка меняет дальнейший бой)\n", Same,
			Compared, WithKd);
		Fails += (Same == Compared && Compared > 0) ? 0 : 1;
	}

	// --- 4. Постановка: все стадии, нет телепортов, часы честные, в нейтральный угол — в обход тела ---
	{
		int32 Seen[5] = {0, 0, 0, 0, 0};
		int32 Viol = 0, Unf = 0, MaxStage = 0;
		double MaxStageStep = 0, MaxFightStep = 0, MinBody = 1e9;
		int32 Stats[2][4] = {}; // [углы][победы красного, досрочки, нокдауны, бои]
		for (int32 Mode = 0; Mode < 2; ++Mode)
			for (uint32 Seed = 1; Seed <= 200; ++Seed)
			{
				const FRunSummary S = RunFight(Seed, false, 55.f, 3.f, Mode == 1, true);
				if (S.Method == 0) ++Unf;
				Stats[Mode][0] += S.Winner == 0;
				Stats[Mode][1] += (S.Method == 2 || S.Method == 3) ? 1 : 0;
				Stats[Mode][2] += S.Kds[0] + S.Kds[1];
				Stats[Mode][3] += 1;
				if (Mode == 0) continue;
				for (int32 K = 0; K < 5; ++K) Seen[K] += S.StageSeen[K];
				Viol += S.Violations;
				MaxStageStep = FMath::Max(MaxStageStep, S.MaxStageStep);
				MaxFightStep = FMath::Max(MaxFightStep, S.MaxFightStep);
				MinBody = FMath::Min(MinBody, S.MinBodyClear);
				MaxStage = FMath::Max(MaxStage, S.MaxStageTicks);
			}
		printf("постановка (панчер vs стеклянный, 200 сидов, 55 с): стадий out %d, rest %d, neutral %d, resume %d; нарушений %d, недоигранных %d\n",
			Seen[1], Seen[2], Seen[3], Seen[4], Viol, Unf);
		printf("  макс. шаг за тик: постановка %.4f м (WALK_SPEED·dt = %.4f), бой %.4f м; самая долгая ходьба %d тиков (%.2f с); мин. запас до тела %.3f м\n",
			MaxStageStep, BoxingStaging::WALK_SPEED / 60.0, MaxFightStep, MaxStage, MaxStage / 60.0, MinBody);
		for (int32 Mode = 0; Mode < 2; ++Mode)
			printf("  %s: победы панчера %d/%d, досрочек %d, нокдаунов %d (%.2f за бой)\n", Mode ? "с углами " : "без углов", Stats[Mode][0], Stats[Mode][3],
				Stats[Mode][1], Stats[Mode][2], Stats[Mode][2] / double(Stats[Mode][3]));
		const bool bOk = Viol == 0 && Unf == 0 && Seen[1] > 0 && Seen[2] > 0 && Seen[3] > 0 && Seen[4] > 0;
		printf("постановка: %s\n", bOk ? "OK" : "FAIL");
		Fails += bOk ? 0 : 1;
		const FRunSummary D1 = RunFight(7, false, 55.f, 3.f, true, true);
		const FRunSummary D2 = RunFight(7, false, 55.f, 3.f, true, true);
		const bool bDet2 = D1.Hash == D2.Hash && D1.FightHash == D2.FightHash && D1.Ticks == D2.Ticks;
		printf("determinism с нокдаунами (PG seed 7): %s\n", bDet2 ? "OK" : "MISMATCH");
		Fails += bDet2 ? 0 : 1;
	}

	// --- 5. Трасса постановки (сверка с web: выход из углов — бой с тика 126 в (∓0.575, 0)) ---
	printf("трасса AB seed 42 [углы, 55 с, перерыв 0]:\n");
	const FRunSummary T1 = RunFight(42, false, 55.f, 0.f, true, false, true);
	printf("трасса PG seed 7 [углы, 55 с, перерыв 0]:\n");
	RunFight(7, false, 55.f, 0.f, true, true, true);
	const bool bWalk = T1.FirstFightTick == 126;
	printf("выход из углов = web (бой с тика 126): %s\n", bWalk ? "OK" : "MISMATCH");
	Fails += bWalk ? 0 : 1;

	Fails += CheckGeometry();
	Fails += G_Fail ? 1 : 0;
	printf("ИТОГ: %s\n", Fails ? "FAIL" : "OK");
	return Fails ? 1 : 0;
}
