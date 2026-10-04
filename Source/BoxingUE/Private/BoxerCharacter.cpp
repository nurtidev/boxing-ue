#include "BoxerCharacter.h"
#include "BoxerLook.h"
#include "BoxingFightGameMode.h"
#include "FightReferee.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"

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
#include "UnrealClient.h" // S-70: -BoxFootShots

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
	// S-62: обход гарда — радиус перчатки защищающегося и кулака бьющего (см при росте 178; × масштаб облика).
	constexpr float GUARD_GLOVE_R = 9.f;
	constexpr float GUARD_FIST_R = 7.f;

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
	// S-62: длинный кадр (скриншот, подгрузка — 0.5–1 с) проматывал падение ЗА конец клипа, монтаж доигрывал и выходил,
	// и лежащий на счёте вставал в стойку. Пока лежит / в финале — вернуть на кадр удержания.
	if ((ActiveMontageSlot == EBoxMontageSlot::Knockdown && bKnockedDown) || ActiveMontageSlot == EBoxMontageSlot::Finale)
	{
		const FAnimMontageInstance* Inst = ActiveMontage ? Anim->GetActiveInstanceForMontage(ActiveMontage) : nullptr;
		if (ActiveMontage && (!Inst || Inst->IsStopped()))
		{
			UAnimMontage* M = ActiveMontage;
			const EBoxMontageSlot Slot = ActiveMontageSlot;
			const float Hold = FMath::Max(0.f, M->GetPlayLength() - M->GetDefaultBlendOutTime() - HOLD_MARGIN);
			if (PlaySlotMontage(Slot, M, 1.f, Hold))
			{
				Anim->Montage_Pause(M);
			}
		}
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

FBoxerSitFrame ABoxerCharacter::GetSitFrame() const
{
	// S-71: кадр посадки для видимого меша (BoxerSit.h); вне перерыва W = 0 — поза не трогается.
	FBoxerSitFrame F;
	const float K = FMath::Clamp(Sit.W, 0.f, 1.f);
	F.W = K * K * (3.f - 2.f * K);
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	F.Fwd = Fwd;
	F.Left = FVector(Fwd.Y, -Fwd.X, 0.f);
	// Кисти — на канаты своего угла (IK сидя).
	if (F.W > 1e-3f)
	{
		if (const ABoxingFightGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABoxingFightGameMode>() : nullptr)
		{
			BoxerSit::RopeHands(GM->GetRingFloorCenter(), FighterIndex, GetActorLocation(), F.Left, F.HandTarget);
			F.bHands = true;
		}
	}
	return F;
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
	Sit.Update(BoxerSit::WantsSit(Snapshot, FighterIndex), DeltaSeconds); // S-71: сесть на стул в углу (перерыв)
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
	bClinchHold = bFighting && Snapshot.bClinch && Snapshot.ClinchBreakIn > 0.f; // S-76/S-75: сцепка (до «Брейк!»)
	SlipAmount = bFighting ? F.Slip : 0.f;
	SlipPhase = bFighting ? F.SlipPhase : 0.f;
	SlipSide = SlipAmount > 0.f ? 1 : (SlipAmount < 0.f ? -1 : 0);
	if (SlipSide == 0 && SlipPhase > 0.f && SlipPhase < 1.f)
	{
		SlipSide = PrevSlipSide; // края синуса (sin 0 = 0) — сторона прежняя
	}
	// S-58: остановлен на ногах (RSC по итогам раунда, отказ/сдался — досрочка без нокдауна в этом кадре) не падает
	// (web S-32 stoppageView): ядро помечает лежащим любого проигравшего досрочкой, а рефери разводит руками и объявляет.
	bStoppedStanding = F.bKO && !bWasDown && (bStoppedStanding || GetWorld()->GetTimeSeconds() - LastKdEventAt > 0.5);
	bKnockedDown = F.bDown && !bStoppedStanding;
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

	UpdateFall(Snapshot, DeltaSeconds); // S-62: падение внутри канатов (визуальный доворот/сдвиг)

	// Ход — здесь, в тике GameMode: CharacterMovement тикает ПОСЛЕ него (пререквизит в GameMode),
	// поэтому скорость/ввод этого кадра он и применит, а AnimBP (тикает после CMC) их увидит.
	TrackFightTarget(DeltaSeconds);
}

void ABoxerCharacter::HandleFightEvent(const FFightEvent& Event, const FVector& AttackerLocation)
{
	// S-75: сторона атакующего — мой удар ушёл в нырок («провалился» вперёд по инерции); контра прошла — окно закрыто.
	if (Event.Attacker == FighterIndex && Event.Defender != FighterIndex)
	{
		if (Event.Kind == EFightEventKind::Slipped)
		{
			KickReaction(EBoxFeelEvent::Whiff, BoxingBP::Punch(Event.Punch), BoxingBP::Target(Event.Target), 0.f, false);
		}
		else if (Event.Kind == EFightEventKind::Hit && Event.bCounter)
		{
			CounterOpenUntil = -1.0;
		}
	}
	if (Event.Defender == FighterIndex)
	{
		if (Event.Kind == EFightEventKind::Slipped)
		{
			CounterOpenUntil = GetWorld()->GetTimeSeconds() + 0.62; // COUNTER_WINDOW ядра
		}
		else if (Event.Kind == EFightEventKind::Blocked)
		{
			GuardFx.OnBlocked(Event.Magnitude);
		}
		else if (Event.Kind == EFightEventKind::Hit && Event.bGuardBreak)
		{
			GuardFx.OnGuardBreak();
		}
	}
	if (Event.Defender != FighterIndex)
	{
		return;
	}
	if (Event.Kind == EFightEventKind::Knockdown)
	{
		LastKdEventAt = GetWorld()->GetTimeSeconds(); // S-58: досрочка нокдауном (3-й / нокаут) — падает
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
	bCounterPunch = GetWorld()->GetTimeSeconds() < CounterOpenUntil; // S-75: удар в контр-окне после уклона
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
	FaceOnHit(Punch, Target, Magnitude, false); // S-74
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
	FaceOnHit(Punch, EBoxPunchTarget::Head, Magnitude, true); // S-74
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
	FaceOnKnockdown(); // S-74
	// Досрочка в этом же кадре (не встанет) — падение-нокаут, иначе — нокдаун.
	UAnimMontage* M = (bKO && KnockoutMontage) ? KnockoutMontage.Get() : KnockdownMontage.Get();
	StartFall(); // S-62: где ляжет тело — внутри канатов, мимо соперника и рефери
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
	UpdateFace(DeltaSeconds); // S-74: мимика и повреждения лица
	UpdateFootProbe(DeltaSeconds);
	// S-60: облик бойца (рост, телосложение, кожа, волосы) — как только появился видимый child actor.
	if (!bLookApplied && VisualChild && VisualChild->GetChildActor())
	{
		bLookApplied = true;
		FBoxerLook Look;
		if (UBoxerLookLibrary::FindLook(Preset.Id, Preset.Name, Look))
		{
			const bool bHeadgear = VisualOverrideClass && VisualOverrideClass->GetName().EndsWith(TEXT("_Amateur_C"));
			UBoxerLookLibrary::ApplyBoxerLook(VisualChild->GetChildActor(), VisualChild, Look, bHeadgear);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("LOOK: нет записи облика для %s (%s) — по умолчанию"), *Preset.Name, *Preset.Id);
		}
	}

	// S-78 (блокер QA): ходьба постановки (в угол, в нейтральный угол, выход) — GASP не в стрейфе, а «по ходу»: в стрейфе
	// Motion Matching держал таз в прежнем курсе (лицом к сопернику), а капсула и верх (монтаж стойки в пространстве меша)
	// разворачивались по ходу — корпус скручивался до 180°, ноги шли назад. Курс капсулы по ходу задаёт ядро.
	{
		static const bool bStageStrafe = FParse::Param(FCommandLine::Get(), TEXT("BoxStageStrafe")); // A/B: как было
		const bool bWalkStage = bStaging && FightTargetVelocity.Size2D() > 25.f;
		const bool bWantStrafe = bStageStrafe || !bWalkStage;
		if (bWantStrafe != bGaspWantsToStrafe)
		{
			bGaspWantsToStrafe = bWantStrafe;
			GaspStateTimer = 0.f;
		}
	}
	// GASP может перезаписать входное состояние (смена контроллера и т.п.) — подтверждаем раз в 0.5 с.
	GaspStateTimer -= DeltaSeconds;
	if (GaspStateTimer <= 0.f)
	{
		GaspStateTimer = 0.5f;
		PushGaspInputState();
	}
}

void ABoxerCharacter::UpdateFootProbe(float DeltaSeconds)
{
	// S-70 (отладка): -BoxFootShots=T0,N,ШАГ — серия из N снимков раз в ШАГ с, с T0 с реального времени
	// → Docs/screens/<-BoxShotPrefix, иначе feel5_feet>_tNNNNN.png (мс от T0). Снимает красный.
	static const TArray<float> FootShots = [] {
		TArray<float> V;
		FString S;
		if (FParse::Value(FCommandLine::Get(), TEXT("BoxFootShots="), S, false))
		{
			TArray<FString> Parts;
			S.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts) V.Add(FCString::Atof(*P));
		}
		return V;
	}();
	if (FighterIndex == 0 && FootShots.Num() >= 3 && FootProbe.ShotsTaken < static_cast<int32>(FootShots[1]))
	{
		const float Now = GetWorld()->GetRealTimeSeconds();
		const float At = FootShots[0] + FootProbe.ShotsTaken * FootShots[2];
		if (Now >= At)
		{
			static const FString Prefix = [] { FString V = TEXT("feel5_feet"); FParse::Value(FCommandLine::Get(), TEXT("BoxShotPrefix="), V); return V; }();
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") /
				FString::Printf(TEXT("%s_t%05d.png"), *Prefix, FMath::RoundToInt((At - FootShots[0]) * 1000.f)));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			++FootProbe.ShotsTaken;
		}
	}
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("BoxFootLog"));
	const USkeletalMeshComponent* M = bOn ? GetFeelMesh() : nullptr;
	if (!M || DeltaSeconds <= 0.004f || bKnockedDown || bReplayDriven || Sit.W > 0.01f)
	{
		FootProbe.bPrev = false;
		return;
	}
	// S-70: подушечки — из самой оценки позы (анимпоток, тот же трансформ компонента): кость меша на игровом потоке —
	// поза прошлой оценки × трансформ компонента ЭТОГО кадра, и опора «ехала» бы со скоростью корпуса. Новая оценка —
	// по счётчику; её шаг — шаг анимации (хит-стоп ≈ 0 — пропуск).
	const FBoxerFeelDebug Dbg = GetFeelDebug();
	FVector P[2];
	if (Dbg.bBallW)
	{
		if (Dbg.EvalSeq == FootProbe.LastSeq)
		{
			return;
		}
		FootProbe.LastSeq = Dbg.EvalSeq;
		DeltaSeconds = Dbg.EvalDt;
		if (DeltaSeconds <= 0.004f)
		{
			FootProbe.bPrev = false;
			return;
		}
		P[0] = Dbg.BallW[0];
		P[1] = Dbg.BallW[1];
	}
	else
	{
		static const FName Balls[2] = {TEXT("ball_l"), TEXT("ball_r")};
		static const FName Feet[2] = {TEXT("foot_l"), TEXT("foot_r")};
		for (int32 I = 0; I < 2; ++I)
		{
			const FName B = M->GetBoneIndex(Balls[I]) != INDEX_NONE ? Balls[I] : Feet[I];
			if (M->GetBoneIndex(B) == INDEX_NONE)
			{
				return;
			}
			P[I] = M->GetBoneLocation(B);
		}
	}
	for (int32 I = 0; I < 2; ++I)
	{
		FootProbe.MinZ = FMath::Min(FootProbe.MinZ, static_cast<float>(P[I].Z));
	}
	// Переносы планировщика и шаги ядра (на ногах, вне повтора).
	FootProbe.ProbeT += DeltaSeconds;
	if (Dbg.bFeet)
	{
		if (FootProbe.Swings0 < 0)
		{
			FootProbe.Swings0 = Dbg.FootSwings;
			FootProbe.Drags0 = Dbg.FootDrags;
		}
		FootProbe.Swings = Dbg.FootSwings - FootProbe.Swings0;
		FootProbe.Drags = Dbg.FootDrags - FootProbe.Drags0;
		FootProbe.MaxHipDrop = FMath::Max(FootProbe.MaxHipDrop, Dbg.HipDropCm);
		FMemory::Memcpy(FootProbe.Why, Dbg.FootWhy, sizeof(FootProbe.Why));
		FMemory::Memcpy(FootProbe.Ctx, Dbg.FootCtx, sizeof(FootProbe.Ctx));
	}
	static const float Verbose = [] { float V = 0.f; FParse::Value(FCommandLine::Get(), TEXT("BoxFootVerbose="), V); return V; }();
	if (Verbose > 0.f && Dbg.bFeet)
	{
		FootProbe.VerboseT -= DeltaSeconds;
		if (FootProbe.VerboseT <= 0.f)
		{
			FootProbe.VerboseT = Verbose;
			UE_LOG(LogTemp, Log, TEXT("FEETV [%d] t=%.2f w=%.2f U %.2f/%.2f heel %.2f/%.2f stretch %.3f/%.3f err %.1f/%.1f см lunge %.1f/%.1f см drop %.1f v=%.0f punch=%d step=%d"),
				FighterIndex, GetWorld()->GetTimeSeconds(), Dbg.FeetW, Dbg.FootU[0], Dbg.FootU[1], Dbg.FootHeel[0], Dbg.FootHeel[1], Dbg.FootStretch[0], Dbg.FootStretch[1],
				Dbg.FootErrCm[0], Dbg.FootErrCm[1], Dbg.FootLungeCm[0], Dbg.FootLungeCm[1], Dbg.HipDropCm, GetVelocity().Size2D(), bPunching ? 1 : 0, static_cast<int32>(StepKind));
			UE_LOG(LogTemp, Log, TEXT("FEETV [%d] колени L/R: после IK %.0f/%.0f°, клип %.0f/%.0f°, чашечка %.0f/%.0f° (−999 — прямая); ход вперёд %.0f вбок %.0f см/с"),
				FighterIndex, Dbg.KneeDeg[0], Dbg.KneeDeg[1], Dbg.ClipKneeDeg[0], Dbg.ClipKneeDeg[1], Dbg.KneecapDeg[0], Dbg.KneecapDeg[1],
				FVector::DotProduct(GetVelocity(), GetActorForwardVector()), FVector::DotProduct(GetVelocity(), GetActorRightVector()));
			UE_LOG(LogTemp, Log, TEXT("FEETV [%d] стойка: передняя %s; L (%.1f, %.1f) см %.0f°, R (%.1f, %.1f) см %.0f° (вперёд, вправо)"), FighterIndex,
				Dbg.LeadSide == 0 ? TEXT("левая") : TEXT("правая"), Dbg.StanceCm[0].X, Dbg.StanceCm[0].Y, Dbg.StanceYawDeg[0], Dbg.StanceCm[1].X, Dbg.StanceCm[1].Y, Dbg.StanceYawDeg[1]);
		}
	}
	// S-74: колени и локоть (выворот) — по виду хода корпуса.
	if (Dbg.bFeet && Dbg.FeetW > 0.98f)
	{
		const FVector Vb = GetVelocity();
		const float Fv = static_cast<float>(FVector::DotProduct(Vb, GetActorForwardVector()));
		const float Rv = static_cast<float>(FVector::DotProduct(Vb, GetActorRightVector()));
		const int32 Mk = Vb.Size2D() < 35.f ? 0 : (FMath::Abs(Rv) > FMath::Abs(Fv) ? 3 : (Fv > 0.f ? 1 : 2));
		for (int32 I = 0; I < 2; ++I)
		{
			if (Dbg.KneeDeg[I] > KNEE_DEG_NONE) { FootProbe.Knee[Mk].Add(Dbg.KneeDeg[I]); }
			if (Dbg.ClipKneeDeg[I] > KNEE_DEG_NONE) { FootProbe.ClipKnee[Mk].Add(Dbg.ClipKneeDeg[I]); }
			if (Dbg.KneecapDeg[I] > KNEE_DEG_NONE) { FootProbe.Kneecap.Add(Dbg.KneecapDeg[I]); }
		}
	}
	if (Dbg.ElbowDeg > KNEE_DEG_NONE)
	{
		FootProbe.Elbow[FMath::Clamp(Dbg.ElbowKind, 0, 2)].Add(Dbg.ElbowDeg);
		FootProbe.ElbowUpFrames += Dbg.ElbowUp > 0.7f ? 1 : 0;
		FootProbe.ElbowInFrames += Dbg.ElbowOut < -0.7f ? 1 : 0;
	}
	if (Dbg.ElbowJumpDeg > KNEE_DEG_NONE)
	{
		FootProbe.ElbowJump.Add(Dbg.ElbowJumpDeg);
	}
	const uint8 StepNow = static_cast<uint8>(StepKind);
	FootProbe.EngineSteps += (StepNow != 0 && FootProbe.PrevStep == 0) ? 1 : 0;
	FootProbe.PrevStep = StepNow;
	// Вид хода: скорость актора в осях бойца — вбок сильнее, чем вперёд/назад, и быстрее 35 см/с — «боковой шаг».
	const FVector V = GetVelocity();
	const float Lat = FMath::Abs(FVector::DotProduct(V, GetActorRightVector()));
	const float Fwd = FMath::Abs(FVector::DotProduct(V, GetActorForwardVector()));
	const bool bMove = V.Size2D() > 35.f;
	const int32 K = (bMove && Lat > Fwd) ? 1 : 0;
	if (bMove)
	{
		FootProbe.MoveT[K] += DeltaSeconds;
	}
	if (FootProbe.bPrev)
	{
		for (int32 I = 0; I < 2; ++I)
		{
			// Опора: подушечка у настила (до 3 см над «полом» подушечек) — классификация в конце боя (LogFootProbe):
			// пол — 2-й перцентиль высоты подушечек, а не минимум (один кадр с носком под настилом сдвигал минимум, и
			// опорой не считалось почти ничего).
			FFootProbe::FSample Smp;
			Smp.ZHi = static_cast<float>(FMath::Max(P[I].Z, FootProbe.Prev[I].Z));
			Smp.ZLo = static_cast<float>(FMath::Min(P[I].Z, FootProbe.Prev[I].Z));
			Smp.D = static_cast<float>(FVector::Dist2D(P[I], FootProbe.Prev[I]));
			Smp.Dt = DeltaSeconds;
			Smp.K = static_cast<uint8>(K);
			FootProbe.Samples.Add(Smp);
			// S-70: опора по планировщику — ступня не в переносе (и не волоком) в этой и прошлой оценке.
			if (Dbg.bFeet && Dbg.FeetW > 0.98f && Dbg.FootU[I] < 0.f && FootProbe.PrevU[I] < 0.f)
			{
				const double D = FVector::Dist2D(P[I], FootProbe.Prev[I]);
				FootProbe.PlanSum[K] += D;
				FootProbe.PlanT[K] += DeltaSeconds;
				FootProbe.PlanSpeeds[K].Add(static_cast<float>(D / DeltaSeconds));
			}
		}
	}
	FootProbe.PrevU[0] = Dbg.bFeet ? Dbg.FootU[0] : 0.f;
	FootProbe.PrevU[1] = Dbg.bFeet ? Dbg.FootU[1] : 0.f;
	FootProbe.Prev[0] = P[0];
	FootProbe.Prev[1] = P[1];
	FootProbe.bPrev = true;
}

