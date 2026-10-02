// Общая база экранов оболочки (S-55): UMG-виджеты, собранные целиком в C++ (без ассетов WBP — их не
// собрать без GUI-редактора). Дерево строится в NativeOnInitialized через WidgetTree->ConstructWidget;
// кнопкам с замыканиями нужен UObject-прокси (OnClicked — динамический делегат без параметров).
//
// Масштаб: размеры заданы для эталона 1920×1080, DPI-кривая UMG по короткой стороне (1080 → 1.0,
// 1440 → 1.33) масштабирует всё сама — на 1440p те же пропорции, текст крупнее в пикселях.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "UIWidgetBase.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UPanelWidget;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class UWidget;

// Палитра — как web (App.css: тёмный фон, бирюза бренда, золото, цвета углов).
namespace BoxUi
{
	extern const FLinearColor Bg;          // фон экранов
	extern const FLinearColor Panel;       // карточки
	extern const FLinearColor PanelHi;     // приподнятые карточки
	extern const FLinearColor Line;        // разделители
	extern const FLinearColor Text;        // основной текст
	extern const FLinearColor Muted;       // второстепенный
	extern const FLinearColor Accent;      // бирюза бренда
	extern const FLinearColor Gold;
	extern const FLinearColor Red;         // красный угол
	extern const FLinearColor Blue;        // синий угол
	extern const FLinearColor Good;
	extern const FLinearColor Warn;
	extern const FLinearColor Bad;

	FSlateFontInfo Font(int32 Size, bool bBold = false);
	// Цвет стата по значению (statColor веба).
	FLinearColor StatColor(float V);
	FLinearColor WithAlpha(const FLinearColor& C, float A);
	// Темнее (RGB × K, альфа та же).
	FLinearColor Dim(const FLinearColor& C, float K);
}

// Вид кнопки.
enum class EBoxBtn : uint8
{
	Primary,   // главная (бирюза)
	Secondary, // обычная
	Ghost,     // прозрачная, текстовая
	Tab,       // вкладка/чип (выбранная — Accent)
	Danger,
};

UCLASS()
class BOXINGUE_API UBoxingUiClick : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Fn;

	UFUNCTION()
	void Fire()
	{
		if (Fn)
		{
			Fn();
		}
	}
};

UCLASS(Abstract)
class BOXINGUE_API UBoxingUiWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	// Собрать дерево (WidgetTree уже есть). Вернуть корень.
	virtual UWidget* BuildUi() { return nullptr; }

	// ---------- Строители ----------
	UTextBlock* Txt(const FString& S, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bBold = false);
	UBorder* Box(UWidget* Content, const FLinearColor& Color, float Radius = 10.f, FMargin Pad = FMargin(16.f));
	UButton* Btn(const FString& Label, TFunction<void()> OnClick, EBoxBtn Kind = EBoxBtn::Secondary, int32 FontSize = 16,
		bool bSelected = false, UTextBlock** OutLabel = nullptr);
	// Кнопка с произвольным содержимым (строка ростера).
	UButton* BtnWith(UWidget* Content, TFunction<void()> OnClick, const FLinearColor& Fill, const FLinearColor& Hover, float Radius = 8.f);
	void StyleBtn(UButton* B, EBoxBtn Kind, bool bSelected);
	UProgressBar* Bar(const FLinearColor& Fill, float Percent = 1.f);
	UWidget* Sized(UWidget* W, float Width, float Height = 0.f);
	UWidget* Spacer(float W, float H);

	// Слоты (общие настройки отступов/выравнивания).
	static void AddH(UHorizontalBox* Row, UWidget* W, bool bFill = false, FMargin Pad = FMargin(0.f), EVerticalAlignment VA = VAlign_Center);
	static void AddV(UVerticalBox* Col, UWidget* W, bool bFill = false, FMargin Pad = FMargin(0.f), EHorizontalAlignment HA = HAlign_Fill);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxingUiClick>> Clicks;
};
