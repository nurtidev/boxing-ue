// ABoxingFightGameMode — режиссёр боя (S-41, трек B).
//
// Владеет FBoxingFightCore, тикает его ФИКСИРОВАННЫМ шагом (аккумулятор, 1/60 с — детерминизм),
// применяет ввод игрока на границе шага, спаунит двух ABoxerCharacter (0 — красный угол/игрок,
// 1 — синий/ИИ), раздаёт им снимок и события ядра.
//
// Подключение к уровню: World Settings → GameMode Override = BoxingFightGameMode, либо в URL
// карты: ?game=/Script/BoxingUE.BoxingFightGameMode. Центр ринга — актор с тегом RingCenter
// (иначе RingCenter из свойств, Z — по трассе вниз до пола).
//
// Командная строка (для headless-проверок): -BoxAutopilot (оба под ИИ), -BoxSeed=N,
// -BoxRoundSec=S, -BoxBreakSec=S, -BoxQuitAfter=S (выход через S сек реального времени),
// -BoxLogEvery=S (лог позиций/состояния), -BoxLogEvents (лог всех событий),
// -BoxShots=5,9,14 (скриншоты в Docs/screens/fight_<сек>.png), -BoxHitShots=N (скриншоты в кадре
// контакта первых N попаданий/блоков, с интервалом ≥ 1.5 с), -BoxShotPrefix=ring_fight (имя файлов),
// -BoxVisual=<класс>|none (визуальная подмена), -BoxVisualRed=/-BoxVisualBlue= (своя подмена угла),
// -BoxPhysHits=0|1 (физреакция), -BoxFeel=0 (без слоя «ощущения удара»), -BoxMinSep=СМ (мин. дистанция визуала),
// -BoxNoCorners (бой без постановки углов, S-53), -BoxStageShots (скриншоты стадий постановки: <префикс>_stage_*.png),
// -BoxBot=novice|average|strong|masher (бот «человека» за красный), -BoxBotFights=N [-BoxBotFrom=K] (серия боёв бота
// без отрисовки → сводка «BOT СВОДКА» в лог → выход), -BoxGlassJaw (синий падает на здоровье 0 и не встаёт) — S-57.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BoxingFightCore.h"
#include "BoxingFightBPTypes.h"
#include "FightBot.h"
#include "BoxingFightGameMode.generated.h"

class ABoxerCharacter;
class UMaterialParameterCollection;

