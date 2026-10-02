// Рефери в бою (S-58): тело для BoxRef::FBrain — ход CharacterMovement'ом (ноги — Motion Matching GASP),
// облик BP_RefereeLook_*, жесты процедурно на костях видимого меша (порт web Referee.tsx). См. FightReferee.h.
#include "FightReferee.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "BoxingFightPlayerController.h"
#include "FightFx.h"
#include "Animation/AnimInstance.h"
#include "BonePose.h"
#include "BoneContainer.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Retargeter/IKRetargeter.h"
#include "UnrealClient.h"

namespace
{
	// Слежение капсулы за местом логики (1/с): скорость = ход логики + ошибка × K.
	constexpr float TRACK_GAIN = 10.f;
	constexpr float TRACK_DEADBAND = 0.5f; // см
	// Голова и пальцы (Referee.tsx): скорость сглаживания, предел поворота головы.
	constexpr float HEAD_RATE = 5.f;
	constexpr float HEAD_YAW_MAX = 0.85f;
	constexpr float FINGER_RATE = 14.f;
	// Боевая стойка: наклон корпуса вперёд и сгиб колен (рад) при Ready = 1.
	constexpr float READY_LEAN = 0.13f;
	constexpr float READY_KNEE = 0.16f;
	// Пальцы (Referee.tsx curlFor): [большой, указательный, средний, безымянный, мизинец], доли по фалангам.
	constexpr float CURL_RELAX[5] = {0.08f, 0.3f, 0.42f, 0.5f, 0.58f};
	constexpr float CURL_OPEN[5] = {0.02f, 0.04f, 0.05f, 0.06f, 0.08f};
	constexpr float CURL_FIST[5] = {0.45f, 1.35f, 1.45f, 1.5f, 1.5f};
	constexpr float JOINT_K[3] = {0.8f, 1.15f, 0.75f};
	// Метрика «рефери закрывает бойца»: вертикальная ось рефери, радиус тела (см), высоты точек бойца.
	constexpr float OCC_R = 24.f;
	constexpr float OCC_TOP = 180.f;
	constexpr float OCC_FRAC = 0.4f;

	void CurlFor(BoxRef::EHand H, float Out[5])
	{
		using BoxRef::EHand;
		if (H == EHand::Relax)
		{
			FMemory::Memcpy(Out, CURL_RELAX, sizeof(CURL_RELAX));
			return;
		}
		if (H == EHand::Open)
		{
			FMemory::Memcpy(Out, CURL_OPEN, sizeof(CURL_OPEN));
			return;
		}
		// счёт пальцами: 1 — указательный, 2 — +средний, 3 — +безымянный, 4 — +мизинец, 5 — +большой
		const int32 N = H == EHand::Point ? 1 : static_cast<int32>(H);
		for (int32 I = 0; I < 5; ++I)
		{
			Out[I] = I == 0 ? (N >= 5 ? CURL_OPEN[0] : CURL_FIST[0]) : (I <= N ? CURL_OPEN[I] : CURL_FIST[I]);
		}
	}

	FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, const FString& Name)
	{
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(*Name));
		if (Mesh == INDEX_NONE)
		{
			return FCompactPoseBoneIndex(INDEX_NONE);
		}
		return Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
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

	// Поворот From → в сторону To (ось From × To) на угол Angle.
	FQuat Turn(const FVector& From, const FVector& To, float Angle)
	{
		if (FMath::Abs(Angle) < 1e-5f)
		{
			return FQuat::Identity;
		}
		const FVector Axis = FVector::CrossProduct(From, To).GetSafeNormal();
		return Axis.IsNearlyZero() ? FQuat::Identity : FQuat(Axis, Angle);
	}

	// Повернуть кость так, чтобы «сустав → сустав-ребёнок» смотрел вдоль Dir (компонент), с долей W.
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

	FVector WorldDir(const FRefereePoseFrame& F, const BoxRef::FV3& V)
	{
		return (F.Left * V.X + FVector::UpVector * V.Y + F.Fwd * V.Z).GetSafeNormal();
	}

	float SmoothTo(float Cur, float Want, float Rate, float Dt)
	{
		return Cur + (Want - Cur) * (1.f - FMath::Exp(-Rate * Dt));
	}

	// Свойство GASP (BPI_SandboxCharacter_Pawn) — та же запись, что у бойцов (ABoxerCharacter::PushGaspInputState).
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
}

// ---------------------------------------------------------------------------------------------
// Поза: жесты поверх ретаргета
// ---------------------------------------------------------------------------------------------

void FRefereePoseFx::Resolve(const FBoneContainer& Bones)
{
	if (Serial == Bones.GetSerialNumber() && ContainerPtr == &Bones)
	{
		return;
	}
	Serial = Bones.GetSerialNumber();
	ContainerPtr = &Bones;
	Pelvis = FindBone(Bones, TEXT("pelvis"));
	Spine[0] = FindBone(Bones, TEXT("spine_02"));
	Spine[1] = FindBone(Bones, TEXT("spine_03"));
	Spine[2] = FindBone(Bones, TEXT("spine_04"));
	SpineTop = FindBone(Bones, TEXT("spine_05"));
	Neck = FindBone(Bones, TEXT("neck_01"));
	Head = FindBone(Bones, TEXT("head"));
	static const TCHAR* Sfx[2] = {TEXT("_l"), TEXT("_r")};
	static const TCHAR* Fingers[5] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
	for (int32 S = 0; S < 2; ++S)
	{
		Clav[S] = FindBone(Bones, FString(TEXT("clavicle")) + Sfx[S]);
		Upper[S] = FindBone(Bones, FString(TEXT("upperarm")) + Sfx[S]);
		Lower[S] = FindBone(Bones, FString(TEXT("lowerarm")) + Sfx[S]);
		Hand[S] = FindBone(Bones, FString(TEXT("hand")) + Sfx[S]);
		Thigh[S] = FindBone(Bones, FString(TEXT("thigh")) + Sfx[S]);
		Calf[S] = FindBone(Bones, FString(TEXT("calf")) + Sfx[S]);
		Foot[S] = FindBone(Bones, FString(TEXT("foot")) + Sfx[S]);
		for (int32 F = 0; F < 5; ++F)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				Finger[S][F][J] = FindBone(Bones, FString::Printf(TEXT("%s_%02d%s"), Fingers[F], J + 1, Sfx[S])).GetInt();
			}
		}
	}
}

