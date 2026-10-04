// S-75 (game feel): защита читается глазами — плотный блок, удар в блок, пробитый блок, нырок поверх клипа.
// Чистая логика (FBoxerGuardState, BoxerFeel::SlipPose) — тест BoxingUE.BoxerDefense; применение к позе —
// FBoxerPoseFx::ApplyGuard (зовётся из FBoxerPoseFx::Apply, BoxerFeel.cpp). Только визуал: ядро не трогается.
#include "BoxerFeel.h"

#include "BonePose.h"
#include "BoneContainer.h"
#include "TwoBoneIK.h"

namespace BoxDefensePriv
{
	bool DOk(FCompactPoseBoneIndex I) { return I.GetInt() != INDEX_NONE; }

	FTransform DCS(const FCompactPose& P, FCompactPoseBoneIndex I)
	{
		FTransform T = P[I];
		FCompactPoseBoneIndex Par = P.GetParentBoneIndex(I);
		while (Par.GetInt() != INDEX_NONE)
		{
			T = T * P[Par];
			Par = P.GetParentBoneIndex(Par);
		}
		return T;
	}

	FQuat DParentRotCS(const FCompactPose& P, FCompactPoseBoneIndex I)
	{
		const FCompactPoseBoneIndex Par = P.GetParentBoneIndex(I);
		return Par.GetInt() != INDEX_NONE ? DCS(P, Par).GetRotation() : FQuat::Identity;
	}

	void DRotateCS(FCompactPose& P, FCompactPoseBoneIndex I, const FQuat& D)
	{
		if (!DOk(I))
		{
			return;
		}
		const FQuat Qp = DParentRotCS(P, I);
		P[I].SetRotation((Qp.Inverse() * D * Qp * P[I].GetRotation()).GetNormalized());
	}

	FQuat DTurn(const FVector& From, const FVector& To, float Angle)
	{
		if (FMath::Abs(Angle) < 1e-5f)
		{
			return FQuat::Identity;
		}
		const FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		return Axis.IsNearlyZero() ? FQuat::Identity : FQuat(Axis, Angle);
	}

