#include "BoxerFeel.h"

#include "BonePose.h"
#include "BoneContainer.h"
#include "TwoBoneIK.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace BoxerFeelImpl {}
using namespace BoxerFeelImpl; // S-74: свои хелперы — именованным пространством (unity-сборка: у соседних .cpp свои анонимные Ok/CS/…)

namespace BoxerFeelImpl
{
	struct FSpring
	{
		float W;   // частота, рад/с
		float Z;   // демпфирование
		float Max; // предел отклонения
	};

	// Порт SPRING веба (impact.ts): голова хлёсткая, корпус медленнее, колени тяжёлые, шаг без отскока.
	const FSpring GSprings[BOX_REACT_NUM] = {
		{17.f, 0.45f, 0.75f}, // HeadPitch
		{16.f, 0.45f, 0.8f},  // HeadYaw
		{16.f, 0.5f, 0.45f},  // HeadRoll
		{10.f, 0.6f, 0.5f},   // TorsoPitch
		{10.f, 0.6f, 0.35f},  // TorsoRoll
		{10.f, 0.6f, 0.35f},  // TorsoYaw
		{6.5f, 0.8f, 0.55f},  // Knee
		{7.f, 0.95f, 0.22f},  // Back
		{7.f, 0.95f, 0.14f},  // Side
	};

	// Усиление реакции на попадание (блок и промах — как есть) и порог «подкосились колени».
	constexpr float LAND_GAIN = 1.35f;
	constexpr float KNEE_MAG = 1.25f;

	// Доли поворота по костям (contact.ts веба): корпус 0.3/0.35/0.35, шея 0.4 + голова 0.6.
	constexpr float TORSO_SPLIT[3] = {0.3f, 0.35f, 0.35f};
	constexpr float NECK_SPLIT = 0.4f;
	constexpr float HEAD_SPLIT = 0.6f;

	// Рука дотягивается до 98.5% длины (прямая рука в IK вырождается).
	constexpr float REACH_FRAC = 0.985f;

	// S-70 (web SkinnedFighter applyLean): наклон корпуса берёт долю выноса удара. LEAN_ARM — от поясницы до плеча (см
	// меша): вынос плеча на радиан наклона; LEAN_TYPE — по виду удара (прямой / хук / апперкот).
	constexpr float LEAN_ARM_CM = 45.f;
	constexpr float LEAN_SHARE = 0.45f;
	constexpr float LEAN_MAX = 0.36f;
	constexpr float LEAN_TYPE[3] = {1.f, 0.7f, 0.5f};
	constexpr float LEAN_SPLIT[3] = {0.4f, 0.35f, 0.25f};

	float PeakFactor(float Z)
	{
		const float D = FMath::Sqrt(1.f - Z * Z);
		return FMath::Exp((-Z / D) * FMath::Atan2(D, Z));
	}

	FCompactPoseBoneIndex Find(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		if (Mesh == INDEX_NONE)
		{
			return FCompactPoseBoneIndex(INDEX_NONE);
		}
		return Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
	}

	bool Ok(FCompactPoseBoneIndex I) { return I.GetInt() != INDEX_NONE; }

