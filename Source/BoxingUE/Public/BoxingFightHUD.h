// ABoxingFightHUD — HUD боя (S-55): ведёт UMG-виджеты UBoxingFightHudWidget (бой) и UBoxingFightResultWidget
// (итог: через ResultDelay после конца боя — дать доиграть нокаут/победу; ввод переключается на UI, курсор).
// F1 — скрыть/показать легенду управления. Esc / Start — меню паузы (UBoxingPauseWidget, S-59). Canvas-HUD S-41 остался запасным: -BoxCanvasHud или нет виджета.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "BoxingFightHUD.generated.h"

class UBoxingFightHudWidget;
class UBoxingFightResultWidget;
class UBoxingPauseWidget;

UCLASS()
class BOXINGUE_API ABoxingFightHUD : public AHUD
{
	GENERATED_BODY()

public:
	ABoxingFightHUD();

	virtual void DrawHUD() override;
	virtual void Tick(float DeltaSeconds) override;

	// Пауза между концом боя и экраном итога (с, реальное время).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|HUD")
	float ResultDelay = 3.5f;

	// ---------- Пауза (S-59): Esc / Start в бою ----------
	// Мир на паузе (SetGamePaused): GameMode не тикает — ядро не шагает, аккумулятор шага не копится, поэтому
	// поток событий боя с паузой тот же, что без неё. Ввод — только UI.
	void OpenPause();
	void ClosePause();
	bool IsPauseOpen() const { return PauseWidget != nullptr; }
	// S-62 (game-feel): панель итога открыта — камера ставит кадр итога (победитель слева от панели).
	bool IsResultOpen() const { return ResultWidget != nullptr; }
	// Сдача: поражение RSC (ABoxingFightGameMode::Surrender), итог — сразу после короткой паузы.
	void SurrenderFromPause();
	void ExitToMenu();

protected:
	virtual void BeginPlay() override;

private:
	void ShowResult();

	UPROPERTY(Transient)
	TObjectPtr<UBoxingFightHudWidget> HudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UBoxingFightResultWidget> ResultWidget;

	UPROPERTY(Transient)
	TObjectPtr<UBoxingPauseWidget> PauseWidget;

	// Повтор нокаута (S-54): любая кнопка — пропустить; итог ждёт конца повтора.
	void UpdateReplayInput();
	TArray<FKey> AllKeys;

	bool bCanvasHud = false;
	float OverTime = 0.f;

	// Запасной Canvas-HUD (S-41).
	void DrawCanvasHud();
	void DrawFighterPanel(int32 Index, float X, float Y, float W, bool bRightAligned);
	void DrawBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, bool bRightAligned);
	void DrawCenteredText(const FString& Text, float CY, const FLinearColor& Color, float Scale);
};
