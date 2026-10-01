// Сидированный бой ИИ-vs-ИИ до конца + проверка детерминизма (запуск — когда появится компилятор).
#include "BoxingFightCore.h"
extern "C" int printf(const char*, ...);

static FFightConfig MakeConfig(uint32 Seed, float RoundSec = 180.f, float BreakSec = 60.f)
{
	FFightConfig C;
	C.Seed = Seed;
	C.Rounds = 3;
	C.RoundSeconds = RoundSec;
	C.BreakSeconds = BreakSec;
	FFighterSetup& A = C.Fighters[0];
	A.Stats = {78, 80, 76, 74, 75, 82, 79};
	A.ReachCm = 185; A.WeightKg = 71; A.Style = EBoxStyle::Technical; A.bAiControlled = true;
	FFighterSetup& B = C.Fighters[1];
	B.Stats = {84, 74, 70, 78, 77, 74, 72};
	B.ReachCm = 180; B.WeightKg = 71; B.Style = EBoxStyle::Pressure; B.bAiControlled = true;
	return C;
}

struct FRunSummary { uint32 Hash; int32 Winner; int32 Method; int32 Stopped; int32 Ticks; int32 Hits[2]; int32 Kds[2]; };

static FRunSummary RunFight(uint32 Seed, bool bVerbose, float RoundSec = 180.f, float BreakSec = 60.f)
{
	FBoxingFightCore Core;
	Core.Init(MakeConfig(Seed, RoundSec, BreakSec));
	FRunSummary S = {2166136261u, -1, 0, 0, 0, {0, 0}, {0, 0}};
	const float Dt = 1.f / 60.f;
	while (!Core.IsOver() && S.Ticks < 60 * 60 * 30)
	{
		Core.Tick(Dt);
		++S.Ticks;
		TArray<FFightEvent> Ev = Core.PollEvents();
		for (int32 K = 0; K < Ev.Num(); ++K)
		{
			const FFightEvent& E = Ev[K];
			S.Hash = (S.Hash ^ (uint32(E.Kind) * 131u + uint32(E.Attacker + 1) * 7u + uint32(E.Magnitude * 1000.f))) * 16777619u;
			if (E.Kind == EFightEventKind::Hit) ++S.Hits[E.Attacker];
			if (E.Kind == EFightEventKind::Knockdown) ++S.Kds[E.Defender];
			if (bVerbose && (E.Kind == EFightEventKind::Knockdown || E.Kind == EFightEventKind::RoundEnd))
				printf("  t=%.2f r%d %s\n", E.Time, E.Round, E.Kind == EFightEventKind::Knockdown ? "KNOCKDOWN" : "round end");
		}
		const FFightSnapshot Sn = Core.GetSnapshot();
		S.Hash = (S.Hash ^ uint32(Sn.Fighters[0].X * 1e4f + 50000.f) ^ (uint32(Sn.Fighters[1].Health * 100.f) << 8)) * 16777619u;
	}
	const FFightResult& R = Core.GetResult();
	S.Winner = R.WinnerIndex; S.Method = int32(R.Method); S.Stopped = R.StoppedRound;
	if (bVerbose)
	{
		static const char* M[] = {"None", "DEC", "KO", "RSC", "DRAW"};
		printf("seed %u: winner=%d method=%s stopped=%d ticks=%d (%.1f s game time)\n", Seed, R.WinnerIndex, M[S.Method], R.StoppedRound, S.Ticks, S.Ticks / 60.0);
		for (int32 J = 0; J < 3; ++J) printf("  judge %d: %d-%d\n", J + 1, R.JudgeTotals[J].Red, R.JudgeTotals[J].Blue);
		for (int32 K = 0; K < R.Rounds.Num(); ++K)
			printf("  r%d landed %.1f-%.1f kd %d-%d dmg %d-%d stam %d-%d\n", R.Rounds[K].Round, R.Rounds[K].Landed[0], R.Rounds[K].Landed[1],
				R.Rounds[K].Knockdowns[0], R.Rounds[K].Knockdowns[1], R.Rounds[K].Damage[0], R.Rounds[K].Damage[1], R.Rounds[K].Stamina[0], R.Rounds[K].Stamina[1]);
		printf("  hits %d-%d, hash %08x\n", S.Hits[0], S.Hits[1], S.Hash);
	}
	return S;
}

int main()
{
	// mulberry32: эталон из web/src/engine/rng.ts (node) для seed 12345 — сравнить вручную.
	FBoxingRng Rng; Rng.Seed(12345);
	printf("mulberry32(12345): %.17g %.17g %.17g\n", Rng.Next(), Rng.Next(), Rng.Next());

	const FRunSummary A = RunFight(42, true);
	const FRunSummary B = RunFight(42, false);
	printf("determinism seed 42: %s\n", (A.Hash == B.Hash && A.Ticks == B.Ticks) ? "OK" : "MISMATCH");

	int32 Wins[2] = {0, 0}, Draws = 0, Early = 0, Unfinished = 0;
	for (uint32 Seed = 1; Seed <= 200; ++Seed)
	{
		const FRunSummary S = RunFight(Seed, false);
		if (S.Method == 0) ++Unfinished;
		else if (S.Winner < 0) ++Draws; else ++Wins[S.Winner];
		if (S.Method == 2 || S.Method == 3) ++Early;
	}
	printf("200 seeds: red %d, blue %d, draws %d, stoppages %d, unfinished %d\n", Wins[0], Wins[1], Draws, Early, Unfinished);
	// Паритет с TS (web: corners:false, autopilot, roundSeconds 55, dt=fround(1/60)) — эталон снят vite-node:
	//   seed 42:   winner=0 DEC judges 30-27 30-27 30-27 hits=50-26 kd=0-0
	//   seed 7:    winner=0 DEC judges 30-27 30-27 30-27 hits=62-39 kd=0-0
	//   seed 1001: winner=0 DEC judges 28-29 29-28 29-29 hits=41-39 kd=0-0
	struct FRef { uint32 Seed; int32 Hits0, Hits1; };
	const FRef Refs[3] = {{42, 50, 26}, {7, 62, 39}, {1001, 41, 39}};
	int32 ParityFails = 0;
	for (int32 K = 0; K < 3; ++K)
	{
		const FRunSummary P = RunFight(Refs[K].Seed, true, 55.f, 0.f);
		const bool bOk = P.Hits[0] == Refs[K].Hits0 && P.Hits[1] == Refs[K].Hits1 && P.Winner == 0;
		printf("parity seed %u: %s\n", Refs[K].Seed, bOk ? "OK" : "MISMATCH");
		if (!bOk) ++ParityFails;
	}
	printf("mulberry32 expected: 0.9797282677609473 0.3067522644996643 0.484205421525985\n");
	return Unfinished == 0 && A.Hash == B.Hash && ParityFails == 0 ? 0 : 1;
}
