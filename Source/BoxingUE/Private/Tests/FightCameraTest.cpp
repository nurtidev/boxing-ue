// Автотест S-78: смешивание кадров камеры «по орбите» и ограничитель итогового кадра (BoxFx::BlendView / LimitViewDir).
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.FightCamera;Quit"
#include "FightFx.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxFightCameraTest, "BoxingUE.FightCamera", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxFightCameraTest::RunTest(const FString& Parameters)
{
	using namespace BoxFx;
	auto PitchOf = [](const FVector& D) { return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.GetSafeNormal().Z, -1.f, 1.f))); };
	auto YawOf = [](const FVector& D) { return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)); };

	// --- случай QA (D1, выход на 2-й раунд): камера перерыва у угла → камера выхода за спиной игрока ---
	{
		const FVector RestCam(-23.f, -94.f, 162.f), RestLook(-263.f, -263.f, 110.f);
		const FVector OutCam(-436.f, -311.f, 291.f), OutLook(0.f, 0.f, 120.f);
		const FVector Seated(-263.f, -263.f, 100.f); // сидящий в углу
		float MinPitch = 0.f, MinHead = 1e6f, MaxStep = 0.f;
		FVector PrevDir = FVector::ZeroVector;
		for (int32 I = 0; I <= 40; ++I)
		{
			FVector C, L;
			BlendView(RestCam, RestLook, OutCam, OutLook, I / 40.f, C, L);
			const FVector D = L - C;
			MinPitch = FMath::Min(MinPitch, PitchOf(D));
			MinHead = FMath::Min(MinHead, static_cast<float>(FVector::Dist(C, Seated)));
			if (!PrevDir.IsNearlyZero())
			{
				MaxStep = FMath::Max(MaxStep, FMath::Abs(WrapDeg(YawOf(D) - YawOf(PrevDir))));
			}
			PrevDir = D;
		}
		TestTrue(TEXT("перерыв → выход: камера не ныряет сверху (наклон > −45°)"), MinPitch > -45.f);
		TestTrue(TEXT("перерыв → выход: не пролетает над сидящим (> 1.5 м)"), MinHead > 150.f);
		TestTrue(TEXT("перерыв → выход: курс без скачков (< 15° на 1/40 перехода)"), MaxStep < 15.f);
	}
	// --- концы смешивания — ровно кадры A и B ---
	{
		const FVector A(100.f, 0.f, 200.f), La(0.f, 0.f, 100.f), Bc(-50.f, 300.f, 150.f), Lb(10.f, 10.f, 90.f);
		FVector C, L;
		BlendView(A, La, Bc, Lb, 0.f, C, L);
		TestTrue(TEXT("T = 0 — кадр A"), C.Equals(A, 0.1f) && L.Equals(La, 0.1f));
		BlendView(A, La, Bc, Lb, 1.f, C, L);
		TestTrue(TEXT("T = 1 — кадр B"), C.Equals(Bc, 0.1f) && L.Equals(Lb, 0.1f));
	}
	// --- нокдаун/KO: сторона кадра ближе к нынешней камере (без облёта на полкруга) ---
	{
		const FVector Rc(0.f, 0.f, 0.f), Body(0.f, 0.f, 0.f), Stand(-200.f, 0.f, 0.f);
		for (const FVector& Cur : {FVector(0.f, -400.f, 180.f), FVector(0.f, 400.f, 180.f)})
		{
			int32 Side = 0;
			FVector C, L;
			KnockdownShot(Body, Stand, Rc, Side, C, L, &Cur);
			const float Turn = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct((C - Body).GetSafeNormal2D(), (Cur - Body).GetSafeNormal2D())), -1.f, 1.f)));
			TestTrue(FString::Printf(TEXT("кадр нокдауна с той же стороны, что камера (облёт %.0f° < 90°)"), Turn), Turn < 90.f);
		}
	}
	// --- ограничитель ---
	{
		const FVector Down = FVector(0.2f, 0.f, -1.f).GetSafeNormal();
		bool bTurn = false, bPitch = false;
		const FVector D = LimitViewDir(FVector::ZeroVector, Down, 1.f / 60.f, true, &bTurn, &bPitch);
		TestTrue(TEXT("наклон −79° → не круче −45°"), bPitch && FMath::Abs(PitchOf(D) - CAM_PITCH_MIN_DEG) < 0.1f);
		const FVector Prev(1.f, 0.f, 0.f), Want(-1.f, 0.05f, 0.f);
		const FVector R = LimitViewDir(Prev, Want, 1.f / 60.f, false, &bTurn, &bPitch);
		TestTrue(TEXT("разворот 180° за кадр → не больше 160°/с"), bTurn && FMath::Abs(YawOf(R)) <= CAM_MAX_TURN_DPS / 60.f + 0.01f);
		const FVector Cut = LimitViewDir(Prev, Want, 1.f / 60.f, true, &bTurn, &bPitch);
		TestTrue(TEXT("склейка — поворот сразу"), !bTurn && FMath::Abs(FMath::Abs(YawOf(Cut)) - 180.f) < 3.f);
		const FVector Slow = LimitViewDir(Prev, FVector(1.f, 0.02f, -0.01f), 1.f / 60.f, false, &bTurn, &bPitch);
		TestTrue(TEXT("обычное ведение — без ограничения"), !bTurn && !bPitch && Slow.Equals(FVector(1.f, 0.02f, -0.01f).GetSafeNormal(), 1e-3f));
	}
	return true;
}

#endif
