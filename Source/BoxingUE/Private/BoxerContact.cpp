// S-78: тела не проходят друг в друга (визуал, ядро не трогается). См. BoxerFeel.h.
//
// Плейтест QA UE-6: в ближнем бою головы входили одна в другую (masher: 8.7 см), кулак — в грудь (9 см) и в лицо (6–8 см),
// в том числе вне удара (гонг, клинч кончился). Наведение (S-41/S-62) упирает только БЬЮЩУЮ руку и только в «сырую» голову
// соперника (до его подшага и реакции), раздвижка VisMinSep — точки слежения, а не головы. Здесь:
//  * ApplySeparation — головы: каждый боец берёт ПОЛОВИНУ недостающего до (R + R + запас) по голове соперника ДО его
//    раздвижки (FBoxerFeelDebug::BodyHeadPre) — суммарно ровно до касания, без обратной связи и дрожи; сдвиг — наклоном
//    корпуса назад (до SEP_LEAN_MAX), остаток — тазом. Лицо в его корпус (нырок, подшаг в корпус) — целиком мой сдвиг.
//  * ApplyHandStop — обе перчатки (шар у кисти) выталкиваются из его головы (шар), шеи/затылка (шар у кости head) и
//    корпуса (капсула таз → spine_05) двухзвенной IK: удар упирается в поверхность, а не проходит насквозь.
// Тело соперника — его прошлый кадр анимпотока с упреждением на кадр (FBoxerBodyTrack, игровой поток).
#include "BoxerFeel.h"

#include "BonePose.h"
#include "BoneContainer.h"
#include "TwoBoneIK.h"

namespace BoxerContactImpl
{
	// Запас раздвижки голов (см): у соседа кадр назад, ступни (FBoxerFootIk) ещё чуть двигают таз после нас.
	constexpr float SEP_MARGIN_CM = 3.f;
	// Кость head ↔ кость head не ближе (см): центры голов по курсу могут разойтись, а затылки — сойтись (нырок, наклон).
	constexpr float HEAD_BONE_MIN_CM = 20.f;
	// Наклон корпуса на раздвижку (рад) и сдвиг таза сверх него (см).
	constexpr float SEP_LEAN_MAX = 0.32f;
	constexpr float SEP_PELVIS_MAX_CM = 25.f;
	constexpr float SEP_SPLIT[3] = {0.4f, 0.35f, 0.25f};
	// Перчатка — шар перед костью кисти (по предплечью), см мира: фронт = 3 + 7 = 10 (FistReachCm наведения).
	constexpr float GLOVE_FWD_CM = 3.f;
	constexpr float GLOVE_R_CM = 7.5f;
	// Шея/затылок — шар у кости head (центр головы — выше и впереди неё).
	constexpr float NECK_R_CM = 8.f;

	bool Ok(FCompactPoseBoneIndex I) { return I.GetInt() != INDEX_NONE; }

	FTransform CS(const FCompactPose& P, FCompactPoseBoneIndex I)
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

	FQuat ParentRotCS(const FCompactPose& P, FCompactPoseBoneIndex I)
	{
		const FCompactPoseBoneIndex Par = P.GetParentBoneIndex(I);
		return Par.GetInt() != INDEX_NONE ? CS(P, Par).GetRotation() : FQuat::Identity;
	}

	void RotateCS(FCompactPose& P, FCompactPoseBoneIndex I, const FQuat& D)
	{
		if (!Ok(I))
		{
			return;
		}
		const FQuat Qp = ParentRotCS(P, I);
		P[I].SetRotation((Qp.Inverse() * D * Qp * P[I].GetRotation()).GetNormalized());
	}

	// Центр головы (мир): над костью head и чуть вперёд по курсу — как AimCenterNow соперника.
	FVector HeadCenter(const FVector& HeadBone, const FVector& Fwd, float UpCm)
	{
		return HeadBone + FVector::UpVector * UpCm + Fwd * 2.f;
	}
}

// ---------------------------------------------------------------------------------------------
// Чистая геометрия (тест BoxingUE.BoxerContact)
// ---------------------------------------------------------------------------------------------

