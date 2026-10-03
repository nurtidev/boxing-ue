// HUD боя на UMG (S-55) — замена Canvas-HUD: имена и страны, здоровье/стамина обоих, раунд и время,
// нокдаун со счётом и подъёмом, перерыв, подсказки по ситуации, легенда управления (клавиатура + геймпад,
// F1 — скрыть/показать). Данные — снимок ядра из ABoxingFightGameMode, каждый кадр.
#pragma once

#include "CoreMinimal.h"
#include "UIWidgetBase.h"
#include "UIFightHud.generated.h"

class ABoxingFightGameMode;
class UBoxingBreakPanelWidget;
class UBorder;
class UProgressBar;
class UTextBlock;
class UWidget;

UCLASS()
class BOXINGUE_API UBoxingFightHudWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	void SetControlsVisible(bool bVisible);
	bool AreControlsVisible() const { return bControls; }
	// S-71: панель перерыва (карты судей, статистика, совет угла, «Продолжить»).
	UBoxingBreakPanelWidget* GetBreakPanel() const { return BreakPanel; }

protected:
	virtual UWidget* BuildUi() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	struct FPanel
	{
		UTextBlock* Name = nullptr;
		UTextBlock* Origin = nullptr;
		UProgressBar* Health = nullptr;
		UTextBlock* HealthNum = nullptr;
		UProgressBar* Stamina = nullptr;
		UTextBlock* Info = nullptr;
	};
	UWidget* MakePanel(int32 Index);
	void UpdatePanel(int32 Index, const ABoxingFightGameMode& GM, float Time);
	static void SetText(UTextBlock* T, const FString& S);

	FPanel Panels[2];
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RoundText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClockText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UWidget> Banner;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BannerTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BannerBig;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BannerSub;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> RiseBar;
	UPROPERTY(Transient) TObjectPtr<UWidget> CueBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CueText;
	UPROPERTY(Transient) TObjectPtr<UWidget> Controls;
	UPROPERTY(Transient) TObjectPtr<UWidget> AutoBadge;
	UPROPERTY(Transient) TObjectPtr<UBoxingBreakPanelWidget> BreakPanel;
	// Сценарий проверки (-BoxUiAuto): снимки перерыва и «Продолжить» за игрока.
	int32 ShotBreakRound = 0;
	bool bShotBreakWait = false;
	bool bShotBreakReady = false;
	float ReadyTime = 0.f;
	// Повтор нокаута (S-59): плашка «ПОВТОР» и подсказка пропуска; панели/часы/баннер на это время скрыты.
	UPROPERTY(Transient) TObjectPtr<UWidget> PanelRoot0;
	UPROPERTY(Transient) TObjectPtr<UWidget> PanelRoot1;
	UPROPERTY(Transient) TObjectPtr<UWidget> MidBox;
	UPROPERTY(Transient) TObjectPtr<UWidget> ReplayBadge;
	UPROPERTY(Transient) TObjectPtr<UWidget> ReplayHint;
	bool bReplayShown = false;
	bool bShotReplay = false;
	float ReplayTime = 0.f;

	bool bNamesSet = false;
	bool bControls = true;
	float Clock = 0.f;
	// Сценарий проверки (-BoxUiAuto): снимки боя.
	float FightTime = 0.f;
	// S-63: время в фазе нокдауна — баннер «на настиле» ждёт, пока соперник ляжет (BoxerCharacter::IsFloored).
	float DownPhaseTime = 0.f;
	bool bShotHud = false;
	bool bShotKd = false;
	bool bShotBreak = false;
	float DownTime = 0.f;
	float BreakTime = 0.f;
};
