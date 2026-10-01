#include "BoxerCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/UnrealType.h"

namespace
{
	// Усиление слежения за точкой ядра (1/с): скорость = упреждение + ошибка × K.
	constexpr float TRACK_GAIN = 12.f;
	// Ниже этой ошибки (см) и без упреждения — стоим (Motion Matching видит «стойку»).
	constexpr float TRACK_DEADBAND = 0.4f;
	// Запас до конца монтажа, на котором держим «последний кадр» (блок, нокдаун).
	constexpr float HOLD_MARGIN = 0.03f;
}

ABoxerCharacter::ABoxerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	PhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));
}

void ABoxerCharacter::BeginPlay()
{
	Super::BeginPlay();

	USkeletalMeshComponent* Sk = GetMesh();
	if (Sk)
	{
		// Без рендера (сервер, -nullrhi, скрытый базовый меш под ретаргетом) поза всё равно считается.
		Sk->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (PhysicalAnimation)
		{
			PhysicalAnimation->SetSkeletalMeshComponent(Sk);
		}
		// Физреакции нужны тела физассета: у меша GASP коллизия может быть выключена — тогда тел нет.
		if (bPhysicalHitReactions && Sk->GetPhysicsAsset() &&
			(Sk->GetCollisionEnabled() == ECollisionEnabled::NoCollision || Sk->GetCollisionEnabled() == ECollisionEnabled::QueryOnly))
		{
			Sk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
	}
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		// Ход задаёт ядро: аналоговый минимум скорости GASP (150 см/с) дёргал бы медленные шаги.
		Cmc->MinAnalogWalkSpeed = 0.f;
	}
	LoadDefaultMontages();
	ApplyVisualOverride();
	PushGaspInputState();
}

void ABoxerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	PushGaspInputState();
}

// ---------------------------------------------------------------------------------------------
// Монтажи: автоподхват и время контакта
// ---------------------------------------------------------------------------------------------

