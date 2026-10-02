// Автотест S-62: падение внутри канатов (BoxerFall) и наведение в голову мимо перчаток (BoxerFeel::AimAroundGuard).
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.BoxerFall;Quit"
#include "BoxerFall.h"
#include "BoxerFeel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Раскладка «упал на спину» как у AM_Knockdown (замер клипа: голова 152 см за ногами, таз 95), руки в стороны.
	BoxerFall::FLayout BackFall(float Scale)
	{
		BoxerFall::FLayout L;
		const FVector2D Pts[] = {{0, -12}, {0, 12}, {-50, -10}, {-50, 10}, {-95, 0}, {-130, 0}, {-152, 0}, {-135, -70}, {-135, 70}, {-120, -45}, {-120, 45}};
		for (const FVector2D& P : Pts)
		{
			L.Pts.Add(P * Scale);
		}
		L.Head = FVector2D(-152, 0) * Scale;
		L.Pelvis = FVector2D(-95, 0) * Scale;
		L.bValid = true;
		return L;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxerFallTest, "BoxingUE.BoxerFall", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxerFallTest::RunTest(const FString& Parameters)
{
	using namespace BoxerFall;
	const FLayout L = BackFall(1.f);

	// --- оси ---
	TestTrue(TEXT("ToWorld: курс 0 — вперёд по +X"), ToWorld(FVector2D(100, 0), FVector2D::ZeroVector, 0.f).Equals(FVector2D(100, 0), 0.01));
	TestTrue(TEXT("ToWorld: курс 90 — вперёд по +Y"), ToWorld(FVector2D(100, 0), FVector2D::ZeroVector, 90.f).Equals(FVector2D(0, 100), 0.01));
	TestTrue(TEXT("ToWorld: вправо при курсе 0 — +Y"), ToWorld(FVector2D(0, 50), FVector2D::ZeroVector, 0.f).Equals(FVector2D(0, 50), 0.01));

	// --- в центре: ничего не трогаем ---
	{
		FPlaceIn In;
		In.Pos = FVector2D(0, 0);
		In.YawDeg = 30.f;
		In.Layout = &L;
		const FPlaceOut O = Solve(In);
		TestTrue(TEXT("центр: внутри"), O.bInside);
		TestEqual(TEXT("центр: без доворота"), O.TurnDeg, 0.f);
		TestTrue(TEXT("центр: без сдвига"), O.Offset.IsNearlyZero());
	}

	// --- находка QA: нокдаун у канатов (−2.54, −1.78), лицом в ринг — тело было за помостом ---
	{
		FPlaceIn In;
		In.Pos = FVector2D(-254, -178);
		In.YawDeg = 50.f;
		In.Layout = &L;
		TestTrue(TEXT("QA kd60: без правки голова за канатами"), Overhang(L, In.Pos, In.YawDeg, ROPE_LIMIT_CM) > 50.f);
		const FPlaceOut O = Solve(In);
		TestTrue(TEXT("QA kd60: решение внутри"), O.bInside);
		TestEqual(TEXT("QA kd60: после правки — внутри канатов"), Overhang(L, In.Pos + O.Offset, In.YawDeg + O.TurnDeg, ROPE_LIMIT_CM), 0.f);
		TestTrue(TEXT("QA kd60: доворот в разумных пределах"), FMath::Abs(O.TurnDeg) <= MAX_TURN_DEG);
	}

	// --- перебор: любая точка ядра (±RING_HALF 2.77 м), любой курс, рост 154…206 см — тело внутри канатов ---
	{
		int32 Bad = 0, Cases = 0;
		float MaxShift = 0.f;
		for (const float Scale : {0.88f, 1.f, 1.13f, 1.18f})
		{
			const FLayout Ls = BackFall(Scale);
			for (int32 X = -277; X <= 277; X += 46)
			{
				for (int32 Y = -277; Y <= 277; Y += 46)
				{
					for (int32 Yaw = -180; Yaw < 180; Yaw += 30)
					{
						FPlaceIn In;
						In.Pos = FVector2D(X, Y);
						In.YawDeg = static_cast<float>(Yaw);
						In.Layout = &Ls;
						const FPlaceOut O = Solve(In);
						++Cases;
						if (!O.bInside || Overhang(Ls, In.Pos + O.Offset, In.YawDeg + O.TurnDeg, ROPE_LIMIT_CM) > 0.01f)
						{
							++Bad;
						}
						MaxShift = FMath::Max(MaxShift, static_cast<float>(O.Offset.Size()));
					}
				}
			}
		}
		TestEqual(FString::Printf(TEXT("перебор %d раскладок: все внутри канатов"), Cases), Bad, 0);
		TestTrue(FString::Printf(TEXT("сдвиг не больше 1.8 м (макс. %.0f см; спиной вплотную к канатам)"), MaxShift), MaxShift <= 180.f);
	}

	// --- тело не ложится на соперника/рефери ---
	{
		FPlaceIn In;
		In.Pos = FVector2D(-254, 0);
		In.YawDeg = 0.f; // спиной к канатам −X: падает за канаты
		In.Layout = &L;
		const FPlaceOut Free = Solve(In);
		// Рефери стоит там, куда легло бы тело без препятствий.
		const FVector2D Ref = ToWorld(L.Head, In.Pos + Free.Offset, In.YawDeg + Free.TurnDeg);
		In.Avoid.Add({Ref, Ref, 60.f});
		const FPlaceOut O = Solve(In);
		float Near = 1e6f;
		for (const FVector2D& P : L.Pts)
		{
			Near = FMath::Min(Near, static_cast<float>(FVector2D::Distance(ToWorld(P, In.Pos + O.Offset, In.YawDeg + O.TurnDeg), Ref)));
		}
		TestTrue(TEXT("рефери: решение внутри"), O.bInside);
		TestTrue(FString::Printf(TEXT("рефери: тело мимо него (мин. %.0f см ≥ 50)"), Near), Near >= 50.f);
	}

	// --- огибающая ---
	TestEqual(TEXT("Blend(0) = 0"), Blend(0.f, 0.7f), 0.f);
	TestEqual(TEXT("Blend(конец) = 1"), Blend(0.7f, 0.7f), 1.f);
	TestTrue(TEXT("Blend монотонна"), Blend(0.2f, 0.7f) < Blend(0.4f, 0.7f));

	// --- наведение мимо перчаток (BoxerFeel::AimAroundGuard) ---
	{
		using namespace BoxerFeel;
		const FVector Center(100, 0, 170);
		const FVector Shoulder(0, -15, 145);
		const FVector A0 = (Center - Shoulder).GetSafeNormal();
		const FVector Far[2] = {FVector(80, 60, 120), FVector(80, -60, 120)};
		FGuardAim G;
		const bool bMoved = AimAroundGuard(Center, 10.f, Shoulder, A0, true, 35.f, Far, 16.f, nullptr, G);
		TestFalse(TEXT("гард в стороне: точка не сдвинута"), bMoved);
		TestTrue(TEXT("гард в стороне: касание навстречу удару"), G.Surface.Equals(Center - A0 * 10.f, 0.5));

		// Перчатка у подбородка, чуть ниже линии «плечо → лицо» (как у низкого против 198-см: удар шёл в перчатку).
		const FVector Chin = Center - A0 * 17.f - FVector(0, 0, 9);
		const FVector Block[2] = {Chin, FVector(80, 60, 120)};
		const float Before = SegPointDist(Shoulder, Center - A0 * 10.f, Chin) - 16.f;
		TestTrue(TEXT("перчатка у подбородка: путь в лоб сквозь неё"), Before < 0.f);
		const bool bMoved2 = AimAroundGuard(Center, 10.f, Shoulder, A0, true, 35.f, Block, 16.f, nullptr, G);
		TestTrue(TEXT("перчатка у подбородка: точка сдвинута"), bMoved2);
		// Концом прямого на сфере 10 см всю перчатку не обойти — остаток добирает отвод перчатки (GuardPush ниже).
		TestTrue(FString::Printf(TEXT("перчатка у подбородка: путь заметно дальше от неё (запас %.1f см, было %.1f)"), G.ClearCm, Before), G.ClearCm >= Before + 5.f);
		TestTrue(TEXT("перчатка на линии: касание на голове"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(G.Surface, Center)), 10.f, 0.1f));
		TestTrue(TEXT("перчатка на линии: кулак приходит снаружи"), FVector::DotProduct(G.Approach, (G.Surface - Center).GetSafeNormal()) < 0.f);

		// Гистерезис: тот же выбор при прошлом решении.
		FGuardAim G2;
		AimAroundGuard(Center, 10.f, Shoulder, A0, true, 35.f, Block, 16.f, &G.UV, G2);
		TestTrue(TEXT("гистерезис: выбор держится"), G2.UV.Equals(G.UV, 1e-3));

		// Хук: путь — последние 35 см по направлению подхода.
		const FVector HookA = FVector(0.35f, -0.65f, 0.f).GetSafeNormal();
		const FVector HookGlove[2] = {Center - HookA * 22.f - FVector(0, 0, 11), FVector(80, -60, 120)};
		const bool bHook = AimAroundGuard(Center, 10.f, Shoulder, HookA, false, 35.f, HookGlove, 16.f, nullptr, G);
		TestTrue(TEXT("хук сквозь перчатку: сдвинут, путь мимо"), bHook && G.ClearCm >= 0.f);
		TestTrue(TEXT("хук: направление подхода прежнее"), G.Approach.Equals(HookA, 1e-3));

		// Перчатка прямо на линии удара посередине пути — концом не обойти: её отводит защита (GuardPush).
		const FVector A(0, 0, 150), B(60, 0, 150);
		const FVector Push = GuardPush(FVector(30, 4, 150), A, B, 16.f, 18.f);
		TestTrue(TEXT("GuardPush: отвод поперёк удара"), FMath::Abs(Push.X) < 1e-3 && Push.Y > 0.f);
		TestTrue(TEXT("GuardPush: после отвода — мимо пути"), FMath::IsNearlyEqual(static_cast<float>(SegPointDist(A, B, FVector(30, 4, 150) + Push)), 16.f, 0.01f));
		TestTrue(TEXT("GuardPush: не дальше предела"), GuardPush(FVector(30, 0.1f, 150), A, B, 40.f, 18.f).Size() <= 18.f + 1e-3);
		TestTrue(TEXT("GuardPush: путь и так мимо — ноль"), GuardPush(FVector(30, 20, 150), A, B, 16.f, 18.f).IsNearlyZero());
	}
	return true;
}

#endif
