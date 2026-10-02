#include "BoxingFightGameMode.h"

#include "BoxerCharacter.h"
#include "BoxingFightHUD.h"
#include "BoxingFightPlayerController.h"
#include "BoxingGameInstanceSubsystem.h"
#include "FightFx.h"
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
	// S-55: пара из Выставки (меню) — пресеты/раунды/сид; без выбора остаются свои пресеты.
	if (const UBoxingGameInstanceSubsystem* Shell = UBoxingGameInstanceSubsystem::Get(this))
	{
		Shell->ApplyToFightMode(*this);
	}
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
	if (FParse::Param(Cmd, TEXT("BoxNoCorners")))
	{
		bCorners = false;
	}
	bStageShots = FParse::Param(Cmd, TEXT("BoxStageShots"));
	FParse::Value(Cmd, TEXT("BoxPreGong="), PreGongHold);
	FString Visual;
	if (FParse::Value(Cmd, TEXT("BoxVisual="), Visual))
	{
		VisualOverridePath = Visual.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FSoftClassPath() : FSoftClassPath(Visual);
	}
	auto ReadVisual = [Cmd](const TCHAR* Key, FSoftClassPath& Out)
	{
		FString V;
		if (FParse::Value(Cmd, Key, V))
		{
			// "none" — явно без подмены (манекен) — помечаем путём-заглушкой "None".
			Out = V.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FSoftClassPath(TEXT("/Script/None.None")) : FSoftClassPath(V);
		}
	};
	ReadVisual(TEXT("BoxVisualRed="), VisualOverridePathRed);
	ReadVisual(TEXT("BoxVisualBlue="), VisualOverridePathBlue);
	// Облик задан руками — авто-выбор любительской формы по типу боя не нужен.
	if (FString(Cmd).Contains(TEXT("BoxVisualRed=")) || FString(Cmd).Contains(TEXT("BoxVisualBlue=")))
	{
		bAutoAmateurLook = false;
	}
	FParse::Value(Cmd, TEXT("BoxPhysHits="), PhysHitsOverride);
	FParse::Value(Cmd, TEXT("BoxFeel="), FeelOverride);
	FParse::Value(Cmd, TEXT("BoxMinSep="), VisMinSepCm);
	if (FeelOverride == 0)
	{
		VisMinSepCm = 0.f; // A/B: «как до» — без раздвижки
	}
	FParse::Value(Cmd, TEXT("BoxHitShots="), HitShotsLeft);
	FString Delays;
	if (FParse::Value(Cmd, TEXT("BoxHitShotDelay="), Delays, false))
	{
		TArray<FString> Parts;
		Delays.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			HitShotDelays.Add(FCString::Atof(*P));
		}
	}
	FParse::Value(Cmd, TEXT("BoxShotPrefix="), ShotPrefix);
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
		const FSoftClassPath VisPath = VisualPathFor(I);
		if (UClass* VisualCls = VisPath.IsValid() ? VisPath.TryLoadClass<AActor>() : nullptr)
		{
			B->VisualOverrideClass = VisualCls;
		}
		else if (VisPath.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("FIGHT: подмена %s для бойца %d не загрузилась — виден манекен"), *VisPath.ToString(), I);
		}
		if (FeelOverride >= 0)
		{
			B->bFeel = FeelOverride != 0;
		}
		if (PhysHitsOverride >= 0)
		{
			B->bPhysicalHitReactions = PhysHitsOverride != 0;
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
	Cfg.bCorners = bCorners;
	Cfg.Seed = static_cast<uint32>(Seed);
	Core.Init(Cfg);
	Snap = Core.GetSnapshot();
	Accum = 0.0;
	Pending.Reset();
	bHasHeldStep = false;
	PreGongLeft = bCorners ? FMath::Max(0.f, PreGongHold) : 0.f;
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

	CheckFeelContacts();
	// S-54: хит-стоп/повтор нокаута держат ядро (меньше шагов за реальное время — сид и ввод по шагам те же).
	UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	// S-53: до гонга бойцы PreGongHold с стоят в своих углах (уровень догружается, видна расстановка); ядро не шагает.
	if (PreGongLeft > 0.f)
	{
		PreGongLeft -= DeltaSeconds;
		Snap = Core.GetSnapshot();
		if (Fx)
		{
			Fx->OnSnapshot(Snap, this);
		}
		PushStateToBoxers(DeltaSeconds);
		DebugStage();
		DebugLog(DeltaSeconds);
		return;
	}
	Accum += DeltaSeconds * (Fx ? Fx->CoreTimeScale() : 1.f);
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
	if (Fx)
	{
		Fx->OnSnapshot(Snap, this); // гонг, фон зала (S-54)
	}
	PushStateToBoxers(DeltaSeconds);
	DebugStage();
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
		// Скриншот в кадре контакта (событие ядра = кадр «Contact» монтажа удара).
		if (HitShotsLeft > 0 && (E.Kind == EFightEventKind::Hit || E.Kind == EFightEventKind::Blocked) && RealTime - LastHitShotAt >= 1.5f)
		{
			--HitShotsLeft;
			LastHitShotAt = RealTime;
			const FString ShotName = FString::Printf(TEXT("%s_hit%d_%s_%s"), *ShotPrefix, ++HitShotIndex, PunchName(E.Punch),
				E.Kind == EFightEventKind::Hit ? TEXT("land") : TEXT("block"));
			if (HitShotDelays.Num() > 0)
			{
				for (const float D : HitShotDelays)
				{
					DelayedShots.Add({FString::Printf(TEXT("%s_t%03d"), *ShotName, FMath::RoundToInt(D * 1000.f)), RealTime + D});
				}
			}
			else
			{
				TakeShot(ShotName);
			}
		}
		const ABoxerCharacter* Att = GetBoxer(E.Attacker);
		const FVector AttLoc = Att ? Att->GetActorLocation() : FVector::ZeroVector;
		// Метрика контакта: зазор кулака в кадре контакта — читается в СЛЕДУЮЩЕМ тике (поза этого кадра ещё не посчитана).
		if (E.Attacker >= 0 && E.Attacker < 2 && (E.Kind == EFightEventKind::Hit || E.Kind == EFightEventKind::Blocked))
		{
			PendingFeelCheck[E.Attacker] = E.Kind == EFightEventKind::Hit ? 1 : 2;
		}
		// «Ощущение» (S-54): хит-стоп/slow-mo/камера/звук — ДО раздачи бойцам (заморозка действует в этом же кадре).
		if (UBoxingFightFx* Fx = UBoxingFightFx::Get(this))
		{
			Fx->OnFightEvent(E, this);
		}
		for (int32 I = 0; I < 2; ++I)
		{
			if (ABoxerCharacter* B = GetBoxer(I))
			{
				B->HandleFightEvent(E, AttLoc);
			}
		}
	}
}