void ABoxerCharacter::LogFootProbe()
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("BoxFootLog"));
	if (!bOn)
	{
		return;
	}
	const TCHAR* Names[2] = {TEXT("прочее"), TEXT("боковой ход")};
	// Геометрическая опора: подушечка у настила (до 3 см над «полом» подушечек — 2-й перцентиль их высоты за бой).
	float Floor = 0.f;
	{
		FFootProbe& F = FootProbe;
		TArray<float> Lo;
		Lo.Reserve(F.Samples.Num());
		for (const FFootProbe::FSample& S : F.Samples)
		{
			Lo.Add(S.ZLo);
		}
		Lo.Sort();
		Floor = Lo.Num() ? Lo[Lo.Num() / 50] : 0.f;
		for (const FFootProbe::FSample& S : F.Samples)
		{
			if (S.ZHi < Floor + 3.f && S.Dt > 0.f)
			{
				F.SlideSum[S.K] += S.D;
				F.PlantT[S.K] += S.Dt;
				++F.Frames[S.K];
				F.Fast[S.K] += S.D / S.Dt > 20.f ? 1 : 0;
				F.Speeds[S.K].Add(S.D / S.Dt);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("FEET ПОЛ [%d]: подушечки — мин. %.1f, пол (p2) %.1f, медиана %.1f см (мир Z)"), FighterIndex,
			Lo.Num() ? Lo[0] : 0.f, Floor, Lo.Num() ? Lo[Lo.Num() / 2] : 0.f);
	}
	for (int32 K = 0; K < 2; ++K)
	{
		FFootProbe& F = FootProbe;
		F.Speeds[K].Sort();
		const int32 N = F.Speeds[K].Num();
		const float Med = N ? F.Speeds[K][N / 2] : 0.f;
		const float P25 = N ? F.Speeds[K][N / 4] : 0.f;
		const float P90 = N ? F.Speeds[K][FMath::Min(N - 1, N * 9 / 10)] : 0.f;
		UE_LOG(LogTemp, Log, TEXT("FEET СВОДКА [%d] %s %s: %s — хода %.1f с, опоры %.1f с, скольжение в опоре ср. %.1f см/с, медиана %.1f, p25 %.1f, p90 %.1f, кадров > 20 см/с %.1f%%"),
			FighterIndex, *Preset.Name, IsSouthpaw() ? TEXT("левша") : TEXT("правша"), Names[K], F.MoveT[K], F.PlantT[K],
			F.PlantT[K] > 0 ? F.SlideSum[K] / F.PlantT[K] : 0.0, Med, P25, P90, F.Frames[K] ? 100.0 * F.Fast[K] / F.Frames[K] : 0.0);
		// S-70: опора по планировщику (ступня не в переносе и не волоком).
		F.PlanSpeeds[K].Sort();
		const int32 Np = F.PlanSpeeds[K].Num();
		if (Np > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("FEET ПЛАН [%d] %s: %s — опоры %.1f с, скольжение ср. %.1f см/с, медиана %.1f, p90 %.1f"),
				FighterIndex, IsSouthpaw() ? TEXT("левша") : TEXT("правша"), Names[K], F.PlanT[K], F.PlanT[K] > 0 ? F.PlanSum[K] / F.PlanT[K] : 0.0,
				F.PlanSpeeds[K][Np / 2], F.PlanSpeeds[K][FMath::Min(Np - 1, Np * 9 / 10)]);
		}
	}
	FFootProbe& F = FootProbe;
	UE_LOG(LogTemp, Log, TEXT("FEET ШАГИ [%d] %s: замер %.1f с, переносов %d (%.2f/с, волоком %d), шагов ядра %d (%.2f/с) — %.2f переноса на шаг ядра, таз опускался до %.1f см"),
		FighterIndex, IsSouthpaw() ? TEXT("левша") : TEXT("правша"), F.ProbeT, F.Swings, F.ProbeT > 0 ? F.Swings / F.ProbeT : 0.0, F.Drags,
		F.EngineSteps, F.ProbeT > 0 ? F.EngineSteps / F.ProbeT : 0.0, F.EngineSteps > 0 ? static_cast<double>(F.Swings) / F.EngineSteps : 0.0, F.MaxHipDrop);
	{
		// S-74: колени — угол сгиба от курса ступни (0 — колено над носком; > 90° — колено назад/внутрь, «вывернуто»).
		const TCHAR* Mv[4] = {TEXT("стоит"), TEXT("вперёд"), TEXT("назад"), TEXT("вбок")};
		auto Line = [](const FBoxerJointStat& J)
		{
			return FString::Printf(TEXT("кадров %d, ср. %.1f°, макс. %.1f°, > 45° %.2f%%, > 90° %.2f%%, переворотов %d"), J.Frames,
				J.Frames ? J.SumDeg / J.Frames : 0.0, J.MaxDeg, J.Frames ? 100.0 * J.Over45 / J.Frames : 0.0, J.Frames ? 100.0 * J.Over90 / J.Frames : 0.0, J.Flips);
		};
		for (int32 K = 0; K < 4; ++K)
		{
			UE_LOG(LogTemp, Log, TEXT("FEET КОЛЕНИ [%d] %s: после IK — %s | клип до IK — %s"), FighterIndex, Mv[K], *Line(F.Knee[K]), *Line(F.ClipKnee[K]));
		}
		UE_LOG(LogTemp, Log, TEXT("FEET ЧАШЕЧКА [%d]: от плоскости сгиба — %s"), FighterIndex, *Line(F.Kneecap));
		const TCHAR* Pk[3] = {TEXT("прямой"), TEXT("хук"), TEXT("апперкот")};
		for (int32 K = 0; K < 3; ++K)
		{
			UE_LOG(LogTemp, Log, TEXT("FEET ЛОКОТЬ [%d] %s: наведение от сгиба клипа — %s"), FighterIndex, Pk[K], *Line(F.Elbow[K]));
		}
		UE_LOG(LogTemp, Log, TEXT("FEET ЛОКОТЬ [%d]: локоть вверх %d кадров, внутрь %d кадров; скачок сгиба за кадр — %s"), FighterIndex, F.ElbowUpFrames, F.ElbowInFrames, *Line(F.ElbowJump));
	}
	UE_LOG(LogTemp, Log, TEXT("FEET ПРИЧИНЫ [%d]: нога на пределе %d, подтяг %d, порог %d, разворот ступни %d, шаг ядра %d, ходьба %d | в ударе %d, выпад %d, шаг ядра %d, разворот на месте %d, ход корпуса %d (всё — с начала боя)"),
		FighterIndex, F.Why[0], F.Why[1], F.Why[2], F.Why[3], F.Why[4], F.Why[5], F.Ctx[0], F.Ctx[1], F.Ctx[2], F.Ctx[3], F.Ctx[4]);
}

void ABoxerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	LogFootProbe();
	Super::EndPlay(Reason);
}

void ABoxerCharacter::TrackFightTarget(float DeltaSeconds)
{
	UCharacterMovementComponent* Cmc = GetCharacterMovement();
	if (!bHasTarget || !Cmc || DeltaSeconds <= 0.f)
	{
		return;
	}
	const FVector Pos = GetActorLocation();
	// S-62: лежащий — с визуальным сдвигом/доворотом падения (тело внутри канатов); ядро о них не знает.
	const FVector Goal = FightTarget + FVector(FallOffsetNow.X, FallOffsetNow.Y, 0.f);
	const float GoalYaw = FightYaw + FallTurnNow;
	FVector Err = Goal - Pos;
	Err.Z = 0.f;
	const float Dist = Err.Size();
	if (Dist > TeleportDistance)
	{
		SnapToFightState(Goal, GoalYaw);
		return;
	}

	// Курс — всегда лицом к сопернику (ядро). Контроллер — туда же: в стрейфе GASP крутит
	// капсулу к ControlRotation (bUseControllerDesiredRotation), так CMC не спорит с ядром.
	// На постановке (S-53) курс меняется рывками (развернулся к углу / дошёл — к сопернику) — доворот с
	// постоянной скоростью STAGE_TURN_DPS; в бою — как раньше, сразу.
	const FRotator Facing = bStaging
		? FMath::RInterpConstantTo(FRotator(0.f, GetActorRotation().Yaw, 0.f), FRotator(0.f, GoalYaw, 0.f), DeltaSeconds, STAGE_TURN_DPS)
		: FRotator(0.f, GoalYaw, 0.f);
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
	// S-75: удар в блок — клип AM_BlockHit откидывал корпус, как от попадания; корпус от него — вполовину (отдачу в перчатки
	// и «сжаться» дают FBoxerGuardState::Push и реакция Block), руки — плотный блок (ApplyGuard).
	P.TorsoAlpha = ActiveMontageSlot == EBoxMontageSlot::Hit ? HitMontageTorsoAlpha
		: (ActiveMontageSlot == EBoxMontageSlot::BlockHit && bFeel ? 0.5f : 1.f);
	P.ArmsAlpha = 1.f;
	P.bFx = bFeel && !VisualAnim;
	P.bMirror = IsSouthpaw();
	P.MirrorAxis = static_cast<uint8>(SouthpawMirrorAxis);
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
	bGuardUVValid = false;
	AimKind = Target == EBoxPunchTarget::Body ? 1 : (Opp->bBlocking ? 2 : 0);
	AimRadius = AimKind == 1 ? BodyRadiusCm : (AimKind == 2 ? GuardRadiusCm : HeadRadiusCm);
	// Передняя рука — левая у правши, правая у левши (S-62: поза левши зеркалится целиком). S-75: до выбора цели — в блок
	// бьём в перчатку со стороны бьющей руки (AimCenterNow).
	bAimLeftArm = (BoxingBP::ArmOf(Punch) == EBoxPunchArm::Lead) != IsSouthpaw();
	FVector Center;
	if (!AimCenterNow(Center))
	{
		return false;
	}
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
	case 2:
	{
		// Перчатки перед лицом. S-75: плотный блок держит перчатки по бокам лица — середина между ними и есть лицо; бьём в
		// перчатку со стороны бьющей руки (моя левая приходит в его правую), чуть к середине.
		const FVector Mid = (HandL + HandR) * 0.5f;
		const FVector Side = GetActorRightVector() * (bAimLeftArm ? -1.f : 1.f);
		const FVector G = FVector::DotProduct(HandL - Mid, Side) > 0.f ? HandL : HandR;
		OutCenter = FMath::Lerp(Mid, G, 0.8f) + OF * 6.f;
		break;
	}
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
	FBoxReactKick K = BoxerFeel::ReactionKick(Kind, P, bRear, Target == EBoxPunchTarget::Body, Magnitude, bSlipped);
	// S-75: «провалился» (Whiff) — это мой удар: зеркалит МОЯ стойка.
	if (Kind == EBoxFeelEvent::Whiff ? IsSouthpaw() : (Opponent && Opponent->IsSouthpaw()))
	{
		// S-62: бьёт левша — его передняя/задняя руки с другой стороны: отдача вбок/поворот — зеркально.
		for (const EBoxReactChannel C : {EBoxReactChannel::HeadYaw, EBoxReactChannel::HeadRoll, EBoxReactChannel::TorsoRoll, EBoxReactChannel::TorsoYaw, EBoxReactChannel::Side})
		{
			K[C] = -K[C];
		}
	}
	React.Kick(K);
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
	// S-75: защита читается — плотный блок / удар в блок / пробит (FBoxerGuardState), нырок поверх клипа. -BoxDefFx=0 — выкл. (A/B).
	{
		static const bool bDefFx = [] { int32 V = 1; FParse::Value(FCommandLine::Get(), TEXT("BoxDefFx="), V); return V != 0; }();
		if (bDefFx && !bKnockedDown && !bWasFinale)
		{
			GuardFx.Update(DeltaSeconds, bBlocking && !bPunching, GuardIntegrity);
		}
		else
		{
			GuardFx.Reset();
		}
		Feel.GuardW = GuardFx.W;
		Feel.GuardSag = GuardFx.Sag;
		Feel.GuardPushCm = GuardFx.Push;
		Feel.GuardBreakW = GuardFx.BreakW();
		Feel.Slip = bDefFx ? SlipAmount : 0.f;
		// S-76/S-75: клинч — руки на его плечо/спину по его видимому мешу (кадр назад — не важно, пара стоит).
		const bool bClinchPose = bDefFx && bClinchHold && !bKnockedDown && Opponent;
		if (DeltaSeconds > 0.f)
		{
			ClinchW = bClinchPose ? FMath::Min(1.f, ClinchW + DeltaSeconds / 0.2f) : FMath::Max(0.f, ClinchW - DeltaSeconds / 0.25f);
		}
		Feel.ClinchW = ClinchW;
		const USkeletalMeshComponent* OM = Opponent ? Opponent->GetFeelMesh() : nullptr;
		if (ClinchW > 0.f && OM && OM->GetBoneIndex(TEXT("upperarm_r")) != INDEX_NONE && OM->GetBoneIndex(TEXT("upperarm_l")) != INDEX_NONE)
		{
			const FVector OF = Opponent->GetActorForwardVector(); // смотрит на меня
			const float OS = Opponent->VisualScale();
			Feel.ClinchOver = OM->GetBoneLocation(TEXT("upperarm_r")) - OF * (12.f * OS) + FVector::UpVector * (4.f * OS);
			Feel.ClinchUnder = OM->GetBoneLocation(TEXT("upperarm_l")) - OF * (10.f * OS) - FVector::UpVector * (16.f * OS);
		}
		else
		{
			Feel.ClinchW = 0.f;
		}
	}
	// S-70: ступни (FBoxerFootIk на видимом меше). Лёжа/вставая/сидя — IK гаснет (ноги клипа / позы стула).
	{
		static const bool bFootLock = [] { int32 V = 1; FParse::Value(FCommandLine::Get(), TEXT("BoxFootLock="), V); return V != 0; }();
		const bool bFloorPose = bKnockedDown || ActiveMontageSlot == EBoxMontageSlot::Knockdown || ActiveMontageSlot == EBoxMontageSlot::GetUp;
		Feel.bFeetOn = bFootLock && !bFloorPose && !bFeetIkOff && !Sit.bWant && Sit.W <= 0.01f;
		Feel.bFeetWalking = bStaging && FightTargetVelocity.Size2D() > 5.f;
		Feel.FeetStep = static_cast<int32>(StepKind);
		Feel.bFeetPunch = bPunching;
		Feel.FeetPunchPhase = PunchPhaseAnim;
		Feel.bFeetRearArm = CurrentPunch == EBoxPunchType::Cross || CurrentPunch == EBoxPunchType::HookR || CurrentPunch == EBoxPunchType::UpperR;
		Feel.FeetPunchKind = CurrentPunch == EBoxPunchType::Jab ? 0 : CurrentPunch == EBoxPunchType::Cross ? 1
			: (CurrentPunch == EBoxPunchType::HookL || CurrentPunch == EBoxPunchType::HookR) ? 2 : 3;
		Feel.FeetScale = VisualScale();
		Feel.bFeetSouthpaw = IsSouthpaw();
		// Стойка учится по спокойным кадрам: стоит, не бьёт, не ныряет, без реакции и без клипа удара в блок/реакции.
		Feel.bFeetCalm = Feel.bFeetOn && !bPunching && SlipSide == 0 && !React.IsActive() && !Feel.bFeetWalking &&
			GetVelocity().Size2D() < 15.f && ActiveMontageSlot != EBoxMontageSlot::Hit && ActiveMontageSlot != EBoxMontageSlot::BlockHit &&
			ActiveMontageSlot != EBoxMontageSlot::Finale;
	}
	Feel.FistReachCm = FistReachCm;
	// S-78: тела не проходят друг в друга — голова/корпус соперника из его прошлого кадра анимпотока (с упреждением на кадр):
	// головы раздвигаются поровну, перчатки упираются в поверхность. Лёжа/сидя — плавно гаснет; клинч — руки на нём (без упора).
	{
		static const bool bNoBodyStop = FParse::Param(FCommandLine::Get(), TEXT("BoxNoBodyStop")); // A/B
		const bool bBodyOn = !bNoBodyStop && Opponent && !bKnockedDown && !Opponent->bKnockedDown && !Sit.bWant && Sit.W <= 0.01f;
		BodyTrack.Update(Opponent ? Opponent->GetFeelDebug() : FBoxerFeelDebug(), bBodyOn, DeltaSeconds, Feel);
		Feel.HeadR = HeadRadiusCm;
		Feel.HeadUpCm = HeadCenterUpCm;
		Feel.OppHeadR = Opponent ? Opponent->HeadRadiusCm : HeadRadiusCm;
		Feel.OppBodyR = Opponent ? Opponent->BodyRadiusCm : BodyRadiusCm;
		Feel.HandPushW *= 1.f - ClinchW;
	}
	// S-62: подшаг — в осях своего меша (× свой масштаб облика); против более высокого — длиннее (голова выше и дальше:
	// раздвижка VisMinSep идёт по среднему росту), иначе низкий бил 198-см «в перчатку».
	const float HeightRatio = (Opponent && Preset.HeightCm > 0.f && Opponent->Preset.HeightCm > 0.f) ? Opponent->Preset.HeightCm / Preset.HeightCm : 1.f;
	// Квадрат отношения: голова высокого и выше, и дальше (ядро засчитывает прямые с 1.3–1.4 м и у мухача против тяжа).
	// Подшаг в осях меша (× масштаб облика): ÷ масштаб — в мире не короче MaxLungeCm у маленьких (дистанция ядра в метрах
	// одна на всех: мухачи с 1.3–1.5 м не дотягивались).
	Feel.MaxLungeCm = MaxLungeCm / FMath::Max(0.8f, VisualScale()) * FMath::Clamp(HeightRatio * HeightRatio, 1.f, 1.9f);
	Feel.bAim = false;
	Feel.AimWeight = 0.f;
	Feel.ReachWeight = 0.f;
	// S-62: удар соперника в голову/корпус мимо блока — моя перчатка на его пути отводится (не проходит сквозь кулак).
	// В блоке (или он и бьёт в перчатки) — не трогаем: там перчатки и есть цель.
	Feel.bThreat = false;
	Feel.ThreatW = 0.f;
	static const bool bNoGuardPush = FParse::Param(FCommandLine::Get(), TEXT("BoxNoGuardPush")); // A/B
	if (!bNoGuardPush && Opponent && Opponent->bPunching && Opponent->bAimCaptured && Opponent->AimKind != 2 && !bBlocking && !bKnockedDown)
	{
		const FBoxerFeelDebug OD = Opponent->GetFeelDebug();
		if (OD.AimW > 0.f && !OD.Elbow.IsNearlyZero())
		{
			Feel.bThreat = true;
			Feel.ThreatA = OD.Elbow;
			// Путь — до точки касания цели (кулак придёт туда в контакте), а не до фронта кулака прошлого кадра анимпотока.
			Feel.ThreatB = Opponent->Feel.bAim ? Opponent->Feel.AimSurface : OD.FistFront;
			Feel.ThreatClear = GUARD_GLOVE_R * VisualScale() + GUARD_FIST_R * Opponent->VisualScale() + 3.f;
			Feel.ThreatW = FMath::Clamp(OD.AimW * 1.3f, 0.f, 1.f);
		}
	}
	if (bAimCaptured && bPunching && !bKnockedDown && Opponent && ActiveMontageSlot == EBoxMontageSlot::Punch)
	{
		float Aim = 0.f, Reach = 0.f;
		BoxerFeel::PunchEnvelopes(PunchPhase, PunchContactFraction, Aim, Reach, bCounterPunch); // S-75: контра «быстрее»
		// S-75: соперник поднял/опустил блок уже после старта удара (ИИ закрывается по замаху) — до контакта цель меняется:
		// в блок — в перчатки (ядро засчитает Blocked), блок опущен — снова голова. Удар в корпус не трогаем.
		if (AimKind != 1 && PunchPhase < PunchContactFraction && (AimKind == 2) != Opponent->bBlocking)
		{
			AimKind = Opponent->bBlocking ? 2 : 0;
			AimRadius = AimKind == 2 ? GuardRadiusCm : HeadRadiusCm;
			bGuardUVValid = false;
		}
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
		// S-62: в голову — мимо перчаток защиты (засчитанное попадание не приходит в перчатку и не проходит сквозь неё).
		// Соперник в блоке на старте удара — цель и так перчатки (AimKind 2).
		static const bool bNoGuardAim = FParse::Param(FCommandLine::Get(), TEXT("BoxNoGuardAim")); // A/B
		const FBoxerFeelDebug ORaw = Opponent->GetFeelDebug();
		const USkeletalMeshComponent* MyMesh = GetFeelMesh();
		const FName ShoulderBone(bAimLeftArm ? TEXT("upperarm_l") : TEXT("upperarm_r"));
		if (!bNoGuardAim && AimKind == 0 && ORaw.bRaw && MyMesh && MyMesh->GetBoneIndex(ShoulderBone) != INDEX_NONE)
		{
			const FVector OF = Opponent->GetActorForwardVector();
			const float OS = Opponent->VisualScale();
			// Центр перчатки — перед костью кисти (кулак в перчатке), радиусы — с масштабом облика.
			const FVector Gloves[2] = {ORaw.RawHandL + OF * (6.f * OS), ORaw.RawHandR + OF * (6.f * OS)};
			const float Clear = GUARD_GLOVE_R * OS + GUARD_FIST_R * VisualScale();
			BoxerFeel::FGuardAim G;
			const bool bStraight = Feel.PunchKind == 0;
			BoxerFeel::AimAroundGuard(Center, AimRadius, MyMesh->GetBoneLocation(ShoulderBone), AimApproach, bStraight, 35.f * VisualScale(),
				Gloves, Clear, bGuardUVValid ? &GuardUV : nullptr, G);
			GuardUV = G.UV;
			bGuardUVValid = true;
			Feel.AimApproach = G.Approach;
			Feel.AimSurface = G.Surface;
		}
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
	Loc.X = FightTarget.X + FallOffsetNow.X;
	Loc.Y = FightTarget.Y + FallOffsetNow.Y;
	SetActorLocationAndRotation(Loc, FRotator(0.f, FightYaw + FallTurnNow, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Cmc = GetCharacterMovement())
	{
		Cmc->StopMovementImmediately();
	}
}

// ---------------------------------------------------------------------------------------------
// Падение внутри канатов (S-62): раскладка тела из клипа + визуальный доворот/сдвиг (BoxerFall.h)
// ---------------------------------------------------------------------------------------------

float ABoxerCharacter::VisualScale() const
{
	return VisualChild ? static_cast<float>(VisualChild->GetRelativeScale3D().X) : 1.f;
}

const ABoxerCharacter::FFallClip* ABoxerCharacter::FallClipFor(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return nullptr;
	}
	if (const FFallClip* Found = FallClips.Find(Montage))
	{
		return Found->bValid ? Found : nullptr;
	}
	FFallClip& Clip = FallClips.Add(Montage);
	USkeletalMeshComponent* Sk = GetMesh();
	USkeletalMesh* Asset = Sk ? Sk->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || Montage->SlotAnimTracks.Num() == 0)
	{
		return nullptr;
	}
	// Поза клипа по кадрам — сэмпл сегмента трека монтажа на скелете логического меша (как анимпоток).
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	TArray<FBoneIndexType> Req;
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		Req.Add(static_cast<FBoneIndexType>(I));
	}
	FBoneContainer Bones(Req, UE::Anim::FCurveFilterSettings(), *Asset);
	static const TCHAR* Names[] = {TEXT("head"), TEXT("pelvis"), TEXT("hand_l"), TEXT("hand_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"),
		TEXT("foot_l"), TEXT("foot_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("spine_05")};
	TArray<FCompactPoseBoneIndex> Idx;
	for (const TCHAR* N : Names)
	{
		const int32 Mi = Ref.FindBoneIndex(FName(N));
		Idx.Add(Mi == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mi)));
	}
	if (Idx[0].GetInt() == INDEX_NONE || Idx[1].GetInt() == INDEX_NONE)
	{
		return nullptr;
	}
	const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	const float Hold = FMath::Max(0.05f, Montage->GetPlayLength() - Montage->GetDefaultBlendOutTime() - HOLD_MARGIN);
	for (float T = 0.f; T <= Hold + 1e-3f; T += 0.05f)
	{
		const float Tt = FMath::Min(T, Hold);
		const FAnimSegment* Seg = Track.GetSegmentAtTime(Tt);
		const UAnimSequenceBase* Seq = Seg ? Seg->GetAnimReference().Get() : nullptr;
		if (!Seq)
		{
			continue;
		}
		FCompactPose Pose;
		Pose.SetBoneContainer(&Bones);
		FBlendedCurve Curve;
		Curve.InitFrom(Bones);
		UE::Anim::FStackAttributeContainer Attr;
		FAnimationPoseData Data(Pose, Curve, Attr);
		Seq->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Seg->ConvertTrackPosToAnimPos(Tt))));
		FCSPose<FCompactPose> CSP;
		CSP.InitPose(Pose);
		for (int32 K = 0; K < Idx.Num(); ++K)
		{
			if (Idx[K].GetInt() != INDEX_NONE)
			{
				Clip.Pts.Add(CSP.GetComponentSpaceTransform(Idx[K]).GetLocation());
			}
		}
		const FVector Pelvis = CSP.GetComponentSpaceTransform(Idx[1]).GetLocation();
		if (Clip.FloorTime < 0.f && Pelvis.Z < 40.f)
		{
			Clip.FloorTime = Tt;
		}
		Clip.Head = CSP.GetComponentSpaceTransform(Idx[0]).GetLocation();
		Clip.Pelvis = Pelvis;
		// S-66: итоговая поза целиком — стопы (середина) и конечности (кисти 2/3, колени 8/9).
		Clip.bHasFeet = Idx[6].GetInt() != INDEX_NONE && Idx[7].GetInt() != INDEX_NONE;
		if (Clip.bHasFeet)
		{
			Clip.Feet = 0.5f * (CSP.GetComponentSpaceTransform(Idx[6]).GetLocation() + CSP.GetComponentSpaceTransform(Idx[7]).GetLocation());
		}
		Clip.Limbs.Reset();
		for (const int32 K : {2, 3, 8, 9})
		{
			if (Idx[K].GetInt() != INDEX_NONE)
			{
				Clip.Limbs.Add(CSP.GetComponentSpaceTransform(Idx[K]).GetLocation());
			}
		}
	}
	Clip.bValid = Clip.Pts.Num() > 0;
	UE_LOG(LogTemp, Log, TEXT("BOXER %s [%d]: падение %s — %.2f с, таз на настиле с %.2f с, итог: голова (%.0f, %.0f, %.0f), таз (%.0f, %.0f, %.0f) [комп.]"),
		*GetName(), FighterIndex, *Montage->GetName(), Hold, Clip.FloorTime, Clip.Head.X, Clip.Head.Y, Clip.Head.Z, Clip.Pelvis.X, Clip.Pelvis.Y, Clip.Pelvis.Z);
	return Clip.bValid ? &Clip : nullptr;
}