void FRefereePoseFx::Apply(FCompactPose& Pose, const FRefereePoseFrame& Fr, const FTransform& CompToWorld)
{
	if (!Fr.bValid)
	{
		return;
	}
	Resolve(Pose.GetBoneContainer());
	if (!Ok(Pelvis))
	{
		return;
	}
	const FVector F = CompToWorld.InverseTransformVectorNoScale(Fr.Fwd).GetSafeNormal();
	const FVector L = CompToWorld.InverseTransformVectorNoScale(Fr.Left).GetSafeNormal();
	const FVector U = CompToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	auto ToCS = [&CompToWorld](const FVector& W) { return CompToWorld.InverseTransformVectorNoScale(W).GetSafeNormal(); };

	// --- боевая стойка: колени мягче (бедро вперёд, голень назад, стопа — в пол; таз опускается) ---
	const float Knee = READY_KNEE * Fr.Ready;
	if (Knee > 1e-3f && Ok(Thigh[0]) && Ok(Thigh[1]) && Ok(Foot[0]) && Ok(Foot[1]))
	{
		const float Y0 = 0.5f * (FVector::DotProduct(CS(Pose, Foot[0]).GetLocation(), U) + FVector::DotProduct(CS(Pose, Foot[1]).GetLocation(), U));
		const FQuat Fw = Turn(-U, F, Knee);
		const FQuat Bk = Turn(-U, F, -2.f * Knee);
		for (int32 S = 0; S < 2; ++S)
		{
			RotateCS(Pose, Thigh[S], Fw);
			RotateCS(Pose, Calf[S], Bk);
			RotateCS(Pose, Foot[S], Fw);
		}
		const float Y1 = 0.5f * (FVector::DotProduct(CS(Pose, Foot[0]).GetLocation(), U) + FVector::DotProduct(CS(Pose, Foot[1]).GetLocation(), U));
		const FVector Off = -U * FMath::Max(0.f, Y1 - Y0);
		const FQuat Qp = ParentRotCS(Pose, Pelvis);
		Pose[Pelvis].AddToTranslation(Qp.UnrotateVector(Off));
	}

	// --- корпус: наклон (жест + стойка) ---
	const float Lean = Fr.Lean + READY_LEAN * Fr.Ready;
	static const float Share[3] = {0.4f, 0.35f, 0.25f};
	for (int32 I = 0; I < 3; ++I)
	{
		RotateCS(Pose, Spine[I], Turn(U, F, Lean * Share[I]));
	}

	// --- ключицы: поднятая рука тянет плечо вверх ---
	for (int32 S = 0; S < 2; ++S)
	{
		const FVector Out = S == 0 ? L : -L;
		const float Up = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Fr.Upper[S], FVector::UpVector)));
		RotateCS(Pose, Clav[S], Turn(Out, U, Up * 0.32f * Fr.ArmW[S]));
	}

	// --- руки: направления логики (доля жеста ArmW: 0 — руки локомоции GASP) ---
	for (int32 S = 0; S < 2; ++S)
	{
		Aim(Pose, Upper[S], Lower[S], ToCS(Fr.Upper[S]), Fr.ArmW[S]);
		Aim(Pose, Lower[S], Hand[S], ToCS(Fr.Fore[S]), Fr.ArmW[S]);
	}

	// --- кисти: сгиб фаланг к ладони ---
	for (int32 S = 0; S < 2; ++S)
	{
		const FCompactPoseBoneIndex Idx(Finger[S][1][0]);
		const FCompactPoseBoneIndex Mid(Finger[S][2][0]);
		const FCompactPoseBoneIndex Pnk(Finger[S][4][0]);
		if (!Ok(Hand[S]) || !Ok(Idx) || !Ok(Mid) || !Ok(Pnk))
		{
			continue;
		}
		// Ладонь: (кисть → средний) × (мизинец → указательный); у левой и правой — зеркально.
		const FVector H = CS(Pose, Hand[S]).GetLocation();
		FVector Palm = FVector::CrossProduct(CS(Pose, Mid).GetLocation() - H, CS(Pose, Idx).GetLocation() - CS(Pose, Pnk).GetLocation()).GetSafeNormal();
		if (S == 0)
		{
			Palm = -Palm;
		}
		for (int32 K = 0; K < 5; ++K)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				const FCompactPoseBoneIndex B(Finger[S][K][J]);
				if (!Ok(B))
				{
					continue;
				}
				const FCompactPoseBoneIndex Next(J < 2 ? Finger[S][K][J + 1] : INDEX_NONE);
				const FCompactPoseBoneIndex Prev(J > 0 ? Finger[S][K][J - 1] : INDEX_NONE);
				FVector D = Ok(Next) ? CS(Pose, Next).GetLocation() - CS(Pose, B).GetLocation()
					: (Ok(Prev) ? CS(Pose, B).GetLocation() - CS(Pose, Prev).GetLocation() : FVector::ZeroVector);
				D = D.GetSafeNormal();
				const FVector Axis = FVector::CrossProduct(D, Palm).GetSafeNormal();
				if (Axis.IsNearlyZero())
				{
					continue;
				}
				const float A = Fr.Curl[S][K] * JOINT_K[J];
				RotateCS(Pose, B, FQuat(Axis, A));
			}
		}
	}

	// --- голова: следит за действием (шея и голова делят поворот), взгляд держит при наклоне ---
	const float Nod = -Fr.HeadPitch - Lean * 0.85f;
	RotateCS(Pose, SpineTop, Turn(F, L, Fr.HeadYaw * 0.15f));
	RotateCS(Pose, Neck, Turn(F, L, Fr.HeadYaw * 0.35f) * Turn(U, F, Nod * 0.4f));
	RotateCS(Pose, Head, Turn(F, L, Fr.HeadYaw * 0.5f) * Turn(U, F, Nod * 0.6f));
}

void FRefereeVisualRootNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Retarget.Initialize_AnyThread(Context);
}

void FRefereeVisualRootNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Retarget.CacheBones_AnyThread(Context);
}

void FRefereeVisualRootNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Retarget.Update_AnyThread(Context);
}

void FRefereeVisualRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	Retarget.Evaluate_AnyThread(Output);
	Fx.Apply(Output.Pose, Frame, Output.AnimInstanceProxy->GetComponentTransform());
}

void FRefereeVisualProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&Root);
	OutNodes.Add(&Root.Retarget);
}

void FRefereeVisualProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (UFightRefereeAnimInstance* Inst = Cast<UFightRefereeAnimInstance>(InAnimInstance))
	{
		if (const ABoxingReferee* R = Inst->Referee.Get())
		{
			Root.Frame = R->GetPoseFrame();
		}
	}
}

void UFightRefereeAnimInstance::SetupRetarget(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter, const FRetargetProfile* Profile)
{
	RetargeterRef = Retargeter;
	FRefereeVisualProxy& Proxy = GetProxyOnGameThread<FRefereeVisualProxy>();
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
// Актор
// ---------------------------------------------------------------------------------------------

ABoxingReferee* ABoxingReferee::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ABoxingReferee> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ABoxingReferee::ABoxingReferee()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

ABoxingReferee* ABoxingReferee::SpawnFor(ABoxingFightGameMode* GM)
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (!GM || !GM->GetWorld() || FParse::Param(Cmd, TEXT("BoxNoRef")))
	{
		return nullptr;
	}
	const ABoxingReferee* Def = GetDefault<ABoxingReferee>();
	UClass* Cls = Def->RefereeClassPath.TryLoadClass<ABoxingReferee>();
	if (!Cls)
	{
		UE_LOG(LogTemp, Warning, TEXT("REF: класс %s не найден (запусти Tools/EditorScripts/feel_referee_bp.py) — нативный ABoxingReferee без AnimBP GASP"),
			*Def->RefereeClassPath.ToString());
		Cls = ABoxingReferee::StaticClass();
	}
	// Облик: любители (как pro = rounds > 3 веба) / профи; -BoxRefLook= — вручную.
	FString LookArg;
	FSoftClassPath Look = GM->Rounds <= 3 ? Def->LookAmateurPath : Def->LookProPath;
	if (FParse::Value(Cmd, TEXT("BoxRefLook="), LookArg))
	{
		if (LookArg.Equals(TEXT("pro"), ESearchCase::IgnoreCase)) Look = Def->LookProPath;
		else if (LookArg.Equals(TEXT("amateur"), ESearchCase::IgnoreCase)) Look = Def->LookAmateurPath;
		else if (LookArg.Equals(TEXT("none"), ESearchCase::IgnoreCase)) Look = FSoftClassPath();
		else Look = FSoftClassPath(LookArg);
	}
	const ABoxingReferee* Cdo = Cls->GetDefaultObject<ABoxingReferee>();
	const float HalfHeight = (Cdo && Cdo->GetCapsuleComponent()) ? Cdo->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	FVector Loc = GM->FightToWorld(0.f, 0.f);
	Loc.Z = GM->GetRingFloorCenter().Z + HalfHeight + 2.f;
	const FTransform Xf(FRotator::ZeroRotator, Loc);
	ABoxingReferee* R = GM->GetWorld()->SpawnActorDeferred<ABoxingReferee>(Cls, Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!R)
	{
		UE_LOG(LogTemp, Error, TEXT("REF: не удалось заспаунить рефери"));
		return nullptr;
	}
	R->Mode = GM;
	if (UClass* VisCls = Look.IsValid() ? Look.TryLoadClass<AActor>() : nullptr)
	{
		R->VisualOverrideClass = VisCls;
	}
	else if (Look.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("REF: облик %s не загрузился (Content/BoxingLocal вне git?) — виден манекен"), *Look.ToString());
	}
	R->FinishSpawning(Xf);
	if (!R->GetController())
	{
		R->SpawnDefaultController(); // CMC симулирует ход только у управляемой пешки
	}
	R->AddTickPrerequisiteActor(GM); // сначала ядро (снимок), потом рефери
	if (UCharacterMovementComponent* Cmc = R->GetCharacterMovement())
	{
		Cmc->PrimaryComponentTick.AddPrerequisite(R, R->PrimaryActorTick); // CMC — после того, как задан ход
	}
	for (int32 I = 0; I < 2; ++I)
	{
		if (ABoxerCharacter* B = GM->GetBoxer(I))
		{
			R->AddTickPrerequisiteActor(B); // места бойцов — этого кадра
		}
	}
	UE_LOG(LogTemp, Log, TEXT("REF: рефери %s, облик %s (%s бой)"), *Cls->GetName(), *GetNameSafe(R->VisualOverrideClass.Get()),
		GM->Rounds <= 3 ? TEXT("любительский") : TEXT("профи"));
	return R;
}

