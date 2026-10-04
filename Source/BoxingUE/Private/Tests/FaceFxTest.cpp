// Автотест S-74: мимика и повреждения лица (FaceFx.h) — повреждения как web damage.test (порт damage.ts), мимика:
// гримаса по силе (слабое — без гримасы), сжатые зубы на своём силовом и выдох после контакта, усталость по стамине,
// «поплыл» после тяжёлого и нокдауна, отёк закрывает глаз своей стороны; каналы → кривые RigLogic.
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.FaceFx;Quit"
#include "FaceFx.h"
#include "ImpactSpray.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using EP = FBoxerFaceDamage::EPunch;

	void Run(FBoxerFaceFx& F, float Sec, const FBoxerFaceInput& In)
	{
		for (float T = 0.f; T < Sec; T += 1.f / 60.f)
		{
			F.Update(1.f / 60.f, In);
		}
	}

	float Peak(FBoxerFaceFx& F, float Sec, const FBoxerFaceInput& In, EBoxFaceCh Ch)
	{
		float M = 0.f;
		for (float T = 0.f; T < Sec; T += 1.f / 60.f)
		{
			F.Update(1.f / 60.f, In);
			M = FMath::Max(M, F.Frame()[Ch]);
		}
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxFaceFxTest, "BoxingUE.FaceFx", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxFaceFxTest::RunTest(const FString& Parameters)
{
	// --- повреждения (damage.ts) ---
	{
		FBoxerFaceDamage D;
		D.ApplyHit(1.25f, EP::Jab, false, false, false); // передняя рука (bRear = false)
		TestTrue(TEXT("урон: голова краснеет"), D.Head > 0.f);
		TestTrue(TEXT("урон: передняя рука — правая половина лица"), D.EyeR > 0.f && D.EyeL == 0.f && D.CheekR > 0.f);
		FBoxerFaceDamage B;
		B.ApplyHit(1.25f, EP::Hook, true, true, false);
		TestTrue(TEXT("урон: хук в корпус — бок, лицо не трогает"), B.BodyL > 0.f && B.Head == 0.f && B.EyeL == 0.f);
		FBoxerFaceDamage Bl;
		Bl.ApplyHit(2.f, EP::Cross, true, false, true);
		TestTrue(TEXT("урон: блок — лишь лёгкое покраснение"), Bl.Head > 0.f && Bl.Head < 0.02f && Bl.EyeL == 0.f);
		// Рассечение — только по набитому глазу и сильным кроссом/хуком.
		FBoxerFaceDamage C;
		C.ApplyHit(2.5f, EP::Hook, true, false, false);
		TestEqual(TEXT("урон: свежий глаз — без рассечения"), C.CutL, 0.f);
		for (int32 I = 0; I < 6; ++I) { C.ApplyHit(2.5f, EP::Hook, true, false, false); }
		TestTrue(TEXT("урон: набитый глаз — рассечение"), C.CutL > 0.f);
		const float Nose = C.Nose, Eye = C.EyeL, Cut = C.CutL;
		C.ApplyKnockdown();
		TestTrue(TEXT("урон: нокдаун добавляет"), C.Nose > Nose && C.Head > 0.15f);
		const float Nose2 = C.Nose;
		C.BetweenRounds();
		TestTrue(TEXT("урон: перерыв — кровь подчищена, отёк и рассечение остаются"), C.Nose < Nose2 && C.EyeL >= Eye - 0.021f && C.CutL == Cut);
		FBoxerFaceDamage M;
		for (int32 I = 0; I < 500; ++I) { M.ApplyHit(3.f, EP::Cross, false, false, false); }
		TestTrue(TEXT("урон: не больше 1"), M.Max() <= 1.f);
	}

	FBoxerFaceInput Calm;
	// --- гримаса: слабое — нет, сильное — сильнее среднего ---
	{
		FBoxerFaceFx F;
		Run(F, 0.5f, Calm);
		F.OnHit(0.3f, false, false);
		const float Weak = Peak(F, 0.3f, Calm, EBoxFaceCh::BrowDown);
		FBoxerFaceFx F2;
		Run(F2, 0.5f, Calm);
		F2.OnHit(0.9f, false, false);
		const float Mid = Peak(F2, 0.3f, Calm, EBoxFaceCh::BrowDown);
		FBoxerFaceFx F3;
		Run(F3, 0.5f, Calm);
		F3.OnHit(2.f, false, false);
		const float Hard = Peak(F3, 0.3f, Calm, EBoxFaceCh::BrowDown);
		AddInfo(FString::Printf(TEXT("гримаса (брови): слабый %.2f, средний %.2f, тяжёлый %.2f"), Weak, Mid, Hard));
		TestTrue(TEXT("гримаса: слабый удар — лицо спокойно"), Weak < 0.05f);
		TestTrue(TEXT("гримаса: тяжёлый > средний > 0"), Hard > Mid && Mid > 0.1f);
		TestTrue(TEXT("тяжёлый в голову — поплыл"), F3.Daze() > 0.3f);
		TestTrue(TEXT("средний — не поплыл"), F2.Daze() < 0.05f);
		Run(F3, 1.2f, Calm);
		TestTrue(TEXT("гримаса гаснет за ~1 с"), F3.Frame()[EBoxFaceCh::BrowDown] < 0.1f + 0.15f * F3.Tired());
		Run(F3, 4.f, Calm);
		TestTrue(TEXT("поплыл проходит"), F3.Daze() < 0.05f);
		// В корпус — сжатые зубы, в голову — зажмурился.
		FBoxerFaceFx Fb;
		Run(Fb, 0.5f, Calm);
		Fb.OnHit(1.5f, true, false);
		TestTrue(TEXT("в корпус: сжатые зубы"), Peak(Fb, 0.3f, Calm, EBoxFaceCh::Clench) > 0.4f);
		FBoxerFaceFx Fh;
		Run(Fh, 0.5f, Calm);
		Fh.OnHit(1.5f, false, false);
		TestTrue(TEXT("в голову: зажмурился"), Peak(Fh, 0.3f, Calm, EBoxFaceCh::BlinkL) > 0.5f);
		// Блок мягче попадания.
		FBoxerFaceFx Fk;
		Run(Fk, 0.5f, Calm);
		Fk.OnHit(1.5f, false, true);
		TestTrue(TEXT("блок мягче попадания"), Peak(Fk, 0.3f, Calm, EBoxFaceCh::BrowDown) < Hard * 0.8f);
	}

	// --- свой силовой: сжатые зубы до контакта, выдох после ---
	{
		FBoxerFaceFx F;
		Run(F, 0.5f, Calm);
		FBoxerFaceInput P = Calm;
		P.bPunching = true;
		P.bPowerPunch = true;
		P.ContactFrac = 0.5f;
		float ClenchBefore = 0.f, FunnelAfter = 0.f;
		for (int32 I = 0; I <= 30; ++I)
		{
			P.PunchPhase = I / 30.f;
			F.Update(1.f / 60.f, P);
			if (P.PunchPhase < 0.5f) ClenchBefore = FMath::Max(ClenchBefore, F.Frame()[EBoxFaceCh::Clench]);
			else FunnelAfter = FMath::Max(FunnelAfter, F.Frame()[EBoxFaceCh::Funnel]);
		}
		TestTrue(*FString::Printf(TEXT("силовой: зубы сжаты до контакта (%.2f)"), ClenchBefore), ClenchBefore > 0.4f);
		TestTrue(*FString::Printf(TEXT("силовой: выдох после контакта (%.2f)"), FunnelAfter), FunnelAfter > 0.3f);
		FBoxerFaceFx J;
		Run(J, 0.5f, Calm);
		P.bPowerPunch = false;
		float JabClench = 0.f;
		for (int32 I = 0; I <= 30; ++I)
		{
			P.PunchPhase = I / 30.f;
			J.Update(1.f / 60.f, P);
			JabClench = FMath::Max(JabClench, J.Frame()[EBoxFaceCh::Clench]);
		}
		TestTrue(TEXT("джеб: лицо не напрягается"), JabClench < 0.05f);
	}

	// --- усталость: рот приоткрыт и «дышит» ---
	{
		FBoxerFaceFx Fresh, Tired;
		FBoxerFaceInput T = Calm;
		T.Stamina = 0.1f;
		Run(Fresh, 3.f, Calm);
		Run(Tired, 3.f, T);
		float Lo = 1.f, Hi = 0.f;
		for (int32 I = 0; I < 120; ++I)
		{
			Tired.Update(1.f / 60.f, T);
			Lo = FMath::Min(Lo, Tired.Frame()[EBoxFaceCh::JawOpen]);
			Hi = FMath::Max(Hi, Tired.Frame()[EBoxFaceCh::JawOpen]);
		}
		TestTrue(TEXT("свежий: рот закрыт"), Fresh.Frame()[EBoxFaceCh::JawOpen] < 0.02f);
		TestTrue(*FString::Printf(TEXT("выдохся: рот приоткрыт (%.2f..%.2f) и дышит"), Lo, Hi), Hi > 0.2f && Hi - Lo > 0.1f);
	}

	// --- нокдаун: поплыл, отёк закрывает свой глаз ---
	{
		FBoxerFaceFx F;
		F.OnKnockdown();
		FBoxerFaceInput D = Calm;
		D.bDown = true;
		Run(F, 0.5f, D);
		TestTrue(TEXT("нокдаун: поплыл"), F.Daze() > 0.9f);
		FBoxerFaceFx E;
		E.Damage.EyeL = 1.f;
		Run(E, 0.3f, Calm);
		TestTrue(TEXT("отёк левого: левый глаз прикрыт, правый открыт"), E.Frame()[EBoxFaceCh::BlinkL] > 0.5f && E.Frame()[EBoxFaceCh::BlinkR] < 0.5f);
	}

	// --- брызги пота (impactBurst веба): слабое — ни капли, тяжёлое — больше и быстрее, блок — пара капель ---
	{
		using namespace BoxSpray;
		TestEqual(TEXT("брызги: слабый джеб — ни капли"), ImpactBurst(0.3f, EKind::Head).Drops, 0);
		const FBurst Mid = ImpactBurst(0.9f, EKind::Head), Hard = ImpactBurst(2.f, EKind::Head), Body = ImpactBurst(2.f, EKind::Body);
		TestTrue(TEXT("брызги: тяжёлый > средний"), Hard.Drops > Mid.Drops && Hard.Speed > Mid.Speed && Mid.Drops > 0);
		TestTrue(TEXT("брызги: корпус скромнее головы"), Body.Drops < Hard.Drops);
		const FBurst Blk = ImpactBurst(1.5f, EKind::Block);
		TestTrue(TEXT("брызги: блок — пара капель"), Blk.Drops > 0 && Blk.Drops <= 7);
	}

	// --- кривые: все каналы мапятся, фильтр по известным именам ---
	{
		FBoxerFaceMap All;
		All.Build(TSet<FName>());
		TestTrue(TEXT("кривые: все каналы без фильтра"), All.NumCurves() >= BOX_FACE_NUM && All.Missing.Num() == 0);
		TSet<FName> Known;
		Known.Add(FName(TEXT("CTRL_expressions_jawOpen")));
		FBoxerFaceMap One;
		One.Build(Known);
		TestEqual(TEXT("кривые: только известные"), One.NumCurves(), 1);
		FBoxerFaceFrame Fr;
		Fr.bOn = true;
		Fr[EBoxFaceCh::JawOpen] = 0.4f;
		FBoxerFaceCurves C;
		One.Fill(Fr, C);
		TestTrue(TEXT("кривые: значение доходит"), C.Num == 1 && FMath::IsNearlyEqual(C.Values[0], 0.4f));
	}
	return true;
}

#endif
