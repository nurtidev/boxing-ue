// Автотест S-75 «защита читается»: чистая логика рук блока (FBoxerGuardState), нырка (BoxerFeel::SlipPose), «провала»
// атакующего (ReactionKick Whiff), огибающей контры и подсказок защиты (BoxFx::DefenseCueFor).
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.BoxerDefense;Quit"
#include "BoxerFeel.h"
#include "FightFx.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxerDefenseTest, "BoxingUE.BoxerDefense", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxerDefenseTest::RunTest(const FString& Parameters)
{
	const float Dt = 1.f / 60.f;
	auto Run = [Dt](FBoxerGuardState& G, float Sec, bool bUp, float Integrity)
	{
		for (float T = 0.f; T < Sec - 1e-4f; T += Dt)
		{
			G.Update(Dt, bUp, Integrity);
		}
	};

	// --- блок: подъём/опускание ---
	{
		FBoxerGuardState G;
		Run(G, 0.05f, true, 1.f);
		TestTrue(TEXT("блок: за 0.05 с ещё поднимается"), G.W > 0.3f && G.W < 1.f);
		Run(G, 0.05f, true, 1.f);
		TestEqual(TEXT("блок: поднят за ~0.08 с"), G.W, 1.f);
		TestEqual(TEXT("свежий блок не проседает"), G.Sag, 0.f);
		Run(G, 0.1f, false, 1.f);
		TestTrue(TEXT("опускание медленнее подъёма"), G.W > 0.4f && G.W < 0.6f);
		Run(G, 0.15f, false, 1.f);
		TestEqual(TEXT("опущен за ~0.2 с"), G.W, 0.f);
		FBoxerGuardState H;
		Run(H, 1.f, true, 0.1f);
		TestTrue(TEXT("руки устали — перчатки ниже"), H.Sag > 0.95f);
		FBoxerGuardState Z = H;
		Z.Update(0.f, false, 1.f);
		TestEqual(TEXT("хит-стоп (dt 0) — поза стоит"), Z.W, H.W);
	}
	// --- удар в блок: перчатки вдавлены к лицу и возвращаются без отскока ---
	{
		FBoxerGuardState G;
		Run(G, 0.2f, true, 1.f);
		G.OnBlocked(0.1f);
		float Peak = 0.f, PeakT = 0.f, Min = 0.f;
		for (float T = 0.f; T < 0.6f; T += Dt)
		{
			G.Update(Dt, true, 1.f);
			if (G.Push > Peak)
			{
				Peak = G.Push;
				PeakT = T;
			}
			Min = FMath::Min(Min, G.Push);
		}
		TestTrue(TEXT("лёгкий в блок: пик ≈ 3.8 см"), FMath::Abs(Peak - 3.8f) < 0.6f);
		TestTrue(TEXT("пик быстро (< 0.08 с)"), PeakT < 0.08f);
		TestTrue(TEXT("без отскока"), Min >= 0.f);
		TestTrue(TEXT("вернулись к 0.6 с"), G.Push < 0.2f);
		FBoxerGuardState S;
		S.OnBlocked(5.f);
		float SP = 0.f;
		for (float T = 0.f; T < 0.2f; T += Dt)
		{
			S.Update(Dt, true, 1.f);
			SP = FMath::Max(SP, S.Push);
		}
		TestTrue(TEXT("силовой в блок — сильнее, не больше предела"), SP > Peak && SP <= FBoxerGuardState::PUSH_MAX_CM + 1e-3f);
	}
	// --- пробит: руки разлетаются ~0.6 с ---
	{
		FBoxerGuardState G;
		Run(G, 0.2f, true, 1.f);
		TestEqual(TEXT("без пробития — 0"), G.BreakW(), 0.f);
		G.OnGuardBreak();
		Run(G, 0.1f, false, 1.f);
		TestTrue(TEXT("пробит: руки в стороны сразу"), G.BreakW() > 0.9f);
		Run(G, 0.6f, false, 1.f);
		TestEqual(TEXT("через 0.7 с — снова в стойку"), G.BreakW(), 0.f);
	}
	// --- нырок ---
	{
		const BoxerFeel::FSlipPose L = BoxerFeel::SlipPose(-1.f);
		const BoxerFeel::FSlipPose R = BoxerFeel::SlipPose(1.f);
		const BoxerFeel::FSlipPose N = BoxerFeel::SlipPose(0.f);
		TestTrue(TEXT("нырок влево: макушка и таз влево"), L.Roll < 0.f && L.SideCm > 0.f);
		TestTrue(TEXT("нырок вправо: зеркально"), FMath::IsNearlyEqual(R.Roll, -L.Roll) && FMath::IsNearlyEqual(R.SideCm, -L.SideCm));
		TestTrue(TEXT("колени подсели, корпус вперёд — в обе стороны"), L.Knee > 0.1f && R.Knee == L.Knee && L.Bend > 0.f);
		TestTrue(TEXT("вне нырка — ноль"), N.Roll == 0.f && N.SideCm == 0.f && N.Knee == 0.f && N.Bend == 0.f);
		// Голова уходит с линии больше, чем радиус головы + кулака (≈ 11 + 6 см): крен 0.24 рад на ~55 см + таз 7 см.
		TestTrue(TEXT("голова уходит с линии > 17 см"), FMath::Abs(L.Roll) * 55.f + FMath::Abs(L.SideCm) > 17.f);
	}
	// --- «провалился» атакующий и удар в блок ---
	{
		using C = EBoxReactChannel;
		const FBoxReactKick W = BoxerFeel::ReactionKick(EBoxFeelEvent::Whiff, EBoxFeelPunch::Cross, true, false, 0.f, false);
		TestTrue(TEXT("провал: корпус вперёд, шаг вперёд"), W.bValid && W[C::TorsoPitch] < 0.f && W[C::Back] < 0.f);
		const FBoxReactKick J = BoxerFeel::ReactionKick(EBoxFeelEvent::Whiff, EBoxFeelPunch::Straight, false, false, 0.f, false);
		TestTrue(TEXT("джеб проваливает меньше кросса"), FMath::Abs(J[C::TorsoPitch]) < FMath::Abs(W[C::TorsoPitch]));
		const FBoxReactKick B = BoxerFeel::ReactionKick(EBoxFeelEvent::Block, EBoxFeelPunch::Cross, true, false, 0.4f, false);
		const FBoxReactKick H = BoxerFeel::ReactionKick(EBoxFeelEvent::Land, EBoxFeelPunch::Cross, true, false, 0.4f, false);
		TestTrue(TEXT("блок: голова не запрокидывается (в отличие от попадания)"), B[C::HeadPitch] <= 0.f && H[C::HeadPitch] > 0.1f);
		TestTrue(TEXT("блок: корпус сжимается (колени)"), B[C::Knee] > 0.f);
	}
	// --- контра: рука на линии раньше, контакт тот же ---
	{
		float A0, R0, A1, R1;
		BoxerFeel::PunchEnvelopes(0.25f, 0.5f, A0, R0, false);
		BoxerFeel::PunchEnvelopes(0.25f, 0.5f, A1, R1, true);
		TestTrue(TEXT("контра: доворот раньше"), A1 > A0 + 0.2f);
		BoxerFeel::PunchEnvelopes(0.5f, 0.5f, A0, R0, false);
		BoxerFeel::PunchEnvelopes(0.5f, 0.5f, A1, R1, true);
		TestTrue(TEXT("контра: в контакте — та же поверхность"), FMath::IsNearlyEqual(R0, 1.f) && FMath::IsNearlyEqual(R1, 1.f));
	}
	// --- подсказки защиты (setCue веба) ---
	{
		using namespace BoxFx;
		FFightEvent E;
		E.Kind = EFightEventKind::Slipped;
		E.Attacker = 1;
		E.Defender = 0;
		TestTrue(TEXT("мой нырок → «Уклон! Бей в ответ»"), DefenseCueFor(E, 0) == EDefCue::Slip);
		TestTrue(TEXT("нырок соперника — без подсказки"), DefenseCueFor(E, 1) == EDefCue::None);
		TestTrue(TEXT("автопилот (нет игрока) — без подсказок"), DefenseCueFor(E, -1) == EDefCue::None);
		TestEqual(TEXT("уклон держится контр-окно"), DefenseCueSeconds(EDefCue::Slip), COUNTER_WINDOW_S);
		E.Kind = EFightEventKind::Hit;
		E.Attacker = 0;
		E.Defender = 1;
		E.bCounter = true;
		TestTrue(TEXT("моя контра → «Контра!»"), DefenseCueFor(E, 0) == EDefCue::Counter);
		E.bCounter = false;
		E.bGuardBreak = true;
		TestTrue(TEXT("я пробил блок"), DefenseCueFor(E, 0) == EDefCue::Broke);
		TestTrue(TEXT("мой блок пробит"), DefenseCueFor(E, 1) == EDefCue::GuardBreak);
		E.bGuardBreak = false;
		E.bCaught = true;
		TestTrue(TEXT("пойман на нырке — защищающемуся"), DefenseCueFor(E, 1) == EDefCue::Caught && DefenseCueFor(E, 0) == EDefCue::None);
		E.bCaught = false;
		TestTrue(TEXT("обычное попадание — без подсказки"), DefenseCueFor(E, 0) == EDefCue::None);
		TestTrue(TEXT("тексты веба"), FString(DefenseCueText(EDefCue::GuardBreak)) == TEXT("Твой блок пробит!") &&
			FString(DefenseCueText(EDefCue::Caught)) == TEXT("Пойман на нырке"));
		TestEqual(TEXT("контра: стоп-кадр и на среднем"), CounterStopMs(0.f, true), COUNTER_STOP_MS);
		TestEqual(TEXT("не контра — как было"), CounterStopMs(0.f, false), 0.f);
		TestEqual(TEXT("тяжёлая контра — не короче своего"), CounterStopMs(45.f, true), 45.f);
	}
	return true;
}

#endif
