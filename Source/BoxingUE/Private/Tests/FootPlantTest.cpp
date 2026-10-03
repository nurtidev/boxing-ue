// Автотест S-70: планировщик ступней BoxFoot (порт web/test/footPlant.test.ts) — опора стоит в мире, шаг — перенос одной
// ступни, боксёрский порядок ног, подтяг второй, пивот на подушечке, ходьба попеременно, выпад (поглощение/шаг в пик),
// предсказание шага ядра, «две ступни на шаг» без дроби.
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.FootPlant;Quit"
#include "FootPlant.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace BoxFoot;

	// Стойка правши в осях корпуса (X — к сопернику, Y — вправо, UE): передняя (левая) впереди-слева, задняя — сзади-справа.
	struct FSt
	{
		float X, Y, Yaw;
	};
	const FSt STANCE[2] = {{0.3f, -0.22f, 0.9f}, {-0.26f, 0.09f, 1.2f}};
	constexpr float TOE = 0.125f;
	constexpr float DT = 1.f / 60.f;

	struct FPose
	{
		float X = 0.f, Y = 0.f, Yaw = 0.f;
	};

	void GoalsAt(const FPose& P, FGoal Out[2])
	{
		const float C = FMath::Cos(P.Yaw), S = FMath::Sin(P.Yaw);
		for (int32 I = 0; I < 2; ++I)
		{
			Out[I].X = P.X + STANCE[I].X * C - STANCE[I].Y * S;
			Out[I].Y = P.Y + STANCE[I].X * S + STANCE[I].Y * C;
			Out[I].Yaw = P.Yaw + STANCE[I].Yaw;
		}
	}

	struct FRun
	{
		TArray<TArray<FFootNow>> Frames;
		int32 FirstStepper = -1;
	};

	FGaitOpts Opts(float VX, float VY, bool bWalking)
	{
		FGaitOpts O;
		O.Dt = DT;
		O.VX = VX;
		O.VY = VY;
		O.bWalking = bWalking;
		O.Stretch[0] = O.Stretch[1] = 0.9f;
		O.ToeLen = TOE;
		O.Lead = bWalking ? 0.2f : 0.09f;
		return O;
	}

	// Прогнать корпус по траектории Path(t) с шагом DT.
	// bEngine — как в игре (FBoxerFootIk): идёт шаг ядра (корпус движется) → TrackStep, ступни встают в конец шага.
	FRun Run(TFunctionRef<FPose(float)> Path, float T, FGait& G, bool bWalking = false, bool bEngine = false)
	{
		FRun R;
		FPose Prev = Path(0.f);
		FGoal Goals[2];
		GoalsAt(Prev, Goals);
		UpdateGait(G, Goals, Opts(0.f, 0.f, bWalking));
		FStepTrack Tr;
		for (float t = DT; t <= T + 1e-6f; t += DT)
		{
			const FPose P = Path(t);
			const float VX = (P.X - Prev.X) / DT, VY = (P.Y - Prev.Y) / DT;
			const FV2 Ahead = TrackStep(Tr, bEngine && FMath::Abs(VX) + FMath::Abs(VY) > 1e-4f ? 1 : 0, P.X - Prev.X, P.Y - Prev.Y, DT);
			Prev = P;
			GoalsAt(P, Goals);
			FGaitOpts O = Opts(VX, VY, bWalking);
			if (bEngine && InStepRhythm(Tr))
			{
				O.bAhead = true;
				O.Ahead = Ahead;
				O.Lead = 0.f;
			}
			UpdateGait(G, Goals, O);
			TArray<FFootNow> F = {FootNow(G.Feet[0]), FootNow(G.Feet[1])};
			if (R.FirstStepper < 0)
			{
				R.FirstStepper = F[0].U >= 0.f ? 0 : (F[1].U >= 0.f ? 1 : -1);
			}
			R.Frames.Add(F);
		}
		return R;
	}

	// Шаг ядра: 0.2 м за 0.24 с в направлении (DX, DY), затем стоит.
	FPose EngineStep(float DX, float DY, float t)
	{
		const float K = FMath::Min(1.f, t / 0.24f) * 0.2f;
		return FPose{DX * K, DY * K, 0.f};
	}

	// Путь опоры: ступня стоит (не в переносе) в соседних кадрах.
	float PlantedSlide(const FRun& R)
	{
		float S = 0.f;
		for (int32 I = 1; I < R.Frames.Num(); ++I)
		{
			for (int32 K = 0; K < 2; ++K)
			{
				const FFootNow& A = R.Frames[I - 1][K];
				const FFootNow& B = R.Frames[I][K];
				if (A.U < 0.f && B.U < 0.f)
				{
					S += FMath::Sqrt(FMath::Square(B.X - A.X) + FMath::Square(B.Y - A.Y));
				}
			}
		}
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootPlantTest, "BoxingUE.FootPlant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFootPlantTest::RunTest(const FString& Parameters)
{
	// --- стоит — ноги стоят ---
	{
		FGait G;
		const FRun R = Run([](float) { return FPose(); }, 2.f, G);
		bool bAny = false;
		for (const auto& F : R.Frames)
		{
			bAny |= F[0].U >= 0.f || F[1].U >= 0.f;
		}
		TestFalse(TEXT("стоит: ни одного шага"), bAny);
		TestEqual(TEXT("стоит: опора не сдвигается"), PlantedSlide(R), 0.f);
	}

	// --- шаг корпуса: опора не скользит, обе ступни доходят до стойки; боксёрский порядок ---
	{
		const float Dirs[4][2] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1}};
		const int32 First[4] = {0, 1, 0, 1}; // вперёд — передняя, назад — задняя, влево (−Y) — левая (передняя), вправо — задняя
		for (int32 D = 0; D < 4; ++D)
		{
			FGait G;
			const float DX = Dirs[D][0], DY = Dirs[D][1];
			const FRun R = Run([DX, DY](float t) { return EngineStep(DX, DY, t); }, 1.2f, G, false, true);
			TestEqual(*FString::Printf(TEXT("шаг %d: опора не скользит"), D), PlantedSlide(R), 0.f);
			FGoal End[2];
			GoalsAt(FPose{DX * 0.2f, DY * 0.2f, 0.f}, End);
			for (int32 I = 0; I < 2; ++I)
			{
				const float E = FMath::Sqrt(FMath::Square(G.Feet[I].X - End[I].X) + FMath::Square(G.Feet[I].Y - End[I].Y));
				TestTrue(*FString::Printf(TEXT("шаг %d: ступня %d в стойке (%.3f м)"), D, I, E), E < 0.03f);
			}
			TestEqual(*FString::Printf(TEXT("шаг %d: первой шагает ступня по ходу"), D), R.FirstStepper, First[D]);
			// шаг-приставка: шагнули обе, вторая — волоком
			bool bSw[2] = {false, false};
			for (const auto& F : R.Frames)
			{
				bSw[0] |= F[0].U >= 0.f;
				bSw[1] |= F[1].U >= 0.f;
			}
			TestTrue(*FString::Printf(TEXT("шаг %d: вторая подтянулась"), D), bSw[0] && bSw[1]);
			TestTrue(*FString::Printf(TEXT("шаг %d: два переноса, не дробь (%d)"), D, G.Swings), G.Swings == 2);
		}
	}

	// --- челнок шагов: никогда обе ступни в воздухе ---
	{
		FGait G;
		const FRun R = Run([](float t)
		{
			const int32 N = FMath::FloorToInt(t / 0.34f);
			const float K = FMath::Min(1.f, (t - N * 0.34f) / 0.24f);
			return FPose{(N + K) * 0.2f, 0.f, 0.f};
		}, 2.f, G, false, true);
		int32 Both = 0;
		for (const auto& F : R.Frames)
		{
			Both += (F[0].U >= 0.f && F[0].Lift > 0.012f && F[1].U >= 0.f && F[1].Lift > 0.012f) ? 1 : 0;
		}
		TestEqual(TEXT("челнок: обе ступни в воздухе — ни разу"), Both, 0);
		TestEqual(TEXT("челнок: опора не скользит"), PlantedSlide(R), 0.f);
		// 6 шагов ядра за 2 с — переносов не больше 2 на шаг (веб: «ровно две ступни на шаг»)
		TestTrue(*FString::Printf(TEXT("челнок: переносов %d ≤ 2 × шагов"), G.Swings), G.Swings <= 2 * 6);
	}

	// --- пивот вокруг подушечки передней: малый разворот — носок на месте, передняя не шагает ---
	{
		FGait G;
		Run([](float) { return FPose(); }, 0.2f, G);
		auto Toe = [&G](int32 K) { return FVector2D(G.Feet[K].X + FMath::Cos(G.Feet[K].Yaw) * TOE, G.Feet[K].Y + FMath::Sin(G.Feet[K].Yaw) * TOE); };
		const FVector2D T0 = Toe(0);
		const FRun R = Run([T0](float t)
		{
			const float Yaw = FMath::Min(1.f, t / 0.22f) * 0.3f;
			const float C = FMath::Cos(Yaw), S = FMath::Sin(Yaw);
			const float H = Yaw + STANCE[0].Yaw;
			const float AX = T0.X - FMath::Cos(H) * TOE, AY = T0.Y - FMath::Sin(H) * TOE;
			return FPose{AX - (STANCE[0].X * C - STANCE[0].Y * S), AY - (STANCE[0].X * S + STANCE[0].Y * C), Yaw};
		}, 0.8f, G);
		bool bLeadStep = false, bRearStep = false;
		for (const auto& F : R.Frames)
		{
			bLeadStep |= F[0].U >= 0.f;
			bRearStep |= F[1].U >= 0.f;
		}
		TestFalse(TEXT("пивот: передняя не шагала"), bLeadStep);
		TestTrue(TEXT("пивот: задняя переступила"), bRearStep);
		TestTrue(TEXT("пивот: курс передней довернулся"), FMath::Abs(G.Feet[0].Yaw - (0.3f + STANCE[0].Yaw)) < 0.05f);
		TestTrue(TEXT("пивот: носок передней на месте"), FVector2D::Distance(Toe(0), T0) < 0.01f);
	}

	// --- ходьба: попеременно, одна ступня всегда на опоре ---
	{
		FGait G;
		const FRun R = Run([](float t) { return FPose{t * 1.8f, 0.f, 0.f}; }, 2.f, G, true);
		int32 Switches = 0, Last = -1, Air2 = 0;
		for (const auto& F : R.Frames)
		{
			const int32 Air = (F[0].U >= 0.f && F[0].U < 0.94f ? 1 : 0) + (F[1].U >= 0.f && F[1].U < 0.94f ? 1 : 0);
			Air2 += Air >= 2 ? 1 : 0;
			const int32 K = F[0].U >= 0.f && F[0].U < 0.1f ? 0 : (F[1].U >= 0.f && F[1].U < 0.1f ? 1 : -1);
			if (K >= 0 && K != Last)
			{
				++Switches;
				Last = K;
			}
		}
		TestEqual(TEXT("ходьба: обе в воздухе — ни разу"), Air2, 0);
		TestTrue(*FString::Printf(TEXT("ходьба: ≥ 5 чередующихся шагов за 2 с (%d)"), Switches), Switches > 4);
	}

	// --- телепорт — встать в стойку без шага ---
	{
		FGait G;
		Run([](float) { return FPose(); }, 0.2f, G);
		Run([](float) { return FPose{3.f, 1.f, 0.f}; }, DT, G);
		FGoal Goals[2];
		GoalsAt(FPose{3.f, 1.f, 0.f}, Goals);
		for (int32 I = 0; I < 2; ++I)
		{
			TestFalse(TEXT("телепорт: без переноса"), G.Feet[I].bSwing);
			TestTrue(TEXT("телепорт: ступня в стойке"), FMath::Abs(G.Feet[I].X - Goals[I].X) + FMath::Abs(G.Feet[I].Y - Goals[I].Y) < 1e-4f);
		}
	}

	// --- выпад: упреждение без перелёта (≤ 10 см за точкой стойки) ---
	{
		FGait G;
		Run([](float) { return FPose(); }, 0.2f, G);
		float MaxOver = 0.f;
		for (float t = DT; t <= 0.12f + 1e-6f; t += DT)
		{
			FGoal Goals[2];
			GoalsAt(FPose{4.f * t, 0.f, 0.f}, Goals);
			UpdateGait(G, Goals, Opts(0.f, 0.f, false));
			if (G.Feet[0].bSwing)
			{
				MaxOver = FMath::Max(MaxOver, G.Feet[0].X - Goals[0].X);
			}
		}
		TestTrue(*FString::Printf(TEXT("выпад: перелёт %.3f ≤ 0.1 м"), MaxOver), MaxOver <= 0.1f + 1e-5f);
	}

	// --- поглощение выпада и удержание шага в пике ---
	{
		const FV2 In = OverAbsorb(0.1f, 0.05f, LEAD_ABSORB);
		TestTrue(TEXT("выпад до порога — коленом (шага нет)"), In.Len() < 1e-6f);
		FV2 St, Sl;
		SplitLunge(0, 0.35f, 0.f, 1.f, St, Sl);
		TestTrue(TEXT("передняя: вперёд сверх порога — шаг"), FMath::IsNearlyEqual(St.X, 0.35f - LEAD_ABSORB.Fwd, 1e-4f) && Sl.Len() < 1e-6f);
		SplitLunge(1, 0.35f, 0.f, 1.f, St, Sl);
		TestTrue(TEXT("задняя: вперёд — скольжение до SLIDE_MAX, остаток шагом"),
			FMath::IsNearlyEqual(Sl.X, FMath::Min(0.35f - REAR_ABSORB.Fwd, SLIDE_MAX), 1e-4f) && St.X >= 0.f);
		FLungeHold H;
		HoldLunge(H, FV2(0.2f, 0.f));
		HoldLunge(H, FV2(0.15f, 0.f));
		TestTrue(TEXT("удержание: спад выше половины пика — держит пик"), FMath::IsNearlyEqual(H.X, 0.2f, 1e-5f));
		HoldLunge(H, FV2(0.08f, 0.f));
		TestTrue(TEXT("удержание: спад ниже половины — разом в 0"), FMath::Abs(H.X) < 1e-6f);
		HoldLunge(H, FV2(0.06f, 0.f));
		TestTrue(TEXT("удержание: остаток спада не даёт нового шажка"), FMath::Abs(H.X) < 1e-6f);
		HoldLunge(H, FV2(0.25f, 0.f));
		TestTrue(TEXT("удержание: новый выпад выше пика — снова шаг"), FMath::IsNearlyEqual(H.X, 0.25f, 1e-5f));
	}

	// --- предсказание шага ядра: длительность учится, остаток — скорость × оставшееся время ---
	{
		FStepTrack Tr;
		const float V = 0.2f / 0.24f;
		for (int32 Rep = 0; Rep < 3; ++Rep)
		{
			for (float t = 0.f; t < 0.24f - 1e-4f; t += DT)
			{
				TrackStep(Tr, 1, V * DT, 0.f, DT);
			}
			for (float t = 0.f; t < 0.1f; t += DT)
			{
				TrackStep(Tr, 0, 0.f, 0.f, DT);
			}
		}
		TestTrue(*FString::Printf(TEXT("шаг ядра: длительность выучена (%.3f)"), Tr.Durs[1]), FMath::Abs(Tr.Durs[1] - 0.24f) < 0.03f);
		const FV2 A = TrackStep(Tr, 1, V * DT, 0.f, DT);
		TestTrue(*FString::Printf(TEXT("шаг ядра: с первого кадра — остаток вперёд (%.3f)"), A.X), A.X > 0.12f && A.X < 0.22f && FMath::Abs(A.Y) < 1e-3f);
		TestTrue(TEXT("шаг ядра: ритм шагов"), InStepRhythm(Tr));
	}

	// --- форма переноса ---
	{
		float D1, L1, D2, L2;
		SwingShape(0.2f, false, D1, L1);
		SwingShape(0.7f, true, D2, L2);
		TestTrue(TEXT("перенос: в бою короче и ниже, чем в ходьбе"), D1 < D2 && L1 < L2 && L1 <= 0.05f);
		FGoal Goals[2];
		GoalsAt(FPose(), Goals);
		const float Errs[2] = {0.f, 0.f};
		TestEqual(TEXT("ведёт по ходу: вперёд — передняя"), LeaderOf(Goals, 1.f, 0.f, Errs), 0);
		TestEqual(TEXT("ведёт по ходу: назад — задняя"), LeaderOf(Goals, -1.f, 0.f, Errs), 1);
	}
	return true;
}

#endif
