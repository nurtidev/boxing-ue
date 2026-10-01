#include "BoxerCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
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
	// Запас до конца монтажа, на котором держим «последний кадр» (нокдаун, финал).
	constexpr float HOLD_MARGIN = 0.03f;
	// Плавный выход из удара/уклона, когда цикл ядра закончился, — назад в стойку слота UpperBody.
	constexpr float PUNCH_BLEND_OUT = 0.18f;
	// Тяжёлое попадание (порог хит-стопа веба).
	constexpr float HEAVY_MAG = 1.1f;
	// Попыток найти видимый меш подмены (child actor создаётся при регистрации — обычно сразу).
	constexpr int32 PHYS_INIT_TRIES = 30;

	const FName NAME_Contact(TEXT("Contact"));
	const FName NAME_Peak(TEXT("Peak"));
	const FName NAME_GuardUp(TEXT("GuardUp"));
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

	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		// Без рендера (-nullrhi, скрытый логический меш под ретаргетом) поза всё равно считается.
		Sk->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		// Ход задаёт ядро: аналоговый минимум скорости GASP (150 см/с) дёргал бы медленные шаги.
		Cmc->MinAnalogWalkSpeed = 0.f;
	}
	LoadDefaultMontages();
	ApplyVisualOverride();
	PushGaspInputState();
	InitPhysics();
}

void ABoxerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	PushGaspInputState();
}

// ---------------------------------------------------------------------------------------------
// Монтажи: автоподхват, нотифаи
// ---------------------------------------------------------------------------------------------

UAnimMontage* ABoxerCharacter::FindMontage(const TCHAR* Name) const
{
	const FString Asset = FString::Printf(TEXT("AM_%s"), Name);
	for (const FString& Folder : MontageFolders)
	{
		const FString Package = Folder / Asset;
		if (!FPackageName::DoesPackageExist(Package))
		{
			continue;
		}
		if (UAnimMontage* M = LoadObject<UAnimMontage>(nullptr, *(Package + TEXT(".") + Asset), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return M;
		}
	}
	return nullptr;
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
	Fill(BodyHookMontage, TEXT("BodyHook"));
	Fill(BlockMontage, TEXT("Block"));
	Fill(BlockHitMontage, TEXT("BlockHit"));
	Fill(SlipLeftMontage, TEXT("SlipL"));
	Fill(SlipRightMontage, TEXT("SlipR"));
	Fill(HitHeadMontage, TEXT("HitHead"));
	Fill(HitBodyMontage, TEXT("HitBody"));
	Fill(GuardMontage, TEXT("Guard"));
	Fill(KnockdownMontage, TEXT("Knockdown"));
	Fill(KnockoutMontage, TEXT("Knockout"));
	Fill(GetUpMontage, TEXT("GetUp"));
	Fill(VictoryMontage, TEXT("Victory"));
	Fill(DefeatMontage, TEXT("Defeat"));
	UE_LOG(LogTemp, Log, TEXT("BOXER %s [%d]: монтажей найдено %d из 19 (%s); контакт джеба %.3f с"), *GetName(), FighterIndex,
		Found, *FString::Join(MontageFolders, TEXT(", ")), GetPunchContactTime(EBoxPunchType::Jab));
}

float ABoxerCharacter::GetMontageNotifyTime(const UAnimMontage* Montage, FName NotifyName)
{
	if (!Montage)
	{
		return -1.f;
	}
	const FString Wanted = NotifyName.ToString();
	for (const FAnimNotifyEvent& N : Montage->Notifies)
	{
		// AnimNotify_PlayMontageNotify: имя — в самом нотифае (GetNotifyName), у «именных» — в событии.
		const bool bMatch = N.NotifyName == NotifyName || (N.Notify && N.Notify->GetNotifyName() == Wanted);
		if (bMatch)
		{
			return N.GetTriggerTime();
		}
	}
	return -1.f;
}

UAnimMontage* ABoxerCharacter::GetPunchMontage(EBoxPunchType Punch, EBoxPunchTarget Target) const
{
	if (Target == EBoxPunchTarget::Body && BodyHookMontage && (Punch == EBoxPunchType::HookL || Punch == EBoxPunchType::HookR))
	{
		return BodyHookMontage;
	}
	const TObjectPtr<UAnimMontage>* MPtr = PunchMontages.Find(Punch);
	return MPtr ? MPtr->Get() : nullptr;
}

float ABoxerCharacter::GetPunchContactTime(EBoxPunchType Punch) const
{
	if (const float* T = PunchContactTimes.Find(Punch))
	{
		if (*T > 0.f)
		{
			return *T;
		}
	}
	const UAnimMontage* M = GetPunchMontage(Punch, EBoxPunchTarget::Head);
	if (!M)
	{
		return 0.f;
	}
	const float T = GetMontageNotifyTime(M, NAME_Contact);
	return T > 0.f ? T : M->GetPlayLength() * 0.5f; // как в вебе: контакт клипа = середина
}

float ABoxerCharacter::BlockGuardUpTime() const
{
	if (!BlockMontage)
	{
		return 0.f;
	}
	const float T = GetMontageNotifyTime(BlockMontage, NAME_GuardUp);
	return T > 0.f ? T : BlockMontage->GetPlayLength() * 0.6f;
}

UAnimInstance* ABoxerCharacter::GetAnimInst() const
{
	const USkeletalMeshComponent* Sk = GetMesh();
	return Sk ? Sk->GetAnimInstance() : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Монтажи: проигрывание и синхронизация с ядром
// ---------------------------------------------------------------------------------------------

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

void ABoxerCharacter::HoldAt(float Time)
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim || !ActiveMontage)
	{
		return;
	}
	if (Anim->Montage_IsPlaying(ActiveMontage) && Anim->Montage_GetPosition(ActiveMontage) >= Time)
	{
		Anim->Montage_SetPosition(ActiveMontage, Time);
		Anim->Montage_Pause(ActiveMontage);
	}
}

