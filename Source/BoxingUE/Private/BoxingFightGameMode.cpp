#include "BoxingFightGameMode.h"

#include "BoxerCharacter.h"
#include "BoxingFightHUD.h"
#include "BoxingFightPlayerController.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* EventName(EFightEventKind K)
	{
		switch (K)
		{
		case EFightEventKind::Hit: return TEXT("Hit");
		case EFightEventKind::Blocked: return TEXT("Blocked");
		case EFightEventKind::Slipped: return TEXT("Slipped");
		case EFightEventKind::Miss: return TEXT("Miss");
		case EFightEventKind::Knockdown: return TEXT("Knockdown");
		case EFightEventKind::Gassed: return TEXT("Gassed");
		case EFightEventKind::RoundEnd: return TEXT("RoundEnd");
		case EFightEventKind::FightEnd: return TEXT("FightEnd");
		}
		return TEXT("?");
	}

	const TCHAR* PunchName(EPunchType P)
	{
		static const TCHAR* Names[] = {TEXT("Jab"), TEXT("Cross"), TEXT("HookL"), TEXT("HookR"), TEXT("UpperL"), TEXT("UpperR")};
		return Names[static_cast<int32>(P)];
	}

	// Отладка: скорость, которую видит AnimBP GASP (CharacterProperties.Velocity из BPI_SandboxCharacter_Pawn).
	// −1 — AnimBP не GASP или поле не найдено.
	float AbpSeenSpeed(const ABoxerCharacter* B)
	{
		const USkeletalMeshComponent* Sk = B ? B->GetMesh() : nullptr;
		const UAnimInstance* Anim = Sk ? Sk->GetAnimInstance() : nullptr;
		if (!Anim)
		{
			return -1.f;
		}
		const FStructProperty* Props = CastField<FStructProperty>(Anim->GetClass()->FindPropertyByName(TEXT("CharacterProperties")));
		if (!Props)
		{
			return -1.f;
		}
		const void* PropsPtr = Props->ContainerPtrToValuePtr<void>(Anim);
		for (TFieldIterator<FStructProperty> It(Props->Struct); It; ++It)
		{
			if (It->GetName().StartsWith(TEXT("Velocity")) && It->Struct == TBaseStructure<FVector>::Get())
			{
				return static_cast<float>(It->ContainerPtrToValuePtr<FVector>(PropsPtr)->Size2D());
			}
		}
		return -1.f;
	}

	const TCHAR* MethodText(EFightMethod M)
	{
		switch (M)
		{
		case EFightMethod::Decision: return TEXT("решением судей");
		case EFightMethod::KO: return TEXT("нокаутом");
		case EFightMethod::RSC: return TEXT("остановкой боя (RSC)");
		case EFightMethod::Draw: return TEXT("ничья");
		default: return TEXT("");
		}
	}

	const TCHAR* DecisionText(EDecisionKind D)
	{
		switch (D)
		{
		case EDecisionKind::Unanimous: return TEXT("единогласно");
		case EDecisionKind::Majority: return TEXT("большинством");
		case EDecisionKind::Split: return TEXT("раздельным");
		case EDecisionKind::TieBreak: return TEXT("по доп. показателям");
		case EDecisionKind::DrawUnanimous: return TEXT("единогласная");
		case EDecisionKind::DrawMajority: return TEXT("большинством");
		case EDecisionKind::DrawSplit: return TEXT("раздельная");
		default: return TEXT("");
		}
	}
}

