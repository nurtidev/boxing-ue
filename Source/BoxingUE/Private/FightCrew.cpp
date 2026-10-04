// Угловые в бою и сцена перерыва (S-71): порт web crew.ts + CornerCrew.tsx. См. FightCrew.h.
#include "FightCrew.h"

#include "BoxerCharacter.h"
#include "BoxerLook.h"
#include "BoxingFightGameMode.h"
#include "FightFx.h"
#include "FightStaging.h"
#include "Animation/AnimInstance.h"
#include "BonePose.h"
#include "BoneContainer.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/LODSyncComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Retargeter/IKRetargeter.h"
#include "UnrealClient.h"

namespace FightCrewImpl
{
	constexpr float TRACK_GAIN = 8.f;     // 1/с: догон ошибки места
	constexpr float TRACK_DEADBAND = 1.f; // см
	constexpr float MAX_SPEED = 260.f;    // см/с (подъём на апрон — быстрым шагом)
	constexpr float TURN_DPS = 220.f;     // доворот корпуса
	constexpr float HEAD_RATE = 5.f;
	constexpr float ARM_WALK_SPEED = 60.f; // быстрее — руки отдаём локомоции GASP

	float Smooth01(float X)
	{
		const float K = FMath::Clamp(X, 0.f, 1.f);
		return K * K * (3.f - 2.f * K);
	}

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

	void RotateCS(FCompactPose& P, FCompactPoseBoneIndex I, const FQuat& D)
	{
		if (!Ok(I))
		{
			return;
		}
		const FCompactPoseBoneIndex Par = P.GetParentBoneIndex(I);
		const FQuat Qp = Par.GetInt() != INDEX_NONE ? CS(P, Par).GetRotation() : FQuat::Identity;
		P[I].SetRotation((Qp.Inverse() * D * Qp * P[I].GetRotation()).GetNormalized());
	}

	FQuat Turn(const FVector& From, const FVector& To, float Angle)
	{
		const FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		return (Axis.IsNearlyZero() || FMath::Abs(Angle) < 1e-5f) ? FQuat::Identity : FQuat(Axis, Angle);
	}

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

	// Свойство GASP (BPI_SandboxCharacter_Pawn) — как у бойцов и рефери.
	void SetGaspInput(AActor* A, bool bWalk, bool bStrafe)
	{
		UFunction* Fn = A ? A->FindFunction(FName(TEXT("Set_CharacterInputState"))) : nullptr;
		if (!Fn || Fn->ParmsSize <= 0)
		{
			return;
		}
		uint8* Parms = static_cast<uint8*>(FMemory_Alloca(Fn->ParmsSize));
		FMemory::Memzero(Parms, Fn->ParmsSize);
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Parms);
			const FStructProperty* SP = CastField<FStructProperty>(*It);
			if (!SP)
			{
				continue;
			}
			void* StructPtr = SP->ContainerPtrToValuePtr<void>(Parms);
			for (TFieldIterator<FBoolProperty> B(SP->Struct); B; ++B)
			{
				const FString PName = B->GetName();
				if (PName.StartsWith(TEXT("WantsToWalk")))
				{
					B->SetPropertyValue_InContainer(StructPtr, bWalk);
				}
				else if (PName.StartsWith(TEXT("WantsToStrafe")))
				{
					B->SetPropertyValue_InContainer(StructPtr, bStrafe);
				}
			}
		}
		A->ProcessEvent(Fn, Parms);
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}
	}

	const TCHAR* CornerName(int32 C) { return C == 0 ? TEXT("Red") : TEXT("Blue"); }
	const TCHAR* RoleName(BoxCrew::ERole R) { return R == BoxCrew::ERole::Coach ? TEXT("Coach") : TEXT("Cutman"); }
}

// ---------------------------------------------------------------------------------------------
// Чистая логика (crew.ts)
// ---------------------------------------------------------------------------------------------

FVector BoxCrew::PlaceBetween(const FVector& A, const FVector& B, float U)
{
	const float K = FMath::Clamp(U, 0.f, 1.f);
	const float E = K * K * (3.f - 2.f * K);
	const float Ey = FMath::Min(1.f, K * 1.6f);
	const float Ty = Ey * Ey * (3.f - 2.f * Ey);
	return FVector(A.X + (B.X - A.X) * E, A.Y + (B.Y - A.Y) * E, A.Z + (B.Z - A.Z) * Ty);
}

FVector BoxCrew::PlaceVelocity(const FVector& A, const FVector& B, float U, float DuDt)
{
	const float K = FMath::Clamp(U, 0.f, 1.f);
	const float De = 6.f * K * (1.f - K);
	const float Ey = K * 1.6f;
	const float Dty = Ey < 1.f ? 1.6f * 6.f * Ey * (1.f - Ey) : 0.f;
	return FVector((B.X - A.X) * De * DuDt, (B.Y - A.Y) * De * DuDt, (B.Z - A.Z) * Dty * DuDt);
}

FVector BoxCrew::SpotCore(int32 Corner, ERole Role, bool bRest)
{
	// crew.ts crewSpot: X/Y здесь — оси ядра X/Z, Z — высота ступней (м).
	const double Rope = BoxingStaging::ROPE_HALF;
	const double Sx = Corner == 0 ? -1.0 : 1.0;
	const double Sz = Sx;
	const double Inset = BoxingStaging::ROPE_HALF - BoxingStaging::CORNER_SPOT;
	if (!bRest)
	{
		const double Out = Rope + 0.6 + 0.42;
		const double Along = Rope - 0.35;
		return Role == ERole::Coach ? FVector(Sx * Out, Sz * Along, -1.1) : FVector(Sx * (Rope + 0.35), Sz * Out, -1.1);
	}
	const double Out = Rope + 0.3;
	const double Along = Rope - Inset - 0.55;
	return Role == ERole::Coach ? FVector(Sx * Out, Sz * Along, 0.0) : FVector(Sx * Along, Sz * Out, 0.0);
}

FVector BoxCrew::StoolCore(int32 Corner, bool bStow)
{
	const double S = Corner == 0 ? -1.0 : 1.0;
	const double C = bStow ? 3.43 : BoxingStaging::CORNER_SPOT;
	return FVector(S * C, S * C, 0.0);
}

int32 BoxCrew::Reaction(const FFightEvent& E, FKick Out[2])
{
	if (E.Kind == EFightEventKind::Knockdown && E.Defender >= 0 && E.Defender <= 1)
	{
		Out[0] = {E.Defender, 0.f, 1.f};
		Out[1] = {1 - E.Defender, 1.f, 0.f};
		return 2;
	}
	if (E.Kind == EFightEventKind::FightEnd && E.Defender >= 0 && E.Defender <= 1)
	{
		Out[0] = {E.Defender, 0.f, 0.6f};
		Out[1] = {1 - E.Defender, 1.f, 0.f};
		return 2;
	}
	if (E.Kind != EFightEventKind::Hit || E.Magnitude < REACT_MAG || E.Defender < 0 || E.Defender > 1)
	{
		return 0;
	}
	const float K = FMath::Min(1.f, 0.55f + (E.Magnitude - REACT_MAG) * 0.5f);
	Out[0] = {1 - E.Defender, K, 0.f};
	Out[1] = {E.Defender, 0.f, K * 0.85f};
	return 2;
}

void BoxCrew::FMood::Kick(const FKick& K)
{
	// Не дёргаться на каждый удар серии: новый всплеск — только сильнее текущего.
	if (K.Cheer > Cheer * 0.8f)
	{
		Cheer = FMath::Max(Cheer, K.Cheer);
		CheerT = 0.f;
		CheerOsc = CheerW < 0.05f ? 0.f : CheerOsc; // S-78: жест уже идёт — качание не начинается заново
	}
	if (K.Worry > Worry * 0.8f)
	{
		Worry = FMath::Max(Worry, K.Worry);
		WorryT = 0.f;
		WorryOsc = WorryW < 0.05f ? 0.f : WorryOsc;
	}
}

