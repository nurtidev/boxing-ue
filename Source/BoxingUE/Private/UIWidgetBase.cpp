#include "UIWidgetBase.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"

namespace BoxUi
{
	const FLinearColor Bg = FLinearColor::FromSRGBColor(FColor(0x0B, 0x12, 0x18));
	const FLinearColor Panel = FLinearColor::FromSRGBColor(FColor(0x13, 0x1E, 0x27));
	const FLinearColor PanelHi = FLinearColor::FromSRGBColor(FColor(0x1A, 0x29, 0x35));
	const FLinearColor Line = FLinearColor::FromSRGBColor(FColor(0x26, 0x38, 0x46));
	const FLinearColor Text = FLinearColor::FromSRGBColor(FColor(0xEA, 0xF1, 0xF5));
	const FLinearColor Muted = FLinearColor::FromSRGBColor(FColor(0x8F, 0xA0, 0xAE));
	const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor(0x1F, 0xB8, 0xC4));
	const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(0xE9, 0xB9, 0x4F));
	const FLinearColor Red = FLinearColor::FromSRGBColor(FColor(0xE0, 0x3A, 0x3A));
	const FLinearColor Blue = FLinearColor::FromSRGBColor(FColor(0x2F, 0x7C, 0xF0));
	const FLinearColor Good = FLinearColor::FromSRGBColor(FColor(0x58, 0xC5, 0x86));
	const FLinearColor Warn = FLinearColor::FromSRGBColor(FColor(0xF2, 0xC2, 0x3A));
	const FLinearColor Bad = FLinearColor::FromSRGBColor(FColor(0xF0, 0x4E, 0x3E));

	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	FLinearColor StatColor(float V)
	{
		if (V >= 88.f) return Gold;
		if (V >= 80.f) return Good;
		if (V >= 70.f) return Accent;
		if (V >= 60.f) return Muted;
		return FLinearColor::FromSRGBColor(FColor(0x5F, 0x71, 0x80));
	}

	FLinearColor WithAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, A);
	}

	FLinearColor Dim(const FLinearColor& C, float K)
	{
		return FLinearColor(C.R * K, C.G * K, C.B * K, C.A);
	}
}

void UBoxingUiWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true); // экран целиком принимает фокус (режим «только UI»), дальше фокус — на первую кнопку
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = BuildUi();
	}
}

UTextBlock* UBoxingUiWidget::Txt(const FString& S, int32 Size, const FLinearColor& Color, bool bBold)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetText(FText::FromString(S));
	T->SetFont(BoxUi::Font(Size, bBold));
	T->SetColorAndOpacity(FSlateColor(Color));
	return T;
}

UBorder* UBoxingUiWidget::Box(UWidget* Content, const FLinearColor& Color, float Radius, FMargin Pad)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(FSlateRoundedBoxBrush(Color, Radius));
	B->SetPadding(Pad);
	if (Content)
	{
		B->SetContent(Content);
	}
	return B;
}

void UBoxingUiWidget::StyleBtn(UButton* B, EBoxBtn Kind, bool bSelected)
{
	FLinearColor Fill, Hover, Press, Outline = FLinearColor::Transparent;
	switch (Kind)
	{
	case EBoxBtn::Primary:
		Fill = BoxUi::Accent;
		Hover = FMath::Lerp(BoxUi::Accent, FLinearColor::White, 0.18f);
		Press = BoxUi::Dim(BoxUi::Accent, 0.8f);
		break;
	case EBoxBtn::Danger:
		Fill = BoxUi::Red;
		Hover = FMath::Lerp(BoxUi::Red, FLinearColor::White, 0.15f);
		Press = BoxUi::Dim(BoxUi::Red, 0.8f);
		break;
	case EBoxBtn::Ghost:
		Fill = FLinearColor(0.f, 0.f, 0.f, 0.f);
		Hover = BoxUi::WithAlpha(BoxUi::PanelHi, 0.9f);
		Press = BoxUi::Line;
		break;
	case EBoxBtn::Tab:
		Fill = bSelected ? BoxUi::Accent : BoxUi::Panel;
		Hover = bSelected ? FMath::Lerp(BoxUi::Accent, FLinearColor::White, 0.15f) : BoxUi::PanelHi;
		Press = bSelected ? BoxUi::Dim(BoxUi::Accent, 0.85f) : BoxUi::Line;
		Outline = bSelected ? FLinearColor::Transparent : BoxUi::Line;
		break;
	default:
		Fill = BoxUi::PanelHi;
		Hover = BoxUi::Line;
		Press = BoxUi::Panel;
		Outline = BoxUi::Line;
		break;
	}
	const float R = 8.f;
	FButtonStyle St;
	St.SetNormal(FSlateRoundedBoxBrush(Fill, R, Outline, Outline.A > 0.f ? 1.f : 0.f));
	// Наведение/фокус с геймпада — светлая рамка, чтобы было видно, где курсор.
	St.SetHovered(FSlateRoundedBoxBrush(Hover, R, BoxUi::Text, 2.f));
	St.SetPressed(FSlateRoundedBoxBrush(Press, R, BoxUi::Text, 2.f));
	// Неактивная: главная/опасная — приглушённый свой цвет (тёмный текст читается), прочие — тёмная плашка.
	const bool bFilled = Kind == EBoxBtn::Primary || Kind == EBoxBtn::Danger;
	St.SetDisabled(bFilled ? FSlateRoundedBoxBrush(BoxUi::Dim(Fill, 0.5f), R) : FSlateRoundedBoxBrush(BoxUi::WithAlpha(BoxUi::Panel, 0.6f), R, BoxUi::Line, 1.f));
	St.SetNormalPadding(FMargin(0.f));
	St.SetPressedPadding(FMargin(0.f));
	B->SetStyle(St);
}