ABoxingFightGameMode::ABoxingFightGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	PlayerControllerClass = ABoxingFightPlayerController::StaticClass();
	HUDClass = ABoxingFightHUD::StaticClass();
	DefaultPawnClass = nullptr; // игрок не вселяется в бойца: ввод идёт в ядро через контроллер

	// Эталонная пара паритета (Docs/FIGHT_CORE_PORT.md): технарь против прессингёра.
	RedPreset.Name = TEXT("Красный угол");
	RedPreset.Power = 78; RedPreset.HandSpeed = 80; RedPreset.Footwork = 76; RedPreset.Stamina = 74;
	RedPreset.Chin = 75; RedPreset.Technique = 82; RedPreset.Defense = 79;
	RedPreset.HeightCm = 180; RedPreset.ReachCm = 185; RedPreset.WeightKg = 71; RedPreset.Style = EBoxerStyle::Technical;

	BluePreset.Name = TEXT("Синий угол");
	BluePreset.Power = 84; BluePreset.HandSpeed = 74; BluePreset.Footwork = 70; BluePreset.Stamina = 78;
	BluePreset.Chin = 77; BluePreset.Technique = 74; BluePreset.Defense = 72;
	BluePreset.HeightCm = 176; BluePreset.ReachCm = 180; BluePreset.WeightKg = 71; BluePreset.Style = EBoxerStyle::Pressure;
}

void ABoxingFightGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ReadCommandLine();
}

void ABoxingFightGameMode::ReadCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("BoxAutopilot")))
	{
		bAutopilot = true;
	}
	FParse::Value(Cmd, TEXT("BoxSeed="), Seed);
	FParse::Value(Cmd, TEXT("BoxRoundSec="), RoundSeconds);
	FParse::Value(Cmd, TEXT("BoxBreakSec="), BreakSeconds);
	FParse::Value(Cmd, TEXT("BoxQuitAfter="), QuitAfter);
	FParse::Value(Cmd, TEXT("BoxLogEvery="), LogEvery);
	bLogEvents = FParse::Param(Cmd, TEXT("BoxLogEvents"));
	FString Visual;
	if (FParse::Value(Cmd, TEXT("BoxVisual="), Visual))
	{
		VisualOverridePath = FSoftClassPath(Visual);
	}
	FString Shots;
	if (FParse::Value(Cmd, TEXT("BoxShots="), Shots, false))
	{
		TArray<FString> Parts;
		Shots.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			ShotTimes.Add(FCString::Atof(*P));
		}
	}
}

void ABoxingFightGameMode::StartPlay()
{
	Super::StartPlay();
	LocateRing();

	UClass* Cls = BoxerClass.Get();
	if (!Cls)
	{
		Cls = BoxerClassPath.TryLoadClass<ABoxerCharacter>();
	}
	if (!Cls)
	{
		UE_LOG(LogTemp, Warning, TEXT("FIGHT: класс %s не найден — нативный ABoxerCharacter без AnimBP GASP"), *BoxerClassPath.ToString());
		Cls = ABoxerCharacter::StaticClass();
	}

	UClass* VisualCls = VisualOverridePath.IsValid() ? VisualOverridePath.TryLoadClass<AActor>() : nullptr;
	const ABoxerCharacter* Cdo = Cls->GetDefaultObject<ABoxerCharacter>();
	const float HalfHeight = (Cdo && Cdo->GetCapsuleComponent()) ? Cdo->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;

	// Ядро стартует до спауна: стартовая расстановка берётся из его первого снимка.
	StartFight();

	for (int32 I = 0; I < 2; ++I)
	{
		const FFighterState& F = Snap.Fighters[I];
		FVector Loc = FightToWorld(F.X, F.Z);
		Loc.Z = RingFloor.Z + HalfHeight + 2.f;
		const FTransform Xf(FRotator(0.f, F.YawDegUE, 0.f), Loc);
		ABoxerCharacter* B = GetWorld()->SpawnActorDeferred<ABoxerCharacter>(Cls, Xf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!B)
		{
			UE_LOG(LogTemp, Error, TEXT("FIGHT: не удалось заспаунить бойца %d"), I);
			continue;
		}
		B->FighterIndex = I;
		B->Preset = I == 0 ? RedPreset : BluePreset;
		if (VisualCls)
		{
			B->VisualOverrideClass = VisualCls;
		}
		B->FinishSpawning(Xf);
		if (!B->GetController())
		{
			B->SpawnDefaultController(); // CMC симулирует ход только у управляемой пешки
		}
		B->AddTickPrerequisiteActor(this); // сначала ядро, потом бойцы
		if (UCharacterMovementComponent* Cmc = B->GetCharacterMovement())
		{
			// Ход бойца выставляется в тике GameMode (ApplyFightState) — CMC должен считать после него.
			Cmc->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		}
		B->SnapToFightState(Loc, F.YawDegUE);
		(I == 0 ? RedBoxer : BlueBoxer) = B;
	}
	if (RedBoxer && BlueBoxer)
	{
		RedBoxer->Opponent = BlueBoxer;
		BlueBoxer->Opponent = RedBoxer;
	}
	UE_LOG(LogTemp, Log, TEXT("FIGHT: старт — класс %s, ринг (%.0f, %.0f, %.0f), сид %d, раундов %d × %.0f с, автопилот %d"),
		*Cls->GetName(), RingFloor.X, RingFloor.Y, RingFloor.Z, Seed, Rounds, RoundSeconds, bAutopilot ? 1 : 0);
}