void ABoxingReferee::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Cmd = FCommandLine::Get();
	bLog = FParse::Param(Cmd, TEXT("BoxRefLog"));
	bShots = FParse::Param(Cmd, TEXT("BoxRefShots"));
	bDraw = FParse::Param(Cmd, TEXT("BoxRefDraw"));
	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		Sk->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->MinAnalogWalkSpeed = 0.f;
	}
	// Рефери не толкает бойцов и не упирается в них (их ведёт ядро): капсула не блокирует пешек.
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Cap->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	ApplyVisualOverride();
	PushGaspInputState();
	SetupVisual();
}

void ABoxingReferee::EndPlay(const EEndPlayReason::Type Reason)
{
	LogSummary(TEXT("конец"));
	Super::EndPlay(Reason);
}

void ABoxingReferee::ApplyVisualOverride()
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
		Child = NewObject<UChildActorComponent>(this, TEXT("RefereeVisual"));
		Child->SetupAttachment(GetMesh());
		Child->RegisterComponent();
	}
	Child->SetChildActorClass(VisualOverrideClass);
	VisualChild = Child;
	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		Sk->SetVisibility(false, false); // логический манекен только ведёт позу
	}
}

void ABoxingReferee::SetupVisual()
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
	// Видимый меш с ретаргетом позы (как у бойцов, ABoxerCharacter::SetupFeel): его AnimInstance → наш (ретаргет + жесты).
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
			UE_LOG(LogTemp, Warning, TEXT("REF: у %s нет меша с ретаргетом позы — жестов не будет"), *Vis->GetName());
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
		UE_LOG(LogTemp, Warning, TEXT("REF: ретаргетер не найден — жестов не будет"));
		bVisualReady = true;
		return;
	}
	Best->SetAnimInstanceClass(UFightRefereeAnimInstance::StaticClass());
	VisualAnim = Cast<UFightRefereeAnimInstance>(Best->GetAnimInstance());
	if (VisualAnim)
	{
		VisualAnim->Referee = this;
		VisualAnim->SetupRetarget(Logic, Rtg, bHasProfile ? &Profile : nullptr);
		Best->AddTickPrerequisiteComponent(Logic);
	}
	bVisualReady = true;
	UE_LOG(LogTemp, Log, TEXT("REF: видимый меш %s.%s → UFightRefereeAnimInstance (%s), ретаргетер %s"), *Vis->GetName(), *Best->GetName(),
		VisualAnim ? TEXT("ok") : TEXT("НЕ создан"), *Rtg->GetName());
}

void ABoxingReferee::PushGaspInputState()
{
	SetGaspInput(this, true, true);
}

BoxRef::FV ABoxingReferee::CameraForBrain(BoxRef::EPhase Phase) const
{
	const ABoxingFightGameMode* GM = Mode.Get();
	const FVector Floor = GM ? GM->GetRingFloorCenter() : FVector::ZeroVector;
	FVector Base, Final;
	const ABoxingFightPlayerController* PC = Cast<ABoxingFightPlayerController>(GetWorld()->GetFirstPlayerController());
	if (PC && PC->GetRefereeCamera(Base, Final))
	{
		// Как в вебе (S-4): в бою и на выходе — камера боя (не кадр перерыва, который ещё догоняет игрока),
		// в перерыве, на счёте и в итоге — та, что на экране (кадр угла / нокдауна).
		const bool bFinal = Phase == BoxRef::EPhase::Rest || Phase == BoxRef::EPhase::Down || Phase == BoxRef::EPhase::Over;
		const FVector C = bFinal ? Final : Base;
		return BoxRef::FV((C.X - Floor.X) / 100.0, (C.Y - Floor.Y) / 100.0);
	}
	// Нет контроллера боя — камера боя веба: за спиной игрока 1.5 м и вправо 3.05 м.
	if (GM && GM->GetBoxer(0) && GM->GetBoxer(1))
	{
		const FVector P0 = (GM->GetBoxer(0)->GetActorLocation() - Floor) / 100.0;
		const FVector P1 = (GM->GetBoxer(1)->GetActorLocation() - Floor) / 100.0;
		const double L = FMath::Max(1e-3, FVector::Dist2D(P0, P1));
		const double Ux = (P1.X - P0.X) / L, Uz = (P1.Y - P0.Y) / L;
		const double Mx = (P0.X + P1.X) / 2, Mz = (P0.Y + P1.Y) / 2;
		return BoxRef::FV(Mx - Ux * 1.5 - Uz * 3.05, Mz - Uz * 1.5 + Ux * 3.05);
	}
	return BoxRef::FV(0, 4.6);
}

