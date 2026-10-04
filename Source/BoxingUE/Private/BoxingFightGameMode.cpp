#include "BoxingFightGameMode.h"

#include "BoxerCharacter.h"
#include "BoxingFightHUD.h"
#include "BoxingFightPlayerController.h"
#include "BoxingGameInstanceSubsystem.h"
#include "FightFx.h"
#include "FightReferee.h"
#include "FightCrew.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
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
	// S-57: бот «человека» за красный угол (ввод — тот же путь, что у клавиатуры) и серия боёв без UI.
	FString BotName;
	if (FParse::Value(Cmd, TEXT("BoxBot="), BotName))
	{
		bBot = true;
		bAutopilot = false; // бот — «человек»: ИИ только синий
		BotSkill = BotName.Equals(TEXT("novice"), ESearchCase::IgnoreCase) ? EFightBotSkill::Novice
			: BotName.Equals(TEXT("strong"), ESearchCase::IgnoreCase) ? EFightBotSkill::Strong
			: BotName.Equals(TEXT("masher"), ESearchCase::IgnoreCase) ? EFightBotSkill::Masher
			: EFightBotSkill::Average;
		if (BotSkill == EFightBotSkill::Average && !BotName.Equals(TEXT("average"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Warning, TEXT("BOT: неизвестный -BoxBot=%s (novice|average|strong|masher) — играет average"), *BotName);
		}
	}
	FParse::Value(Cmd, TEXT("BoxBotFights="), BotFights);
	FParse::Value(Cmd, TEXT("BoxBotFrom="), BotFrom);
	bGlassJaw = FParse::Param(Cmd, TEXT("BoxGlassJaw"));
	if (BotFights > 0 && !bBot)
	{
		bBot = true;
		bAutopilot = false;
	}
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

void ABoxingFightGameMode::ApplyArenaDress(bool bPro)
{
	// Профи-набор сохранён в уровне скрытым; коллизию пола держит любительский канвас (Docs/PERF.md).
	TArray<AActor*> Am, Pro;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), TEXT("ArenaAmateur"), Am);
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), TEXT("ArenaPro"), Pro);
	for (AActor* A : Am)
	{
		A->SetActorHiddenInGame(bPro);
	}
	for (AActor* A : Pro)
	{
		A->SetActorHiddenInGame(!bPro);
	}
}

