// Харнесс ядра боя вне UE: детерминизм, 200 сидов, паритет с TS, постановка раунда (S-53), доли исходов vs веб и бот «человека» (S-57).
#include "BoxingFightCore.h"
#include "FightBot.h"
#include "FightProfile.h"
#include <chrono>
#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on) // S-73: как ядро — харнесс бит-в-бит с UE при любом /fp
#endif
extern "C" int printf(const char*, ...);

namespace
{
	double AbsD(double V) { return V < 0 ? -V : V; }
	double HypotD(double X, double Z) { return FMath::Sqrt(X * X + Z * Z); }
	bool G_StyleNoClinch = false; // S-76: sim.exe styles|pro N … noclinch — сравнение без клинча

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

// --- S-57: доля побед / досрочек / нокдаунов против эталона веба (WebRef.inc, генератор — webref.ts) ---
namespace
{
	struct FRefProf
	{
		double S[7];
		double Reach, Weight, Mass, Dur, Seasoning;
		EBoxStyle Style;
	};
	struct FRefTally
	{
		double W, Stop, Kd;
	};
	struct FRefPair
	{
		const char* Name;
		int32 Rounds;
		bool bPro;
		FRefProf A, B;
		FRefTally Sim, Web;
	};
	const FRefPair GWebRef[] = {
#include "WebRef.inc"
	};
	constexpr int32 GWebRefNum = int32(sizeof(GWebRef) / sizeof(GWebRef[0]));

	FFighterSetup SetupOf(const FRefProf& P)
	{
		FFighterSetup S;
		S.Stats = {float(P.S[0]), float(P.S[1]), float(P.S[2]), float(P.S[3]), float(P.S[4]), float(P.S[5]), float(P.S[6])};
		S.ReachCm = float(P.Reach);
		S.WeightKg = float(P.Weight);
		S.MassForPower = float(P.Mass);
		S.DurabilityMass = float(P.Dur);
		S.Seasoning = float(P.Seasoning);
		S.Style = P.Style;
		S.bAiControlled = true;
		return S;
	}

	uint32 RefSeed(int32 I) { return uint32(I) * 2654435761u + 17u; } // seedOf веба: (i·2654435761 + 17) >>> 0

	// Как autoFight веба: нечётные сиды — фаворит в синем углу. Тик 1/60 (как GameMode), перерыв — сразу.
	FRefTally RunRefPair(const FRefPair& P, int32 N, float RoundSec)
	{
		FRefTally T = {0, 0, 0};
		for (int32 I = 0; I < N; ++I)
		{
			const bool bSwap = I % 2 == 1;
			FFightConfig C;
			C.Seed = RefSeed(I);
			C.Rounds = P.Rounds;
			C.RoundSeconds = RoundSec;
			C.BreakSeconds = 0.f;
			C.bAllowDraw = P.bPro;
			C.Fighters[bSwap ? 1 : 0] = SetupOf(P.A);
			C.Fighters[bSwap ? 0 : 1] = SetupOf(P.B);
			FBoxingFightCore Core;
			Core.Init(C);
			for (int32 K = 0; K < 60 * 60 * 60 && !Core.IsOver(); ++K)
			{
				Core.Tick(1.f / 60.f);
				Core.PollEvents();
			}
			const FFightResult& R = Core.GetResult();
			int32 W = R.WinnerIndex;
			if (bSwap && W >= 0) W = 1 - W;
			T.W += W == 0 ? 1 : 0;
			T.Stop += (R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC) ? 1 : 0;
			T.Kd += R.Knockdowns[0] + R.Knockdowns[1];
		}
		T.W /= N;
		T.Stop /= N;
		T.Kd /= N;
		return T;
	}

	// Допуски — как interactive-parity.test.ts (интерактив веба vs simulate): победы ±0.12, досрочки ±0.12,
	// нокдауны ±max(0.25, 30%). Эталон — интерактив веба на 55 с; UE проверяется и на 55, и на 180 с. Доля побед —
	// к вебу ИЛИ к simulate (сам веб отходит от simulate до ~0.09 на паре 4 р.: чужой допуск не съедаем).
	int32 CheckWebRef(int32 N, float RoundSec, bool bVerbose)
	{
		int32 Fails = 0;
		double SumKdUe = 0, SumKdWeb = 0, SumStUe = 0, SumStWeb = 0;
		printf("доля побед/досрочек/нокдаунов vs эталон веба (раунд UE %.0f с, %d боёв на пару; веб — 55 с, автопилот):\n", RoundSec, N);
		for (int32 K = 0; K < GWebRefNum; ++K)
		{
			const FRefPair& P = GWebRef[K];
			const FRefTally U = RunRefPair(P, N, RoundSec);
			const bool bW = AbsD(U.W - P.Web.W) <= 0.12 || AbsD(U.W - P.Sim.W) <= 0.12;
			const bool bS = AbsD(U.Stop - P.Web.Stop) <= 0.12;
			const bool bK = AbsD(U.Kd - P.Web.Kd) <= FMath::Max(0.25, P.Web.Kd * 0.3);
			const bool bOk = bW && bS && bK;
			Fails += bOk ? 0 : 1;
			SumKdUe += U.Kd; SumKdWeb += P.Web.Kd; SumStUe += U.Stop; SumStWeb += P.Web.Stop;
			if (bVerbose || !bOk)
				printf("  %s %-34s побед A UE %.3f / веб %.3f / sim %.3f; досрочек %.3f / %.3f / %.3f; нокдаунов %.3f / %.3f / %.3f\n", bOk ? "  " : "!!", P.Name,
					U.W, P.Web.W, P.Sim.W, U.Stop, P.Web.Stop, P.Sim.Stop, U.Kd, P.Web.Kd, P.Sim.Kd);
		}
		printf("  сумма по парам: досрочек UE %.2f / веб %.2f, нокдаунов за бой UE %.2f / веб %.2f — %s\n", SumStUe, SumStWeb, SumKdUe, SumKdWeb,
			Fails ? "MISMATCH" : "OK");
		return Fails ? 1 : 0;
	}
}

// --- S-57: бот «человека» (FightBot) против ИИ — как бот веба (interactive-parity «человек против ИИ») ---
namespace
{
	struct FBotTally
	{
		int32 N = 0, Wins = 0, Ko = 0, Rsc = 0, Dec = 0, KdFor = 0, KdAgainst = 0, HitsFor = 0, HitsAgainst = 0, Gassed = 0;
		int32 Draws = 0, Decisions = 0, SameCards = 0, LossStop = 0; // S-61: ничьи, решения, решения с тремя одинаковыми картами, проигрыши досрочно
		double Rate() const { return N ? double(Wins) / N : 0; }
	};

	// Сид k-го боя серии — как humanWinRate веба (seedOf(i) ^ 0xabc); тот же у GameMode -BoxBotFights.
	uint32 BotSeed(int32 K) { return RefSeed(K) ^ 0xabcu; }

	// Ввод — как GameMode: нажатия бота копятся и применяются на границе шага, удержание ног повторяется каждый шаг.
	FBotTally RunBotSetups(EFightBotSkill Skill, const FFighterSetup& Me, const FFighterSetup& Ai, int32 N, float RoundSec, int32 Rounds = 3,
		bool bPro = false)
	{
		FBotTally T;
		for (int32 K = 0; K < N; ++K)
		{
			FFightConfig C;
			C.Seed = BotSeed(K);
			C.Rounds = Rounds;
			C.RoundSeconds = RoundSec;
			C.BreakSeconds = 0.f;
			C.bAllowDraw = bPro;
			C.bProRules = bPro;
			C.bClinch = !G_StyleNoClinch;
			C.Fighters[0] = Me;
			C.Fighters[0].bAiControlled = false;
			C.Fighters[1] = Ai;
			C.Fighters[1].bAiControlled = true;
			FBoxingFightCore Core;
			Core.Init(C);
			FFightBot Bot;
			Bot.Reset(Skill, C.Seed, 0);
			TArray<FFightBotCmd> Cmds;
			const float Dt = 1.f / 60.f;
			for (int32 Step = 0; Step < 60 * 60 * 60 && !Core.IsOver(); ++Step)
			{
				Cmds.Reset();
				bool bHeld = false;
				EFightAction Held = EFightAction::StepBack;
				Bot.Think(Core.GetSnapshot(), Dt, Cmds, bHeld, Held);
				for (int32 I = 0; I < Cmds.Num(); ++I) Core.ApplyAction(0, Cmds[I].Action, Cmds[I].Target);
				if (bHeld) Core.ApplyAction(0, Held);
				Core.Tick(Dt);
				TArray<FFightEvent> Ev = Core.PollEvents();
				for (int32 E = 0; E < Ev.Num(); ++E)
				{
					if (Ev[E].Kind == EFightEventKind::Hit) (Ev[E].Attacker == 0 ? T.HitsFor : T.HitsAgainst) += 1;
					if (Ev[E].Kind == EFightEventKind::Knockdown) (Ev[E].Defender == 1 ? T.KdFor : T.KdAgainst) += 1;
					if (Ev[E].Kind == EFightEventKind::Gassed) ++T.Gassed;
				}
			}
			const FFightResult& R = Core.GetResult();
			++T.N;
			T.Wins += R.WinnerIndex == 0;
			T.Ko += R.Method == EFightMethod::KO;
			T.Rsc += R.Method == EFightMethod::RSC;
			T.Dec += R.Method == EFightMethod::Decision || R.Method == EFightMethod::Draw;
			T.Draws += R.WinnerIndex < 0;
			T.LossStop += R.WinnerIndex == 1 && (R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC);
			if (R.Method == EFightMethod::Decision || R.Method == EFightMethod::Draw)
			{
				++T.Decisions;
				T.SameCards += R.JudgeTotals[0].Red == R.JudgeTotals[1].Red && R.JudgeTotals[0].Blue == R.JudgeTotals[1].Blue &&
					R.JudgeTotals[0].Red == R.JudgeTotals[2].Red && R.JudgeTotals[0].Blue == R.JudgeTotals[2].Blue;
			}
		}
		return T;
	}
	FBotTally RunBot(EFightBotSkill Skill, const FRefProf& Me, const FRefProf& Ai, int32 N, float RoundSec)
	{
		return RunBotSetups(Skill, SetupOf(Me), SetupOf(Ai), N, RoundSec);
	}