	// Компонентный трансформ кости по локальной позе (Child_CS = Local * Parent_CS).
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
		return Par.GetInt() != INDEX_NONE ? BoxerFeelImpl::CS(P, Par).GetRotation() : FQuat::Identity;
	}

	// Повернуть кость вокруг оси компонента (D — поворот в осях компонента), дети следуют.
	void RotateCS(FCompactPose& P, FCompactPoseBoneIndex I, const FQuat& D)
	{
		if (!BoxerFeelImpl::Ok(I))
		{
			return;
		}
		const FQuat Qp = BoxerFeelImpl::ParentRotCS(P, I);
		P[I].SetRotation((Qp.Inverse() * D * Qp * P[I].GetRotation()).GetNormalized());
	}

	// Поворот, переводящий From в сторону To (ось = From × To), на угол Angle.
	FQuat Turn(const FVector& From, const FVector& To, float Angle)
	{
		if (FMath::Abs(Angle) < 1e-5f)
		{
			return FQuat::Identity;
		}
		const FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		return Axis.IsNearlyZero() ? FQuat::Identity : FQuat(Axis, Angle);
	}

	float Smooth(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / FMath::Max(1e-4f, B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
}

// ---------------------------------------------------------------------------------------------
// Реакция: пинок и пружины (порт impact.ts)
// ---------------------------------------------------------------------------------------------

float BoxerFeel::ImpactScale(float Mag)
{
	return FMath::Clamp(0.2f + 0.7f * FMath::Max(0.f, Mag), 0.3f, 1.8f);
}

FBoxReactKick BoxerFeel::ReactionKick(EBoxFeelEvent Kind, EBoxFeelPunch Punch, bool bRear, bool bBody, float Mag, bool bSlipped)
{
	using C = EBoxReactChannel;
	FBoxReactKick K;
	const float S = ImpactScale(Mag);
	// Удар левой (lead у правши) приходит защищающемуся в ПРАВУЮ сторону и толкает влево (+), правой — наоборот.
	const float Side = bRear ? -1.f : 1.f;
	const bool bHook = Punch == EBoxFeelPunch::Hook;

	if (Kind == EBoxFeelEvent::Whiff)
	{
		// S-75: «провалился» (свой удар ушёл в нырок): корпус и голова вперёд по инерции, полшага вперёд, доворот за
		// рукой (правая — корпус уходит влево, левая — вправо), колени подсели. Каналы — в осях самого атакующего.
		const float Hard = Punch == EBoxFeelPunch::Straight ? 0.7f : 1.f; // джеб «проваливает» меньше силовых
		K[C::TorsoPitch] = -0.2f * Hard;
		K[C::HeadPitch] = -0.08f * Hard;
		K[C::Back] = -0.07f * Hard;
		K[C::TorsoYaw] = (bRear ? 0.12f : -0.08f) * Hard;
		K[C::TorsoRoll] = (bRear ? -0.05f : 0.05f) * Hard;
		K[C::Knee] = 0.06f * Hard;
		K.bValid = true;
		return K;
	}
	if (Kind == EBoxFeelEvent::Miss)
	{
		// Промах «в лоб»: голова уходит с линии наружу от бьющей руки; нырок уже виден — без реакции.
		if (bSlipped)
		{
			return K;
		}
		const float R = bRear ? -1.f : 1.f;
		K[C::TorsoRoll] = 0.14f * R;
		K[C::HeadRoll] = 0.12f * R;
		K[C::TorsoYaw] = -0.06f * R;
		K[C::HeadPitch] = 0.05f;
		K[C::Side] = -0.05f * R;
		K[C::Back] = 0.02f;
		K.bValid = true;
		return K;
	}
	if (Kind == EBoxFeelEvent::Block)
	{
		// В перчатки: руки вдавливает к лицу (FBoxerGuardState::Push), подбородок прячется, корпус чуть сжимается
		// (колени), полшага назад. S-75: голова больше не запрокидывается (было +0.07 — читалось как попадание).
		const float B = FMath::Min(1.3f, 0.45f + 0.6f * FMath::Max(0.f, Mag));
		K[C::HeadPitch] = -0.03f * B;
		K[C::TorsoPitch] = 0.03f * B;
		K[C::Knee] = 0.04f * B;
		K[C::TorsoYaw] = bHook ? 0.06f * Side * B : 0.f;
		K[C::Back] = 0.035f * B;
		K[C::Side] = bHook ? 0.015f * Side * B : 0.f;
		K.bValid = true;
		return K;
	}

	const float Heavy = FMath::Max(0.f, Mag - KNEE_MAG);
	if (bBody)
	{
		// В корпус: сгиб вперёд, колени подсели; хук — крен к стороне удара.
		K[C::TorsoPitch] = -0.16f * S;
		K[C::HeadPitch] = -0.07f * S;
		K[C::TorsoRoll] = bHook ? 0.12f * Side * S : 0.f;
		K[C::TorsoYaw] = bHook ? 0.08f * Side * S : 0.f;
		K[C::Knee] = 0.05f * S + 0.18f * Heavy;
		K[C::Back] = (bHook ? 0.02f : 0.045f) * S;
		K[C::Side] = bHook ? 0.03f * Side * S : 0.f;
	}
	else if (bHook)
	{
		K[C::HeadYaw] = 0.34f * Side * S;
		K[C::HeadRoll] = -0.14f * Side * S;
		K[C::HeadPitch] = 0.05f * S;
		K[C::TorsoRoll] = -0.05f * Side * S;
		K[C::TorsoYaw] = 0.08f * Side * S;
		K[C::Knee] = 0.22f * Heavy;
		K[C::Back] = 0.02f * S;
		K[C::Side] = 0.04f * Side * S;
	}
	else if (Punch == EBoxFeelPunch::Uppercut)
	{
		K[C::HeadPitch] = 0.4f * S;
		K[C::TorsoPitch] = 0.1f * S;
		K[C::HeadYaw] = 0.05f * Side * S;
		K[C::Knee] = 0.22f * Heavy;
		K[C::Back] = 0.035f * S;
	}
	else
	{
		// Прямые: голова отлетает назад, чуть поворачивается от руки удара, корпус откидывает.
		K[C::HeadPitch] = 0.24f * S;
		K[C::HeadYaw] = (Punch == EBoxFeelPunch::Cross ? 0.1f : 0.05f) * Side * S;
		K[C::TorsoPitch] = 0.08f * S;
		K[C::Knee] = 0.22f * Heavy;
		K[C::Back] = 0.06f * S;
	}
	for (float& V : K.V)
	{
		V *= LAND_GAIN;
	}
	K.bValid = true;
	return K;
}

void BoxerFeel::PunchEnvelopes(float Phase, float ContactFrac, float& OutAim, float& OutReach, bool bSnap)
{
	const float C = FMath::Clamp(ContactFrac, 0.05f, 0.95f);
	const float P = FMath::Clamp(Phase, 0.f, 1.f);
	if (P < C)
	{
		const float U = P / C;
		// S-75: контра — рука на линии почти сразу, «дотянуться» начинается раньше (замах короче на глаз; контакт — тот же).
		OutAim = bSnap ? BoxerFeelImpl::Smooth(0.f, 0.45f, U) : BoxerFeelImpl::Smooth(0.15f, 0.75f, U);
		OutReach = bSnap ? BoxerFeelImpl::Smooth(0.3f, 0.9f, U) : BoxerFeelImpl::Smooth(0.5f, 1.f, U);
	}
	else
	{
		const float V = (P - C) / (1.f - C);
		OutAim = 1.f - BoxerFeelImpl::Smooth(0.25f, 0.95f, V);
		OutReach = 1.f - BoxerFeelImpl::Smooth(0.1f, 0.6f, V);
	}
}

float BoxerFeel::BendAngle(const FVector& Axis, const FVector& Bend, const FVector& FootFwd, bool& bValid)
{
	const FVector Ax = Axis.GetSafeNormal();
	const FVector B = (Bend - Ax * FVector::DotProduct(Bend, Ax)).GetSafeNormal();
	const FVector F = (FootFwd - Ax * FVector::DotProduct(FootFwd, Ax)).GetSafeNormal();
	bValid = !Ax.IsNearlyZero() && !B.IsNearlyZero() && !F.IsNearlyZero();
	if (!bValid)
	{
		return 0.f;
	}
	return FMath::Atan2(static_cast<float>(FVector::DotProduct(FVector::CrossProduct(F, B), Ax)), static_cast<float>(FVector::DotProduct(F, B)));
}

FVector BoxerFeel::BendOf(const FVector& A, const FVector& B, const FVector& C, float MinCm)
{
	const FVector D = (C - A).GetSafeNormal();
	if (D.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	const FVector V = B - A;
	const FVector N = V - D * FVector::DotProduct(V, D);
	return N.Size() < MinCm ? FVector::ZeroVector : N.GetSafeNormal();
}

FVector BoxerFeel::KneeBendDir(const FVector& Hip, const FVector& ClipKnee, const FVector& Goal, const FVector& FootFwd, float L1, float L2,
	float ClipShare, bool bGuard, float KneeMaxDevRad, const FVector& Inward, float KneeMaxInRad)
{
	const FVector D = (Goal - Hip).GetSafeNormal();
	if (D.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	const FVector V1 = ClipKnee - Hip;
	FVector Nc = V1 - D * FVector::DotProduct(V1, D);
	const float NcLen = static_cast<float>(Nc.Size());
	Nc = Nc.GetSafeNormal();
	FVector FF = FootFwd - D * FVector::DotProduct(FootFwd, D);
	FF = FF.GetSafeNormal();
	float Bent = FMath::Min(1.f, NcLen / FMath::Max(1e-3f, 0.04f * (L1 + L2))) * 0.6f * FMath::Clamp(ClipShare, 0.f, 1.f);
	if (bGuard && !FF.IsNearlyZero() && !Nc.IsNearlyZero())
	{
		// Колено клипа назад от носка (≥ 90°) — не в счёт, в пределах 60° — целиком, между — плавно.
		Bent *= BoxerFeelImpl::Smooth(0.f, 0.5f, static_cast<float>(FVector::DotProduct(Nc, FF)));
	}
	FVector N = Nc * Bent + FF * (1.f - Bent);
	N -= D * FVector::DotProduct(N, D);
	N = N.GetSafeNormal();
	if (N.IsNearlyZero())
	{
		N = !FF.IsNearlyZero() ? FF : Nc;
	}
	if (bGuard && !FF.IsNearlyZero() && !N.IsNearlyZero())
	{
		// Не дальше KneeMaxDevRad от курса ступни: колено над носком, ни назад, ни внутрь.
		bool bOk = false;
		const float A = BendAngle(D, N, FF, bOk);
		float Lim = KneeMaxDevRad;
		if (bOk && !Inward.IsNearlyZero())
		{
			// Какой знак угла — внутрь (к другой ноге): поворот носка на +угол сдвигает колено к Inward?
			const FVector Plus = FQuat(D, 0.3f).RotateVector(FF);
			const float InSign = FVector::DotProduct(Plus - FF, Inward) > 0.f ? 1.f : -1.f;
			Lim = FMath::Sign(A) == InSign ? FMath::Min(KneeMaxDevRad, KneeMaxInRad) : KneeMaxDevRad;
		}
		if (bOk && FMath::Abs(A) > Lim)
		{
			N = FQuat(D, FMath::Sign(A) * Lim).RotateVector(FF).GetSafeNormal();
		}
	}
	return N;
}

FVector BoxerFeel::GuardPush(const FVector& Glove, const FVector& A, const FVector& B, float Clear, float MaxCm)
{
	const FVector AB = B - A;
	const double L2 = AB.SizeSquared();
	const double T = L2 > 1e-6 ? FMath::Clamp(FVector::DotProduct(Glove - A, AB) / L2, 0.0, 1.0) : 0.0;
	const FVector Near = A + AB * T;
	FVector Away = Glove - Near;
	const float D = static_cast<float>(Away.Size());
	if (D >= Clear)
	{
		return FVector::ZeroVector;
	}
	if (D < 0.5f)
	{
		// Ровно на линии — вбок, поперёк удара (по горизонтали).
		Away = FVector::CrossProduct(FVector::UpVector, AB).GetSafeNormal();
		if (Away.IsNearlyZero())
		{
			Away = FVector::RightVector;
		}
	}
	else
	{
		Away /= D;
	}
	return Away * FMath::Min(Clear - D, MaxCm);
}

float BoxerFeel::SegPointDist(const FVector& A, const FVector& B, const FVector& P)
{
	const FVector AB = B - A;
	const double L2 = AB.SizeSquared();
	const double T = L2 > 1e-6 ? FMath::Clamp(FVector::DotProduct(P - A, AB) / L2, 0.0, 1.0) : 0.0;
	return static_cast<float>(FVector::Dist(P, A + AB * T));
}

bool BoxerFeel::AimAroundGuard(const FVector& Center, float Radius, const FVector& Start, const FVector& Approach, bool bStraight, float PathCm,
	const FVector Gloves[2], float Clear, const FVector2D* Prev, FGuardAim& Out)
{
	const FVector A0 = Approach.GetSafeNormal();
	const FVector Up = FVector::UpVector;
	FVector Side = FVector::CrossProduct(Up, A0).GetSafeNormal();
	if (Side.IsNearlyZero())
	{
		Side = FVector::RightVector;
	}
	const FVector UpN = FVector::CrossProduct(A0, Side).GetSafeNormal() * (FVector::DotProduct(FVector::CrossProduct(A0, Side), Up) >= 0.f ? 1.f : -1.f);
	auto Eval = [&](float U, float V, FGuardAim& R)
	{
		// Точка на передней полусфере: от «навстречу удару» вбок на U и вверх на V (доли радиуса).
		const FVector N = (-A0 + Side * U + UpN * V).GetSafeNormal();
		R.Surface = Center + N * Radius;
		R.Approach = bStraight ? (R.Surface - Start).GetSafeNormal() : A0;
		R.UV = FVector2D(U, V);
		if (FVector::DotProduct(R.Approach, N) > -0.2f)
		{
			return false; // кулак пришёл бы по касательной/с тыла
		}
		const FVector From = bStraight ? Start : R.Surface - R.Approach * PathCm;
		float Near = TNumericLimits<float>::Max();
		for (int32 G = 0; G < 2; ++G)
		{
			Near = FMath::Min(Near, SegPointDist(From, R.Surface, Gloves[G]) - Clear);
		}
		R.ClearCm = Near;
		return true;
	};
	auto Cost = [&](const FGuardAim& R)
	{
		float C = static_cast<float>(R.UV.Size()) * 0.6f;
		if (R.ClearCm < 0.f)
		{
			C += 3.f + (-R.ClearCm) * 0.2f;
		}
		if (Prev)
		{
			C += static_cast<float>(FVector2D::Distance(R.UV, *Prev)) * 0.35f;
		}
		return C;
	};
	FGuardAim Base;
	if (!Eval(0.f, 0.f, Base))
	{
		Base.Surface = Center - A0 * Radius;
		Base.Approach = A0;
		Base.ClearCm = 0.f;
	}
	FGuardAim Best = Base;
	float BestC = Cost(Base);
	if (Base.ClearCm < 0.f || (Prev && !Prev->IsNearlyZero()))
	{
		for (int32 Iu = -4; Iu <= 4; ++Iu)
		{
			for (int32 Iv = -2; Iv <= 4; ++Iv)
			{
				FGuardAim R;
				if (!Eval(Iu * 0.25f, Iv * 0.25f, R))
				{
					continue;
				}
				const float C = Cost(R);
				if (C < BestC - 1e-4f)
				{
					BestC = C;
					Best = R;
				}
			}
		}
	}
	Out = Best;
	return !Best.UV.IsNearlyZero();
}

void FBoxerReactionRig::Kick(const FBoxReactKick& K)
{
	if (!K.bValid)
	{
		return;
	}
	for (int32 C = 0; C < BOX_REACT_NUM; ++C)
	{
		if (K.V[C] != 0.f)
		{
			Vel[C] += K.V[C] * GSprings[C].W / BoxerFeelImpl::PeakFactor(GSprings[C].Z);
		}
	}
}

void FBoxerReactionRig::Update(float Dt)
{
	if (Dt <= 0.f)
	{
		return;
	}
	// Провал кадра (скриншот, подгрузка) — не сбрасываем реакцию (в вебе сброс был для «защёлкивания» повтора),
	// а режем шаг: иначе удар в кадре со скриншотом терял реакцию целиком.
	Dt = FMath::Min(Dt, 1.f / 30.f);
	const int32 N = FMath::Min(12, FMath::CeilToInt(Dt * 120.f));
	const float H = Dt / N;
	for (int32 C = 0; C < BOX_REACT_NUM; ++C)
	{
		float X0 = X[C];
		float V = Vel[C];
		if (X0 == 0.f && V == 0.f)
		{
			continue;
		}
		const FSpring& Sp = GSprings[C];
		for (int32 I = 0; I < N; ++I)
		{
			V += (-Sp.W * Sp.W * X0 - 2.f * Sp.Z * Sp.W * V) * H;
			X0 += V * H;
		}
		if (FMath::Abs(X0) > Sp.Max)
		{
			X0 = FMath::Sign(X0) * Sp.Max;
			if (FMath::Sign(V) == FMath::Sign(X0))
			{
				V = 0.f;
			}
		}
		if (FMath::Abs(X0) < 1e-5f && FMath::Abs(V) < 1e-4f)
		{
			X0 = V = 0.f;
		}
		X[C] = X0;
		Vel[C] = V;
	}
}

void FBoxerReactionRig::Reset()
{
	for (int32 C = 0; C < BOX_REACT_NUM; ++C)
	{
		X[C] = Vel[C] = 0.f;
	}
}

bool FBoxerReactionRig::IsActive() const
{
	for (int32 C = 0; C < BOX_REACT_NUM; ++C)
	{
		if (X[C] != 0.f || Vel[C] != 0.f)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Применение к позе
// ---------------------------------------------------------------------------------------------

void FBoxerPoseFx::Resolve(const FBoneContainer& Bones)
{
	if (Serial == Bones.GetSerialNumber() && ContainerPtr == &Bones)
	{
		return;
	}
	Serial = Bones.GetSerialNumber();
	ContainerPtr = &Bones;
	Pelvis = BoxerFeelImpl::Find(Bones, TEXT("pelvis"));
	Spine[0] = BoxerFeelImpl::Find(Bones, TEXT("spine_03"));
	Spine[1] = BoxerFeelImpl::Find(Bones, TEXT("spine_04"));
	Spine[2] = BoxerFeelImpl::Find(Bones, TEXT("spine_05"));
	if (!BoxerFeelImpl::Ok(Spine[2]))
	{
		// Скелет UE4/Manny-подобный с тремя позвонками.
		Spine[0] = BoxerFeelImpl::Find(Bones, TEXT("spine_01"));
		Spine[1] = BoxerFeelImpl::Find(Bones, TEXT("spine_02"));
		Spine[2] = BoxerFeelImpl::Find(Bones, TEXT("spine_03"));
	}
	Neck = BoxerFeelImpl::Find(Bones, TEXT("neck_01"));
	Head = BoxerFeelImpl::Find(Bones, TEXT("head"));
	const TCHAR* Sfx[2] = {TEXT("_l"), TEXT("_r")};
	for (int32 S = 0; S < 2; ++S)
	{
		Thigh[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("thigh")) + Sfx[S]));
		Calf[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("calf")) + Sfx[S]));
		Foot[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("foot")) + Sfx[S]));
		UpperArm[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("upperarm")) + Sfx[S]));
		LowerArm[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("lowerarm")) + Sfx[S]));
		Hand[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("hand")) + Sfx[S]));
		Clav[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("clavicle")) + Sfx[S])); // S-75
	}
}

