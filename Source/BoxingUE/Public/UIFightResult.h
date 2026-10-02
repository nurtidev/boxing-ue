// Экран итога боя (S-55): победитель, метод (нокаут/RSC/решение и его вид), карты трёх судей по раундам
// и итог, нокдауны; «Реванш» (та же пара, новый сид) и «В меню».
#pragma once

#include "CoreMinimal.h"
#include "UIWidgetBase.h"
#include "UIFightResult.generated.h"

class UButton;

UCLASS()
class BOXINGUE_API UBoxingFightResultWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	void FocusFirst();

protected:
	virtual UWidget* BuildUi() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void Rematch();
	void ToMenu();

	UPROPERTY(Transient)
	TObjectPtr<UButton> RematchButton;

	float AutoTime = 0.f;
	int32 AutoStep = 0;
};