void BoxCrew::FMood::Update(float Dt, bool bRest, bool bSeated)
{
	CheerT += Dt;
	WorryT += Dt;
	CheerOsc += Dt;
	WorryOsc += Dt;
	if (CheerT > 1.2f) Cheer = FMath::Max(0.f, Cheer - Dt * 1.4f);
	if (WorryT > 1.4f) Worry = FMath::Max(0.f, Worry - Dt * 1.2f);
	if (bRest)
	{
		Cheer = Worry = 0.f;
	}
	auto Step = [Dt](float Cur, float Want, float Rate) {
		return Want > Cur ? FMath::Min(Want, Cur + Rate * Dt) : FMath::Max(Want, Cur - Rate * Dt);
	};
	// S-78: видимая доля жеста — плавно (была скачком в кадр всплеска).
	CheerW = Step(CheerW, FMath::Min(1.f, Cheer * 1.4f), FMath::Min(1.f, Cheer * 1.4f) > CheerW ? MOOD_IN_RATE : MOOD_OUT_RATE);
	WorryW = Step(WorryW, FMath::Min(1.f, Worry * 1.4f), FMath::Min(1.f, Worry * 1.4f) > WorryW ? MOOD_IN_RATE : MOOD_OUT_RATE);
	Up = Step(Up, bRest ? 1.f : 0.f, bRest ? UP_RATE : DOWN_RATE);
	Lean = Step(Lean, bSeated && Up > 0.85f ? 1.f : 0.f, LEAN_RATE);
	Stool = Step(Stool, bRest && (bSeated || Up > 0.6f) ? 1.f : 0.f, STOOL_RATE);
	RestT = bSeated && bRest ? RestT + Dt : 0.f;
}

BoxCrew::FArm BoxCrew::MixArm(const FArm& A, const FArm& B, float K)
{
	FArm R;
	R.Fwd = A.Fwd + (B.Fwd - A.Fwd) * K;
	R.Out = A.Out + (B.Out - A.Out) * K;
	R.Elbow = A.Elbow + (B.Elbow - A.Elbow) * K;
	return R;
}

void BoxCrew::ArmDirs(const FArm& A, int32 Side, const FVector& F, const FVector& L, const FVector& U, FVector& OutUpper, FVector& OutFore)
{
	// Веб applyBody: из T-позы (рука вбок) — вниз на (ARM_HANG − out) вокруг «вперёд», потом подъём вперёд на fwd вокруг
	// «влево»; предплечье — ещё на elbow вокруг той же оси. Повороты вокруг L не меняют L-составляющую.
	const FVector Out = Side == 0 ? L : -L;
	const float Ang = ARM_HANG - A.Out;
	const float C = FMath::Cos(Ang), S = FMath::Sin(Ang);
	OutUpper = (Out * C + (-U * FMath::Cos(A.Fwd) + F * FMath::Sin(A.Fwd)) * S).GetSafeNormal();
	const float Fe = A.Fwd + A.Elbow;
	OutFore = (Out * C + (-U * FMath::Cos(Fe) + F * FMath::Sin(Fe)) * S).GetSafeNormal();
}

FQuat BoxCrew::LimitRotation(const FQuat& Prev, const FQuat& Want, float MaxRadPerSec, float Dt)
{
	if (Dt <= 0.f)
	{
		return Prev; // повторная оценка той же позы — не шаг
	}
	if (Dt > LIMIT_MAX_DT)
	{
		return Want;
	}
	const float Ang = static_cast<float>(Prev.AngularDistance(Want));
	const float Max = MaxRadPerSec * Dt;
	return Ang <= Max ? Want : FQuat::Slerp(Prev, Want, Max / Ang).GetNormalized();
}

FVector BoxCrew::StoolSpot(const FVector& In, const FVector& Boxer, const FVector& Post, const FVector& Seat, float SitW, float ClearCm)
{
	// До посадки: точка угла, но не ближе ClearCm к бойцу (по горизонтали) — выталкивается к столбу (за спину севшего).
	FVector A = In;
	const FVector D(In.X - Boxer.X, In.Y - Boxer.Y, 0.f);
	if (D.Size() < ClearCm)
	{
		FVector Dir(Post.X - Boxer.X, Post.Y - Boxer.Y, 0.f);
		Dir = Dir.GetSafeNormal();
		if (!Dir.IsNearlyZero())
		{
			A = FVector(Boxer.X + Dir.X * ClearCm, Boxer.Y + Dir.Y * ClearCm, In.Z);
		}
	}
	// Садится — въезжает под таз.
	const float K = FMath::Clamp(SitW, 0.f, 1.f);
	const float E = K * K * (3.f - 2.f * K);
	return FVector(FMath::Lerp(A.X, Seat.X, E), FMath::Lerp(A.Y, Seat.Y, E), In.Z);
}

void BoxCrew::CutmanScript(float RestT, float& OutBottle, float& OutWipe)
{
	// Цикл 7 с: 0.3–3.3 — бутылка к губам, 3.7–6.6 — протирает лицо (по 0.45 с на подход/отход).
	const float T = FMath::Fmod(FMath::Max(0.f, RestT), 7.f);
	auto Env = [T](float A, float B) {
		return FightCrewImpl::Smooth01((T - A) / 0.45f) * FightCrewImpl::Smooth01((B - T) / 0.45f);
	};
	OutBottle = RestT < 0.3f ? 0.f : Env(0.3f, 3.3f);
	OutWipe = Env(3.7f, 6.6f);
}

BoxCrew::FBody BoxCrew::Pose(ERole Role, const FMood& M, float T, float Phase, float YawToFighter, float PitchToFighter)
{
	const bool bCoach = Role == ERole::Coach;
	const float Ph = T + Phase;
	const float Breathe = FMath::Sin(Ph * 1.7f) * 0.02f;
	const FArm Down{0.08f, 0.f, 0.25f};
	// Бой: тренер опирается руками на край помоста (он на уровне груди), катмен — руки вниз.
	const FArm Ledge{1.0f, 0.12f, 0.6f};
	FArm L = bCoach ? Ledge : FArm{0.08f, 0.f, 0.45f};
	FArm R = bCoach ? Ledge : Down;
	float Lean = 0.08f + Breathe;
	// «Да!» — кулак вверх с качанием, кивок; тревога — руки к лицу (тренер показывает гард) / катмен за голову.
	const float Ch = M.CheerW; // S-78: плавная доля (не скачок)
	const float Nod = Ch * FMath::Sin(M.CheerOsc * 10.f) * 0.16f * FMath::Exp(-M.CheerOsc * 1.6f);
	if (Ch > 0.01f)
	{
		const float Pump = FMath::Sin(M.CheerOsc * 11.f) * 0.28f * FMath::Exp(-M.CheerOsc * 1.2f);
		R = MixArm(R, FArm{2.55f + Pump, 0.35f, 0.7f + Pump}, Ch);
		L = MixArm(L, FArm{bCoach ? 1.6f : 0.6f, 0.3f, 1.2f}, Ch * 0.6f);
	}
	const float Wo = M.WorryW;
	if (Wo > 0.01f)
	{
		const float Wave = FMath::Sin(M.WorryOsc * 9.f) * 0.18f * FMath::Exp(-M.WorryOsc * 0.9f);
		const FArm G = bCoach ? FArm{1.25f + Wave, 0.15f, 1.55f} : FArm{2.3f, 0.55f, 2.25f};
		L = MixArm(L, G, Wo);
		FArm G2 = G;
		G2.Fwd -= Wave * 2.f;
		R = MixArm(R, G2, Wo);
		Lean += 0.1f * Wo;
	}
	// Перерыв: наклон через верхний канат к бойцу. Тренер: левая — на канате, правая говорит; катмен — сценарий
	// (бутылка / протирает) — кисти ведёт IK, здесь — запасная поза рук.
	const float K = M.Lean;
	FBody B;
	if (K > 0.01f)
	{
		const float Talk = FMath::Sin(Ph * 4.2f) * 0.2f + FMath::Sin(Ph * 2.3f) * 0.12f;
		const FArm RestL = FArm{0.55f, 0.12f, 0.3f}; // на верхнем канате
		const FArm RestR = bCoach ? FArm{0.7f + Talk * 0.6f, 0.3f, 0.75f - Talk * 0.8f} : FArm{0.25f, 0.1f, 0.6f};
		L = MixArm(L, RestL, K);
		R = MixArm(R, RestR, K);
		Lean += ((bCoach ? 0.62f : 0.58f) - Lean) * K;
		if (!bCoach)
		{
			CutmanScript(M.RestT, B.Bottle, B.Wipe);
			B.Bottle *= K;
			B.Wipe *= K;
			Lean += 0.12f * B.Wipe + 0.3f * B.Bottle; // тянется через канат к лицу (S-78: с бутылкой — ниже, иначе не дотягивался)
		}
	}
	// Поднимается/спускается — руки к себе (не тянуться сквозь канаты).
	if (M.Up > 0.02f && M.Up < 0.98f)
	{
		const float Uu = FMath::Sin(M.Up * PI);
		L = MixArm(L, Down, Uu);
		R = MixArm(R, Down, Uu);
	}
	B.Lean = Lean;
	B.Twist = YawToFighter * 0.35f;
	B.HeadYaw = YawToFighter * 0.65f;
	B.HeadPitch = PitchToFighter * (1.f - K * 0.5f) + Nod;
	B.Arm[0] = L;
	B.Arm[1] = R;
	return B;
}