FSoftClassPath ABoxingFightGameMode::VisualPathFor(int32 Index) const
{
	const FSoftClassPath& Own = Index == 0 ? VisualOverridePathRed : VisualOverridePathBlue;
	if (Own.IsValid())
	{
		// "/Script/None.None" — явное «без подмены» из командной строки (-BoxVisualRed=none).
		if (Own.ToString() == TEXT("/Script/None.None"))
		{
			return FSoftClassPath();
		}
		// Любительский бой (3 раунда, как pro = rounds > 3 в вебе) — любительская форма угла: шлем у женщин
		// и младше 19, мужчины-элита без шлема. Нет класса на машине — профи-облик угла ниже.
		if (bAutoAmateurLook && Rounds <= 3)
		{
			const FBoxerPreset& P = Index == 0 ? RedPreset : BluePreset;
			const TCHAR* Corner = Index == 0 ? TEXT("Red") : TEXT("Blue");
			const TCHAR* Kind = (P.bFemale || P.Age < 19) ? TEXT("Amateur") : TEXT("AmateurElite");
			const FSoftClassPath Am(FString::Printf(TEXT("/Game/BoxingLocal/Characters/BP_BoxerLook_%s_%s.BP_BoxerLook_%s_%s_C"), Corner, Kind, Corner, Kind));
			if (Am.TryLoadClass<AActor>())
			{
				return Am;
			}
		}
		if (Own.TryLoadClass<AActor>())
		{
			return Own;
		}
		UE_LOG(LogTemp, Warning, TEXT("BOXER облик %s не найден (Content/BoxingLocal вне git?) — беру %s"), *Own.ToString(), *VisualOverridePath.ToString());
	}
	return VisualOverridePath;
}