UAnimMontage* ABoxerCharacter::FindMontage(const TCHAR* Name) const
{
	const FString Asset = FString::Printf(TEXT("AM_%s"), Name);
	const FString Package = MontageFolder / Asset;
	if (!FPackageName::DoesPackageExist(Package))
	{
		return nullptr;
	}
	return LoadObject<UAnimMontage>(nullptr, *(Package + TEXT(".") + Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void ABoxerCharacter::LoadDefaultMontages()
{
	static const TPair<EBoxPunchType, const TCHAR*> PunchNames[] = {
		{EBoxPunchType::Jab, TEXT("Jab")},       {EBoxPunchType::Cross, TEXT("Cross")},
		{EBoxPunchType::HookL, TEXT("HookL")},   {EBoxPunchType::HookR, TEXT("HookR")},
		{EBoxPunchType::UpperL, TEXT("UpperL")}, {EBoxPunchType::UpperR, TEXT("UpperR")},
	};
	int32 Found = 0;
	for (const TPair<EBoxPunchType, const TCHAR*>& P : PunchNames)
	{
		TObjectPtr<UAnimMontage>* Existing = PunchMontages.Find(P.Key);
		if (Existing && *Existing)
		{
			++Found;
			continue;
		}
		if (UAnimMontage* M = FindMontage(P.Value))
		{
			PunchMontages.Add(P.Key, M);
			++Found;
		}
	}
	auto Fill = [this, &Found](TObjectPtr<UAnimMontage>& Slot, const TCHAR* Name)
	{
		if (!Slot)
		{
			Slot = FindMontage(Name);
		}
		if (Slot)
		{
			++Found;
		}
	};
	Fill(BlockMontage, TEXT("Block"));
	Fill(SlipLeftMontage, TEXT("SlipL"));
	Fill(SlipRightMontage, TEXT("SlipR"));
	Fill(HitHeadMontage, TEXT("HitHead"));
	Fill(HitBodyMontage, TEXT("HitBody"));
	Fill(KnockdownMontage, TEXT("Knockdown"));
	Fill(GetUpMontage, TEXT("GetUp"));
	UE_LOG(LogTemp, Log, TEXT("BOXER %s [%d]: монтажей найдено %d из 13 (%s)"), *GetName(), FighterIndex, Found, *MontageFolder);
}

float ABoxerCharacter::GetPunchContactTime(EBoxPunchType Punch) const
{
	const TObjectPtr<UAnimMontage>* MPtr = PunchMontages.Find(Punch);
	const UAnimMontage* M = MPtr ? MPtr->Get() : nullptr;
	if (const float* T = PunchContactTimes.Find(Punch))
	{
		if (*T > 0.f)
		{
			return *T;
		}
	}
	if (!M)
	{
		return 0.f;
	}
	for (const FAnimNotifyEvent& N : M->Notifies)
	{
		const FString NName = N.NotifyName.ToString();
		if (NName.Contains(TEXT("Contact")) || NName.Equals(TEXT("Hit"), ESearchCase::IgnoreCase))
		{
			return N.GetTime();
		}
	}
	return M->GetPlayLength() * 0.5f; // как в вебе: контакт клипа = середина (POSE_CONTACT 0.5)
}

UAnimInstance* ABoxerCharacter::GetAnimInst() const
{
	const USkeletalMeshComponent* Sk = GetMesh();
	return Sk ? Sk->GetAnimInstance() : nullptr;
}

bool ABoxerCharacter::PlaySlotMontage(EBoxMontageSlot Slot, UAnimMontage* Montage, float Rate, float StartPos)
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim || !Montage)
	{
		return false;
	}
	const float Len = Anim->Montage_Play(Montage, FMath::Clamp(Rate, 0.1f, 6.f), EMontagePlayReturnType::MontageLength,
		FMath::Clamp(StartPos, 0.f, Montage->GetPlayLength()), true);
	if (Len <= 0.f)
	{
		return false;
	}
	ActiveMontage = Montage;
	ActiveMontageSlot = Slot;
	return true;
}

void ABoxerCharacter::StopSlotMontage(float BlendOut)
{
	if (UAnimInstance* Anim = GetAnimInst())
	{
		if (ActiveMontage)
		{
			Anim->Montage_Stop(BlendOut, ActiveMontage);
		}
	}
	ActiveMontage = nullptr;
	ActiveMontageSlot = EBoxMontageSlot::None;
}

void ABoxerCharacter::HoldAtEnd()
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim || !ActiveMontage)
	{
		return;
	}
	const float Len = ActiveMontage->GetPlayLength();
	const float Hold = FMath::Max(0.f, Len - ActiveMontage->GetDefaultBlendOutTime() - HOLD_MARGIN);
	if (Anim->Montage_IsPlaying(ActiveMontage) && Anim->Montage_GetPosition(ActiveMontage) >= Hold)
	{
		Anim->Montage_SetPosition(ActiveMontage, Hold);
		Anim->Montage_Pause(ActiveMontage);
	}
}

void ABoxerCharacter::SyncPunchMontage()
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim || !ActiveMontage || PunchDuration <= 0.f)
	{
		return;
	}
	// Кусочно-линейное соответствие: [0, контакт ядра] → [0, ContactTime], [контакт, конец] → [ContactTime, длина].
	const float Len = ActiveMontage->GetPlayLength();
	const float C = FMath::Clamp(GetPunchContactTime(CurrentPunch), 0.02f, Len - 0.02f);
	const float Cf = FMath::Clamp(PunchContactFraction, 0.05f, 0.95f);
	const float U = FMath::Clamp(PunchPhase, 0.f, 1.f);
	const bool bBefore = U < Cf;
	const float Desired = bBefore ? (U / Cf) * C : C + ((U - Cf) / (1.f - Cf)) * (Len - C);
	const float Rate = bBefore ? C / (Cf * PunchDuration) : (Len - C) / ((1.f - Cf) * PunchDuration);
	Anim->Montage_SetPlayRate(ActiveMontage, FMath::Clamp(Rate, 0.1f, 6.f));
	if (FMath::Abs(Anim->Montage_GetPosition(ActiveMontage) - Desired) > MontageSnapTolerance)
	{
		Anim->Montage_SetPosition(ActiveMontage, Desired);
	}
}

void ABoxerCharacter::SyncSlipMontage()
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim || !ActiveMontage)
	{
		return;
	}
	const float Len = ActiveMontage->GetPlayLength();
	Anim->Montage_SetPlayRate(ActiveMontage, FMath::Clamp(Len / FMath::Max(0.05f, SlipWindowSeconds), 0.1f, 6.f));
	const float Desired = SlipPhase * Len;
	if (FMath::Abs(Anim->Montage_GetPosition(ActiveMontage) - Desired) > MontageSnapTolerance)
	{
		Anim->Montage_SetPosition(ActiveMontage, Desired);
	}
}

