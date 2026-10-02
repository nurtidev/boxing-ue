// Настройки игры (S-59): звук (общая громкость / эффекты / зал), вибрация геймпада, подсказки управления в бою,
// графика (пресет Scalability, разрешение, режим окна). Язык — пока только русский.
//
// Хранение: свои поля — секция [BoxingSettings] в GameUserSettings.ini (GConfig, как mute S-54),
// графика — штатный UGameUserSettings (Scalability, разрешение, режим окна). Всё сохраняется сразу,
// разрешение и режим окна — по «Применить экран» (смена окна — тяжёлая операция, без случайных нажатий).
//
// Чтение из звука/вибрации/HUD — через BoxSettings::Get() (кэш, без файлового ввода на каждый звук).
#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "UIWidgetBase.h"
#include "UISettings.generated.h"

class UButton;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

namespace BoxSettings
{
	struct FData
	{
		float Master = 1.f;  // 0..1
		float Sfx = 1.f;     // удары, блоки, гонг, падение
		float Crowd = 1.f;   // фон зала, «у-ух», вздох, овация
		bool bVibration = true;
		bool bControlsHints = true;
	};

	BOXINGUE_API const FData& Get();
	BOXINGUE_API void Set(const FData& D); // сохраняет в GameUserSettings.ini
	// Множители для звука: общая × категория.
	BOXINGUE_API float SfxGain();
	BOXINGUE_API float CrowdGain();
	BOXINGUE_API bool Vibration();
}

// Кнопка только для мыши (стрелки ‹ › у строк настроек): фокус клавиатуры/геймпада на неё не уходит.
UCLASS()
class BOXINGUE_API UBoxingMouseOnlyButton : public UButton
{
	GENERATED_BODY()

public:
	UBoxingMouseOnlyButton() { InitIsFocusable(false); }
};

UCLASS()
class BOXINGUE_API UBoxingSettingsWidget : public UBoxingUiWidget
{
	GENERATED_BODY()

public:
	// Закрыть (кнопка «Готово», Esc / B). Зовущий экран сам возвращает фокус.
	TFunction<void()> OnClose;
	void FocusFirst();

protected:
	virtual UWidget* BuildUi() override;
	virtual bool HandleBack() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	enum class ERow : uint8 { Master, Sfx, Crowd, Vibration, Hints, Quality, Resolution, WindowMode };
	struct FRow
	{
		ERow Kind = ERow::Master;
		UButton* Btn = nullptr;
		UTextBlock* Value = nullptr;
		UProgressBar* Bar = nullptr;
	};
	TArray<FRow> Rows;

	UWidget* MakeRow(UVerticalBox* Col, ERow Kind, const FString& Label);
	void Change(ERow Kind, int32 Dir);
	void Refresh();
	void ApplyScreen();
	void Close();
	FRow* FocusedRow();

	TArray<FIntPoint> Resolutions;
	int32 ResIndex = 0;
	int32 WindowModeSel = 0; // EWindowMode: 0 полный экран, 1 окно без рамки, 2 окно

	UPROPERTY(Transient) TObjectPtr<UButton> FirstButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ApplyButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ScreenNote;
};