	void PrintBot(const char* Label, EFightBotSkill Skill, const FBotTally& T)
	{
		printf("  %-8s %-22s побед %3d/%d (%.0f%%)  KO %d RSC %d реш. %d  нокдаунов %d/%d  попаданий за бой %.1f/%.1f  «нет сил» за бой %.1f\n",
			FFightBot::SkillName(Skill), Label, T.Wins, T.N, T.Rate() * 100, T.Ko, T.Rsc, T.Dec, T.KdFor, T.KdAgainst,
			double(T.HitsFor) / T.N, double(T.HitsAgainst) / T.N, double(T.Gassed) / T.N);
	}

	// Ориентиры interactive-parity веба: средний vs равного 35..72%, сильный > средний > новичок, спам < среднего и < 5%,
	// статы решают: средний бьёт слабого (76 vs 70) > 70%, сильному (70 vs 76) проигрывает чаще (< 40%).
	int32 CheckBots(int32 N, float RoundSec, bool bVerbose)
	{
		const FRefProf& Even = GWebRef[0].A;   // равные 70/70
		const FRefProf& Strong76 = GWebRef[1].A; // 76
		printf("бот «человека» против ИИ (3 р. × %.0f с, %d боёв, ввод — как GameMode):\n", RoundSec, N);
		FBotTally Tl[4];
		const EFightBotSkill Skills[4] = {EFightBotSkill::Novice, EFightBotSkill::Average, EFightBotSkill::Strong, EFightBotSkill::Masher};
		for (int32 K = 0; K < 4; ++K)
		{
			Tl[K] = RunBot(Skills[K], Even, Even, N, RoundSec);
			if (bVerbose) PrintBot("против равного 70/70", Skills[K], Tl[K]);
		}
		const FBotTally Up = RunBot(EFightBotSkill::Average, Strong76, Even, N / 2, RoundSec);
		const FBotTally Down = RunBot(EFightBotSkill::Average, Even, Strong76, N / 2, RoundSec);
		if (bVerbose)
		{
			PrintBot("76 против 70", EFightBotSkill::Average, Up);
			PrintBot("70 против 76", EFightBotSkill::Average, Down);
			// Пресеты GameMode по умолчанию (L_Ring без меню): сверка с UE «-BoxBot=average -BoxBotFights=100» — те же сиды и ввод,
			// итог должен совпасть бой-в-бой.
			const FRefProf Red = {{78, 80, 76, 74, 75, 82, 79}, 185, 71, 71, 71, 1, EBoxStyle::Technical};
			const FRefProf Blue = {{84, 74, 70, 78, 77, 74, 72}, 180, 71, 71, 71, 1, EBoxStyle::Pressure};
			PrintBot("пресеты GameMode", EFightBotSkill::Average, RunBot(EFightBotSkill::Average, Red, Blue, 100, RoundSec));
		}
		const double Nov = Tl[0].Rate(), Avg = Tl[1].Rate(), Str = Tl[2].Rate(), Mash = Tl[3].Rate();
		const bool bOk = Avg >= 0.35 && Avg <= 0.72 && Str > Avg && Nov < Avg && Mash < Avg && Mash < 0.05 && Up.Rate() > 0.7 && Down.Rate() < 0.4;
		printf("  новичок %.2f < средний %.2f < сильный %.2f; спам %.2f; средний 76/70 %.2f, 70/76 %.2f — %s\n", Nov, Avg, Str, Mash, Up.Rate(),
			Down.Rate(), bOk ? "OK" : "MISMATCH");
		return bOk ? 0 : 1;
	}
}

// --- S-61: ростер UE (RosterRef.inc, генератор — rosterref.mjs): пары плейтеста QA и досрочки профи по весу ---
#include "RosterRef.inc"
namespace
{
	struct FRosterRef
	{
		const char* Name;
		bool bFemale;
		int32 Kind; // 0 любитель, 1 профи, 2 легенда
		double W, Reach;
		double S[7], PS[7];
		double PSeason, ProOverall;
		EBoxStyle Style;
	};
	const FRosterRef GRoster[] = {ROSTER_REF_BOXERS};
	struct FQaRef
	{
		const char* Name;
		int32 R, B;
	};
	const FQaRef GQa[] = {ROSTER_REF_QA};
	struct FProPairRef
	{
		const char* Div;
		int32 A, B, Rounds;
	};
	const FProPairRef GProPairs[] = {ROSTER_REF_PRO_PAIRS};
	constexpr int32 GProPairNum = int32(sizeof(GProPairs) / sizeof(GProPairs[0]));
	const double GProClassesM[] = {ROSTER_REF_PRO_CLASSES_M};
	const double GProClassesF[] = {ROSTER_REF_PRO_CLASSES_F};

	// Как FRosterBoxer::ToPreset → FBoxerPreset::ToSetup: в профи-бою — ProStats + обстрелянность, иначе Stats.
	FFighterSetup RosterSetup(const FRosterRef& B, bool bProFight)
	{
		const double* X = bProFight ? B.PS : B.S;
		FFighterSetup S;
		S.Stats = {float(X[0]), float(X[1]), float(X[2]), float(X[3]), float(X[4]), float(X[5]), float(X[6])};
		S.ReachCm = float(B.Reach);
		S.WeightKg = float(B.W);
		S.Style = B.Style;
		S.Seasoning = bProFight ? float(B.PSeason) : 1.f;
		S.bFemale = B.bFemale;
		S.bAiControlled = true;
		return S;
	}

	// Пара Выставки, как UBoxingGameInstanceSubsystem::ApplyToFightMode: профи-правила, если хоть один не любитель; вес
	// боя — кэтчвейт по категориям (любители — WEIGHT_CLASSES веба, иначе — веса дивизионов профи/легенд ростера).
	struct FPairSetup
	{
		FFighterSetup R, B;
		bool bPro = false;
		double Ring = 0;
	};
	FPairSetup PairOf(const FRosterRef& R, const FRosterRef& B, bool bProject)
	{
		FPairSetup P;
		P.bPro = R.Kind != 0 || B.Kind != 0;
		P.R = RosterSetup(R, P.bPro);
		P.B = RosterSetup(B, P.bPro);
		const double* Cls = GProClassesM;
		int32 Num = int32(sizeof(GProClassesM) / sizeof(double));
		if (!P.bPro)
		{
			Cls = R.bFemale ? BoxingFightProfile::AMATEUR_CLASSES_F : BoxingFightProfile::AMATEUR_CLASSES_M;
			Num = R.bFemale ? int32(sizeof(BoxingFightProfile::AMATEUR_CLASSES_F) / sizeof(double))
							: int32(sizeof(BoxingFightProfile::AMATEUR_CLASSES_M) / sizeof(double));
		}
		else if (R.bFemale)
		{
			Cls = GProClassesF;
			Num = int32(sizeof(GProClassesF) / sizeof(double));
		}
		P.Ring = BoxingFightProfile::Catchweight(R.W, B.W, Cls, Num);
		if (bProject)
		{
			P.R = BoxingFightProfile::ProjectToWeight(P.R, P.Ring);
			P.B = BoxingFightProfile::ProjectToWeight(P.B, P.Ring);
		}
		return P;
	}

