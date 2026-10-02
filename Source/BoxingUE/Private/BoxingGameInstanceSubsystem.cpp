#include "BoxingGameInstanceSubsystem.h"

#include "BoxingFightGameMode.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "FightFx.h"
#include "UIFightResult.h"
#include "UIMainMenu.h"
#include "UILoading.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "MoviePlayer.h"
#include "HAL/IConsoleManager.h"

const TCHAR* BoxStat::Label(int32 I)
{
	static const TCHAR* Labels[] = {TEXT("Сила"), TEXT("Скорость"), TEXT("Ноги"), TEXT("Кардио"), TEXT("Подбородок"), TEXT("Техника"), TEXT("Защита")};
	return (I >= 0 && I < Num) ? Labels[I] : TEXT("?");
}

namespace
{
	EBoxerStyle StyleOf(const FString& S)
	{
		static const TCHAR* Names[] = {TEXT("technical"), TEXT("volume"), TEXT("puncher"), TEXT("pressure"), TEXT("counter"), TEXT("speed"), TEXT("balanced")};
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			if (S == Names[I])
			{
				return static_cast<EBoxerStyle>(I);
			}
		}
		return EBoxerStyle::Balanced;
	}

	TArray<float> StatsOf(const TSharedPtr<FJsonObject>& O)
	{
		static const TCHAR* Keys[] = {TEXT("power"), TEXT("handSpeed"), TEXT("footwork"), TEXT("stamina"), TEXT("chin"), TEXT("technique"), TEXT("defense")};
		TArray<float> Out;
		Out.Init(70.f, BoxStat::Num);
		if (O.IsValid())
		{
			for (int32 I = 0; I < BoxStat::Num; ++I)
			{
				double V = 70.0;
				O->TryGetNumberField(Keys[I], V);
				Out[I] = static_cast<float>(V);
			}
		}
		return Out;
	}

	ERosterKind KindOf(const FString& S)
	{
		return S == TEXT("pro") ? ERosterKind::Pro : (S == TEXT("legend") ? ERosterKind::Legend : ERosterKind::Amateur);
	}

	// S-63: шрифт UI эмодзи не рисует («тофу», Could not find Glyph U+1f3db у легенд веба: «🏛 50-0»). Убираем символы
	// вне BMP (суррогатные пары), вариационные селекторы и ZWJ; пробелы по краям — тоже.
	FString NoEmoji(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if ((C >= 0xD800 && C <= 0xDFFF) || (C >= 0xFE00 && C <= 0xFE0F) || C == 0x200D)
			{
				continue;
			}
			Out.AppendChar(C);
		}
		return Out.TrimStartAndEnd();
	}
}

bool FRosterBoxer::IsWorldChampion() const
{
	if (Badge.IsEmpty())
	{
		return false;
	}
	static const TCHAR* Bad[] = {TEXT("#"), TEXT("интерим"), TEXT("рег."), TEXT("топ"), TEXT("экс"), TEXT("Ring")};
	for (const TCHAR* B : Bad)
	{
		if (Badge.Contains(B, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}
	return Badge.Contains(TEXT("WBC")) || Badge.Contains(TEXT("WBA")) || Badge.Contains(TEXT("IBF")) || Badge.Contains(TEXT("WBO"));
}

FBoxerPreset FRosterBoxer::ToPreset(bool bProFight) const
{
	const TArray<float>& S = (bProFight && ProStats.Num() == BoxStat::Num) ? ProStats : Stats;
	FBoxerPreset P;
	P.Name = Name;
	P.Id = Id; // облик (S-60, Appearance.json)
	if (S.Num() == BoxStat::Num)
	{
		P.Power = S[BoxStat::Power];
		P.HandSpeed = S[BoxStat::HandSpeed];
		P.Footwork = S[BoxStat::Footwork];
		P.Stamina = S[BoxStat::Stamina];
		P.Chin = S[BoxStat::Chin];
		P.Technique = S[BoxStat::Technique];
		P.Defense = S[BoxStat::Defense];
	}
	P.HeightCm = HeightCm;
	P.ReachCm = ReachCm;
	P.WeightKg = WeightKg;
	P.Style = Style;
	P.bFemale = bFemale;
	P.bSouthpaw = Stance.Equals(TEXT("southpaw"), ESearchCase::IgnoreCase); // S-62: зеркальная стойка (визуал)
	if (Age > 0)
	{
		P.Age = Age;
	}
	// Налог на дистанцию (идея №11 веба): только в профи-бою, у любителя без профи-боёв.
	P.Seasoning = bProFight ? FMath::Clamp(ProSeasoning, 0.f, 1.f) : 1.f;
	return P;
}

FString FRosterBoxer::Origin() const
{
	return City.IsEmpty() ? Country : FString::Printf(TEXT("%s · %s"), *Country, *City);
}

UBoxingGameInstanceSubsystem* UBoxingGameInstanceSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBoxingGameInstanceSubsystem>() : nullptr;
}

void UBoxingGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadRoster();

	const TCHAR* Cmd = FCommandLine::Get();
	bAuto = FParse::Param(Cmd, TEXT("BoxUiAuto"));
	bAutoRematch = FParse::Param(Cmd, TEXT("BoxUiRematch"));
	FParse::Value(Cmd, TEXT("BoxUiTab="), AutoTab);
	FParse::Value(Cmd, TEXT("BoxUiGender="), AutoGender);
	FParse::Value(Cmd, TEXT("BoxUiRounds="), AutoRounds);
	FParse::Value(Cmd, TEXT("BoxUiShotPrefix="), ShotPrefix);
	FString Pick;
	if (FParse::Value(Cmd, TEXT("BoxUiPick="), Pick, false))
	{
		Pick.ParseIntoArray(AutoPick, TEXT(","));
	}
	ReadPickFromCommandLine();

	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UBoxingGameInstanceSubsystem::OnPreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UBoxingGameInstanceSubsystem::OnPostLoadMap);

	FString Script;
	if (FParse::Value(Cmd, TEXT("BoxUiScript="), Script, false) && !Script.IsEmpty())
	{
		Script.ParseIntoArray(ScriptSteps, TEXT(","));
		bScript = ScriptSteps.Num() > 0;
		ScriptTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBoxingGameInstanceSubsystem::TickScript));
		UE_LOG(LogTemp, Log, TEXT("UI-SCRIPT: %d шагов"), ScriptSteps.Num());
	}
}

