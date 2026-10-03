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
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "Engine/StreamableManager.h"
#include "FightProfile.h"
#include <atomic>
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

// Прогноз пары для экрана Выставки (S-63 ← S-61): вес боя, сгонка/набор каждого, «честный» шанс исхода ядром.
struct FPairForecast
{
	bool bValid = false;
	float RingKg = 0.f;      // вес боя (кэтчвейт, привязанный к реальной категории)
	float RedDelta = 0.f;    // натуральный − вес боя: > 0 сгонка, < 0 переход вверх
	float BlueDelta = 0.f;
	float Stretch = 0.f;     // WeightStretch: ≤ 4 реальный бой, ≤ 8 кэтчвейт, ≤ 14 бой мечты, дальше — фэнтези
	BoxingFightProfile::FOutcomeOdds Odds;   // ИИ против ИИ — «кто сильнее по ядру» (нейтральный взгляд)
};

// «Шансы при твоей игре» (S-65): красного (игрока) ведёт бот «человека» трёх уровней — см. BoxingFightProfile::PredictForPlayer.
struct FPlayerForecast
{
	bool bValid = false;
	BoxingFightProfile::FPlayerOdds Odds;    // Novice / Average («обычная игра») / Strong; RedWin — шанс игрока
	float Ms = 0.f;                          // сколько считалось (фон)
};
// Фоновый расчёт FPlayerForecast (внутреннее: поток пула пишет Result, затем bDone).
struct FPlayerForecastJob
{
	std::atomic<bool> bDone{false};
	FPlayerForecast Result;
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
	virtual void Deinitialize() override;

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
	// Вес боя пары (App.tsx веба): середина пары → ближайшая реальная категория — любители WEIGHT_CLASSES, профи/легенды —
	// веса дивизионов профи-ростера своего пола.
	float RingWeightKg(const FRosterBoxer& Red, const FRosterBoxer& Blue, bool bProFight) const;
	// Прогноз пары (кэш по паре/раундам; первый расчёт ≈ 50–70 мс — 60 боёв ядра, BoxingFightProfile::PredictOutcome).
	const FPairForecast& ForecastPair(const FRosterBoxer& Red, const FRosterBoxer& Blue, int32 Rounds, bool bProRules) const;
	// «Шансы при твоей игре» (S-65): готовый прогноз из кэша или nullptr — тогда (один раз на ключ) запускает расчёт в пуле
	// потоков (≈ 0.3 с на 3 р., ≈ 1 с на 10 р.) и вернёт результат на следующих вызовах. Звать можно каждый тик UI.
	const FPlayerForecast* PlayerForecast(const FRosterBoxer& Red, const FRosterBoxer& Blue, int32 Rounds, bool bProRules) const;
	// Пресеты/раунды/сид выбранной пары → GameMode (зовётся из InitGame). Нет выбора — ничего не меняет.
	bool ApplyToFightMode(ABoxingFightGameMode& Mode) const;

	// Переходы.
	void StartExhibitionFight(const UObject* WorldContext);   // открыть L_Ring с текущим выбором
	void Rematch(const UObject* WorldContext);                // тот же выбор, новый сид
	void OpenMenu(const UObject* WorldContext);
	// Сразу открыть экран Выставки при входе в меню (после «В меню» из итога — нет: главный экран).
	bool bOpenExhibitionOnMenu = false;

	// ---------- Загрузка боя (S-63) ----------
	// Было: каждый «В бой»/«Реванш» — 12.7–16.1 с замершего кадра (LoadMap L_Ring синхронно грузил BP_Boxer с базами
	// Motion Matching, облики, грумы; после боя LoadMap собирал мусор — и следующий бой грузил всё заново).
	// Стало: (1) ассеты боя подгружаются АСИНХРОННО ещё в меню (StartFightPreload: меню живое, арена крутится);
	// (2) всё загруженное из контента удерживается между картами (KeepLoadedAssetsResident на PreLoadMap) — реванш и
	// повторный бой не грузят заново; (3) экран загрузки: UMG-виджет поверх меню/итога, пока догружается, и Slate-экран
	// MoviePlayer на сам LoadMap (рисуется своим потоком — кадр не замирает).
	void StartFightPreload();
	// S-67: холодный старт — сначала арена меню (фон), потом предзагрузка боя: оба шли одной очередью асинхронной загрузки
	// (предзагрузка — с высоким приоритетом), и арена появлялась за меню лишь через ~18 с. Предзагрузка стартует, когда
	// фон показан (не позже 8 с). Bg == nullptr — сразу.
	void StartFightPreloadAfter(class ULevelStreaming* Bg);
	bool IsFightPreloadDone() const;
	// S-67: разрешение рендера по умолчанию (пока игрок не выбрал своё): высокое/эпик — 100 % (≤ 1440p внутренних), среднее 85, низкое 70.
	static float DefaultRenderScaleFor(const class UGameUserSettings* G);
	float GetFightPreloadProgress() const;
	bool IsTransitionPending() const { return bTransitionPending; }
	// Тексты экрана загрузки: имена углов, детали боя, подсказка управления, стадия.
	FString LoadingRedName() const;
	FString LoadingBlueName() const;
	FString LoadingInfo() const;
	FString LoadingTip() const;
	FString LoadingStage() const;

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