	// sim.exe qa <N> — таблица плейтеста QA (bal.sh): бот за красного, 4 уровня, без и с проекцией веса; судьи.
	int32 QaTable(int32 N, float RoundSec)
	{
		const EFightBotSkill Skills[4] = {EFightBotSkill::Novice, EFightBotSkill::Average, EFightBotSkill::Strong, EFightBotSkill::Masher};
		printf("пары плейтеста QA: бот за красного против ИИ, %d боёв, раунд %.0f с, сиды bal.sh (k·2654435761 + 17) ^ 0xabc\n", N, RoundSec);
		for (const FQaRef& Q : GQa)
		{
			const FRosterRef& R = GRoster[Q.R];
			const FRosterRef& B = GRoster[Q.B];
			for (int32 Proj = 0; Proj < 2; ++Proj)
			{
				const FPairSetup P = PairOf(R, B, Proj == 1);
				const int32 Rounds = P.bPro ? 10 : 3;
				printf("  %-30s %s %2d р., вес %s %3.0f:", Q.Name, P.bPro ? "профи " : "любит.", Rounds, Proj ? "боя " : "свой", Proj ? P.Ring : 0.0);
				int32 Dec = 0, Same = 0, Draw = 0, Fights = 0, Stops = 0;
				double Kd = 0;
				for (int32 K = 0; K < 4; ++K)
				{
					const FBotTally T = RunBotSetups(Skills[K], P.R, P.B, N, RoundSec, Rounds, P.bPro);
					printf(" %s %3.0f%%", FFightBot::SkillName(Skills[K]), T.Rate() * 100);
					Dec += T.Decisions;
					Same += T.SameCards;
					Draw += T.Draws;
					Fights += T.N;
					Stops += T.Ko + T.Rsc;
					Kd += T.KdFor + T.KdAgainst;
				}
				printf(" | досрочек %.0f%%, нокдаунов/бой %.2f, ничьих %.0f%%, три одинаковые карты %.0f%% решений\n", 100.0 * Stops / Fights,
					Kd / Fights, 100.0 * Draw / Fights, Dec ? 100.0 * Same / Dec : 0.0);
				if (Proj)
				{
					// Прогноз карточки Выставки (ForecastPair → PredictOutcome): ИИ против ИИ, 60 боёв.
					const BoxingFightProfile::FOutcomeOdds O = BoxingFightProfile::PredictOutcome(P.R, P.B, Rounds, P.bPro);
					printf("  %-30s прогноз карточки: красный %.0f%% (досрочно %.0f%%), синий %.0f%% (досрочно %.0f%%), ничья %.0f%%\n", "", O.RedWin * 100,
						O.RedStoppage * 100, O.BlueWin * 100, O.BlueStoppage * 100, O.Draw * 100);
				}
			}
		}
		return 0;
	}

	// S-65: судейство любителей по World Boxing — 5 судей, раунд без ничьих, равная карта → судья называет победителя.
	// Любительские пары QA (бот average за красного) + эталон AB (ИИ против ИИ), N боёв: формулировки решений, ровные карты,
	// раунды 10-10 (должно быть 0), решения «против суммы голосов», и проверка, что подпись совпадает с фактом карт.
	int32 AmateurJudgingTable(int32 N, bool bCheck)
	{
		printf("судейство любителей (S-65, World Boxing): 5 судей, %d боёв на пару, 55 с\n", N);
		int32 Bad = 0;
		auto Run = [&](const char* Name, const FFighterSetup& R0, const FFighterSetup& B0, bool bBot)
		{
			int32 Dec = 0, Una = 0, S41 = 0, S32 = 0, WithDraw = 0, EvenCards = 0, Nominated = 0, Draw10 = 0, Rounds = 0, LabelBad = 0, Judges = 0;
			for (int32 K = 0; K < N; ++K)
			{
				FFightConfig C;
				C.Seed = BotSeed(K);
				C.Rounds = 3;
				C.RoundSeconds = 55.f;
				C.BreakSeconds = 0.f;
				C.Fighters[0] = R0;
				C.Fighters[1] = B0;
				C.Fighters[0].bAiControlled = !bBot;
				C.Fighters[1].bAiControlled = true;
				FBoxingFightCore Core;
				Core.Init(C);
				FFightBot Bot;
				Bot.Reset(EFightBotSkill::Average, C.Seed, 0);
				TArray<FFightBotCmd> Cmds;
				for (int32 Step = 0; Step < 60 * 60 * 30 && !Core.IsOver(); ++Step)
				{
					if (bBot)
					{
						Cmds.Reset();
						bool bHeld = false;
						EFightAction Held = EFightAction::StepBack;
						Bot.Think(Core.GetSnapshot(), 1.f / 60.f, Cmds, bHeld, Held);
						for (int32 I = 0; I < Cmds.Num(); ++I) Core.ApplyAction(0, Cmds[I].Action, Cmds[I].Target);
						if (bHeld) Core.ApplyAction(0, Held);
					}
					Core.Tick(1.f / 60.f);
					Core.PollEvents();
				}
				const FFightResult& R = Core.GetResult();
				Judges = R.NumJudges;
				for (int32 Ri = 0; Ri < R.Rounds.Num(); ++Ri)
					for (int32 J = 0; J < R.NumJudges; ++J)
					{
						const FRoundResult& Rr = R.Rounds[Ri];
						++Rounds;
						Draw10 += Rr.JudgeCards[J].Red == Rr.JudgeCards[J].Blue;
					}
				if (R.Method != EFightMethod::Decision) continue;
				++Dec;
				int32 For = 0, Against = 0, Even = 0;
				for (int32 J = 0; J < R.NumJudges; ++J)
				{
					const FJudgeCard& T = R.JudgeTotals[J];
					int32 Pick = T.Red > T.Blue ? 0 : (T.Blue > T.Red ? 1 : -1);
					if (Pick < 0)
					{
						++EvenCards;
						if (R.TieNominee[J] >= 0) { ++Nominated; Pick = R.TieNominee[J]; }
					}
					if (Pick < 0) ++Even;
					else if (Pick == R.WinnerIndex) ++For;
					else ++Against;
				}
				WithDraw += Even > 0;
				// Подпись по факту карт: единогласно — все за победителя; иначе раздельное; победитель — большинство голосов.
				const EDecisionKind Want = For == R.NumJudges ? EDecisionKind::Unanimous : EDecisionKind::Split;
				LabelBad += R.Decision != Want || For <= Against;
				Una += For == 5;
				S41 += For == 4;
				S32 += For == 3;
			}
			printf("  %-24s %s судей %d: решений %d — единогласно 5:0 %.0f%%, раздельно 4:1/4+н %.0f%%, 3:2/3+н %.0f%%; ровных карт %d (из них судья назвал победителя %d, осталась ничья в %d решениях); "
				"раундов 10-10 %d из %d; подпись ≠ карты %d\n", Name, bBot ? "бот average" : "ИИ vs ИИ  ", Judges, Dec, Dec ? 100.0 * Una / Dec : 0.0,
				Dec ? 100.0 * S41 / Dec : 0.0, Dec ? 100.0 * S32 / Dec : 0.0, EvenCards, Nominated, WithDraw, Draw10, Rounds, LabelBad);
			Bad += (Draw10 > 0) + (LabelBad > 0) + (Judges != 5);
		};
		for (const FQaRef& Q : GQa)
		{
			const FPairSetup P = PairOf(GRoster[Q.R], GRoster[Q.B], true);
			if (P.bPro) continue;
			Run(Q.Name, P.R, P.B, true);
		}
		const FFightConfig Ab = MakeConfig(1, 55.f, 0.f, false, false);
		Run("эталон AB 78 vs 84", Ab.Fighters[0], Ab.Fighters[1], false);
		if (!bCheck) return 0;
		printf("судейство любителей: 5 судей, ни одного 10-10, подпись решения = факт карт — %s\n", Bad ? "MISMATCH" : "OK");
		return Bad ? 1 : 0;
	}

	// S-65: «шансы при твоей игре» (PredictForPlayer: average 150 боёв, края 60, сиды прогноза) против факта — серии бота на
	// НЕЗАВИСИМЫХ сидах (bal.sh: BotSeed), N боёв; ±95% ДИ факта. Плюс прогноз ИИ против ИИ (PredictOutcome) для сравнения.
	// bCheck — проверка харнесса: average-прогноз в пределах 95% ДИ разности серий от факта на всех парах, порядок novice ≤ average ≤ strong.
	int32 PlayerForecastTable(int32 N, bool bCheck)
	{
		const EFightBotSkill Skills[3] = {EFightBotSkill::Novice, EFightBotSkill::Average, EFightBotSkill::Strong};
		printf("шансы при твоей игре (S-65): прогноз = бот за красного, average 240 / края 60 боёв (сиды прогноза), факт — %d боёв (сиды bal.sh), 55 с, вес боя\n", N);
		printf("  %-26s %-9s | %-17s | %-29s | %-29s | %-29s\n", "пара", "", "ИИ vs ИИ (карточка)", "novice прогноз / факт", "average прогноз / факт",
			"strong прогноз / факт");
		int32 Bad = 0;
		double MsSum = 0;
		int32 MsN = 0;
		for (const FQaRef& Q : GQa)
		{
			const FPairSetup P = PairOf(GRoster[Q.R], GRoster[Q.B], true);
			const int32 Rounds = P.bPro ? 10 : 3;
			const BoxingFightProfile::FOutcomeOdds Ai = BoxingFightProfile::PredictOutcome(P.R, P.B, Rounds, P.bPro);
			const auto T0 = std::chrono::steady_clock::now();
			const BoxingFightProfile::FPlayerOdds Pl = BoxingFightProfile::PredictForPlayer(P.R, P.B, Rounds, P.bPro);
			const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
			MsSum += Ms;
			++MsN;
			const BoxingFightProfile::FOutcomeOdds* Fc[3] = {&Pl.Novice, &Pl.Average, &Pl.Strong};
			printf("  %-26s %s %2d р. | %3.0f%% (ничья %2.0f%%) |", Q.Name, P.bPro ? "профи " : "любит.", Rounds, Ai.RedWin * 100, Ai.Draw * 100);
			for (int32 K = 0; K < 3; ++K)
			{
				const FBotTally T = RunBotSetups(Skills[K], P.R, P.B, N, 55.f, Rounds, P.bPro);
				const double F = T.Rate();
				const double Var = FMath::Max(F * (1 - F), 0.01);
				const double Ci = 1.96 * FMath::Sqrt(Var / T.N);
				// Допуск сверки: 95% ДИ разности двух независимых серий (прогноз Fc.Fights + факт N боёв), не меньше 6 п.
				const double Tol = FMath::Max(0.06, 1.96 * FMath::Sqrt(Var / T.N + Var / FMath::Max(1, Fc[K]->Fights)));
				printf(" %3.0f%% / %3.0f%% ±%2.0f (н %2.0f/%2.0f) |", Fc[K]->RedWin * 100, F * 100, Ci * 100, Fc[K]->Draw * 100, 100.0 * T.Draws / T.N);
				if (K == 1 && AbsD(Fc[K]->RedWin - F) > Tol) ++Bad;
			}
			Bad += !(Pl.Novice.RedWin <= Pl.Average.RedWin + 0.05 && Pl.Average.RedWin <= Pl.Strong.RedWin + 0.05);
			printf(" %.0f мс\n", Ms);
		}
		printf("  прогноз трёх уровней: в среднем %.0f мс на пару (360 боёв ядра + бот)\n", MsSum / FMath::Max(1, MsN));
		if (!bCheck) return 0;
		printf("шансы при твоей игре: average-прогноз vs факт в пределах ДИ разности серий, novice ≤ average ≤ strong — %s\n", Bad ? "MISMATCH" : "OK");
		return Bad ? 1 : 0;
	}