void ABoxerCharacter::StartFall()
{
	bFallLogged = false;
	FallT = 0.f;
	FallMaxOutCm = -1e6f;
	FallMontage = (bKO && KnockoutMontage) ? KnockoutMontage.Get() : KnockdownMontage.Get();
	const FFallClip* Clip = FallClipFor(FallMontage);
	if (!Clip || !GetMesh())
	{
		bFallActive = false;
		FallLayout = BoxerFall::FLayout();
		FallSolve = BoxerFall::FPlaceOut();
		return;
	}
	// Компонентное пространство логического меша → оси бойца (видимый MetaHuman — child с масштабом облика).
	const FTransform MeshRel = GetMesh()->GetRelativeTransform();
	const float S = VisualScale();
	const float Side = IsSouthpaw() ? -1.f : 1.f; // левша: поза зеркальна — раскладка тела тоже (вправо ↔ влево)
	auto Local = [&MeshRel, S, Side](const FVector& P)
	{
		const FVector A = MeshRel.TransformPosition(P * S);
		return FVector2D(A.X, A.Y * Side);
	};
	FallLayout = BoxerFall::FLayout();
	for (const FVector& P : Clip->Pts)
	{
		FallLayout.Pts.Add(Local(P));
	}
	FallLayout.Head = Local(Clip->Head);
	FallLayout.Pelvis = Local(Clip->Pelvis);
	FallLayout.bHasFeet = Clip->bHasFeet;
	FallLayout.Feet = Local(Clip->Feet);
	for (const FVector& P : Clip->Limbs)
	{
		FallLayout.Limbs.Add(Local(P));
	}
	FallLayout.bValid = true;
	bFallActive = true;
	// Прежний сдвиг (подъём ещё не отпустил) не обнуляем рывком: новое решение набирается с нуля только если
	// старого не было.
	if (FallW <= 0.f)
	{
		FallSolve = BoxerFall::FPlaceOut();
	}
}

