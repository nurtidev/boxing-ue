// UBoxingGameInstanceSubsystem — оболочка игры (S-55): ростер, выбранная пара Выставки, сценарий UI-проверки.
//
// Ростер — Content/Boxing/Data/Roster.json, выгрузка из web (Tools/RosterExport/export.mjs): статы ВЫВЕДЕНЫ
// движком web (stats.ts/boxer.ts) из реальных регалий, здесь только читаются.
//
// Передача пары в бой: меню кладёт FExhibitionSetup (SetExhibition) и открывает L_Ring; ABoxingFightGameMode
// в InitGame зовёт ApplyToFightMode — при наличии выбора пресеты/раунды/сид берутся отсюда, иначе свои.
// Без меню (L_Ring напрямую): -BoxPickRed=<имя> -BoxPickBlue=<имя> [-BoxPickRounds=N] — тот же выбор.
//
// Сценарий проверки (headless): -BoxUiAuto [-BoxUiTab=amateur|pro|legend] [-BoxUiGender=M|F]
// [-BoxUiPick=<часть имени>,<часть имени>] [-BoxUiRounds=N] [-BoxUiShotPrefix=ui] — меню → Выставка → выбор →
// бой (с -BoxAutopilot) → итог; скриншоты Docs/screens/<prefix>_*.png, выход после итога.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BoxingFightBPTypes.h"
#include "BoxingGameInstanceSubsystem.generated.h"

class ABoxingFightGameMode;

UENUM(BlueprintType)
enum class ERosterKind : uint8
{
	Amateur,
	Pro,
	Legend,
};

// Индексы статов в FRosterBoxer::Stats/ProStats (порядок STAT_NAMES веба).
namespace BoxStat
{
	enum : int32 { Power, HandSpeed, Footwork, Stamina, Chin, Technique, Defense, Num };
	BOXINGUE_API const TCHAR* Label(int32 I);
}

// Боец ростера — строка Roster.json.
USTRUCT(BlueprintType)
struct FRosterBoxer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") ERosterKind Kind = ERosterKind::Amateur;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") bool bFemale = false;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString CountryCode;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Country;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString City;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") int32 Age = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") float WeightKg = 70.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Division;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") float HeightCm = 178.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") float ReachCm = 183.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Stance;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") EBoxerStyle Style = EBoxerStyle::Balanced;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString StyleLabel;
	// 7 выведенных статов 0..100 (BoxStat::*): любительский бой / бой своего уровня.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") TArray<float> Stats;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") float Overall = 0.f;
	// Профи-проекция (fightProfile(вес, pro=true)): у любителя — скидка на необстрелянность, у профи = Stats.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") TArray<float> ProStats;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") float ProOverall = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") float ProSeasoning = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Badge;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString ProRecord;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Accolades;
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Roster") FString Notes;

	// Чемпион мира — настоящие пояса WBC/WBA/IBF/WBO в бейдже без ранга/приставок (isWorldChampion веба).
	bool IsWorldChampion() const;
	// Пресет для GameMode: в профи-бою (раундов > 3) — профи-проекция и обстрелянность.
	FBoxerPreset ToPreset(bool bProFight) const;
	// «Казахстан · Астана» / «США».
	FString Origin() const;
};

// Что выбрано в Выставке.
USTRUCT(BlueprintType)
struct FExhibitionSetup
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") bool bValid = false;
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") FString RedId;
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") FString BlueId;
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") int32 Rounds = 3;
	// Профи-правила: ничья возможна, профи-проекция статов (как proStyle/allowDraw веба).
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") bool bProRules = false;
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") int32 Seed = 1;
	// Вкладка/пол, с которых сделан выбор (вернуться в Выставку на то же место).
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") ERosterKind Tab = ERosterKind::Amateur;
	UPROPERTY(BlueprintReadWrite, Category = "Boxing|Exhibition") bool bFemale = false;
};

UCLASS()
class BOXINGUE_API UBoxingGameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UBoxingGameInstanceSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// Карты оболочки.
	static constexpr const TCHAR* MenuMap = TEXT("/Game/Boxing/Maps/L_Menu");
	static constexpr const TCHAR* RingMap = TEXT("/Game/Boxing/Maps/L_Ring");

	// ---------- Ростер ----------
	const TArray<FRosterBoxer>& GetRoster() const { return Roster; }
	const FRosterBoxer* FindById(const FString& Id) const;
	// Поиск по части имени (без регистра); Kind < 0 — любой.
	const FRosterBoxer* FindByName(const FString& Part, int32 Kind = -1, int32 Female = -1) const;
	bool IsRosterLoaded() const { return Roster.Num() > 0; }
	const FString& GetRosterError() const { return RosterError; }

	// ---------- Выставка ----------
	const FExhibitionSetup& GetExhibition() const { return Exhibition; }
	void SetExhibition(const FExhibitionSetup& Setup) { Exhibition = Setup; }
	// Авто-раунды профи: чемпион vs чемпион → 12, иначе 10 (autoProRounds веба).
	static int32 AutoProRounds(const FRosterBoxer& A, const FRosterBoxer& B);
	// Пресеты/раунды/сид выбранной пары → GameMode (зовётся из InitGame). Нет выбора — ничего не меняет.
	bool ApplyToFightMode(ABoxingFightGameMode& Mode) const;

	// Переходы.
	void StartExhibitionFight(const UObject* WorldContext);   // открыть L_Ring с текущим выбором
	void Rematch(const UObject* WorldContext);                // тот же выбор, новый сид
	void OpenMenu(const UObject* WorldContext);
	// Сразу открыть экран Выставки при входе в меню (после «В меню» из итога — нет: главный экран).
	bool bOpenExhibitionOnMenu = false;

	// ---------- Сценарий UI-проверки ----------
	bool bAuto = false;
	FString AutoTab;          // amateur|pro|legend
	FString AutoGender;       // M|F
	TArray<FString> AutoPick; // части имён красного и синего
	int32 AutoRounds = 0;
	FString ShotPrefix = TEXT("ui");
	int32 AutoFightsDone = 0;
	// -BoxUiRematch: после первого итога — «Реванш» (второй бой), снимки второго боя с суффиксом _rematch.
	bool bAutoRematch = false;
	void TakeUiShot(const FString& Name) const;

private:
	void LoadRoster();
	void ReadPickFromCommandLine();

	TArray<FRosterBoxer> Roster;
	TMap<FString, int32> ById;
	FString RosterError;
	FExhibitionSetup Exhibition;
};