void ABoxerCharacter::HoldAtEnd()
{
	if (ActiveMontage)
	{
		HoldAt(FMath::Max(0.f, ActiveMontage->GetPlayLength() - ActiveMontage->GetDefaultBlendOutTime() - HOLD_MARGIN));
	}
}

void ABoxerCharacter::SyncPeakMontage(float Phase, float PeakFrac, float CycleSeconds, float PeakTime)
{
	UAnimInstance* Anim = GetAnimInst();
	if (!Anim || !ActiveMontage || CycleSeconds <= 0.f)
	{
		return;
	}
	// Кусочно-линейно: [0, пик ядра] → [0, PeakTime], [пик, конец цикла] → [PeakTime, PeakTime + отход].
	const float Len = ActiveMontage->GetPlayLength();
	const float P = FMath::Clamp(PeakTime, 0.02f, Len - 0.02f);
	const float Recover = FMath::Min(Len - P, P * RecoverFactor);
	const float Pf = FMath::Clamp(PeakFrac, 0.05f, 0.95f);
	const float U = FMath::Clamp(Phase, 0.f, 1.f);
	const bool bBefore = U < Pf;
	const float Desired = bBefore ? (U / Pf) * P : P + ((U - Pf) / (1.f - Pf)) * Recover;
	const float Rate = bBefore ? P / (Pf * CycleSeconds) : Recover / ((1.f - Pf) * CycleSeconds);
	Anim->Montage_SetPlayRate(ActiveMontage, FMath::Clamp(Rate, 0.1f, 6.f));
	if (FMath::Abs(Anim->Montage_GetPosition(ActiveMontage) - Desired) > MontageSnapTolerance)
	{
		Anim->Montage_SetPosition(ActiveMontage, Desired);
	}
}

void ABoxerCharacter::StartBlockMontage(float Lead)
{
	if (BlockMontage)
	{
		PlaySlotMontage(EBoxMontageSlot::Block, BlockMontage, 1.f, FMath::Max(0.f, BlockGuardUpTime() - Lead));
	}
}