UButton* UBoxingUiWidget::Btn(const FString& Label, TFunction<void()> OnClick, EBoxBtn Kind, int32 FontSize, bool bSelected, UTextBlock** OutLabel)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	StyleBtn(B, Kind, bSelected);
	const bool bDark = Kind == EBoxBtn::Primary || (Kind == EBoxBtn::Tab && bSelected);
	UTextBlock* T = Txt(Label, FontSize, bDark ? BoxUi::Bg : BoxUi::Text, true);
	T->SetJustification(ETextJustify::Center);
	B->SetContent(T);
	if (UButtonSlot* S = Cast<UButtonSlot>(T->Slot))
	{
		S->SetPadding(FMargin(FontSize * 1.1f, FontSize * 0.55f));
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}
	UBoxingUiClick* C = NewObject<UBoxingUiClick>(this);
	C->Fn = MoveTemp(OnClick);
	B->OnClicked.AddDynamic(C, &UBoxingUiClick::Fire);
	Clicks.Add(C);
	Track(B, Label, Label);
	if (OutLabel)
	{
		*OutLabel = T;
	}
	return B;
}

UButton* UBoxingUiWidget::BtnWith(UWidget* Content, TFunction<void()> OnClick, const FLinearColor& Fill, const FLinearColor& Hover, float Radius,
	const FString& Key)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle St;
	St.SetNormal(FSlateRoundedBoxBrush(Fill, Radius));
	St.SetHovered(FSlateRoundedBoxBrush(Hover, Radius, BoxUi::Text, 2.f));
	St.SetPressed(FSlateRoundedBoxBrush(Hover, Radius, BoxUi::Accent, 2.f));
	St.SetDisabled(FSlateRoundedBoxBrush(Fill, Radius));
	St.SetNormalPadding(FMargin(0.f));
	St.SetPressedPadding(FMargin(0.f));
	B->SetStyle(St);
	B->SetContent(Content);
	if (UButtonSlot* S = Cast<UButtonSlot>(Content->Slot))
	{
		S->SetPadding(FMargin(0.f));
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}
	UBoxingUiClick* C = NewObject<UBoxingUiClick>(this);
	C->Fn = MoveTemp(OnClick);
	B->OnClicked.AddDynamic(C, &UBoxingUiClick::Fire);
	Clicks.Add(C);
	Track(B, Key, Key);
	return B;
}

UProgressBar* UBoxingUiWidget::Bar(const FLinearColor& Fill, float Percent)
{
	UProgressBar* P = WidgetTree->ConstructWidget<UProgressBar>();
	FProgressBarStyle St;
	St.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.55f), 4.f));
	St.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.f));
	St.SetMarqueeImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.f));
	P->SetWidgetStyle(St);
	P->SetFillColorAndOpacity(Fill);
	P->SetPercent(Percent);
	P->SetBorderPadding(FVector2D(0.f, 0.f));
	return P;
}

UWidget* UBoxingUiWidget::Sized(UWidget* W, float Width, float Height)
{
	USizeBox* S = WidgetTree->ConstructWidget<USizeBox>();
	if (Width > 0.f)
	{
		S->SetWidthOverride(Width);
	}
	if (Height > 0.f)
	{
		S->SetHeightOverride(Height);
	}
	S->SetContent(W);
	return S;
}

UWidget* UBoxingUiWidget::Spacer(float W, float H)
{
	USpacer* S = WidgetTree->ConstructWidget<USpacer>();
	S->SetSize(FVector2D(W, H));
	return S;
}

void UBoxingUiWidget::AddH(UHorizontalBox* Row, UWidget* W, bool bFill, FMargin Pad, EVerticalAlignment VA)
{
	UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(W);
	S->SetSize(bFill ? FSlateChildSize(ESlateSizeRule::Fill) : FSlateChildSize(ESlateSizeRule::Automatic));
	S->SetPadding(Pad);
	S->SetVerticalAlignment(VA);
}