void UBoxingGameInstanceSubsystem::Deinitialize()
{
	if (ScriptTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ScriptTicker);
		ScriptTicker.Reset();
	}
	for (FTSTicker::FDelegateHandle* H : {&TransitionTicker, &FirstFrameTicker, &RingCoverTicker})
	{
		if (H->IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(*H);
			H->Reset();
		}
	}
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (PreloadHandle.IsValid())
	{
		PreloadHandle->CancelHandle();
		PreloadHandle.Reset();
	}
	Resident.Empty();
	Super::Deinitialize();
}

void UBoxingGameInstanceSubsystem::LoadRoster()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Boxing/Data/Roster.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		RosterError = FString::Printf(TEXT("Нет файла ростера %s — запусти Tools/RosterExport/export.mjs"), *Path);
		UE_LOG(LogTemp, Warning, TEXT("UI: %s"), *RosterError);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	const TArray<TSharedPtr<FJsonValue>>* Boxers = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("boxers"), Boxers))
	{
		RosterError = TEXT("Roster.json не читается (битый JSON)");
		UE_LOG(LogTemp, Warning, TEXT("UI: %s"), *RosterError);
		return;
	}
	Roster.Reset();
	ById.Reset();
	for (const TSharedPtr<FJsonValue>& V : *Boxers)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid())
		{
			continue;
		}
		FRosterBoxer B;
		B.Id = O->GetStringField(TEXT("id"));
		B.Name = O->GetStringField(TEXT("name"));
		B.Kind = KindOf(O->GetStringField(TEXT("kind")));
		B.bFemale = O->GetStringField(TEXT("gender")) == TEXT("F");
		O->TryGetStringField(TEXT("countryCode"), B.CountryCode);
		O->TryGetStringField(TEXT("country"), B.Country);
		O->TryGetStringField(TEXT("city"), B.City);
		O->TryGetNumberField(TEXT("age"), B.Age);
		double D = 0.0;
		if (O->TryGetNumberField(TEXT("weightKg"), D)) { B.WeightKg = static_cast<float>(D); }
		if (O->TryGetNumberField(TEXT("heightCm"), D)) { B.HeightCm = static_cast<float>(D); }
		if (O->TryGetNumberField(TEXT("reachCm"), D)) { B.ReachCm = static_cast<float>(D); }
		if (O->TryGetNumberField(TEXT("overall"), D)) { B.Overall = static_cast<float>(D); }
		if (O->TryGetNumberField(TEXT("proOverall"), D)) { B.ProOverall = static_cast<float>(D); }
		if (O->TryGetNumberField(TEXT("proSeasoning"), D)) { B.ProSeasoning = static_cast<float>(D); }
		O->TryGetStringField(TEXT("division"), B.Division);
		O->TryGetStringField(TEXT("stance"), B.Stance);
		B.Style = StyleOf(O->GetStringField(TEXT("style")));
		O->TryGetStringField(TEXT("styleLabel"), B.StyleLabel);
		const TSharedPtr<FJsonObject>* SO = nullptr;
		B.Stats = StatsOf(O->TryGetObjectField(TEXT("stats"), SO) ? *SO : nullptr);
		B.ProStats = O->TryGetObjectField(TEXT("proStats"), SO) ? StatsOf(*SO) : B.Stats;
		O->TryGetStringField(TEXT("badge"), B.Badge);
		O->TryGetStringField(TEXT("proRecord"), B.ProRecord);
		O->TryGetStringField(TEXT("accolades"), B.Accolades);
		O->TryGetStringField(TEXT("notes"), B.Notes);
		B.Badge = NoEmoji(B.Badge);
		B.Accolades = NoEmoji(B.Accolades);
		B.Notes = NoEmoji(B.Notes);
		if (B.Id.IsEmpty() || ById.Contains(B.Id))
		{
			continue;
		}
		ById.Add(B.Id, Roster.Num());
		Roster.Add(MoveTemp(B));
	}
	ProClassesM.Reset();
	ProClassesF.Reset();
	for (const FRosterBoxer& B : Roster)
	{
		if (B.Kind == ERosterKind::Pro)
		{
			(B.bFemale ? ProClassesF : ProClassesM).AddUnique(B.WeightKg);
		}
	}
	ProClassesM.Sort();
	ProClassesF.Sort();
	UE_LOG(LogTemp, Log, TEXT("UI: ростер загружен — %d бойцов (%s)"), Roster.Num(), *Path);
}