void ABoxingFightGameMode::StartPlay()
{
	Super::StartPlay();
	LocateRing();
	ApplyArenaDress(Rounds > 3); // профи — как pro = rounds > 3 в вебе

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
	// S-58: рефери (место/жесты — сам по снимку ядра, ядро не трогает). -BoxNoRef — без него.
	ABoxingReferee::SpawnFor(this);
	if (bBot && BotFights > 0)
	{
		RunBotBatch(); // S-57: серия боёв бота без отрисовки → сводка в лог → выход
		return;
	}
	// S-71: угловые (тренер, катмен) и стулья у обоих углов — сами по снимку и событиям ядра. -BoxNoCrew — без них.
	ABoxingCornerCrew::SpawnFor(this);
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
	if (bGlassJaw)
	{
		Cfg.bGlassJaw = true; // S-57: быстрый KO для проверки баннера/повтора (web ?ko=1)
		Cfg.Fighters[1].Stats.Chin = 1.f;
	}
	Cfg.Rounds = Rounds;
	Cfg.RoundSeconds = RoundSeconds;
	Cfg.BreakSeconds = BreakSeconds;
	// S-71 (ux): с игроком перерыв ждёт «Продолжить» (Enter / A, после посадки в угол — ABoxingFightHUD), а сам заканчивается
	// лишь через минуту, как в боксе (страховка). Автопилот/бот и явный -BoxBreakSec= — прежний таймер.
	{
		FString BreakCmd;
		if (!bAutopilot && !bBot && !FParse::Value(FCommandLine::Get(), TEXT("BoxBreakSec="), BreakCmd))
		{
			Cfg.BreakSeconds = FMath::Max(BreakSeconds, 60.f);
		}
	}
	Cfg.bAutoProceed = true;
	Cfg.bAllowDraw = bAllowDraw;
	Cfg.bProRules = bAllowDraw; // S-61: профи-правила (досрочки от удара, рефери, судьи профи) — там же, где ничья возможна
	Cfg.bCorners = bCorners;
	Cfg.Seed = static_cast<uint32>(Seed);
	Core.Init(Cfg);
	if (bBot)
	{
		Bot.Reset(BotSkill, Cfg.Seed, 0);
		bBotHeld = false;
	}
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

void ABoxingFightGameMode::Surrender()
{
	// S-59: «Сдаться» из меню паузы — между шагами ядра (мир только что снят с паузы), ГСЧ не трогается.
	if (!bStarted || bAutopilot || Core.IsOver())
	{
		return;
	}
	Pending.Reset();
	bHasHeldStep = false;
	bSurrendered = Core.Concede(GetPlayerIndex());
	Snap = Core.GetSnapshot();
	DispatchEvents(Core.PollEvents());
}

void ABoxingFightGameMode::StepCore()
{
	if (bBot)
	{
		BotThink(); // бот «жмёт клавиши» до шага — его нажатия применятся на этой же границе
	}
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
	UpdateCrowd(DeltaSeconds);
}

void ABoxingFightGameMode::CrowdPeak(float Level, float HoldSeconds)
{
	CrowdTarget = FMath::Max(CrowdTarget, Level);
	CrowdHold = FMath::Max(CrowdHold, HoldSeconds);
}

void ABoxingFightGameMode::UpdateCrowd(float DeltaSeconds)
{
	// Подъём к пику ~0.3 с, удержание, спад ~2.5 с (Docs/LOOK.md, «API реакции зала»).
	if (CrowdHold > 0.f)
	{
		CrowdHold -= DeltaSeconds;
		CrowdExcite = FMath::Min(CrowdTarget, CrowdExcite + DeltaSeconds / 0.3f);
	}
	else
	{
		CrowdTarget = 0.f;
		CrowdExcite = FMath::Max(0.f, CrowdExcite - DeltaSeconds / 2.5f);
	}
	if (FMath::IsNearlyEqual(CrowdExcite, CrowdSent, 0.005f))
	{
		return;
	}
	if (!CrowdMpc)
	{
		CrowdMpc = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Boxing/Environment/Crowd/MPC_Crowd.MPC_Crowd"));
	}
	if (CrowdMpc)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, CrowdMpc, TEXT("Excite"), CrowdExcite);
		CrowdSent = CrowdExcite;
	}
}