void FBoxerPoseFx::Apply(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug)
{
	Resolve(Pose.GetBoneContainer());
	if (!BoxerFeelImpl::Ok(Pelvis))
	{
		return;
	}
	// S-75: плотный блок / пробитый блок — до записи «сырых» перчаток: соперник целится в перчатки там, где они в блоке.
	ApplyGuard(Pose, Frame, CompToWorld);
	ApplyClinch(Pose, Frame, CompToWorld); // S-76/S-75
	if (OutDebug && BoxerFeelImpl::Ok(Head) && BoxerFeelImpl::Ok(Spine[0]) && BoxerFeelImpl::Ok(Hand[0]) && BoxerFeelImpl::Ok(Hand[1]))
	{
		OutDebug->bRaw = true;
		OutDebug->RawHead = CompToWorld.TransformPosition(BoxerFeelImpl::CS(Pose, Head).GetLocation());
		OutDebug->RawChest = CompToWorld.TransformPosition(BoxerFeelImpl::CS(Pose, Spine[0]).GetLocation());
		OutDebug->RawHandL = CompToWorld.TransformPosition(BoxerFeelImpl::CS(Pose, Hand[0]).GetLocation());
		OutDebug->RawHandR = CompToWorld.TransformPosition(BoxerFeelImpl::CS(Pose, Hand[1]).GetLocation());
	}
	auto R = [&Frame](EBoxReactChannel C) { return Frame.React[static_cast<int32>(C)]; };
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Frame.Fwd).GetSafeNormal();
	const FVector Rt = CompToWorld.InverseTransformVectorNoScale(Frame.Right).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const FVector L = -Rt;

	// --- 1. Смещение таза: отшатывание + подшаг в удар + подсевшие колени ---
	FVector Off = -F * (R(EBoxReactChannel::Back) * 100.f) + L * (R(EBoxReactChannel::Side) * 100.f);
	// S-75: нырок — таз вбок, колени подсели (ниже), корпус в сторону и чуть вперёд (шаг 1б).
	const BoxerFeel::FSlipPose SP = BoxerFeel::SlipPose(Frame.Slip);
	Off += L * SP.SideCm;

	const int32 Arm = Frame.bLeftArm ? 0 : 1;
	const bool bAim = Frame.bAim && Frame.AimWeight > 0.f && BoxerFeelImpl::Ok(UpperArm[Arm]) && BoxerFeelImpl::Ok(LowerArm[Arm]) && BoxerFeelImpl::Ok(Hand[Arm]);
	FVector Surface = FVector::ZeroVector;
	FVector Approach = FVector::ForwardVector;
	float Lunge = 0.f;
	float Lean = 0.f;
	FVector LeanDir = F;
	if (bAim)
	{
		Surface = CompToWorld.InverseTransformPosition(Frame.AimSurface);
		Approach = CompToWorld.InverseTransformVectorNoScale(Frame.AimApproach).GetSafeNormal();
		const FVector S = BoxerFeelImpl::CS(Pose, UpperArm[Arm]).GetLocation();
		const FVector E = BoxerFeelImpl::CS(Pose, LowerArm[Arm]).GetLocation();
		const FVector H = BoxerFeelImpl::CS(Pose, Hand[Arm]).GetLocation();
		const float ArmLen = FVector::Dist(S, E) + FVector::Dist(E, H);
		const FVector Th = Surface - Approach * Frame.FistReachCm;
		// Прямые дотягиваются выпрямленной рукой; хук и апперкот держат согнутый локоть клипа — недостающее добирает подшаг.
		const float Comfort = Frame.bBentArm ? FMath::Clamp(FVector::Dist(H, S), 0.55f * ArmLen, REACH_FRAC * ArmLen) : REACH_FRAC * ArmLen;
		const float Need = FVector::Dist(Th, S) - Comfort;
		FVector Dir = Th - S;
		Dir -= U * FVector::DotProduct(Dir, U);
		Dir = Dir.GetSafeNormal();
		// Подшаг только вперёд (к сопернику) и только когда рука не достаёт.
		if (Need > 0.f && FVector::DotProduct(Dir, F) > 0.2f)
		{
			const float Full = FMath::Min(Need, Frame.MaxLungeCm);
			Lunge = Full * FMath::Max(Frame.ReachWeight, 0.6f * Frame.AimWeight);
			// S-70 (web applyLean): часть выноса берёт наклон корпуса — таз уходит от стоящих ступней меньше, ноги реже
			// переставляются (выпад-шаг — только сверх порога поглощения коленом, FootPlant SplitLunge).
			static const bool bNoLean = FParse::Param(FCommandLine::Get(), TEXT("BoxNoLean")); // A/B
			const float LeanK = bNoLean ? 0.f : LEAN_TYPE[FMath::Clamp(Frame.PunchKind, 0, 2)];
			auto LeanOf = [LeanK](float L) { return FMath::Min(LEAN_MAX, L * LEAN_SHARE / LEAN_ARM_CM) * LeanK; };
			Lean = LeanOf(Lunge);
			LeanDir = Dir;
			Off += Dir * (Lunge - Lean * LEAN_ARM_CM);
			if (OutDebug)
			{
				// Пик выноса таза в этом ударе (известен до контакта) — ступня выпада встаёт туда одним шагом (holdLunge).
				OutDebug->bLungePeak = true;
				OutDebug->LungePeakCS = Dir * (Full - LeanOf(Full) * LEAN_ARM_CM);
			}
		}
	}

	// Колени: бедро вперёд, голень назад, стопа — обратно в пол; таз опускается на подъём ступней.
	const float Knee = R(EBoxReactChannel::Knee) + SP.Knee;
	if (FMath::Abs(Knee) > 1e-4f && BoxerFeelImpl::Ok(Thigh[0]) && BoxerFeelImpl::Ok(Thigh[1]))
	{
		float Y0 = 0.f;
		for (int32 S = 0; S < 2; ++S)
		{
			Y0 += BoxerFeelImpl::Ok(Foot[S]) ? FVector::DotProduct(BoxerFeelImpl::CS(Pose, Foot[S]).GetLocation(), U) : 0.f;
		}
		const FQuat Fw = BoxerFeelImpl::Turn(-U, F, Knee);       // бедро: «низ» к «вперёд»
		const FQuat Bk = BoxerFeelImpl::Turn(-U, F, -2.f * Knee); // голень назад
		for (int32 S = 0; S < 2; ++S)
		{
			BoxerFeelImpl::RotateCS(Pose, Thigh[S], Fw);
			BoxerFeelImpl::RotateCS(Pose, Calf[S], Bk);
			BoxerFeelImpl::RotateCS(Pose, Foot[S], Fw);
		}
		float Y1 = 0.f;
		for (int32 S = 0; S < 2; ++S)
		{
			Y1 += BoxerFeelImpl::Ok(Foot[S]) ? FVector::DotProduct(BoxerFeelImpl::CS(Pose, Foot[S]).GetLocation(), U) : 0.f;
		}
		Off -= U * FMath::Max(0.f, (Y1 - Y0) * 0.5f);
	}
	if (!Off.IsNearlyZero(0.01f))
	{
		const FQuat Qp = BoxerFeelImpl::ParentRotCS(Pose, Pelvis);
		Pose[Pelvis].AddToTranslation(Qp.UnrotateVector(Off));
	}
	// S-70: наклон корпуса к цели (доля выноса удара), по позвонкам 0.4 / 0.35 / 0.25.
	if (Lean > 1e-4f)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			BoxerFeelImpl::RotateCS(Pose, Spine[I], BoxerFeelImpl::Turn(U, LeanDir, Lean * LEAN_SPLIT[I]));
		}
	}
	// --- 1б. S-75: нырок — корпус кренится в сторону ухода и чуть вперёд (голова уходит с линии вбок и вниз) ---
	if (SP.Roll != 0.f || SP.Bend != 0.f)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			BoxerFeelImpl::RotateCS(Pose, Spine[I], BoxerFeelImpl::Turn(U, Rt, SP.Roll * LEAN_SPLIT[I]) * BoxerFeelImpl::Turn(U, F, SP.Bend * LEAN_SPLIT[I]));
		}
	}

	// --- 2. Корпус и голова по вектору удара (аддитивно поверх клипа) ---
	const float TP = R(EBoxReactChannel::TorsoPitch), TR = R(EBoxReactChannel::TorsoRoll), TY = R(EBoxReactChannel::TorsoYaw);
	if (TP != 0.f || TR != 0.f || TY != 0.f)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			const float K = TORSO_SPLIT[I];
			BoxerFeelImpl::RotateCS(Pose, Spine[I], BoxerFeelImpl::Turn(F, L, TY * K) * BoxerFeelImpl::Turn(U, Rt, TR * K) * BoxerFeelImpl::Turn(F, U, TP * K));
		}
	}
	const float HP = R(EBoxReactChannel::HeadPitch), HR = R(EBoxReactChannel::HeadRoll), HY = R(EBoxReactChannel::HeadYaw);
	if (HP != 0.f || HR != 0.f || HY != 0.f)
	{
		BoxerFeelImpl::RotateCS(Pose, Neck, BoxerFeelImpl::Turn(F, L, HY * NECK_SPLIT) * BoxerFeelImpl::Turn(U, Rt, HR * NECK_SPLIT) * BoxerFeelImpl::Turn(F, U, HP * NECK_SPLIT));
		BoxerFeelImpl::RotateCS(Pose, Head, BoxerFeelImpl::Turn(F, L, HY * HEAD_SPLIT) * BoxerFeelImpl::Turn(U, Rt, HR * HEAD_SPLIT) * BoxerFeelImpl::Turn(F, U, HP * HEAD_SPLIT));
	}

	// --- 2б. S-62: удар соперника идёт мимо блока (не в перчатки) — моя перчатка на его пути отводится в сторону, а не
	// проходит сквозь его кулак/предплечье (кулак доходит до головы между перчатками). ---
	if (Frame.bThreat && Frame.ThreatW > 0.f)
	{
		for (int32 S = 0; S < 2; ++S)
		{
			if ((bAim && S == Arm) || !BoxerFeelImpl::Ok(UpperArm[S]) || !BoxerFeelImpl::Ok(LowerArm[S]) || !BoxerFeelImpl::Ok(Hand[S]))
			{
				continue;
			}
			const FTransform UcS = BoxerFeelImpl::CS(Pose, UpperArm[S]);
			const FTransform LcS = BoxerFeelImpl::CS(Pose, LowerArm[S]);
			const FVector Sh = UcS.GetLocation();
			const FVector El = LcS.GetLocation();
			const FVector Hd = BoxerFeelImpl::CS(Pose, Hand[S]).GetLocation();
			const float LenU = FVector::Dist(Sh, El);
			const float LenL = FVector::Dist(El, Hd);
			if (LenU < 1.f || LenL < 1.f)
			{
				continue;
			}
			// Центр перчатки — перед костью кисти по предплечью; всё сравнение — в мире (радиусы с масштабом облика).
			const FVector GloveCS = Hd + (Hd - El).GetSafeNormal() * 6.f;
			const FVector GloveW = CompToWorld.TransformPosition(GloveCS);
			const FVector PushW = BoxerFeel::GuardPush(GloveW, Frame.ThreatA, Frame.ThreatB, Frame.ThreatClear, 18.f) * Frame.ThreatW;
			if (PushW.SizeSquared() < 0.25f)
			{
				continue;
			}
			const FVector Push = CompToWorld.InverseTransformVector(PushW);
			FVector NewE, NewH;
			const FVector Goal = Hd + Push;
			AnimationCore::SolveTwoBoneIK(Sh, El, Hd, El + (El - (Sh + Hd) * 0.5f).GetSafeNormal() * 30.f, Goal, NewE, NewH, LenU, LenL, false, 1.0, 1.0);
			const FQuat Qu = FQuat::FindBetweenNormals((El - Sh).GetSafeNormal(), (NewE - Sh).GetSafeNormal());
			const FQuat NewU = (Qu * UcS.GetRotation()).GetNormalized();
			const FQuat Ql = FQuat::FindBetweenNormals(Qu.RotateVector(Hd - El).GetSafeNormal(), (NewH - NewE).GetSafeNormal());
			const FQuat NewL = (Ql * Qu * LcS.GetRotation()).GetNormalized();
			Pose[UpperArm[S]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, UpperArm[S]).Inverse() * NewU).GetNormalized());
			Pose[LowerArm[S]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, LowerArm[S]).Inverse() * NewL).GetNormalized());
			if (OutDebug)
			{
				OutDebug->GuardPushCm = FMath::Max(OutDebug->GuardPushCm, static_cast<float>(PushW.Size()));
			}
		}
	}

	// --- 2в. S-78: головы не входят друг в друга, лицо — в его корпус (BoxerContact.cpp) ---
	ApplySeparation(Pose, Frame, CompToWorld, OutDebug);

	// --- 3. Наведение бьющей руки: доворот на цель + кулак ровно до поверхности (упор, не насквозь) ---
	if (!bAim)
	{
		PrevElbowBend[0] = PrevElbowBend[1] = FVector::ZeroVector; // S-74: метрика скачка — только внутри удара
	}
	if (bAim)
	{
		const FTransform UcS = BoxerFeelImpl::CS(Pose, UpperArm[Arm]);
		const FTransform LcS = BoxerFeelImpl::CS(Pose, LowerArm[Arm]);
		const FTransform HcS = BoxerFeelImpl::CS(Pose, Hand[Arm]);
		const FVector S = UcS.GetLocation();
		const FVector E = LcS.GetLocation();
		const FVector H = HcS.GetLocation();
		const float LenU = FVector::Dist(S, E);
		const float LenL = FVector::Dist(E, H);
		const FVector Th = Surface - Approach * Frame.FistReachCm;
		const FVector DT = Th - S;
		const FVector Rr = H - S;
		const float Lc = Rr.Size();
		const float Dist = DT.Size();
		if (Lc > 1.f && Dist > 1.f && LenU > 1.f && LenL > 1.f)
		{
			const float Lt = FMath::Min(Dist, REACH_FRAC * (LenU + LenL));
			const FVector DirC = Rr / Lc;
			const FVector DirT = DT / Dist;
			const FQuat Part = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(DirC, DirT), Frame.AimWeight);
			const FVector Dir = Part.RotateVector(DirC);
			// Длина: клип вытянул бы руку глубже поверхности — упор (до Lt); в кадре контакта — ровно Lt.
			float Len = Lc > Lt ? FMath::Lerp(Lc, Lt, Frame.AimWeight) : Lc;
			Len = FMath::Lerp(Len, Lt, Frame.ReachWeight);
			const FVector Goal = S + Dir * Len;

			// Полюс — плоскость сгиба клипа (локоть); прямая рука — локоть вниз.
			// S-74 (QA S-78 №10): у почти прямой руки сгиб клипа — шум, и полюс по нему переворачивал локоть на 115–174° за
			// кадр в конце удара. Сгиб клипа берётся с весом по его величине (1 → 5 см отступа локтя от линии плечо–кисть) и
			// не «внутрь/вверх»; остальное — устойчивое «вниз-наружу».
			const FVector Dsh = (H - S).GetSafeNormal();
			FVector ElbOff = (E - S) - Dsh * FVector::DotProduct(E - S, Dsh);
			const float OffCm = static_cast<float>(ElbOff.Size());
			const FVector Safe = (-U * 0.8f + (Arm == 0 ? L : Rt) * 0.6f).GetSafeNormal();
			const FVector ClipDir = ElbOff.GetSafeNormal();
			const float Rel = BoxerFeelImpl::Smooth(1.f, 5.f, OffCm) * BoxerFeelImpl::Smooth(-0.3f, 0.2f, static_cast<float>(FVector::DotProduct(ClipDir, Safe)));
			FVector PoleDir = (Safe * (1.f - Rel) + ClipDir * Rel).GetSafeNormal();
			if (PoleDir.IsNearlyZero())
			{
				PoleDir = Safe;
			}
			FVector Pole = E + PoleDir * 30.f;
			// Хук: локоть сзади кулака по ходу удара (предплечье поперёк), чуть ниже; апперкот — локоть под кулаком.
			if (Frame.PunchKind == 1)
			{
				Pole = FMath::Lerp(Pole, Goal - Approach * 40.f - U * 8.f, Frame.AimWeight);
			}
			else if (Frame.PunchKind == 2)
			{
				Pole = FMath::Lerp(Pole, Goal - U * 40.f - Approach * 10.f, Frame.AimWeight);
			}
			FVector NewE, NewH;
			AnimationCore::SolveTwoBoneIK(S, E, H, Pole, Goal, NewE, NewH, LenU, LenL, false, 1.0, 1.0);

			const FQuat Qu = FQuat::FindBetweenNormals((E - S).GetSafeNormal(), (NewE - S).GetSafeNormal());
			const FQuat NewU = (Qu * UcS.GetRotation()).GetNormalized();
			const FQuat Ql = FQuat::FindBetweenNormals(Qu.RotateVector(H - E).GetSafeNormal(), (NewH - NewE).GetSafeNormal());
			const FQuat NewL = (Ql * Qu * LcS.GetRotation()).GetNormalized();
			// Локальные: кость = Parent_CS⁻¹ · CS. Кисть наследует (локальная не меняется).
			Pose[UpperArm[Arm]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, UpperArm[Arm]).Inverse() * NewU).GetNormalized());
			Pose[LowerArm[Arm]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, LowerArm[Arm]).Inverse() * NewL).GetNormalized());

			if (OutDebug)
			{
				const FVector HandNow = BoxerFeelImpl::CS(Pose, Hand[Arm]).GetLocation();
				OutDebug->FistGapCm = FVector::DotProduct(Th - HandNow, Approach);
				OutDebug->LungeCm = Lunge;
				OutDebug->AimW = Frame.AimWeight;
				OutDebug->ReachW = Frame.ReachWeight;
				OutDebug->FistFront = CompToWorld.TransformPosition(HandNow + Dir * Frame.FistReachCm);
				OutDebug->Elbow = CompToWorld.TransformPosition(BoxerFeelImpl::CS(Pose, LowerArm[Arm]).GetLocation());
				// S-74: локоть после наведения — сгиб от сгиба клипа (переворот локтя) и вверх ли он смотрит.
				const FVector Cb = BoxerFeel::BendOf(S, E, H, 4.f); // сгиб клипа — только заметный (почти прямая рука — шум)
				const FVector Nb = BoxerFeel::BendOf(S, BoxerFeelImpl::CS(Pose, LowerArm[Arm]).GetLocation(), HandNow);
				// Скачок сгиба за кадр (переворот локтя): прошлый кадр этой руки в том же ударе.
				OutDebug->ElbowJumpDeg = KNEE_DEG_NONE;
				if (!Nb.IsNearlyZero() && !PrevElbowBend[Arm].IsNearlyZero())
				{
					OutDebug->ElbowJumpDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Nb, PrevElbowBend[Arm])), -1.f, 1.f)));
				}
				PrevElbowBend[Arm] = Nb;
				PrevElbowBend[1 - Arm] = FVector::ZeroVector;
				bool bOk = false;
				const float A = (!Cb.IsNearlyZero() && !Nb.IsNearlyZero()) ? BoxerFeel::BendAngle(HandNow - S, Nb, Cb, bOk) : 0.f;
				OutDebug->ElbowDeg = bOk ? FMath::RadiansToDegrees(A) : KNEE_DEG_NONE;
				OutDebug->ElbowUp = static_cast<float>(FVector::DotProduct(Nb, U));
				OutDebug->ElbowOut = static_cast<float>(FVector::DotProduct(Nb, Arm == 0 ? L : Rt));
				OutDebug->ElbowKind = FMath::Clamp(Frame.PunchKind, 0, 2);
			}
		}
	}
	// --- 4. S-78: упор перчаток в его голову/шею/корпус (все руки), итоговое тело — сопернику (BoxerContact.cpp) ---
	ApplyHandStop(Pose, Frame, CompToWorld, OutDebug);
}