void ABoxerCharacter::SolveFall(const FFightSnapshot& Snapshot)
{
	const ABoxingFightGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	if (!GM || !FallLayout.bValid)
	{
		return;
	}
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("BoxFallFree")); // отладка: падать как в клипе (A/B)
	if (bOff)
	{
		FallSolve = BoxerFall::FPlaceOut();
		return;
	}
	const FVector RC = GM->GetRingFloorCenter();
	auto Rel = [&RC](const FVector& P) { return FVector2D(P.X - RC.X, P.Y - RC.Y); };
	BoxerFall::FPlaceIn In;
	In.Pos = Rel(FightTarget);
	In.YawDeg = FightYaw;
	In.Layout = &FallLayout;
	// Соперник (и его путь в нейтральный угол), рефери — тело не ложится на них.
	if (Opponent)
	{
		const FVector2D O = Rel(Opponent->GetActorLocation());
		BoxerFall::FAvoid A{O, O, 55.f};
		const int32 Oi = 1 - FighterIndex;
		if (Snapshot.Stage.Kind == ERingStageKind::Neutral && Snapshot.Stage.bHasTarget[Oi])
		{
			A.B = Rel(GM->FightToWorld(Snapshot.Stage.TargetX[Oi], Snapshot.Stage.TargetZ[Oi]));
			A.R = 50.f;
		}
		In.Avoid.Add(A);
	}
	if (const ABoxingReferee* Ref = ABoxingReferee::Find(GetWorld()))
	{
		const FVector2D P = Rel(Ref->GetActorLocation());
		In.Avoid.Add({P, P, 85.f}); // рефери ~20 см в радиусе + запас: падающая рука не задевает его ног
	}
	FallSolve = BoxerFall::Solve(In);
}