void ABoxingFightGameMode::RestartPlayer(AController* NewPlayer)
{
	// Пешки у игрока нет: бойцами управляет ядро, ввод контроллер шлёт в ядро, камера — своя.
}

void ABoxingFightGameMode::LocateRing()
{
	FVector Center = RingCenter;
	bool bTagged = false;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(RingCenterTag))
		{
			Center = It->GetActorLocation();
			bTagged = true;
			break;
		}
	}
	RingFloor = Center;
	if (!bTagged)
	{
		// Пол ринга — первая поверхность вниз от точки на 3 м выше центра.
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(BoxingRingFloor), false);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Center + FVector(0, 0, 300), Center - FVector(0, 0, 5000), ECC_Visibility, Q))
		{
			RingFloor.Z = Hit.ImpactPoint.Z;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("FIGHT: пол ринга (%.0f, %.0f, %.0f)%s"), RingFloor.X, RingFloor.Y, RingFloor.Z,
		bTagged ? TEXT(" по тегу RingCenter") : TEXT(""));
}

FVector ABoxingFightGameMode::FightToWorld(float X, float Z) const
{
	// Ядро: метры в плоскости (X, Z) → UE: см, X_ue = X, Y_ue = Z (перестановка осей без зеркала).
	return FVector(RingFloor.X + X * 100.f, RingFloor.Y + Z * 100.f, RingFloor.Z);
}

void ABoxingFightGameMode::StartFight()
{
	FFightConfig Cfg;
	Cfg.Fighters[0] = RedPreset.ToSetup(bAutopilot);
	Cfg.Fighters[1] = BluePreset.ToSetup(true);
	Cfg.Rounds = Rounds;
	Cfg.RoundSeconds = RoundSeconds;
	Cfg.BreakSeconds = BreakSeconds;
	Cfg.bAutoProceed = true;
	Cfg.bAllowDraw = bAllowDraw;
	Cfg.Seed = static_cast<uint32>(Seed);
	Core.Init(Cfg);
	Snap = Core.GetSnapshot();
	Accum = 0.0;
	Pending.Reset();
	bHasHeldStep = false;
	bStarted = true;
}

ABoxerCharacter* ABoxingFightGameMode::GetBoxer(int32 Index) const
{
	return Index == 0 ? RedBoxer.Get() : (Index == 1 ? BlueBoxer.Get() : nullptr);
}

void ABoxingFightGameMode::QueueAction(EFightAction Action, EPunchTarget Target)
{
	if (bAutopilot)
	{
		return;
	}
	Pending.Add({Action, Target});
}

void ABoxingFightGameMode::SetHeldStep(EFightAction Action, bool bHeld)
{
	bHasHeldStep = bHeld && !bAutopilot;
	HeldStep = Action;
}