void UBoxingGameInstanceSubsystem::ReadPickFromCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FString RedName, BlueName;
	if (!FParse::Value(Cmd, TEXT("BoxPickRed="), RedName, false) || !FParse::Value(Cmd, TEXT("BoxPickBlue="), BlueName, false))
	{
		return;
	}
	const FRosterBoxer* R = FindByName(RedName);
	const FRosterBoxer* B = FindByName(BlueName);
	if (!R || !B)
	{
		UE_LOG(LogTemp, Warning, TEXT("UI: -BoxPickRed/-BoxPickBlue — боец не найден (%s / %s)"), *RedName, *BlueName);
		return;
	}
	FExhibitionSetup S;
	S.bValid = true;
	S.RedId = R->Id;
	S.BlueId = B->Id;
	S.bProRules = R->Kind != ERosterKind::Amateur || B->Kind != ERosterKind::Amateur;
	S.Rounds = S.bProRules ? AutoProRounds(*R, *B) : 3;
	FParse::Value(Cmd, TEXT("BoxPickRounds="), S.Rounds);
	S.Seed = FMath::Rand();
	S.Tab = R->Kind;
	S.bFemale = R->bFemale;
	Exhibition = S;
	UE_LOG(LogTemp, Log, TEXT("UI: пара из командной строки — %s vs %s, %d р."), *R->Name, *B->Name, S.Rounds);
}

const FRosterBoxer* UBoxingGameInstanceSubsystem::FindById(const FString& Id) const
{
	const int32* I = ById.Find(Id);
	return I ? &Roster[*I] : nullptr;
}

const FRosterBoxer* UBoxingGameInstanceSubsystem::FindByName(const FString& Part, int32 Kind, int32 Female) const
{
	const FString P = Part.TrimStartAndEnd();
	if (P.IsEmpty())
	{
		return nullptr;
	}
	for (const FRosterBoxer& B : Roster)
	{
		if ((Kind < 0 || static_cast<int32>(B.Kind) == Kind) && (Female < 0 || B.bFemale == (Female != 0))
			&& B.Name.Contains(P, ESearchCase::IgnoreCase))
		{
			return &B;
		}
	}
	return nullptr;
}

float UBoxingGameInstanceSubsystem::RingWeightKg(const FRosterBoxer& Red, const FRosterBoxer& Blue, bool bProFight) const
{
	using namespace BoxingFightProfile;
	const bool bFemale = Red.bFemale && Blue.bFemale;
	if (!bProFight)
	{
		return bFemale ? Catchweight(Red.WeightKg, Blue.WeightKg, AMATEUR_CLASSES_F, UE_ARRAY_COUNT(AMATEUR_CLASSES_F))
			: Catchweight(Red.WeightKg, Blue.WeightKg, AMATEUR_CLASSES_M, UE_ARRAY_COUNT(AMATEUR_CLASSES_M));
	}
	const TArray<double>& Pro = bFemale ? ProClassesF : ProClassesM;
	return Pro.Num() ? Catchweight(Red.WeightKg, Blue.WeightKg, Pro.GetData(), Pro.Num()) : 0.5f * (Red.WeightKg + Blue.WeightKg);
}

