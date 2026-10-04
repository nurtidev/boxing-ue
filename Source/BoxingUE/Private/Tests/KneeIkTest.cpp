// Автотест S-74: колено в IK ног не выворачивается (баг владельца: «когда идёшь назад, колено разворачивается в другую
// сторону»). Плоскость сгиба — BoxerFeel::KneeBendDir: колено клипа + курс ступни, колено клипа «назад от носка» не в
// счёт, итог не дальше 35° от носка. Проверки: (1) случайные позы — колено всегда над носком (±35°), обычные позы (клип
// и так над носком) не меняются; (2) серии шагов планировщика BoxFoot во всех 8 направлениях, правша и левша, с клипом
// «ходьбы GASP» (нога клипа заносится по ходу, колено клипа — вперёд по корпусу) — ни одного кадра колена назад/внутрь.
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.KneeIk;Quit"
#include "BoxerFeel.h"
#include "FootPlant.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float L1 = 45.f, L2 = 45.f; // бедро, голень (см)
	constexpr float MAX_DEV_DEG = 35.f;

	// Колено двухзвенника по закону косинусов в плоскости N (как SolveTwoBoneIK с полюсом по N).
	FVector KneeAt(const FVector& Hip, const FVector& Goal, const FVector& N)
	{
		const FVector D = (Goal - Hip).GetSafeNormal();
		const float Dist = FMath::Clamp(static_cast<float>(FVector::Dist(Hip, Goal)), FMath::Abs(L1 - L2) + 0.01f, (L1 + L2) * 0.999f);
		const float Ax = (L1 * L1 + Dist * Dist - L2 * L2) / (2.f * Dist);
		const float H = FMath::Sqrt(FMath::Max(0.f, L1 * L1 - Ax * Ax));
		return Hip + D * Ax + N * H;
	}

	// Угол сгиба колена от носка (град); KNEE_DEG_NONE — нога прямая.
	float KneeDev(const FVector& Hip, const FVector& Knee, const FVector& Ank, const FVector& FootFwd)
	{
		const FVector B = BoxerFeel::BendOf(Hip, Knee, Ank);
		bool bOk = false;
		const float A = B.IsNearlyZero() ? 0.f : BoxerFeel::BendAngle(Ank - Hip, B, FootFwd, bOk);
		return bOk ? FMath::RadiansToDegrees(A) : KNEE_DEG_NONE;
	}

	FVector Yaw(float Rad) { return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKneeIkTest, "BoxingUE.KneeIk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKneeIkTest::RunTest(const FString& Parameters)
{
	// --- 1. Случайные позы: колено над носком, обычные позы не меняются ---
	{
		FRandomStream R(7474);
		int32 Bad = 0, OldBad = 0, Changed = 0, Normal = 0;
		float WorstDeg = 0.f;
		for (int32 K = 0; K < 4000; ++K)
		{
			const FVector Hip(0.f, 0.f, 92.f);
			const FVector Goal(R.FRandRange(-45.f, 45.f), R.FRandRange(-35.f, 35.f), R.FRandRange(6.f, 25.f));
			const FVector FootFwd = Yaw(R.FRandRange(-PI, PI));
			// Колено клипа — куда угодно (в том числе назад от носка), отступ 0..20 см от линии.
			const FVector ClipKnee = (Hip + Goal) * 0.5f + FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-0.3f, 0.3f)).GetSafeNormal() * R.FRandRange(0.f, 20.f);
			const float Share = R.FRandRange(0.f, 1.f);
			const FVector N = BoxerFeel::KneeBendDir(Hip, ClipKnee, Goal, FootFwd, L1, L2, Share, true);
			const float Dev = KneeDev(Hip, KneeAt(Hip, Goal, N), Goal, FootFwd);
			if (Dev > KNEE_DEG_NONE)
			{
				WorstDeg = FMath::Max(WorstDeg, FMath::Abs(Dev));
				Bad += FMath::Abs(Dev) > MAX_DEV_DEG + 0.5f ? 1 : 0;
			}
			const FVector No = BoxerFeel::KneeBendDir(Hip, ClipKnee, Goal, FootFwd, L1, L2, Share, false);
			const float DevO = KneeDev(Hip, KneeAt(Hip, Goal, No), Goal, FootFwd);
			OldBad += (DevO > KNEE_DEG_NONE && FMath::Abs(DevO) > 90.f) ? 1 : 0;
			// Колено клипа и так над носком (≤ 30°) — защита не вмешивается.
			const float ClipDev = KneeDev(Hip, ClipKnee, Goal, FootFwd);
			if (ClipDev > KNEE_DEG_NONE && FMath::Abs(ClipDev) < 30.f)
			{
				++Normal;
				Changed += FVector::DotProduct(N, No) < FMath::Cos(FMath::DegreesToRadians(1.f)) ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("случайные позы: худший угол %.1f°, без защиты колено назад (> 90°) в %d из 4000"), WorstDeg, OldBad));
		TestEqual(TEXT("случайные позы: колено не дальше 35° от носка"), Bad, 0);
		TestTrue(*FString::Printf(TEXT("обычные позы не меняются (%d из %d)"), Changed, Normal), Normal > 100 && Changed == 0);
	}

	// --- 2. Серии шагов во всех направлениях (правша/левша) с клипом ходьбы GASP ---
	{
		const FVector2D Dirs[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, 0.7071f}, {-0.7071f, -0.7071f}};
		const TCHAR* DirNames[8] = {TEXT("вперёд"), TEXT("назад"), TEXT("вправо"), TEXT("влево"), TEXT("вперёд-вправо"), TEXT("вперёд-влево"), TEXT("назад-вправо"), TEXT("назад-влево")};
		constexpr float StepDt = 1.f / 60.f;
		for (int32 Lefty = 0; Lefty < 2; ++Lefty)
		{
			const float M = Lefty ? -1.f : 1.f;
			// Стойка (м, рад; X — к сопернику, Y — вправо): передняя впереди-слева носком внутрь, задняя сзади-справа развёрнута.
			const FVector2D StP[2] = {{0.17f, -0.1f * M}, {-0.19f, 0.1f * M}};
			const float StYaw[2] = {0.45f * M, 0.95f * M};
			const int32 SideOf[2] = {Lefty ? 1 : 0, Lefty ? 0 : 1}; // планировщик: 0 — передняя → сторона (0 — левая)
			for (int32 Di = 0; Di < 8; ++Di)
			{
				BoxFoot::FGait G;
				int32 Frames = 0, Bad = 0, OldBad = 0, InBad = 0;
				float Worst = 0.f;
				float BX = 0.f, BY = 0.f;
				for (int32 Fi = 0; Fi < 150; ++Fi)
				{
					const float T = Fi * StepDt;
					// Ход 0.8 м/с 1.6 с серией шагов (толчок-подтяг 0.44 с), затем стоит.
					const float Ph = FMath::Fmod(T, 0.44f) / 0.44f;
					const float Vs = T < 1.6f ? 0.8f * 2.f * FMath::Square(FMath::Sin(PI * Ph)) : 0.f;
					const float VX = Dirs[Di].X * Vs, VY = Dirs[Di].Y * Vs;
					BX += VX * StepDt;
					BY += VY * StepDt;
					BoxFoot::FGoal Goals[2];
					for (int32 I = 0; I < 2; ++I)
					{
						Goals[I].X = BX + StP[I].X;
						Goals[I].Y = BY + StP[I].Y;
						Goals[I].Yaw = StYaw[I];
					}
					BoxFoot::FGaitOpts O;
					O.Dt = Fi == 0 ? 0.f : StepDt;
					O.VX = VX;
					O.VY = VY;
					O.Stretch[0] = O.Stretch[1] = 0.9f;
					O.ToeLen = 0.125f;
					O.Lead = 0.09f;
					BoxFoot::UpdateGait(G, Goals, O);
					for (int32 I = 0; I < 2; ++I)
					{
						const int32 S = SideOf[I];
						const BoxFoot::FFootNow F = BoxFoot::FootNow(G.Feet[I]);
						// Таз — над корпусом, тазобедренный сустав ±10 см вбок; присед 6 см.
						const FVector Hip(BX * 100.f, BY * 100.f + (S == 0 ? -10.f : 10.f), 86.f);
						const FVector Goal(F.X * 100.f, F.Y * 100.f, 8.f + F.Lift * 100.f);
						const FVector FootFwd = Yaw(F.Yaw);
						// Клип ходьбы GASP: нога клипа заносится вдоль хода (± 28 см, противофаза ног), колено клипа — вперёд по
						// корпусу (+X), как у живого шага назад: ступня клипа уходит назад, а колено смотрит к сопернику.
						const float Sw = FMath::Sin(2.f * PI * T / 0.9f + (S == 0 ? 0.f : PI)) * (Vs > 0.05f ? 28.f : 0.f);
						const FVector ClipFoot = FVector(Hip.X + Dirs[Di].X * Sw, Hip.Y + Dirs[Di].Y * Sw, 10.f);
						const FVector ClipKnee = KneeAt(Hip, ClipFoot, FVector(1.f, 0.f, 0.f));
						const FVector Inward(0.f, S == 0 ? 1.f : -1.f, 0.f);
						const FVector N = BoxerFeel::KneeBendDir(Hip, ClipKnee, Goal, FootFwd, L1, L2, 1.f, true, 0.61f, Inward);
						const float Dev = KneeDev(Hip, KneeAt(Hip, Goal, N), Goal, FootFwd);
						// Внутрь (к другой ноге) — не дальше 12°: горизонталь сгиба против носка.
						{
							const FVector Kn = KneeAt(Hip, Goal, N);
							FVector Bd = BoxerFeel::BendOf(Hip, Kn, Goal);
							FVector Fh = FootFwd;
							Bd.Z = 0.f;
							if (Bd.Size() > 0.2f && Dev > KNEE_DEG_NONE)
							{
								const FVector Bn = Bd.GetSafeNormal();
								const float Side = FVector::DotProduct(Bn - Fh * FVector::DotProduct(Bn, Fh), Inward);
								InBad += (Side > 0.f && FMath::Abs(Dev) > 12.5f) ? 1 : 0;
							}
						}
						const FVector No = BoxerFeel::KneeBendDir(Hip, ClipKnee, Goal, FootFwd, L1, L2, 1.f, false);
						const float DevO = KneeDev(Hip, KneeAt(Hip, Goal, No), Goal, FootFwd);
						if (Dev > KNEE_DEG_NONE)
						{
							++Frames;
							Worst = FMath::Max(Worst, FMath::Abs(Dev));
							Bad += FMath::Abs(Dev) > MAX_DEV_DEG + 0.5f ? 1 : 0;
						}
						OldBad += (DevO > KNEE_DEG_NONE && FMath::Abs(DevO) > 60.f) ? 1 : 0;
					}
				}
				const FString Name = FString::Printf(TEXT("%s %s"), Lefty ? TEXT("левша") : TEXT("правша"), DirNames[Di]);
				AddInfo(FString::Printf(TEXT("%s: кадров %d, худший угол %.1f°, переносов %d; без защиты > 60° — %d кадров"), *Name, Frames, Worst, G.Swings, OldBad));
				TestTrue(*FString::Printf(TEXT("%s: колено оценивалось"), *Name), Frames > 100);
				TestEqual(*FString::Printf(TEXT("%s: колено не назад и не вбок (> 35°)"), *Name), Bad, 0);
				TestEqual(*FString::Printf(TEXT("%s: колено не внутрь (> 12°)"), *Name), InBad, 0);
				TestTrue(*FString::Printf(TEXT("%s: ступни шагали"), *Name), G.Swings >= 2);
			}
		}
	}

	// --- 3. Метрика: переворот считается один раз на вход в > 90° ---
	{
		FBoxerJointStat J;
		const float Seq[] = {10.f, 95.f, 120.f, 30.f, -100.f, 20.f};
		for (const float D : Seq) { J.Add(D); }
		TestEqual(TEXT("метрика: переворотов"), J.Flips, 2);
		TestEqual(TEXT("метрика: кадров > 90°"), J.Over90, 3);
		TestEqual(TEXT("метрика: максимум"), J.MaxDeg, 120.f);
	}
	return true;
}

#endif