// ---------------------------------------------------------------------------------------------
// Поза на видимом меше
// ---------------------------------------------------------------------------------------------

void FCrewPoseFx::Resolve(const FBoneContainer& Bones)
{
	if (Serial == Bones.GetSerialNumber() && ContainerPtr == &Bones)
	{
		return;
	}
	Serial = Bones.GetSerialNumber();
	ContainerPtr = &Bones;
	LimitBones.Reset(); // S-78: индексы костей — заново
	LimitRate.Reset();
	Spine[0] = FightCrewImpl::FindBone(Bones, TEXT("spine_02"));
	Spine[1] = FightCrewImpl::FindBone(Bones, TEXT("spine_03"));
	Spine[2] = FightCrewImpl::FindBone(Bones, TEXT("spine_04"));
	Neck = FightCrewImpl::FindBone(Bones, TEXT("neck_01"));
	Head = FightCrewImpl::FindBone(Bones, TEXT("head"));
	static const TCHAR* UpperN[2] = {TEXT("upperarm_l"), TEXT("upperarm_r")};
	static const TCHAR* LowerN[2] = {TEXT("lowerarm_l"), TEXT("lowerarm_r")};
	static const TCHAR* HandN[2] = {TEXT("hand_l"), TEXT("hand_r")};
	for (int32 S = 0; S < 2; ++S)
	{
		Upper[S] = FightCrewImpl::FindBone(Bones, UpperN[S]);
		Lower[S] = FightCrewImpl::FindBone(Bones, LowerN[S]);
		Hand[S] = FightCrewImpl::FindBone(Bones, HandN[S]);
	}
}

void FCrewPoseFx::Apply(FCompactPose& Pose, const FCrewPoseFrame& Fr, const FTransform& CompToWorld)
{
	if (!Fr.bValid)
	{
		return;
	}
	Resolve(Pose.GetBoneContainer());
	if (!FightCrewImpl::Ok(Spine[0]))
	{
		return;
	}
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Fr.Fwd).GetSafeNormal();
	const FVector L = CompToWorld.InverseTransformVectorNoScale(Fr.Left).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	auto ToCS = [&CompToWorld](const FVector& W) { return CompToWorld.InverseTransformVectorNoScale(W).GetSafeNormal(); };

	// --- корпус: наклон и поворот к бойцу ---
	static const float Share[3] = {0.35f, 0.35f, 0.3f};
	for (int32 I = 0; I < 3; ++I)
	{
		FightCrewImpl::RotateCS(Pose, Spine[I], FightCrewImpl::Turn(F, L, Fr.Twist * Share[I] * Fr.BodyW) * FightCrewImpl::Turn(U, F, Fr.Lean * Share[I] * Fr.BodyW));
	}

	// --- руки: направления логики ---
	for (int32 S = 0; S < 2; ++S)
	{
		FightCrewImpl::Aim(Pose, Upper[S], Lower[S], ToCS(Fr.Upper[S]), Fr.ArmW[S]);
		FightCrewImpl::Aim(Pose, Lower[S], Hand[S], ToCS(Fr.Fore[S]), Fr.ArmW[S]);
	}

	// --- IK кисти (двухзвенный): бутылка к губам, ладонь к лицу ---
	for (int32 S = 0; S < 2; ++S)
	{
		if (Fr.IkW[S] <= 1e-3f || !FightCrewImpl::Ok(Upper[S]) || !FightCrewImpl::Ok(Lower[S]) || !FightCrewImpl::Ok(Hand[S]))
		{
			continue;
		}
		const FVector Sh = FightCrewImpl::CS(Pose, Upper[S]).GetLocation();
		const FVector El = FightCrewImpl::CS(Pose, Lower[S]).GetLocation();
		const FVector Ha = FightCrewImpl::CS(Pose, Hand[S]).GetLocation();
		const float A = (El - Sh).Size(), B = (Ha - El).Size();
		const FVector T = CompToWorld.InverseTransformPosition(Fr.IkTarget[S]);
		FVector ToT = T - Sh;
		const float D = FMath::Clamp(static_cast<float>(ToT.Size()), 1.f, A + B - 0.5f);
		const FVector N = ToT.GetSafeNormal();
		if (N.IsNearlyZero() || A < 1.f || B < 1.f)
		{
			continue;
		}
		FVector Pole = (El - Sh) - N * FVector::DotProduct(El - Sh, N);
		// Локоть — вниз и наружу (не внутрь корпуса).
		const FVector Out = S == 0 ? L : -L;
		const FVector Pref = (-U + Out * 0.6f).GetSafeNormal();
		Pole = (Pole.GetSafeNormal() + Pref).GetSafeNormal();
		Pole = (Pole - N * FVector::DotProduct(Pole, N)).GetSafeNormal();
		if (Pole.IsNearlyZero())
		{
			Pole = (-U - N * FVector::DotProduct(-U, N)).GetSafeNormal();
		}
		const float X = (A * A - B * B + D * D) / (2.f * D);
		const float H = FMath::Sqrt(FMath::Max(0.f, A * A - X * X));
		const FVector ElW = Sh + N * X + Pole * H;
		FightCrewImpl::Aim(Pose, Upper[S], Lower[S], ElW - Sh, Fr.IkW[S]);
		const FVector El2 = FightCrewImpl::CS(Pose, Lower[S]).GetLocation();
		FightCrewImpl::Aim(Pose, Lower[S], Hand[S], (Sh + N * D) - El2, Fr.IkW[S]);
	}

	// --- голова: на бойца (шея и голова делят поворот), взгляд держит при наклоне ---
	const float Nod = Fr.HeadPitch - Fr.Lean * Fr.BodyW * 0.45f;
	FightCrewImpl::RotateCS(Pose, Neck, FightCrewImpl::Turn(F, L, Fr.HeadYaw * 0.4f) * FightCrewImpl::Turn(U, F, Nod * 0.4f));
	FightCrewImpl::RotateCS(Pose, Head, FightCrewImpl::Turn(F, L, Fr.HeadYaw * 0.6f) * FightCrewImpl::Turn(U, F, Nod * 0.6f));
}

void FCrewVisualRootNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Retarget.Initialize_AnyThread(Context);
}

void FCrewVisualRootNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Retarget.CacheBones_AnyThread(Context);
}

void FCrewVisualRootNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Retarget.Update_AnyThread(Context);
	Dt += Context.GetDeltaTime();
}

void FCrewVisualRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	Retarget.Evaluate_AnyThread(Output);
	Fx.Apply(Output.Pose, Frame, Output.AnimInstanceProxy->GetComponentTransform());
	Fx.LimitSpeed(Output.Pose, Dt); // S-78: без рывков позы (подъём на апрон, всплеск реакции)
	Dt = 0.f;
}

void FCrewPoseFx::LimitSpeed(FCompactPose& Pose, float Dt)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("BoxCrewNoLimit")); // A/B
	if (bOff)
	{
		return;
	}
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	if (LimitBones.Num() == 0)
	{
		struct FB { const TCHAR* Name; float Rate; };
		static const FB List[] = {
			{TEXT("spine_01"), 6.f}, {TEXT("spine_02"), 6.f}, {TEXT("spine_03"), 6.f}, {TEXT("spine_04"), 6.f}, {TEXT("spine_05"), 6.f},
			{TEXT("neck_01"), 8.f}, {TEXT("head"), 10.f},
			{TEXT("clavicle_l"), 8.f}, {TEXT("clavicle_r"), 8.f}, {TEXT("upperarm_l"), 9.f}, {TEXT("upperarm_r"), 9.f},
			{TEXT("lowerarm_l"), 11.f}, {TEXT("lowerarm_r"), 11.f}, {TEXT("hand_l"), 12.f}, {TEXT("hand_r"), 12.f},
			{TEXT("thigh_l"), 9.f}, {TEXT("thigh_r"), 9.f}, {TEXT("calf_l"), 12.f}, {TEXT("calf_r"), 12.f},
			{TEXT("foot_l"), 14.f}, {TEXT("foot_r"), 14.f},
		};
		for (const FB& B : List)
		{
			const FCompactPoseBoneIndex I = FightCrewImpl::FindBone(Bones, B.Name);
			if (FightCrewImpl::Ok(I))
			{
				LimitBones.Add(I.GetInt());
				LimitRate.Add(B.Rate);
			}
		}
		LimitPrev.SetNum(LimitBones.Num());
		bLimitPrev = false;
	}
	for (int32 K = 0; K < LimitBones.Num(); ++K)
	{
		const FCompactPoseBoneIndex I(LimitBones[K]);
		if (I.GetInt() >= Pose.GetNumBones())
		{
			bLimitPrev = false;
			return;
		}
		const FQuat Want = Pose[I].GetRotation();
		const FQuat Q = bLimitPrev ? BoxCrew::LimitRotation(LimitPrev[K], Want, LimitRate[K], Dt) : Want;
		Pose[I].SetRotation(Q);
		LimitPrev[K] = Q;
	}
	bLimitPrev = true;
}

void FCrewVisualProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&Root);
	OutNodes.Add(&Root.Retarget);
}

void FCrewVisualProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (UFightCrewAnimInstance* Inst = Cast<UFightCrewAnimInstance>(InAnimInstance))
	{
		if (const AFightCrewMember* M = Inst->Member.Get())
		{
			Root.Frame = M->GetPoseFrame();
		}
	}
}

void UFightCrewAnimInstance::SetupRetarget(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter, const FRetargetProfile* Profile)
{
	RetargeterRef = Retargeter;
	FCrewVisualProxy& Proxy = GetProxyOnGameThread<FCrewVisualProxy>();
	FAnimNode_RetargetPoseFromMesh& N = Proxy.Root.Retarget;
	N.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	N.SourceMeshComponent = Source;
	N.IKRetargeterAsset = Retargeter;
	if (Profile)
	{
		N.CustomRetargetProfile = *Profile;
	}
	N.bSuppressWarnings = true;
}

// ---------------------------------------------------------------------------------------------
// Угловой
// ---------------------------------------------------------------------------------------------

AFightCrewMember::AFightCrewMember()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

float AFightCrewMember::HalfHeight() const
{
	return GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
}

FVector AFightCrewMember::GetFeet() const
{
	return GetActorLocation() - FVector(0.f, 0.f, HalfHeight());
}

void AFightCrewMember::BeginPlay()
{
	Super::BeginPlay();
	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		// Логический манекен не виден, но ведёт позу (источник ретаргета) — тикать всегда.
		Sk->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Sk->SetCastShadow(false);
	}
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->MinAnalogWalkSpeed = 0.f;
	}
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Cap->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Cap->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	ApplyVisualOverride();
	FightCrewImpl::SetGaspInput(this, true, true);
	SetupVisual();
}

void AFightCrewMember::ApplyVisualOverride()
{
	if (!VisualOverrideClass)
	{
		return;
	}
	UChildActorComponent* Child = nullptr;
	TArray<UChildActorComponent*> ChildComps;
	GetComponents(ChildComps);
	for (UChildActorComponent* C : ChildComps)
	{
		if (C && C->GetName().StartsWith(TEXT("VisualOverride")))
		{
			Child = C;
			break;
		}
	}
	if (!Child)
	{
		Child = NewObject<UChildActorComponent>(this, TEXT("CrewVisual"));
		Child->SetupAttachment(GetMesh());
		Child->RegisterComponent();
	}
	Child->SetChildActorClass(VisualOverrideClass);
	VisualChild = Child;
	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		Sk->SetVisibility(false, false);
	}
}