const FPairForecast& UBoxingGameInstanceSubsystem::ForecastPair(const FRosterBoxer& Red, const FRosterBoxer& Blue, int32 Rounds, bool bProRules) const
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d|%d"), *Red.Id, *Blue.Id, Rounds, bProRules ? 1 : 0);
	if (const FPairForecast* Hit = ForecastCache.Find(Key))
	{
		return *Hit;
	}
	const double T0 = FPlatformTime::Seconds();
	const bool bProFight = Rounds > 3;
	FPairForecast F;
	F.RingKg = RingWeightKg(Red, Blue, bProFight);
	F.RedDelta = Red.WeightKg - F.RingKg;
	F.BlueDelta = Blue.WeightKg - F.RingKg;
	F.Stretch = static_cast<float>(BoxingFightProfile::WeightStretch(F.RingKg, Red.WeightKg, Blue.WeightKg));
	const FFighterSetup R = BoxingFightProfile::ProjectToWeight(Red.ToPreset(bProFight).ToSetup(true), F.RingKg);
	const FFighterSetup B = BoxingFightProfile::ProjectToWeight(Blue.ToPreset(bProFight).ToSetup(true), F.RingKg);
	F.Odds = BoxingFightProfile::PredictOutcome(R, B, Rounds, bProRules);
	F.bValid = F.Odds.Fights > 0;
	UE_LOG(LogTemp, Log, TEXT("UI: прогноз %s vs %s (%d р., вес %.1f) — %.0f%% / %.0f%% / ничья %.0f%%, %d боёв за %.0f мс"), *Red.Name, *Blue.Name,
		Rounds, F.RingKg, F.Odds.RedWin * 100.f, F.Odds.BlueWin * 100.f, F.Odds.Draw * 100.f, F.Odds.Fights, (FPlatformTime::Seconds() - T0) * 1000.0);
	return ForecastCache.Add(Key, F);
}

int32 UBoxingGameInstanceSubsystem::AutoProRounds(const FRosterBoxer& A, const FRosterBoxer& B)
{
	return A.IsWorldChampion() && B.IsWorldChampion() ? 12 : 10;
}

bool UBoxingGameInstanceSubsystem::ApplyToFightMode(ABoxingFightGameMode& Mode) const
{
	if (!Exhibition.bValid)
	{
		return false;
	}
	const FRosterBoxer* R = FindById(Exhibition.RedId);
	const FRosterBoxer* B = FindById(Exhibition.BlueId);
	if (!R || !B)
	{
		return false;
	}
	const bool bProFight = Exhibition.Rounds > 3; // как FightView веба: pro = rounds > 3
	Mode.RedPreset = R->ToPreset(bProFight);
	Mode.BluePreset = B->ToPreset(bProFight);
	// S-61: проекция на вес боя (fightProfile веба) — тот же вес и та же проекция, что в прогнозе ForecastPair: сгонка режет
	// кардио/подбородок, переход вверх — массу для урона. WeightKg пресета остаётся натуральным (тело бойца в UE).
	const float RingKg = RingWeightKg(*R, *B, bProFight);
	for (FBoxerPreset* P : {&Mode.RedPreset, &Mode.BluePreset})
	{
		const FFighterSetup S = BoxingFightProfile::ProjectToWeight(P->ToSetup(true), RingKg);
		P->Stamina = S.Stats.Stamina;
		P->Chin = S.Stats.Chin;
		P->MassForPower = S.MassForPower;
		P->DurabilityMass = S.DurabilityMass;
		P->RingWeightKg = RingKg;
	}
	Mode.Rounds = FMath::Clamp(Exhibition.Rounds, 1, 12);
	Mode.bAllowDraw = Exhibition.bProRules;
	Mode.Seed = Exhibition.Seed;
	// Отладка HUD нокдауна: -BoxUiDebugChin=N / -BoxUiDebugChinRed=N — подбородок синего / красного угла (стеклянная челюсть для скриншотов).
	float DebugChin = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("BoxUiDebugChin="), DebugChin) && DebugChin >= 0.f)
	{
		Mode.BluePreset.Chin = DebugChin;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("BoxUiDebugChinRed="), DebugChin) && DebugChin >= 0.f)
	{
		Mode.RedPreset.Chin = DebugChin;
	}
	UE_LOG(LogTemp, Log, TEXT("UI: бой из Выставки — %s (%.1f, %.0f кг) vs %s (%.1f, %.0f кг), вес боя %.0f кг, %d р., %s, сид %d"), *R->Name,
		bProFight ? R->ProOverall : R->Overall, R->WeightKg, *B->Name, bProFight ? B->ProOverall : B->Overall, B->WeightKg, RingKg, Mode.Rounds,
		Exhibition.bProRules ? TEXT("профи") : TEXT("любители"), Mode.Seed);
	return true;
}

void UBoxingGameInstanceSubsystem::StartExhibitionFight(const UObject* WorldContext)
{
	if (!Exhibition.bValid)
	{
		return;
	}
	BeginRingTransition(WorldContext, TEXT("в бой"));
}

void UBoxingGameInstanceSubsystem::Rematch(const UObject* WorldContext)
{
	Exhibition.Seed = FMath::Rand();
	BeginRingTransition(WorldContext, TEXT("реванш"));
}

