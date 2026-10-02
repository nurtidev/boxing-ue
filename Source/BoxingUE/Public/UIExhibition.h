// Экран «Выставка» (S-55, порт App.tsx веба): вкладки Любители / Профи / Легенды, пол, поиск, вес;
// список ростера по уровню; клик — в красный угол, второй — в синий (повторный клик снимает); карточки углов
// с 7 выведенными статами; раунды (любители — 3, профи — авто/4/6/8/10/12); «В бой!» → L_Ring.
#pragma once

#include "CoreMinimal.h"
#include "BoxingGameInstanceSubsystem.h"
#include "UIWidgetBase.h"
#include "UIExhibition.generated.h"

class UButton;
class UEditableTextBox;
class UHorizontalBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class BOXINGUE_API UBoxingExhibitionWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	void FocusFirst();

protected:
	virtual UWidget* BuildUi() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool HandleBack() override { Back(); return true; }

	UFUNCTION()
	void OnSearchChanged(const FText& Text);

private:
	void SetTab(ERosterKind Kind);
	void SetFemale(bool bFemale);
	void CycleWeight();
	void Pick(const FString& Id);
	void Swap();
	void Fight();
	void Back();

	void RebuildFilters();
	void RebuildList();
	void RebuildSide();
	UWidget* MakeRow(const FRosterBoxer& B);
	UWidget* MakeCard(const FRosterBoxer* B, bool bRed, bool bProFight);
	TArray<const FRosterBoxer*> Filtered() const;
	int32 EffectiveRounds() const;
	bool IsProRules() const { return Tab != ERosterKind::Amateur; }

	ERosterKind Tab = ERosterKind::Amateur;
	bool bFemale = false;
	FString Search;
	float WeightFilter = 0.f; // 0 — все
	FString RedId;
	FString BlueId;
	int32 ProRoundsSel = 0;   // 0 — авто

	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> FilterRow;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> SearchBox;
	UPROPERTY(Transient) TObjectPtr<UButton> WeightButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WeightLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> List;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Side;
	UPROPERTY(Transient) TObjectPtr<UButton> FirstTab;

	float AutoTime = 0.f;
	int32 AutoStep = 0;
};
