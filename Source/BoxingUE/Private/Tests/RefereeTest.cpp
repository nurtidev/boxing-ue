// Автотест логики рефери (S-58) — зеркало web/test/referee.test.ts + refereeRest.test.ts: место сбоку от пары
// с дальней от камеры стороны, обход пары через торец, ограничение хода, стадии (центр на выходе, нейтральный угол
// в перерыве, счёт у сбитого вне пути стоящего, досрочка, рука победителю) и живой бой ядра (автопилот).
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosound -ExecCmds="Automation RunTests BoxingUE.Referee;Quit"
#include "RefereeBrain.h"
#include "BoxerFall.h"
#include "BoxingFightCore.h"
#include "FightStaging.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace BoxRef;

	bool InRing(const FV& P) { return FMath::Abs(P.X) <= REF_RING + 1e-9 && FMath::Abs(P.Z) <= REF_RING + 1e-9; }

	// Камера боя: за спиной игрока (1.5 м) и вправо от него (3.05 м) — как в сцене.
	FV FightCam(const FV& F0, const FV& F1)
	{
		double L = Hyp(F0, F1);
		if (L == 0) L = 1;
		const double Ux = (F1.X - F0.X) / L, Uz = (F1.Z - F0.Z) / L;
		const double Mx = (F0.X + F1.X) / 2, Mz = (F0.Z + F1.Z) / 2;
		return FV(Mx - Ux * 1.5 - Uz * 3.05, Mz - Uz * 1.5 + Ux * 3.05);
	}

	FV Corner(int32 I)
	{
		const BoxingStaging::FRingPoint P = BoxingStaging::CornerOf(I);
		return FV(P.X, P.Z);
	}
	FV Neutral(int32 K)
	{
		const BoxingStaging::FRingPoint P = BoxingStaging::NeutralCorner(K);
		return FV(P.X, P.Z);
	}

	// Точка на ломаной по доле длины U (alongPath веба).
	FV AlongPath(const TArray<FV>& R, double U)
	{
		double Len = 0;
		for (int32 I = 1; I < R.Num(); ++I) Len += Hyp(R[I - 1], R[I]);
		double Want = FMath::Clamp(U, 0.0, 1.0) * Len;
		for (int32 I = 1; I < R.Num(); ++I)
		{
			const double S = Hyp(R[I - 1], R[I]);
			if (Want <= S || I == R.Num() - 1)
			{
				const double K = S > 0 ? FMath::Min(1.0, Want / S) : 1;
				return FV(R[I - 1].X + (R[I].X - R[I - 1].X) * K, R[I - 1].Z + (R[I].Z - R[I - 1].Z) * K);
			}
			Want -= S;
		}
		return R.Last();
	}

	FInput Mk(EPhase Ph, const FV& F0, const FV& F1, const FV& Cam)
	{
		FInput In;
		In.Phase = Ph;
		In.Fighters[0] = F0;
		In.Fighters[1] = F1;
		In.Camera = Cam;
		return In;
	}

	FRefFrame Run(FBrain& B, const TFunction<FInput(double)>& Inp, double Sec)
	{
		FRefFrame F = B.Update(Inp(0), 0);
		for (double T = 0; T < Sec; T += 1.0 / 60) F = B.Update(Inp(T), 1.0 / 60);
		return F;
	}

	FFightConfig CoreConfig(uint32 Seed, bool bKdPair)
	{
		FFightConfig C;
		C.Seed = Seed;
		C.Rounds = 3;
		C.RoundSeconds = 40;
		C.BreakSeconds = 3;
		C.bCorners = true;
		FFighterSetup& A = C.Fighters[0];
		FFighterSetup& Bf = C.Fighters[1];
		if (!bKdPair)
		{
			A.Stats = {78, 80, 76, 74, 75, 82, 79};
			A.ReachCm = 185; A.WeightKg = 71; A.Style = EBoxStyle::Technical;
			Bf.Stats = {84, 74, 70, 78, 77, 74, 72};
			Bf.ReachCm = 180; Bf.WeightKg = 71; Bf.Style = EBoxStyle::Pressure;
		}
		else
		{
			A.Stats = {92, 76, 72, 76, 74, 74, 70};
			A.ReachCm = 183; A.WeightKg = 75; A.Style = EBoxStyle::Puncher;
			Bf.Stats = {72, 74, 72, 70, 58, 72, 68};
			Bf.ReachCm = 180; Bf.WeightKg = 75; Bf.Style = EBoxStyle::Volume;
		}
		A.bAiControlled = true;
		Bf.bAiControlled = true;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxRefereeTest, "BoxingUE.Referee", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxRefereeTest::RunTest(const FString& Parameters)
{
	const FV P0(-0.6, 0), P1(0.6, 0);
	const FV Cam(0, 4.6);

	// ---------- место в бою ----------
	{
		const FV Cams[3] = {FV(0.35, 4.6), FV(-1.5, 3.05), FV(2, -4)};
		for (const FV& C : Cams)
		{
			const FV P = SidePlacement(P0, P1, C, nullptr);
			const double Side = Hyp(P, ClosestOnSeg(P0, P1, P));
			TestTrue(TEXT("сбоку: не ближе SIDE_MIN"), Side >= SIDE_MIN - 1e-6);
			TestTrue(TEXT("сбоку: не дальше SIDE_MAX"), Side <= SIDE_MAX + 1e-6);
			TestTrue(TEXT("с дальней от камеры стороны"), FMath::Sign(P.Z) == -FMath::Sign(C.Z));
			TestTrue(TEXT("не у бойцов"), FMath::Min(Hyp(P, P0), Hyp(P, P1)) >= FIGHTER_CLEAR);
			TestFalse(TEXT("не заслоняет"), BlocksView(P, C, P0) || BlocksView(P, C, P1));
			TestTrue(TEXT("внутри канатов"), InRing(P));
		}
		const FV Q0(-0.6, -2.3), Q1(0.6, -2.3), Qc(0, 2.5);
		const FV P = SidePlacement(Q0, Q1, Qc, nullptr);
		TestTrue(TEXT("пара у канатов: внутри ринга"), InRing(P));
		TestTrue(TEXT("пара у канатов: не у бойцов"), FMath::Min(Hyp(P, Q0), Hyp(P, Q1)) >= FIGHTER_CLEAR);
		TestFalse(TEXT("пара у канатов: не заслоняет"), BlocksView(P, Qc, Q0) || BlocksView(P, Qc, Q1));
	}
	{
		FBrain B;
		B.Place(SidePlacement(P0, P1, Cam, nullptr), 0);
		const FV Start = B.Pos;
		for (int32 I = 0; I < 120; ++I)
		{
			const double J = FMath::Sin(I * 0.3) * 0.12;
			B.Update(Mk(EPhase::Fight, FV(P0.X + J, P0.Z), FV(P1.X + J, P1.Z), Cam), 1.0 / 60);
		}
		TestTrue(TEXT("цель держится (не переминается)"), Hyp(B.Pos, Start) < 0.3);
	}

	{
		// Перерыв: угол — по камере БОЯ (в перерыве камера у угла игрока), следующий раунд — с дальней стороны.
		FBrain B;
		FInput In = Mk(EPhase::Rest, Corner(0), Corner(1), FV(-2.6, -3.4));
		In.bHasFightCam = true;
		In.FightCam = FV(-1.5, 3.05);
		Run(B, [&](double) { return In; }, 5);
		TestTrue(TEXT("перерыв: нейтральный угол дальний от камеры боя"), Hyp(B.Pos, RestSpot(In.FightCam)) < 0.1 && B.Pos.Z < 0);
	}

	// ---------- ход ----------
	{
		FBrain B;
		B.Place(SidePlacement(P0, P1, FV(0, 4.6), nullptr), 0);
		double MinF = 1e9, MinGap = 1e9;
		bool bRing = true;
		for (int32 I = 0; I < 400; ++I)
		{
			B.Update(Mk(EPhase::Fight, P0, P1, FV(0, -4.6)), 1.0 / 60);
			MinF = FMath::Min(MinF, FMath::Min(Hyp(B.Pos, P0), Hyp(B.Pos, P1)));
			MinGap = FMath::Min(MinGap, Hyp(B.Pos, ClosestOnSeg(P0, P1, B.Pos)));
			bRing &= InRing(B.Pos);
		}
		TestTrue(TEXT("камера перешла: рефери на дальней стороне"), B.Pos.Z > 1);
		TestTrue(TEXT("камера перешла: не сквозь бойцов (> 0.7 м)"), MinF > 0.7);
		TestTrue(TEXT("камера перешла: не через просвет пары (> 0.5 м)"), MinGap > 0.5);
		TestTrue(TEXT("камера перешла: внутри канатов"), bRing);
	}
	{
		FBrain B;
		B.Place(FV(0, 0), 0);
		FV Prev = B.Pos;
		double PrevV = 0, MaxV = 0, MaxA = 0;
		for (int32 I = 0; I < 200; ++I)
		{
			B.Update(Mk(EPhase::Rest, Corner(0), Corner(1), Cam), 1.0 / 60);
			const double V = Hyp(B.Pos, Prev) * 60;
			MaxV = FMath::Max(MaxV, V);
			MaxA = FMath::Max(MaxA, FMath::Abs(V - PrevV) * 60);
			Prev = B.Pos;
			PrevV = V;
		}
		TestTrue(FString::Printf(TEXT("плавно: скорость < 2 м/с (%.2f)"), MaxV), MaxV < 2.0);
		TestTrue(FString::Printf(TEXT("плавно: ускорение < 6.5 м/с² (%.2f)"), MaxA), MaxA < 6.5);
	}
	{
		const double Dts[3] = {1.0 / 30, 1.0 / 60, 1.0 / 120};
		const FV Cams[4] = {FV(0, 6), FV(-3, 4), FV(0, 6), FV(0.35, 4.6)};
		const double Zoffs[4] = {0, 0, 0.3, -0.2};
		for (const double Dt : Dts)
		{
			for (int32 C = 0; C < 4; ++C)
			{
				FBrain B;
				bool bPrev = false;
				FV Prev;
				double MaxStep = 0, MinF = 1e9;
				const int32 N = FMath::RoundToInt(2.5 / Dt);
				for (int32 I = 0; I < N; ++I)
				{
					const double K = FMath::Min(1.0, (I * Dt) / 1.5);
					const FV F0(-2.3 + 1.7 * K, -2.3 * (1 - K) + Zoffs[C]);
					const FV F1(2.3 - 1.7 * K, 2.3 * (1 - K) - Zoffs[C]);
					const FRefFrame Fr = B.Update(Mk(I * Dt < 2 ? EPhase::Out : EPhase::Fight, F0, F1, Cams[C]), Dt);
					const FV Now(Fr.X, Fr.Z);
					if (bPrev) MaxStep = FMath::Max(MaxStep, Hyp(Now, Prev));
					Prev = Now;
					bPrev = true;
					MinF = FMath::Min(MinF, FMath::Min(Hyp(Now, F0), Hyp(Now, F1)));
				}
				TestTrue(TEXT("центр → в сторону: шаг за кадр ≤ MAX_SPEED·dt"), MaxStep <= 1.9 * Dt + 1e-9);
				TestTrue(TEXT("центр → в сторону: заранее, не задев сходящихся (> 0.75 м)"), MinF > 0.75);
				TestTrue(TEXT("центр → в сторону: ушёл из центра (> 1 м)"), Hyp(Prev, FV(0, 0)) > 1.0);
			}
		}
	}
	{
		FBrain B;
		B.Place(FV(0.75, -0.35), 0);
		FV Prev = B.Pos;
		double MaxStep = 0;
		for (int32 I = 0; I < 180; ++I)
		{
			FInput In = Mk(EPhase::Down, P0, P1, Cam);
			In.Down.bValid = true;
			In.Down.Who = 1;
			In.Down.Count = 1 + I / 45;
			In.Down.bHasNeutral = true;
			In.Down.Neutral = Neutral(0);
			B.Update(In, 1.0 / 60);
			MaxStep = FMath::Max(MaxStep, Hyp(B.Pos, Prev));
			Prev = B.Pos;
		}
		TestTrue(TEXT("смена режима с телом на рефери — выходит шагом"), MaxStep <= 1.9 / 60 + 1e-9);
	}

	// ---------- обход пары (блокер спринта 4) ----------
	{
		const FV Cams[3] = {FV(0, 4.6), FV(1, 4), FV(-1.5, 3.5)};
		const double Sxs[5] = {-0.4, -0.2, 0, 0.2, 0.4};
		const double Szs[3] = {0.66, 0.85, 1.0};
		int32 Bad = 0;
		for (const FV& C : Cams)
			for (const double Sx : Sxs)
				for (const double Sz : Szs)
				{
					FBrain B;
					B.Place(FV(Sx, Sz), 0);
					for (int32 I = 0; I < 6 * 60; ++I) B.Update(Mk(EPhase::Fight, P0, P1, C), 1.0 / 60);
					const FV Goal = SidePlacement(P0, P1, C, nullptr);
					if (FMath::Sign(B.Pos.Z) != -FMath::Sign(C.Z) || Hyp(B.Pos, Goal) >= 0.6) ++Bad;
				}
		TestEqual(TEXT("обход пары: цель за парой — дошёл через торец (застрявших)"), Bad, 0);
		FV DetP;
		int32 End = -1;
		TestTrue(TEXT("обход: точка есть"), PairDetour(FV(0, 0.8), FV(0, -1.5), P0, P1, -1, DetP, End));
		TestTrue(TEXT("обход: за торцом, вне тел"), FMath::Abs(DetP.X) > 0.6 + 0.8);
		TestFalse(TEXT("путь не режет пару — обхода нет"), PairDetour(FV(2, 0.8), FV(2, -1.5), P0, P1, -1, DetP, End));
	}

	// ---------- стадии ----------
	{
		FBrain B;
		B.Place(FV(0, 0), 0);
		Run(B, [&](double) { return Mk(EPhase::Out, Corner(0), Corner(1), Cam); }, 1);
		TestTrue(TEXT("выход из углов: пара далеко — в центре"), Hyp(B.Pos, FV(0, 0)) < 0.05);
		Run(B, [&](double) { return Mk(EPhase::Out, P0, P1, Cam); }, 3);
		TestTrue(TEXT("выход из углов: сошлись — в стороне"), Hyp(B.Pos, FV(0, 0)) > 1.1);
	}
	{
		FBrain B;
		Run(B, [&](double) { return Mk(EPhase::Rest, Corner(0), Corner(1), Cam); }, 5);
		const FV Spot = RestSpot(Cam);
		TestTrue(TEXT("перерыв: в нейтральном углу"), Hyp(B.Pos, Spot) < 0.1);
		const FV Far = Hyp(Neutral(0), Cam) > Hyp(Neutral(1), Cam) ? Neutral(0) : Neutral(1);
		TestTrue(TEXT("перерыв: в дальнем от камеры"), Hyp(Spot, Far) < 0.6);
	}
	{
		// Нокдаун: у сбитого со стороны нейтрального угла, рука счёта в такт.
		FBrain B;
		const FV Down(0.6, 0);
		const FV Nt = Neutral(0);
		B.Place(FV(0.3, -1.5), 0);
		TArray<FV> Path = {FV(-0.6, -1.6), Nt};
		TArray<FV> RouteP = {FV(-0.6, 0), FV(-0.6, -1.6), Nt};
		auto Inp = [&](double T)
		{
			FInput In = Mk(EPhase::Down, AlongPath(RouteP, FMath::Min(1.0, T / 2)), Down, Cam);
			In.Down.bValid = true;
			In.Down.Who = 1;
			In.Down.Count = 1 + FMath::FloorToInt(T / 0.75);
			In.Down.bHasNeutral = true;
			In.Down.Neutral = Nt;
			In.Down.Path = Path;
			In.Down.bArrived = T > 2;
			return In;
		};
		Run(B, Inp, 3.0);
		TestTrue(TEXT("счёт: у сбитого (0.6..1.4 м)"), Hyp(B.Pos, Down) > 0.6 && Hyp(B.Pos, Down) < 1.4);
		TestTrue(TEXT("счёт: со стороны нейтрального угла"), Hyp(B.Pos, Nt) < Hyp(Down, Nt));
		int32 LowN = 0, HighN = 0, LowBad = 0, HighBad = 0;
		double T = 3.0;
		for (int32 K = 0; K < 4 * 45; ++K, T += 1.0 / 60)
		{
			const FRefFrame F = B.Update(Inp(T), 1.0 / 60);
			const double Ph = FMath::Fmod(T, 0.75) / 0.75;
			const double Y = FMath::Max(F.Arms[0].Upper.Y, F.Arms[1].Upper.Y);
			if (FMath::Abs(Ph - 0.08) < 0.012) { ++LowN; LowBad += Y >= 0.1; }
			if (FMath::Abs(Ph - 0.7) < 0.012) { ++HighN; HighBad += Y <= 0.5; }
		}
		TestTrue(TEXT("такт: замеры есть"), LowN > 2 && HighN > 2);
		TestEqual(TEXT("такт: на счёт рука внизу"), LowBad, 0);
		TestEqual(TEXT("такт: к середине — над головой"), HighBad, 0);
	}
	for (int32 Nk = 0; Nk < 2; ++Nk)
	{
		// Считает одной и той же рукой весь счёт (угол с любой стороны).
		FBrain B;
		const FV Down(0.6, 0);
		B.Place(FV(0.3, -1.5), 0);
		auto Inp = [&](double T)
		{
			FInput In = Mk(EPhase::Down, FV(-0.6, 0), Down, Cam);
			In.Down.bValid = true;
			In.Down.Who = 1;
			In.Down.Count = 1 + FMath::FloorToInt(T / 0.75);
			In.Down.bHasNeutral = true;
			In.Down.Neutral = Neutral(Nk);
			return In;
		};
		B.Update(Inp(0), 0);
		TArray<TPair<double, double>> Ys;
		for (int32 I = 0; I < 8 * 45; ++I)
		{
			const FRefFrame F = B.Update(Inp(I / 60.0), 1.0 / 60);
			Ys.Add({F.Arms[0].Upper.Y, F.Arms[1].Upper.Y});
		}
		TSet<int32> Arms;
		bool bSwing = true;
		for (int32 K = 1; K < 8; ++K)
		{
			const auto& Lo = Ys[FMath::RoundToInt((K * 0.75 + 0.06) * 60)];
			const auto& Hi = Ys[FMath::RoundToInt((K * 0.75 + 0.5) * 60)];
			const double D0 = Hi.Key - Lo.Key, D1 = Hi.Value - Lo.Value;
			bSwing &= FMath::Max(D0, D1) > 0.4;
			Arms.Add(D0 > D1 ? 0 : 1);
		}
		TestTrue(TEXT("счёт: взмах на каждый такт"), bSwing);
		TestEqual(TEXT("счёт: одна и та же рука"), Arms.Num(), 1);
	}
	{
		// Стоящий идёт в угол со скоростью 4 м/с сквозь точку счёта — до рефери не меньше 0.55 м.
		int32 Cases = 0;
		double Worst = 1e9;
		bool bRing = true;
		const FV Offs[3] = {FV(0, 0), FV(0.8, -0.6), FV(-0.7, 0.5)};
		const FV CamPs[2] = {FV(0.35, 4.6), FV(3.5, 3.5)};
		for (int32 A = 0; A < 12; ++A)
		{
			const double Th = (A / 12.0) * UE_DOUBLE_PI * 2;
			for (const FV& Off : Offs)
				for (const FV& CamP : CamPs)
				{
					const double Ux = FMath::Cos(Th), Uz = FMath::Sin(Th);
					const FV Stand0(Off.X - Ux * 0.56, Off.Z - Uz * 0.56);
					const FV Down(Off.X + Ux * 0.56, Off.Z + Uz * 0.56);
					const BoxingStaging::FRingPoint Cp = BoxingStaging::NeutralFor({Down.X, Down.Z}, {Stand0.X, Stand0.Z});
					const FV Cr(Cp.X, Cp.Z);
					const TArray<FV> RouteP = {Stand0, Cr};
					if (RouteDist(RouteP, CountSpot(Down, Cr, CamP)) > 0.35) continue;
					++Cases;
					const double Len = Hyp(Stand0, Cr);
					FBrain B;
					B.Update(Mk(EPhase::Fight, Stand0, Down, CamP), 0);
					for (double T = 0; T < 3; T += 1.0 / 30) B.Update(Mk(EPhase::Fight, Stand0, Down, CamP), 1.0 / 30);
					double Min = 1e9;
					for (double T = 0; T < 4; T += 1.0 / 30)
					{
						const double U = FMath::Min(1.0, (FMath::Max(0.0, T - 0.28) * 4) / Len);
						const FV St = AlongPath(RouteP, U);
						FInput In = Mk(EPhase::Down, St, Down, CamP);
						In.Down.bValid = true;
						In.Down.Who = 1;
						In.Down.Count = 1 + FMath::FloorToInt(T / 0.55);
						In.Down.bHasNeutral = true;
						In.Down.Neutral = Cr;
						In.Down.bArrived = U >= 1;
						B.Update(In, 1.0 / 30);
						Min = FMath::Min(Min, Hyp(B.Pos, St));
						bRing &= InRing(B.Pos);
					}
					Worst = FMath::Min(Worst, Min);
				}
		}
		AddInfo(FString::Printf(TEXT("4 м/с сквозь точку счёта: раскладок %d, мин. до стоящего %.2f м"), Cases, Worst));
		TestTrue(TEXT("4 м/с: раскладки есть"), Cases > 3);
		TestTrue(FString::Printf(TEXT("4 м/с: до стоящего ≥ 0.55 м (%.2f)"), Worst), Worst >= 0.55);
		TestTrue(TEXT("4 м/с: внутри канатов"), bRing);
	}
	{
		// Досрочка: сперва видит падение (STOP_DELAY), потом скрещивает и разводит руки.
		FBrain B;
		B.Place(FV(0, -1.5), 0);
		FInput D = Mk(EPhase::Down, P0, P1, Cam);
		D.Down.bValid = true;
		D.Down.Who = 1;
		D.Down.Count = 9;
		B.Update(D, 1.0 / 60);
		FInput O = Mk(EPhase::Over, P0, P1, Cam);
		O.Over.bValid = true;
		O.Over.Winner = 0;
		O.Over.bStoppage = true;
		double MinX = 9, MaxX = -9, MaxEarly = -9;
		for (int32 I = 0; I < 150; ++I)
		{
			const FRefFrame F = B.Update(O, 1.0 / 60);
			if (I < 30) MaxEarly = FMath::Max(MaxEarly, F.Arms[0].Upper.X);
			MinX = FMath::Min(MinX, F.Arms[0].Upper.X);
			MaxX = FMath::Max(MaxX, F.Arms[0].Upper.X);
		}
		TestTrue(TEXT("досрочка: скрестил (левая за середину)"), MinX < 0);
		TestTrue(TEXT("досрочка: развёл в сторону"), MaxX > 0.7);
		TestTrue(TEXT("досрочка: не сразу (сперва падение)"), MaxEarly < 0.5);
	}
	for (int32 W = -1; W <= 1; ++W)
	{
		// Решение: между бойцами, рука победителю (ничья — обе).
		FBrain B;
		B.Place(FV(1, -1.5), 0);
		FInput O = Mk(EPhase::Over, P0, P1, Cam);
		O.Over.bValid = true;
		O.Over.Winner = W;
		const FRefFrame F = Run(B, [&](double) { return O; }, 4);
		TestTrue(TEXT("решение: между бойцами"), FMath::Abs(B.Pos.X) < 0.15);
		TestTrue(TEXT("решение: за линией плеч от камеры"), B.Pos.Z < 0);
		const bool U0 = F.Arms[0].Upper.Y > 0.7, U1 = F.Arms[1].Upper.Y > 0.7;
		if (W < 0)
		{
			TestTrue(TEXT("ничья — обе руки"), U0 && U1);
		}
		else
		{
			const FV& Wp = O.Fighters[W];
			double Lat, Fwd;
			ToLocal(Wp.X - B.Pos.X, Wp.Z - B.Pos.Z, F.Yaw, Lat, Fwd);
			TestTrue(TEXT("рука — со стороны победителя"), Lat > 0 ? (U0 && !U1) : (!U0 && U1));
		}
	}
	for (int32 W = 0; W < 2; ++W)
	{
		// S-45: по пути руку не поднимает; встал — держит обоих внизу, потом поднимает руку победителя.
		FBrain B;
		const FV A0(-0.6, 0.2), A1(0.6, -0.2);
		B.Place(FV(1.6, -2.2), 0);
		FInput O = Mk(EPhase::Over, A0, A1, Cam);
		O.Over.bValid = true;
		O.Over.Winner = W;
		FRefFrame F = B.Update(O, 0);
		double FirstRaise = -1, MaxSp = 0;
		FV Prev = B.Pos;
		for (double T = 0; T < 6; T += 1.0 / 60)
		{
			F = B.Update(O, 1.0 / 60);
			const double Sp = Hyp(Prev, B.Pos) * 60;
			Prev = B.Pos;
			if (F.Arms[0].Fore.Y > 0.5 || F.Arms[1].Fore.Y > 0.5)
			{
				if (FirstRaise < 0) FirstRaise = T;
				MaxSp = FMath::Max(MaxSp, Sp);
			}
		}
		TestTrue(TEXT("S-45: не на ходу (сперва дошёл)"), FirstRaise > 1);
		TestTrue(TEXT("S-45: поднимал стоя"), MaxSp < 0.4);
		TestTrue(TEXT("S-45: держит итог"), F.Raised > 2);
		const FV& Wp = O.Fighters[W];
		const FV& Lp = O.Fighters[1 - W];
		double LatW, LatL, Fw;
		ToLocal(Wp.X - B.Pos.X, Wp.Z - B.Pos.Z, F.Yaw, LatW, Fw);
		ToLocal(Lp.X - B.Pos.X, Lp.Z - B.Pos.Z, F.Yaw, LatL, Fw);
		const bool bWLeft = LatW > LatL;
		const FArm& Hi = F.Arms[bWLeft ? 0 : 1];
		const FArm& Lo = F.Arms[bWLeft ? 1 : 0];
		TestTrue(TEXT("S-45: рука победителя вверху"), Hi.Fore.Y > 0.7);
		TestTrue(TEXT("S-45: к победителю"), FMath::Sign(Hi.Fore.X) == (bWLeft ? 1 : -1));
		TestTrue(TEXT("S-45: вторая внизу к проигравшему"), Lo.Fore.Y < -0.3 && FMath::Sign(Lo.Fore.X) == (bWLeft ? -1 : 1));
		TestTrue(TEXT("S-45: не в бойцах"), FMath::Min(Hyp(B.Pos, Wp), Hyp(B.Pos, Lp)) > 0.55);
		TestTrue(TEXT("S-45: между ними"), Hyp(B.Pos, FV(0, 0)) < 0.7);
	}
	{
		// Остановка на ногах (S-32): развёл руками, затем объявляет победителя; лежащего — нет.
		FBrain B;
		B.Place(FV(0, -1.5), 0);
		FInput O = Mk(EPhase::Over, P0, P1, Cam);
		O.Over.bValid = true;
		O.Over.Winner = 0;
		O.Over.bStoppage = true;
		O.Over.bStanding = true;
		B.Update(O, 0);
		bool bWaved = false, bRaisedEarly = false;
		for (double T = 0; T < 2; T += 1.0 / 60)
		{
			const FRefFrame F = B.Update(O, 1.0 / 60);
			bWaved |= F.Arms[0].Upper.X > 0.7;
			bRaisedEarly |= F.Raised > 0;
		}
		TestTrue(TEXT("стоящего остановили: развёл руками"), bWaved);
		TestFalse(TEXT("стоящего остановили: не сразу объявляет"), bRaisedEarly);
		const FRefFrame F = Run(B, [&](double) { return O; }, 5);
		TestTrue(TEXT("стоящего остановили: потом объявляет"), F.Raised > 0.5);
		FBrain B2;
		B2.Place(FV(0, -1.5), 0);
		O.Over.bStanding = false;
		const FRefFrame F2 = Run(B2, [&](double) { return O; }, 7);
		TestEqual(TEXT("лежащего не объявляет"), F2.Raised, 0.0);
	}
	{
		// Детерминирован.
		auto Go = [&]()
		{
			FBrain B;
			double H = 0;
			for (int32 I = 0; I < 300; ++I)
			{
				const double A = I * 0.02;
				const FRefFrame F = B.Update(Mk(EPhase::Fight, FV(-0.6 * FMath::Cos(A), -0.6 * FMath::Sin(A)), FV(0.6 * FMath::Cos(A), 0.6 * FMath::Sin(A)), Cam), 1.0 / 60);
				H = H * 1.000001 + F.X * 3 + F.Z * 7 + F.Yaw + F.Arms[0].Upper.Y;
			}
			return H;
		};
		TestEqual(TEXT("детерминирован"), Go(), Go());
	}
	{
		TestEqual(TEXT("такт: на счёт внизу"), CountLift(0, 0.75), 0.0);
		TestEqual(TEXT("такт: к середине вверху"), CountLift(0.5, 0.75), 1.0);
		TestTrue(TEXT("такт: опускается к следующему"), CountLift(0.74, 0.75) < 0.2);
		TestEqual(TEXT("такт: затянулся — внизу"), CountLift(2, 0.75), 0.0);
	}

	// ---------- живой бой ядра (автопилот, 3 раунда, углы): сбоку, не заслоняет, не входит в бойцов ----------
	for (int32 Kd = 0; Kd < 2; ++Kd)
	{
		int32 FightFrames = 0, SideOkN = 0, Blocked = 0, Downs = 0;
		double MinF = 1e9;
		bool bRing = true, bOut = false, bFight = false, bOver = false;
		for (uint32 Seed = 1; Seed <= (Kd ? 6u : 3u); ++Seed)
		{
			FBoxingFightCore Core;
			Core.Init(CoreConfig(Kd ? Seed * 7 + 3 : Seed * 11 + 7, Kd == 1));
			FBrain B;
			const double Dt = 0.05;
			int32 OverTicks = 0;
			for (int32 I = 0; I < 20000 && OverTicks < 200; ++I)
			{
				Core.Tick(static_cast<float>(Dt));
				Core.PollEvents();
				const FFightSnapshot S = Core.GetSnapshot();
				if (S.Phase == EFightPhase::Over) ++OverTicks;
				const FV At[2] = {FV(S.Fighters[0].X, S.Fighters[0].Z), FV(S.Fighters[1].X, S.Fighters[1].Z)};
				const FV C = FightCam(At[0], At[1]);
				const FFightResult* R = Core.IsOver() ? &Core.GetResult() : nullptr;
				const bool bStanding = R && R->WinnerIndex >= 0 && !S.Fighters[1 - R->WinnerIndex].bDown;
				const FInput In = InputFromSnapshot(S, R, At, C, bStanding);
				bOut |= In.Phase == EPhase::Out;
				bFight |= In.Phase == EPhase::Fight;
				bOver |= In.Phase == EPhase::Over;
				Downs += In.Phase == EPhase::Down;
				B.Update(In, Dt);
				bRing &= InRing(B.Pos);
				if (In.Phase == EPhase::Fight && S.Phase == EFightPhase::Fighting)
				{
					++FightFrames;
					const double Sd = Hyp(B.Pos, ClosestOnSeg(At[0], At[1], B.Pos));
					SideOkN += Sd >= SIDE_MIN - 0.15 && Sd <= SIDE_MAX + 0.15;
					Blocked += BlocksView(B.Pos, C, At[0]) || BlocksView(B.Pos, C, At[1]);
				}
				for (int32 F = 0; F < 2; ++F)
				{
					if (!S.Fighters[F].bDown) MinF = FMath::Min(MinF, Hyp(B.Pos, At[F]));
				}
			}
		}
		const double SideK = FightFrames ? double(SideOkN) / FightFrames : 0;
		const double BlockK = FightFrames ? double(Blocked) / FightFrames : 1;
		AddInfo(FString::Printf(TEXT("живой бой%s: кадров боя %d, сбоку %.3f, заслон %.3f, мин. до бойца %.2f м, кадров счёта %d"),
			Kd ? TEXT(" (нокдауны)") : TEXT(""), FightFrames, SideK, BlockK, MinF, Downs));
		TestTrue(TEXT("живой бой: стадии out/fight/over"), bOut && bFight && bOver);
		TestTrue(TEXT("живой бой: внутри канатов"), bRing);
		TestTrue(FString::Printf(TEXT("живой бой: не входит в стоящих (> 0.55 м, %.2f)"), MinF), MinF > 0.55);
		TestTrue(FString::Printf(TEXT("живой бой: сбоку на 1.2–2 м > 85%% (%.3f)"), SideK), SideK > 0.85);
		TestTrue(FString::Printf(TEXT("живой бой: заслон < 2%% (%.3f)"), BlockK), BlockK < 0.02);
		if (Kd)
		{
			TestTrue(TEXT("живой бой: нокдауны были (счёт проверен)"), Downs > 0);
		}
	}
	{
		// S-66 (QA UE-3, Головкин — Фьюри, сид 11): нокаут — сбитый не встаёт, итог досрочкой. Тело лежащего —
		// длинная капсула «стопы → таз → голова» + кисти/колени (раскладка клипа нокдауна, см. BoxerFall); рефери
		// на счёте, на разводе рук и потом над лежащим — НЕ на теле (было: стоял ногой между бёдрами, 18 см) и не
		// переминается на стыке кругов обхода. Перебор: точка падения × курс × откуда идёт рефери.
		struct FLay
		{
			FVector2D Feet, Pelvis, Head;
			TArray<FVector2D> Limbs;
		};
		// Раскладка AM_Knockdown (оси бойца, см: X вперёд, Y вправо; голова в 152 см за точкой, ×1.02 облика).
		const FLay Lay{FVector2D(12, 2), FVector2D(-97, 18), FVector2D(-155, 6),
			{FVector2D(-125, -48), FVector2D(-118, 52), FVector2D(-45, -16), FVector2D(-42, 20)}};
		auto Body = [&Lay](const FV& Pt, double YawDeg, FInput& In)
		{
			const FVector2D P(Pt.X * 100, Pt.Z * 100);
			auto W = [&](const FVector2D& L)
			{
				const FVector2D V = BoxerFall::ToWorld(L, P, static_cast<float>(YawDeg));
				return FV(V.X / 100, V.Y / 100);
			};
			In.bHasLying = true;
			In.LyingHead = W(Lay.Head);
			In.LyingPelvis = W(Lay.Pelvis);
			In.bHasLyingFeet = true;
			In.LyingFeet = W(Lay.Feet);
			for (const FVector2D& L : Lay.Limbs) In.LyingLimbs.Add(W(L));
		};
		// Ось рефери → тело (до оси капсулы / кисти-колена), по самому телу — без радиусов обхода.
		auto AxisGap = [](const FInput& In, const FV& P)
		{
			double D = FMath::Min(Hyp(P, ClosestOnSeg(In.LyingFeet, In.LyingPelvis, P)), Hyp(P, ClosestOnSeg(In.LyingPelvis, In.LyingHead, P)));
			for (const FV& L : In.LyingLimbs) D = FMath::Min(D, Hyp(P, L) + 0.15); // кисть/колено тоньше корпуса: 25 см до неё ≈ 40 см до оси
			return D;
		};
		struct FCase
		{
			FV Down, Stand, RefFrom, Cam;
			double YawDeg;
		};
		TArray<FCase> Cases;
		// Ровно случай QA: точка (−1.98, 0.84), курс −106° + доворот −25°, рефери подходит от (−2.38, 2.08), камера нокдауна.
		Cases.Add({FV(-1.98, 0.84), FV(-1.2, 0.35), FV(-2.38, 2.08), FV(-3.89, 2.71), -131});
		const FV Pts[5] = {FV(0, 0), FV(-1.9, 0.9), FV(1.2, -1.4), FV(1.6, 1.5), FV(-0.8, -1.9)};
		const FV Froms[3] = {FV(0, 2), FV(2, -0.5), FV(-1.5, -1.5)};
		for (const FV& Pt : Pts)
			for (int32 A = 0; A < 12; ++A)
				for (const FV& Fr : Froms)
				{
					const double Yaw = A * 30.0 - 180;
					const double R = FMath::DegreesToRadians(Yaw);
					// Стоящий — перед падающим (тот падает назад от удара), камера — сбоку от линии.
					const FV St(FMath::Clamp(Pt.X + FMath::Cos(R) * 0.9, -2.7, 2.7), FMath::Clamp(Pt.Z + FMath::Sin(R) * 0.9, -2.7, 2.7));
					const FV Cm(FMath::Clamp(Pt.X - FMath::Sin(R) * 3.0, -4.2, 4.2), FMath::Clamp(Pt.Z + FMath::Cos(R) * 3.0, -4.2, 4.2));
					// Как в сцене: тело ложится внутри канатов (BoxerFall: доворот и сдвиг точки).
					BoxerFall::FLayout L;
					L.Pts = {Lay.Feet, Lay.Pelvis, Lay.Head};
					L.Pts.Append(Lay.Limbs);
					L.bValid = true;
					BoxerFall::FPlaceIn Pi;
					Pi.Pos = FVector2D(Pt.X * 100, Pt.Z * 100);
					Pi.YawDeg = static_cast<float>(Yaw);
					Pi.Layout = &L;
					const BoxerFall::FPlaceOut Po = BoxerFall::Solve(Pi);
					const FV Dp(Pt.X + Po.Offset.X / 100, Pt.Z + Po.Offset.Y / 100);
					Cases.Add({Dp, St, Fr, Cm, Yaw + Po.TurnDeg});
				}
		double WorstSettled = 1e9, WorstQa = 1e9, WorstJitter = 0, QaJitter = 0;
		int32 OnBody = 0, Frames = 0, Bad = 0;
		bool bRing = true;
		for (int32 Ci = 0; Ci < Cases.Num(); ++Ci)
		{
			const FCase& C = Cases[Ci];
			FBrain B;
			B.Place(C.RefFrom, 0);
			const BoxingStaging::FRingPoint Np = BoxingStaging::NeutralFor({C.Down.X, C.Down.Z}, {C.Stand.X, C.Stand.Z});
			const FV Nt(Np.X, Np.Z);
			const TArray<FV> RouteP = {C.Stand, Nt};
			double Min = 1e9, Jit = 0;
			FV Prev = B.Pos;
			FV At7 = B.Pos;
			const double Dt = 1.0 / 30;
			for (double T = 0; T < 9; T += Dt)
			{
				// 0..5 с — счёт (стоящий идёт в угол 1.8 м/с), дальше — нокаут: итог досрочкой, лежит.
				const bool bOver = T >= 5;
				const double U = FMath::Min(1.0, FMath::Max(0.0, T - 0.3) * 1.8 / FMath::Max(0.1, Hyp(C.Stand, Nt)));
				const FV St = AlongPath(RouteP, U);
				FInput In = Mk(bOver ? EPhase::Over : EPhase::Down, C.Down, St, C.Cam);
				if (!bOver)
				{
					In.Down.bValid = true;
					In.Down.Who = 0;
					In.Down.Count = 1 + FMath::FloorToInt(T / 0.5);
					In.Down.bHasNeutral = true;
					In.Down.Neutral = Nt;
					In.Down.bArrived = U >= 1;
				}
				else
				{
					In.Over.bValid = true;
					In.Over.Winner = 1;
					In.Over.bStoppage = true;
				}
				Body(C.Down, C.YawDeg, In);
				B.Update(In, Dt);
				bRing &= InRing(B.Pos);
				const double G = AxisGap(In, B.Pos);
				// Тело лежит (клип: таз на настиле с 0.8 с) и рефери успел сойти с пути падения — 1.5 с.
				if (T >= 1.5)
				{
					Min = FMath::Min(Min, G);
					++Frames;
					OnBody += G < 0.3 ? 1 : 0;
				}
				// Над лежащим стоит, а не переминается: путь за последние 2 с сверх чистого смещения (дошёл позже — не в счёт).
				if (T >= 7) Jit += Hyp(B.Pos, Prev);
				else At7 = B.Pos;
				Prev = B.Pos;
			}
			Jit -= Hyp(B.Pos, At7);
			if (Ci == 0)
			{
				WorstQa = Min;
				QaJitter = Jit;
			}
			if ((Min < 0.4 || Jit > 0.3) && Bad++ < 10)
			{
				AddInfo(FString::Printf(TEXT("  плохо: точка (%.2f, %.2f) курс %.0f, стоящий (%.2f, %.2f), рефери от (%.2f, %.2f), камера (%.2f, %.2f) → до тела %.2f, ход %.2f, конец (%.2f, %.2f)"),
					C.Down.X, C.Down.Z, C.YawDeg, C.Stand.X, C.Stand.Z, C.RefFrom.X, C.RefFrom.Z, C.Cam.X, C.Cam.Z, Min, Jit, B.Pos.X, B.Pos.Z));
			}
			WorstSettled = FMath::Min(WorstSettled, Min);
			WorstJitter = FMath::Max(WorstJitter, Jit);
		}
		AddInfo(FString::Printf(TEXT("S-66 нокаут: раскладок %d, случай QA — до тела %.2f м, ход за 2 с %.2f м; худший — до тела %.2f м, ход %.2f м; кадров «на теле» %d из %d"),
			Cases.Num(), WorstQa, QaJitter, WorstSettled, WorstJitter, OnBody, Frames));
		TestTrue(FString::Printf(TEXT("S-66 случай QA: не на теле (≥ 0.5 м, %.2f)"), WorstQa), WorstQa >= 0.5);
		TestTrue(FString::Printf(TEXT("S-66 случай QA: стоит, не переминается (%.2f м за 2 с)"), QaJitter), QaJitter < 0.15);
		TestTrue(FString::Printf(TEXT("S-66 нокаут: до тела ≥ 0.4 м во всех раскладках (%.2f)"), WorstSettled), WorstSettled >= 0.4);
		TestTrue(TEXT("S-66 нокаут: ни кадра на теле"), OnBody == 0);
		TestTrue(FString::Printf(TEXT("S-66 нокаут: не переминается (%.2f м за 2 с)"), WorstJitter), WorstJitter < 0.3);
		TestTrue(TEXT("S-66 нокаут: внутри канатов"), bRing);
	}
	return true;
}

#endif