// ======================================================================
// Загрузка боя (S-63)
// ======================================================================
void UBoxingGameInstanceSubsystem::StartFightPreload()
{
	if (PreloadHandle.IsValid() || FParse::Param(FCommandLine::Get(), TEXT("BoxNoPreload")))
	{
		return;
	}
	PreloadStartAt = FPlatformTime::Seconds();
	// Что грузит бой: класс бойца (BP_Boxer → AnimBP GASP → базы Motion Matching, манекен), рефери, облики углов и
	// рефери (+ грумы), монтажи ударов, звук, ввод, ретаргетер. Папки — через реестр ассетов: новое в них (облики
	// tech-artist, клипы game-feel) подхватится само.
	TArray<FSoftObjectPath> Paths = {
		FSoftObjectPath(TEXT("/Game/Boxing/Blueprints/BP_Boxer.BP_Boxer_C")),
		FSoftObjectPath(TEXT("/Game/Boxing/Blueprints/BP_Referee.BP_Referee_C")),
		FSoftObjectPath(TEXT("/Game/MetaHumans/Common/Common/Rigs/RTG_UEFN_to_Metahuman_nrw.RTG_UEFN_to_Metahuman_nrw")),
	};
	const TArray<FString> Folders = {TEXT("/Game/BoxingLocal/Characters"), TEXT("/Game/BoxingLocal/Anim"), TEXT("/Game/Boxing/Anim"),
		TEXT("/Game/Boxing/Audio"), TEXT("/Game/Boxing/Input"), TEXT("/Game/Boxing/Characters")};
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AR.ScanPathsSynchronous(Folders, false);
	for (const FString& F : Folders)
	{
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(*F), Assets, true);
		for (const FAssetData& A : Assets)
		{
			if (!A.IsRedirector())
			{
				Paths.AddUnique(A.GetSoftObjectPath());
			}
		}
	}
	PreloadRequested = Paths.Num();
	SetAsyncBudget(25.f);
	PreloadHandle = Streamable.RequestAsyncLoad(Paths, FStreamableDelegate::CreateWeakLambda(this, [this]()
	{
		PreloadDoneAt = FPlatformTime::Seconds();
		SetAsyncBudget(DefaultAsyncBudget);
		UE_LOG(LogTemp, Log, TEXT("LOAD: предзагрузка боя готова — %d ассетов за %.2f с"), PreloadRequested, PreloadDoneAt - PreloadStartAt);
	}), FStreamableManager::AsyncLoadHighPriority, true);
	UE_LOG(LogTemp, Log, TEXT("LOAD: предзагрузка боя — %d ассетов (асинхронно)"), PreloadRequested);
}

void UBoxingGameInstanceSubsystem::SetAsyncBudget(float Ms)
{
	IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit"));
	if (!V)
	{
		return;
	}
	if (DefaultAsyncBudget < 0.f)
	{
		DefaultAsyncBudget = V->GetFloat();
	}
	V->Set(Ms > 0.f ? Ms : DefaultAsyncBudget, ECVF_SetByCode);
}

bool UBoxingGameInstanceSubsystem::IsFightPreloadDone() const
{
	return !PreloadHandle.IsValid() || PreloadHandle->HasLoadCompleted() || PreloadHandle->WasCanceled();
}

float UBoxingGameInstanceSubsystem::GetFightPreloadProgress() const
{
	return PreloadHandle.IsValid() ? PreloadHandle->GetProgress() : 1.f;
}

FString UBoxingGameInstanceSubsystem::LoadingRedName() const
{
	const FRosterBoxer* R = FindById(Exhibition.RedId);
	return R ? R->Name : TEXT("Красный угол");
}

FString UBoxingGameInstanceSubsystem::LoadingBlueName() const
{
	const FRosterBoxer* B = FindById(Exhibition.BlueId);
	return B ? B->Name : TEXT("Синий угол");
}

FString UBoxingGameInstanceSubsystem::LoadingInfo() const
{
	const FRosterBoxer* R = FindById(Exhibition.RedId);
	const FRosterBoxer* B = FindById(Exhibition.BlueId);
	const int32 N = Exhibition.Rounds;
	const TCHAR* Word = (N % 10 == 1 && N % 100 != 11) ? TEXT("раунд")
		: ((N % 10 >= 2 && N % 10 <= 4 && (N % 100 < 12 || N % 100 > 14)) ? TEXT("раунда") : TEXT("раундов"));
	FString S = FString::Printf(TEXT("%s · %d %s"), Exhibition.bProRules ? TEXT("Профи") : TEXT("Любители"), N, Word);
	if (R && B)
	{
		auto Kg = [](float W) { return FMath::IsNearlyEqual(W, FMath::RoundToFloat(W)) ? FString::FromInt(FMath::RoundToInt(W)) : FString::Printf(TEXT("%.1f"), W); };
		const float Ring = RingWeightKg(*R, *B, N > 3);
		S += FString::Printf(TEXT(" · вес боя %s кг"), *Kg(Ring));
		if (!FMath::IsNearlyEqual(R->WeightKg, Ring) || !FMath::IsNearlyEqual(B->WeightKg, Ring))
		{
			S += FString::Printf(TEXT(" (%s / %s)"), *Kg(R->WeightKg), *Kg(B->WeightKg));
		}
	}
	return S;
}