void AFightCrewMember::SetupVisual()
{
	if (bVisualReady)
	{
		return;
	}
	++VisualTries;
	USkeletalMeshComponent* Logic = GetMesh();
	AActor* Vis = VisualChild ? VisualChild->GetChildActor() : nullptr;
	if (!Logic || !Vis)
	{
		bVisualReady = !VisualChild || VisualTries >= 30;
		return;
	}
	// Облик под бойца (Appearance.json, Docs/LOOK.md «S-68»): запись `crew:<id>:<роль>`, иначе облик угла.
	FBoxerLook Look;
	if (UBoxerLookLibrary::FindLook(LookId, FString(), Look) || UBoxerLookLibrary::FindLook(LookFallbackId, FString(), Look))
	{
		UBoxerLookLibrary::ApplyBoxerLook(Vis, VisualChild, Look, false);
	}
	// Бюджет (PERF.md S-68): угловые мелкие в кадре — без теней и LOD1+.
	TArray<UPrimitiveComponent*> Prims;
	Vis->GetComponents(Prims);
	for (UPrimitiveComponent* P : Prims)
	{
		if (!P)
		{
			continue;
		}
		if (!bShadows)
		{
			P->SetCastShadow(false);
		}
		if (USkinnedMeshComponent* Sk = Cast<USkinnedMeshComponent>(P))
		{
			if (ForcedLod > 0)
			{
				Sk->SetForcedLOD(ForcedLod + 1);
			}
		}
	}
	TArray<ULODSyncComponent*> Syncs;
	Vis->GetComponents(Syncs);
	for (ULODSyncComponent* S : Syncs)
	{
		if (S && ForcedLod > 0)
		{
			S->ForcedLOD = ForcedLod;
		}
	}
	// Видимый меш с ретаргетом позы → наш AnimInstance (ретаргет + жесты), как у рефери.
	TArray<USkeletalMeshComponent*> Meshes;
	Vis->GetComponents(Meshes);
	USkeletalMeshComponent* Best = nullptr;
	UIKRetargeter* Rtg = nullptr;
	FRetargetProfile Profile;
	bool bHasProfile = false;
	for (USkeletalMeshComponent* M : Meshes)
	{
		UAnimInstance* AI = M ? M->GetAnimInstance() : nullptr;
		if (!AI)
		{
			continue;
		}
		for (TFieldIterator<FProperty> It(AI->GetClass()); It && !Best; ++It)
		{
			if (const FStructProperty* SP = CastField<FStructProperty>(*It))
			{
				if (SP->Struct && SP->Struct->IsChildOf(FAnimNode_RetargetPoseFromMesh::StaticStruct()))
				{
					const FAnimNode_RetargetPoseFromMesh* N = SP->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(AI);
					Best = M;
					Rtg = N->IKRetargeterAsset;
					Profile = N->CustomRetargetProfile;
					bHasProfile = true;
				}
			}
		}
		if (Best && !Rtg)
		{
			for (TFieldIterator<FObjectPropertyBase> It(AI->GetClass()); It; ++It)
			{
				if (It->PropertyClass && It->PropertyClass->IsChildOf(UIKRetargeter::StaticClass()))
				{
					Rtg = Cast<UIKRetargeter>(It->GetObjectPropertyValue_InContainer(AI));
					if (Rtg)
					{
						break;
					}
				}
			}
		}
		if (Best)
		{
			break;
		}
	}
	if (!Best)
	{
		if (VisualTries >= 30)
		{
			UE_LOG(LogTemp, Warning, TEXT("CREW: у %s нет меша с ретаргетом позы — жестов не будет"), *Vis->GetName());
			bVisualReady = true;
		}
		return;
	}
	if (!Rtg)
	{
		Rtg = LoadObject<UIKRetargeter>(nullptr, TEXT("/Game/MetaHumans/Common/Common/Rigs/RTG_UEFN_to_Metahuman_nrw.RTG_UEFN_to_Metahuman_nrw"));
	}
	if (!Rtg)
	{
		bVisualReady = true;
		return;
	}
	Best->SetAnimInstanceClass(UFightCrewAnimInstance::StaticClass());
	VisualAnim = Cast<UFightCrewAnimInstance>(Best->GetAnimInstance());
	if (VisualAnim)
	{
		VisualAnim->Member = this;
		VisualAnim->SetupRetarget(Logic, Rtg, bHasProfile ? &Profile : nullptr);
		Best->AddTickPrerequisiteComponent(Logic);
	}
	bVisualReady = true;
	UE_LOG(LogTemp, Log, TEXT("CREW: %s %s — облик %s (%s), жесты %s"), FightCrewImpl::CornerName(Corner), FightCrewImpl::RoleName(Role),
		*Vis->GetClass()->GetName(), Look.bValid ? *Look.Id : TEXT("запечённый"), VisualAnim ? TEXT("ok") : TEXT("НЕТ"));
}

void AFightCrewMember::SetHiddenAll(bool bHide)
{
	SetActorHiddenInGame(bHide);
	if (VisualChild && VisualChild->GetChildActor())
	{
		VisualChild->GetChildActor()->SetActorHiddenInGame(bHide);
	}
}

void AFightCrewMember::SnapTo(const FVector& Feet, const FVector& FaceTo)
{
	const FVector D = (FaceTo - Feet).GetSafeNormal2D();
	const FRotator Rot(0.f, D.IsNearlyZero() ? GetActorRotation().Yaw : D.Rotation().Yaw, 0.f);
	SetActorLocationAndRotation(Feet + FVector(0.f, 0.f, HalfHeight() + 2.f), Rot, false, nullptr, ETeleportType::TeleportPhysics);
	if (Controller)
	{
		Controller->SetControlRotation(Rot);
	}
}

void AFightCrewMember::Drive(const FCrewDrive& D, float DeltaSeconds)
{
	Last = D;
	bHasDrive = true;
}

void AFightCrewMember::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bVisualReady)
	{
		SetupVisual();
	}
	GaspStateTimer -= DeltaSeconds;
	if (GaspStateTimer <= 0.f)
	{
		GaspStateTimer = 0.5f;
		FightCrewImpl::SetGaspInput(this, true, true);
	}
	UCharacterMovementComponent* Cmc = GetCharacterMovement();
	if (!bHasDrive || !Cmc || DeltaSeconds <= 0.f || IsHidden())
	{
		return;
	}
	const FCrewDrive& D = Last;
	const float Half = HalfHeight();
	const FVector Want = D.Feet + FVector(0.f, 0.f, Half);
	const FVector Pos = GetActorLocation();

	// Переход пол ↔ апрон: помост по высоте не перешагнуть — на время подъёма/спуска капсула летит по пути без коллизии
	// (ноги — тот же Motion Matching по скорости), на месте — снова ходьба по полу/апрону.
	const bool bClimb = D.bClimb || FMath::Abs(Want.Z - Pos.Z) > 12.f;
	if (bClimb != bClimbing)
	{
		bClimbing = bClimb;
		GetCapsuleComponent()->SetCollisionEnabled(bClimb ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		Cmc->SetMovementMode(bClimb ? MOVE_Flying : MOVE_Walking);
	}

	// Корпус: к цели (центр ринга в бою, стул в перерыве); на ходу — по ходу.
	FVector FaceDir = (D.FaceTo - Pos).GetSafeNormal2D();
	if (FaceDir.IsNearlyZero())
	{
		FaceDir = GetActorForwardVector();
	}
	const FRotator Facing = FMath::RInterpConstantTo(FRotator(0.f, GetActorRotation().Yaw, 0.f), FRotator(0.f, FaceDir.Rotation().Yaw, 0.f), DeltaSeconds, FightCrewImpl::TURN_DPS);
	SetActorRotation(Facing);
	if (Controller)
	{
		Controller->SetControlRotation(Facing);
	}

	FVector Err = Want - Pos;
	if (!bClimb)
	{
		Err.Z = 0.f;
	}
	if (Err.Size() > 250.f)
	{
		SnapTo(D.Feet, D.FaceTo);
		Cmc->Velocity = FVector::ZeroVector;
	}
	else if (Err.Size() < FightCrewImpl::TRACK_DEADBAND && D.Vel.SizeSquared() < 1.f)
	{
		Cmc->Velocity = bClimb ? FVector::ZeroVector : FVector(0.f, 0.f, Cmc->Velocity.Z);
	}
	else
	{
		const float Gain = FMath::Min(FightCrewImpl::TRACK_GAIN, 1.f / DeltaSeconds);
		FVector V = (D.Vel + Err * Gain).GetClampedToMaxSize(FightCrewImpl::MAX_SPEED);
		if (!bClimb)
		{
			V.Z = Cmc->Velocity.Z;
		}
		Cmc->Velocity = V;
		const FVector V2(V.X, V.Y, 0.f);
		const float MaxSpeed = FMath::Max(1.f, Cmc->GetMaxSpeed());
		if (V2.Size() > 3.f)
		{
			AddMovementInput(V2.GetSafeNormal(), FMath::Clamp(V2.Size() / MaxSpeed, 0.05f, 1.f));
		}
	}

	// --- поза ---
	const BoxCrew::FMood Mood = D.Mood ? *D.Mood : BoxCrew::FMood();
	const FVector Fwd = Facing.Vector();
	const FVector Left(Fwd.Y, -Fwd.X, 0.f);
	const FVector Eye = GetFeet() + FVector(0.f, 0.f, 160.f * GetActorScale3D().Z);
	const FVector ToF = D.Fighter - Eye;
	float Dy = FMath::Atan2(static_cast<float>(FVector::DotProduct(ToF, Left)), static_cast<float>(FVector::DotProduct(ToF, Fwd)));
	Dy = FMath::Clamp(Dy, -1.1f, 1.1f);
	const float Dist = FMath::Max(1.f, static_cast<float>(ToF.Size2D()));
	const float Pitch = FMath::Clamp(FMath::Atan2(static_cast<float>(-ToF.Z), Dist), -0.5f, 0.7f);
	const float Kh = 1.f - FMath::Exp(-FightCrewImpl::HEAD_RATE * DeltaSeconds);
	HeadYaw += (Dy - HeadYaw) * Kh;
	HeadPitch += (Pitch - HeadPitch) * Kh;
	const BoxCrew::FBody B = BoxCrew::Pose(Role, Mood, D.Time, Phase, HeadYaw, HeadPitch);

	const float Speed = GetVelocity().Size2D();
	const float Walk = FMath::Clamp((Speed - 20.f) / FightCrewImpl::ARM_WALK_SPEED, 0.f, 1.f);
	PoseFrame.bValid = true;
	PoseFrame.Fwd = Fwd;
	PoseFrame.Left = Left;
	PoseFrame.Lean = B.Lean;
	PoseFrame.Twist = B.Twist;
	PoseFrame.HeadYaw = B.HeadYaw;
	PoseFrame.HeadPitch = B.HeadPitch;
	PoseFrame.BodyW = 1.f - 0.7f * Walk;
	for (int32 S = 0; S < 2; ++S)
	{
		BoxCrew::ArmDirs(B.Arm[S], S, Fwd, Left, FVector::UpVector, PoseFrame.Upper[S], PoseFrame.Fore[S]);
		PoseFrame.ArmW[S] = 1.f - Walk;
		PoseFrame.IkW[S] = 0.f;
	}
	// Катмен: бутылка (в левой кисти, часть меша) — к губам сидящего бойца; правая ладонь — протирает лицо.
	if (Role == BoxCrew::ERole::Cutman && (B.Bottle > 1e-3f || B.Wipe > 1e-3f))
	{
		const FVector Ff = D.FighterFwd.GetSafeNormal2D();
		const FVector Mouth = D.Fighter + Ff * 11.f - FVector(0.f, 0.f, 9.f);
		const FVector Shoulder = GetFeet() + FVector(0.f, 0.f, 140.f * GetActorScale3D().Z);
		// Кость кисти — у запястья: бутылка в кулаке впереди неё, кисть не доходит до губ ~13 см. S-78: кисть — ПЕРЕД лицом
		// (со стороны катмена чуть-чуть) и ниже губ: было «от губ к плечу катмена» — он сбоку, и бутылка оказывалась у уха.
		const FVector ToMe = FVector(Shoulder.X - Mouth.X, Shoulder.Y - Mouth.Y, 0.f).GetSafeNormal();
		const FVector SideC = ToMe - Ff * FVector::DotProduct(ToMe, Ff);
		PoseFrame.IkTarget[0] = Mouth + (Ff * 0.6f + SideC * 0.7f).GetSafeNormal() * 11.f - FVector(0.f, 0.f, 4.f);
		PoseFrame.IkW[0] = B.Bottle;
		const float Wipe = FMath::Sin(D.Time * 5.5f);
		const FVector Side(Ff.Y, -Ff.X, 0.f);
		const FVector Face = D.Fighter + Ff * 10.f + Side * (Wipe * 4.f) + FVector(0.f, 0.f, 2.f + FMath::Cos(D.Time * 5.5f) * 3.f);
		PoseFrame.IkTarget[1] = Face + (Shoulder - Face).GetSafeNormal() * 9.f;
		PoseFrame.IkW[1] = B.Wipe;
	}
}