	// S-73: сверка «шансов при твоей игре» (карточка) с бот-сериями UE. Варианты на одной паре, бот average:
	//   card    — ровно карточка: PredictWithBot, сиды прогноза (^0x5eed), с постановкой углов (S-73), 240 боёв;
	//   fc N    — те же сиды, N боёв (сходимость карточки);
	//   ue N    — как -BoxBotFights UE: сиды ^0xabc, С постановкой углов (RunBotSetups);
	//   = UE    — PredictWithBot на сидах ^0xabc: обязан совпасть с «ue» бой-в-бой (тот же путь, что -BoxBotFights);
	//   кусками по 50 — разброс 50-боевых серий (как плейтест QA).
	int32 ForecastAudit(int32 N, int32 Only)
	{
		printf("S-73: сверка прогноза average (карточка) с бот-сериями, %d боёв на вариант, раунд 55 с, вес боя\n", N);
		for (int32 Qi = 0; Qi < int32(sizeof(GQa) / sizeof(GQa[0])); ++Qi)
		{
			if (Only >= 0 && Qi != Only) continue;
			const FQaRef& Q = GQa[Qi];
			const FPairSetup P = PairOf(GRoster[Q.R], GRoster[Q.B], true);
			const int32 Rounds = P.bPro ? 10 : 3;
			const auto Ci = [](double F, int32 Num) { return 1.96 * FMath::Sqrt(FMath::Max(F * (1 - F), 0.01) / FMath::Max(1, Num)); };
			const BoxingFightProfile::FOutcomeOdds Card = BoxingFightProfile::PredictWithBot(P.R, P.B, Rounds, P.bPro, EFightBotSkill::Average, 240);
			const BoxingFightProfile::FOutcomeOdds Fc = BoxingFightProfile::PredictWithBot(P.R, P.B, Rounds, P.bPro, EFightBotSkill::Average, N);
			const BoxingFightProfile::FOutcomeOdds Nc = BoxingFightProfile::PredictWithBot(P.R, P.B, Rounds, P.bPro, EFightBotSkill::Average, N, 0xabcu);
			const FBotTally Ue = RunBotSetups(EFightBotSkill::Average, P.R, P.B, N, 55.f, Rounds, P.bPro);
			printf("  %-26s %d р. | карточка %3.0f%% | прогноз-сиды %d: %4.1f%% ±%3.1f | прогноз на UE-сидах %4.1f%% (%s) | UE-сиды с углами (как -BoxBotFights) %4.1f%% ±%3.1f\n",
				Q.Name, Rounds, Card.RedWin * 100, N, Fc.RedWin * 100, Ci(Fc.RedWin, N) * 100, Nc.RedWin * 100, int32(Nc.RedWin * N + 0.5f) == Ue.Wins ? "= UE бой-в-бой" : "≠ UE!", Ue.Rate() * 100,
				Ci(Ue.Rate(), Ue.N) * 100);
			// Разброс серий по 50 (UE-сиды с углами, непересекающиеся куски).
			double Lo = 1, Hi = 0;
			int32 Chunks = 0, Far = 0;
			for (int32 From = 0; From + 50 <= N; From += 50)
			{
				int32 W = 0;
				char Seq[51] = {};
				for (int32 K = From; K < From + 50; ++K)
				{
					FFightConfig C;
					C.Seed = BotSeed(K);
					C.Rounds = Rounds;
					C.RoundSeconds = 55.f;
					C.BreakSeconds = 0.f;
					C.bAllowDraw = P.bPro;
					C.bProRules = P.bPro;
					C.Fighters[0] = P.R;
					C.Fighters[0].bAiControlled = false;
					C.Fighters[1] = P.B;
					C.Fighters[1].bAiControlled = true;
					FBoxingFightCore Core;
					Core.Init(C);
					FFightBot Bot;
					Bot.Reset(EFightBotSkill::Average, C.Seed, 0);
					TArray<FFightBotCmd> Cmds;
					for (int32 S = 0; S < 60 * 60 * 60 && !Core.IsOver(); ++S)
					{
						Cmds.Reset();
						bool bHeld = false;
						EFightAction Held = EFightAction::StepBack;
						Bot.Think(Core.GetSnapshot(), 1.f / 60.f, Cmds, bHeld, Held);
						for (int32 I = 0; I < Cmds.Num(); ++I) Core.ApplyAction(0, Cmds[I].Action, Cmds[I].Target);
						if (bHeld) Core.ApplyAction(0, Held);
						Core.Tick(1.f / 60.f);
						Core.PollEvents();
					}
					W += Core.GetResult().WinnerIndex == 0;
					Seq[K - From] = Core.GetResult().WinnerIndex == 0 ? 'W' : '.';
				}
				const double R = W / 50.0;
				if (From < 200) printf("      серия %d-%d: %d/50 %s\n", From, From + 49, W, Seq);
				Lo = FMath::Min(Lo, R);
				Hi = FMath::Max(Hi, R);
				Far += AbsD(R - Fc.RedWin) >= 0.17 ? 1 : 0;
				++Chunks;
			}
			if (Chunks > 0)
				printf("      серии по 50 (UE-сиды, углы): %d серий, от %.0f%% до %.0f%%; отклонение ≥ 17 п. от прогноза — %d из %d\n", Chunks, Lo * 100, Hi * 100,
					Far, Chunks);
		}
		return 0;
	}