FString UBoxingGameInstanceSubsystem::LoadingTip() const
{
	static const TCHAR* Tips[] = {
		TEXT("Q / E (правый стик вбок) — уклоны. Нырок по замаху соперника открывает контру: бей сразу."),
		TEXT("Пробел (LT) — блок. Руки устают: держи коротко, серия силовых пробивает блок."),
		TEXT("Shift + удар (RT + удар) — в корпус: попасть легче, а соперник быстрее выдыхается."),
		TEXT("A / D (левый стик) — к сопернику и назад, W / S — обход по дуге. Шаг вбок открывает угол для удара."),
		TEXT("Один и тот же удар подряд соперник читает. Меняй джеб, кросс, хуки и уровни."),
		TEXT("Пустой бак — удар не выходит. Отдышись: в блоке и на дистанции стамина возвращается."),
		TEXT("Нокдаун — жми удары или блок, чтобы встать до счёта «10». Esc / Start — пауза."),
	};
	return Tips[TipIndex % UE_ARRAY_COUNT(Tips)];
}

FString UBoxingGameInstanceSubsystem::LoadingStage() const
{
	if (!IsFightPreloadDone())
	{
		return FString::Printf(TEXT("Загружаем бойцов и арену… %d%%"), FMath::RoundToInt(GetFightPreloadProgress() * 100.f));
	}
	return TEXT("Выходим на ринг…");
}

void UBoxingGameInstanceSubsystem::BeginRingTransition(const UObject* WorldContext, const TCHAR* What)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (bTransitionPending || !W)
	{
		return;
	}
	bTransitionPending = true;
	bTransitionOpened = false;
	TransitionAt = FPlatformTime::Seconds();
	TransitionFrames = 0;
	TransitionWhat = What;
	TransitionWorld = W;
	TipIndex = FMath::RandRange(0, 1000);
	StartFightPreload();
	if (!IsFightPreloadDone())
	{
		SetAsyncBudget(150.f); // экран загрузки: грузим почти на полной скорости (полоса всё равно обновляется)
	}
	// Экран загрузки — сразу (он же последний кадр перед LoadMap, если ассеты уже в памяти).
	if (APlayerController* PC = W->GetFirstPlayerController())
	{
		if (UBoxingLoadingWidget* L = CreateWidget<UBoxingLoadingWidget>(PC, UBoxingLoadingWidget::StaticClass()))
		{
			L->AddToViewport(100);
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(L->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(Mode); // кнопки экрана под загрузкой больше не нажимаются
		}
	}
	UE_LOG(LogTemp, Log, TEXT("LOAD: %s — экран загрузки, предзагрузка %s (%.0f%%)"), What,
		IsFightPreloadDone() ? TEXT("готова") : TEXT("идёт"), GetFightPreloadProgress() * 100.f);
	TransitionTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBoxingGameInstanceSubsystem::TickTransition));
}

bool UBoxingGameInstanceSubsystem::TickTransition(float Dt)
{
	++TransitionFrames;
	UWorld* W = TransitionWorld.Get();
	if (!W)
	{
		bTransitionPending = false;
		TransitionTicker.Reset();
		return false;
	}
	// ≥ 2 кадров: экран загрузки успел нарисоваться (иначе LoadMap заморозил бы предыдущий кадр).
	static const bool bFlush = FParse::Param(FCommandLine::Get(), TEXT("BoxLoadFlush")); // A/B: не ждать предзагрузку, LoadMap догрузит синхронно
	if (TransitionFrames < 2 || (!bFlush && !IsFightPreloadDone()))
	{
		return true;
	}
	bTransitionOpened = true;
	UE_LOG(LogTemp, Log, TEXT("LOAD: %s — ассеты готовы через %.2f с, открываю %s"), *TransitionWhat, FPlatformTime::Seconds() - TransitionAt, RingMap);
	UGameplayStatics::OpenLevel(W, FName(RingMap));
	TransitionTicker.Reset();
	return false;
}

void UBoxingGameInstanceSubsystem::KeepLoadedAssetsResident()
{
	// LoadMap собирает мусор: всё, на что не ссылается новый мир, выгружается — и следующий бой грузил BP_Boxer,
	// базы Motion Matching, облики, текстуры заново (≈ 13 с). Держим ассеты контента, загруженные к этому моменту
	// (только верхний уровень пакетов: ассет, его класс/CDO; карты, внешние акторы и всё, что живёт в мире, — нет).
	const double T0 = FPlatformTime::Seconds();
	int32 Added = 0;
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Pkg = *It;
		if (!Pkg || Pkg == GetTransientPackage() || Pkg->HasAnyPackageFlags(PKG_ContainsMap | PKG_PlayInEditor | PKG_CompiledIn))
		{
			continue;
		}
		const FString Name = Pkg->GetName();
		if (!Name.StartsWith(TEXT("/Game/")) || Name.Contains(TEXT("__External")))
		{
			continue;
		}
		ForEachObjectWithPackage(Pkg, [this, Pkg, &Added](UObject* O)
		{
			if (O && O->GetOuter() == Pkg && !O->IsA<UWorld>() && !O->IsA<ULevel>() && !O->IsA<AActor>() && !O->IsA<UActorComponent>()
				&& !O->HasAnyFlags(RF_Transient))
			{
				bool bAlready = false;
				Resident.Add(O, &bAlready);
				Added += bAlready ? 0 : 1;
			}
			return true;
		}, false);
	}
	UE_LOG(LogTemp, Log, TEXT("LOAD: удержание ассетов между картами — +%d (всего %d) за %.0f мс"), Added, Resident.Num(),
		(FPlatformTime::Seconds() - T0) * 1000.0);
}

