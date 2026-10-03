// Панель перерыва в бою (S-71) — порт .ifight-rest веба: карты судей за раунд и всего (любители — 5 судей, профи — 3),
// статистика раунда (попадания/удары, в корпус, нокдауны), «совет угла» (CornerAdvice: тренер + катмен, с родом),
// полоса дыхания в углу «7 → 35» (ядро набирает отдых за 3 с), кнопка «Продолжить» (Enter / A) — активна, когда игрок
// сел в углу (ABoxerCharacter::IsSeatedInCorner от game-feel, через рефлексию; без флага — дошёл до угла + 1 с, страховка 6 с).
// Сбоку справа (по вертикали — середина), сцена угла слева от панели не перекрывается. В автопилоте — без советов и кнопки,
// только «Раунд N через X с» (авто-переход ядра).
#pragma once

#include "CoreMinimal.h"
#include "CornerAdvice.h"
#include "UIWidgetBase.h"
#include "UIBreakPanel.generated.h"

class ABoxingFightGameMode;
class UBorder;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UWidget;

UCLASS()
class BOXINGUE_API UBoxingBreakPanelWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	// Каждый кадр боя (копилка раунда идёт всегда; панель видна только в перерыве). bHuman — есть игрок (не автопилот).
	void UpdateFrom(ABoxingFightGameMode& GM, float Dt, bool bHuman);
	// В перерыве и игрок сел (или сработала страховка) — «Продолжить» доступна.
	bool CanProceed() const { return bInBreak && bReady; }
	bool IsInBreak() const { return bInBreak; }
	bool IsReady() const { return bReady; }
	float BreakTime() const { return BreakT; }

protected:
	virtual UWidget* BuildUi() override;
	virtual void NativeDestruct() override;
	virtual bool WantsFocusRestore() const override { return false; }

private:
	void Bind(ABoxingFightGameMode& GM);
	void EnterBreak(ABoxingFightGameMode& GM, bool bHuman);
	bool PlayerSeated(const ABoxingFightGameMode& GM) const;
	static void SetText(UTextBlock* T, const FString& S);

	CornerAdvice::FCornerTally Tally;
	TWeakObjectPtr<ABoxingFightGameMode> BoundGM;
	FDelegateHandle EventHandle;
	FJudgeCard PrevTotals[MAX_JUDGES];
	int32 BreakRound = -1;
	bool bInBreak = false;
	bool bReady = false;
	bool bHumanBreak = false;
	float BreakT = 0.f;
	float ArrivedT = -1.f;
	float StartStam = 0.f;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> RoundText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VerdictText;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> JudgeHead;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> JudgeRound;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> JudgeTotal;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> StatsBox;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> BreathGain;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> BreathNow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BreathNum;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BreathNote;
	UPROPERTY(Transient) TObjectPtr<UWidget> TipsSection;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> TipsBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> ButtonBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ButtonText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ButtonKeys;
	UPROPERTY(Transient) TObjectPtr<UWidget> WaitDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FooterText;
};
