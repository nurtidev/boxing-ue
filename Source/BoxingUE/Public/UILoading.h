// Экран загрузки боя (S-63): «В бой» из Выставки / «Быстрый бой» / «Реванш».
//
// Два слоя одного вида:
//  * UBoxingLoadingWidget — UMG поверх меню/итога, пока ассеты боя догружаются асинхронно (мир живой, полоса прогресса
//    честная — доля загруженных пакетов предзагрузки);
//  * BoxLoading::MakeSlate — тот же экран на чистом Slate для MoviePlayer: на время самого LoadMap игровой поток занят,
//    а MoviePlayer рисует экран своим потоком (крутится индикатор, кадр не замирает). Без UObject — старый мир
//    собирается сборщиком мусора, пока экран на экране.
//
// Содержимое: углы и имена пары, детали боя (любители/профи, раунды, вес), стадия загрузки, подсказка управления
// (клавиатура + геймпад, по кругу).
#pragma once

#include "CoreMinimal.h"
#include "UIWidgetBase.h"
#include "UILoading.generated.h"

class SWidget;
class UProgressBar;
class UTextBlock;

namespace BoxLoading
{
	struct FTexts
	{
		FString Red;
		FString Blue;
		FString Info;
		FString Tip;
	};
	// Экран для MoviePlayer (стадия — «Выходим на ринг…», индикатор вместо полосы).
	TSharedRef<SWidget> MakeSlate(const FTexts& T);
}

UCLASS()
class BOXINGUE_API UBoxingLoadingWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

protected:
	virtual UWidget* BuildUi() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool WantsFocusRestore() const override { return false; }

private:
	UPROPERTY(Transient) TObjectPtr<UProgressBar> Progress;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Stage;
	float Shown = 0.f; // сглаженный прогресс (полоса не дёргается назад)
};