// ---------------------------------------------------------------------------------------------
// Менеджер
// ---------------------------------------------------------------------------------------------

namespace FightCrewImpl
{
	TWeakObjectPtr<ABoxingCornerCrew> GCrew;
}

ABoxingCornerCrew::ABoxingCornerCrew()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	for (int32 C = 0; C < 2; ++C)
	{
		for (int32 R = 0; R < 2; ++R)
		{
			Spot[C][R][0] = Spot[C][R][1] = FVector::ZeroVector;
		}
		StoolAt[C][0] = StoolAt[C][1] = FVector::ZeroVector;
	}
}

ABoxingCornerCrew* ABoxingCornerCrew::Find(const UWorld* World)
{
	ABoxingCornerCrew* C = FightCrewImpl::GCrew.Get();
	return C && C->GetWorld() == World ? C : nullptr;
}

AFightCrewMember* ABoxingCornerCrew::GetMember(int32 InCorner, BoxCrew::ERole InRole) const
{
	const int32 I = FMath::Clamp(InCorner, 0, 1) * 2 + (InRole == BoxCrew::ERole::Coach ? 0 : 1);
	return Members[I];
}

void ABoxingCornerCrew::NotifyEvent(const UObject* WorldContext, const FFightEvent& E)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (ABoxingCornerCrew* C = Find(W))
	{
		C->OnFightEvent(E);
	}
}

void ABoxingCornerCrew::OnFightEvent(const FFightEvent& E)
{
	BoxCrew::FKick K[2];
	const int32 N = BoxCrew::Reaction(E, K);
	for (int32 I = 0; I < N; ++I)
	{
		Mood[FMath::Clamp(K[I].Corner, 0, 1)].Kick(K[I]);
	}
}

ABoxingCornerCrew* ABoxingCornerCrew::SpawnFor(ABoxingFightGameMode* GM)
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (!GM || !GM->GetWorld() || FParse::Param(Cmd, TEXT("BoxNoCrew")))
	{
		return nullptr;
	}
	const ABoxingCornerCrew* Def = GetDefault<ABoxingCornerCrew>();
	if (!Def->CrewClassPath.TryLoadClass<AFightCrewMember>())
	{
		UE_LOG(LogTemp, Warning, TEXT("CREW: нет класса %s (запусти Tools/EditorScripts/feel_crew_bp.py) — угловых не будет"), *Def->CrewClassPath.ToString());
		return nullptr;
	}
	ABoxingCornerCrew* C = GM->GetWorld()->SpawnActor<ABoxingCornerCrew>(ABoxingCornerCrew::StaticClass(), FTransform::Identity);
	if (C)
	{
		C->Setup(GM);
	}
	return C;
}

FVector ABoxingCornerCrew::MarkerOr(const FString& Name, const FVector& Fallback) const
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->Tags.Num() > 0 && It->Tags.Last().ToString() == Name)
		{
			return It->GetActorLocation();
		}
		if (It->GetActorNameOrLabel() == Name)
		{
			return It->GetActorLocation();
		}
	}
	return Fallback;
}

void ABoxingCornerCrew::ReadMarkers()
{
	const ABoxingFightGameMode* GM = Mode.Get();
	const FVector Floor = GM ? GM->GetRingFloorCenter() : FVector::ZeroVector;
	auto ToWorld = [&Floor](const FVector& M) { return FVector(Floor.X + M.X * 100.f, Floor.Y + M.Y * 100.f, Floor.Z + M.Z * 100.f); };
	int32 Found = 0;
	for (int32 C = 0; C < 2; ++C)
	{
		for (int32 R = 0; R < 2; ++R)
		{
			const BoxCrew::ERole RoleR = R == 0 ? BoxCrew::ERole::Coach : BoxCrew::ERole::Cutman;
			for (int32 M = 0; M < 2; ++M)
			{
				const FVector Fb = ToWorld(BoxCrew::SpotCore(C, RoleR, M == 1));
				const FString Name = FString::Printf(TEXT("CornerCrew_%s_%s_%s"), FightCrewImpl::CornerName(C), FightCrewImpl::RoleName(RoleR), M == 1 ? TEXT("Rest") : TEXT("Fight"));
				Spot[C][R][M] = MarkerOr(Name, Fb);
				Found += Spot[C][R][M] != Fb ? 1 : 0;
			}
		}
		// Катмен работает с лицом сидящего (бутылка к губам, протирает): точка маркера — 55 см от стула вдоль каната,
		// рукой через канат не достать — в перерыве он ближе к столбу на CUTMAN_REST_IN.
		Spot[C][1][1].X += (C == 0 ? -1.f : 1.f) * BoxCrew::CUTMAN_REST_IN;
		StoolAt[C][0] = MarkerOr(FString::Printf(TEXT("CornerStool_%s"), FightCrewImpl::CornerName(C)), ToWorld(BoxCrew::StoolCore(C, false)));
		StoolAt[C][1] = MarkerOr(FString::Printf(TEXT("CornerStool_%s_Stow"), FightCrewImpl::CornerName(C)), ToWorld(BoxCrew::StoolCore(C, true)));
	}
	UE_LOG(LogTemp, Log, TEXT("CREW: места — маркеров L_Ring %d из 8 (прочие — формулы веба)"), Found);
}