void UBoxingGameInstanceSubsystem::SetupMovieLoadingScreen()
{
	if (!IsMoviePlayerEnabled() || !GetMoviePlayer())
	{
		return;
	}
	BoxLoading::FTexts T;
	T.Red = LoadingRedName();
	T.Blue = LoadingBlueName();
	T.Info = LoadingInfo();
	T.Tip = LoadingTip();
	FLoadingScreenAttributes A;
	A.bAutoCompleteWhenLoadingCompletes = true;
	A.bMoviesAreSkippable = false;
	A.bWaitForManualStop = false;
	A.MinimumLoadingScreenDisplayTime = 0.f;
	A.WidgetLoadingScreen = BoxLoading::MakeSlate(T);
	GetMoviePlayer()->SetupLoadingScreen(A);
}

bool UBoxingGameInstanceSubsystem::TickRingCover(float Dt)
{
	++RingCoverFrames;
	UUserWidget* L = RingCover.Get();
	// Снять: ≥ 3 кадров и кадр быстрее 0.25 с (прогрев прошёл; на слабой машине — не позже 10-го кадра), не дольше 20 с.
	const bool bDone = !L || (RingCoverFrames >= 3 && Dt < 0.25f) || RingCoverFrames >= 10 || FPlatformTime::Seconds() - RingCoverAt > 20.0;
	if (!bDone)
	{
		return true;
	}
	if (L)
	{
		L->RemoveFromParent();
		UE_LOG(LogTemp, Log, TEXT("LOAD: экран загрузки снят — %d кадров прогрева, %.2f с после карты"), RingCoverFrames,
			FPlatformTime::Seconds() - RingCoverAt);
	}
	RingCover.Reset();
	RingCoverTicker.Reset();
	return false;
}

void UBoxingGameInstanceSubsystem::OnPreLoadMap(const FString& MapName)
{
	KeepLoadedAssetsResident();
	LoadMapAt = FPlatformTime::Seconds();
	if (MapName.Contains(TEXT("L_Ring")) && bTransitionOpened && !FParse::Param(FCommandLine::Get(), TEXT("BoxNoLoadingScreen")))
	{
		SetupMovieLoadingScreen();
	}
}

void UBoxingGameInstanceSubsystem::OnPostLoadMap(UWorld* World)
{
	const double Now = FPlatformTime::Seconds();
	const FString Map = World ? World->GetMapName() : FString();
	if (bTransitionPending)
	{
		UE_LOG(LogTemp, Log, TEXT("LOAD: %s — карта %s: LoadMap %.2f с, от нажатия %.2f с"), *TransitionWhat, *Map,
			Now - LoadMapAt, Now - TransitionAt);
		const double From = TransitionAt;
		const FString What = TransitionWhat;
		// Тот же экран загрузки поверх первых кадров боя: первый кадр на холодную — прогрев PSO/шейдеров (до ~8 с), без
		// экрана это снова «замерший кадр». Снимается, когда кадры пошли ровно (TickRingCover).
		if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			if (UBoxingLoadingWidget* L = CreateWidget<UBoxingLoadingWidget>(PC, UBoxingLoadingWidget::StaticClass()))
			{
				L->AddToViewport(100);
				RingCover = L;
				RingCoverFrames = 0;
				RingCoverAt = Now;
				RingCoverTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBoxingGameInstanceSubsystem::TickRingCover));
			}
		}
		// Первый кадр боя (после LoadMap ещё BeginPlay и первый тик).
		FirstFrameTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, From, What, Frames = 0](float) mutable
		{
			if (++Frames < 2)
			{
				return true;
			}
			UE_LOG(LogTemp, Log, TEXT("LOAD: %s — первый кадр боя через %.2f с от нажатия"), *What, FPlatformTime::Seconds() - From);
			FirstFrameTicker.Reset();
			return false;
		}));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("LOAD: карта %s — LoadMap %.2f с"), *Map, LoadMapAt > 0.0 ? Now - LoadMapAt : 0.0);
	}
	bTransitionPending = false;
	bTransitionOpened = false;
}

void UBoxingGameInstanceSubsystem::OpenMenu(const UObject* WorldContext)
{
	UGameplayStatics::OpenLevel(WorldContext, FName(MenuMap));
}

void UBoxingGameInstanceSubsystem::TakeUiShot(const FString& Name) const
{
	// Второй бой сценария (реванш) — свои файлы, меню — как есть.
	const FString Suffix = (AutoFightsDone > 0 && !Name.StartsWith(TEXT("menu"))) ? TEXT("_rematch") : TEXT("");
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") / (ShotPrefix + TEXT("_") + Name + Suffix + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogTemp, Log, TEXT("UI: скриншот %s"), *Path);
}