void ABoxingFightGameMode::PushStateToBoxers(float DeltaSeconds)
{
	FVector Targets[2];
	for (int32 I = 0; I < 2; ++I)
	{
		Targets[I] = FightToWorld(Snap.Fighters[I].X, Snap.Fighters[I].Z);
	}
	// Минимальная визуальная дистанция по росту (VIS_MIN_SEP веба): клипы Mixamo наклоняют корпус вперёд,
	// и на ближней дистанции ядра (0.9–1.0 м) торсы влезали друг в друга. Раздвигаем точки слежения
	// симметрично вдоль оси пары — ядро (и исходы) не трогаем, кулак доводит наведение/подшаг.
	if (VisMinSepCm > 0.f)
	{
		FVector D = Targets[1] - Targets[0];
		D.Z = 0.f;
		const float Dist = D.Size();
		const float AvgH = 0.5f * (RedPreset.HeightCm + BluePreset.HeightCm);
		const float MinSep = VisMinSepCm * (AvgH > 0.f ? AvgH / 178.f : 1.f);
		if (Dist > 1.f && Dist < MinSep)
		{
			const FVector N = D / Dist;
			const float Push = 0.5f * (MinSep - Dist);
			Targets[0] -= N * Push;
			Targets[1] += N * Push;
			++SepPushes;
		}
	}
	for (int32 I = 0; I < 2; ++I)
	{
		ABoxerCharacter* B = GetBoxer(I);
		if (!B)
		{
			continue;
		}
		// × CustomTimeDilation: на хит-стопе (S-54) таймеры бойца стоят вместе с его картинкой.
		B->ApplyFightState(Snap, Core.GetFightTime(), Targets[I], Snap.Fighters[I].YawDegUE, DeltaSeconds * B->CustomTimeDilation);
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

void ABoxingFightGameMode::TakeShot(const FString& Name)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") / (Name + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogTemp, Log, TEXT("FIGHT: скриншот %s"), *Path);
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
	// «Слипание»: горизонтальная дистанция торсов и голов видимых мешей (в бою, оба на ногах).
	if (RedBoxer && BlueBoxer && Snap.Phase == EFightPhase::Fighting && !Snap.Fighters[0].bDown && !Snap.Fighters[1].bDown)
	{
		const USkeletalMeshComponent* M0 = RedBoxer->GetFeelMesh();
		const USkeletalMeshComponent* M1 = BlueBoxer->GetFeelMesh();
		auto Sep = [M0, M1](const TCHAR* Bone)
		{
			const FName N(Bone);
			if (!M0 || !M1 || M0->GetBoneIndex(N) == INDEX_NONE || M1->GetBoneIndex(N) == INDEX_NONE)
			{
				return 1e6f;
			}
			return static_cast<float>(FVector::Dist2D(M0->GetBoneLocation(N), M1->GetBoneLocation(N)));
		};
		if (RealTime > 2.f)
		{
			MinChestSepCm = FMath::Min(MinChestSepCm, Sep(TEXT("spine_05")));
			MinHeadSepCm = FMath::Min(MinHeadSepCm, Sep(TEXT("head")));
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
				Line += FString::Printf(TEXT(" | %s core(%.2f,%.2f) act(%.0f,%.0f,%.0f) yaw=%.0f err=%.1f v=%.0f a=%.0f abpV=%.0f hp=%.0f st=%.0f %s%s%s mont=%d phys=%.2f"),
					I == 0 ? TEXT("R") : TEXT("B"), F.X, F.Z, L.X, L.Y, L.Z, B->GetActorRotation().Yaw,
					(L - B->FightTarget).Size2D(), Speed, Acc, AbpSeenSpeed(B), F.Health, F.StaminaPct,
					F.bPunching ? PunchName(F.Punch) : TEXT("-"), F.bBlocking ? TEXT(" blk") : TEXT(""), F.bDown ? TEXT(" DOWN") : TEXT(""),
					static_cast<int32>(B->ActiveMontageSlot), B->GetPhysBlend());
			}
			UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
		}
	}

	// Серия после контакта: один снимок за кадр (запрос скриншота в кадре один), по порядку.
	if (DelayedShots.Num() > 0 && RealTime >= DelayedShots[0].Value)
	{
		TakeShot(DelayedShots[0].Key);
		DelayedShots.RemoveAt(0);
	}
	for (int32 S = ShotTimes.Num() - 1; S >= 0; --S)
	{
		if (RealTime >= ShotTimes[S])
		{
			TakeShot(FString::Printf(TEXT("%s_%02d"), *ShotPrefix, FMath::RoundToInt(ShotTimes[S])));
			ShotTimes.RemoveAt(S);
		}
	}

	if (QuitAfter > 0.f && RealTime >= QuitAfter)
	{
		UE_LOG(LogTemp, Log, TEXT("FIGHT СВОДКА: t=%.1f core=%.2f раунд %d, событий: hit %d, block %d, slip %d, miss %d, kd %d; макс. расхождение визуала с ядром %d см; итог: %s"),
			RealTime, Core.GetFightTime(), Snap.Round, EventCount[0], EventCount[1], EventCount[2], EventCount[3], EventCount[4],
			MaxTrackErrorCm, Core.IsOver() ? *GetResultText() : TEXT("бой идёт"));
		UE_LOG(LogTemp, Log, TEXT("FEEL СВОДКА: мин. дистанция торсов (spine_05) %.0f см, голов %.0f см; раздвижек %d кадров (мин. %.0f см); контактов с наведением %d, зазор кулака |ср.| %.1f см, макс. %.1f см; подшаг макс. %.1f см"),
			MinChestSepCm, MinHeadSepCm, SepPushes, VisMinSepCm, FeelContacts, FeelContacts ? FeelGapAbsSum / FeelContacts : 0.f, FeelGapMaxAbs, FeelLungeMax);
		QuitAfter = -1.f;
		FPlatformMisc::RequestExit(false, TEXT("BoxQuitAfter"));
	}
}

