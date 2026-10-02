#include "BoxerFeel.h"

#include "BonePose.h"
#include "BoneContainer.h"
#include "TwoBoneIK.h"

namespace
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
		return Par.GetInt() != INDEX_NONE ? CS(P, Par).GetRotation() : FQuat::Identity;
	}

	// Повернуть кость вокруг оси компонента (D — поворот в осях компонента), дети следуют.
	void RotateCS(FCompactPose& P, FCompactPoseBoneIndex I, const FQuat& D)
	{
		if (!Ok(I))
		{
			return;
		}
		const FQuat Qp = ParentRotCS(P, I);
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
		// В перчатки: руки вдавливает в лицо, корпус откидывает, полшага назад.
		const float B = FMath::Min(1.3f, 0.45f + 0.6f * FMath::Max(0.f, Mag));
		K[C::HeadPitch] = 0.07f * B;
		K[C::TorsoPitch] = 0.06f * B;
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

void BoxerFeel::PunchEnvelopes(float Phase, float ContactFrac, float& OutAim, float& OutReach)
{
	const float C = FMath::Clamp(ContactFrac, 0.05f, 0.95f);
	const float P = FMath::Clamp(Phase, 0.f, 1.f);
	if (P < C)
	{
		const float U = P / C;
		OutAim = Smooth(0.15f, 0.75f, U);
		OutReach = Smooth(0.5f, 1.f, U);
	}
	else
	{
		const float V = (P - C) / (1.f - C);
		OutAim = 1.f - Smooth(0.25f, 0.95f, V);
		OutReach = 1.f - Smooth(0.1f, 0.6f, V);
	}
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
			Vel[C] += K.V[C] * GSprings[C].W / PeakFactor(GSprings[C].Z);
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
	Pelvis = Find(Bones, TEXT("pelvis"));
	Spine[0] = Find(Bones, TEXT("spine_03"));
	Spine[1] = Find(Bones, TEXT("spine_04"));
	Spine[2] = Find(Bones, TEXT("spine_05"));
	if (!Ok(Spine[2]))
	{
		// Скелет UE4/Manny-подобный с тремя позвонками.
		Spine[0] = Find(Bones, TEXT("spine_01"));
		Spine[1] = Find(Bones, TEXT("spine_02"));
		Spine[2] = Find(Bones, TEXT("spine_03"));
	}
	Neck = Find(Bones, TEXT("neck_01"));
	Head = Find(Bones, TEXT("head"));
	const TCHAR* Sfx[2] = {TEXT("_l"), TEXT("_r")};
	for (int32 S = 0; S < 2; ++S)
	{
		Thigh[S] = Find(Bones, *(FString(TEXT("thigh")) + Sfx[S]));
		Calf[S] = Find(Bones, *(FString(TEXT("calf")) + Sfx[S]));
		Foot[S] = Find(Bones, *(FString(TEXT("foot")) + Sfx[S]));
		UpperArm[S] = Find(Bones, *(FString(TEXT("upperarm")) + Sfx[S]));
		LowerArm[S] = Find(Bones, *(FString(TEXT("lowerarm")) + Sfx[S]));
		Hand[S] = Find(Bones, *(FString(TEXT("hand")) + Sfx[S]));
	}
}

void FBoxerPoseFx::Apply(FCompactPose& Pose, const FBoxerFeelFrame& Frame, const FTransform& CompToWorld, FBoxerFeelDebug* OutDebug)
{
	Resolve(Pose.GetBoneContainer());
	if (!Ok(Pelvis))
	{
		return;
	}
	if (OutDebug && Ok(Head) && Ok(Spine[0]) && Ok(Hand[0]) && Ok(Hand[1]))
	{
		OutDebug->bRaw = true;
		OutDebug->RawHead = CompToWorld.TransformPosition(CS(Pose, Head).GetLocation());
		OutDebug->RawChest = CompToWorld.TransformPosition(CS(Pose, Spine[0]).GetLocation());
		OutDebug->RawHandL = CompToWorld.TransformPosition(CS(Pose, Hand[0]).GetLocation());
		OutDebug->RawHandR = CompToWorld.TransformPosition(CS(Pose, Hand[1]).GetLocation());
	}
	auto R = [&Frame](EBoxReactChannel C) { return Frame.React[static_cast<int32>(C)]; };
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Frame.Fwd).GetSafeNormal();
	const FVector Rt = CompToWorld.InverseTransformVectorNoScale(Frame.Right).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	const FVector L = -Rt;

	// --- 1. Смещение таза: отшатывание + подшаг в удар + подсевшие колени ---
	FVector Off = -F * (R(EBoxReactChannel::Back) * 100.f) + L * (R(EBoxReactChannel::Side) * 100.f);

	const int32 Arm = Frame.bLeftArm ? 0 : 1;
	const bool bAim = Frame.bAim && Frame.AimWeight > 0.f && Ok(UpperArm[Arm]) && Ok(LowerArm[Arm]) && Ok(Hand[Arm]);
	FVector Surface = FVector::ZeroVector;
	FVector Approach = FVector::ForwardVector;
	float Lunge = 0.f;
	if (bAim)
	{
		Surface = CompToWorld.InverseTransformPosition(Frame.AimSurface);
		Approach = CompToWorld.InverseTransformVectorNoScale(Frame.AimApproach).GetSafeNormal();
		const FVector S = CS(Pose, UpperArm[Arm]).GetLocation();
		const FVector E = CS(Pose, LowerArm[Arm]).GetLocation();
		const FVector H = CS(Pose, Hand[Arm]).GetLocation();
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
			Lunge = FMath::Min(Need, Frame.MaxLungeCm) * FMath::Max(Frame.ReachWeight, 0.6f * Frame.AimWeight);
			Off += Dir * Lunge;
		}
	}

	// Колени: бедро вперёд, голень назад, стопа — обратно в пол; таз опускается на подъём ступней.
	const float Knee = R(EBoxReactChannel::Knee);
	if (FMath::Abs(Knee) > 1e-4f && Ok(Thigh[0]) && Ok(Thigh[1]))
	{
		float Y0 = 0.f;
		for (int32 S = 0; S < 2; ++S)
		{
			Y0 += Ok(Foot[S]) ? FVector::DotProduct(CS(Pose, Foot[S]).GetLocation(), U) : 0.f;
		}
		const FQuat Fw = Turn(-U, F, Knee);       // бедро: «низ» к «вперёд»
		const FQuat Bk = Turn(-U, F, -2.f * Knee); // голень назад
		for (int32 S = 0; S < 2; ++S)
		{
			RotateCS(Pose, Thigh[S], Fw);
			RotateCS(Pose, Calf[S], Bk);
			RotateCS(Pose, Foot[S], Fw);
		}
		float Y1 = 0.f;
		for (int32 S = 0; S < 2; ++S)
		{
			Y1 += Ok(Foot[S]) ? FVector::DotProduct(CS(Pose, Foot[S]).GetLocation(), U) : 0.f;
		}
		Off -= U * FMath::Max(0.f, (Y1 - Y0) * 0.5f);
	}
	if (!Off.IsNearlyZero(0.01f))
	{
		const FQuat Qp = ParentRotCS(Pose, Pelvis);
		Pose[Pelvis].AddToTranslation(Qp.UnrotateVector(Off));
	}

	// --- 2. Корпус и голова по вектору удара (аддитивно поверх клипа) ---
	const float TP = R(EBoxReactChannel::TorsoPitch), TR = R(EBoxReactChannel::TorsoRoll), TY = R(EBoxReactChannel::TorsoYaw);
	if (TP != 0.f || TR != 0.f || TY != 0.f)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			const float K = TORSO_SPLIT[I];
			RotateCS(Pose, Spine[I], Turn(F, L, TY * K) * Turn(U, Rt, TR * K) * Turn(F, U, TP * K));
		}
	}
	const float HP = R(EBoxReactChannel::HeadPitch), HR = R(EBoxReactChannel::HeadRoll), HY = R(EBoxReactChannel::HeadYaw);
	if (HP != 0.f || HR != 0.f || HY != 0.f)
	{
		RotateCS(Pose, Neck, Turn(F, L, HY * NECK_SPLIT) * Turn(U, Rt, HR * NECK_SPLIT) * Turn(F, U, HP * NECK_SPLIT));
		RotateCS(Pose, Head, Turn(F, L, HY * HEAD_SPLIT) * Turn(U, Rt, HR * HEAD_SPLIT) * Turn(F, U, HP * HEAD_SPLIT));
	}

	// --- 3. Наведение бьющей руки: доворот на цель + кулак ровно до поверхности (упор, не насквозь) ---
	if (bAim)
	{
		const FTransform UcS = CS(Pose, UpperArm[Arm]);
		const FTransform LcS = CS(Pose, LowerArm[Arm]);
		const FTransform HcS = CS(Pose, Hand[Arm]);
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
			const FVector Mid = (S + H) * 0.5f;
			FVector Pole = E - Mid;
			Pole = Pole.SizeSquared() > 4.f ? E + Pole.GetSafeNormal() * 30.f : E - U * 30.f;
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
			Pose[UpperArm[Arm]].SetRotation((ParentRotCS(Pose, UpperArm[Arm]).Inverse() * NewU).GetNormalized());
			Pose[LowerArm[Arm]].SetRotation((ParentRotCS(Pose, LowerArm[Arm]).Inverse() * NewL).GetNormalized());

			if (OutDebug)
			{
				const FVector HandNow = CS(Pose, Hand[Arm]).GetLocation();
				OutDebug->FistGapCm = FVector::DotProduct(Th - HandNow, Approach);
				OutDebug->LungeCm = Lunge;
				OutDebug->AimW = Frame.AimWeight;
				OutDebug->ReachW = Frame.ReachWeight;
				OutDebug->FistFront = CompToWorld.TransformPosition(HandNow + Dir * Frame.FistReachCm);
			}
		}
	}
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
	const FCompactPoseBoneIndex ClavL = Find(Bones, TEXT("clavicle_l"));
	const FCompactPoseBoneIndex ClavR = Find(Bones, TEXT("clavicle_r"));
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