// ---------------------------------------------------------------------------------------------
// Маска верха тела
// ---------------------------------------------------------------------------------------------

void FBoxerUpperMask::Resolve(const FBoneContainer& Bones, FName BlendRoot)
{
	if (Serial == Bones.GetSerialNumber() && ContainerPtr == &Bones)
	{
		return;
	}
	Serial = Bones.GetSerialNumber();
	ContainerPtr = &Bones;
	const int32 RootMesh = Bones.GetPoseBoneIndexForBoneName(BlendRoot);
	Root = RootMesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(RootMesh));
	const int32 Num = Bones.GetCompactPoseNumBones();
	Kind.SetNumZeroed(Num);
	if (!IsValid())
	{
		return;
	}
	const FCompactPoseBoneIndex ClavL = BoxerFeelImpl::Find(Bones, TEXT("clavicle_l"));
	const FCompactPoseBoneIndex ClavR = BoxerFeelImpl::Find(Bones, TEXT("clavicle_r"));
	// Родитель в компактной позе всегда раньше ребёнка — один проход сверху вниз.
	for (int32 I = 0; I < Num; ++I)
	{
		const FCompactPoseBoneIndex B(I);
		if (B == Root)
		{
			Kind[I] = 1;
			continue;
		}
		if (B == ClavL || B == ClavR)
		{
			Kind[I] = 2;
			continue;
		}
		const FCompactPoseBoneIndex P = Bones.GetParentBoneIndex(B);
		Kind[I] = P.GetInt() != INDEX_NONE ? Kind[P.GetInt()] : 0;
	}
}

