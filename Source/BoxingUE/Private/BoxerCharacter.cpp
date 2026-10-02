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
#include "Misc/CommandLine.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/UnrealType.h"
#include "Retargeter/IKRetargeter.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	// Усиление слежения за точкой ядра (1/с): скорость = упреждение + ошибка × K.
	constexpr float TRACK_GAIN = 12.f;
	// Ниже этой ошибки (см) и без упреждения — стоим (Motion Matching видит «стойку»).
	constexpr float TRACK_DEADBAND = 0.4f;
	// Доворот курса на постановке раунда (S-53), град/с: разворот к углу ≈ 0.4 с.
	constexpr float STAGE_TURN_DPS = 420.f;
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
	// Отладка «ощущения»: -BoxReactGain=K (множитель пружин реакции), -BoxHitMontage=0 (без клипов реакции — только пружины).
	FParse::Value(FCommandLine::Get(), TEXT("BoxReactGain="), ReactionGain);
	int32 HitClips = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("BoxHitMontage="), HitClips) && HitClips == 0)
	{
		HitMontageMinMagnitude = 1e6f;
	}
	SetupFeel();
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
	// Клип bodyHook бьёт ЛЕВОЙ: правый хук в корпус — клипом hookR, наведение руки опустит его к корпусу (как в вебе).
	const bool bBodyClipArm = Punch == EBoxPunchType::HookL || (Punch == EBoxPunchType::HookR && !bFeel);
	if (Target == EBoxPunchTarget::Body && BodyHookMontage && bBodyClipArm)
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

bool ABoxerCharacter::IsUpperSlot(EBoxMontageSlot Slot)
{
	return Slot == EBoxMontageSlot::Punch || Slot == EBoxMontageSlot::Block || Slot == EBoxMontageSlot::BlockHit ||
		Slot == EBoxMontageSlot::Slip || Slot == EBoxMontageSlot::Hit;
}

UAnimInstance* ABoxerCharacter::AnimFor(EBoxMontageSlot Slot) const
{
	// Слой верха (пост-процесс логического меша) — удары/блок/уклоны/реакции; полнотелые — AnimBP GASP.
	if (UpperAnim && IsUpperSlot(Slot))
	{
		return UpperAnim;
	}
	return GetAnimInst();
}

bool ABoxerCharacter::PlaySlotMontage(EBoxMontageSlot Slot, UAnimMontage* Montage, float Rate, float StartPos)
{
	UAnimInstance* Anim = AnimFor(Slot);
	if (!Anim || !Montage)
	{
		return false;
	}
	// Действие переезжает на другой экземпляр (верх ↔ всё тело) — прежнее гасим, иначе оно доиграет поверх.
	if (ActiveMontage && ActiveAnim.IsValid() && ActiveAnim.Get() != Anim)
	{
		ActiveAnim->Montage_Stop(0.15f, ActiveMontage);
	}
	// Полнотелое (нокдаун, подъём, финал) — слой верха молчит целиком (и стойка рук тоже).
	if (UpperAnim && !IsUpperSlot(Slot))
	{
		UpperAnim->Montage_Stop(0.15f, nullptr);
	}
	const float Len = Anim->Montage_Play(Montage, FMath::Clamp(Rate, 0.1f, 6.f), EMontagePlayReturnType::MontageLength,
		FMath::Clamp(StartPos, 0.f, Montage->GetPlayLength()), true);
	if (Len <= 0.f)
	{
		return false;
	}
	ActiveMontage = Montage;
	ActiveMontageSlot = Slot;
	ActiveAnim = Anim;
	return true;
}

void ABoxerCharacter::StopSlotMontage(float BlendOut)
{
	UAnimInstance* Anim = ActiveAnim.IsValid() ? ActiveAnim.Get() : GetAnimInst();
	if (Anim && ActiveMontage)
	{
		Anim->Montage_Stop(BlendOut, ActiveMontage);
	}
	ActiveMontage = nullptr;
	ActiveMontageSlot = EBoxMontageSlot::None;
}