FVector BoxerFeel::PushOutSphere(const FVector& P, float Rp, const FVector& C, float R, const FVector& Fallback)
{
	const float Want = Rp + R;
	const FVector D = P - C;
	const float Len = D.Size();
	if (Len >= Want)
	{
		return FVector::ZeroVector;
	}
	const FVector N = Len > 0.01f ? D / Len : Fallback.GetSafeNormal();
	return N.IsNearlyZero() ? FVector::ZeroVector : C + N * Want - P;
}

FVector BoxerFeel::PushOutCapsule(const FVector& P, float Rp, const FVector& A, const FVector& B, float R, const FVector& Fallback)
{
	const FVector AB = B - A;
	const float L2 = AB.SizeSquared();
	const float T = L2 > 1e-4f ? FMath::Clamp(static_cast<float>(FVector::DotProduct(P - A, AB) / L2), 0.f, 1.f) : 0.f;
	return PushOutSphere(P, Rp, A + AB * T, R, Fallback);
}

FVector BoxerFeel::HeadSeparation(const FVector& Mine, const FVector& Theirs, float Want, float Share, const FVector& Fallback)
{
	const FVector D = Mine - Theirs;
	if (D.SizeSquared() >= Want * Want)
	{
		return FVector::ZeroVector;
	}
	const float Dz = static_cast<float>(D.Z);
	const float H = static_cast<float>(D.Size2D());
	const float HNeed = FMath::Sqrt(FMath::Max(0.f, Want * Want - Dz * Dz));
	const float Need = (HNeed - H) * Share;
	if (Need <= 0.f)
	{
		return FVector::ZeroVector;
	}
	FVector Dir = H > 0.5f ? FVector(D.X / H, D.Y / H, 0.f) : Fallback.GetSafeNormal2D();
	return Dir.IsNearlyZero() ? FVector::ZeroVector : Dir * Need;
}

void FBoxerBodyTrack::Update(const FBoxerFeelDebug& Opp, bool bOn, float Dt, FBoxerFeelFrame& Out)
{
	if (Opp.bBody)
	{
		const FVector New[N] = {Opp.BodyHeadC, Opp.BodyHeadBone, Opp.BodyPelvis, Opp.BodyChest, Opp.BodyHeadPre, Opp.BodyHeadBonePre};
		if (!bHave)
		{
			for (int32 I = 0; I < N; ++I)
			{
				Cur[I] = Prev[I] = New[I];
			}
			bHave = true;
		}
		else
		{
			// Новый кадр анимпотока (повторная оценка позы на паузе/хит-стопе даёт те же точки — скорость не обнуляем).
			bool bMoved = false;
			for (int32 I = 0; I < N && !bMoved; ++I)
			{
				bMoved = !New[I].Equals(Cur[I], 0.01f);
			}
			if (bMoved)
			{
				for (int32 I = 0; I < N; ++I)
				{
					Prev[I] = Cur[I];
					Cur[I] = New[I];
				}
			}
		}
	}
	else
	{
		bHave = false;
	}
	const float Target = bOn && bHave ? 1.f : 0.f;
	if (Dt > 0.f)
	{
		W = Target > W ? FMath::Min(Target, W + Dt * RATE) : FMath::Max(Target, W - Dt * RATE);
	}
	Out.bBody = bHave && W > 0.f;
	Out.SepW = Out.bBody ? W : 0.f;
	Out.HandPushW = Out.SepW;
	if (!Out.bBody)
	{
		return;
	}
	FVector P[N];
	for (int32 I = 0; I < N; ++I)
	{
		P[I] = Cur[I] + (Cur[I] - Prev[I]).GetClampedToMaxSize(PREDICT_MAX_CM);
	}
	Out.OppHeadC = P[0];
	Out.OppHeadBone = P[1];
	Out.OppPelvis = P[2];
	Out.OppChest = P[3];
	Out.OppHeadPre = P[4];
	Out.OppHeadBonePre = P[5];
}

// ---------------------------------------------------------------------------------------------
// Поза
// ---------------------------------------------------------------------------------------------