void ABoxerCharacter::UpdateMontages()
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim)
	{
		return;
	}
	// Монтаж кончился или его перебили снаружи — слот свободен.
	if (ActiveMontageSlot != EBoxMontageSlot::None && (!ActiveMontage || !Anim->Montage_IsActive(ActiveMontage)))
	{
		ActiveMontage = nullptr;
		ActiveMontageSlot = EBoxMontageSlot::None;
	}

	switch (ActiveMontageSlot)
	{
	case EBoxMontageSlot::Punch:
		if (bPunching)
		{
			SyncPunchMontage();
		}
		break;
	case EBoxMontageSlot::Slip:
		if (SlipSide != 0)
		{
			SyncSlipMontage();
		}
		break;
	case EBoxMontageSlot::Block:
		if (!bBlocking)
		{
			StopSlotMontage(0.15f);
		}
		else
		{
			HoldAtEnd();
		}
		break;
	case EBoxMontageSlot::Knockdown:
		if (bKnockedDown)
		{
			HoldAtEnd();
		}
		break;
	default:
		break;
	}

	// Блок держится, а слот освободился (после реакции/удара) — снова поднять руки.
	if (bBlocking && ActiveMontageSlot == EBoxMontageSlot::None && BlockMontage)
	{
		PlaySlotMontage(EBoxMontageSlot::Block, BlockMontage, 1.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Состояние из ядра
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::SnapToFightState(const FVector& WorldTarget, float YawDeg)
{
	FightTarget = WorldTarget;
	FightYaw = YawDeg;
	bHasTarget = true;
	FVector Loc = GetActorLocation();
	Loc.X = WorldTarget.X;
	Loc.Y = WorldTarget.Y;
	SetActorLocationAndRotation(Loc, FRotator(0.f, YawDeg, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (Controller)
	{
		Controller->SetControlRotation(FRotator(0.f, YawDeg, 0.f));
	}
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->Velocity = FVector::ZeroVector;
	}
	FightTargetVelocity = FVector::ZeroVector;
}

void ABoxerCharacter::ApplyFightState(const FFightSnapshot& Snapshot, double CoreTime, const FVector& WorldTarget, float YawDeg, float DeltaSeconds)
{
	const FFighterState& F = Snapshot.Fighters[FighterIndex];
	CoreNow = CoreTime;
	if (CoreTime > PrevTargetCoreTime + 1e-6)
	{
		if (PrevTargetCoreTime >= 0.0)
		{
			FVector V = (WorldTarget - PrevTargetPos) / static_cast<float>(CoreTime - PrevTargetCoreTime);
			V.Z = 0.f;
			FightTargetVelocity = V.Size() > MaxTrackSpeed ? FVector::ZeroVector : V; // скачок (сброс раунда) — не ход
		}
		PrevTargetCoreTime = CoreTime;
		PrevTargetPos = WorldTarget;
	}
	FightTarget = WorldTarget;
	FightYaw = YawDeg;
	bHasTarget = true;

	// --- поля для AnimBP/HUD ---
	bPunching = F.bPunching;
	if (F.bPunching)
	{
		CurrentPunch = BoxingBP::Punch(F.Punch);
		PunchTarget = BoxingBP::Target(F.PunchTarget);
		PunchArm = BoxingBP::ArmOf(CurrentPunch);
		PunchDuration = F.PunchDuration;
		if (F.PunchDuration > 0.f)
		{
			PunchContactFraction = FMath::Clamp(F.PunchPhase + F.PunchTimeToContact / F.PunchDuration, 0.f, 1.f);
		}
	}
	PunchPhase = F.bPunching ? F.PunchPhase : 0.f;
	PunchPhaseAnim = F.bPunching ? F.PunchPhaseAnim : 0.f;
	bBlocking = F.bBlocking;
	GuardIntegrity = F.GuardIntegrity;
	SlipAmount = F.Slip;
	SlipPhase = F.SlipPhase;
	SlipSide = F.Slip > 0.f ? 1 : (F.Slip < 0.f ? -1 : 0);
	if (SlipSide == 0 && F.SlipPhase > 0.f && F.SlipPhase < 1.f)
	{
		SlipSide = PrevSlipSide; // края синуса (sin 0 = 0) — сторона прежняя
	}
	bKnockedDown = F.bDown;
	bKO = F.bKO;
	bStunned = F.bStaggered;
	Hurt = F.Hurt;
	HurtTarget = BoxingBP::Target(F.HurtTarget);
	StepKind = BoxingBP::Step(F.Step);
	RopeLevel = F.RopeLevel;
	Victory = F.Victory;
	bDefeated = F.bDefeated;
	Health = F.Health;
	StaminaPct = F.StaminaPct;
	Knockdowns = F.Knockdowns;
	bGassed = F.bGassed;

	// --- фронты: нокдаун / подъём ---
	if (bKnockedDown && !bWasDown)
	{
		OnKnockdown();
	}
	else if (!bKnockedDown && bWasDown)
	{
		OnGetUp();
	}

	// --- фронт: новый удар (время старта цикла сменилось) ---
	if (bPunching)
	{
		const double Start = CoreTime - static_cast<double>(F.PunchPhase) * F.PunchDuration;
		if (!bWasPunching || FMath::Abs(Start - LastPunchStart) > 0.02)
		{
			LastPunchStart = Start;
			OnPunchStarted(CurrentPunch, PunchTarget);
		}
	}

	// --- фронт: уклон ---
	if (SlipSide != 0 && PrevSlipSide == 0 && !bKnockedDown)
	{
		UAnimMontage* M = SlipSide < 0 ? SlipLeftMontage.Get() : SlipRightMontage.Get();
		if (M && ActiveMontageSlot != EBoxMontageSlot::Knockdown)
		{
			PlaySlotMontage(EBoxMontageSlot::Slip, M, M->GetPlayLength() / FMath::Max(0.05f, SlipWindowSeconds), SlipPhase * M->GetPlayLength());
		}
	}

	// --- фронт: блок ---
	if (bBlocking && !bWasBlocking && BlockMontage &&
		(ActiveMontageSlot == EBoxMontageSlot::None || ActiveMontageSlot == EBoxMontageSlot::Hit))
	{
		PlaySlotMontage(EBoxMontageSlot::Block, BlockMontage, 1.f);
	}

	UpdateMontages();

	bWasDown = bKnockedDown;
	bWasPunching = bPunching;
	bWasBlocking = bBlocking;
	PrevSlipSide = SlipSide;

	// Ход — здесь, в тике GameMode: CharacterMovement тикает ПОСЛЕ него (пререквизит в GameMode),
	// поэтому скорость/ввод этого кадра он и применит, а AnimBP (тикает после CMC) их увидит.
	TrackFightTarget(DeltaSeconds);
}

void ABoxerCharacter::HandleFightEvent(const FFightEvent& Event, const FVector& AttackerLocation)
{
	if (Event.Defender != FighterIndex)
	{
		return;
	}
	FVector Dir = GetActorLocation() - AttackerLocation;
	Dir.Z = 0.f;
	Dir = Dir.GetSafeNormal();
	if (Event.Kind == EFightEventKind::Hit)
	{
		OnHitReceived(BoxingBP::Punch(Event.Punch), BoxingBP::Target(Event.Target), Event.Magnitude, Dir);
	}
	else if (Event.Kind == EFightEventKind::Blocked)
	{
		OnBlockedPunch(BoxingBP::Punch(Event.Punch), Event.Magnitude, Dir);
	}
}

// ---------------------------------------------------------------------------------------------
// События (реализации по умолчанию)
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::OnPunchStarted_Implementation(EBoxPunchType Punch, EBoxPunchTarget Target)
{
	OnPunchStartedDelegate.Broadcast(Punch, Target);
	const TObjectPtr<UAnimMontage>* MPtr = PunchMontages.Find(Punch);
	UAnimMontage* M = MPtr ? MPtr->Get() : nullptr;
	if (!M || bKnockedDown || PunchDuration <= 0.f)
	{
		return;
	}
	const float Len = M->GetPlayLength();
	const float C = FMath::Clamp(GetPunchContactTime(Punch), 0.02f, Len - 0.02f);
	const float Cf = FMath::Clamp(PunchContactFraction, 0.05f, 0.95f);
	const float U = FMath::Clamp(PunchPhase, 0.f, Cf);
	// Скорость так, чтобы кадр контакта монтажа совпал с резолюцией удара в ядре.
	const float Rate = C / (Cf * PunchDuration);
	if (PlaySlotMontage(EBoxMontageSlot::Punch, M, Rate, (U / Cf) * C))
	{
		SyncPunchMontage();
	}
}

void ABoxerCharacter::OnHitReceived_Implementation(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction)
{
	OnHitReceivedDelegate.Broadcast(Punch, Target, Magnitude, Direction);
	HitReaction(Punch, Target, Magnitude, Direction);
	// Монтаж реакции не перебивает свой удар, уклон и нокдаун.
	if (Magnitude < HitMontageMinMagnitude || bKnockedDown ||
		ActiveMontageSlot == EBoxMontageSlot::Punch || ActiveMontageSlot == EBoxMontageSlot::Slip ||
		ActiveMontageSlot == EBoxMontageSlot::Knockdown || ActiveMontageSlot == EBoxMontageSlot::GetUp)
	{
		return;
	}
	UAnimMontage* M = Target == EBoxPunchTarget::Body ? HitBodyMontage.Get() : HitHeadMontage.Get();
	PlaySlotMontage(EBoxMontageSlot::Hit, M, 1.f);
}

void ABoxerCharacter::OnBlockedPunch_Implementation(EBoxPunchType Punch, float Magnitude, FVector Direction)
{
}

void ABoxerCharacter::OnKnockdown_Implementation()
{
	OnKnockdownDelegate.Broadcast();
	if (KnockdownMontage)
	{
		PlaySlotMontage(EBoxMontageSlot::Knockdown, KnockdownMontage, 1.f);
	}
}

void ABoxerCharacter::OnGetUp_Implementation()
{
	OnGetUpDelegate.Broadcast();
	if (GetUpMontage)
	{
		PlaySlotMontage(EBoxMontageSlot::GetUp, GetUpMontage, 1.f);
	}
	else if (ActiveMontageSlot == EBoxMontageSlot::Knockdown)
	{
		StopSlotMontage(0.4f);
	}
}

void ABoxerCharacter::HitReaction_Implementation(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction)
{
	if (!bPhysicalHitReactions || !PhysicalAnimation || Magnitude <= 0.f)
	{
		return;
	}
	USkeletalMeshComponent* Sk = GetMesh();
	if (!Sk || !Sk->GetPhysicsAsset() || Sk->Bodies.Num() == 0)
	{
		return;
	}
	const FName Bone = Target == EBoxPunchTarget::Body ? PhysBodyBone : PhysHeadBone;
	if (Sk->GetBoneIndex(Bone) == INDEX_NONE)
	{
		return;
	}
	if (PhysBone != NAME_None && PhysBone != Bone)
	{
		Sk->SetAllBodiesBelowSimulatePhysics(PhysBone, false, true);
	}
	// Пружины к анимационной позе: кость отлетает импульсом и возвращается к клипу.
	FPhysicalAnimationData Data;
	Data.bIsLocalSimulation = true;
	Data.OrientationStrength = 1200.f;
	Data.AngularVelocityStrength = 120.f;
	Data.PositionStrength = 0.f;
	Data.VelocityStrength = 0.f;
	if (bUsePhysicalAnimationDrive)
	{
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Bone, Data, true);
	}
	Sk->SetAllBodiesBelowSimulatePhysics(Bone, true, true);
	Sk->SetAllBodiesBelowPhysicsBlendWeight(Bone, PhysBlendPeak, false, true);
	Sk->AddImpulse(Direction * FMath::Min(Magnitude, 3.f) * PhysImpulsePerMagnitude, Bone, true);
	PhysBone = Bone;
	PhysTimeLeft = PhysHitDuration;
}

void ABoxerCharacter::UpdatePhysicalReaction(float DeltaSeconds)
{
	if (PhysBone == NAME_None)
	{
		return;
	}
	USkeletalMeshComponent* Sk = GetMesh();
	PhysTimeLeft -= DeltaSeconds;
	if (!Sk)
	{
		PhysBone = NAME_None;
		return;
	}
	if (PhysTimeLeft <= 0.f)
	{
		Sk->SetAllBodiesBelowSimulatePhysics(PhysBone, false, true);
		Sk->SetAllBodiesBelowPhysicsBlendWeight(PhysBone, 0.f, false, true);
		PhysBone = NAME_None;
		return;
	}
	const float K = FMath::Clamp(PhysTimeLeft / FMath::Max(0.01f, PhysHitDuration), 0.f, 1.f);
	Sk->SetAllBodiesBelowPhysicsBlendWeight(PhysBone, PhysBlendPeak * K, false, true);
}

// ---------------------------------------------------------------------------------------------
// Локомоция: точка ядра → CharacterMovement (Motion Matching видит Velocity/Acceleration)
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePhysicalReaction(DeltaSeconds);

	// GASP может перезаписать входное состояние (смена контроллера и т.п.) — подтверждаем раз в 0.5 с.
	GaspStateTimer -= DeltaSeconds;
	if (GaspStateTimer <= 0.f)
	{
		GaspStateTimer = 0.5f;
		PushGaspInputState();
	}
}

void ABoxerCharacter::TrackFightTarget(float DeltaSeconds)
{
	UCharacterMovementComponent* Cmc = GetCharacterMovement();
	if (!bHasTarget || !Cmc || DeltaSeconds <= 0.f)
	{
		return;
	}
	const FVector Pos = GetActorLocation();
	FVector Err = FightTarget - Pos;
	Err.Z = 0.f;
	const float Dist = Err.Size();
	if (Dist > TeleportDistance)
	{
		SnapToFightState(FightTarget, FightYaw);
		return;
	}

	// Курс — всегда лицом к сопернику (ядро). Контроллер — туда же: в стрейфе GASP крутит
	// капсулу к ControlRotation (bUseControllerDesiredRotation), так CMC не спорит с ядром.
	const FRotator Facing(0.f, FightYaw, 0.f);
	SetActorRotation(Facing);
	if (Controller)
	{
		Controller->SetControlRotation(Facing);
	}

	if (Dist < TRACK_DEADBAND && FightTargetVelocity.SizeSquared() < 1.f)
	{
		Cmc->Velocity.X = 0.f;
		Cmc->Velocity.Y = 0.f;
		return;
	}
	// Упреждение (скорость точки ядра) + догон ошибки за ~1/TRACK_GAIN с; на большом кадре — не дальше цели.
	const float Gain = FMath::Min(TRACK_GAIN, 1.f / DeltaSeconds);
	FVector V = FightTargetVelocity + Err * Gain;
	V = V.GetClampedToMaxSize(MaxTrackSpeed);
	Cmc->Velocity.X = V.X;
	Cmc->Velocity.Y = V.Y;
	// Входное ускорение в ту же сторону: GASP/Motion Matching отличает «иду» от «скольжу/торможу».
	const float MaxSpeed = FMath::Max(1.f, Cmc->GetMaxSpeed());
	AddMovementInput(V.GetSafeNormal(), FMath::Clamp(V.Size() / MaxSpeed, 0.05f, 1.f));
}

void ABoxerCharacter::PushGaspInputState()
{
	// Интерфейс BPI_SandboxCharacter_Pawn реализован в BP (копия SandboxCharacter_CMC): у нативного
	// класса функции нет — тогда просто ничего не делаем.
	UFunction* Fn = FindFunction(FName(TEXT("Set_CharacterInputState")));
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
				B->SetPropertyValue_InContainer(StructPtr, bGaspWantsToWalk);
			}
			else if (PName.StartsWith(TEXT("WantsToStrafe")))
			{
				B->SetPropertyValue_InContainer(StructPtr, bGaspWantsToStrafe);
			}
		}
	}
	ProcessEvent(Fn, Parms);
	for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Parms);
	}
	if (!bGaspStateSent)
	{
		bGaspStateSent = true;
		UE_LOG(LogTemp, Log, TEXT("BOXER %s [%d]: GASP input state — walk=%d strafe=%d"), *GetName(), FighterIndex,
			bGaspWantsToWalk ? 1 : 0, bGaspWantsToStrafe ? 1 : 0);
	}
}

void ABoxerCharacter::ApplyVisualOverride()
{
	if (!VisualOverrideClass)
	{
		return;
	}
	// В копии SandboxCharacter_CMC уже есть ChildActorComponent «VisualOverride» на меше — берём его.
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
		Child = NewObject<UChildActorComponent>(this, TEXT("BoxerVisual"));
		Child->SetupAttachment(GetMesh());
		Child->RegisterComponent();
	}
	Child->SetChildActorClass(VisualOverrideClass);
	// Базовый манекен только ведёт позу (ретаргет берёт её с него) — сам не рисуется, иначе торчит сквозь
	// MetaHuman. Поза считается и скрытым (VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones).
	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		Sk->SetVisibility(false, false);
	}
}