void ABoxerCharacter::HoldAt(float Time)
{
	UAnimInstance* Anim = ActiveAnim.IsValid() ? ActiveAnim.Get() : GetAnimInst();
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
	UAnimInstance* Anim = ActiveAnim.IsValid() ? ActiveAnim.Get() : GetAnimInst();
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
	UAnimInstance* Anim = ActiveAnim.IsValid() ? ActiveAnim.Get() : GetAnimInst();
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
	if (UpperAnim)
	{
		UpdateGuards(DeltaSeconds);
		return;
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
	if (bReplayDriven)
	{
		return; // повтор нокаута (S-54): бойца ведёт запись, ядро и монтажи не трогаем
	}
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
	// Вне боя (нокдаун/перерыв/финал) ядро не двигает бойцов, а в перерыве и время стоит — прошлая скорость не «залипает».
	const bool bFighting = Snapshot.Phase == EFightPhase::Fighting;
	if (!bFighting)
	{
		FightTargetVelocity = FVector::ZeroVector;
	}
	// Постановка раунда (S-53): выход из угла / в угол / в нейтральный угол — ходьба вне боевого времени,
	// поэтому скорость точки — из ядра (WalkVel, м/с → см/с), а курс доворачивается плавно (TrackFightTarget).
	bStaging = Snapshot.Stage.Kind != ERingStageKind::None;
	if (F.WalkSpeed > 0.f)
	{
		FightTargetVelocity = FVector(F.WalkVelX * 100.f, F.WalkVelZ * 100.f, 0.f);
	}
	FightTarget = WorldTarget;
	FightYaw = YawDeg;
	bHasTarget = true;

	// --- поля для AnimBP/HUD ---
	// Вне боя ядро держит удар/уклон замороженными (фаза упёрта в 1, сброс — лишь на следующем шаге боя) — не играем их.
	bPunching = F.bPunching && bFighting && F.PunchPhase < 1.f;
	if (bPunching)
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
	PunchPhase = bPunching ? F.PunchPhase : 0.f;
	PunchPhaseAnim = bPunching ? F.PunchPhaseAnim : 0.f;
	bBlocking = F.bBlocking;
	GuardIntegrity = F.GuardIntegrity;
	SlipAmount = bFighting ? F.Slip : 0.f;
	SlipPhase = bFighting ? F.SlipPhase : 0.f;
	SlipSide = SlipAmount > 0.f ? 1 : (SlipAmount < 0.f ? -1 : 0);
	if (SlipSide == 0 && SlipPhase > 0.f && SlipPhase < 1.f)
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

	// --- фронт: новый удар (момент контакта сменился; он постоянен весь цикл, в отличие от зажатой фазы) ---
	if (bPunching)
	{
		const double Contact = CoreTime + static_cast<double>(F.PunchTimeToContact);
		if (!bWasPunching || FMath::Abs(Contact - LastPunchContact) > 0.02)
		{
			LastPunchContact = Contact;
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
	else if (Event.Kind == EFightEventKind::Miss || Event.Kind == EFightEventKind::Slipped)
	{
		// Промах «в лоб» — голова уходит с линии (кулак наводился на прежнее место); нырок уже виден клипом.
		KickReaction(EBoxFeelEvent::Miss, BoxingBP::Punch(Event.Punch), BoxingBP::Target(Event.Target), 0.f,
			Event.Kind == EFightEventKind::Slipped);
	}
}

// ---------------------------------------------------------------------------------------------
// События (реализации по умолчанию)
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::OnPunchStarted_Implementation(EBoxPunchType Punch, EBoxPunchTarget Target)
{
	OnPunchStartedDelegate.Broadcast(Punch, Target);
	UE_LOG(LogTemp, Log, TEXT("PUNCHSTART %s [%d] t=%.2f удар=%d"), *GetName(), FighterIndex, GetWorld()->GetTimeSeconds(), (int32)Punch);
	UAnimMontage* M = GetPunchMontage(Punch, Target);
	if (!M || bKnockedDown || PunchDuration <= 0.f || ActiveMontageSlot == EBoxMontageSlot::Finale)
	{
		return;
	}
	ActivePunchTarget = Target;
	bAimCaptured = bFeel && CaptureAim(Punch, Target);
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
	KickReaction(EBoxFeelEvent::Land, Punch, Target, Magnitude, false);
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
	KickReaction(EBoxFeelEvent::Block, Punch, EBoxPunchTarget::Head, Magnitude, false);
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
	// Физреакция + ПАРАЛЛЕЛЬНАЯ оценка позы = падение: тик UPhysicalAnimationComponent читает GetBoneSpaceTransforms() меша,
	// пока задача оценки держит массив у себя (пустой → «1 into 0»; Docs/VERIFY_ON_PC.md, разд. 3). Выключить только
	// у видимого меша (CanRunParallelWork) мало — вместо падения зависание; глобально — проверено, 60+ с без сбоев.
	if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("a.ParallelAnimEvaluation")))
	{
		Cv->Set(0, ECVF_SetByCode);
	}
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
	if (bFeel && !bFeelReady && FeelInitTries < 30)
	{
		SetupFeel();
	}
	UpdateFeel(DeltaSeconds);

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
	// На постановке (S-53) курс меняется рывками (развернулся к углу / дошёл — к сопернику) — доворот с
	// постоянной скоростью STAGE_TURN_DPS; в бою — как раньше, сразу.
	const FRotator Facing = bStaging
		? FMath::RInterpConstantTo(FRotator(0.f, GetActorRotation().Yaw, 0.f), FRotator(0.f, FightYaw, 0.f), DeltaSeconds, STAGE_TURN_DPS)
		: FRotator(0.f, FightYaw, 0.f);
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

// ---------------------------------------------------------------------------------------------
// «Ощущение удара»: слой верха тела, наведение кулака, реакция (Docs/FIGHT_FEEL.md)
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::SetupFeel()
{
	if (!bFeel || bFeelReady)
	{
		return;
	}
	++FeelInitTries;
	USkeletalMeshComponent* Logic = GetMesh();
	if (!Logic)
	{
		return;
	}

	// 1. Слой верха тела — нативный пост-процесс логического меша (его собственный пост-процесс не нужен: меш скрыт).
	if (bUpperBodyLayer && !UpperAnim)
	{
		Logic->SetOverridePostProcessAnimBP(UBoxerLayerAnimInstance::StaticClass());
		UpperAnim = Cast<UBoxerLayerAnimInstance>(Logic->GetPostProcessInstance());
		if (UpperAnim)
		{
			UpperAnim->Boxer = this;
		}
		UE_LOG(LogTemp, Log, TEXT("BOXER %s [%d]: слой верха тела %s"), *GetName(), FighterIndex,
			UpperAnim ? TEXT("— пост-процесс UBoxerLayerAnimInstance") : TEXT("НЕ создан (удары на всё тело)"));
	}

	// 2. Видимый меш: AnimInstance с ретаргетом (как ABP_GenericRetarget) + процедурный слой.
	if (!VisualChild)
	{
		bFeelReady = true; // манекен виден сам: процедурный слой — в пост-процессе (GetLayerParams().bFx)
		return;
	}
	AActor* Vis = VisualChild->GetChildActor();
	if (!Vis)
	{
		if (FeelInitTries >= 30)
		{
			bFeelReady = true;
		}
		return;
	}
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
			// Ретаргетер может приходить в узел пином из переменной — ищем объектное свойство экземпляра.
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
		if (FeelInitTries >= 30)
		{
			UE_LOG(LogTemp, Warning, TEXT("BOXER %s: у %s нет меша с ретаргетом позы — процедурный слой выключен"), *GetName(), *Vis->GetName());
			bFeelReady = true;
		}
		return;
	}
	if (!Rtg)
	{
		Rtg = LoadObject<UIKRetargeter>(nullptr, TEXT("/Game/MetaHumans/Common/Common/Rigs/RTG_UEFN_to_Metahuman_nrw.RTG_UEFN_to_Metahuman_nrw"));
	}
	if (!Rtg)
	{
		UE_LOG(LogTemp, Warning, TEXT("BOXER %s: ретаргетер %s не найден — процедурный слой выключен"), *GetName(), *Best->GetName());
		bFeelReady = true;
		return;
	}
	const FString OldClass = GetNameSafe(Best->GetAnimInstance() ? Best->GetAnimInstance()->GetClass() : nullptr);
	Best->SetAnimInstanceClass(UBoxerVisualAnimInstance::StaticClass());
	VisualAnim = Cast<UBoxerVisualAnimInstance>(Best->GetAnimInstance());
	if (VisualAnim)
	{
		VisualAnim->Boxer = this;
		VisualAnim->SetupRetarget(Logic, Rtg, bHasProfile ? &Profile : nullptr);
		Best->AddTickPrerequisiteComponent(Logic); // поза источника — этого кадра
		VisualMesh = Best;
	}
	bFeelReady = true;
	UE_LOG(LogTemp, Log, TEXT("BOXER %s [%d]: видимый меш %s.%s: %s → UBoxerVisualAnimInstance (%s), ретаргетер %s"), *GetName(), FighterIndex,
		*Vis->GetName(), *Best->GetName(), *OldClass, VisualAnim ? TEXT("ok") : TEXT("НЕ создан"), *Rtg->GetName());
}

FBoxerLayerParams ABoxerCharacter::GetLayerParams() const
{
	FBoxerLayerParams P;
	P.TorsoAlpha = ActiveMontageSlot == EBoxMontageSlot::Hit ? HitMontageTorsoAlpha : 1.f;
	P.ArmsAlpha = 1.f;
	P.bFx = bFeel && !VisualAnim;
	return P;
}

USkeletalMeshComponent* ABoxerCharacter::GetFeelMesh() const
{
	if (VisualMesh)
	{
		return VisualMesh;
	}
	if (const AActor* Vis = VisualChild ? VisualChild->GetChildActor() : nullptr)
	{
		TArray<USkeletalMeshComponent*> Meshes;
		Vis->GetComponents(Meshes);
		for (USkeletalMeshComponent* M : Meshes)
		{
			if (M && M->GetName().Equals(TEXT("Body"), ESearchCase::IgnoreCase))
			{
				return M;
			}
		}
	}
	return GetMesh();
}

FBoxerFeelDebug ABoxerCharacter::GetFeelDebug() const
{
	if (VisualAnim)
	{
		return VisualAnim->PeekDebug();
	}
	return UpperAnim ? UpperAnim->PeekDebug() : FBoxerFeelDebug();
}

void ABoxerCharacter::UpdateGuards(float DeltaSeconds)
{
	UAnimInstance* Main = GetAnimInst();
	const bool bStill = GetVelocity().Size2D() < GuardStartSpeed && FightTargetVelocity.Size2D() < GuardStartSpeed;
	GuardStill = bStill ? GuardStill + DeltaSeconds : 0.f;
	const bool bMainBusy = ActiveMontageSlot == EBoxMontageSlot::Knockdown || ActiveMontageSlot == EBoxMontageSlot::GetUp ||
		ActiveMontageSlot == EBoxMontageSlot::Finale;
	const bool bAllowed = bPlayGuardMontage && GuardMontage && !bKnockedDown && !bWasFinale && !bMainBusy;
	auto Live = [this](UAnimInstance* A)
	{
		const FAnimMontageInstance* I = A ? A->GetActiveInstanceForMontage(GuardMontage) : nullptr;
		return I && !I->IsStopped();
	};
	// Всё тело (ноги в боевой стойке) — когда стоит; на ходу ноги отдаём Motion Matching.
	if (Main)
	{
		const bool bOn = Live(Main);
		if (bAllowed && !bOn && GuardStill >= GuardSettleSeconds)
		{
			Main->Montage_Play(GuardMontage, 1.f, EMontagePlayReturnType::MontageLength, 0.f, false);
		}
		else if (bOn && (!bAllowed || GetVelocity().Size2D() > GuardStopSpeed))
		{
			Main->Montage_Stop(GuardBlendSeconds, GuardMontage);
		}
	}
	// Верх (руки у подбородка) — всегда, когда слой верха свободен: и на ходу перчатки не падают.
	const bool bUpOn = Live(UpperAnim);
	if (bAllowed && ActiveMontageSlot == EBoxMontageSlot::None && !bUpOn)
	{
		UpperAnim->Montage_Play(GuardMontage, 1.f, EMontagePlayReturnType::MontageLength, 0.f, false);
	}
	else if (bUpOn && !bAllowed)
	{
		UpperAnim->Montage_Stop(GuardBlendSeconds, GuardMontage);
	}
}

bool ABoxerCharacter::CaptureAim(EBoxPunchType Punch, EBoxPunchTarget Target)
{
	ABoxerCharacter* Opp = Opponent;
	USkeletalMeshComponent* OM = Opp ? Opp->GetFeelMesh() : nullptr;
	USkeletalMeshComponent* MM = GetFeelMesh();
	if (!OM || !MM)
	{
		return false;
	}
	auto Bone = [](const USkeletalMeshComponent* M, const TCHAR* Name, FVector& Out)
	{
		const FName N(Name);
		if (M->GetBoneIndex(N) == INDEX_NONE)
		{
			return false;
		}
		Out = M->GetBoneLocation(N, EBoneSpaces::WorldSpace);
		return true;
	};
	const FVector Up = FVector::UpVector;
	// Цель: голова / корпус / перчатки (блок на старте удара). Дальше центр цели живой — по позе соперника ДО его
	// процедурного слоя (без его подшага и реакции), а на время его нырка замирает: нырок честно уводит голову
	// с линии, кулак проходит мимо (как в вебе).
	AimKind = Target == EBoxPunchTarget::Body ? 1 : (Opp->bBlocking ? 2 : 0);
	AimRadius = AimKind == 1 ? BodyRadiusCm : (AimKind == 2 ? GuardRadiusCm : HeadRadiusCm);
	FVector Center;
	if (!AimCenterNow(Center))
	{
		return false;
	}
	bAimLeftArm = BoxingBP::ArmOf(Punch) == EBoxPunchArm::Lead;
	FVector Shoulder;
	if (!Bone(MM, bAimLeftArm ? TEXT("upperarm_l") : TEXT("upperarm_r"), Shoulder))
	{
		Shoulder = GetActorLocation() + Up * 50.f;
	}
	const FVector MF = GetActorForwardVector();
	const FVector MR = GetActorRightVector();
	switch (Punch)
	{
	case EBoxPunchType::HookL:
	case EBoxPunchType::HookR:
	{
		// Хук приходит сбоку: левый идёт слева направо (в правую щёку соперника), правый — наоборот.
		const float S = bAimLeftArm ? 1.f : -1.f;
		AimApproach = (MR * (0.65f * S) + MF * 0.35f).GetSafeNormal();
		break;
	}
	case EBoxPunchType::UpperL:
	case EBoxPunchType::UpperR:
		AimApproach = (Up * 0.55f + MF * 0.45f).GetSafeNormal();
		break;
	default:
		AimApproach = (Center - Shoulder).GetSafeNormal();
		break;
	}
	AimCenterLocal = Opp->GetActorTransform().InverseTransformPosition(Center);
	return true;
}

bool ABoxerCharacter::AimCenterNow(FVector& OutCenter) const
{
	const ABoxerCharacter* Opp = Opponent;
	if (!Opp)
	{
		return false;
	}
	const FVector OF = Opp->GetActorForwardVector();
	FVector Head, Chest, HandL, HandR;
	const FBoxerFeelDebug Raw = Opp->GetFeelDebug();
	if (Raw.bRaw)
	{
		Head = Raw.RawHead;
		Chest = Raw.RawChest;
		HandL = Raw.RawHandL;
		HandR = Raw.RawHandR;
	}
	else
	{
		const USkeletalMeshComponent* OM = Opp->GetFeelMesh();
		if (!OM || OM->GetBoneIndex(TEXT("head")) == INDEX_NONE || OM->GetBoneIndex(TEXT("spine_03")) == INDEX_NONE ||
			OM->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE || OM->GetBoneIndex(TEXT("hand_r")) == INDEX_NONE)
		{
			return false;
		}
		Head = OM->GetBoneLocation(TEXT("head"));
		Chest = OM->GetBoneLocation(TEXT("spine_03"));
		HandL = OM->GetBoneLocation(TEXT("hand_l"));
		HandR = OM->GetBoneLocation(TEXT("hand_r"));
	}
	switch (AimKind)
	{
	case 1: OutCenter = Chest + OF * 2.f; break;
	case 2: OutCenter = (HandL + HandR) * 0.5f + OF * 4.f; break; // перчатки перед лицом
	default: OutCenter = Head + FVector::UpVector * HeadCenterUpCm + OF * 2.f; break;
	}
	return true;
}

void ABoxerCharacter::KickReaction(EBoxFeelEvent Kind, EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, bool bSlipped)
{
	if (!bFeel || bKnockedDown)
	{
		return;
	}
	EBoxFeelPunch P = EBoxFeelPunch::Straight;
	switch (Punch)
	{
	case EBoxPunchType::Cross: P = EBoxFeelPunch::Cross; break;
	case EBoxPunchType::HookL:
	case EBoxPunchType::HookR: P = EBoxFeelPunch::Hook; break;
	case EBoxPunchType::UpperL:
	case EBoxPunchType::UpperR: P = EBoxFeelPunch::Uppercut; break;
	default: break;
	}
	const bool bRear = BoxingBP::ArmOf(Punch) == EBoxPunchArm::Rear;
	React.Kick(BoxerFeel::ReactionKick(Kind, P, bRear, Target == EBoxPunchTarget::Body, Magnitude, bSlipped));
}

void ABoxerCharacter::UpdateFeel(float DeltaSeconds)
{
	if (!bFeel)
	{
		Feel = FBoxerFeelFrame();
		return;
	}
	if (bKnockedDown || bWasFinale)
	{
		React.Reset();
	}
	else
	{
		React.Update(DeltaSeconds);
	}
	static const bool bFeelLog = FParse::Param(FCommandLine::Get(), TEXT("BoxFeelLog"));
	if (bFeelLog && React.IsActive())
	{
		UE_LOG(LogTemp, Log, TEXT("FEEL react [%d] dt=%.3f head p/y/r %.2f %.2f %.2f torso p/r/y %.2f %.2f %.2f knee %.2f back %.3f side %.3f"), FighterIndex, DeltaSeconds,
			React.X[0], React.X[1], React.X[2], React.X[3], React.X[4], React.X[5], React.X[6], React.X[7], React.X[8]);
	}
	for (int32 C = 0; C < BOX_REACT_NUM; ++C)
	{
		Feel.React[C] = React.X[C] * ReactionGain;
	}
	static const bool bReactTest = FParse::Param(FCommandLine::Get(), TEXT("BoxReactTest"));
	if (bReactTest && FighterIndex == 0)
	{
		// Отладка: постоянная поза реакции у красного (голова назад 0.6 рад, корпус вправо 0.3 рад) — проверить оси.
		Feel.React[static_cast<int32>(EBoxReactChannel::HeadPitch)] = 0.6f;
		Feel.React[static_cast<int32>(EBoxReactChannel::TorsoRoll)] = 0.3f;
	}
	Feel.Fwd = GetActorForwardVector();
	Feel.Right = GetActorRightVector();
	Feel.FistReachCm = FistReachCm;
	Feel.MaxLungeCm = MaxLungeCm;
	Feel.bAim = false;
	Feel.AimWeight = 0.f;
	Feel.ReachWeight = 0.f;
	if (bAimCaptured && bPunching && !bKnockedDown && Opponent && ActiveMontageSlot == EBoxMontageSlot::Punch)
	{
		float Aim = 0.f, Reach = 0.f;
		BoxerFeel::PunchEnvelopes(PunchPhase, PunchContactFraction, Aim, Reach);
		FVector Live;
		if (Opponent->SlipSide == 0 && AimCenterNow(Live))
		{
			AimCenterLocal = Opponent->GetActorTransform().InverseTransformPosition(Live);
		}
		const FVector Center = Opponent->GetActorTransform().TransformPosition(AimCenterLocal);
		Feel.bAim = true;
		Feel.bLeftArm = bAimLeftArm;
		Feel.bBentArm = CurrentPunch != EBoxPunchType::Jab && CurrentPunch != EBoxPunchType::Cross;
		Feel.PunchKind = (CurrentPunch == EBoxPunchType::HookL || CurrentPunch == EBoxPunchType::HookR) ? 1 : (Feel.bBentArm ? 2 : 0);
		Feel.AimApproach = AimApproach;
		Feel.AimSurface = Center - AimApproach * AimRadius;
		Feel.AimWeight = Aim;
		Feel.ReachWeight = Reach;
		static const bool bDraw = FParse::Param(FCommandLine::Get(), TEXT("BoxFeelDraw"));
		if (bDraw)
		{
			// Отладка: красная — поверхность цели (фронт кулака должен коснуться её в контакте), жёлтая — центр.
			DrawDebugSphere(GetWorld(), Feel.AimSurface, 3.f, 8, FColor::Red, false, -1.f, SDPG_Foreground);
			DrawDebugSphere(GetWorld(), Center, AimRadius, 12, FColor::Yellow, false, -1.f, SDPG_Foreground);
			DrawDebugLine(GetWorld(), Feel.AimSurface - AimApproach * 25.f, Feel.AimSurface, FColor::Red, false, -1.f, SDPG_Foreground, 1.f);
			const FBoxerFeelDebug Last = GetFeelDebug(); // зелёная — фронт кулака (прошлый кадр анимпотока)
			if (Last.AimW > 0.f)
			{
				DrawDebugSphere(GetWorld(), Last.FistFront, 4.f, 8, FColor::Green, false, -1.f, SDPG_Foreground);
			}
		}
	}
	else if (!bPunching)
	{
		bAimCaptured = false;
	}
}

// ---------------------------------------------------------------------------------------------
// Повтор нокаута (S-54): бойца ведёт запись UBoxingFightFx
// ---------------------------------------------------------------------------------------------

void ABoxerCharacter::BeginReplayDrive()
{
	bReplayDriven = true;
	ReplayFeel = Feel;
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->StopMovementImmediately();
	}
}

void ABoxerCharacter::SetReplayFrame(const FVector& Loc, float YawDeg, const TArray<FTransform>& Bones, const FBoxerFeelFrame& InFeel)
{
	if (!bReplayDriven)
	{
		return;
	}
	ReplayBones = Bones;
	ReplayFeel = InFeel;
	SetActorLocationAndRotation(Loc, FRotator(0.f, YawDeg, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->StopMovementImmediately();
	}
}

void ABoxerCharacter::EndReplayDrive()
{
	if (!bReplayDriven)
	{
		return;
	}
	bReplayDriven = false;
	ReplayBones.Reset();
	// Вернуть живое место/курс сразу («защёлкнуть»), поза — снова из анимации (держит финальный кадр).
	FVector Loc = GetActorLocation();
	Loc.X = FightTarget.X;
	Loc.Y = FightTarget.Y;
	SetActorLocationAndRotation(Loc, FRotator(0.f, FightYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->StopMovementImmediately();
	}
}