// ---------------------------------------------------------------------------------------------
// S-70: ступни — планировщик BoxFoot + двухзвенная IK ног (порт plantFeet/legIk веба)
// ---------------------------------------------------------------------------------------------

namespace BoxerFeelImpl
{
	// SkinnedFighter.tsx веба (м, рад, с) — 1:1.
	constexpr float FOOT_STEP_LEAD = 0.09f;   // упреждение точки стойки по скорости корпуса (с)
	constexpr float FOOT_WALK_LEAD = 0.2f;    // ходьба постановки — шаг «через» опорную ногу
	constexpr float FOOT_WALK_HALF_W = 0.1f;  // ходьба: ступни по бокам от оси хода (м, эталонный рост)
	constexpr float FOOT_HEEL_REAR = 0.2f;    // задняя пятка приподнята — стойка на подушечке задней ноги
	constexpr float FOOT_HEEL_LEAD = 0.03f;
	constexpr float FOOT_HEEL_PIVOT = 0.3f;   // пивот на подушечке — пятка выше
	constexpr float FOOT_WEIGHT_SHIFT = 0.22f; // доля смещения таза к опорной ступне на переносе
	constexpr float FOOT_WEIGHT_SHIFT_MAX = 0.04f;
	constexpr float FOOT_STEP_DIP = 0.014f;   // таз «пружинит» вниз на каждом шаге
	constexpr float FOOT_REACH_HEEL_MAX = 0.7f; // нога не дотягивается: сперва пятка (рад)…
	constexpr float FOOT_HIP_DROP_MAX = 0.035f; // …потом таз не глубже (м)
	constexpr float FOOT_FOLLOW_TH_PUNCH = 0.06f;
	constexpr float FOOT_DRAG_HEEL = 0.38f;   // подтяг волоком — пятка вверх, носок по настилу
	constexpr float FOOT_DRAG_V = 2.5f;       // м/с: предел подтягивания стоящей ступни волоком
	constexpr float FOOT_STANCE_RATE = 1.5f;  // 1/с: обучение стойки по спокойным кадрам
	constexpr float FOOT_CALM_MIN = 0.25f;    // с спокойных кадров до включения IK (стойка известна)
	// Боксёрская стойка правши (м, на эталонный рост; X — к сопернику, Y — вправо от бойца; от центра таза) и курс ступней
	// (рад от курса бойца, + — носок вправо): передняя впереди-слева носком чуть внутрь, задняя сзади-справа развёрнута.
	const FVector2D FOOT_STANCE_LEAD(0.17f, -0.1f);
	const FVector2D FOOT_STANCE_REAR(-0.19f, 0.1f);
	constexpr float FOOT_STANCE_CROUCH = 0.06f; // м: таз ниже, чем у позы GASP (она «стоя») — колени согнуты
	constexpr float FOOT_STANCE_LEAD_YAW = 0.45f;
	constexpr float FOOT_STANCE_REAR_YAW = 0.95f;
	constexpr float FOOT_LUNGE_ABSORB_K = 1.8f; // S-74: множитель порогов поглощения выпада (FootPlant LEAD/REAR_ABSORB)
	constexpr float FOOT_HIP_BLADE = 0.35f; // таз боком (рад, + — вправо): корпус (spine_01 и выше) не трогаем
	// Пивот ступни в ударе (рад веба, + — носок влево): задняя — на ударах правой, передняя — на левом хуке.
	constexpr float TWIST_LEAD[4] = {0.f, 0.f, -0.45f, -0.2f}; // джеб, кросс, хук, апперкот
	constexpr float TWIST_REAR[4] = {0.45f, 0.5f, 0.45f, 0.35f};

	float SmoothTo(float Cur, float Target, float Rate, float Dt)
	{
		return Cur + (Target - Cur) * (1.f - FMath::Exp(-Rate * Dt));
	}

	FVector FlatN(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.f).GetSafeNormal();
	}
}

void FBoxerFootIk::Reset()
{
	*this = FBoxerFootIk();
}

void FBoxerFootIk::Resolve(const FBoneContainer& Bones)
{
	if (Serial == Bones.GetSerialNumber() && ContainerPtr == &Bones)
	{
		return;
	}
	Serial = Bones.GetSerialNumber();
	ContainerPtr = &Bones;
	Pelvis = BoxerFeelImpl::Find(Bones, TEXT("pelvis"));
	Spine1 = BoxerFeelImpl::Find(Bones, TEXT("spine_01"));
	const TCHAR* Sfx[2] = {TEXT("_l"), TEXT("_r")};
	for (int32 S = 0; S < 2; ++S)
	{
		Thigh[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("thigh")) + Sfx[S]));
		Calf[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("calf")) + Sfx[S]));
		Foot[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("foot")) + Sfx[S]));
		Ball[S] = BoxerFeelImpl::Find(Bones, *(FString(TEXT("ball")) + Sfx[S]));
	}
	// S-74: ось коленной чашечки в осях бедра — из позы привязки: «вперёд» ступни (щиколотка → подушечка) поперёк бедра.
	bKneeAxis = false;
	auto RefCS = [&Bones](FCompactPoseBoneIndex I)
	{
		FTransform T = Bones.GetRefPoseTransform(I);
		for (FCompactPoseBoneIndex P = Bones.GetParentBoneIndex(I); P.GetInt() != INDEX_NONE; P = Bones.GetParentBoneIndex(P))
		{
			T = T * Bones.GetRefPoseTransform(P);
		}
		return T;
	};
	if (BoxerFeelImpl::Ok(Thigh[0]) && BoxerFeelImpl::Ok(Thigh[1]) && BoxerFeelImpl::Ok(Calf[0]) && BoxerFeelImpl::Ok(Calf[1]) && BoxerFeelImpl::Ok(Foot[0]) && BoxerFeelImpl::Ok(Foot[1]) && BoxerFeelImpl::Ok(Ball[0]) && BoxerFeelImpl::Ok(Ball[1]))
	{
		bKneeAxis = true;
		for (int32 S = 0; S < 2; ++S)
		{
			const FTransform Th = RefCS(Thigh[S]);
			const FVector Ax = (RefCS(Calf[S]).GetLocation() - Th.GetLocation()).GetSafeNormal();
			FVector Fw = RefCS(Ball[S]).GetLocation() - RefCS(Foot[S]).GetLocation();
			Fw -= Ax * FVector::DotProduct(Fw, Ax);
			if (Fw.SizeSquared() < 1.f)
			{
				bKneeAxis = false;
				break;
			}
			KneeAxisLocal[S] = Th.GetRotation().UnrotateVector(Fw.GetSafeNormal());
		}
	}
}

void FBoxerFootIk::Apply(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& C2W, float Dt, FBoxerFeelDebug* OutDebug)
{
	ApplyInner(Pose, Frame, C2W, Dt, OutDebug);
	// Замер (-BoxFootLog): подушечки в мире ровно в этой оценке позы (тот же трансформ компонента, что у картинки).
	++Seq;
	if (OutDebug && BoxerFeelImpl::Ok(Ball[0]) && BoxerFeelImpl::Ok(Ball[1]))
	{
		OutDebug->bBallW = true;
		OutDebug->EvalSeq = Seq;
		OutDebug->EvalDt = Dt;
		for (int32 S = 0; S < 2; ++S)
		{
			OutDebug->BallW[S] = C2W.TransformPosition(BoxerFeelImpl::CS(Pose, Ball[S]).GetLocation());
		}
	}
}