UCLASS()
class BOXINGUE_API ABoxingFightGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABoxingFightGameMode();

	// ---------- Настройки боя ----------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	FBoxerPreset RedPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	FBoxerPreset BluePreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	int32 Seed = 12345;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight", meta = (ClampMin = "1", ClampMax = "12"))
	int32 Rounds = 3;

	// Баланс ядра откалиброван под 55-секундный раунд веба (см. Docs/FIGHT_CORE_PORT.md, «Упрощения» п.3).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	float RoundSeconds = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	float BreakSeconds = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	bool bAllowDraw = false;

	// Постановка раунда (S-53): старт и перерыв в своих углах, выход по гонгу, нейтральный угол на нокдауне.
	// false — раунд сразу с центра (прежний режим). Командная строка: -BoxNoCorners.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	bool bCorners = true;

	// Пауза до первого гонга (с): бойцы стоят в своих углах, ядро не шагает (сид и бой те же). -BoxPreGong=S.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	float PreGongHold = 1.0f;

	// Оба бойца под ИИ (ввод игрока игнорируется) — демо/проверка.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	bool bAutopilot = false;

	// Шаг ядра (с). 1/60 — детерминизм: один сид + один ввод по шагам → один бой.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Fight")
	float FixedStep = 1.f / 60.f;

	// ---------- Ринг и бойцы ----------

	// Класс бойца. Пусто — BoxerClassPath (BP_Boxer), а без него — нативный ABoxerCharacter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Ring")
	TSubclassOf<ABoxerCharacter> BoxerClass;

	// Визуальная подмена бойцов: MetaHuman Kellan из GASP (ретаргет позы с логического манекена).
	// Пусто — виден манекен GASP. Командная строка: -BoxVisual=<путь класса> или -BoxVisual=none.
	UPROPERTY(EditAnywhere, Category = "Boxing|Ring")
	FSoftClassPath VisualOverridePath = FSoftClassPath(TEXT("/Game/MetaHumans/Kellan/BP_Kellan.BP_Kellan_C"));

	// Физреакция на попадание (на видимом меше подмены). -1 — как в классе бойца; 0/1 — принудительно.
	UPROPERTY(EditAnywhere, Category = "Boxing|Ring")
	int32 PhysHitsOverride = -1;

	// Любительский бой (Rounds <= 3) — любительская форма угла (BP_BoxerLook_<угол>_Amateur[Elite]);
	// выключается, если облик угла задан в командной строке (-BoxVisualRed= / -BoxVisualBlue=).
	UPROPERTY(EditAnywhere, Category = "Boxing|Ring")
	bool bAutoAmateurLook = true;

	// Своя подмена для красного/синего угла (облик бойца: форма в цвет угла, tech-artist).
	// Пусто или класса нет на машине (Content/BoxingLocal вне git) — VisualOverridePath.
	// Командная строка: -BoxVisualRed=<путь класса>|none, -BoxVisualBlue=<путь класса>|none.
	UPROPERTY(EditAnywhere, Category = "Boxing|Ring")
	FSoftClassPath VisualOverridePathRed = FSoftClassPath(TEXT("/Game/BoxingLocal/Characters/BP_BoxerLook_Red.BP_BoxerLook_Red_C"));

	UPROPERTY(EditAnywhere, Category = "Boxing|Ring")
	FSoftClassPath VisualOverridePathBlue = FSoftClassPath(TEXT("/Game/BoxingLocal/Characters/BP_BoxerLook_Blue.BP_BoxerLook_Blue_C"));

	// «Ощущение удара» (слой верха, наведение кулака, реакция): -1 — как в классе бойца; 0/1 — принудительно (-BoxFeel=0|1).
	UPROPERTY(EditAnywhere, Category = "Boxing|Feel")
	int32 FeelOverride = -1;

	// Минимальная ВИЗУАЛЬНАЯ дистанция между центрами бойцов (см) для пары ростом 178 см; масштабируется
	// средним ростом пары (как VIS_MIN_SEP веба). Ядро не трогается — раздвигаются только точки слежения.
	// 0 — выкл. Командная строка: -BoxMinSep=СМ.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float VisMinSepCm = 100.f;

	UPROPERTY(EditAnywhere, Category = "Boxing|Ring")
	FSoftClassPath BoxerClassPath = FSoftClassPath(TEXT("/Game/Boxing/Blueprints/BP_Boxer.BP_Boxer_C"));

	// Центр ринга (если в уровне нет актора с тегом RingCenterTag). Z уточняется трассой до пола.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Ring")
	FVector RingCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Ring")
	FName RingCenterTag = TEXT("RingCenter");

	// ---------- Чтение (HUD/камера/BP) ----------

	UFUNCTION(BlueprintPure, Category = "Boxing")
	ABoxerCharacter* GetBoxer(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Boxing")
	EBoxFightPhase GetFightPhase() const { return BoxingBP::Phase(Snap.Phase); }

	UFUNCTION(BlueprintPure, Category = "Boxing")
	int32 GetRound() const { return Snap.Round; }

	UFUNCTION(BlueprintPure, Category = "Boxing")
	float GetTimeLeft() const { return Snap.TimeLeft; }

	UFUNCTION(BlueprintPure, Category = "Boxing")
	int32 GetPlayerIndex() const { return 0; }

	// Высота пола ринга (см, мир) и центр.
	UFUNCTION(BlueprintPure, Category = "Boxing")
	FVector GetRingFloorCenter() const { return RingFloor; }

	// Точка ядра (м, X/Z) → мир (см).
	FVector FightToWorld(float X, float Z) const;

	const FFightSnapshot& GetSnapshot() const { return Snap; }
	const FBoxingFightCore& GetCore() const { return Core; }
	bool IsFightStarted() const { return bStarted; }
	bool IsAutopilot() const { return bAutopilot; }
	// Текст итога для HUD (пусто, пока бой идёт).
	FString GetResultText() const;

	// ---------- Ввод (из PlayerController) ----------
	// Действие применяется к ядру на границе следующего фиксированного шага (детерминизм).
	void QueueAction(EFightAction Action, EPunchTarget Target = EPunchTarget::Head);
	// Удерживаемый шаг ног (повторяется каждый шаг ядра; ядро само держит кулдаун). None — отпущено.
	void SetHeldStep(EFightAction Action, bool bHeld);
	// Сдача игрока (меню паузы, S-59): поражение RSC в текущем раунде, события раздаются сразу.
	void Surrender();
	bool WasSurrendered() const { return bSurrendered; }
	// S-71 (ux): каждое событие ядра — подписчикам UI (копилка «совета угла» в перерыве). Только чтение, ГСЧ не трогает.
	TMulticastDelegate<void(const FFightEvent&)> OnFightEventUi;

	// Оформление арены по типу боя (S-64): акторы L_Ring с тегом ArenaAmateur / ArenaPro.
	UFUNCTION(BlueprintCallable, Category = "Boxing")
	void ApplyArenaDress(bool bPro);

protected:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// Реакция зала (S-77): параметр Excite в MPC_Crowd, 0 — сидят, 1 — встают.
	void CrowdPeak(float Level, float HoldSeconds);
	void UpdateCrowd(float DeltaSeconds);
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CrowdMpc;
	float CrowdExcite = 0.f;
	float CrowdTarget = 0.f;
	float CrowdHold = 0.f;
	float CrowdSent = -1.f;

	void ReadCommandLine();
	void LocateRing();
	void StartFight();
	void StepCore();
	// S-57: бот «человека» за красный угол (-BoxBot=novice|average|strong|masher) — жмёт через QueueAction/SetHeldStep,
	// как клавиатура; -BoxBotFights=N — N боёв подряд без отрисовки, сводка в лог (LogTemp «BOT …»), затем выход.
	void BotThink();
	void RunBotBatch();
	bool bBot = false;
	EFightBotSkill BotSkill = EFightBotSkill::Average;
	FFightBot Bot;
	bool bBotHeld = false;
	int32 BotFights = 0;
	int32 BotFrom = 0;
	bool bGlassJaw = false; // -BoxGlassJaw: синий — «груша» с подбородком 1, падает на здоровье 0 и не встаёт (web ?ko=1)
	void DispatchEvents(TArray<FFightEvent>&& Events);
	void PushStateToBoxers(float DeltaSeconds);
	void CheckFeelContacts();
	int32 PendingFeelCheck[2] = {0, 0};
	void DebugLog(float DeltaSeconds);

	FBoxingFightCore Core;
	FFightSnapshot Snap;
	bool bStarted = false;
	double Accum = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<ABoxerCharacter> RedBoxer;

	UPROPERTY(Transient)
	TObjectPtr<ABoxerCharacter> BlueBoxer;

	FVector RingFloor = FVector::ZeroVector;

	struct FQueued
	{
		EFightAction Action;
		EPunchTarget Target;
	};
	TArray<FQueued> Pending;
	bool bHasHeldStep = false;
	bool bSurrendered = false;
	EFightAction HeldStep = EFightAction::StepFwd;

	// Отладка / headless.
	float QuitAfter = -1.f;
	float LogEvery = -1.f;
	bool bLogEvents = false;
	TArray<float> ShotTimes;
	int32 HitShotsLeft = 0;
	float LastHitShotAt = -100.f;
	// -BoxHitShotDelay=0,0.06,0.12: серия снимков через эти сек после контакта (видно реакцию); пусто — один снимок в контакте.
	TArray<float> HitShotDelays;
	TArray<TPair<FString, float>> DelayedShots;
	int32 HitShotIndex = 0;
	FString ShotPrefix = TEXT("fight");
	void TakeShot(const FString& Name);
	float RealTime = 0.f;
	float LogTimer = 0.f;
	int32 EventCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	int32 MaxTrackErrorCm = 0;
	// Метрики «ощущения»: минимум дистанции торсов (spine_05) и голов видимых мешей, зазор кулака в контакте.
	float MinChestSepCm = 1e6f;
	float MinHeadSepCm = 1e6f;
	int32 FeelContacts = 0;
	float FeelGapAbsSum = 0.f;
	float FeelGapMaxAbs = 0.f;
	float FeelLungeMax = 0.f;
	// S-62: засчитанные попадания, где путь кулака (локоть → фронт) проходит сквозь перчатку защиты (ближе 16 см к её центру).
	int32 FeelHits = 0;
	int32 FeelHitsThroughGlove = 0;
	float FeelGloveMinCm = 1e6f;
	int32 SepPushes = 0;
	FSoftClassPath VisualPathFor(int32 Index) const;
	// Постановка (S-53): лог смены стадий и скриншоты стадий (-BoxStageShots).
	void DebugStage();
	bool bStageShots = false;
	ERingStageKind LoggedStage = ERingStageKind::None;
	float StageArrivedAt = -1.f;
	int32 StageShotsTaken[5] = {0, 0, 0, 0, 0};
	float PreGongLeft = 0.f; // осталось паузы до первого гонга (PreGongHold, только с углами)
};