void FBoxerPoseFx::ApplySeparation(FCompactPose& Pose, const FBoxerFeelFrame& Fr, const FTransform& C2W, FBoxerFeelDebug* Dbg)
{
	if (!BoxerContactImpl::Ok(Head) || !BoxerContactImpl::Ok(Spine[0]) || !BoxerContactImpl::Ok(Pelvis))
	{
		return;
	}
	const FVector Fw = Fr.Fwd.GetSafeNormal2D();
	const FVector Hb = C2W.TransformPosition(BoxerContactImpl::CS(Pose, Head).GetLocation());
	const FVector Hc = BoxerContactImpl::HeadCenter(Hb, Fw, Fr.HeadUpCm);
	if (Dbg)
	{
		Dbg->BodyHeadPre = Hc;
		Dbg->BodyHeadBonePre = Hb;
	}
	if (!Fr.bBody || Fr.SepW <= 1e-3f)
	{
		return;
	}
	// Моя половина: сосед берёт свою по МОЕЙ голове до раздвижки — вместе ровно до касания.
	FVector P = BoxerFeel::HeadSeparation(Hc, Fr.OppHeadPre, Fr.HeadR + Fr.OppHeadR + BoxerContactImpl::SEP_MARGIN_CM, 0.5f, -Fw);
	P += BoxerFeel::HeadSeparation(Hb + P, Fr.OppHeadBonePre, BoxerContactImpl::HEAD_BONE_MIN_CM, 0.5f, -Fw);
	// Лицо в его корпус — целиком мой сдвиг (горизонтально: раздвигаем наклоном).
	FVector Pc = BoxerFeel::PushOutCapsule(Hc + P, Fr.HeadR, Fr.OppPelvis, Fr.OppChest, Fr.OppBodyR, -Fw);
	Pc.Z = 0.f;
	P += Pc;
	P *= Fr.SepW;
	const float Len = static_cast<float>(P.Size2D());
	if (Dbg)
	{
		Dbg->SepCm = Len;
	}
	if (Len < 0.05f)
	{
		return;
	}
	const FVector DirW(P.X / Len, P.Y / Len, 0.f);
	// Наклон корпуса от поясницы: голова уходит на угол × плечо рычага (высота головы над spine_03).
	const FVector S0 = C2W.TransformPosition(BoxerContactImpl::CS(Pose, Spine[0]).GetLocation());
	const float Lever = FMath::Max(20.f, static_cast<float>(Hc.Z - S0.Z));
	const float Ang = FMath::Min(BoxerContactImpl::SEP_LEAN_MAX, Len / Lever);
	const FVector UCs = C2W.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const FVector DCs = C2W.InverseTransformVectorNoScale(DirW).GetSafeNormal();
	const FVector Axis = FVector::CrossProduct(UCs, DCs).GetSafeNormal();
	if (!Axis.IsNearlyZero())
	{
		for (int32 I = 0; I < 3; ++I)
		{
			BoxerContactImpl::RotateCS(Pose, Spine[I], FQuat(Axis, Ang * BoxerContactImpl::SEP_SPLIT[I]));
		}
	}
	// Остаток — тазом (всё тело; ступни потом переставит FBoxerFootIk, как выпад).
	const FVector Hb2 = C2W.TransformPosition(BoxerContactImpl::CS(Pose, Head).GetLocation());
	const float Rest = FMath::Min(BoxerContactImpl::SEP_PELVIS_MAX_CM, Len - static_cast<float>(FVector::DotProduct(Hb2 - Hb, DirW)));
	if (Rest > 0.1f)
	{
		const FVector OffCs = C2W.InverseTransformVector(DirW * Rest);
		Pose[Pelvis].AddToTranslation(BoxerContactImpl::ParentRotCS(Pose, Pelvis).UnrotateVector(OffCs));
	}
}