void ABoxerCharacter::UpdateFall(const FFightSnapshot& Snapshot, float DeltaSeconds)
{
	if (bKnockedDown && bFallActive)
	{
		FallT += DeltaSeconds;
		// Первые доли секунды решение уточняется (стоящий получает цель в нейтральном углу, рефери смещается),
		// пока сдвиг почти не набран; дальше — заморожено.
		if (FallT < 0.3f)
		{
			SolveFall(Snapshot);
		}
		// Большой сдвиг — набирается дольше (тело не «уезжает» по настилу рывком).
		const float BlendS = FallBlendSeconds * (1.f + 0.4f * static_cast<float>(FallSolve.Offset.Size()) / 100.f);
		FallW = FMath::Max(FallW, BoxerFall::Blend(FallT, BlendS));
		static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("BoxFallLog"));
		if (bLog)
		{
			// Факт: насколько кости видимого тела выходят за канаты (305 см от центра) за всё падение.
			const ABoxingFightGameMode* GM = GetWorld()->GetAuthGameMode<ABoxingFightGameMode>();
			const USkeletalMeshComponent* M = GetFeelMesh();
			if (GM && M)
			{
				const FVector RC = GM->GetRingFloorCenter();
				static const TCHAR* Bones[] = {TEXT("head"), TEXT("pelvis"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r")};
				for (const TCHAR* B : Bones)
				{
					if (M->GetBoneIndex(FName(B)) != INDEX_NONE)
					{
						const FVector P = M->GetBoneLocation(FName(B)) - RC;
						FallMaxOutCm = FMath::Max3(FallMaxOutCm, static_cast<float>(FMath::Abs(P.X)) - 305.f, static_cast<float>(FMath::Abs(P.Y)) - 305.f);
					}
				}
			}
			if (FallT >= 2.5f && FallT - DeltaSeconds < 2.5f)
			{
				UE_LOG(LogTemp, Log, TEXT("FALL [%d]: факт — кости тела за канатами макс. %.0f см (< 0 — внутри), лёг %d"), FighterIndex, FallMaxOutCm, IsFloored() ? 1 : 0);
			}
		}
		if (bLog && !bFallLogged && FallT >= 0.3f)
		{
			bFallLogged = true;
			const ABoxingFightGameMode* GM = GetWorld()->GetAuthGameMode<ABoxingFightGameMode>();
			const FVector RC = GM ? GM->GetRingFloorCenter() : FVector::ZeroVector;
			const FVector2D Pos(FightTarget.X - RC.X, FightTarget.Y - RC.Y);
			UE_LOG(LogTemp, Log, TEXT("FALL [%d]: точка (%.0f, %.0f) курс %.0f, масштаб %.2f; за канатами было %.0f см → доворот %.0f°, сдвиг (%.0f, %.0f), внутри %d"),
				FighterIndex, Pos.X, Pos.Y, FightYaw, VisualScale(), BoxerFall::Overhang(FallLayout, Pos, FightYaw, BoxerFall::ROPE_LIMIT_CM),
				FallSolve.TurnDeg, FallSolve.Offset.X, FallSolve.Offset.Y, FallSolve.bInside ? 1 : 0);
		}
	}
	else if (ActiveMontageSlot == EBoxMontageSlot::GetUp && FallW > 0.f)
	{
		// Встаёт — клип подъёма начинается с лежачей позы: сдвиг держим до конца подъёма.
	}
	else if (FallW > 0.f)
	{
		FallW = FMath::Max(0.f, FallW - DeltaSeconds / FMath::Max(0.05f, FallReleaseSeconds));
		if (FallW <= 0.f)
		{
			bFallActive = false;
			FallSolve = BoxerFall::FPlaceOut();
		}
	}
	FallOffsetNow = FallSolve.Offset * FallW;
	FallTurnNow = FallSolve.TurnDeg * FallW;
}

bool ABoxerCharacter::IsFloored() const
{
	if (!bKnockedDown)
	{
		return false;
	}
	const FFallClip* Clip = FallMontage ? FallClips.Find(FallMontage) : nullptr;
	if (!Clip || !Clip->bValid || Clip->FloorTime <= 0.f)
	{
		return true;
	}
	const UAnimInstance* Anim = ActiveAnim.IsValid() ? ActiveAnim.Get() : nullptr;
	if (Anim && ActiveMontage == FallMontage && ActiveMontageSlot == EBoxMontageSlot::Knockdown)
	{
		return Anim->Montage_GetPosition(ActiveMontage) >= Clip->FloorTime;
	}
	return FallT >= Clip->FloorTime + 0.3f; // монтажа нет — по времени
}

bool ABoxerCharacter::GetLyingBody(FVector& OutHead, FVector& OutPelvis) const
{
	if (!bKnockedDown || !FallLayout.bValid || !bFallActive)
	{
		return false;
	}
	const FVector2D Pos(FightTarget.X + FallSolve.Offset.X, FightTarget.Y + FallSolve.Offset.Y);
	const float Yaw = FightYaw + FallSolve.TurnDeg;
	const FVector2D H = BoxerFall::ToWorld(FallLayout.Head, Pos, Yaw);
	const FVector2D P = BoxerFall::ToWorld(FallLayout.Pelvis, Pos, Yaw);
	OutHead = FVector(H.X, H.Y, FightTarget.Z);
	OutPelvis = FVector(P.X, P.Y, FightTarget.Z);
	return true;
}

bool ABoxerCharacter::GetLyingLayout(FVector& OutHead, FVector& OutPelvis, FVector& OutFeet, TArray<FVector>& OutLimbs) const
{
	OutLimbs.Reset();
	if (!GetLyingBody(OutHead, OutPelvis))
	{
		return false;
	}
	const FVector2D Pos(FightTarget.X + FallSolve.Offset.X, FightTarget.Y + FallSolve.Offset.Y);
	const float Yaw = FightYaw + FallSolve.TurnDeg;
	// Стоп в клипе нет — точка бойца (ноги остаются примерно там, где он стоял).
	const FVector2D F = FallLayout.bHasFeet ? BoxerFall::ToWorld(FallLayout.Feet, Pos, Yaw) : Pos;
	OutFeet = FVector(F.X, F.Y, FightTarget.Z);
	for (const FVector2D& L : FallLayout.Limbs)
	{
		const FVector2D W = BoxerFall::ToWorld(L, Pos, Yaw);
		OutLimbs.Add(FVector(W.X, W.Y, FightTarget.Z));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Левша (S-62): зеркальная стойка — визуал
// ---------------------------------------------------------------------------------------------

bool ABoxerCharacter::IsSouthpaw() const
{
	// -BoxSouthpaw=0 — как раньше (все правши); =red / =blue / =both — принудительно (отладка); иначе — стойка из ростера.
	static const FString Mode = [] { FString V; FParse::Value(FCommandLine::Get(), TEXT("BoxSouthpaw="), V); return V.ToLower(); }();
	if (Mode == TEXT("0") || Mode == TEXT("off"))
	{
		return false;
	}
	if (Mode == TEXT("both") || (Mode == TEXT("red") && FighterIndex == 0) || (Mode == TEXT("blue") && FighterIndex == 1))
	{
		return true;
	}
	return Preset.bSouthpaw;
}