// ======================================================================
// Сценарий ввода (S-59): нажатия через Slate — та же навигация, что у игрока с клавиатурой/геймпадом.
// ======================================================================
void UBoxingGameInstanceSubsystem::SendKey(const FKey& Key, bool bDown) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	const FKeyEvent E(Key, FModifierKeysState(), 0, false, 0, 0);
	if (bDown)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(E);
	}
	else
	{
		FSlateApplication::Get().ProcessKeyUpEvent(E);
	}
}

bool UBoxingGameInstanceSubsystem::ScriptCondition(const FString& Step) const
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	const ABoxingFightGameMode* GM = W ? W->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	if (Step == TEXT("ring"))
	{
		return GM && GM->IsFightStarted();
	}
	if (Step.StartsWith(TEXT("ft")))
	{
		return GM && GM->IsFightStarted() && GM->GetCore().GetFightTime() >= FCString::Atod(*Step.Mid(2));
	}
	if (Step == TEXT("rp") || Step == TEXT("rpend"))
	{
		const UBoxingFightFx* Fx = W ? UBoxingFightFx::Get(W) : nullptr;
		const bool bPlaying = Fx && Fx->IsReplaying();
		return Step == TEXT("rp") ? bPlaying : !bPlaying;
	}
	if (Step == TEXT("res"))
	{
		for (TObjectIterator<UBoxingFightResultWidget> It; It; ++It)
		{
			if (It->GetWorld() == W && It->IsInViewport())
			{
				return true;
			}
		}
		return false;
	}
	if (Step == TEXT("menu"))
	{
		for (TObjectIterator<UBoxingMainMenuWidget> It; It; ++It)
		{
			if (It->GetWorld() == W && It->IsInViewport())
			{
				return true;
			}
		}
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("UI-SCRIPT: неизвестный шаг «%s» — пропуск"), *Step);
	return true;
}

bool UBoxingGameInstanceSubsystem::TickScript(float Dt)
{
	ScriptClock += Dt;
	if (ScriptKeyUp.IsValid())
	{
		SendKey(ScriptKeyUp, false);
		ScriptKeyUp = FKey();
		ScriptWaitUntil = ScriptClock + 0.35; // дать экрану перестроиться (фокус, пересборка списка)
		return true;
	}
	if (ScriptClock < ScriptWaitUntil)
	{
		return true;
	}
	if (ScriptIdx >= ScriptSteps.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("UI-SCRIPT: сценарий выполнен"));
		ScriptTicker.Reset();
		return false;
	}
	const FString Step = ScriptSteps[ScriptIdx].TrimStartAndEnd();
	if (ScriptStepAt <= 0.0)
	{
		ScriptStepAt = ScriptClock;
	}
	auto Next = [this, &Step]()
	{
		UE_LOG(LogTemp, Log, TEXT("UI-SCRIPT: [%d] %s (%.2f с)"), ScriptIdx, *Step, ScriptClock);
		++ScriptIdx;
		ScriptStepAt = 0.0;
	};
	if (Step.Len() > 1 && Step[0] == TEXT('w') && (FChar::IsDigit(Step[1]) || Step[1] == TEXT('.')))
	{
		ScriptWaitUntil = ScriptClock + FCString::Atod(*Step.Mid(1));
		Next();
	}
	else if (Step.StartsWith(TEXT("k:")))
	{
		const FKey Key(FName(*Step.Mid(2)));
		if (!Key.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("UI-SCRIPT: нет клавиши %s"), *Step.Mid(2));
		}
		else
		{
			SendKey(Key, true);
			ScriptKeyUp = Key;
		}
		Next();
	}
	else if (Step.StartsWith(TEXT("s:")))
	{
		TakeUiShot(Step.Mid(2));
		ScriptWaitUntil = ScriptClock + 0.4;
		Next();
	}
	else if (Step == TEXT("f"))
	{
		UE_LOG(LogTemp, Log, TEXT("UI-SCRIPT: фокус → %s"), *UBoxingUiWidget::DescribeFocus());
		Next();
	}
	else if (Step == TEXT("quit"))
	{
		Next();
		UE_LOG(LogTemp, Log, TEXT("UI-SCRIPT: выход"));
		FPlatformMisc::RequestExit(false, TEXT("BoxUiScript"));
		ScriptTicker.Reset();
		return false;
	}
	else if (ScriptCondition(Step))
	{
		Next();
	}
	else if (ScriptClock - ScriptStepAt > 240.0)
	{
		UE_LOG(LogTemp, Error, TEXT("UI-SCRIPT: шаг «%s» не дождался 240 с — выход"), *Step);
		FPlatformMisc::RequestExit(false, TEXT("BoxUiScript"));
		ScriptTicker.Reset();
		return false;
	}
	return true;
}