	float DSmooth(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / FMath::Max(1e-4f, B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	// Перчатки в блоке (пространство компонента, см) — от кости головы (уровень челюсти): кость кисти перед лицом,
	// перчатка закрывает щёку и подбородок. Передняя рука чуть дальше.
	constexpr float GUARD_FWD_CM = 17.f;
	constexpr float GUARD_LEAD_FWD_CM = 3.f;
	constexpr float GUARD_SIDE_CM = 9.f;
	constexpr float GUARD_UP_CM = -4.f;
	constexpr float GUARD_MIN_FWD_CM = 11.f; // вдавленная ударом перчатка не уходит в лицо
	constexpr float GUARD_SAG_DOWN_CM = 10.f; // руки устают — перчатки ниже (открыт подбородок)
	constexpr float GUARD_SHRUG = 0.14f;      // плечи вверх (рад ключицы)
	constexpr float GUARD_CHIN = 0.08f;       // подбородок вниз (рад головы)
	// Пробит: руки в стороны от плеч.
	constexpr float BREAK_SIDE_CM = 30.f;
	constexpr float BREAK_FWD_CM = 18.f;
	constexpr float BREAK_DOWN_CM = 8.f;
	// Пружина «вдавило в лицо» — критическое демпфирование, без отскока.
	constexpr float PUSH_W = 22.f;
}
using namespace BoxDefensePriv;

// ---------------------------------------------------------------------------------------------
// Чистая логика
// ---------------------------------------------------------------------------------------------

BoxerFeel::FSlipPose BoxerFeel::SlipPose(float Slip)
{
	FSlipPose P;
	const float S = FMath::Clamp(Slip, -1.f, 1.f);
	const float A = FMath::Abs(S);
	// Влево (S < 0): макушка влево (Roll < 0), таз влево (SideCm > 0), корпус чуть вперёд, колени подсели.
	P.Roll = 0.24f * S;
	P.Bend = 0.1f * A;
	P.SideCm = -7.f * S;
	P.Knee = 0.18f * A;
	return P;
}

void FBoxerGuardState::Update(float Dt, bool bGuardUp, float Integrity)
{
	if (Dt <= 0.f)
	{
		return; // хит-стоп: поза стоит
	}
	Dt = FMath::Min(Dt, 1.f / 15.f);
	const float Want = bGuardUp ? 1.f : 0.f;
	const float Rate = Want > W ? 1.f / RAISE_S : 1.f / LOWER_S;
	W = Want > W ? FMath::Min(Want, W + Rate * Dt) : FMath::Max(Want, W - Rate * Dt);
	// Руки устают: целостность ниже 0.6 — перчатки опускаются (к 0.1 — полностью).
	const float SagWant = bGuardUp ? FMath::Clamp((0.6f - Integrity) / 0.5f, 0.f, 1.f) : 0.f;
	Sag += (SagWant - Sag) * FMath::Min(1.f, 6.f * Dt);
	// Пружина вдавленных перчаток — критическое демпфирование, точное решение (Эйлер срезал пик на четверть).
	{
		const float E = FMath::Exp(-PUSH_W * Dt);
		const float B = PushV + PUSH_W * Push;
		Push = (Push + B * Dt) * E;
		PushV = (PushV - PUSH_W * B * Dt) * E;
	}
	if (Push < 0.f)
	{
		Push = 0.f;
		PushV = FMath::Max(0.f, PushV);
	}
	Push = FMath::Min(Push, PUSH_MAX_CM);
	if (Push < 1e-3f && FMath::Abs(PushV) < 1e-2f)
	{
		Push = PushV = 0.f;
	}
	if (BreakT >= 0.f)
	{
		BreakT += Dt;
		if (BreakT > BREAK_S)
		{
			BreakT = -1.f;
		}
	}
}

void FBoxerGuardState::OnBlocked(float Mag)
{
	// Пик вдавливания 3..7 см по силе удара; у критически демпфированной пружины пик = v / (w·e).
	const float Peak = FMath::Clamp(3.f + 8.f * FMath::Max(0.f, Mag), 3.f, PUSH_MAX_CM);
	PushV = FMath::Max(PushV, Peak * PUSH_W * UE_EULERS_NUMBER);
}

void FBoxerGuardState::OnGuardBreak()
{
	BreakT = 0.f;
	Push = PushV = 0.f;
}

float FBoxerGuardState::BreakW() const
{
	if (BreakT < 0.f)
	{
		return 0.f;
	}
	return DSmooth(0.f, 0.08f, BreakT) * (1.f - DSmooth(0.35f, BREAK_S, BreakT));
}

// ---------------------------------------------------------------------------------------------
// Применение к позе
// ---------------------------------------------------------------------------------------------

void FBoxerPoseFx::ApplyGuard(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld)
{
	const float Bw = FMath::Clamp(Frame.GuardBreakW, 0.f, 1.f);
	const float Gw = FMath::Clamp(Frame.GuardW, 0.f, 1.f) * (1.f - Bw);
	if ((Gw < 1e-3f && Bw < 1e-3f) || !DOk(Head))
	{
		return;
	}
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Frame.Fwd).GetSafeNormal();
	const FVector Rt = CompToWorld.InverseTransformVectorNoScale(Frame.Right).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();

	// Плечи вверх (ключица поднимает плечо), подбородок вниз — за перчатки.
	for (int32 S = 0; S < 2; ++S)
	{
		if (DOk(Clav[S]) && DOk(UpperArm[S]))
		{
			const FVector D = (DCS(Pose, UpperArm[S]).GetLocation() - DCS(Pose, Clav[S]).GetLocation()).GetSafeNormal();
			DRotateCS(Pose, Clav[S], DTurn(D, U, GUARD_SHRUG * Gw));
		}
	}
	DRotateCS(Pose, Head, DTurn(U, F, GUARD_CHIN * Gw));

	const FVector Hd = DCS(Pose, Head).GetLocation();
	const int32 Lead = Frame.bFeetSouthpaw ? 1 : 0;
	const float Fwd = FMath::Max(GUARD_MIN_FWD_CM, GUARD_FWD_CM + 3.f * Frame.GuardSag - Frame.GuardPushCm);
	for (int32 S = 0; S < 2; ++S)
	{
		if (!DOk(UpperArm[S]) || !DOk(LowerArm[S]) || !DOk(Hand[S]))
		{
			continue;
		}
		const float Side = S == 0 ? -1.f : 1.f; // левая — влево от лица
		const FTransform UcS = DCS(Pose, UpperArm[S]);
		const FTransform LcS = DCS(Pose, LowerArm[S]);
		const FVector Sh = UcS.GetLocation();
		const FVector E = LcS.GetLocation();
		const FVector H = DCS(Pose, Hand[S]).GetLocation();
		const float LenU = FVector::Dist(Sh, E);
		const float LenL = FVector::Dist(E, H);
		if (LenU < 1.f || LenL < 1.f)
		{
			continue;
		}
		const FVector GuardT = Hd + F * (Fwd + (S == Lead ? GUARD_LEAD_FWD_CM : 0.f)) + Rt * (Side * GUARD_SIDE_CM) +
			U * (GUARD_UP_CM - GUARD_SAG_DOWN_CM * Frame.GuardSag);
		const FVector BreakT = Sh + Rt * (Side * BREAK_SIDE_CM) + F * BREAK_FWD_CM - U * BREAK_DOWN_CM;
		FVector Goal = FMath::Lerp(H, GuardT, Gw);
		Goal = FMath::Lerp(Goal, BreakT, Bw);

		// Локоть: клип → в блоке вниз и к корпусу (к средней линии) → пробит — наружу.
		const FVector Mid = (Sh + H) * 0.5f;
		const FVector ClipPole = (E - Mid).SizeSquared() > 4.f ? E + (E - Mid).GetSafeNormal() * 30.f : E - U * 30.f;
		const FVector GuardPole = (Sh + Goal) * 0.5f - U * 40.f + F * 6.f - Rt * (Side * 6.f);
		const FVector BreakPole = Sh + Rt * (Side * 30.f) - U * 30.f;
		FVector Pole = FMath::Lerp(ClipPole, GuardPole, Gw);
		Pole = FMath::Lerp(Pole, BreakPole, Bw);

		FVector NewE, NewH;
		AnimationCore::SolveTwoBoneIK(Sh, E, H, Pole, Goal, NewE, NewH, LenU, LenL, false, 1.0, 1.0);
		const FQuat Qu = FQuat::FindBetweenNormals((E - Sh).GetSafeNormal(), (NewE - Sh).GetSafeNormal());
		const FQuat NewU = (Qu * UcS.GetRotation()).GetNormalized();
		const FQuat Ql = FQuat::FindBetweenNormals(Qu.RotateVector(H - E).GetSafeNormal(), (NewH - NewE).GetSafeNormal());
		const FQuat NewL = (Ql * Qu * LcS.GetRotation()).GetNormalized();
		Pose[UpperArm[S]].SetRotation((DParentRotCS(Pose, UpperArm[S]).Inverse() * NewU).GetNormalized());
		Pose[LowerArm[S]].SetRotation((DParentRotCS(Pose, LowerArm[S]).Inverse() * NewL).GetNormalized());
	}
}

// ---------------------------------------------------------------------------------------------
// S-76/S-75: клинч
// ---------------------------------------------------------------------------------------------

void FBoxerPoseFx::ApplyClinch(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld)
{
	const float W = FMath::Clamp(Frame.ClinchW, 0.f, 1.f);
	if (W < 1e-3f || !DOk(Pelvis) || !DOk(Head))
	{
		return;
	}
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Frame.Fwd).GetSafeNormal();
	const FVector Rt = CompToWorld.InverseTransformVectorNoScale(Frame.Right).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const FVector L = -Rt;
	// Навалился: таз к сопернику, корпус вперёд, голова вбок (влево — его правая сторона: головы расходятся, щека к щеке).
	constexpr float CLINCH_PELVIS_CM = 14.f;
	constexpr float CLINCH_LEAN = 0.3f;
	constexpr float CLINCH_HEAD_YAW = 0.45f;
	constexpr float CLINCH_HEAD_SIDE = 0.12f;
	const FQuat Qp = DParentRotCS(Pose, Pelvis);
	Pose[Pelvis].AddToTranslation(Qp.UnrotateVector(F * (CLINCH_PELVIS_CM * W)));
	const float Split[3] = {0.4f, 0.35f, 0.25f};
	for (int32 I = 0; I < 3; ++I)
	{
		DRotateCS(Pose, Spine[I], DTurn(U, F, CLINCH_LEAN * W * Split[I]) * DTurn(U, L, CLINCH_HEAD_SIDE * W * Split[I]));
	}
	DRotateCS(Pose, Head, DTurn(F, L, CLINCH_HEAD_YAW * W));