void ABoxingFightGameMode::DispatchEvents(TArray<FFightEvent>&& Events)
{
	for (const FFightEvent& E : Events)
	{
		OnFightEventUi.Broadcast(E); // S-71: копилка «совета угла» (UI)
		++EventCount[static_cast<int32>(E.Kind)];
		// Реакция зала (S-77): нокдаун — вскакивают, итог — стоят дольше, тяжёлое попадание — полувстают.
		if (E.Kind == EFightEventKind::Knockdown)
		{
			CrowdPeak(1.f, 1.2f);
		}
		else if (E.Kind == EFightEventKind::FightEnd)
		{
			CrowdPeak(1.f, 5.f);
		}
		else if (E.Kind == EFightEventKind::Hit && E.Magnitude >= 1.7f)
		{
			CrowdPeak(0.55f, 0.3f);
		}
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
		ABoxingCornerCrew::NotifyEvent(this, E); // S-71: реакции углов
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
		// Профи-женщина (S-60): топ в цвет угла, тело без кожи под ним. Нет класса — профи-облик угла.
		if (Rounds > 3 && (Index == 0 ? RedPreset : BluePreset).bFemale)
		{
			const TCHAR* Corner = Index == 0 ? TEXT("Red") : TEXT("Blue");
			const FSoftClassPath Fem(FString::Printf(TEXT("/Game/BoxingLocal/Characters/BP_BoxerLook_%s_Female.BP_BoxerLook_%s_Female_C"), Corner, Corner));
			if (Fem.TryLoadClass<AActor>())
			{
				return Fem;
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
	for (int32 J = 0; J < R.NumJudges; ++J) // S-65: любители — 5 судей
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
		UE_LOG(LogTemp, Log, TEXT("FEEL гард (S-62): попаданий %d, путь кулака сквозь перчатку защиты (< 16 см до её центра) %d (%.0f%%), мин. от пути до центра перчатки %.1f см"),
			FeelHits, FeelHitsThroughGlove, FeelHits ? 100.f * FeelHitsThroughGlove / FeelHits : 0.f, FeelGloveMinCm);
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
		// S-62: засчитанное попадание — путь кулака (локоть → фронт) не должен идти сквозь перчатку защиты.
		float GloveCm = -1.f;
		const ABoxerCharacter* Def = GetBoxer(1 - I);
		const USkeletalMeshComponent* DM = Def ? Def->GetFeelMesh() : nullptr;
		if (Kind == 1 && DM && !Dbg.Elbow.IsNearlyZero())
		{
			GloveCm = 1e6f;
			for (const TCHAR* Sfx : {TEXT("_l"), TEXT("_r")})
			{
				const FName H(*(FString(TEXT("hand")) + Sfx)), L(*(FString(TEXT("lowerarm")) + Sfx));
				if (DM->GetBoneIndex(H) == INDEX_NONE || DM->GetBoneIndex(L) == INDEX_NONE)
				{
					continue;
				}
				const FVector Hp = DM->GetBoneLocation(H);
				const FVector Glove = Hp + (Hp - DM->GetBoneLocation(L)).GetSafeNormal() * 6.f;
				GloveCm = FMath::Min(GloveCm, BoxerFeel::SegPointDist(Dbg.Elbow, Dbg.FistFront, Glove));
			}
			++FeelHits;
			FeelHitsThroughGlove += GloveCm < 16.f ? 1 : 0; // перчатка ~9 см + кулак ~7 см: объёмы пересекаются
			FeelGloveMinCm = FMath::Min(FeelGloveMinCm, GloveCm);
		}
		if (bLogEvents)
		{
			UE_LOG(LogTemp, Log, TEXT("FEEL контакт %s [%d]: зазор кулака %.1f см, подшаг %.1f см, aim %.2f reach %.2f, дист. ядра %.2f м, до перчатки защиты %.1f см"),
				Kind == 1 ? TEXT("Hit") : TEXT("Blocked"), I, Dbg.FistGapCm, Dbg.LungeCm, Dbg.AimW, Dbg.ReachW, Snap.Distance, GloveCm);
		}
	}
}

// ---------- S-57: бот «человека» ----------

void ABoxingFightGameMode::BotThink()
{
	TArray<FFightBotCmd> Cmds;
	bool bHeld = false;
	EFightAction Held = EFightAction::StepBack;
	Bot.Think(Core.GetSnapshot(), FixedStep, Cmds, bHeld, Held);
	// Тот же путь, что у клавиатуры/геймпада (PlayerController): нажатия → QueueAction, удержание ног → SetHeldStep.
	for (const FFightBotCmd& C : Cmds)
	{
		QueueAction(C.Action, C.Target);
	}
	if (bHeld || bBotHeld)
	{
		SetHeldStep(Held, bHeld); // отпускаем только своё удержание — клавиши живого игрока не трогаем
		bBotHeld = bHeld;
	}
}

void ABoxingFightGameMode::RunBotBatch()
{
	const int32 SavedSeed = Seed;
	const double Wall0 = FPlatformTime::Seconds();
	int32 Wins = 0, Losses = 0, Draws = 0;
	int32 WinBy[5] = {0, 0, 0, 0, 0}, LossBy[5] = {0, 0, 0, 0, 0}; // по EFightMethod
	int32 KdFor = 0, KdAgainst = 0, HitsFor = 0, HitsAgainst = 0, BlockedFor = 0, BlockedAgainst = 0, Slipped = 0, Gassed = 0;
	int32 Pressed = 0;
	const TCHAR* MethodShort[5] = {TEXT("—"), TEXT("решение"), TEXT("KO"), TEXT("RSC"), TEXT("ничья")};
	UE_LOG(LogTemp, Log, TEXT("BOT: серия %d боёв, бот %s за красный «%s» против ИИ «%s», %d р. × %.0f с, сиды (k·2654435761 + 17) ^ 0xabc с k = %d"),
		BotFights, UTF8_TO_TCHAR(FFightBot::SkillName(BotSkill)), *RedPreset.Name, *BluePreset.Name, Rounds, RoundSeconds, BotFrom);
	for (int32 K = 0; K < BotFights; ++K)
	{
		const uint32 FightSeed = (static_cast<uint32>(BotFrom + K) * 2654435761u + 17u) ^ 0xabcu; // как harness / humanWinRate веба
		Seed = static_cast<int32>(FightSeed);
		StartFight();
		int32 F[2] = {0, 0}, Kd[2] = {0, 0}, Gs = 0;
		for (int32 Step = 0; Step < 60 * 60 * 60 && !Core.IsOver(); ++Step)
		{
			StepCore();
			for (const FFightEvent& E : Core.PollEvents())
			{
				switch (E.Kind)
				{
				case EFightEventKind::Hit: ++F[E.Attacker == 0 ? 0 : 1]; break;
				case EFightEventKind::Knockdown: ++Kd[E.Defender == 1 ? 0 : 1]; break;
				case EFightEventKind::Blocked: ++(E.Attacker == 0 ? BlockedFor : BlockedAgainst); break;
				case EFightEventKind::Slipped: Slipped += E.Defender == 0 ? 1 : 0; break;
				case EFightEventKind::Gassed: ++Gs; break;
				default: break;
				}
			}
		}
		const FFightResult& R = Core.GetResult();
		const int32 M = FMath::Clamp(static_cast<int32>(R.Method), 0, 4);
		const TCHAR* Verdict = R.WinnerIndex == 0 ? TEXT("ПОБЕДА") : (R.WinnerIndex < 0 ? TEXT("НИЧЬЯ") : TEXT("поражение"));
		if (R.WinnerIndex == 0) { ++Wins; ++WinBy[M]; }
		else if (R.WinnerIndex < 0) { ++Draws; }
		else { ++Losses; ++LossBy[M]; }
		HitsFor += F[0]; HitsAgainst += F[1]; KdFor += Kd[0]; KdAgainst += Kd[1]; Gassed += Gs; Pressed += Bot.PunchesPressed;
		FString Cards; // S-65: все судьи (любители — 5); ровная карта, где судья назвал победителя, — «*к»/«*с»
		for (int32 J = 0; J < R.NumJudges; ++J)
		{
			Cards += FString::Printf(TEXT("%s%d-%d%s"), J ? TEXT(" ") : TEXT(""), R.JudgeTotals[J].Red, R.JudgeTotals[J].Blue,
				R.TieNominee[J] < 0 ? TEXT("") : (R.TieNominee[J] == 0 ? TEXT("*к") : TEXT("*с")));
		}
		UE_LOG(LogTemp, Log, TEXT("BOT бой %d (сид %u): %s %s%s%s, судьи %s, нокдауны %d-%d, попадания %d-%d, нажато ударов %d, блоков %d, уклонов %d, «нет сил» %d"),
			BotFrom + K, FightSeed, Verdict, MethodShort[M], R.StoppedRound > 0 ? *FString::Printf(TEXT(" в %d р."), R.StoppedRound) : TEXT(""),
			R.Method == EFightMethod::Decision || R.Method == EFightMethod::Draw ? *FString::Printf(TEXT(" (%s)"), DecisionText(R.Decision)) : TEXT(""),
			*Cards, Kd[0], Kd[1], F[0], F[1], Bot.PunchesPressed, Bot.Blocks, Bot.Slips, Gs);
	}
	const double N = FMath::Max(1, BotFights);
	UE_LOG(LogTemp, Log, TEXT("BOT СВОДКА: %s, %d боёв: побед %d (%.0f%%), поражений %d, ничьих %d; победы: решением %d, KO %d, RSC %d; поражения: решением %d, KO %d, RSC %d"),
		UTF8_TO_TCHAR(FFightBot::SkillName(BotSkill)), BotFights, Wins, 100.0 * Wins / N, Losses, Draws, WinBy[1], WinBy[2], WinBy[3], LossBy[1], LossBy[2], LossBy[3]);
	UE_LOG(LogTemp, Log, TEXT("BOT СВОДКА: нокдаунов за бой %.2f / против %.2f; попаданий за бой %.1f / %.1f; в блок %.1f / %.1f; уклонов бота %.1f; ударов нажато %.1f; «нет сил» %.1f; %.1f с"),
		KdFor / N, KdAgainst / N, HitsFor / N, HitsAgainst / N, BlockedFor / N, BlockedAgainst / N, Slipped / N, Pressed / N, Gassed / N,
		FPlatformTime::Seconds() - Wall0);
	Seed = SavedSeed;
	FPlatformMisc::RequestExit(false, TEXT("BoxBotFights"));
}