void ABoxerCharacter::UpdateMontages(float DeltaSeconds)
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
			const float Contact = PunchContactTimes.Contains(CurrentPunch) && ActiveMontage != BodyHookMontage
				? GetPunchContactTime(CurrentPunch) : GetMontageNotifyTime(ActiveMontage, NAME_Contact);
			SyncPeakMontage(PunchPhase, PunchContactFraction, PunchDuration, Contact > 0.f ? Contact : ActiveMontage->GetPlayLength() * 0.5f);
		}
		else
		{
			StopSlotMontage(PUNCH_BLEND_OUT); // цикл ядра кончился — назад в стойку
		}
		break;
	case EBoxMontageSlot::Slip:
		if (SlipSide != 0)
		{
			const float Peak = GetMontageNotifyTime(ActiveMontage, NAME_Peak);
			SyncPeakMontage(SlipPhase, 0.5f, SlipWindowSeconds, Peak > 0.f ? Peak : ActiveMontage->GetPlayLength() * 0.25f);
		}
		else
		{
			StopSlotMontage(PUNCH_BLEND_OUT);
		}
		break;
	case EBoxMontageSlot::Block:
		if (!bBlocking)
		{
			StopSlotMontage(0.15f);
		}
		else
		{
			HoldAt(BlockGuardUpTime()); // руки подняты — держим кадр «GuardUp», пока блок
		}
		break;
	case EBoxMontageSlot::BlockHit:
		if (!bBlocking)
		{
			StopSlotMontage(0.15f);
		}
		break;
	case EBoxMontageSlot::Knockdown:
		if (bKnockedDown)
		{
			HoldAtEnd();
		}
		break;
	case EBoxMontageSlot::Finale:
		HoldAtEnd();
		break;
	case EBoxMontageSlot::Guard:
		if (!bPlayGuardMontage || GetVelocity().Size2D() > GuardStopSpeed || bKnockedDown || bWasFinale)
		{
			StopSlotMontage(GuardBlendSeconds); // пошёл — ноги отдаём локомоции GASP
		}
		break;
	default:
		break;
	}

	// Блок держится, а слот освободился (после реакции/удара) — снова руки вверх (почти сразу у «GuardUp»).
	if (bBlocking && (ActiveMontageSlot == EBoxMontageSlot::None || ActiveMontageSlot == EBoxMontageSlot::Guard))
	{
		StartBlockMontage(0.05f);
	}
	// Боевая стойка: стоит (почти без скорости) дольше GuardSettleSeconds и слот свободен.
	const bool bStill = GetVelocity().Size2D() < GuardStartSpeed && FightTargetVelocity.Size2D() < GuardStartSpeed;
	GuardStill = bStill ? GuardStill + DeltaSeconds : 0.f;
	if (bPlayGuardMontage && GuardMontage && ActiveMontageSlot == EBoxMontageSlot::None && !bKnockedDown && !bWasFinale &&
		!bBlocking && GuardStill >= GuardSettleSeconds)
	{
		PlaySlotMontage(EBoxMontageSlot::Guard, GuardMontage, 1.f);
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

	// --- фронт: финал (победа / поражение на ногах) ---
	const bool bFinale = Victory >= 1.f || bDefeated;
	if (bFinale && !bWasFinale && !bKnockedDown)
	{
		UAnimMontage* M = Victory >= 1.f ? VictoryMontage.Get() : DefeatMontage.Get();
		PlaySlotMontage(EBoxMontageSlot::Finale, M, 1.f);
	}
	bWasFinale = bFinale;

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
	if (SlipSide != 0 && PrevSlipSide == 0 && !bKnockedDown && ActiveMontageSlot != EBoxMontageSlot::Finale)
	{
		UAnimMontage* M = SlipSide < 0 ? SlipLeftMontage.Get() : SlipRightMontage.Get();
		if (M && ActiveMontageSlot != EBoxMontageSlot::Knockdown)
		{
			if (PlaySlotMontage(EBoxMontageSlot::Slip, M, 1.f))
			{
				const float Peak = GetMontageNotifyTime(M, NAME_Peak);
				SyncPeakMontage(SlipPhase, 0.5f, SlipWindowSeconds, Peak > 0.f ? Peak : M->GetPlayLength() * 0.25f);
			}
		}
	}

	// --- фронт: блок ---
	if (bBlocking && !bWasBlocking &&
		(ActiveMontageSlot == EBoxMontageSlot::None || ActiveMontageSlot == EBoxMontageSlot::Hit || ActiveMontageSlot == EBoxMontageSlot::Guard))
	{
		StartBlockMontage(BlockRaiseSeconds);
	}

	UpdateMontages(DeltaSeconds);

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
	UAnimMontage* M = GetPunchMontage(Punch, Target);
	if (!M || bKnockedDown || PunchDuration <= 0.f || ActiveMontageSlot == EBoxMontageSlot::Finale)
	{
		return;
	}
	ActivePunchTarget = Target;
	if (PlaySlotMontage(EBoxMontageSlot::Punch, M, 1.f))
	{
		// Скорость/позицию сразу ставит синхронизация: кадр «Contact» монтажа = резолюция удара в ядре.
		const float Contact = (M != BodyHookMontage && PunchContactTimes.Contains(Punch)) ? GetPunchContactTime(Punch) : GetMontageNotifyTime(M, NAME_Contact);
		SyncPeakMontage(PunchPhase, PunchContactFraction, PunchDuration, Contact > 0.f ? Contact : M->GetPlayLength() * 0.5f);
	}
}

