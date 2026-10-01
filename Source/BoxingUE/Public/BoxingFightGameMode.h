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
// -BoxVisual=<класс>|none (визуальная подмена), -BoxPhysHits=0|1 (физреакция).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BoxingFightCore.h"
#include "BoxingFightBPTypes.h"
#include "BoxingFightGameMode.generated.h"

class ABoxerCharacter;

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

protected:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ReadCommandLine();
	void LocateRing();
	void StartFight();
	void StepCore();
	void DispatchEvents(TArray<FFightEvent>&& Events);
	void PushStateToBoxers(float DeltaSeconds);
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
	EFightAction HeldStep = EFightAction::StepFwd;

	// Отладка / headless.
	float QuitAfter = -1.f;
	float LogEvery = -1.f;
	bool bLogEvents = false;
	TArray<float> ShotTimes;
	int32 HitShotsLeft = 0;
	float LastHitShotAt = -100.f;
	int32 HitShotIndex = 0;
	FString ShotPrefix = TEXT("fight");
	void TakeShot(const FString& Name);
	float RealTime = 0.f;
	float LogTimer = 0.f;
	int32 EventCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	int32 MaxTrackErrorCm = 0;
};
