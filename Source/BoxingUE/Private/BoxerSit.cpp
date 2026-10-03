// S-71: боец садится на стул в углу (порт web sitPose.ts). См. BoxerSit.h.
#include "BoxerSit.h"

#include "BonePose.h"
#include "BoneContainer.h"
#include "FightTypes.h"

namespace BoxerSitImpl
{
	FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return Mesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
	}

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

	FQuat Turn(const FVector& From, const FVector& To, float Angle)
	{
		const FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		return (Axis.IsNearlyZero() || FMath::Abs(Angle) < 1e-5f) ? FQuat::Identity : FQuat(Axis, Angle);
	}

	// Кость Bone так, чтобы «Bone → Child» смотрел вдоль Dir (компонент), с долей W.
	void Aim(FCompactPose& P, FCompactPoseBoneIndex Bone, FCompactPoseBoneIndex Child, const FVector& Dir, float W)
	{
		if (!Ok(Bone) || !Ok(Child) || W <= 1e-3f)
		{
			return;
		}
		const FVector Cur = (CS(P, Child).GetLocation() - CS(P, Bone).GetLocation()).GetSafeNormal();
		if (Cur.IsNearlyZero() || Dir.IsNearlyZero())
		{
			return;
		}
		const FQuat D = FQuat::FindBetweenNormals(Cur, Dir.GetSafeNormal());
		RotateCS(P, Bone, W >= 0.999f ? D : FQuat::Slerp(FQuat::Identity, D, W));
	}
}

namespace BoxerSitImpl
{
	// Двухзвенный IK: кисть (Hand) — в T (компонент), локоть — к Pref; доля W.
	void TwoBone(FCompactPose& P, FCompactPoseBoneIndex Up, FCompactPoseBoneIndex Lo, FCompactPoseBoneIndex Ha, const FVector& T, const FVector& Pref, float W)
	{
		const FVector Sh = CS(P, Up).GetLocation();
		const FVector El = CS(P, Lo).GetLocation();
		const FVector H = CS(P, Ha).GetLocation();
		const float A = (El - Sh).Size(), B = (H - El).Size();
		const FVector ToT = T - Sh;
		const FVector N = ToT.GetSafeNormal();
		if (N.IsNearlyZero() || A < 1.f || B < 1.f)
		{
			return;
		}
		const float D = FMath::Clamp(static_cast<float>(ToT.Size()), 1.f, A + B - 0.5f);
		FVector Pole = Pref - N * FVector::DotProduct(Pref, N);
		Pole = Pole.GetSafeNormal();
		if (Pole.IsNearlyZero())
		{
			return;
		}
		const float X = (A * A - B * B + D * D) / (2.f * D);
		const float Hh = FMath::Sqrt(FMath::Max(0.f, A * A - X * X));
		Aim(P, Up, Lo, (Sh + N * X + Pole * Hh) - Sh, W);
		Aim(P, Lo, Ha, (Sh + N * D) - CS(P, Lo).GetLocation(), W);
	}
}

bool BoxerSit::WantsSit(const FFightSnapshot& S, int32 Fighter)
{
	if (Fighter < 0 || Fighter > 1 || S.Phase != EFightPhase::Between || S.Stage.Kind != ERingStageKind::Rest)
	{
		return false;
	}
	return S.Stage.bArrived[Fighter] && !S.Fighters[Fighter].bDown;
}

void BoxerSit::RopeHands(const FVector& Floor, int32 Corner, const FVector& At, const FVector& Left, FVector Out[2])
{
	const float S = Corner == 0 ? -1.f : 1.f;
	for (int32 I = 0; I < 2; ++I)
	{
		const FVector O = (I == 0 ? Left : -Left).GetSafeNormal2D();
		// Канат, к которому смотрит рука: нормали сторон угла — (S, 0) и (0, S).
		const bool bAxisX = O.X * S >= O.Y * S;
		FVector P = At + O * HAND_ALONG_CM;
		if (bAxisX)
		{
			P.X = Floor.X + S * (ROPE_HALF_CM + HAND_OVER_CM);
		}
		else
		{
			P.Y = Floor.Y + S * (ROPE_HALF_CM + HAND_OVER_CM);
		}
		P.Z = Floor.Z + ROPE_Z_CM + 4.f;
		Out[I] = P;
	}
}