	// S-76: ИИ стилей против человека — бот × стиль ИИ на равных 70/70 (3 р. × 55 с) + «почерк» стиля (что ИИ делает в бою с
	// average). Ориентир: average 40–55% против КАЖДОГО стиля, strong > average > novice, спам/абьюз ≤ 10%.
	struct FStyleFeel
	{
		int32 Fights = 0;
		double Ticks = 0, DistSum = 0, HumanRope = 0, Clinch = 0, HumanStamSum = 0;
		int32 AiPunches = 0, AiPower = 0, AiBody = 0, AiCounterHits = 0, AiHits = 0, GuardBreaks = 0, AiSteps = 0, Clinches = 0;
		int32 MaxSeries = 0, Series3 = 0; // серии ИИ: ударов подряд с паузой < 0.45 с
	};
	FBotTally RunStyleSeries(EFightBotSkill Skill, const FFighterSetup& Me, const FFighterSetup& Ai, int32 N, FStyleFeel* Feel, float RoundSec = 55.f)
	{
		FBotTally T;
		for (int32 K = 0; K < N; ++K)
		{
			FFightConfig C;
			C.Seed = BotSeed(K);
			C.Rounds = 3;
			C.RoundSeconds = RoundSec;
			C.BreakSeconds = 0.f;
			C.bClinch = !G_StyleNoClinch;
			C.Fighters[0] = Me;
			C.Fighters[0].bAiControlled = false;
			C.Fighters[1] = Ai;
			C.Fighters[1].bAiControlled = true;
			FBoxingFightCore Core;
			Core.Init(C);
			FFightBot Bot;
			Bot.Reset(Skill, C.Seed, 0);
			TArray<FFightBotCmd> Cmds;
			const float Dt = 1.f / 60.f;
			bool bWasPunching = false, bWasClinch = false;
			EStepKind PrevStep = EStepKind::None;
			double LastAiPunchT = -10;
			int32 Run = 0;
			for (int32 Step = 0; Step < 60 * 60 * 60 && !Core.IsOver(); ++Step)
			{
				Cmds.Reset();
				bool bHeld = false;
				EFightAction Held = EFightAction::StepBack;
				const FFightSnapshot S = Core.GetSnapshot();
				Bot.Think(S, Dt, Cmds, bHeld, Held);
				for (int32 I = 0; I < Cmds.Num(); ++I) Core.ApplyAction(0, Cmds[I].Action, Cmds[I].Target);
				if (bHeld) Core.ApplyAction(0, Held);
				Core.Tick(Dt);
				if (Feel && S.Phase == EFightPhase::Fighting)
				{
					const FFighterState& A = S.Fighters[1];
					Feel->Ticks += 1;
					Feel->DistSum += S.Distance;
					Feel->HumanRope += S.Fighters[0].RopeLevel > 0 ? 1 : 0;
					Feel->HumanStamSum += S.Fighters[0].StaminaPct;
					Feel->Clinch += S.bClinch ? 1 : 0;
					if (S.bClinch && !bWasClinch) ++Feel->Clinches;
					bWasClinch = S.bClinch;
					if (A.bPunching && !bWasPunching)
					{
						++Feel->AiPunches;
						const double Now = Step * double(Dt);
						Run = Now - LastAiPunchT < 0.45 + A.PunchDuration ? Run + 1 : 1;
						if (Run == 3) ++Feel->Series3;
						Feel->MaxSeries = FMath::Max(Feel->MaxSeries, Run);
						LastAiPunchT = Now;
						Feel->AiPower += A.Punch != EPunchType::Jab;
						Feel->AiBody += A.PunchTarget == EPunchTarget::Body;
					}
					bWasPunching = A.bPunching;
					if (A.Step != EStepKind::None && A.Step != PrevStep) ++Feel->AiSteps;
					PrevStep = A.Step;
				}
				TArray<FFightEvent> Ev = Core.PollEvents();
				for (int32 E = 0; E < Ev.Num(); ++E)
				{
					if (Ev[E].Kind == EFightEventKind::Hit) (Ev[E].Attacker == 0 ? T.HitsFor : T.HitsAgainst) += 1;
					if (Ev[E].Kind == EFightEventKind::Knockdown) (Ev[E].Defender == 1 ? T.KdFor : T.KdAgainst) += 1;
					if (Ev[E].Kind == EFightEventKind::Gassed) ++T.Gassed;
					if (Feel && Ev[E].Kind == EFightEventKind::Hit && Ev[E].Attacker == 1)
					{
						++Feel->AiHits;
						Feel->AiCounterHits += Ev[E].bCounter;
						Feel->GuardBreaks += Ev[E].bGuardBreak;
					}
				}
			}
			const FFightResult& R = Core.GetResult();
			++T.N;
			T.Wins += R.WinnerIndex == 0;
			T.Ko += R.Method == EFightMethod::KO;
			T.Rsc += R.Method == EFightMethod::RSC;
			T.Dec += R.Method == EFightMethod::Decision || R.Method == EFightMethod::Draw;
			T.LossStop += R.WinnerIndex == 1 && (R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC);
			if (Feel) ++Feel->Fights;
		}
		return T;
	}

	// S-76: вариант судейства любителей (ВЫКЛ. по умолчанию — решение владельца): доля единогласных 5:0 при разных порогах
	// «явного» раунда / разбросе / вкусе судей. Пары — любительские пары QA (бот average) + равные 70/70 (бот average) + эталон AB
	// (ИИ против ИИ); реальный ориентир — ~69% единогласных (ЧМ 2025). Поток боя не меняется (попадания бой-в-бой с вариантом 0).
	int32 JudgeVariantTable(int32 N)
	{
		struct FVar { const char* Name; float Clear, Noise, Lean; };
		const FVar Vars[] = {
			{"сейчас (4.5 / ±1.6 / 0)", 0, 0, 0},
			{"порог 6", 6, 0, 0},
			{"порог 8", 8, 0, 0},
			{"порог 6, ±2.5", 6, 2.5f, 0},
			{"порог 8, ±2.5", 8, 2.5f, 0},
			{"порог 6, ±2, вкус 0.25", 6, 2, 0.25f},
			{"порог 8, ±2.5, вкус 0.25", 8, 2.5f, 0.25f},
			{"порог 10, ±3, вкус 0.3", 10, 3, 0.3f},
			{"порог 12, ±4, вкус 0.3", 12, 4, 0.3f},
			{"порог 15, ±5, вкус 0.3", 15, 5, 0.3f},
			{"порог 20, ±6, вкус 0.35", 20, 6, 0.35f},
			{"порог 25, ±8, вкус 0.4", 25, 8, 0.4f},
			{"порог 15, ±6, без вкуса", 15, 6, 0},
		};
		struct FPairRun { const char* Name; FFighterSetup R, B; bool bBot; };
		TArray<FPairRun> Pairs;
		for (const FQaRef& Q : GQa)
		{
			const FPairSetup P = PairOf(GRoster[Q.R], GRoster[Q.B], true);
			if (!P.bPro) Pairs.Add({Q.Name, P.R, P.B, true});
		}
		Pairs.Add({"равные 70/70", SetupOf(GWebRef[0].A), SetupOf(GWebRef[0].A), true});
		const FFightConfig Ab = MakeConfig(1, 55.f, 0.f, false, false);
		Pairs.Add({"эталон AB (ИИ vs ИИ)", Ab.Fighters[0], Ab.Fighters[1], false});
		printf("S-76: вариант судейства любителей (не включён), %d боёв на пару, 55 с; единогласных 5:0 среди решений, по парам и всего; "
			   "победитель по очкам ≠ варианта «сейчас»; ориентир реальности ~69%%\n", N);
		TArray<int32> BaseWin;
		TArray<int32> BaseHits;
		int32 Bad = 0;
		for (const FVar& V : Vars)
		{
			printf("  %-28s", V.Name);
			int32 DecAll = 0, UnaAll = 0, Flip = 0, Idx = 0;
			for (const FPairRun& P : Pairs)
			{
				int32 Dec = 0, Una = 0;
				for (int32 K = 0; K < N; ++K, ++Idx)
				{
					FFightConfig C;
					C.Seed = BotSeed(K);
					C.Rounds = 3;
					C.RoundSeconds = 55.f;
					C.BreakSeconds = 0.f;
					C.Fighters[0] = P.R;
					C.Fighters[1] = P.B;
					C.Fighters[0].bAiControlled = !P.bBot;
					C.Fighters[1].bAiControlled = true;
					C.AmJudgeClear = V.Clear;
					C.AmJudgeNoise = V.Noise;
					C.AmJudgeLean = V.Lean;
					FBoxingFightCore Core;
					Core.Init(C);
					FFightBot Bot;
					Bot.Reset(EFightBotSkill::Average, C.Seed, 0);
					TArray<FFightBotCmd> Cmds;
					int32 Hits = 0;
					for (int32 Step = 0; Step < 60 * 60 * 30 && !Core.IsOver(); ++Step)
					{
						if (P.bBot)
						{
							Cmds.Reset();
							bool bHeld = false;
							EFightAction Held = EFightAction::StepBack;
							Bot.Think(Core.GetSnapshot(), 1.f / 60.f, Cmds, bHeld, Held);
							for (int32 I = 0; I < Cmds.Num(); ++I) Core.ApplyAction(0, Cmds[I].Action, Cmds[I].Target);
							if (bHeld) Core.ApplyAction(0, Held);
						}
						Core.Tick(1.f / 60.f);
						for (const FFightEvent& E : Core.PollEvents()) Hits += E.Kind == EFightEventKind::Hit ? (E.Attacker == 0 ? 1 : 1000) : 0;
					}
					const FFightResult& R = Core.GetResult();
					if (&V == &Vars[0])
					{
						BaseWin.Add(R.WinnerIndex);
						BaseHits.Add(Hits);
					}
					else
					{
						Flip += BaseWin[Idx] != R.WinnerIndex;
						Bad += BaseHits[Idx] != Hits; // поток боя обязан совпасть
					}
					if (R.Method != EFightMethod::Decision) continue;
					++Dec;
					int32 For = 0;
					for (int32 J = 0; J < R.NumJudges; ++J)
					{
						const FJudgeCard& Tc = R.JudgeTotals[J];
						const int32 Pick = Tc.Red > Tc.Blue ? 0 : (Tc.Blue > Tc.Red ? 1 : R.TieNominee[J]);
						For += Pick == R.WinnerIndex;
					}
					Una += For == R.NumJudges;
				}
				printf(" %3.0f%%", Dec ? 100.0 * Una / Dec : 0.0);
				DecAll += Dec;
				UnaAll += Una;
			}
			printf(" | всего %3.0f%% единогласных, победитель сменился в %.1f%% боёв\n", DecAll ? 100.0 * UnaAll / DecAll : 0.0, 100.0 * Flip / FMath::Max(1, Idx));
		}
		printf("  (пары по порядку:");
		for (const FPairRun& P : Pairs) printf(" %s;", P.Name);
		printf(") поток боя у вариантов = «сейчас»: %s\n", Bad ? "НЕТ" : "да");
		return Bad ? 1 : 0;
	}