	// ---------- Сценарий ввода (S-59) ----------
	// -BoxUiScript=<шаги через запятую>: нажатия идут через FSlateApplication (как от клавиатуры/геймпада), поэтому
	// проверяется настоящая навигация. Шаги: w<с> — ждать; k:<FKey> — нажать и отпустить (Escape, Enter, Down,
	// Gamepad_DPad_Down, Gamepad_FaceButton_Bottom, Gamepad_Special_Right…); s:<имя> — скриншот <prefix>_<имя>.png;
	// f — в лог, какая кнопка в фокусе; ring — ждать начала боя; ft<с> — ждать время ядра; res — ждать экран итога;
	// menu — ждать главный экран; rp / rpend — ждать начала / конца повтора нокаута; quit — выход.
	bool bScript = false;

private:
	// Переход на карту боя: экран загрузки сразу, OpenLevel — когда предзагрузка готова.
	void BeginRingTransition(const UObject* WorldContext, const TCHAR* What);
	bool TickTransition(float Dt);
	void OnPreLoadMap(const FString& MapName);
	void OnPostLoadMap(UWorld* World);
	void KeepLoadedAssetsResident();
	void SetupMovieLoadingScreen();
	// Бюджет асинхронной загрузки за кадр (s.AsyncLoadingTimeLimit, мс): в меню — умеренный (меню живое), на экране
	// загрузки — большой (грузим почти на полной скорости, полоса прогресса всё равно рисуется).
	void SetAsyncBudget(float Ms);
	float DefaultAsyncBudget = -1.f;
	// Первые кадры боя (прогрев PSO/шейдеров MetaHuman — первый кадр до ~8 с на холодную) — под тем же экраном загрузки.
	bool TickRingCover(float Dt);
	TWeakObjectPtr<UUserWidget> RingCover;
	int32 RingCoverFrames = 0;
	double RingCoverAt = 0.0;
	FTSTicker::FDelegateHandle RingCoverTicker;

	FStreamableManager Streamable;
	// S-67: предзагрузка — очередь кусков (ядро боя → облики по одному → прочее), в полёте один кусок. По «В бой» до конца
	// очереди догружается только пара (её облики, рефери, грумы и кожа по Appearance.json), остаток очереди отбрасывается
	// (LoadMap ждёт лишь кусок в полёте); недогруженное доберёт следующий вход в меню.
	struct FPreloadChunk
	{
		FString Name;
		TArray<FSoftObjectPath> Paths;
		bool bCore = false; // нужен любому бою: «В бой» его не отбрасывает
	};
	TArray<FPreloadChunk> PreloadQueue;
	int32 PreloadQueueTotal = 0;   // ассетов в очереди при старте (для прогресса)
	int32 PreloadQueueDone = 0;    // из них — в завершённых кусках
	FString PreloadChunkName;
	double PreloadChunkAt = 0.0;
	int32 PreloadChunkSize = 0;
	void StartNextPreloadChunk();
	void RequestPairOnly();
	TArray<FSoftObjectPath> PairAssetPaths() const;
	TSharedPtr<FStreamableHandle> PairHandle;
	int32 PairRequested = 0;
	TWeakObjectPtr<class ULevelStreaming> MenuBg;
	double MenuBgAt = 0.0;
	FTSTicker::FDelegateHandle MenuBgTicker;
	TSharedPtr<FStreamableHandle> PreloadHandle;   // кусок в полёте
	TArray<TSharedPtr<FStreamableHandle>> PreloadKeep; // все запрошенные куски: держат загруженное от сборки мусора в меню
	FTSTicker::FDelegateHandle PreloadTicker;
	bool bPreloadPartial = false;  // «В бой» отложил часть очереди — следующий StartFightPreload её доберёт
	bool TickPreload(float Dt);
	int32 PreloadRequested = 0;
	double PreloadStartAt = 0.0;
	double PreloadDoneAt = 0.0;
	// Удержание загруженного между картами (только ассеты контента: не мир, не акторы).
	UPROPERTY(Transient)
	TSet<TObjectPtr<UObject>> Resident;
	bool bTransitionPending = false;
	bool bTransitionOpened = false;
	double TransitionAt = 0.0;
	double LoadMapAt = 0.0;
	int32 TransitionFrames = 0;
	FString TransitionWhat;
	TWeakObjectPtr<UWorld> TransitionWorld;
	FTSTicker::FDelegateHandle TransitionTicker;
	FTSTicker::FDelegateHandle FirstFrameTicker;
	int32 TipIndex = 0;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;

	void ApplyDefaultRenderScale();
	void SetWorldRendering(UWorld* W, bool bOn);
	void LoadRoster();
	void ReadPickFromCommandLine();
	bool TickScript(float Dt);
	bool ScriptCondition(const FString& Step) const;
	void SendKey(const FKey& Key, bool bDown) const;
	TArray<FString> ScriptSteps;
	int32 ScriptIdx = 0;
	double ScriptClock = 0.0;
	double ScriptWaitUntil = 0.0;
	double ScriptStepAt = 0.0;
	FKey ScriptKeyUp;
	FTSTicker::FDelegateHandle ScriptTicker;

	TArray<FRosterBoxer> Roster;
	mutable TMap<FString, FPairForecast> ForecastCache;
	mutable TMap<FString, TSharedPtr<FPlayerForecastJob, ESPMode::ThreadSafe>> PlayerForecastJobs; // S-65
	TArray<double> ProClassesM;
	TArray<double> ProClassesF;
	TMap<FString, int32> ById;
	FString RosterError;
	FExhibitionSetup Exhibition;
};