void FBoxerSitState::Update(bool bWantSit, float Dt)
{
	bWant = bWantSit;
	ArrivedFor = bWantSit ? ArrivedFor + Dt : 0.f;
	const bool bDown = bWantSit && ArrivedFor >= BoxerSit::SIT_DELAY;
	W = bDown ? FMath::Min(1.f, W + BoxerSit::SIT_RATE * Dt) : FMath::Max(0.f, W - BoxerSit::STAND_RATE * Dt);
}

void FBoxerSitFx::Resolve(const FBoneContainer& Bones)
{
	if (Serial == Bones.GetSerialNumber() && ContainerPtr == &Bones)
	{
		return;
	}
	Serial = Bones.GetSerialNumber();
	ContainerPtr = &Bones;
	Pelvis = BoxerSitImpl::FindBone(Bones, TEXT("pelvis"));
	Spine[0] = BoxerSitImpl::FindBone(Bones, TEXT("spine_01"));
	Spine[1] = BoxerSitImpl::FindBone(Bones, TEXT("spine_02"));
	Spine[2] = BoxerSitImpl::FindBone(Bones, TEXT("spine_03"));
	Neck = BoxerSitImpl::FindBone(Bones, TEXT("neck_01"));
	Head = BoxerSitImpl::FindBone(Bones, TEXT("head"));
	static const TCHAR* UpperN[2] = {TEXT("upperarm_l"), TEXT("upperarm_r")};
	static const TCHAR* LowerN[2] = {TEXT("lowerarm_l"), TEXT("lowerarm_r")};
	static const TCHAR* HandN[2] = {TEXT("hand_l"), TEXT("hand_r")};
	static const TCHAR* ThighN[2] = {TEXT("thigh_l"), TEXT("thigh_r")};
	static const TCHAR* CalfN[2] = {TEXT("calf_l"), TEXT("calf_r")};
	static const TCHAR* FootN[2] = {TEXT("foot_l"), TEXT("foot_r")};
	static const TCHAR* BallN[2] = {TEXT("ball_l"), TEXT("ball_r")};
	for (int32 S = 0; S < 2; ++S)
	{
		Upper[S] = BoxerSitImpl::FindBone(Bones, UpperN[S]);
		Lower[S] = BoxerSitImpl::FindBone(Bones, LowerN[S]);
		Hand[S] = BoxerSitImpl::FindBone(Bones, HandN[S]);
		Thigh[S] = BoxerSitImpl::FindBone(Bones, ThighN[S]);
		Calf[S] = BoxerSitImpl::FindBone(Bones, CalfN[S]);
		Foot[S] = BoxerSitImpl::FindBone(Bones, FootN[S]);
		Ball[S] = BoxerSitImpl::FindBone(Bones, BallN[S]);
	}
}