	// Руки: левая — сверху за его правое плечо, правая — снизу под его левую руку (оба бойца так — «over-under» сходится).
	const FVector Goals[2] = {CompToWorld.InverseTransformPosition(Frame.ClinchOver), CompToWorld.InverseTransformPosition(Frame.ClinchUnder)};
	for (int32 S = 0; S < 2; ++S)
	{
		if (!DOk(UpperArm[S]) || !DOk(LowerArm[S]) || !DOk(Hand[S]))
		{
			continue;
		}
		const float Side = S == 0 ? -1.f : 1.f;
		const FTransform UcS = DCS(Pose, UpperArm[S]);
		const FTransform LcS = DCS(Pose, LowerArm[S]);
		const FVector Sh = UcS.GetLocation();
		const FVector E = LcS.GetLocation();
		const FVector H = DCS(Pose, Hand[S]).GetLocation();
		const float LenU = FVector::Dist(Sh, E);
		const float LenL = FVector::Dist(E, H);
		if (LenU < 1.f || LenL < 1.f)
		{
			continue;
		}
		const FVector Goal = FMath::Lerp(H, Goals[S], W);
		// Локоть: сверху — наружу и вверх (рука через его плечо), снизу — наружу и вниз (под его рукой).
		const FVector Pole = Sh + Rt * (Side * 35.f) + (S == 0 ? U * 5.f : -U * 30.f);
		FVector NewE, NewH;
		AnimationCore::SolveTwoBoneIK(Sh, E, H, FMath::Lerp(E + (E - (Sh + H) * 0.5f).GetSafeNormal() * 30.f, Pole, W), Goal, NewE, NewH, LenU, LenL, false, 1.0, 1.0);
		const FQuat Qu = FQuat::FindBetweenNormals((E - Sh).GetSafeNormal(), (NewE - Sh).GetSafeNormal());
		const FQuat NewU = (Qu * UcS.GetRotation()).GetNormalized();
		const FQuat Ql = FQuat::FindBetweenNormals(Qu.RotateVector(H - E).GetSafeNormal(), (NewH - NewE).GetSafeNormal());
		const FQuat NewL = (Ql * Qu * LcS.GetRotation()).GetNormalized();
		Pose[UpperArm[S]].SetRotation((DParentRotCS(Pose, UpperArm[S]).Inverse() * NewU).GetNormalized());
		Pose[LowerArm[S]].SetRotation((DParentRotCS(Pose, LowerArm[S]).Inverse() * NewL).GetNormalized());
	}
}
