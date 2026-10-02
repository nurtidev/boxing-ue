// Главный экран (S-55): «Выставка», «Быстрый бой», «Выход»; Карьера — заглушка «скоро».
#pragma once

#include "CoreMinimal.h"
#include "UIWidgetBase.h"
#include "UIMainMenu.generated.h"

class UButton;

UCLASS()
class BOXINGUE_API UBoxingMainMenuWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	void FocusFirst();

protected:
	virtual UWidget* BuildUi() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void OpenExhibition();
	void QuickFight();

	UPROPERTY(Transient)
	TObjectPtr<UButton> FirstButton;

	float AutoTime = 0.f;
	int32 AutoStep = 0;
};