void ABoxingReferee::Tick(float DeltaSeconds)
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
		PushGaspInputState();
	}
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM)
	{
		GM = Cast<ABoxingFightGameMode>(GetWorld()->GetAuthGameMode());
		Mode = GM;
	}
	if (!GM || !GM->IsFightStarted() || !GM->GetBoxer(0) || !GM->GetBoxer(1))
	{
		return;
	}
	// Повтор нокаута (S-54) ведёт бойцов записью — рефери записи нет: на время повтора он скрыт и стоит.
	const UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	const bool bReplay = Fx && Fx->IsReplaying();
	if (bReplay != bHiddenForReplay)
	{
		bHiddenForReplay = bReplay;
		SetActorHiddenInGame(bReplay);
		if (VisualChild && VisualChild->GetChildActor())
		{
			VisualChild->GetChildActor()->SetActorHiddenInGame(bReplay);
		}
	}
	if (bReplay)
	{
		if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
		{
			Cmc->Velocity = FVector::ZeroVector;
		}
		return;
	}

	Clock += DeltaSeconds; // время рефери — без повтора (он стоит)
	// Отладка: -BoxRefSurrenderAt=С — игрок сдаётся через С сек (проверка позы «отказ» стоя и объявления; не в автопилоте).
	static float SurrenderAt = [] { float V = -1.f; FParse::Value(FCommandLine::Get(), TEXT("BoxRefSurrenderAt="), V); return V; }();
	if (SurrenderAt > 0.f && Clock >= SurrenderAt && GM->GetSnapshot().Phase == EFightPhase::Fighting)
	{
		SurrenderAt = -1.f;
		UE_LOG(LogTemp, Log, TEXT("REF: отладка — игрок сдаётся"));
		GM->Surrender();
	}
	const FFightSnapshot& S = GM->GetSnapshot();
	const FVector Floor = GM->GetRingFloorCenter();
	BoxRef::FV At[2];
	for (int32 I = 0; I < 2; ++I)
	{
		const FVector P = GM->GetBoxer(I)->GetActorLocation();
		At[I] = BoxRef::FV((P.X - Floor.X) / 100.0, (P.Y - Floor.Y) / 100.0);
	}
	const FFightResult* Result = GM->GetCore().IsOver() ? &GM->GetCore().GetResult() : nullptr;
	// Остановлен на ногах (web S-32 stoppageView): RSC по итогам раунда или отказ — боец решает это сам (не падает).
	bool bStanding = false;
	if (Result && S.Phase == EFightPhase::Over && Result->WinnerIndex >= 0)
	{
		const ABoxerCharacter* Loser = GM->GetBoxer(1 - Result->WinnerIndex);
		bStanding = Loser && Loser->IsStoppedStanding();
	}
	// Фаза — для выбора камеры; потом вход целиком.
	BoxRef::FInput In = BoxRef::InputFromSnapshot(S, Result, At, BoxRef::FV(), bStanding);
	In.Camera = CameraForBrain(In.Phase);
	In.bHasFightCam = true;
	for (int32 I = 0; I < 2; ++I)
	{
		const float H = GM->GetBoxer(I)->Preset.HeightCm;
		In.FighterScale[I] = H > 0.f ? H / 178.0 : 1.0; // габарит бойца по росту (облик S-60 масштабирует визуал так же)
	}
	In.FightCam = CameraForBrain(BoxRef::EPhase::Fight);
	if (!bBrainInit)
	{
		bBrainInit = true;
		const FVector P = GetActorLocation();
		const BoxRef::FV P0((P.X - Floor.X) / 100.0, (P.Y - Floor.Y) / 100.0);
		Brain.Place(P0, FMath::Atan2(-(In.Camera.Z - P0.Z), In.Camera.X - P0.X)); // лицом к камере
	}
	// Шаг логики: реальный кадр × замедление мира (slow-mo нокдауна замедляет и рефери), без хит-стопа бойцов.
	const double Dt = FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, 0.1);
	Frame = Brain.Update(In, Dt);
	OverT = In.Phase == BoxRef::EPhase::Over ? OverT + Dt : 0.0;
	bOverStoppageLying = In.Over.bValid && In.Over.bStoppage && !In.Over.bStanding;
	TrackBrain(DeltaSeconds);
	BuildPose(In, DeltaSeconds);
	UpdateMetrics(In, DeltaSeconds);
	UpdateShots(In);
	UpdateDebugCamera();
}

void ABoxingReferee::UpdateDebugCamera()
{
	// -BoxRefCam[=ДИСТ,ВЫСОТА,УГОЛ] — камера боя переставлена на рефери (жесты крупно): спереди под углом УГОЛ (град,
	// + — влево от него) на ДИСТ см, на высоте ВЫСОТА см. Рефери тикает после контроллера (он ставит камеру боя).
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("BoxRefCam")) || FString(FCommandLine::Get()).Contains(TEXT("BoxRefCam="));
	if (!bOn)
	{
		return;
	}
	ABoxingFightPlayerController* PC = Cast<ABoxingFightPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !PC->FightCamera)
	{
		return;
	}
	if (!bDebugCamPrereq)
	{
		bDebugCamPrereq = true;
		AddTickPrerequisiteActor(PC);
	}
	float Dist = 230.f, Height = 150.f, Ang = 25.f;
	FString Arg;
	if (FParse::Value(FCommandLine::Get(), TEXT("BoxRefCam="), Arg))
	{
		TArray<FString> P;
		Arg.ParseIntoArray(P, TEXT(","));
		if (P.Num() > 0) Dist = FCString::Atof(*P[0]);
		if (P.Num() > 1) Height = FCString::Atof(*P[1]);
		if (P.Num() > 2) Ang = FCString::Atof(*P[2]);
	}
	const ABoxingFightGameMode* GM = Mode.Get();
	const float FloorZ = GM ? GM->GetRingFloorCenter().Z : 0.f;
	const FVector Me = GetActorLocation();
	const FVector Dir = Pose.Fwd.RotateAngleAxis(-Ang, FVector::UpVector);
	const FVector Cam(Me.X + Dir.X * Dist, Me.Y + Dir.Y * Dist, FloorZ + Height);
	const FVector Look(Me.X, Me.Y, FloorZ + 125.f);
	PC->FightCamera->SetActorLocationAndRotation(Cam, (Look - Cam).Rotation());
}