	// S-76: клинч — инварианты. Бот average против прессинга/объёмника (там клинчей больше всего), N боёв: в сцепке удары не
	// стартуют, «Брейк!» — ровно один на клинч и после него пара расходится до CLINCH_SEP (1.35 м) без телепортов, клинч
	// кончается за ≤ 2.2 с; в автопилоте клинча нет; бой детерминирован.
	int32 ClinchCheck(int32 N)
	{
		int32 Clinches = 0, Breaks = 0, Ends = 0, PunchInClinch = 0, Viol = 0, Fights = 0;
		double MaxDur = 0, MaxStep = 0, MinSepAfter = 9, SumDur = 0;
		for (int32 StyleK = 0; StyleK < 2; ++StyleK)
		{
			FFighterSetup Me = SetupOf(GWebRef[0].A), Ai = SetupOf(GWebRef[0].A);
			Ai.Style = StyleK == 0 ? EBoxStyle::Pressure : EBoxStyle::Volume;
			for (int32 K = 0; K < N; ++K)
			{
				FFightConfig C;
				C.Seed = BotSeed(K);
				C.Rounds = 3;
				C.RoundSeconds = 55.f;
				C.BreakSeconds = 0.f;
				C.Fighters[0] = Me;
				C.Fighters[0].bAiControlled = false;
				C.Fighters[1] = Ai;
				C.Fighters[1].bAiControlled = true;
				FBoxingFightCore Core;
				Core.Init(C);
				FFightBot Bot;
				Bot.Reset(EFightBotSkill::Average, C.Seed, 0);
				TArray<FFightBotCmd> Cmds;
				bool bWas = false, bBroke = false;
				double Start = 0, X[2] = {0, 0}, Z[2] = {0, 0};
				bool bPunchWas[2] = {false, false};
				int32 SepCheck = 0;
				for (int32 Step = 0; Step < 60 * 60 * 60 && !Core.IsOver(); ++Step)
				{
					Cmds.Reset();
					bool bHeld = false;
					EFightAction Held = EFightAction::StepBack;
					const FFightSnapshot S0 = Core.GetSnapshot();
					Bot.Think(S0, 1.f / 60.f, Cmds, bHeld, Held);
					for (int32 I = 0; I < Cmds.Num(); ++I) Core.ApplyAction(0, Cmds[I].Action, Cmds[I].Target);
					if (bHeld) Core.ApplyAction(0, Held);
					Core.Tick(1.f / 60.f);
					const FFightSnapshot S = Core.GetSnapshot();
					for (const FFightEvent& E : Core.PollEvents())
					{
						if (E.Kind == EFightEventKind::Clinch) ++Clinches, bBroke = false;
						if (E.Kind == EFightEventKind::Break) { ++Breaks; Viol += bBroke ? 1 : 0; bBroke = true; }
					}
					if (S.bClinch)
					{
						if (!bWas) Start = Core.GetFightTime();
						for (int32 I = 0; I < 2; ++I)
						{
							// Новый удар в сцепке (старт = bPunching появился) — нарушение.
							if (S.Fighters[I].bPunching && !bPunchWas[I] && S.ClinchTime > 0.05f) ++PunchInClinch;
							if (bWas) MaxStep = FMath::Max(MaxStep, HypotD(S.Fighters[I].X - X[I], S.Fighters[I].Z - Z[I]));
						}
					}
					else if (bWas && S.Phase == EFightPhase::Fighting)
					{
						++Ends;
						const double Dur = Core.GetFightTime() - Start;
						MaxDur = FMath::Max(MaxDur, Dur);
						SumDur += Dur;
						SepCheck = 1;
					}
					if (SepCheck == 1 && !S.bClinch)
					{
						MinSepAfter = FMath::Min(MinSepAfter, double(S.Distance));
						SepCheck = 0;
					}
					bWas = S.bClinch && S.Phase == EFightPhase::Fighting;
					for (int32 I = 0; I < 2; ++I)
					{
						X[I] = S.Fighters[I].X;
						Z[I] = S.Fighters[I].Z;
						bPunchWas[I] = S.Fighters[I].bPunching;
					}
				}
				++Fights;
			}
		}
		// Автопилот: клинча нет (паритет с вебом).
		int32 AutoClinch = 0;
		for (uint32 Seed = 1; Seed <= 50; ++Seed)
		{
			FFightConfig C = MakeConfig(Seed, 55.f, 0.f, true, false);
			C.Fighters[1].Style = EBoxStyle::Pressure;
			C.Fighters[0].Style = EBoxStyle::Volume;
			FBoxingFightCore Core;
			Core.Init(C);
			for (int32 Step = 0; Step < 60 * 60 * 60 && !Core.IsOver(); ++Step)
			{
				Core.Tick(1.f / 60.f);
				for (const FFightEvent& E : Core.PollEvents()) AutoClinch += E.Kind == EFightEventKind::Clinch;
			}
		}
		const double Lim = (1.35 - 1.0) / 0.6 / 60.0 + 1e-4; // разведение: пара — (CLINCH_SEP − CLINCH_DIST) за CLINCH_SEP_S, на тик
		const bool bOk = Clinches > 0 && Ends <= Breaks && Breaks <= Clinches && PunchInClinch == 0 && Viol == 0 &&
			MaxDur <= 2.2 && MaxStep <= Lim && MinSepAfter >= 1.3 && AutoClinch == 0;
		printf("клинч (S-76): %d боёв (average против прессинга/объёмника): клинчей %d (%.2f за бой), «брейк» %d, разведено %d, средняя длина %.2f с, макс. %.2f с; "
			   "ударов в сцепке %d; макс. сдвиг за тик в клинче %.4f м; дистанция после разведения ≥ %.2f м; в автопилоте клинчей %d — %s\n",
			Fights, Clinches, double(Clinches) / FMath::Max(1, Fights), Breaks, Ends, Ends ? SumDur / Ends : 0.0, MaxDur, PunchInClinch, MaxStep, MinSepAfter,
			AutoClinch, bOk ? "OK" : "FAIL");
		return bOk ? 0 : 1;
	}

	int32 StyleTable(int32 N, bool bCheck, float RoundSec = 55.f)
	{
		static const char* StyleName[7] = {"technical", "volume", "puncher", "pressure", "counter", "speed", "balanced"};
		const EFightBotSkill Skills[] = {EFightBotSkill::Novice, EFightBotSkill::Average, EFightBotSkill::Strong, EFightBotSkill::Masher,
			EFightBotSkill::JabSpam, EFightBotSkill::Turtle, EFightBotSkill::Runner};
		constexpr int32 NumSkills = int32(sizeof(Skills) / sizeof(Skills[0]));
		const FRefProf& Even = GWebRef[0].A; // равные 70/70
		printf("S-76: бот × стиль ИИ, равные 70/70, 3 р. × %.0f с, %d боёв (доля побед бота)%s:\n", RoundSec, N,
			G_StyleNoClinch ? ", без клинча" : "");
		printf("  %-10s", "стиль");
		for (int32 K = 0; K < NumSkills; ++K) printf(" %8s", FFightBot::SkillName(Skills[K]));
		printf(" | почерк ИИ против average: уд/мин, силовых, корпус, серий≥3/бой, контр-попаданий/бой, дистанция, клинчей/бой | бегун у канатов, "
			   "дистанция | черепахе пробил блок/бой\n");
		int32 Bad = 0;
		for (int32 S = 0; S < 7; ++S)
		{
			FFighterSetup Me = SetupOf(Even);
			FFighterSetup Ai = SetupOf(Even);
			Ai.Style = static_cast<EBoxStyle>(S);
			double Rate[NumSkills];
			FStyleFeel Feel, RunFeel, TurtleFeel;
			printf("  %-10s", StyleName[S]);
			for (int32 K = 0; K < NumSkills; ++K)
			{
				FStyleFeel* F = Skills[K] == EFightBotSkill::Average ? &Feel
					: (Skills[K] == EFightBotSkill::Runner ? &RunFeel : (Skills[K] == EFightBotSkill::Turtle ? &TurtleFeel : nullptr));
				const FBotTally T = RunStyleSeries(Skills[K], Me, Ai, N, F, RoundSec);
				Rate[K] = T.Rate();
				printf(" %7.1f%%", Rate[K] * 100);
			}
			const double Min = Feel.Ticks / 60.0 / 60.0;
			printf(" | %4.1f  %3.0f%%  %3.0f%%  %4.1f  %4.1f  %.2f м  %3.1f | %3.0f%%  %.2f м | %4.1f\n", Feel.AiPunches / FMath::Max(1e-9, Min),
				100.0 * Feel.AiPower / FMath::Max(1, Feel.AiPunches), 100.0 * Feel.AiBody / FMath::Max(1, Feel.AiPunches),
				double(Feel.Series3) / FMath::Max(1, Feel.Fights), double(Feel.AiCounterHits) / FMath::Max(1, Feel.Fights),
				Feel.DistSum / FMath::Max(1.0, Feel.Ticks), double(Feel.Clinches) / FMath::Max(1, Feel.Fights),
				100.0 * RunFeel.HumanRope / FMath::Max(1.0, RunFeel.Ticks), RunFeel.DistSum / FMath::Max(1.0, RunFeel.Ticks),
				double(TurtleFeel.GuardBreaks) / FMath::Max(1, TurtleFeel.Fights));
			// Ориентир: average 40–55%, novice < average < strong, спам и абьюз ≤ 10%.
			const bool bOk = Rate[1] >= 0.40 && Rate[1] <= 0.55 && Rate[0] < Rate[1] && Rate[1] < Rate[2] && Rate[3] <= 0.10 && Rate[4] <= 0.10 &&
				Rate[5] <= 0.10 && Rate[6] <= 0.10;
			if (!bOk) ++Bad;
			if (!bOk && bCheck) printf("  !! %s вне ориентира\n", StyleName[S]);
		}
		if (!bCheck) return 0;
		printf("ИИ стилей против человека (S-76): average 40–55%% против каждого стиля, novice < average < strong, спам/абьюз ≤ 10%% — %s\n", Bad ? "MISMATCH" : "OK");
		return Bad ? 1 : 0;
	}