void ABoxingFightGameMode::StepCore()
{
	// Ввод — строго на границе шага: тот же сид + та же последовательность (шаг, действие) → тот же бой.
	for (const FQueued& Q : Pending)
	{
		Core.ApplyAction(0, Q.Action, Q.Target);
	}
	Pending.Reset();
	if (bHasHeldStep)
	{
		Core.ApplyAction(0, HeldStep);
	}
	Core.Tick(FixedStep);
}

void ABoxingFightGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bStarted)
	{
		return;
	}

	Accum += DeltaSeconds;
	int32 Steps = 0;
	TArray<FFightEvent> Events;
	while (Accum >= FixedStep)
	{
		Accum -= FixedStep;
		if (++Steps > 12)
		{
			Accum = 0.0; // кадр-провал (загрузка/брейкпойнт): время боя не догоняем
			break;
		}
		StepCore();
		Events.Append(Core.PollEvents());
	}
	Snap = Core.GetSnapshot();
	// События раньше состояния: реакция на попадание, а нокдаун (фронт в снимке) её перебивает.
	DispatchEvents(MoveTemp(Events));
	PushStateToBoxers(DeltaSeconds);
	DebugLog(DeltaSeconds);
}

void ABoxingFightGameMode::DispatchEvents(TArray<FFightEvent>&& Events)
{
	for (const FFightEvent& E : Events)
	{
		++EventCount[static_cast<int32>(E.Kind)];
		const bool bMajor = E.Kind == EFightEventKind::Knockdown || E.Kind == EFightEventKind::RoundEnd || E.Kind == EFightEventKind::FightEnd;
		if (bLogEvents || bMajor)
		{
			UE_LOG(LogTemp, Log, TEXT("FIGHT EV t=%.2f r%d %s att=%d def=%d %s mag=%.2f%s%s"), E.Time, E.Round, EventName(E.Kind),
				E.Attacker, E.Defender, PunchName(E.Punch), E.Magnitude, E.Target == EPunchTarget::Body ? TEXT(" body") : TEXT(""),
				E.bCounter ? TEXT(" counter") : TEXT(""));
		}
		if (E.Kind == EFightEventKind::FightEnd)
		{
			UE_LOG(LogTemp, Log, TEXT("FIGHT ИТОГ: %s"), *GetResultText());
		}
		const ABoxerCharacter* Att = GetBoxer(E.Attacker);
		const FVector AttLoc = Att ? Att->GetActorLocation() : FVector::ZeroVector;
		for (int32 I = 0; I < 2; ++I)
		{
			if (ABoxerCharacter* B = GetBoxer(I))
			{
				B->HandleFightEvent(E, AttLoc);
			}
		}
	}
}

void ABoxingFightGameMode::PushStateToBoxers(float DeltaSeconds)
{
	for (int32 I = 0; I < 2; ++I)
	{
		ABoxerCharacter* B = GetBoxer(I);
		if (!B)
		{
			continue;
		}
		const FFighterState& F = Snap.Fighters[I];
		B->ApplyFightState(Snap, Core.GetFightTime(), FightToWorld(F.X, F.Z), F.YawDegUE, DeltaSeconds);
	}
}

FString ABoxingFightGameMode::GetResultText() const
{
	if (!Core.IsOver())
	{
		return FString();
	}
	const FFightResult& R = Core.GetResult();
	FString Cards;
	for (int32 J = 0; J < 3; ++J)
	{
		Cards += FString::Printf(TEXT("%s%d-%d"), J ? TEXT("  ") : TEXT(""), R.JudgeTotals[J].Red, R.JudgeTotals[J].Blue);
	}
	if (R.WinnerIndex < 0)
	{
		return FString::Printf(TEXT("Ничья (%s). Судьи: %s"), DecisionText(R.Decision), *Cards);
	}
	const FBoxerPreset& W = R.WinnerIndex == 0 ? RedPreset : BluePreset;
	FString Text = FString::Printf(TEXT("Победа: %s — %s"), *W.Name, MethodText(R.Method));
	if (R.Method == EFightMethod::Decision)
	{
		Text += FString::Printf(TEXT(" (%s). Судьи: %s"), DecisionText(R.Decision), *Cards);
	}
	else if (R.StoppedRound > 0)
	{
		Text += FString::Printf(TEXT(", раунд %d"), R.StoppedRound);
	}
	return Text;
}

