// Автотест угловых и посадки (S-71) — зеркало web/test/crew.test.ts + sitPose: места пол ↔ апрон (совпадают с
// маркерами L_Ring), переход «вверх — сначала по высоте», реакции угла, настроение (наверх в перерыве, наклон и стул
// только при севшем бойце, по гонгу — вниз), сценарий катмена, направления рук, посадка бойца (задержка, флаг «сел»,
// по гонгу встаёт), кадр перерыва для сидящего — ниже.
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosound -ExecCmds="Automation RunTests BoxingUE.FightCrew;Quit"
#include "FightCrew.h"
#include "BoxerSit.h"
#include "FightFx.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxingFightCrewTest, "BoxingUE.FightCrew", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxingFightCrewTest::RunTest(const FString& Parameters)
{
	using namespace BoxCrew;
	// --- места = маркеры build_ring.py (см): тренер красных в бою (−407, −270, −110), в перерыве (−335, −208, 0) ---
	{
		const FVector A = SpotCore(0, ERole::Coach, false) * 100.f;
		const FVector B = SpotCore(0, ERole::Coach, true) * 100.f;
		TestTrue(TEXT("тренер красных, бой"), A.Equals(FVector(-407.f, -270.f, -110.f), 0.6f));
		TestTrue(TEXT("тренер красных, перерыв"), B.Equals(FVector(-335.f, -208.f, 0.f), 0.6f));
		TestTrue(TEXT("катмен синих, бой"), (SpotCore(1, ERole::Cutman, false) * 100.f).Equals(FVector(340.f, 407.f, -110.f), 0.6f));
		TestTrue(TEXT("катмен синих, перерыв"), (SpotCore(1, ERole::Cutman, true) * 100.f).Equals(FVector(208.f, 335.f, 0.f), 0.6f));
		TestTrue(TEXT("стул красных в углу"), (StoolCore(0, false) * 100.f).Equals(FVector(-263.f, -263.f, 0.f), 0.6f));
		// Переход: концы точные, на трети пути уже выше половины подъёма, а по горизонтали — меньше половины.
		TestTrue(TEXT("переход: u=0 — бой"), PlaceBetween(A, B, 0.f).Equals(A, 0.01f));
		TestTrue(TEXT("переход: u=1 — апрон"), PlaceBetween(A, B, 1.f).Equals(B, 0.01f));
		const FVector M = PlaceBetween(A, B, 0.33f);
		TestTrue(TEXT("переход: сначала поднялся"), (M.Z - A.Z) / (B.Z - A.Z) > 1.5f * (M.X - A.X) / (B.X - A.X));
		// Скорость пути ≈ разностной производной.
		const float U = 0.4f, Du = 0.9f, H = 1e-3f;
		const FVector Num = (PlaceBetween(A, B, U + H) - PlaceBetween(A, B, U - H)) / (2.f * H) * Du;
		TestTrue(TEXT("переход: скорость — производная пути"), PlaceVelocity(A, B, U, Du).Equals(Num, 1.f));
	}
	// --- реакции (crewReaction) ---
	{
		FFightEvent E;
		FKick K[2];
		E.Kind = EFightEventKind::Hit;
		E.Attacker = 0;
		E.Defender = 1;
		E.Magnitude = 0.7f;
		TestEqual(TEXT("слабое попадание — угол молчит"), Reaction(E, K), 0);
		E.Magnitude = 1.4f;
		TestEqual(TEXT("тяжёлое — два всплеска"), Reaction(E, K), 2);
		TestTrue(TEXT("тяжёлое: свой угол радуется, чужой тревожится"), K[0].Corner == 0 && K[0].Cheer > 0.5f && K[1].Corner == 1 && K[1].Worry > 0.4f);
		E.Kind = EFightEventKind::Knockdown;
		E.Defender = 0;
		Reaction(E, K);
		TestTrue(TEXT("нокдаун: угол упавшего — тревога 1"), K[0].Corner == 0 && K[0].Worry == 1.f && K[1].Cheer == 1.f);
		E.Kind = EFightEventKind::Blocked;
		E.Magnitude = 3.f;
		TestEqual(TEXT("блок — без реакции"), Reaction(E, K), 0);
	}
	// --- настроение: наверх в перерыве, стул/наклон — по севшему, по гонгу вниз ---
	{
		FMood M;
		M.Kick({0, 1.f, 0.f});
		TestTrue(TEXT("всплеск"), M.Cheer == 1.f && M.CheerT == 0.f);
		M.Kick({0, 0.5f, 0.f});
		TestTrue(TEXT("слабее текущего — не перебивает"), M.Cheer == 1.f);
		for (int32 I = 0; I < 60; ++I) M.Update(1.f / 60.f, true, false);
		TestTrue(TEXT("перерыв: эмоции гаснут, поднимаются"), M.Cheer == 0.f && M.Up > 0.8f && M.Up < 1.f);
		TestTrue(TEXT("не сел — не наклоняется"), M.Lean == 0.f);
		for (int32 I = 0; I < 120; ++I) M.Update(1.f / 60.f, true, true);
		TestTrue(TEXT("сел: наверху, наклон, стул, сценарий идёт"), M.Up == 1.f && M.Lean == 1.f && M.Stool == 1.f && M.RestT > 1.9f);
		for (int32 I = 0; I < 30; ++I) M.Update(1.f / 60.f, false, false);
		TestTrue(TEXT("гонг: стул убран, наклон снят, спускаются"), M.Stool == 0.f && M.Lean < 0.1f && M.Up < 1.f && M.RestT == 0.f);
		for (int32 I = 0; I < 120; ++I) M.Update(1.f / 60.f, false, false);
		TestTrue(TEXT("бой: снова у помоста"), M.Up == 0.f);
	}
	// --- S-78: жест реакции набирается плавно, повторный всплеск не дёргает руки; предел скорости костей; стул не в ногах ---
	{
		FMood M;
		M.Kick({0, 1.f, 0.f});
		M.Update(1.f / 60.f, false, false);
		TestTrue(TEXT("S-78: в кадр всплеска жест не скачком"), M.CheerW > 0.f && M.CheerW < 0.1f);
		const FBody B0 = Pose(ERole::Coach, FMood(), 0.f, 0.f, 0.f, 0.f);
		const FBody B1 = Pose(ERole::Coach, M, 0.f, 0.f, 0.f, 0.f);
		TestTrue(TEXT("S-78: рука в кадр всплеска почти на месте"), FMath::Abs(B1.Arm[1].Fwd - B0.Arm[1].Fwd) < 0.15f);
		for (int32 I = 0; I < 20; ++I) M.Update(1.f / 60.f, false, false);
		const float Osc = M.CheerOsc;
		M.Kick({0, 1.f, 0.f});
		TestTrue(TEXT("S-78: повторный всплеск не сбрасывает качание"), M.CheerOsc == Osc && M.CheerW > 0.9f);
		const FQuat A = FQuat::Identity, Bq(FVector::UpVector, 2.f);
		const FQuat L = LimitRotation(A, Bq, 9.f, 1.f / 60.f);
		TestTrue(TEXT("S-78: предел скорости — 9 рад/с"), FMath::IsNearlyEqual(static_cast<float>(A.AngularDistance(L)), 0.15f, 1e-3f));
		TestTrue(TEXT("S-78: длинный кадр — сразу"), LimitRotation(A, Bq, 9.f, 0.5f).Equals(Bq, 1e-4f));
		TestTrue(TEXT("S-78: повторная оценка — стоит"), LimitRotation(A, Bq, 9.f, 0.f).Equals(A, 1e-4f));
		const FVector In(-263.f, -263.f, 0.f), Post(-343.f, -343.f, 0.f);
		const FVector Bx(-258.f, -258.f, 90.f);
		const FVector S0 = StoolSpot(In, Bx, Post, Bx, 0.f, STOOL_CLEAR_CM);
		TestTrue(TEXT("S-78: стоит в углу — стул за ним, не в ногах"), FVector::Dist2D(S0, Bx) >= STOOL_CLEAR_CM - 0.1f && FVector::Dist2D(S0, Post) < FVector::Dist2D(Bx, Post));
		TestTrue(TEXT("S-78: сел — стул под тазом"), FVector::Dist2D(StoolSpot(In, Bx, Post, Bx, 1.f, STOOL_CLEAR_CM), Bx) < 0.1f);
		TestTrue(TEXT("S-78: далеко от угла — стул в углу"), StoolSpot(In, FVector(0.f, 0.f, 90.f), Post, FVector::ZeroVector, 0.f, STOOL_CLEAR_CM).Equals(In, 0.01f));
	}
	// --- сценарий катмена: бутылка, потом протирает — не одновременно ---
	{
		float B = 0.f, W = 0.f;
		CutmanScript(1.5f, B, W);
		TestTrue(TEXT("1.5 с — бутылка у губ"), B > 0.99f && W == 0.f);
		CutmanScript(5.f, B, W);
		TestTrue(TEXT("5 с — протирает"), W > 0.99f && B == 0.f);
		float MaxBoth = 0.f;
		for (float T = 0.f; T < 14.f; T += 0.05f)
		{
			CutmanScript(T, B, W);
			MaxBoth = FMath::Max(MaxBoth, FMath::Min(B, W));
		}
		TestTrue(TEXT("бутылка и полотенце не одновременно"), MaxBoth < 0.01f);
	}
	// --- руки: опущенная — вниз, «да!» — кулак вверх; левая/правая зеркальны ---
	{
		const FVector F(1, 0, 0), L(0, -1, 0), U(0, 0, 1);
		FVector Up, Fo, Up2, Fo2;
		ArmDirs(FArm{0.08f, 0.f, 0.25f}, 0, F, L, U, Up, Fo);
		TestTrue(TEXT("опущенная рука — вниз"), Up.Z < -0.9f);
		ArmDirs(FArm{2.55f, 0.35f, 0.7f}, 1, F, L, U, Up2, Fo2);
		TestTrue(TEXT("«да!» — плечо вверх"), Up2.Z > 0.4f);
		ArmDirs(FArm{0.08f, 0.f, 0.25f}, 1, F, L, U, Up2, Fo2);
		TestTrue(TEXT("правая — зеркало левой"), FMath::IsNearlyEqual(Up.Y, -Up2.Y, 1e-4f) && FMath::IsNearlyEqual(Up.Z, Up2.Z, 1e-4f));
		FMood M;
		M.Lean = 1.f;
		M.Up = 1.f;
		const FBody Bc = Pose(ERole::Coach, M, 0.f, 0.f, 0.f, 0.f);
		TestTrue(TEXT("перерыв: тренер наклонился к бойцу"), Bc.Lean > 0.5f);
		M.RestT = 1.5f;
		const FBody Bu = Pose(ERole::Cutman, M, 0.f, 0.f, 0.f, 0.f);
		TestTrue(TEXT("перерыв: катмен с бутылкой"), Bu.Bottle > 0.99f);
	}
	// --- посадка бойца ---
	{
		FFightSnapshot S;
		S.Phase = EFightPhase::Between;
		S.Stage.Kind = ERingStageKind::Rest;
		S.Stage.bArrived[0] = false;
		TestFalse(TEXT("идёт в угол — не садится"), BoxerSit::WantsSit(S, 0));
		S.Stage.bArrived[0] = true;
		TestTrue(TEXT("дошёл — садится"), BoxerSit::WantsSit(S, 0));
		FBoxerSitState St;
		St.Update(true, 0.2f);
		TestTrue(TEXT("сначала разворот (задержка)"), St.W == 0.f);
		for (int32 I = 0; I < 60; ++I) St.Update(true, 1.f / 60.f);
		TestTrue(TEXT("сел за ~1 с"), St.IsSeated());
		St.Update(false, 1.f / 60.f);
		TestFalse(TEXT("гонг — флаг «сел» снят сразу"), St.IsSeated());
		for (int32 I = 0; I < 30; ++I) St.Update(false, 1.f / 60.f);
		TestTrue(TEXT("встал за 0.5 с"), St.W == 0.f);
		S.Stage.Kind = ERingStageKind::Out;
		S.Phase = EFightPhase::Walkout;
		TestFalse(TEXT("выход — не сидит"), BoxerSit::WantsSit(S, 0));
	}
	// --- кадр перерыва для сидящего: ниже и ближе (кадр веба), чем для стоящего 198 см ---
	{
		const FVector RC(0.f, 0.f, 0.f), At(-263.f, -263.f, 0.f);
		FVector C0, L0, C1, L1;
		BoxFx::RestShot(0, 16.f / 9.f, At, RC, C0, L0, 198.f / 178.f, 0.f);
		BoxFx::RestShot(0, 16.f / 9.f, At, RC, C1, L1, 198.f / 178.f, 1.f);
		TestTrue(TEXT("сел: взгляд ниже"), L1.Z < L0.Z);
		TestTrue(TEXT("сел: камера не дальше"), FVector::Dist2D(C1, At) <= FVector::Dist2D(C0, At) + 0.1f);
	}
	return true;
}

#endif