void FBoxerFootIk::ApplyInner(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& C2W, float Dt, FBoxerFeelDebug* OutDebug)
{
	Resolve(Pose.GetBoneContainer());
	if (!BoxerFeelImpl::Ok(Pelvis))
	{
		return;
	}
	for (int32 S = 0; S < 2; ++S)
	{
		if (!BoxerFeelImpl::Ok(Thigh[S]) || !BoxerFeelImpl::Ok(Calf[S]) || !BoxerFeelImpl::Ok(Foot[S]) || !BoxerFeelImpl::Ok(Ball[S]))
		{
			return;
		}
	}
	Dt = FMath::Max(0.f, Dt);
	const float DtS = FMath::Min(Dt, 0.1f); // сглаживание: длинный кадр не «перескакивает»
	// S-74: защита колена от выворота (A/B: -BoxKneeGuard=0 — как в S-70).
	static const bool bKneeGuardCfg = [] { int32 V = 1; FParse::Value(FCommandLine::Get(), TEXT("BoxKneeGuard="), V); return V != 0; }();
	const bool bKneeGuard = bKneeGuardCfg;
	const FVector UpCS = C2W.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const float Scale = FMath::Max(0.1f, static_cast<float>(C2W.GetScale3D().X));
	const FQuat CQ = C2W.GetRotation();

	// --- 1. Стойка: учится по спокойным кадрам позы (до IK), в пространстве компонента ---
	FVector AnkCS[2], BallCS[2];
	FQuat FootQCS[2];
	for (int32 S = 0; S < 2; ++S)
	{
		const FTransform F = BoxerFeelImpl::CS(Pose, Foot[S]);
		AnkCS[S] = F.GetLocation();
		FootQCS[S] = F.GetRotation();
		BallCS[S] = BoxerFeelImpl::CS(Pose, Ball[S]).GetLocation();
	}
	const FVector PelCS = BoxerFeelImpl::CS(Pose, Pelvis).GetLocation();
	if (Frame.bFeetCalm && Dt > 0.f)
	{
		CalmTime += Dt;
	}
	if (!bStance || (Frame.bFeetCalm && Dt > 0.f))
	{
		const float K = bStance ? 1.f - FMath::Exp(-FOOT_STANCE_RATE * Dt) : 1.f;
		for (int32 S = 0; S < 2; ++S)
		{
			StAnkle[S] = bStance ? FMath::Lerp(StAnkle[S], AnkCS[S], K) : AnkCS[S];
			StBall[S] = bStance ? FMath::Lerp(StBall[S], BallCS[S], K) : BallCS[S];
			StFootQ[S] = bStance ? FQuat::Slerp(StFootQ[S], FootQCS[S], K).GetNormalized() : FootQCS[S];
		}
		StPelvis = bStance ? FMath::Lerp(StPelvis, PelCS, K) : PelCS;
		bStance = true;
	}

	const bool bWant = Frame.bFeetOn && CalmTime >= FOOT_CALM_MIN;
	IkW = BoxerFeelImpl::SmoothTo(IkW, bWant ? 1.f : 0.f, bWant ? 6.f : 20.f, DtS);
	if (IkW < 0.02f)
	{
		// Лёжа / вставая / сидя — ноги клипа; встал — ступни заново встают в стойку (снимок без шагов).
		Gait.bReady = false;
		bPrevBody = false;
		Track = BoxFoot::FStepTrack();
		Hold[0] = Hold[1] = BoxFoot::FLungeHold();
		WsX = WsY = Dip = HipDrop = 0.f;
		ReachHeel[0] = ReachHeel[1] = Twist[0] = Twist[1] = 0.f;
		return;
	}

	// --- 2. Точки стойки в мире ---
	const FVector BodyW = C2W.GetLocation();
	const FVector Fwd = BoxerFeelImpl::FlatN(Frame.Fwd);
	const FVector Right = BoxerFeelImpl::FlatN(Frame.Right);
	const float BodyYaw = FMath::Atan2(Fwd.Y, Fwd.X);
	const float Rs = FMath::Max(0.5f, Frame.FeetScale);
	FVector StAnkW[2], StBallW[2];
	float StYaw[2];
	float ToeLen = 0.f, ToeDrop = 0.f; // м (мир)
	for (int32 S = 0; S < 2; ++S)
	{
		StAnkW[S] = C2W.TransformPosition(StAnkle[S]);
		StBallW[S] = C2W.TransformPosition(StBall[S]);
		const FVector D = StBallW[S] - StAnkW[S];
		StYaw[S] = FMath::Atan2(D.Y, D.X);
		ToeLen += 0.5f * FVector(D.X, D.Y, 0.f).Size() / 100.f;
		ToeDrop += 0.5f * static_cast<float>(-D.Z) / 100.f;
	}
	ToeLen = FMath::Max(ToeLen, 0.05f);
	// Стойка ног у GASP — обычная («квадратная»: ступни рядом, носки вперёд), не боксёрская: место и курс ступней —
	// боксёрская стойка (FOOT_STANCE_*: передняя впереди и носком внутрь, задняя сзади и развёрнута), от центра таза
	// спокойной позы; высота щиколотки, длина стопы и поворот ступни — из позы. Левша — зеркально (передняя правая).
	const int32 LeadS = Frame.bFeetSouthpaw ? 1 : 0;
	const int32 SideOf[2] = {LeadS, 1 - LeadS};
	const float Mirror = LeadS == 1 ? -1.f : 1.f;
	FVector Center = C2W.TransformPosition(StPelvis);
	Center.Z = BodyW.Z;
	FVector StGoalW[2];
	float StGoalYaw[2];
	for (int32 I = 0; I < 2; ++I)
	{
		const int32 S = SideOf[I];
		const FVector2D& P = I == 0 ? FOOT_STANCE_LEAD : FOOT_STANCE_REAR;
		StGoalW[S] = Center + (Fwd * P.X + Right * (P.Y * Mirror)) * (100.f * Rs);
		StGoalYaw[S] = BodyYaw + (I == 0 ? FOOT_STANCE_LEAD_YAW : FOOT_STANCE_REAR_YAW) * Mirror;
	}
	if (Frame.bFeetPoseStance)
	{
		// Не боец (рефери): стойка — как в позе (обычная), по спокойным кадрам.
		for (int32 S = 0; S < 2; ++S)
		{
			StGoalW[S] = StAnkW[S];
			StGoalYaw[S] = StYaw[S];
		}
	}
	if (OutDebug)
	{
		for (int32 S = 0; S < 2; ++S)
		{
			const FVector D = StAnkW[S] - BodyW;
			OutDebug->StanceCm[S] = FVector2D(FVector::DotProduct(D, Fwd), FVector::DotProduct(D, Right));
			OutDebug->StanceYawDeg[S] = FMath::RadiansToDegrees(BoxFoot::WrapAngle(StYaw[S] - BodyYaw));
		}
	}

	WalkMix = BoxerFeelImpl::SmoothTo(WalkMix, Frame.bFeetWalking ? 1.f : 0.f, 8.f, DtS);
	const float Wm = WalkMix;
	const bool bWalking = Frame.bFeetWalking;
	const float CrouchCm = Frame.bFeetPoseStance ? 0.f : FOOT_STANCE_CROUCH * Rs * 100.f * (1.f - Wm); // колени согнуты: таз ниже позы GASP (мир, см)
	BoxFoot::FGoal Goals[2];
	for (int32 I = 0; I < 2; ++I)
	{
		const int32 S = SideOf[I];
		// Ходьба постановки: ступни по бокам оси хода, носками вперёд (левая — слева).
		const FVector Wk = Center + Right * ((S == 0 ? -1.f : 1.f) * FOOT_WALK_HALF_W * Rs * 100.f);
		Goals[I].X = static_cast<float>(StGoalW[S].X * (1.f - Wm) + Wk.X * Wm) / 100.f;
		Goals[I].Y = static_cast<float>(StGoalW[S].Y * (1.f - Wm) + Wk.Y * Wm) / 100.f;
		Goals[I].Yaw = BodyYaw + BoxFoot::WrapAngle(StGoalYaw[S] - BodyYaw) * (1.f - Wm);
	}

	// --- 3. Выпад удара / отдача реакции: таз ушёл от стойки (подшаг FBoxerPoseFx, отшатывание, таз клипа) ---
	const FVector LungeW = C2W.TransformVector(PelCS - StPelvis) / 100.f;
	const float LX = FVector::DotProduct(LungeW, Fwd);
	const float LY = FVector::DotProduct(LungeW, Right);
	if (OutDebug) { OutDebug->FootLungeCm[0] = LX * 100.f; OutDebug->FootLungeCm[1] = LY * 100.f; }
	// Пик выноса удара известен до контакта (FBoxerPoseFx): ступня выпада встаёт туда сразу одним шагом, а не догоняет таз.
	const bool bPeak = OutDebug && OutDebug->bLungePeak && Frame.bFeetPunch && Frame.FeetPunchPhase < 0.5f;
	const FVector PeakW = bPeak ? C2W.TransformVector(OutDebug->LungePeakCS) / 100.f : FVector::ZeroVector;
	BoxFoot::FGaitOpts O;
	// S-74: поглощение выпада коленом/пяткой — на FOOT_LUNGE_ABSORB_K больше, чем в вебе (A/B: -BoxLungeAbsorb=1): у UE
	// наклон корпуса и подшаг крупнее, выпад-шаг «в пик и обратно» давал 2–4 лишних переноса на шаг ядра.
	static const float AbsorbK = [] { float V = FOOT_LUNGE_ABSORB_K; FParse::Value(FCommandLine::Get(), TEXT("BoxLungeAbsorb="), V); return FMath::Clamp(V, 0.5f, 3.f); }();
	const float RsA = Rs * AbsorbK;
	for (int32 I = 0; I < 2; ++I)
	{
		BoxFoot::FV2 St, Sl;
		BoxFoot::SplitLunge(I, LX, LY, RsA, St, Sl);
		if (bPeak)
		{
			BoxFoot::FV2 Pk, PkSl;
			BoxFoot::SplitLunge(I, FVector::DotProduct(PeakW, Fwd), FVector::DotProduct(PeakW, Right), RsA, Pk, PkSl);
			if (Pk.Len() > St.Len())
			{
				St = Pk;
			}
		}
		if (bWalking)
		{
			St = Sl = BoxFoot::FV2();
		}
		BoxFoot::HoldLunge(Hold[I], St, Gait.Feet[I].bSwing && FMath::Abs(Hold[I].X) + FMath::Abs(Hold[I].Y) > 1e-3f);
		O.Lunge[I] = BoxFoot::FV2(Fwd.X * Hold[I].X + Right.X * Hold[I].Y, Fwd.Y * Hold[I].X + Right.Y * Hold[I].Y);
		O.Slide[I] = BoxFoot::FV2(Fwd.X * Sl.X + Right.X * Sl.Y, Fwd.Y * Sl.X + Right.Y * Sl.Y);
	}

	// --- 4. Ход корпуса и шаг ядра ---
	const bool bLive = Dt > 0.f && Dt < 0.5f && bPrevBody;
	const float BX = static_cast<float>(BodyW.X) / 100.f, BY = static_cast<float>(BodyW.Y) / 100.f;
	const float DX = bLive ? BX - static_cast<float>(PrevBody.X) : 0.f;
	const float DY = bLive ? BY - static_cast<float>(PrevBody.Y) : 0.f;
	const BoxFoot::FV2 Ahead = BoxFoot::TrackStep(Track, bLive && !bWalking ? Frame.FeetStep : 0, DX, DY, bLive ? Dt : 0.f);
	if (bLive)
	{
		const float K = 1.f - FMath::Exp(-14.f * Dt);
		VX += (DX / Dt - VX) * K;
		VY += (DY / Dt - VY) * K;
	}
	else if (Dt >= 0.5f)
	{
		VX = VY = 0.f;
	}
	if (Dt > 0.f || !bPrevBody)
	{
		PrevBody = FVector2D(BX, BY);
		bPrevBody = true;
	}

	// Вытянутость ног до их ступней: нога на пределе — ступню переставляют.
	float LegLen[2];
	for (int32 I = 0; I < 2; ++I)
	{
		const int32 S = SideOf[I];
		const FVector H = BoxerFeelImpl::CS(Pose, Thigh[S]).GetLocation();
		const FVector K = BoxerFeelImpl::CS(Pose, Calf[S]).GetLocation();
		LegLen[I] = FVector::Dist(H, K) + FVector::Dist(K, AnkCS[S]);
		const BoxFoot::FFootNow F = BoxFoot::FootNow(Gait.Feet[I]);
		const FVector T = C2W.InverseTransformPosition(FVector(F.X * 100.f, F.Y * 100.f, StAnkW[S].Z));
		O.Stretch[I] = FVector::Dist(H - UpCS * (CrouchCm * IkW / Scale), T) / FMath::Max(1.f, LegLen[I]);
	}
	const bool bRhythm = !bWalking && BoxFoot::InStepRhythm(Track);
	O.Dt = Dt;
	O.VX = VX;
	O.VY = VY;
	O.bWalking = bWalking;
	O.ToeLen = ToeLen;
	O.Lead = bWalking ? FOOT_WALK_LEAD : (bRhythm ? 0.f : FOOT_STEP_LEAD);
	O.FollowTh = BoxFoot::FOLLOW_TH + FOOT_FOLLOW_TH_PUNCH * (Frame.bFeetPunch ? 1.f : 0.f);
	O.bAhead = bRhythm;
	O.Ahead = Ahead;
	const int32 Sw0 = Gait.Swings;
	BoxFoot::UpdateGait(Gait, Goals, O);
	if (Gait.Swings > Sw0)
	{
		// Отладка: в каком контексте начат перенос — удар, выпад (шаг в пик), шаг ядра, разворот на месте, ход корпуса.
		Ctx[0] += Frame.bFeetPunch ? 1 : 0;
		Ctx[1] += (Hold[0].Pk > 0.f || Hold[1].Pk > 0.f) ? 1 : 0;
		Ctx[2] += Track.bOn ? 1 : 0;
		Ctx[3] += FMath::Abs(Gait.WY) > 0.25f ? 1 : 0;
		Ctx[4] += FMath::Sqrt(VX * VX + VY * VY) > 0.18f ? 1 : 0;
	}
	BoxFoot::FFootNow Now[2] = {BoxFoot::FootNow(Gait.Feet[0]), BoxFoot::FootNow(Gait.Feet[1])};

	// Таз боком (боксёрская стойка): поворот таза вокруг вертикали; корпус (spine_01 и выше) остаётся, где был.
	{
		const float Blade = Frame.bFeetPoseStance ? 0.f : FOOT_HIP_BLADE * Mirror * (1.f - Wm) * IkW;
		if (BoxerFeelImpl::Ok(Spine1) && FMath::Abs(Blade) > 1e-4f)
		{
			const FTransform PelOld = BoxerFeelImpl::CS(Pose, Pelvis);
			const FTransform SpOld = BoxerFeelImpl::CS(Pose, Spine1);
			const FQuat NewPelQ = (FQuat(UpCS, Blade) * PelOld.GetRotation()).GetNormalized();
			Pose[Pelvis].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, Pelvis).Inverse() * NewPelQ).GetNormalized());
			FTransform SpLocal = SpOld.GetRelativeTransform(BoxerFeelImpl::CS(Pose, Pelvis));
			SpLocal.SetScale3D(Pose[Spine1].GetScale3D());
			Pose[Spine1] = SpLocal;
		}
	}

	// --- 5. Таз: перенос веса к опорной ступне и «пружина» на шаге ---
	const FVector CenterOff = ((StGoalW[0] + StGoalW[1]) * 0.5f - BodyW) / 100.f * (1.f - Wm);
	float WX = 0.f, WY = 0.f, DipT = 0.f;
	for (int32 I = 0; I < 2; ++I)
	{
		const float U = Now[I].U;
		if (U < 0.f)
		{
			continue;
		}
		const BoxFoot::FFootNow& Sup = Now[1 - I];
		const float K = FMath::Sin(PI * U) * FOOT_WEIGHT_SHIFT * (1.f - Wm * 0.6f);
		WX += (Sup.X - BX - static_cast<float>(CenterOff.X)) * K;
		WY += (Sup.Y - BY - static_cast<float>(CenterOff.Y)) * K;
		DipT = FMath::Max(DipT, FMath::Sin(PI * U));
	}
	const float WL = FMath::Sqrt(WX * WX + WY * WY);
	if (WL > FOOT_WEIGHT_SHIFT_MAX)
	{
		WX *= FOOT_WEIGHT_SHIFT_MAX / WL;
		WY *= FOOT_WEIGHT_SHIFT_MAX / WL;
	}
	WsX = BoxerFeelImpl::SmoothTo(WsX, WX, 14.f, DtS);
	WsY = BoxerFeelImpl::SmoothTo(WsY, WY, 14.f, DtS);
	Dip = BoxerFeelImpl::SmoothTo(Dip, DipT * FOOT_STEP_DIP * Rs, 18.f, DtS);
	FVector PelOffW(WsX * 100.f * IkW, WsY * 100.f * IkW, -CrouchCm * IkW);

	// Нога не дотягивается до стоящей ступни: пятка встаёт (носок на настиле), остаток — таз чуть ниже.
	const float ToeLenCS = ToeLen * 100.f / Scale;
	const FVector OffCS0 = C2W.InverseTransformVector(PelOffW);
	float Need = 0.f; // см (компонент)
	for (int32 I = 0; I < 2; ++I)
	{
		const int32 S = SideOf[I];
		float Lack = 0.f;
		if (Now[I].U < 0.f)
		{
			const FVector H = BoxerFeelImpl::CS(Pose, Thigh[S]).GetLocation() + OffCS0;
			const FVector T = C2W.InverseTransformPosition(FVector(Now[I].X * 100.f, Now[I].Y * 100.f, StAnkW[S].Z));
			const FVector D = T - H;
			const float Up = FVector::DotProduct(-D, UpCS);
			const float Hor = (D + UpCS * Up).Size();
			const float Reach = LegLen[I] * 0.97f;
			Lack = FMath::Max(0.f, Up - FMath::Sqrt(FMath::Max(0.f, Reach * Reach - Hor * Hor)));
		}
		const float Heel = FMath::Asin(FMath::Min(FMath::Sin(FOOT_REACH_HEEL_MAX), Lack / FMath::Max(1e-3f, ToeLenCS)));
		ReachHeel[I] = BoxerFeelImpl::SmoothTo(ReachHeel[I], Heel, Heel > ReachHeel[I] ? 60.f : 10.f, DtS);
		Need = FMath::Max(Need, Lack - ToeLenCS * FMath::Sin(Heel));
	}
	const float Drop = FMath::Min(FOOT_HIP_DROP_MAX * Rs, Need * Scale / 100.f);
	HipDrop = BoxerFeelImpl::SmoothTo(HipDrop, Drop, Drop > HipDrop ? 45.f : 8.f, DtS);
	PelOffW.Z -= (HipDrop + Dip) * 100.f * IkW;
	const FVector OffCS = C2W.InverseTransformVector(PelOffW);
	if (!OffCS.IsNearlyZero(0.01f))
	{
		Pose[Pelvis].AddToTranslation(BoxerFeelImpl::ParentRotCS(Pose, Pelvis).UnrotateVector(OffCS));
	}

	// Не хватило и этого (корпус резко ушёл от стоящей ступни): ступня подтягивается по настилу (волоком), не быстрее DRAG_V.
	for (int32 I = 0; I < 2 && IkW > 0.5f; ++I)
	{
		BoxFoot::FFoot& F = Gait.Feet[I];
		if (F.bSwing)
		{
			continue;
		}
		const int32 S = SideOf[I];
		const FVector H = BoxerFeelImpl::CS(Pose, Thigh[S]).GetLocation();
		const FVector T = C2W.InverseTransformPosition(FVector(F.X * 100.f, F.Y * 100.f, StAnkW[S].Z));
		const FVector D = T - H;
		const float Up = FVector::DotProduct(-D, UpCS) - ToeLenCS * FMath::Sin(ReachHeel[I]);
		const float MaxR = LegLen[I] * 0.995f;
		const float MaxD = FMath::Sqrt(FMath::Max(0.f, MaxR * MaxR - Up * Up));
		const FVector HorV = D - UpCS * FVector::DotProduct(D, UpCS);
		const float Dist = HorV.Size();
		if (Dist <= MaxD + 0.1f || Dist < 0.01f)
		{
			continue;
		}
		const float Mv = FMath::Min(Dist - MaxD, FOOT_DRAG_V * 100.f / Scale * DtS);
		const FVector NewW = C2W.TransformPosition(T - HorV / Dist * Mv);
		F.X = static_cast<float>(NewW.X) / 100.f;
		F.Y = static_cast<float>(NewW.Y) / 100.f;
		Now[I].X = F.X;
		Now[I].Y = F.Y;
	}

	// Удар «от ноги»: опорная ступня бьющей стороны пивотирует на подушечке.
	{
		const float Ph = Frame.FeetPunchPhase;
		const float Pp = Frame.bFeetPunch && Ph > 0.f && Ph < 1.f ? FMath::Pow(FMath::Sin(PI * Ph), 0.7f) : 0.f;
		float Tw[2] = {0.f, 0.f};
		if (Pp > 0.f)
		{
			const int32 Kind = FMath::Clamp(Frame.FeetPunchKind, 0, 3);
			const int32 Arm = Frame.bFeetRearArm ? 1 : 0;
			// Веб: + — носок влево; курс UE растёт вправо (к +Y) — знак обратный; левша — зеркально.
			Tw[Arm] = -(Arm == 1 ? TWIST_REAR[Kind] : TWIST_LEAD[Kind]) * Mirror * Pp * (1.f - Wm);
		}
		for (int32 I = 0; I < 2; ++I)
		{
			Twist[I] = BoxerFeelImpl::SmoothTo(Twist[I], Tw[I], 24.f, DtS);
		}
	}

	// --- 6. IK ног: щиколотка — в точку ступни, курс — планировщика, пятки ---
	for (int32 I = 0; I < 2; ++I)
	{
		const int32 S = SideOf[I];
		const BoxFoot::FFootNow& F = Now[I];
		const BoxFoot::FFoot& FootSt = Gait.Feet[I];
		const float SwingHeel = F.U >= 0.f ? FMath::Sin(PI * F.U) * (F.bDrag ? FOOT_DRAG_HEEL : 0.12f) : 0.f;
		const float Base = Frame.bFeetPoseStance ? 0.f : (I == 1 ? FOOT_HEEL_REAR : FOOT_HEEL_LEAD) * (1.f - Wm);
		const float Tw = F.U < 0.f ? Twist[I] : 0.f;
		const float BallX = F.X + FMath::Cos(F.Yaw) * ToeLen;
		const float BallY = F.Y + FMath::Sin(F.Yaw) * ToeLen;
		const float Fy = F.Yaw + Tw;
		const float Heel = FMath::Max(Base, F.U < 0.f ? ReachHeel[I] : 0.f) + FMath::Max(FootSt.Pivot, FMath::Abs(Tw) * 2.f) * FOOT_HEEL_PIVOT + SwingHeel;
		if (OutDebug) { OutDebug->FootHeel[S] = Heel; OutDebug->FootStretch[S] = O.Stretch[I]; }
		const FVector FyDir(FMath::Cos(Fy), FMath::Sin(Fy), 0.f);
		// Пятка встаёт вокруг подушечки (носок на месте): щиколотка вверх и к носку.
		const float A = ToeLen * 100.f, B = ToeDrop * 100.f;
		const float Ch = FMath::Cos(Heel), Sh = FMath::Sin(Heel);
		FVector TW((BallX - FMath::Cos(Fy) * ToeLen) * 100.f, (BallY - FMath::Sin(Fy) * ToeLen) * 100.f, StAnkW[S].Z + F.Lift * 100.f);
		TW.Z += A * Sh - B * (1.f - Ch);
		TW += FyDir * (A * (1.f - Ch) + B * Sh);

		const FTransform ThCS = BoxerFeelImpl::CS(Pose, Thigh[S]);
		const FTransform CaCS = BoxerFeelImpl::CS(Pose, Calf[S]);
		const FVector Hip = ThCS.GetLocation();
		const FVector Knee = CaCS.GetLocation();
		const FVector Ank = BoxerFeelImpl::CS(Pose, Foot[S]).GetLocation();
		const FVector Goal = FMath::Lerp(Ank, C2W.InverseTransformPosition(TW), IkW);
		const float L1 = FVector::Dist(Hip, Knee);
		const float L2 = FVector::Dist(Knee, Ank);
		FVector D = Goal - Hip;
		if (D.SizeSquared() < 1e-4f || L1 < 1.f || L2 < 1.f)
		{
			continue;
		}
		D.Normalize();
		// Плоскость сгиба: колено клипа (отступ от линии бедро→цель) + курс ступни (колено над носком). S-74: колено клипа,
		// смотрящее назад от носка (клип хода GASP назад заносит ногу, а ступня стоит), не в счёт; итог — не дальше 35° от
		// курса ступни (колено «разворачивалось в другую сторону» при шаге назад).
		const FVector FootFwdCS = C2W.InverseTransformVectorNoScale(FyDir);
		FVector Inward = BoxerFeelImpl::CS(Pose, Thigh[1 - S]).GetLocation() - Hip;
		Inward -= UpCS * FVector::DotProduct(Inward, UpCS);
		const FVector N = BoxerFeel::KneeBendDir(Hip, Knee, Goal, FootFwdCS, L1, L2, 1.f - Wm, bKneeGuard, 0.61f, Inward.GetSafeNormal());
		if (OutDebug)
		{
			bool bOk = false;
			const FVector Cb = BoxerFeel::BendOf(Hip, Knee, Ank);
			const float Ca = Cb.IsNearlyZero() ? 0.f : BoxerFeel::BendAngle(Ank - Hip, Cb, FootFwdCS, bOk);
			OutDebug->ClipKneeDeg[S] = bOk ? FMath::RadiansToDegrees(Ca) : KNEE_DEG_NONE;
		}
		const FVector Pole = (Hip + Goal) * 0.5f + N * 50.f;
		FVector NewK, NewA;
		AnimationCore::SolveTwoBoneIK(Hip, Knee, Ank, Pole, Goal, NewK, NewA, L1, L2, false, 1.0, 1.0);
		FQuat Qu = FQuat::FindBetweenNormals((Knee - Hip).GetSafeNormal(), (NewK - Hip).GetSafeNormal());
		// S-74: бедро довернуть вокруг своей оси — коленная чашечка (ось из позы привязки: «вперёд» ступни) в плоскость
		// сгиба. Иначе кратчайший поворот бедра оставлял чашечку там, где её держал клип, и колено гнулось вбок/назад.
		const FVector ThAxis = (NewK - Hip).GetSafeNormal();
		if (bKneeGuard && bKneeAxis && !ThAxis.IsNearlyZero())
		{
			bool bOk = false;
			const FVector Cap = (Qu * ThCS.GetRotation()).RotateVector(KneeAxisLocal[S]);
			const float CapTw = BoxerFeel::BendAngle(ThAxis, N, Cap, bOk);
			if (bOk)
			{
				Qu = (FQuat(ThAxis, CapTw * IkW) * Qu).GetNormalized();
			}
		}
		const FQuat NewTh = (Qu * ThCS.GetRotation()).GetNormalized();
		const FQuat Ql = FQuat::FindBetweenNormals(Qu.RotateVector(Ank - Knee).GetSafeNormal(), (NewA - NewK).GetSafeNormal());
		const FQuat NewCa = (Ql * Qu * CaCS.GetRotation()).GetNormalized();
		Pose[Thigh[S]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, Thigh[S]).Inverse() * NewTh).GetNormalized());
		Pose[Calf[S]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, Calf[S]).Inverse() * NewCa).GetNormalized());
		if (OutDebug)
		{
			// S-74: колено после IK — куда согнуто относительно носка и куда смотрит чашечка относительно сгиба.
			const FVector K2 = BoxerFeelImpl::CS(Pose, Calf[S]).GetLocation();
			const FVector A2 = BoxerFeelImpl::CS(Pose, Foot[S]).GetLocation();
			const FVector Bd = BoxerFeel::BendOf(Hip, K2, A2);
			bool bOk = false;
			const float Dev = Bd.IsNearlyZero() ? 0.f : BoxerFeel::BendAngle(A2 - Hip, Bd, FootFwdCS, bOk);
			OutDebug->KneeDeg[S] = bOk ? FMath::RadiansToDegrees(Dev) : KNEE_DEG_NONE;
			bool bOk2 = false;
			const float Cap = (bKneeAxis && !Bd.IsNearlyZero()) ? BoxerFeel::BendAngle(K2 - Hip, Bd, NewTh.RotateVector(KneeAxisLocal[S]), bOk2) : 0.f;
			OutDebug->KneecapDeg[S] = bOk2 ? FMath::RadiansToDegrees(Cap) : KNEE_DEG_NONE;
		}

		// Ступня: стойка, довёрнутая на курс планировщика, и подъём пятки (носок вниз вокруг поперечной оси).
		FQuat Qw = FQuat(FVector::UpVector, BoxFoot::WrapAngle(Fy - StYaw[S])) * (CQ * StFootQ[S]);
		const FVector HeelAxisW = FVector::CrossProduct(FVector::UpVector, FyDir).GetSafeNormal();
		if (FMath::Abs(Heel) > 1e-4f)
		{
			Qw = FQuat(HeelAxisW, Heel) * Qw;
		}
		const FQuat QcsT = (CQ.Inverse() * Qw).GetNormalized();
		const FQuat Qcur = BoxerFeelImpl::CS(Pose, Foot[S]).GetRotation();
		const FQuat Qnew = FQuat::Slerp(Qcur, QcsT, IkW).GetNormalized();
		Pose[Foot[S]].SetRotation((BoxerFeelImpl::ParentRotCS(Pose, Foot[S]).Inverse() * Qnew).GetNormalized());
		if (OutDebug) { OutDebug->FootErrCm[S] = FVector::Dist(C2W.TransformPosition(BoxerFeelImpl::CS(Pose, Foot[S]).GetLocation()), TW); }
		// Пальцы — обратно в настил: подушечка стоит, пятка поднята.
		if (FMath::Abs(Heel) > 1e-4f)
		{
			const FVector AxisCS = CQ.Inverse().RotateVector(HeelAxisW);
			BoxerFeelImpl::RotateCS(Pose, Ball[S], FQuat(AxisCS, -Heel * IkW));
		}
		// S-74 (QA S-78 №12): подушечка не под настилом — ниже настила (уровень подушечек стойки) — носок вверх вокруг щиколотки.
		{
			const float FloorZ = static_cast<float>(FMath::Min(StBallW[0].Z, StBallW[1].Z));
			const FVector BallNow = C2W.TransformPosition(BoxerFeelImpl::CS(Pose, Ball[S]).GetLocation());
			const FVector AnkNow = C2W.TransformPosition(BoxerFeelImpl::CS(Pose, Foot[S]).GetLocation());
			const float Under = FloorZ - static_cast<float>(BallNow.Z);
			const float Arm = static_cast<float>(FVector::Dist(BallNow, AnkNow));
			if (Under > 0.3f && Arm > 3.f && IkW > 0.f)
			{
				const FVector AxisW = FVector::CrossProduct(FVector::UpVector, (BallNow - AnkNow).GetSafeNormal2D()).GetSafeNormal();
				const float Lift = FMath::Asin(FMath::Min(1.f, Under / Arm)) * IkW;
				BoxerFeelImpl::RotateCS(Pose, Foot[S], FQuat(CQ.Inverse().RotateVector(AxisW), -Lift));
			}
		}
	}

	if (OutDebug)
	{
		OutDebug->bFeet = true;
		OutDebug->FeetW = IkW;
		OutDebug->LeadSide = LeadS;
		for (int32 I = 0; I < 2; ++I)
		{
			OutDebug->FootU[SideOf[I]] = Now[I].U;
			OutDebug->bFootDrag[SideOf[I]] = Now[I].bDrag;
		}
		OutDebug->FootSwings = Gait.Swings;
		OutDebug->FootDrags = Gait.Drags;
		OutDebug->HipDropCm = HipDrop * 100.f;
		for (int32 K = 0; K < 6; ++K) { OutDebug->FootWhy[K] = Gait.Why[K]; }
		for (int32 K = 0; K < 5; ++K) { OutDebug->FootCtx[K] = Ctx[K]; }
	}
}