void ABoxingReferee::TrackBrain(float DeltaSeconds)
{
	const ABoxingFightGameMode* GM = Mode.Get();
	UCharacterMovementComponent* Cmc = GetCharacterMovement();
	if (!GM || !Cmc || DeltaSeconds <= 0.f)
	{
		return;
	}
	const FVector Want = GM->FightToWorld(static_cast<float>(Frame.X), static_cast<float>(Frame.Z));
	const FVector Pos = GetActorLocation();
	FVector Err = Want - Pos;
	Err.Z = 0.f;
	MaxLagCm = FMath::Max(MaxLagCm, static_cast<float>(Err.Size()));
	const float YawDeg = FMath::RadiansToDegrees(static_cast<float>(-Frame.Yaw));
	const FRotator Facing(0.f, YawDeg, 0.f);
	SetActorRotation(Facing);
	if (Controller)
	{
		Controller->SetControlRotation(Facing);
	}
	if (Err.Size() > TeleportDistance)
	{
		SetActorLocation(FVector(Want.X, Want.Y, Pos.Z), false, nullptr, ETeleportType::TeleportPhysics);
		Cmc->Velocity = FVector::ZeroVector;
		return;
	}
	const FVector BrainV(Brain.Vel.X * 100.0, Brain.Vel.Z * 100.0, 0.0);
	if (Err.Size() < TRACK_DEADBAND && BrainV.SizeSquared() < 1.f)
	{
		Cmc->Velocity.X = 0.f;
		Cmc->Velocity.Y = 0.f;
		return;
	}
	const float Gain = FMath::Min(TRACK_GAIN, 1.f / DeltaSeconds);
	const FVector V = (BrainV + Err * Gain).GetClampedToMaxSize(400.f);
	Cmc->Velocity.X = V.X;
	Cmc->Velocity.Y = V.Y;
	const float MaxSpeed = FMath::Max(1.f, Cmc->GetMaxSpeed());
	AddMovementInput(V.GetSafeNormal(), FMath::Clamp(V.Size() / MaxSpeed, 0.05f, 1.f));
}