void ABoxingCornerCrew::Setup(ABoxingFightGameMode* GM)
{
	FightCrewImpl::GCrew = this;
	Mode = GM;
	const TCHAR* Cmd = FCommandLine::Get();
	bLog = FParse::Param(Cmd, TEXT("BoxCrewLog"));
	FParse::Value(Cmd, TEXT("BoxCrewShots="), ShotPrefix);
	int32 Shadow = 0, Lod = 1;
	FParse::Value(Cmd, TEXT("BoxCrewShadow="), Shadow);
	FParse::Value(Cmd, TEXT("BoxCrewLOD="), Lod);
	ReadMarkers();
	UClass* Cls = CrewClassPath.TryLoadClass<AFightCrewMember>();
	const FVector Floor = GM->GetRingFloorCenter();
	int32 Spawned = 0;
	for (int32 C = 0; C < 2; ++C)
	{
		const ABoxerCharacter* B = GM->GetBoxer(C);
		const FString FighterId = B ? B->Preset.Id : FString();
		for (int32 R = 0; R < 2; ++R)
		{
			const BoxCrew::ERole RoleR = R == 0 ? BoxCrew::ERole::Coach : BoxCrew::ERole::Cutman;
			const FString LookPath = (R == 0 ? CoachLookPath : CutmanLookPath).Replace(TEXT("{c}"), FightCrewImpl::CornerName(C));
			UClass* VisCls = FSoftClassPath(LookPath).TryLoadClass<AActor>();
			if (!VisCls)
			{
				UE_LOG(LogTemp, Warning, TEXT("CREW: нет облика %s (Content/BoxingLocal вне git?) — угловой не ставится"), *LookPath);
				continue;
			}
			const FVector Feet = Spot[C][R][0];
			const FTransform Xf(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, 100.f));
			AFightCrewMember* M = GetWorld()->SpawnActorDeferred<AFightCrewMember>(Cls, Xf, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!M)
			{
				continue;
			}
			M->Corner = C;
			M->Role = RoleR;
			M->Phase = 1.3f * C + 2.1f * R;
			M->VisualOverrideClass = VisCls;
			const TCHAR* RoleKey = R == 0 ? TEXT("coach") : TEXT("cutman");
			M->LookId = FighterId.IsEmpty() ? FString() : FString::Printf(TEXT("crew:%s:%s"), *FighterId, RoleKey);
			M->LookFallbackId = FString::Printf(TEXT("crew:%s:%s"), FightCrewImpl::CornerName(C), RoleKey);
			M->bShadows = Shadow != 0;
			M->ForcedLod = Lod;
			M->FinishSpawning(Xf);
			if (!M->GetController())
			{
				M->SpawnDefaultController();
			}
			M->AddTickPrerequisiteActor(this);
			if (UCharacterMovementComponent* Cmc = M->GetCharacterMovement())
			{
				Cmc->bTickBeforeOwner = false;
				M->PrimaryActorTick.RemovePrerequisite(Cmc, Cmc->PrimaryComponentTick);
				Cmc->PrimaryComponentTick.AddPrerequisite(M, M->PrimaryActorTick);
			}
			M->SnapTo(Feet, FVector(Floor.X, Floor.Y, Feet.Z));
			Members[C * 2 + R] = M;
			++Spawned;
		}
		// Стул: StaticMeshActor-BP, убран за столбом до перерыва.
		const FString SPath = StoolPath.Replace(TEXT("{c}"), FightCrewImpl::CornerName(C));
		if (UClass* SCls = FSoftClassPath(SPath).TryLoadClass<AActor>())
		{
			FActorSpawnParameters P;
			P.Owner = this;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* S = GetWorld()->SpawnActor<AActor>(SCls, FTransform(FRotator::ZeroRotator, StoolAt[C][1]), P);
			if (S)
			{
				if (USceneComponent* Root = S->GetRootComponent())
				{
					Root->SetMobility(EComponentMobility::Movable);
				}
				TArray<UPrimitiveComponent*> Prims;
				S->GetComponents(Prims);
				for (UPrimitiveComponent* Pc : Prims)
				{
					Pc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				S->SetActorHiddenInGame(true);
				Stools[C] = S;
			}
		}
		if (B)
		{
			SeatCm[C] = BoxCrew::SEAT_OF_HEIGHT * (B->Preset.HeightCm > 0.f ? B->Preset.HeightCm : 178.f);
		}
	}
	AddTickPrerequisiteActor(GM);
	for (int32 C = 0; C < 2; ++C)
	{
		if (ABoxerCharacter* B = GM->GetBoxer(C))
		{
			AddTickPrerequisiteActor(B);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("CREW: угловых %d, стульев %d, тени %s, LOD %d"), Spawned, (Stools[0] ? 1 : 0) + (Stools[1] ? 1 : 0),
		Shadow ? TEXT("да") : TEXT("нет"), Lod);
}

void ABoxingCornerCrew::EndPlay(const EEndPlayReason::Type Reason)
{
	if (FightCrewImpl::GCrew.Get() == this)
	{
		FightCrewImpl::GCrew.Reset();
	}
	Super::EndPlay(Reason);
}

void ABoxingCornerCrew::UpdateStool(int32 C, float DeltaSeconds)
{
	AActor* S = Stools[C];
	const ABoxingFightGameMode* GM = Mode.Get();
	if (!S || !GM)
	{
		return;
	}
	const float K = Mood[C].Stool;
	const bool bShow = K > 0.01f && !bHidden;
	if (S->IsHidden() == bShow)
	{
		S->SetActorHiddenInGame(!bShow);
	}
	if (!bShow)
	{
		return;
	}
	// Под таз севшего бойца (место — где он реально стоит, а не идеальная точка угла).
	FVector In = StoolAt[C][0];
	if (const ABoxerCharacter* B = GM->GetBoxer(C))
	{
		// S-78: до посадки стул не ближе STOOL_CLEAR_CM к бойцу (за ним, к столбу) — не в ногах идущего в угол; садится —
		// въезжает под таз (было: стул ехал в точку угла, где боец ещё стоял, ноги — сквозь табурет).
		const FVector Bp = B->GetActorLocation();
		const FVector Seat = Bp - B->GetActorForwardVector().GetSafeNormal2D() * 6.f;
		In = BoxCrew::StoolSpot(In, Bp, StoolAt[C][1], Seat, B->GetSitWeight(), BoxCrew::STOOL_CLEAR_CM);
		// Высота сиденья — по тазу севшего (кость pelvis видимого меша минус полтолщины ягодиц).
		if (B->GetSitWeight() > 0.9f)
		{
			if (const USkeletalMeshComponent* M = B->GetFeelMesh())
			{
				const float Want = FMath::Clamp(static_cast<float>(M->GetBoneLocation(TEXT("pelvis")).Z - In.Z) - 11.f, 32.f, 70.f);
				SeatCm[C] += (Want - SeatCm[C]) * (1.f - FMath::Exp(-6.f * DeltaSeconds));
			}
		}
	}
	const float E = K * K * (3.f - 2.f * K);
	const FVector At = FMath::Lerp(StoolAt[C][1], In, E);
	S->SetActorLocation(At);
	S->SetActorScale3D(FVector(1.f, 1.f, (0.6f + 0.4f * E) * SeatCm[C] / BoxCrew::STOOL_MESH_CM));
}

void ABoxingCornerCrew::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM || !GM->GetBoxer(0) || !GM->GetBoxer(1))
	{
		return;
	}
	// Повтор нокаута ведёт бойцов записью — угловых в записи нет: на время повтора скрыты (как рефери).
	const UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	const bool bReplay = Fx && Fx->IsReplaying();
	if (bReplay != bHidden)
	{
		bHidden = bReplay;
		for (AFightCrewMember* M : Members)
		{
			if (M)
			{
				M->SetHiddenAll(bReplay);
			}
		}
	}
	if (bReplay)
	{
		for (int32 C = 0; C < 2; ++C)
		{
			UpdateStool(C, DeltaSeconds);
		}
		return;
	}
	Clock += DeltaSeconds;
	const FFightSnapshot& S = GM->GetSnapshot();
	const bool bRest = S.Phase == EFightPhase::Between && S.Stage.Kind == ERingStageKind::Rest;
	const FVector Floor = GM->GetRingFloorCenter();
	for (int32 C = 0; C < 2; ++C)
	{
		const ABoxerCharacter* B = GM->GetBoxer(C);
		const bool bSeated = B && B->GetSitWeight() > 0.5f;
		const float PrevUp = Mood[C].Up;
		Mood[C].Update(DeltaSeconds, bRest, bSeated);
		const float DuDt = (Mood[C].Up - PrevUp) / FMath::Max(1e-4f, DeltaSeconds);
		const bool bClimb = Mood[C].Up > 0.f && Mood[C].Up < 1.f;
		FVector Head = B ? B->GetActorLocation() + FVector(0.f, 0.f, 70.f) : Floor;
		if (const USkeletalMeshComponent* Vm = B ? B->GetFeelMesh() : nullptr)
		{
			Head = Vm->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 6.f);
		}
		for (int32 R = 0; R < 2; ++R)
		{
			AFightCrewMember* M = Members[C * 2 + R];
			if (!M)
			{
				continue;
			}
			FCrewDrive D;
			D.Feet = BoxCrew::PlaceBetween(Spot[C][R][0], Spot[C][R][1], Mood[C].Up);
			D.Vel = BoxCrew::PlaceVelocity(Spot[C][R][0], Spot[C][R][1], Mood[C].Up, DuDt);
			// Корпус — к центру ринга (бой) / к стулу (перерыв).
			D.FaceTo = Mood[C].Up > 0.5f ? StoolAt[C][0] : FVector(Floor.X, Floor.Y, D.Feet.Z);
			D.Fighter = Head;
			D.FighterFwd = B ? B->GetActorForwardVector() : FVector::ForwardVector;
			D.bClimb = bClimb;
			D.Mood = &Mood[C];
			D.Time = Clock;
			M->Drive(D, DeltaSeconds);
		}
		UpdateStool(C, DeltaSeconds);
	}
	if (bLog)
	{
		LogTimer -= DeltaSeconds;
		if (LogTimer <= 0.f)
		{
			LogTimer = 1.f;
			const ABoxerCharacter* P = GM->GetBoxer(0);
			const AFightCrewMember* Co = Members[0];
			const AFightCrewMember* Cu = Members[1];
			UE_LOG(LogTemp, Log, TEXT("CREW t=%.1f стадия %d фаза %d | красный: up %.2f lean %.2f стул %.2f сел %.2f%s (сиденье %.0f) тренер (%.0f,%.0f,%.0f) катмен (%.0f,%.0f,%.0f) | синий: up %.2f стул %.2f"),
				Clock, static_cast<int32>(S.Stage.Kind), static_cast<int32>(S.Phase), Mood[0].Up, Mood[0].Lean, Mood[0].Stool,
				P ? P->GetSitWeight() : 0.f, P && P->IsSeatedInCorner() ? TEXT(" СЕЛ") : TEXT(""), SeatCm[0],
				Co ? Co->GetFeet().X : 0.f, Co ? Co->GetFeet().Y : 0.f, Co ? Co->GetFeet().Z : 0.f,
				Cu ? Cu->GetFeet().X : 0.f, Cu ? Cu->GetFeet().Y : 0.f, Cu ? Cu->GetFeet().Z : 0.f, Mood[1].Up, Mood[1].Stool);
		}
	}
	UpdateShots(DeltaSeconds);
}

