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
		if (B.Id.IsEmpty() || ById.Contains(B.Id))
		{
			continue;
		}
		ById.Add(B.Id, Roster.Num());
		Roster.Add(MoveTemp(B));
	}
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
	UE_LOG(LogTemp, Log, TEXT("UI: бой из Выставки — %s (%.1f) vs %s (%.1f), %d р., %s, сид %d"), *R->Name,
		bProFight ? R->ProOverall : R->Overall, *B->Name, bProFight ? B->ProOverall : B->Overall, Mode.Rounds,
		Exhibition.bProRules ? TEXT("профи") : TEXT("любители"), Mode.Seed);
	return true;
}

void UBoxingGameInstanceSubsystem::StartExhibitionFight(const UObject* WorldContext)
{
	if (!Exhibition.bValid)
	{
		return;
	}
	UGameplayStatics::OpenLevel(WorldContext, FName(RingMap));
}

void UBoxingGameInstanceSubsystem::Rematch(const UObject* WorldContext)
{
	Exhibition.Seed = FMath::Rand();
	UGameplayStatics::OpenLevel(WorldContext, FName(RingMap));
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
