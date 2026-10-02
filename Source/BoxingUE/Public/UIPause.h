// Меню паузы боя (S-59): Esc / Start. Продолжить, Управление (раскладка клавиатуры и геймпада), Настройки,
// Сдаться (поражение остановкой боя — RSC, как брошенный после гонга бой веба), В главное меню.
// «Сдаться» и «В главное меню» — с подтверждением (фокус по умолчанию на «Отмена»).
// Пауза мира (SetGamePaused) и переходы — у ABoxingFightHUD; здесь только экран.
#pragma once

#include "CoreMinimal.h"
#include "UIWidgetBase.h"
#include "UIPause.generated.h"

class ABoxingFightHUD;
class UButton;
class UTextBlock;
class UWidget;

UCLASS()
class BOXINGUE_API UBoxingPauseWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	void FocusFirst();
	// Вернуться к главному списку паузы (после закрытия настроек — фокус на «Настройки»).
	void ShowMain(const FString& FocusLabel = TEXT("Продолжить"));

protected:
	virtual UWidget* BuildUi() override;
	virtual bool HandleBack() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool WantsFocusRestore() const override { return !bSettingsOpen; }

private:
	enum class EMode : uint8 { Main, Controls, ConfirmSurrender, ConfirmMenu };
	void SetMode(EMode M);
	ABoxingFightHUD* Hud() const;
	void OpenSettings();

	EMode Mode = EMode::Main;
	bool bSettingsOpen = false;

	UPROPERTY(Transient) TObjectPtr<UWidget> MainPanel;
	UPROPERTY(Transient) TObjectPtr<UWidget> ControlsPanel;
	UPROPERTY(Transient) TObjectPtr<UWidget> ConfirmPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmYesLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubTitle;
	UPROPERTY(Transient) TObjectPtr<UButton> SurrenderButton;
};
