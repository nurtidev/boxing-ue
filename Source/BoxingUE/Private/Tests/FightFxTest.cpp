// Автотест чистой логики «ощущения боя» (S-54) — зеркало web/test/fightFx.test.ts.
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.FightFx;Quit"
#include "FightFx.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxFightFxTest, "BoxingUE.FightFx", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxFightFxTest::RunTest(const FString& Parameters)
{
	using namespace BoxFx;
	const FProfile S = FProfile::S36();
	const FProfile C = FProfile::Classic();

	// --- хит-стоп ---
	TestEqual(TEXT("S36: слабое попадание без стопа"), HitStopMs(EKind::Land, 1.2f, S), 0.f);
	TestEqual(TEXT("S36: порог 1.5 → 25 мс"), HitStopMs(EKind::Land, 1.5f, S), 25.f);
	TestEqual(TEXT("S36: mag 2.3+ → 45 мс"), HitStopMs(EKind::Land, 3.f, S), 45.f);
	TestEqual(TEXT("S36: нокдаун 70 мс"), HitStopMs(EKind::Kd, 1.f, S), 70.f);
	TestEqual(TEXT("S36: блок без стопа"), HitStopMs(EKind::Block, 2.f, S), 0.f);
	TestEqual(TEXT("classic: ниже 1.1 — ноль"), HitStopMs(EKind::Land, 1.05f, C), 0.f);
	TestEqual(TEXT("classic: 1.1 → 50 мс"), HitStopMs(EKind::Land, 1.1f, C), 50.f);
	TestEqual(TEXT("classic: 2.3 → 110 мс"), HitStopMs(EKind::Land, 2.3f, C), 110.f);
	TestEqual(TEXT("classic: нокдаун 110 мс"), HitStopMs(EKind::Kd, 1.f, C), 110.f);
	TestTrue(TEXT("стоп растёт с силой"), HitStopMs(EKind::Land, 1.7f, C) > HitStopMs(EKind::Land, 1.3f, C));

	// --- камера ---
	const FCamKick Weak = CameraKick(EKind::Land, 0.5f);
	const FCamKick Heavy = CameraKick(EKind::Land, 2.0f);
	TestTrue(TEXT("слабый — без наезда"), Weak.PunchIn == 0.f && Weak.Shake > 0.f);
	TestTrue(TEXT("тяжёлый — наезд"), Heavy.PunchIn > 0.5f && Heavy.PunchIn <= 0.8f);
	TestTrue(TEXT("тряска ≤ 0.7 на попадании"), CameraKick(EKind::Land, 9.f).Shake <= 0.7f);
	TestTrue(TEXT("блок — лёгкая тряска"), CameraKick(EKind::Block, 1.f).Shake <= 0.15f && CameraKick(EKind::Block, 1.f).PunchIn == 0.f);
	TestEqual(TEXT("нокдаун — полная тряска"), CameraKick(EKind::Kd, 1.f).Shake, 1.f);
	TestEqual(TEXT("промах — ничего"), CameraKick(EKind::Miss, 1.f).Shake, 0.f);

	// --- slow-mo ---
	const FSlowMo Kd = SlowMoFor(EKind::Kd, 1.f, S);
	TestTrue(TEXT("нокдаун: провал 0.3"), Kd.IsValid() && FMath::IsNearlyEqual(SlowMoScale(Kd, 0.f), 0.3f));
	TestTrue(TEXT("нокдаун: держит 0.35 с"), FMath::IsNearlyEqual(SlowMoScale(Kd, 0.3f), 0.3f));
	TestTrue(TEXT("нокдаун: к 0.9 с — 1"), FMath::IsNearlyEqual(SlowMoScale(Kd, 0.9f), 1.f));
	TestTrue(TEXT("нокдаун: монотонный возврат"), SlowMoScale(Kd, 0.5f) < SlowMoScale(Kd, 0.7f));
	TestFalse(TEXT("S36: тяжёлое попадание без slow-mo"), SlowMoFor(EKind::Land, 2.5f, S).IsValid());
	TestTrue(TEXT("classic: mag 1.9 — slow-mo"), SlowMoFor(EKind::Land, 1.9f, C).IsValid());
	TestFalse(TEXT("classic: mag 1.8 — нет"), SlowMoFor(EKind::Land, 1.8f, C).IsValid());
	TestEqual(TEXT("без провала — 1"), SlowMoScale(FSlowMo(), 0.1f), 1.f);

	// --- повтор ---
	TestEqual(TEXT("подводка 0.5"), ReplaySpeed(-1.5f), 0.5f);
	TestEqual(TEXT("удар 0.28"), ReplaySpeed(0.2f), 0.28f);
	TestTrue(TEXT("переход 0.5→0.28"), ReplaySpeed(-0.5f) < 0.5f && ReplaySpeed(-0.5f) > 0.28f);

	// --- вибрация ---
	TestTrue(TEXT("пропустил тяжёлый — heavy"), HapticFor(EKind::Land, 0, 1.5f, 0) == EHaptic::Heavy);
	TestTrue(TEXT("попал слабо — тихо"), HapticFor(EKind::Land, 1, 0.3f, 0) == EHaptic::None);
	TestTrue(TEXT("промах — тихо"), HapticFor(EKind::Miss, 0, 1.f, 0) == EHaptic::None);
	TestTrue(TEXT("нокдаун — heavy"), HapticFor(EKind::Kd, 1, 1.f, 0) == EHaptic::Heavy);

	// --- кадр нокдауна ---
	{
		const FVector RC(0.f, 0.f, 10.f);
		const FVector Body(-50.f, 0.f, 30.f), Stand(200.f, -200.f, 100.f);
		int32 Side = 0;
		FVector Cam, Look;
		KnockdownShot(Body, Stand, RC, Side, Cam, Look);
		TestTrue(TEXT("нокдаун: угол выбран"), Side != 0);
		TestTrue(TEXT("нокдаун: камера в 3 м от лежащего"), FMath::IsNearlyEqual(FVector::Dist2D(Cam, Body), KD_SHOT_DIST, 1.f));
		TestTrue(TEXT("нокдаун: камера над полом 1.85 м, не дальше апрона"), FMath::IsNearlyEqual(Cam.Z - RC.Z, KD_SHOT_HEIGHT, 0.5f) && FMath::Abs(Cam.X) <= KD_SHOT_LIM && FMath::Abs(Cam.Y) <= KD_SHOT_LIM);
		// Стоящий — не по центру кадра: угол между взглядом и направлением на него ≥ 15°.
		const FVector View = (Look - Cam).GetSafeNormal2D();
		const FVector ToStand = (Stand - Cam).GetSafeNormal2D();
		const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(View, ToStand), -1.f, 1.f)));
		TestTrue(TEXT("нокдаун: стоящий сбоку от центра кадра, но в кадре 16:9"), Deg >= 12.f && Deg <= 34.f);
		// Лежащий — перед камерой, ближе стоящего.
		TestTrue(TEXT("нокдаун: лежащий ближе стоящего"), FVector::Dist2D(Cam, Body) < FVector::Dist2D(Cam, Stand));
		const int32 Was = Side;
		KnockdownShot(Body + FVector(5.f, 5.f, 0.f), Stand, RC, Side, Cam, Look);
		TestEqual(TEXT("нокдаун: сторона держится"), Side, Was);
		// У канатов камера не уходит дальше 4.35 м от центра.
		int32 S2 = 0;
		KnockdownShot(FVector(280.f, 0.f, 30.f), FVector(-200.f, 0.f, 100.f), RC, S2, Cam, Look);
		TestTrue(TEXT("нокдаун у канатов: камера не дальше апрона"), FMath::Abs(Cam.X) <= KD_SHOT_LIM + 0.1f && FMath::Abs(Cam.Y) <= KD_SHOT_LIM + 0.1f);
	}
	// Случай из прогона (сид 60): упал у своего угла под канатами, стоящий ушёл в дальний нейтральный угол.
	{
		const FVector RC(0.f, 0.f, 0.f);
		const FVector Body(-302.f, -238.f, 20.f), Stand(263.f, -263.f, 90.f);
		int32 Side = 0;
		FVector Cam, Look;
		KnockdownShot(Body, Stand, RC, Side, Cam, Look);
		const FVector View = (Look - Cam).GetSafeNormal2D();
		const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(View, (Stand - Cam).GetSafeNormal2D())), -1.f, 1.f)));
		// Упал в углу: стоящий в дальнем углу у края кадра (≤ 45°) — лежащий в кадре важнее (камера не уходит за апрон).
		TestTrue(TEXT("нокдаун у угла: стоящий у края кадра"), Deg <= 45.f);
		TestTrue(TEXT("нокдаун у угла: камера не дальше апрона"), FMath::Abs(Cam.X) <= KD_SHOT_LIM + 0.1f && FMath::Abs(Cam.Y) <= KD_SHOT_LIM + 0.1f);
	}
	// --- кадр перерыва ---
	{
		const FVector RC(0.f, 0.f, 0.f);
		FVector Cam, Look;
		RestShot(0, 16.f / 9.f, FVector(-263.f, -263.f, 0.f), RC, Cam, Look);
		TestTrue(TEXT("перерыв: камера ближе к центру, чем угол"), Cam.Size2D() < FVector(-263.f, -263.f, 0.f).Size2D());
		TestTrue(TEXT("перерыв: смотрит в угол красного"), Look.X < 0.f && Look.Y < 0.f);
		FVector Cam2, Look2;
		RestShot(0, 0.46f, FVector(-263.f, -263.f, 0.f), RC, Cam2, Look2);
		TestTrue(TEXT("перерыв: портрет — дальше и выше"), FVector::Dist2D(Cam2, FVector(-263.f, -263.f, 0.f)) > FVector::Dist2D(Cam, FVector(-263.f, -263.f, 0.f)) && Cam2.Z > Cam.Z);
	}

	// --- виды событий ---
	TestTrue(TEXT("Slipped → miss"), KindOf(EFightEventKind::Slipped) == EKind::Miss);
	TestTrue(TEXT("Hit → land"), KindOf(EFightEventKind::Hit) == EKind::Land);
	TestTrue(TEXT("RoundEnd → other"), KindOf(EFightEventKind::RoundEnd) == EKind::Other);
	return true;
}

#endif