void FBoxerSitFx::Apply(FCompactPose& Pose, const FBoxerSitFrame& Fr, const FTransform& CompToWorld)
{
	LastDropCm = 0.f;
	const float W = FMath::Clamp(Fr.W, 0.f, 1.f);
	if (W <= 1e-3f)
	{
		return;
	}
	Resolve(Pose.GetBoneContainer());
	if (!BoxerSitImpl::Ok(Pelvis) || !BoxerSitImpl::Ok(Thigh[0]) || !BoxerSitImpl::Ok(Thigh[1]) || !BoxerSitImpl::Ok(Calf[0]) || !BoxerSitImpl::Ok(Calf[1]) || !BoxerSitImpl::Ok(Foot[0]) || !BoxerSitImpl::Ok(Foot[1]))
	{
		return;
	}
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Fr.Fwd).GetSafeNormal();
	const FVector L = CompToWorld.InverseTransformVectorNoScale(Fr.Left).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();

	// Ступни до правки (стоя — на настиле): после посадки таз опускается так, чтобы они вернулись на ту же высоту.
	const auto FeetY = [&]() {
		return 0.5f * (FVector::DotProduct(BoxerSitImpl::CS(Pose, Foot[0]).GetLocation(), U) + FVector::DotProduct(BoxerSitImpl::CS(Pose, Foot[1]).GetLocation(), U));
	};
	const float Y0 = FeetY();
	// Наклон стопы (подъём → подушечка) стоя — его и держим сидя (стопа плашмя на настиле).
	float FootDrop[2] = {0.25f, 0.25f};
	for (int32 S = 0; S < 2; ++S)
	{
		if (BoxerSitImpl::Ok(Ball[S]))
		{
			const FVector D = (BoxerSitImpl::CS(Pose, Ball[S]).GetLocation() - BoxerSitImpl::CS(Pose, Foot[S]).GetLocation()).GetSafeNormal();
			FootDrop[S] = FMath::Clamp(-static_cast<float>(FVector::DotProduct(D, U)), 0.f, 0.8f);
		}
	}

	// --- корпус: чуть откинут назад, на подушку угла ---
	static const float Share[3] = {0.4f, 0.35f, 0.25f};
	for (int32 I = 0; I < 3; ++I)
	{
		BoxerSitImpl::RotateCS(Pose, Spine[I], BoxerSitImpl::Turn(U, -F, BoxerSit::BACK * Share[I] * W));
	}

	// --- ноги: бедро вперёд (колени врозь), голень вниз, стопа плашмя вперёд ---
	for (int32 S = 0; S < 2; ++S)
	{
		const FVector Out = S == 0 ? L : -L;
		const FVector Hor = (F * FMath::Cos(BoxerSit::SPREAD) + Out * FMath::Sin(BoxerSit::SPREAD)).GetSafeNormal();
		const FVector ThighDir = (Hor * FMath::Sin(BoxerSit::THIGH) - U * FMath::Cos(BoxerSit::THIGH)).GetSafeNormal();
		const FVector ShinDir = (-U * FMath::Cos(BoxerSit::SHIN_BACK) - F * FMath::Sin(BoxerSit::SHIN_BACK) + Out * 0.08f).GetSafeNormal();
		BoxerSitImpl::Aim(Pose, Thigh[S], Calf[S], ThighDir, W);
		BoxerSitImpl::Aim(Pose, Calf[S], Foot[S], ShinDir, W);
		if (BoxerSitImpl::Ok(Ball[S]))
		{
			const FVector FootH = (F * FMath::Cos(BoxerSit::SPREAD * 0.5f) + Out * FMath::Sin(BoxerSit::SPREAD * 0.5f)).GetSafeNormal();
			const float Dn = FootDrop[S];
			BoxerSitImpl::Aim(Pose, Foot[S], Ball[S], (FootH * FMath::Sqrt(FMath::Max(0.f, 1.f - Dn * Dn)) - U * Dn).GetSafeNormal(), W);
		}
	}

	// --- таз вниз: ступни — на прежнюю высоту (на настил); до рук — кисти IK встают на канаты уже от опущенного таза ---
	const float Drop = FMath::Max(0.f, FeetY() - Y0);
	if (Drop > 0.f)
	{
		const FQuat Qp = BoxerSitImpl::ParentRotCS(Pose, Pelvis);
		Pose[Pelvis].AddToTranslation(Qp.UnrotateVector(-U * Drop));
	}
	LastDropCm = Drop;

	// --- руки: назад на канаты угла, перчатки свисают ---
	for (int32 S = 0; S < 2; ++S)
	{
		const FVector Out = S == 0 ? L : -L;
		const FVector Back = (Out * FMath::Cos(BoxerSit::ARM_BACK) - F * FMath::Sin(BoxerSit::ARM_BACK)).GetSafeNormal();
		const FVector UpperDir = (Back * FMath::Cos(BoxerSit::ARM_DOWN) - U * FMath::Sin(BoxerSit::ARM_DOWN)).GetSafeNormal();
		const float Fd = BoxerSit::ARM_DOWN + BoxerSit::FORE_DROOP;
		const FVector ForeDir = (Back * FMath::Cos(Fd) - U * FMath::Sin(Fd)).GetSafeNormal();
		BoxerSitImpl::Aim(Pose, Upper[S], Lower[S], UpperDir, W);
		BoxerSitImpl::Aim(Pose, Lower[S], Hand[S], ForeDir, W);
		// Кисть — на канат (двухзвенный IK; локоть вниз-назад).
		if (Fr.bHands && BoxerSitImpl::Ok(Upper[S]) && BoxerSitImpl::Ok(Lower[S]) && BoxerSitImpl::Ok(Hand[S]))
		{
			BoxerSitImpl::TwoBone(Pose, Upper[S], Lower[S], Hand[S], CompToWorld.InverseTransformPosition(Fr.HandTarget[S]),
				(-U - F * 0.35f + Out * 0.2f).GetSafeNormal(), W);
		}
	}

	// --- голова: чуть вниз (слушает тренера; корпус откинут — компенсируем) ---
	BoxerSitImpl::RotateCS(Pose, Neck, BoxerSitImpl::Turn(U, F, (BoxerSit::HEAD_DOWN + BoxerSit::BACK) * 0.5f * W));
	BoxerSitImpl::RotateCS(Pose, Head, BoxerSitImpl::Turn(U, F, (BoxerSit::HEAD_DOWN + BoxerSit::BACK) * 0.5f * W));
}