void ABoxingReferee::BuildPose(const BoxRef::FInput& In, float DeltaSeconds)
{
	const float Dt = FMath::Max(0.f, DeltaSeconds);
	const float YawUe = static_cast<float>(-Frame.Yaw);
	Pose.bValid = true;
	Pose.Fwd = FVector(FMath::Cos(YawUe), FMath::Sin(YawUe), 0.f);
	Pose.Left = FVector(FMath::Sin(YawUe), -FMath::Cos(YawUe), 0.f);
	for (int32 I = 0; I < 2; ++I)
	{
		Pose.Upper[I] = WorldDir(Pose, Frame.Arms[I].Upper);
		Pose.Fore[I] = WorldDir(Pose, Frame.Arms[I].Fore);
		Pose.ArmW[I] = static_cast<float>(Frame.ArmW[I]);
		float Want[5];
		CurlFor(Frame.Hands[I], Want);
		for (int32 K = 0; K < 5; ++K)
		{
			Curl[I][K] = SmoothTo(Curl[I][K], Want[K], FINGER_RATE, Dt);
			// Свободная рука (локомоция GASP) — пальцы как у клипа, сгиб — по доле жеста.
			Pose.Curl[I][K] = Curl[I][K] * Pose.ArmW[I];
		}
	}
	Pose.Lean = static_cast<float>(Frame.Lean);
	Pose.Ready = static_cast<float>(Frame.Ready);

	// Куда смотрит (Referee.tsx lookAt): бой — на пару, нокдаун/досрочка — на сбитого, решение — в камеру,
	// перерыв — в ринг; стоит между бойцами — то на одного, то на другого.
	const ABoxingFightGameMode* GM = Mode.Get();
	const FVector Floor = GM ? GM->GetRingFloorCenter() : FVector::ZeroVector;
	auto W = [&Floor](const BoxRef::FV& P, double H) { return FVector(Floor.X + P.X * 100.0, Floor.Y + P.Z * 100.0, Floor.Z + H * 100.0); };
	const BoxRef::FV& F0 = In.Fighters[0];
	const BoxRef::FV& F1 = In.Fighters[1];
	FVector Target = W(BoxRef::FV((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2), 1.45);
	if (BoxRef::Hyp(BoxRef::FV((F0.X + F1.X) / 2, (F0.Z + F1.Z) / 2), Brain.Pos) < 1.2)
	{
		Target = W(FMath::FloorToInt(Clock / 2.6) % 2 == 0 ? F0 : F1, 1.5);
	}
	if (In.Phase == BoxRef::EPhase::Down && In.Down.bValid)
	{
		Target = W(In.Fighters[In.Down.Who], 0.35);
	}
	else if (In.Phase == BoxRef::EPhase::Over && In.Over.bValid && In.Over.bStoppage && In.Over.Winner >= 0 && !In.Over.bStanding)
	{
		Target = W(In.Fighters[1 - In.Over.Winner], 0.35);
	}
	else if (In.Phase == BoxRef::EPhase::Over)
	{
		Target = W(In.Camera, 1.6);
	}
	else if (In.Phase == BoxRef::EPhase::Rest)
	{
		Target = W(BoxRef::FV(In.Camera.X * 0.3, In.Camera.Z * 0.3), 1.5);
	}
	FVector HeadPos = GetActorLocation() + FVector(0.f, 0.f, 70.f);
	FVector D = Target - HeadPos;
	const float Fw = FVector::DotProduct(D, Pose.Fwd);
	const float Lt = FVector::DotProduct(D, Pose.Left);
	const float YawT = FMath::Clamp(FMath::Atan2(Lt, Fw), -HEAD_YAW_MAX, HEAD_YAW_MAX);
	const float PitchT = FMath::Clamp(FMath::Atan2(static_cast<float>(D.Z), FMath::Sqrt(Fw * Fw + Lt * Lt)), -0.55f, 0.3f);
	HeadYaw = SmoothTo(HeadYaw, YawT, HEAD_RATE, Dt);
	HeadPitch = SmoothTo(HeadPitch, PitchT, HEAD_RATE, Dt);
	Pose.HeadYaw = HeadYaw;
	Pose.HeadPitch = HeadPitch;
}

void ABoxingReferee::UpdateMetrics(const BoxRef::FInput& In, float DeltaSeconds)
{
	const ABoxingFightGameMode* GM = Mode.Get();
	if (!GM)
	{
		return;
	}
	const FFightSnapshot& S = GM->GetSnapshot();
	const FVector Me = GetActorLocation();
	const FVector Floor = GM->GetRingFloorCenter();
	// Ход логики (м/с) — без телепорта.
	if (bPrevLoc && DeltaSeconds > 0.f)
	{
		MaxSpeedSeen = FMath::Max(MaxSpeedSeen, static_cast<float>(FVector::Dist2D(Me, PrevActorLoc) / 100.0 / DeltaSeconds));
	}
	PrevActorLoc = Me;
	bPrevLoc = true;
	// До стоящих бойцов (центры).
	for (int32 I = 0; I < 2; ++I)
	{
		if (!S.Fighters[I].bDown)
		{
			MinStandCm = FMath::Min(MinStandCm, static_cast<float>(FVector::Dist2D(Me, GM->GetBoxer(I)->GetActorLocation())));
		}
	}
	if (S.Phase == EFightPhase::Down && KdSeen == 0)
	{
		KdSeen = 1;
	}
	// «Рефери закрывает бойца в кадре»: лучи камеры к точкам бойца (40…170 см) проходят сквозь ось рефери
	// (вертикальный отрезок, радиус OCC_R) ближе к камере, чем боец. Боец закрыт, если закрыто ≥ OCC_FRAC точек.
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager : nullptr;
	if (!PCM)
	{
		return;
	}
	const FVector Cam = PCM->GetCameraLocation();
	const FVector A0(Me.X, Me.Y, Floor.Z);
	const FVector A1(Me.X, Me.Y, Floor.Z + OCC_TOP);
	bool bOcc = false;
	for (int32 I = 0; I < 2 && !bOcc; ++I)
	{
		if (S.Fighters[I].bDown)
		{
			continue;
		}
		const FVector B = GM->GetBoxer(I)->GetActorLocation();
		int32 Hit = 0;
		constexpr int32 N = 6;
		for (int32 K = 0; K < N; ++K)
		{
			const FVector P(B.X, B.Y, Floor.Z + 40.f + 130.f * K / (N - 1));
			FVector OnRay, OnAxis;
			FMath::SegmentDistToSegmentSafe(Cam, P, A0, A1, OnRay, OnAxis);
			const double T = FVector::Dist(Cam, OnRay) / FMath::Max(1.0, FVector::Dist(Cam, P));
			if (FVector::Dist(OnRay, OnAxis) < OCC_R && T < 0.97)
			{
				++Hit;
			}
		}
		bOcc = Hit >= FMath::CeilToInt(OCC_FRAC * N);
	}
	++AllFrames;
	if (bOcc)
	{
		++OccludedAny;
	}
	const bool bFightFrame = S.Phase == EFightPhase::Fighting && In.Phase == BoxRef::EPhase::Fight;
	if (bFightFrame)
	{
		++FightFrames;
		const BoxRef::FV Seg = BoxRef::ClosestOnSeg(In.Fighters[0], In.Fighters[1], Brain.Pos);
		const double Sd = BoxRef::Hyp(Brain.Pos, Seg);
		if (Sd >= BoxRef::SIDE_MIN - 0.15 && Sd <= BoxRef::SIDE_MAX + 0.15)
		{
			++SideOk;
		}
		if (bOcc)
		{
			++OccludedFrames;
		}
		OccludedRun = bOcc ? OccludedRun + DeltaSeconds : 0.f;
		OccludedWorst = FMath::Max(OccludedWorst, OccludedRun);
	}
	else
	{
		OccludedRun = 0.f;
	}
	if (bDraw)
	{
		BoxRef::FV Goal;
		if (Brain.GetTarget(Goal))
		{
			DrawDebugSphere(GetWorld(), GM->FightToWorld(static_cast<float>(Goal.X), static_cast<float>(Goal.Z)) + FVector(0, 0, 5), 12.f, 8, FColor::Yellow, false, -1.f);
		}
		DrawDebugLine(GetWorld(), A0, A1, bOcc ? FColor::Red : FColor::Green, false, -1.f, 0, 2.f);
	}
	if (bLog)
	{
		LogTimer -= DeltaSeconds;
		if (LogTimer <= 0.f)
		{
			LogTimer = 1.f;
			FParse::Value(FCommandLine::Get(), TEXT("BoxRefLogEvery="), LogTimer);
			UE_LOG(LogTemp, Log, TEXT("REF t=%.1f phase=%d place(%.2f,%.2f) yaw=%.0f walk=%.2f ready=%.2f armW=%.2f/%.2f hands=%d/%d raised=%.1f cam(%.2f,%.2f) occ=%d"),
				Clock, static_cast<int32>(In.Phase), Frame.X, Frame.Z, FMath::RadiansToDegrees(-Frame.Yaw), Frame.Walk, Frame.Ready, Frame.ArmW[0], Frame.ArmW[1],
				static_cast<int32>(Frame.Hands[0]), static_cast<int32>(Frame.Hands[1]), Frame.Raised, In.Camera.X, In.Camera.Z, bOcc ? 1 : 0);
		}
	}
	if (S.Phase == EFightPhase::Over && !bSummaryLogged && Frame.Raised > 2.0)
	{
		LogSummary(TEXT("итог"));
	}
}

void ABoxingReferee::LogSummary(const TCHAR* Why)
{
	if (bSummaryLogged && FCString::Strcmp(Why, TEXT("конец")) == 0)
	{
		return;
	}
	bSummaryLogged = true;
	UE_LOG(LogTemp, Log, TEXT("REF СВОДКА (%s): кадров боя %d — рефери закрывает бойца в кадре камеры %d (%.1f%%), макс. подряд %.2f с; во всех фазах %d из %d; сбоку 1.2–2 м %.1f%%; мин. до стоящего бойца %.0f см; макс. ход актора %.2f м/с, макс. отставание от логики %.0f см"),
		Why, FightFrames, OccludedFrames, FightFrames ? 100.f * OccludedFrames / FightFrames : 0.f, OccludedWorst, OccludedAny, AllFrames,
		FightFrames ? 100.f * SideOk / FightFrames : 0.f, MinStandCm, MaxSpeedSeen, MaxLagCm);
}

void ABoxingReferee::UpdateShots(const BoxRef::FInput& In)
{
	if (!bShots)
	{
		return;
	}
	const ABoxingFightGameMode* GM = Mode.Get();
	const FFightSnapshot& S = GM->GetSnapshot();
	const int32 PhaseKey = static_cast<int32>(In.Phase);
	if (PhaseKey != LastShotPhase)
	{
		LastShotPhase = PhaseKey;
		PhaseSince = Clock;
	}
	const double InPhase = Clock - PhaseSince;
	FString Name;
	if (In.Phase == BoxRef::EPhase::Fight && S.Round == 1 && InPhase > 6.0 && !ShotsTaken.Contains(TEXT("fight")))
	{
		Name = TEXT("fight");
	}
	else if (In.Phase == BoxRef::EPhase::Fight && S.Round >= 2 && InPhase > 5.0 && !ShotsTaken.Contains(TEXT("fight2")))
	{
		Name = TEXT("fight2");
	}
	else if (In.Phase == BoxRef::EPhase::Down && In.Down.bValid && !In.Down.bArrived && InPhase > 0.9 && !ShotsTaken.Contains(TEXT("kd_point")))
	{
		Name = TEXT("kd_point");
	}
	else if (In.Phase == BoxRef::EPhase::Down && In.Down.bValid && In.Down.Count >= 4 && In.Down.bArrived && !ShotsTaken.Contains(TEXT("kd_count")))
	{
		Name = TEXT("kd_count");
	}
	else if (In.Phase == BoxRef::EPhase::Rest && InPhase > 7.0 && !ShotsTaken.Contains(TEXT("rest")))
	{
		Name = TEXT("rest");
	}
	else if (In.Phase == BoxRef::EPhase::Out && S.Round == 2 && InPhase > 1.6 && !ShotsTaken.Contains(TEXT("out")))
	{
		Name = TEXT("out");
	}
	else if (In.Phase == BoxRef::EPhase::Over && In.Over.bStoppage && InPhase > 1.3 && InPhase < 2.0 && !ShotsTaken.Contains(TEXT("stop")))
	{
		Name = TEXT("stop");
	}
	else if (In.Phase == BoxRef::EPhase::Over && In.Over.bStoppage && InPhase > 4.5 && !ShotsTaken.Contains(TEXT("stop_late")))
	{
		Name = TEXT("stop_late");
	}
	else if (In.Phase == BoxRef::EPhase::Over && Frame.Raised > 0.35 && !ShotsTaken.Contains(TEXT("winner")))
	{
		Name = TEXT("winner");
	}
	if (Name.IsEmpty())
	{
		return;
	}
	ShotsTaken.Add(Name);
	FString Prefix = TEXT("ref");
	FParse::Value(FCommandLine::Get(), TEXT("BoxRefShotPrefix="), Prefix);
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") / FString::Printf(TEXT("%s_%s.png"), *Prefix, *Name));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogTemp, Log, TEXT("REF: скриншот %s (рефери (%.2f, %.2f), фаза %d, счёт %d)"), *Path, Frame.X, Frame.Z, PhaseKey, In.Down.Count);
}