void ABoxerCharacter::OnHitReceived_Implementation(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction)
{
	OnHitReceivedDelegate.Broadcast(Punch, Target, Magnitude, Direction);
	HitReaction(Punch, Target, Magnitude, Direction, false);
	// Монтаж реакции не перебивает свой удар, уклон, нокдаун и финал.
	if (Magnitude < HitMontageMinMagnitude || bKnockedDown ||
		ActiveMontageSlot == EBoxMontageSlot::Punch || ActiveMontageSlot == EBoxMontageSlot::Slip ||
		ActiveMontageSlot == EBoxMontageSlot::Knockdown || ActiveMontageSlot == EBoxMontageSlot::GetUp ||
		ActiveMontageSlot == EBoxMontageSlot::Finale)
	{
		return;
	}
	UAnimMontage* M = Target == EBoxPunchTarget::Body ? HitBodyMontage.Get() : HitHeadMontage.Get();
	PlaySlotMontage(EBoxMontageSlot::Hit, M, HitMontageRate);
}

void ABoxerCharacter::OnBlockedPunch_Implementation(EBoxPunchType Punch, float Magnitude, FVector Direction)
{
	HitReaction(Punch, EBoxPunchTarget::Head, Magnitude, Direction, true);
	if (BlockHitMontage && !bKnockedDown &&
		(ActiveMontageSlot == EBoxMontageSlot::Block || ActiveMontageSlot == EBoxMontageSlot::BlockHit ||
		 ActiveMontageSlot == EBoxMontageSlot::None || ActiveMontageSlot == EBoxMontageSlot::Guard))
	{
		PlaySlotMontage(EBoxMontageSlot::BlockHit, BlockHitMontage, 1.f);
	}
}

void ABoxerCharacter::OnKnockdown_Implementation()
{
	OnKnockdownDelegate.Broadcast();
	// Досрочка в этом же кадре (не встанет) — падение-нокаут, иначе — нокдаун.
	UAnimMontage* M = (bKO && KnockoutMontage) ? KnockoutMontage.Get() : KnockdownMontage.Get();
	PlaySlotMontage(EBoxMontageSlot::Knockdown, M, 1.f);
}