	// Досрочки профи по весу: топ-10 каждого дивизиона ростера (близкие соседи и перевес через одного/двух), ИИ против ИИ.
	struct FBand
	{
		const char* Name;
		bool bFemale;
		double Lo, Hi;
		double MinStop, MaxStop; // ориентир доли досрочек (реальная статистика титульных боёв профи)
		int32 Fights, Stops, Kd, Draws, Dec, Same, Rounds, SplitRounds, Kos;
		int32 CloseF, CloseS, GapF, GapS; // близкие (разница уровней < 3) и перевес (≥ 5)
	};
	int32 ProStoppageTable(int32 N, float RoundSec)
	{
		FBand Bands[] = {
			{"М тяж/крузер 90–100", false, 89, 101, 0.50, 0.78, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{"М полутяж–1-й ср. 70–79", false, 69, 80, 0.38, 0.62, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{"М полусредние 63–67", false, 62, 68, 0.32, 0.55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{"М лёгкие 55–61", false, 55, 62, 0.25, 0.48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{"М мухи 48–53", false, 47, 54, 0.15, 0.42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			{"Ж все веса", true, 40, 101, 0.08, 0.35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		};
		for (int32 K = 0; K < GProPairNum; ++K)
		{
			const FProPairRef& Pp = GProPairs[K];
			const FRosterRef& A = GRoster[Pp.A];
			const FRosterRef& B = GRoster[Pp.B];
			FBand* Band = nullptr;
			for (FBand& Bd : Bands)
				if (Bd.bFemale == A.bFemale && A.W >= Bd.Lo && A.W < Bd.Hi) Band = &Bd;
			if (!Band) continue;
			const FPairSetup P = PairOf(A, B, true);
			const double Gap = AbsD(A.ProOverall - B.ProOverall);
			for (int32 I = 0; I < N; ++I)
			{
				const bool bSwap = I % 2 == 1;
				FFightConfig C;
				C.Seed = RefSeed(K * 1000 + I);
				C.Rounds = Pp.Rounds;
				C.RoundSeconds = RoundSec;
				C.BreakSeconds = 0.f;
				C.bAllowDraw = true;
				C.bProRules = true;
				C.Fighters[bSwap ? 1 : 0] = P.R;
				C.Fighters[bSwap ? 0 : 1] = P.B;
				FBoxingFightCore Core;
				Core.Init(C);
				for (int32 S = 0; S < 60 * 60 * 60 && !Core.IsOver(); ++S)
				{
					Core.Tick(1.f / 60.f);
					Core.PollEvents();
				}
				const FFightResult& R = Core.GetResult();
				const bool bStop = R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC;
				++Band->Fights;
				Band->Stops += bStop;
				Band->Kos += R.Method == EFightMethod::KO;
				Band->Kd += R.Knockdowns[0] + R.Knockdowns[1];
				Band->Draws += R.WinnerIndex < 0;
				for (int32 Rr = 0; Rr < R.Rounds.Num(); ++Rr)
				{
					const FJudgeCard* Jc = R.Rounds[Rr].JudgeCards;
					++Band->Rounds;
					Band->SplitRounds += !(Jc[0].Red == Jc[1].Red && Jc[0].Blue == Jc[1].Blue && Jc[0].Red == Jc[2].Red && Jc[0].Blue == Jc[2].Blue);
				}
				if (!bStop)
				{
					++Band->Dec;
					Band->Same += R.JudgeTotals[0].Red == R.JudgeTotals[1].Red && R.JudgeTotals[0].Blue == R.JudgeTotals[1].Blue &&
						R.JudgeTotals[0].Red == R.JudgeTotals[2].Red && R.JudgeTotals[0].Blue == R.JudgeTotals[2].Blue;
				}
				if (Gap < 3) { ++Band->CloseF; Band->CloseS += bStop; }
				if (Gap >= 5) { ++Band->GapF; Band->GapS += bStop; }
			}
		}
		int32 Fails = 0;
		printf("досрочки профи по весу (топ-10 дивизионов ростера, ИИ против ИИ, %d боёв на пару, раунд %.0f с; ориентир — реальные титульные бои):\n", N,
			RoundSec);
		double PrevStop = 2;
		for (const FBand& Bd : Bands)
		{
			const double St = Bd.Fights ? double(Bd.Stops) / Bd.Fights : 0;
			const double Close = Bd.CloseF ? double(Bd.CloseS) / Bd.CloseF : 0;
			const double GapSt = Bd.GapF ? double(Bd.GapS) / Bd.GapF : 0;
			// Ориентир + у мужчин тяжелее — не реже (допуск шума 0.03) + перевес досрочит не реже близких (допуск шума 0.05).
			const bool bOk = St >= Bd.MinStop && St <= Bd.MaxStop && (Bd.bFemale || St <= PrevStop + 0.03) && (Bd.GapF < 20 || GapSt >= Close - 0.05);
			if (!Bd.bFemale) PrevStop = St;
			Fails += bOk ? 0 : 1;
			printf("  %s %-26s досрочек %3.0f%% (из них KO %.0f%%; ориентир %.0f–%.0f%%; близкие %.0f%% / перевес ≥5: %.0f%%), нокдаунов/бой %.2f, ничьих %.1f%%, три "
				   "одинаковые карты %.0f%% решений, раундов с разными картами %.0f%%, боёв %d\n",
				bOk ? "  " : "!!", Bd.Name, St * 100, Bd.Stops ? 100.0 * Bd.Kos / Bd.Stops : 0.0, Bd.MinStop * 100, Bd.MaxStop * 100, Close * 100, GapSt * 100,
				double(Bd.Kd) / FMath::Max(1, Bd.Fights), 100.0 * Bd.Draws / FMath::Max(1, Bd.Fights), Bd.Dec ? 100.0 * Bd.Same / Bd.Dec : 0.0,
				100.0 * Bd.SplitRounds / FMath::Max(1, Bd.Rounds), Bd.Fights);
		}
		printf("досрочки профи по весу: %s\n", Fails ? "MISMATCH" : "OK");

		// Бот «человека» против равного в профи (10 р., 75 кг): не безнадёжно и не тривиально, досрочки есть, спам не побеждает.
		const FRefProf& Even = GWebRef[8].A; // равные 75/75 (профиль эталона веба)
		FFighterSetup Me = SetupOf(Even), Ai = SetupOf(Even);
		// S-76: 240 боёв на уровень (было 80): ИИ стилей против человека сменил поток боя, и на 80 боях доля спама/среднего
		// гуляла ±10 п. (масштаб ДИ) — проверка ловила шум, а не баланс.
		const EFightBotSkill Skills[4] = {EFightBotSkill::Novice, EFightBotSkill::Average, EFightBotSkill::Strong, EFightBotSkill::Masher};
		FBotTally Tl[4];
		printf("бот «человека» против равного 75/75 в профи (10 р. × %.0f с, %d боёв):\n", RoundSec, N * 15);
		for (int32 K = 0; K < 4; ++K)
		{
			Tl[K] = RunBotSetups(Skills[K], Me, Ai, N * 15, RoundSec, 10, true);
			printf("  %-8s побед %3.0f%%  KO %d RSC %d решений %d (ничьих %d), проиграл досрочно %d, нокдаунов %d/%d, три одинаковые карты %.0f%% решений\n",
				FFightBot::SkillName(Skills[K]), Tl[K].Rate() * 100, Tl[K].Ko, Tl[K].Rsc, Tl[K].Decisions, Tl[K].Draws, Tl[K].LossStop, Tl[K].KdFor,
				Tl[K].KdAgainst, Tl[K].Decisions ? 100.0 * Tl[K].SameCards / Tl[K].Decisions : 0.0);
		}
		const FBotTally& Avg = Tl[1];
		const double AvgStop = double(Avg.Ko + Avg.Rsc) / FMath::Max(1, Avg.N);
		// Очки: победа 1, ничья 0.5 (в 10-раундовом профи-бою равных ничьих ~10–15% — у судьи 95-95 при 5 раундах на 5).
		const auto Pts = [](const FBotTally& X) { return (X.Wins + 0.5 * X.Draws) / FMath::Max(1, X.N); };
		const bool bBotOk = Pts(Avg) >= 0.35 && Pts(Avg) <= 0.72 && Pts(Tl[2]) > Pts(Avg) && Pts(Tl[0]) < Pts(Avg) && Tl[3].Rate() < 0.05 &&
			AvgStop >= 0.15 && AvgStop <= 0.65;
		printf("  средний: очков %.2f (побед %.2f, досрочек %.2f — ориентир 0.15–0.65); очков: новичок %.2f < средний < сильный %.2f; спам побед %.2f < 0.05 — %s\n",
			Pts(Avg), Avg.Rate(), AvgStop, Pts(Tl[0]), Pts(Tl[2]), Tl[3].Rate(), bBotOk ? "OK" : "MISMATCH");
		Fails += bBotOk ? 0 : 1;
		return Fails ? 1 : 0;
	}
}

int main(int Argc, char** Argv)
{
	// sim.exe bot <N> <раунд, с> — таблица бота «человека» (подробно).
	if (Argc >= 2 && Argv[1][0] == 'b')
	{
		int32 N = 0, Sec = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		if (Argc >= 4) for (const char* C = Argv[3]; *C >= '0' && *C <= '9'; ++C) Sec = Sec * 10 + (*C - '0');
		return CheckBots(N > 0 ? N : 200, Sec > 0 ? float(Sec) : 55.f, true);
	}
	// S-61: sim.exe qa <N> <раунд, с> — пары плейтеста QA; sim.exe pro <N> <раунд, с> — досрочки профи по весу.
	if (Argc >= 2 && (Argv[1][0] == 'q' || Argv[1][0] == 'p'))
	{
		int32 N = 0, Sec = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		if (Argc >= 4) for (const char* C = Argv[3]; *C >= '0' && *C <= '9'; ++C) Sec = Sec * 10 + (*C - '0');
		const float Rs = Sec > 0 ? float(Sec) : 55.f;
		G_StyleNoClinch = Argc >= 5 && Argv[4][0] == 'n';
		return Argv[1][0] == 'q' ? QaTable(N > 0 ? N : 50, Rs) : ProStoppageTable(N > 0 ? N : 20, Rs);
	}
	// S-76: sim.exe clinch <N> — инварианты клинча.
	if (Argc >= 2 && Argv[1][0] == 'c')
	{
		int32 N = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		return ClinchCheck(N > 0 ? N : 150);
	}
	// S-76: sim.exe styles <N> [раунд, с] [noclinch] — бот × стиль ИИ (равные 70/70) + почерк стиля.
	if (Argc >= 2 && Argv[1][0] == 's')
	{
		int32 N = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		int32 Sec = 0;
		for (int32 A = 3; A < Argc; ++A)
		{
			if (Argv[A][0] == 'n') G_StyleNoClinch = true;
			for (const char* C = Argv[A]; *C >= '0' && *C <= '9'; ++C) Sec = Sec * 10 + (*C - '0');
		}
		return StyleTable(N > 0 ? N : 200, true, Sec > 0 ? float(Sec) : 55.f);
	}
	// S-76: sim.exe vjudges <N> — вариант судейства любителей (не включён): доля единогласных при разных порогах/разбросе/вкусе.
	if (Argc >= 2 && Argv[1][0] == 'v')
	{
		int32 N = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		return JudgeVariantTable(N > 0 ? N : 200);
	}
	// S-65: sim.exe judges <N> — судейство любителей по World Boxing на парах QA.
	if (Argc >= 2 && Argv[1][0] == 'j')
	{
		int32 N = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		return AmateurJudgingTable(N > 0 ? N : 200, true);
	}
	// S-65: sim.exe you <N> — «шансы при твоей игре» против факта бот-серий на парах QA.
	if (Argc >= 2 && Argv[1][0] == 'y')
	{
		int32 N = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		return PlayerForecastTable(N > 0 ? N : 200, true);
	}
	// S-73: sim.exe audit <N> [пара] — сверка карточки «при твоей игре» с бот-сериями (пара — индекс GQa, по умолчанию все).
	if (Argc >= 2 && Argv[1][0] == 'a')
	{
		int32 N = 0, Only = -1;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		if (Argc >= 4) { Only = 0; for (const char* C = Argv[3]; *C >= '0' && *C <= '9'; ++C) Only = Only * 10 + (*C - '0'); }
		return ForecastAudit(N > 0 ? N : 1000, Only);
	}
	// sim.exe webref <N> <раунд, с> — только сверка с эталоном веба (подробно).
	if (Argc >= 2 && Argv[1][0] == 'w')
	{
		int32 N = 0, Sec = 0;
		if (Argc >= 3) for (const char* C = Argv[2]; *C >= '0' && *C <= '9'; ++C) N = N * 10 + (*C - '0');
		if (Argc >= 4) for (const char* C = Argv[3]; *C >= '0' && *C <= '9'; ++C) Sec = Sec * 10 + (*C - '0');
		return CheckWebRef(N > 0 ? N : 400, Sec > 0 ? float(Sec) : 55.f, true);
	}
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

	// --- 6. Доля побед/досрочек/нокдаунов vs эталон веба (S-57): 14 пар interactive-parity (+2 профи), раунд 55 и 180 с ---
	// Профи-пары здесь — без bProRules: это сверка ЗЕРКАЛА веба (у веба профи-правил нет); профи-правила — проверка 9.
	Fails += CheckWebRef(400, 55.f, false);
	Fails += CheckWebRef(400, 180.f, false);
	// --- 7. Бот «человека» против ИИ (S-57) ---
	Fails += CheckBots(200, 55.f, true);

	// --- 8. Проекция веса (S-61): BoxingFightProfile::ProjectToWeight == fightProfile веба; кэтчвейт как App.tsx ---
	{
		struct FProjCase { const char* Name; double Target; double St[7]; double Mass, Dur; };
		// vite-node: Boxer.fightProfile(90, true) Головкина (легенда, 72 кг) и Тайсона Фьюри (100 кг); сгонка 81→75 — WebRef.inc.
		const FProjCase Cases[] = {
			{"Геннадий Головкин", 90, {95.5, 85.5, 86.5, 78.416, 82.355, 85, 86}, 66.6, 65.7},
			{"Тайсон Фьюри", 90, {86.7, 81.8, 90.7, 67.76, 73.744, 92, 90}, 95.5, 95},
		};
		const auto Same = [](const char* X, const char* Y) { while (*X && *X == *Y) { ++X; ++Y; } return *X == *Y; };
		int32 Bad = 0;
		for (const FProjCase& Pc : Cases)
		{
			const FRosterRef* Who = nullptr;
			for (const FRosterRef& R : GRoster)
				if (Same(R.Name, Pc.Name)) Who = &R;
			if (!Who) { ++Bad; continue; }
			const FFighterSetup S = BoxingFightProfile::ProjectToWeight(RosterSetup(*Who, true), Pc.Target);
			const float X[7] = {S.Stats.Power, S.Stats.HandSpeed, S.Stats.Footwork, S.Stats.Stamina, S.Stats.Chin, S.Stats.Technique, S.Stats.Defense};
			for (int32 K = 0; K < 7; ++K) Bad += AbsD(X[K] - Pc.St[K]) > 1e-3;
			Bad += AbsD(S.MassForPower - Pc.Mass) > 1e-3 || AbsD(S.DurabilityMass - Pc.Dur) > 1e-3;
		}
		FFighterSetup Cut;
		Cut.Stats = {74.4f, 74, 74.2f, 74, 74, 74, 74};
		Cut.WeightKg = 81;
		const FFighterSetup C2 = BoxingFightProfile::ProjectToWeight(Cut, 75);
		Bad += AbsD(C2.Stats.Stamina - 65.12) > 1e-3 || AbsD(C2.Stats.Chin - 68.672) > 1e-3 || AbsD(C2.MassForPower - 78.3) > 1e-3 || AbsD(C2.DurabilityMass - 78) > 1e-3;
		const int32 NumM = int32(sizeof(GProClassesM) / sizeof(double));
		const int32 NumAm = int32(sizeof(BoxingFightProfile::AMATEUR_CLASSES_M) / sizeof(double));
		Bad += BoxingFightProfile::Catchweight(72, 100, GProClassesM, NumM) != 90;                         // Головкин–Фьюри
		Bad += BoxingFightProfile::Catchweight(92, 54, BoxingFightProfile::AMATEUR_CLASSES_M, NumAm) != 75; // Гадфа–Вейтия
		Bad += BoxingFightProfile::Catchweight(50, 54, BoxingFightProfile::AMATEUR_CLASSES_M, NumAm) != 50; // равенство — меньшая
		printf("проекция веса vs fightProfile веба (Головкин/Фьюри → 90 кг, сгонка 81→75, кэтчвейт): %s\n", Bad ? "MISMATCH" : "OK");
		Fails += Bad ? 1 : 0;
	}
	// --- 9. Профи-правила (S-61): доля досрочек по весу на парах ростера + бот против равного в профи ---
	Fails += ProStoppageTable(16, 55.f);

	// --- 10. S-65: судейство любителей по World Boxing (5 судей, без 10-10, подпись = факт карт) и «шансы при твоей игре» ---
	Fails += AmateurJudgingTable(100, true);
	Fails += PlayerForecastTable(120, true);

	// --- 11. S-76: ИИ стилей против человека — бот × стиль (average 40–55% против каждого, порядок уровней, спам/абьюз ≤ 10%) ---
	Fails += StyleTable(300, true);
	Fails += ClinchCheck(150);
	Fails += JudgeVariantTable(40); // вариант судейства (выкл.): поток боя у вариантов бой-в-бой как «сейчас»

	Fails += CheckGeometry();
	Fails += G_Fail ? 1 : 0;
	printf("ИТОГ: %s\n", Fails ? "FAIL" : "OK");
	return Fails ? 1 : 0;
}