void ABoxingFightGameMode::DebugLog(float DeltaSeconds)
{
	RealTime += DeltaSeconds;

	// Расхождение визуала с ядром (см) — для отчёта.
	for (int32 I = 0; I < 2; ++I)
	{
		if (const ABoxerCharacter* B = GetBoxer(I))
		{
			const FVector D = B->GetActorLocation() - B->FightTarget;
			MaxTrackErrorCm = FMath::Max(MaxTrackErrorCm, FMath::RoundToInt(D.Size2D()));
		}
	}

	if (LogEvery > 0.f)
	{
		LogTimer -= DeltaSeconds;
		if (LogTimer <= 0.f)
		{
			LogTimer = LogEvery;
			FString Line = FString::Printf(TEXT("FIGHT t=%.1f core=%.2f r%d phase=%d left=%.1f dist=%.2f"), RealTime,
				Core.GetFightTime(), Snap.Round, static_cast<int32>(Snap.Phase), Snap.TimeLeft, Snap.Distance);
			for (int32 I = 0; I < 2; ++I)
			{
				const ABoxerCharacter* B = GetBoxer(I);
				const FFighterState& F = Snap.Fighters[I];
				if (!B)
				{
					continue;
				}
				const FVector L = B->GetActorLocation();
				const UCharacterMovementComponent* Cmc = B->GetCharacterMovement();
				const float Speed = Cmc ? Cmc->Velocity.Size2D() : 0.f;
				const float Acc = Cmc ? Cmc->GetCurrentAcceleration().Size2D() : 0.f;
				Line += FString::Printf(TEXT(" | %s core(%.2f,%.2f) act(%.0f,%.0f,%.0f) yaw=%.0f err=%.1f v=%.0f a=%.0f abpV=%.0f hp=%.0f st=%.0f %s%s%s mont=%d"),
					I == 0 ? TEXT("R") : TEXT("B"), F.X, F.Z, L.X, L.Y, L.Z, B->GetActorRotation().Yaw,
					(L - B->FightTarget).Size2D(), Speed, Acc, AbpSeenSpeed(B), F.Health, F.StaminaPct,
					F.bPunching ? PunchName(F.Punch) : TEXT("-"), F.bBlocking ? TEXT(" blk") : TEXT(""), F.bDown ? TEXT(" DOWN") : TEXT(""),
					static_cast<int32>(B->ActiveMontageSlot));
			}
			UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
		}
	}

	for (int32 S = ShotTimes.Num() - 1; S >= 0; --S)
	{
		if (RealTime >= ShotTimes[S])
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") /
				FString::Printf(TEXT("fight_%02d.png"), FMath::RoundToInt(ShotTimes[S])));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogTemp, Log, TEXT("FIGHT: скриншот %s"), *Path);
			ShotTimes.RemoveAt(S);
		}
	}

	if (QuitAfter > 0.f && RealTime >= QuitAfter)
	{
		UE_LOG(LogTemp, Log, TEXT("FIGHT СВОДКА: t=%.1f core=%.2f раунд %d, событий: hit %d, block %d, slip %d, miss %d, kd %d; макс. расхождение визуала с ядром %d см; итог: %s"),
			RealTime, Core.GetFightTime(), Snap.Round, EventCount[0], EventCount[1], EventCount[2], EventCount[3], EventCount[4],
			MaxTrackErrorCm, Core.IsOver() ? *GetResultText() : TEXT("бой идёт"));
		QuitAfter = -1.f;
		FPlatformMisc::RequestExit(false, TEXT("BoxQuitAfter"));
	}
}
