// Автотест S-78 «тела не проходят друг в друга»: выталкивание перчатки из шара/капсулы, раздвижка голов поровну (вместе —
// ровно до касания, без обратной связи), трекер тела соперника (упреждение не дальше предела, плавный вес).
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -ExecCmds="Automation RunTests BoxingUE.BoxerContact;Quit"
#include "BoxerFeel.h"
#include "FightReferee.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxerContactTest, "BoxingUE.BoxerContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoxerContactTest::RunTest(const FString& Parameters)
{
	using namespace BoxerFeel;
	const FVector Back(-1.f, 0.f, 0.f);

	// --- шар: снаружи — ноль; внутри — ровно на поверхность, по радиусу ---
	{
		const FVector C(100.f, 0.f, 150.f);
		TestTrue(TEXT("шар: снаружи не трогаем"), PushOutSphere(C + FVector(-20.f, 0.f, 0.f), 7.f, C, 10.f, Back).IsNearlyZero());
		const FVector P = C + FVector(-9.f, 3.f, 0.f);
		const FVector D = PushOutSphere(P, 7.f, C, 10.f, Back);
		TestTrue(TEXT("шар: вытолкнут на поверхность"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(P + D, C)), 17.f, 0.01f));
		TestTrue(TEXT("шар: по радиусу (направление сохраняется)"), FVector::DotProduct(D.GetSafeNormal(), (P - C).GetSafeNormal()) > 0.999f);
		const FVector D0 = PushOutSphere(C, 7.f, C, 10.f, Back);
		TestTrue(TEXT("шар: центр в центре — к себе (Fallback)"), (C + D0).Equals(C + Back * 17.f, 0.01f));
	}
	// --- капсула: ближайшая точка оси, концы — как шары ---
	{
		const FVector A(0.f, 0.f, 100.f), B(0.f, 0.f, 150.f);
		const FVector P(10.f, 0.f, 120.f);
		const FVector D = PushOutCapsule(P, 7.f, A, B, 13.f, Back);
		TestTrue(TEXT("капсула: перчатка в груди — до 20 см от оси"), FMath::IsNearlyEqual(static_cast<float>((P + D).Size2D()), 20.f, 0.01f) && FMath::IsNearlyZero(static_cast<float>(D.Z), 0.01f));
		TestTrue(TEXT("капсула: выше груди — от конца"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(FVector(5.f, 0.f, 160.f) + PushOutCapsule(FVector(5.f, 0.f, 160.f), 7.f, A, B, 13.f, Back), B)), 20.f, 0.01f));
		TestTrue(TEXT("капсула: снаружи не трогаем"), PushOutCapsule(FVector(25.f, 0.f, 120.f), 7.f, A, B, 13.f, Back).IsNearlyZero());
	}
	// --- головы: каждый берёт половину — вместе ровно Want; по горизонтали, вертикаль не трогаем ---
	{
		const FVector Ha(0.f, 0.f, 160.f), Hb(12.f, 4.f, 166.f);
		const float Want = 22.f;
		const FVector Da = HeadSeparation(Ha, Hb, Want, 0.5f, Back);
		const FVector Db = HeadSeparation(Hb, Ha, Want, 0.5f, -Back);
		TestTrue(TEXT("головы: вместе — ровно до касания"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(Ha + Da, Hb + Db)), Want, 0.05f));
		TestTrue(TEXT("головы: поровну и в разные стороны"), FMath::IsNearlyEqual(static_cast<float>(Da.Size()), static_cast<float>(Db.Size()), 0.01f) && FVector::DotProduct(Da, Db) < 0.f);
		TestTrue(TEXT("головы: только по горизонтали"), FMath::IsNearlyZero(static_cast<float>(Da.Z)) && FMath::IsNearlyZero(static_cast<float>(Db.Z)));
		TestTrue(TEXT("головы: далеко — ноль"), HeadSeparation(Ha, Ha + FVector(30.f, 0.f, 0.f), Want, 0.5f, Back).IsNearlyZero());
		// Одна над другой (разнесены по высоте больше Want) — горизонталь не нужна.
		TestTrue(TEXT("головы: по высоте уже разошлись"), HeadSeparation(Ha, Ha + FVector(2.f, 0.f, 25.f), Want, 0.5f, Back).IsNearlyZero());
		// Совпали по горизонтали — Fallback (к себе).
		const FVector Df = HeadSeparation(Ha, Ha + FVector(0.f, 0.f, 5.f), Want, 0.5f, Back);
		TestTrue(TEXT("головы: друг над другом — по Fallback"), Df.X < -5.f && FMath::IsNearlyZero(static_cast<float>(Df.Y)));
	}
	// --- трекер тела: упреждение на кадр не дальше предела, вес плавный, пропало тело — гаснет сразу ---
	{
		FBoxerBodyTrack T;
		FBoxerFeelFrame F;
		FBoxerFeelDebug D;
		D.bBody = true;
		D.BodyHeadC = FVector(0.f, 0.f, 160.f);
		D.BodyHeadBone = FVector(0.f, 0.f, 152.f);
		D.BodyPelvis = FVector(0.f, 0.f, 95.f);
		D.BodyChest = FVector(0.f, 0.f, 140.f);
		D.BodyHeadPre = D.BodyHeadC;
		D.BodyHeadBonePre = D.BodyHeadBone;
		const float Dt = 1.f / 60.f;
		T.Update(D, true, Dt, F);
		TestTrue(TEXT("трекер: вес набирается не сразу"), F.bBody && F.SepW > 0.f && F.SepW < 0.2f);
		TestTrue(TEXT("трекер: первый кадр — без упреждения"), F.OppHeadC.Equals(D.BodyHeadC, 0.01f));
		D.BodyHeadC += FVector(3.f, 0.f, 0.f);
		T.Update(D, true, Dt, F);
		TestTrue(TEXT("трекер: упреждение на кадр вперёд"), F.OppHeadC.Equals(D.BodyHeadC + FVector(3.f, 0.f, 0.f), 0.01f));
		T.Update(D, true, Dt, F); // повторная оценка той же позы (хит-стоп) — скорость не обнуляется
		TestTrue(TEXT("трекер: повтор кадра скорость не сбрасывает"), F.OppHeadC.Equals(D.BodyHeadC + FVector(3.f, 0.f, 0.f), 0.01f));
		D.BodyHeadC += FVector(20.f, 0.f, 0.f);
		T.Update(D, true, Dt, F);
		TestTrue(TEXT("трекер: упреждение не дальше предела"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist(F.OppHeadC, D.BodyHeadC)), FBoxerBodyTrack::PREDICT_MAX_CM, 0.01f));
		for (int32 I = 0; I < 30; ++I) T.Update(D, true, Dt, F);
		TestEqual(TEXT("трекер: вес набран за ~0.2 с"), F.SepW, 1.f);
		T.Update(D, false, Dt, F);
		TestTrue(TEXT("трекер: выкл. (лёжа) — гаснет плавно"), F.SepW > 0.85f && F.SepW < 1.f);
		D.bBody = false;
		T.Update(D, true, Dt, F);
		TestTrue(TEXT("трекер: тела нет — выкл."), !F.bBody && F.HandPushW == 0.f);
	}
	// --- рефери: поворот головы относительно груди (S-78 №8) ---
	{
		const FVector Up(0.f, 0.f, 1.f), F(1.f, 0.f, 0.f);
		const float T = BoxRefPose::TwistAbout(Up, F, FVector(0.f, 1.f, 0.3f));
		TestTrue(TEXT("рефери: поворот вокруг оси груди — 90°"), FMath::IsNearlyEqual(FMath::Abs(T), HALF_PI, 1e-3f));
		TestTrue(TEXT("рефери: знак — как у FQuat(ось, угол)"), FMath::IsNearlyEqual(BoxRefPose::TwistAbout(Up, F, FQuat(Up, 0.7f).RotateVector(F)), 0.7f, 1e-3f));
		TestEqual(TEXT("рефери: в пределах — не трогаем"), BoxRefPose::TwistExcess(0.5f * BoxRefPose::HEAD_TWIST_MAX, BoxRefPose::HEAD_TWIST_MAX), 0.f);
		TestTrue(TEXT("рефери: 150° — снимаем излишек до предела"), FMath::IsNearlyEqual(-2.6f - BoxRefPose::TwistExcess(-2.6f, BoxRefPose::HEAD_TWIST_MAX), -BoxRefPose::HEAD_TWIST_MAX, 1e-4f));
		TestTrue(TEXT("рефери: предел ≤ 80°"), BoxRefPose::HEAD_TWIST_MAX <= FMath::DegreesToRadians(80.f));
		// Таз: «стоя» растёт сразу, тает медленно; присед GASP на 9 см держится в пределах PELVIS_SAG_MAX.
		float St = 92.f;
		for (int32 I = 0; I < 18; ++I) St = BoxRefPose::StandHeight(St, 83.f, 1.f / 60.f);
		TestTrue(TEXT("рефери: «стоя» за 0.3 с приседа почти не опустилась"), St > 91.f);
		TestTrue(TEXT("рефери: присед 9 см — таз не ниже стоя − 3.5"), FMath::IsNearlyEqual(BoxRefPose::HeldPelvisZ(St, 83.f, BoxRefPose::PELVIS_SAG_MAX), St - BoxRefPose::PELVIS_SAG_MAX, 1e-3f));
		TestEqual(TEXT("рефери: выше — не трогаем"), BoxRefPose::HeldPelvisZ(St, 95.f, BoxRefPose::PELVIS_SAG_MAX), 95.f);
		TestEqual(TEXT("рефери: встал выше — «стоя» сразу"), BoxRefPose::StandHeight(St, 96.f, 1.f / 60.f), 96.f);
	}
	return true;
}

#endif