void UBoxingUiWidget::AddV(UVerticalBox* Col, UWidget* W, bool bFill, FMargin Pad, EHorizontalAlignment HA)
{
	UVerticalBoxSlot* S = Col->AddChildToVerticalBox(W);
	S->SetSize(bFill ? FSlateChildSize(ESlateSizeRule::Fill) : FSlateChildSize(ESlateSizeRule::Automatic));
	S->SetPadding(Pad);
	S->SetHorizontalAlignment(HA);
}

// ---------- Навигация клавиатурой/геймпадом (S-59) ----------

void UBoxingUiWidget::Track(UButton* B, const FString& Key, const FString& Label)
{
	if (!B)
	{
		return;
	}
	FTracked T;
	T.Button = B;
	T.Key = Key;
	T.Label = Label;
	T.Base = B->GetStyle();
	Tracked.Add(MoveTemp(T));
}

UButton* UBoxingUiWidget::FindKeyed(const FString& Key) const
{
	if (Key.IsEmpty())
	{
		return nullptr;
	}
	// С конца: после пересборки списка живая кнопка — последняя с этим ключом.
	for (int32 I = Tracked.Num() - 1; I >= 0; --I)
	{
		UButton* B = Tracked[I].Button.Get();
		if (B && Tracked[I].Key == Key && B->GetCachedWidget().IsValid() && B->GetIsEnabled() && B->IsVisible())
		{
			return B;
		}
	}
	return nullptr;
}

bool UBoxingUiWidget::FocusKey(const FString& Key)
{
	// Ключ запоминается и при неудаче: только что добавленный экран ещё не разложен Slate — фокус встанет
	// на него следующим тиком (UpdateFocus), а не на первую попавшуюся кнопку.
	if (!Key.IsEmpty())
	{
		LastFocusedKey = Key;
	}
	if (UButton* B = FindKeyed(Key))
	{
		B->SetKeyboardFocus();
		return true;
	}
	return false;
}

void UBoxingUiWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateFocus();
}

void UBoxingUiWidget::UpdateFocus()
{
	Tracked.RemoveAll([](const FTracked& T) { return !T.Button.IsValid(); });
	bool bAny = false;
	for (FTracked& T : Tracked)
	{
		UButton* B = T.Button.Get();
		const bool bFocus = B->GetCachedWidget().IsValid() && B->HasKeyboardFocus();
		if (bFocus != T.bFocused)
		{
			T.bFocused = bFocus;
			if (bFocus)
			{
				// Фокус — золотая рамка 3 px поверх «наведения»: видно с дивана, отличается от курсора мыши.
				FButtonStyle St = T.Base;
				FSlateBrush N = T.Base.Hovered;
				N.OutlineSettings.Color = FSlateColor(BoxUi::Gold);
				N.OutlineSettings.Width = 3.f;
				St.SetNormal(N);
				St.SetHovered(N);
				B->SetStyle(St);
			}
			else
			{
				B->SetStyle(T.Base);
			}
		}
		if (bFocus)
		{
			bAny = true;
			if (!T.Key.IsEmpty())
			{
				LastFocusedKey = T.Key;
			}
		}
	}
	if (bAny || Tracked.Num() == 0 || !GetIsEnabled() || !WantsFocusRestore() || !IsInViewport() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	// Фокус потерян (кнопку пересобрали, закрылся верхний экран): вернуть, если он ни у кого другого.
	const TSharedPtr<SWidget> F = FSlateApplication::Get().GetUserFocusedWidget(0);
	const TSharedPtr<SWidget> Mine = GetCachedWidget();
	const bool bFree = !F.IsValid() || F == Mine || F->GetType() == FName(TEXT("SViewport"));
	// HasFocusedDescendants() тут не годится: путь фокуса Slate хранит и мёртвую кнопку, и наш экран — «потомок в фокусе» остаётся.
	if (!bFree)
	{
		return;
	}
	if (FocusKey(LastFocusedKey))
	{
		return;
	}
	for (const FTracked& T : Tracked)
	{
		UButton* B = T.Button.Get();
		if (B->GetCachedWidget().IsValid() && B->GetIsEnabled() && B->IsVisible())
		{
			B->SetKeyboardFocus();
			return;
		}
	}
}

FReply UBoxingUiWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Back
		&& HandleBack())
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FString UBoxingUiWidget::DescribeFocus()
{
	for (TObjectIterator<UBoxingUiWidget> It; It; ++It)
	{
		if (!It->IsInViewport())
		{
			continue;
		}
		for (const FTracked& T : It->Tracked)
		{
			const UButton* B = T.Button.Get();
			if (B && B->GetCachedWidget().IsValid() && B->HasKeyboardFocus())
			{
				return FString::Printf(TEXT("%s: %s"), *It->GetClass()->GetName(), T.Label.IsEmpty() ? *T.Key : *T.Label);
			}
		}
	}
	TSharedPtr<SWidget> F = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetUserFocusedWidget(0) : nullptr;
	return F.IsValid() ? FString::Printf(TEXT("(не кнопка: %s)"), *F->GetTypeAsString()) : TEXT("(нет фокуса)");
}