void ABoxerCharacter::OnGetUp_Implementation()
{
	OnGetUpDelegate.Broadcast();
	if (GetUpMontage)
	{
		PlaySlotMontage(EBoxMontageSlot::GetUp, GetUpMontage, GetUpMontageRate);
	}
	else if (ActiveMontageSlot == EBoxMontageSlot::Knockdown)
	{
		StopSlotMontage(0.4f);
	}
}

// ---------------------------------------------------------------------------------------------
// Физреакция (Docs/HIT_REACTION.md, подход 1) — на видимом меше подмены
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::InitPhysics()
{
	if (bPhysReady || !bPhysicalHitReactions || !PhysicalAnimation || !VisualChild)
	{
		return;
	}
	++PhysInitTries;
	const AActor* Vis = VisualChild->GetChildActor();
	if (!Vis)
	{
		return;
	}
	// Видимое тело: компонент «Body» (MetaHuman) или первый скелетный меш с физассетом.
	USkeletalMeshComponent* Best = nullptr;
	TArray<USkeletalMeshComponent*> Meshes;
	Vis->GetComponents(Meshes);
	for (USkeletalMeshComponent* M : Meshes)
	{
		if (!M || !M->GetPhysicsAsset() || M->GetBoneIndex(PhysRootBone) == INDEX_NONE)
		{
			continue;
		}
		if (!Best || M->GetName().Equals(TEXT("Body"), ESearchCase::IgnoreCase))
		{
			Best = M;
		}
	}
	if (!Best)
	{
		if (PhysInitTries >= PHYS_INIT_TRIES)
		{
			UE_LOG(LogTemp, Warning, TEXT("BOXER %s: физреакция — у %s нет скелетного меша с физассетом, выключена"), *GetName(), *Vis->GetName());
			bPhysicalHitReactions = false;
		}
		return;
	}
	const UDataTable* Table = HitReactionTable.LoadSynchronous();

	// Тела без контактов: только пружины к анимационной позе (капсулы, ринг, соперник не мешают).
	Best->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Best->SetCollisionResponseToAllChannels(ECR_Ignore);
	if (Best->Bodies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BOXER %s: физреакция — у %s не созданы тела, выключена"), *GetName(), *Best->GetName());
		bPhysicalHitReactions = false;
		return;
	}
	PhysicalAnimation->SetSkeletalMeshComponent(Best);
	int32 Rows = 0;
	if (Table && Table->GetRowStruct() == FPhysicalAnimationData::StaticStruct())
	{
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
		{
			if (Best->GetBoneIndex(Row.Key) == INDEX_NONE)
			{
				continue;
			}
			PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Row.Key, *reinterpret_cast<const FPhysicalAnimationData*>(Row.Value), true);
			++Rows;
		}
	}
	if (Rows == 0)
	{
		// Таблицы нет — одна мягкая пружина на весь верх (значения «spine_03» из HIT_REACTION.md).
		FPhysicalAnimationData D;
		D.bIsLocalSimulation = true;
		D.OrientationStrength = 1500.f;
		D.AngularVelocityStrength = 150.f;
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(PhysRootBone, D, true);
	}
	Best->SetAllBodiesBelowSimulatePhysics(PhysRootBone, true, true);
	Best->SetAllBodiesBelowPhysicsBlendWeight(PhysRootBone, 0.f, false, true); // в покое — чистая анимация
	PhysMesh = Best;
	bPhysReady = true;
	UE_LOG(LogTemp, Log, TEXT("BOXER %s: физреакция на %s.%s (тел %d, строк пружин %d)"), *GetName(), *Vis->GetName(),
		*Best->GetName(), Best->Bodies.Num(), Rows);
}

void ABoxerCharacter::UpdatePhysics(float DeltaSeconds)
{
	if (!bPhysReady && bPhysicalHitReactions && VisualChild && PhysInitTries < PHYS_INIT_TRIES)
	{
		InitPhysics();
	}
	if (!bPhysReady || !PhysMesh || PhysBlend <= 0.f)
	{
		return;
	}
	PhysBlend *= FMath::Exp(-DeltaSeconds / FMath::Max(0.01f, PhysBlendTau));
	if (PhysBlend < 0.01f)
	{
		PhysBlend = 0.f;
	}
	PhysMesh->SetAllBodiesBelowPhysicsBlendWeight(PhysRootBone, PhysBlend, false, true);
}