void ABoxingFightGameMode::DebugStage()
{
	// Постановка раунда (S-53): лог смены стадии (цели ядра и где стоят актёры) + скриншоты по стадиям.
	const ERingStageKind Kind = Snap.Stage.Kind;
	static const TCHAR* StageNames[] = {TEXT("none"), TEXT("out"), TEXT("rest"), TEXT("neutral"), TEXT("resume")};
	const int32 K = static_cast<int32>(Kind);
	if (Kind != LoggedStage)
	{
		FString Line = FString::Printf(TEXT("FIGHT STAGE %s -> %s: core=%.2f r%d phase=%d"), StageNames[static_cast<int32>(LoggedStage)], StageNames[K],
			Core.GetFightTime(), Snap.Round, static_cast<int32>(Snap.Phase));
		for (int32 I = 0; I < 2; ++I)
		{
			const ABoxerCharacter* B = GetBoxer(I);
			const FVector L = B ? B->GetActorLocation() - RingFloor : FVector::ZeroVector;
			Line += FString::Printf(TEXT(" | %s core(%.2f,%.2f) act(%.0f,%.0f) yaw=%.0f"), I == 0 ? TEXT("R") : TEXT("B"), Snap.Fighters[I].X, Snap.Fighters[I].Z,
				L.X, L.Y, B ? B->GetActorRotation().Yaw : 0.f);
			if (Snap.Stage.bHasTarget[I])
			{
				Line += FString::Printf(TEXT(" -> (%.2f,%.2f)"), Snap.Stage.TargetX[I], Snap.Stage.TargetZ[I]);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
		LoggedStage = Kind;
		StageArrivedAt = -1.f;
	}
	if (!bStageShots || Kind == ERingStageKind::None)
	{
		return;
	}
	const bool bAllArrived = Snap.Stage.bArrived[0] && Snap.Stage.bArrived[1];
	if (bAllArrived && StageArrivedAt < 0.f)
	{
		StageArrivedAt = RealTime;
	}
	// Кадр стадии: out — до гонга (в углах) и на ходу; rest/neutral — дошли до угла; resume — на ходу назад.
	FString Shot;
	if (Kind == ERingStageKind::Out && StageShotsTaken[K] == 0 && Snap.Round == 1 && RealTime >= PreGongHold * 0.8f)
	{
		Shot = TEXT("corners");
	}
	else if (Kind == ERingStageKind::Out && StageShotsTaken[K] == 1 && Snap.Round == 1 && Snap.Stage.T >= 1.0f)
	{
		Shot = TEXT("out");
	}
	else if (Kind == ERingStageKind::Rest && StageShotsTaken[K] == 0 && Snap.Stage.T >= 0.9f)
	{
		Shot = TEXT("rest_walk");
	}
	else if (Kind == ERingStageKind::Rest && StageShotsTaken[K] == 1 && StageArrivedAt >= 0.f && RealTime - StageArrivedAt >= 0.8f)
	{
		Shot = TEXT("rest");
	}
	else if (Kind == ERingStageKind::Neutral && StageShotsTaken[K] == 0 && Snap.Stage.T >= 0.8f)
	{
		Shot = TEXT("neutral_walk");
	}
	else if (Kind == ERingStageKind::Neutral && StageShotsTaken[K] == 1 && StageArrivedAt >= 0.f && RealTime - StageArrivedAt >= 0.5f)
	{
		Shot = TEXT("neutral");
	}
	else if (Kind == ERingStageKind::Resume && StageShotsTaken[K] == 0 && Snap.Stage.T >= 0.6f)
	{
		Shot = TEXT("resume");
	}
	if (!Shot.IsEmpty())
	{
		++StageShotsTaken[K];
		TakeShot(FString::Printf(TEXT("%s_stage_%s"), *ShotPrefix, *Shot));
	}
}

void ABoxingFightGameMode::CheckFeelContacts()
{
	for (int32 I = 0; I < 2; ++I)
	{
		const int32 Kind = PendingFeelCheck[I];
		PendingFeelCheck[I] = 0;
		const ABoxerCharacter* Att = Kind ? GetBoxer(I) : nullptr;
		if (!Att)
		{
			continue;
		}
		const FBoxerFeelDebug Dbg = Att->GetFeelDebug();
		if (Dbg.FistGapCm < -100.f || Dbg.AimW <= 0.f)
		{
			continue;
		}
		++FeelContacts;
		FeelGapAbsSum += FMath::Abs(Dbg.FistGapCm);
		FeelGapMaxAbs = FMath::Max(FeelGapMaxAbs, FMath::Abs(Dbg.FistGapCm));
		FeelLungeMax = FMath::Max(FeelLungeMax, Dbg.LungeCm);
		if (bLogEvents)
		{
			UE_LOG(LogTemp, Log, TEXT("FEEL контакт %s [%d]: зазор кулака %.1f см, подшаг %.1f см, aim %.2f reach %.2f, дист. ядра %.2f м"),
				Kind == 1 ? TEXT("Hit") : TEXT("Blocked"), I, Dbg.FistGapCm, Dbg.LungeCm, Dbg.AimW, Dbg.ReachW, Snap.Distance);
		}
	}
}