void FBoxerPoseFx::ApplyHandStop(FCompactPose& Pose, const FBoxerFeelFrame& Fr, const FTransform& C2W, FBoxerFeelDebug* Dbg)
{
	if (!BoxerContactImpl::Ok(Head) || !BoxerContactImpl::Ok(Pelvis) || !BoxerContactImpl::Ok(Spine[2]))
	{
		return;
	}
	const FVector Fw = Fr.Fwd.GetSafeNormal2D();
	if (Fr.bBody && Fr.HandPushW > 1e-3f)
	{
		for (int32 S = 0; S < 2; ++S)
		{
			if (!BoxerContactImpl::Ok(UpperArm[S]) || !BoxerContactImpl::Ok(LowerArm[S]) || !BoxerContactImpl::Ok(Hand[S]))
			{
				continue;
			}
			const FTransform UcS = BoxerContactImpl::CS(Pose, UpperArm[S]);
			const FTransform LcS = BoxerContactImpl::CS(Pose, LowerArm[S]);
			const FVector Sh = UcS.GetLocation();
			const FVector El = LcS.GetLocation();
			const FVector Hd = BoxerContactImpl::CS(Pose, Hand[S]).GetLocation();
			const float LenU = FVector::Dist(Sh, El);
			const float LenL = FVector::Dist(El, Hd);
			if (LenU < 1.f || LenL < 1.f)
			{
				continue;
			}
			const FVector HdW = C2W.TransformPosition(Hd);
			const FVector ElW = C2W.TransformPosition(El);
			const FVector G = HdW + (HdW - ElW).GetSafeNormal() * BoxerContactImpl::GLOVE_FWD_CM;
			// Назад к себе — если центры совпали.
			const FVector Back = -Fw;
			FVector D = FVector::ZeroVector;
			for (int32 It = 0; It < 2; ++It)
			{
				D += BoxerFeel::PushOutSphere(G + D, BoxerContactImpl::GLOVE_R_CM, Fr.OppHeadC, Fr.OppHeadR, Back);
				D += BoxerFeel::PushOutSphere(G + D, BoxerContactImpl::GLOVE_R_CM, Fr.OppHeadBone, BoxerContactImpl::NECK_R_CM, Back);
				D += BoxerFeel::PushOutCapsule(G + D, BoxerContactImpl::GLOVE_R_CM, Fr.OppPelvis, Fr.OppChest, Fr.OppBodyR, Back);
			}
			D *= Fr.HandPushW;
			if (D.SizeSquared() < 0.04f)
			{
				continue;
			}
			if (Dbg)
			{
				Dbg->HandPushCm = FMath::Max(Dbg->HandPushCm, static_cast<float>(D.Size()));
			}
			const FVector Goal = Hd + C2W.InverseTransformVector(D);
			const FVector Pole = El + (El - (Sh + Hd) * 0.5f).GetSafeNormal() * 30.f;
			FVector NewE, NewH;
			AnimationCore::SolveTwoBoneIK(Sh, El, Hd, Pole, Goal, NewE, NewH, LenU, LenL, false, 1.0, 1.0);
			const FQuat Qu = FQuat::FindBetweenNormals((El - Sh).GetSafeNormal(), (NewE - Sh).GetSafeNormal());
			const FQuat NewU = (Qu * UcS.GetRotation()).GetNormalized();
			const FQuat Ql = FQuat::FindBetweenNormals(Qu.RotateVector(Hd - El).GetSafeNormal(), (NewH - NewE).GetSafeNormal());
			const FQuat NewL = (Ql * Qu * LcS.GetRotation()).GetNormalized();
			Pose[UpperArm[S]].SetRotation((BoxerContactImpl::ParentRotCS(Pose, UpperArm[S]).Inverse() * NewU).GetNormalized());
			Pose[LowerArm[S]].SetRotation((BoxerContactImpl::ParentRotCS(Pose, LowerArm[S]).Inverse() * NewL).GetNormalized());
		}
	}
}

void FBoxerPoseFx::PublishBody(FCompactPose& Pose, const FBoxerFeelFrame& Fr, const FTransform& C2W, FBoxerFeelDebug* Dbg)
{
	Resolve(Pose.GetBoneContainer());
	if (!Dbg || !BoxerContactImpl::Ok(Head) || !BoxerContactImpl::Ok(Pelvis) || !BoxerContactImpl::Ok(Spine[2]))
	{
		return;
	}
	const FVector Fw = Fr.Fwd.GetSafeNormal2D();
	// Итоговое тело — сопернику (его раздвижка и упор в следующем кадре).
	{
		const FVector Hb = C2W.TransformPosition(BoxerContactImpl::CS(Pose, Head).GetLocation());
		Dbg->bBody = true;
		Dbg->BodyHeadBone = Hb;
		Dbg->BodyHeadC = BoxerContactImpl::HeadCenter(Hb, Fw, Fr.HeadUpCm);
		Dbg->BodyPelvis = C2W.TransformPosition(BoxerContactImpl::CS(Pose, Pelvis).GetLocation());
		Dbg->BodyChest = C2W.TransformPosition(BoxerContactImpl::CS(Pose, Spine[2]).GetLocation());
		if (Dbg->BodyHeadPre.IsNearlyZero())
		{
			Dbg->BodyHeadPre = Dbg->BodyHeadC;
			Dbg->BodyHeadBonePre = Hb;
		}
	}
}