void ABoxerCharacter::HitReaction_Implementation(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction, bool bBlocked)
{
	if (!bPhysicalHitReactions || !bPhysReady || !PhysMesh || Magnitude <= 0.f || bKnockedDown)
	{
		return;
	}
	// Таблица импульсов HIT_REACTION.md (см/с при mag 1, bVelChange). Side — поперёк линии удара:
	// +Side — вправо от атакующего (туда уносит левый хук), Up — вверх.
	const FVector Fwd = Direction.GetSafeNormal2D();
	const FVector Side(-Fwd.Y, Fwd.X, 0.f);
	const bool bLead = BoxingBP::ArmOf(Punch) == EBoxPunchArm::Lead;
	const float SideSign = bLead ? 1.f : -1.f;
	const bool bHook = Punch == EBoxPunchType::HookL || Punch == EBoxPunchType::HookR;
	const bool bUpper = Punch == EBoxPunchType::UpperL || Punch == EBoxPunchType::UpperR;

	FName Bone = PhysHeadBone;
	FVector Dir = Fwd;
	float Speed = 180.f;
	float Mag = Magnitude;
	if (bBlocked)
	{
		Bone = bLead ? TEXT("lowerarm_r") : TEXT("lowerarm_l"); // левый соперника приходит в правую руку
		Speed = 120.f;
		Mag *= 0.4f; // блок «съедает»
	}
	else if (Target == EBoxPunchTarget::Body)
	{
		Bone = PhysBodyBone;
		Speed = 220.f;
		Dir = bHook ? (Fwd * 0.5f + Side * (0.5f * SideSign)) : Fwd;
	}
	else if (bHook)
	{
		Speed = 380.f;
		Dir = Side * (0.7f * SideSign) + Fwd * 0.3f;
	}
	else if (bUpper)
	{
		Speed = 340.f;
		Dir = FVector::UpVector * 0.7f + Fwd * 0.3f;
	}
	else if (Punch == EBoxPunchType::Cross)
	{
		Speed = 320.f;
		Dir = Fwd + Side * (0.27f * SideSign); // ≈15° от бьющей руки
	}
	if (PhysMesh->GetBoneIndex(Bone) == INDEX_NONE)
	{
		Bone = PhysHeadBone;
	}

	const float Peak = FMath::Clamp(PhysBlendBase + PhysBlendPerMag * Mag, 0.f, Magnitude >= HEAVY_MAG && !bBlocked ? PhysBlendMax : 0.6f);
	PhysBlend = FMath::Max(PhysBlend, Peak);
	PhysMesh->SetAllBodiesBelowPhysicsBlendWeight(PhysRootBone, PhysBlend, false, true);
	PhysMesh->AddImpulse(Dir.GetSafeNormal() * Speed * Mag * PhysImpulseScale, Bone, true);
	if (Magnitude >= HEAVY_MAG && !bBlocked)
	{
		PhysMesh->AddImpulse(Fwd * Speed * Mag * 0.3f * PhysImpulseScale, PhysRootBone, true); // тяжёлый — «подсел»
	}
}

// ---------------------------------------------------------------------------------------------
// Локомоция: точка ядра → CharacterMovement (Motion Matching видит Velocity/Acceleration)
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePhysics(DeltaSeconds);

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
	VisualChild = Child;
	// Логический манекен только ведёт позу (ретаргет берёт её с него) — сам не рисуется, иначе торчит сквозь
	// MetaHuman. Поза считается и скрытым (VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones).
	if (USkeletalMeshComponent* Sk = GetMesh())
	{
		Sk->SetVisibility(false, false);
	}
}

USkeletalMeshComponent* ABoxerCharacter::GetVisibleMesh() const
{
	return PhysMesh.Get();
}