void ABoxingCornerCrew::Shot(const FString& Name)
{
	if (ShotsTaken.Contains(Name))
	{
		return;
	}
	ShotsTaken.Add(Name);
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") / (ShotPrefix + TEXT("_") + Name + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogTemp, Log, TEXT("CREW: скриншот %s"), *Path);
}

void ABoxingCornerCrew::UpdateShots(float DeltaSeconds)
{
	if (ShotPrefix.IsEmpty())
	{
		return;
	}
	ABoxingFightGameMode* GM = Mode.Get();
	UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	const FFightSnapshot& S = GM->GetSnapshot();
	const ABoxerCharacter* P = GM->GetBoxer(0);
	const FVector Floor = GM->GetRingFloorCenter();
	// Бой: игровая камера + обзор угла красных (камера над рингом — угловые у помоста, их головы над краем помоста).
	if (S.Phase == EFightPhase::Fighting && S.Stage.Kind == ERingStageKind::None && S.Round == 1)
	{
		FightFor += DeltaSeconds;
		if (FightFor > 7.f)
		{
			Shot(TEXT("fight"));
		}
		if (Fx && FightFor > 8.f && FightFor < 8.6f)
		{
			Fx->bShotCam = true;
			Fx->ShotCam = Floor + FVector(-40.f, 60.f, 240.f);
			Fx->ShotLook = Floor + FVector(-330.f, -330.f, 20.f);
			if (FightFor > 8.4f)
			{
				Shot(TEXT("fight_corner"));
			}
		}
		else if (Fx && Fx->bShotCam && FightFor >= 8.6f && FightFor < 9.f)
		{
			Fx->bShotCam = false;
		}
	}
	const bool bRest = S.Phase == EFightPhase::Between && S.Stage.Kind == ERingStageKind::Rest;
	if (bRest && !bWasRest)
	{
		++RestsSeen;
		RestFor = 0.f;
		SeatedFor = 0.f;
	}
	if (!bRest && bWasRest)
	{
		OutFor = 0.f;
	}
	bWasRest = bRest;
	if (RestsSeen != 1)
	{
		return;
	}
	if (bRest)
	{
		RestFor += DeltaSeconds;
		if (RestFor > 1.0f)
		{
			Shot(TEXT("rest_walk"));
		}
		if (P && P->IsSeatedInCorner())
		{
			SeatedFor += DeltaSeconds;
			if (SeatedFor > 1.6f) Shot(TEXT("rest_bottle"));
			if (SeatedFor > 5.0f) Shot(TEXT("rest_wipe"));
			// Сбоку от угла — посадка, наклон тренера, бутылка у губ (контроль позы).
			if (Fx && SeatedFor > 2.2f && SeatedFor < 2.8f)
			{
				const FVector St = StoolAt[0][0];
				Fx->bShotCam = true;
				Fx->ShotCam = FVector(St.X + 240.f, St.Y - 20.f, Floor.Z + 150.f);
				Fx->ShotLook = FVector(St.X - 10.f, St.Y - 10.f, Floor.Z + 85.f);
				if (SeatedFor > 2.6f) Shot(TEXT("rest_side"));
			}
			else if (Fx && Fx->bShotCam && SeatedFor >= 2.8f)
			{
				Fx->bShotCam = false;
			}
		}
	}
	else if (OutFor >= 0.f)
	{
		OutFor += DeltaSeconds;
		if (OutFor > 0.8f) Shot(TEXT("out"));
	}
}
