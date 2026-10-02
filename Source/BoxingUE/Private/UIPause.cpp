#include "UIPause.h"

#include "BoxingFightGameMode.h"
#include "BoxingFightHUD.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UISettings.h"

ABoxingFightHUD* UBoxingPauseWidget::Hud() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<ABoxingFightHUD>(PC->GetHUD()) : nullptr;
}

UWidget* UBoxingPauseWidget::BuildUi()
{
	const UWorld* W = GetWorld();
	const ABoxingFightGameMode* GM = W ? W->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	const bool bAuto = GM && GM->IsAutopilot();

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	UBorder* Dim = Box(nullptr, FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, FMargin(0.f));
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Col, Txt(TEXT("ПАУЗА"), 44, BoxUi::Text, true), false, FMargin(0.f), HAlign_Center);
	SubTitle = Txt(TEXT(""), 17, BoxUi::Muted);
	SubTitle->SetJustification(ETextJustify::Center);
	AddV(Col, SubTitle, false, FMargin(0.f, 2.f, 0.f, 24.f), HAlign_Center);

	// ---- Главный список.
	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>();
	auto Add = [this, Main](UButton* B, float Bottom = 10.f) { AddV(Main, B, false, FMargin(0.f, 0.f, 0.f, Bottom)); };
	Add(Btn(TEXT("Продолжить"), [this]() { if (ABoxingFightHUD* H = Hud()) H->ClosePause(); }, EBoxBtn::Primary, 24), 18.f);
	Add(Btn(TEXT("Управление"), [this]() { SetMode(EMode::Controls); }, EBoxBtn::Secondary, 21));
	Add(Btn(TEXT("Настройки"), [this]() { OpenSettings(); }, EBoxBtn::Secondary, 21), 18.f);
	SurrenderButton = Btn(TEXT("Сдаться"), [this]() { SetMode(EMode::ConfirmSurrender); }, EBoxBtn::Danger, 21);
	SurrenderButton->SetIsEnabled(!bAuto && GM && !GM->GetCore().IsOver());
	Add(SurrenderButton);
	Add(Btn(TEXT("В главное меню"), [this]() { SetMode(EMode::ConfirmMenu); }, EBoxBtn::Ghost, 21), 0.f);
	if (bAuto)
	{
		AddV(Main, Txt(TEXT("Автопилот: сдаться нельзя — бой ведут оба ИИ"), 14, BoxUi::Muted), false, FMargin(4.f, 10.f, 0.f, 0.f), HAlign_Center);
	}
	MainPanel = Main;
	AddV(Col, Main);

	// ---- Управление.
	UVerticalBox* Ctl = WidgetTree->ConstructWidget<UVerticalBox>();
	auto Line = [this, Ctl](const TCHAR* What, const TCHAR* Kb, const TCHAR* Pad)
	{
		UHorizontalBox* L = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(L, Sized(Txt(What, 17, BoxUi::Text, true), 300.f));
		AddH(L, Sized(Txt(Kb, 17, BoxUi::Text), 260.f));
		AddH(L, Txt(Pad, 17, BoxUi::Text));
		AddV(Ctl, L, false, FMargin(0.f, 3.f));
	};
	{
		UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(H, Sized(Txt(TEXT(""), 15, BoxUi::Muted), 300.f));
		AddH(H, Sized(Txt(TEXT("КЛАВИАТУРА"), 15, BoxUi::Accent, true), 260.f));
		AddH(H, Txt(TEXT("ГЕЙМПАД"), 15, BoxUi::Accent, true));
		AddV(Ctl, H, false, FMargin(0.f, 0.f, 0.f, 6.f));
	}
	Line(TEXT("Джеб / кросс"), TEXT("J / K"), TEXT("X / Y"));
	Line(TEXT("Хук левой / правой"), TEXT("U / I"), TEXT("LB / RB"));
	Line(TEXT("Апперкот левой / правой"), TEXT("N / M"), TEXT("A / B"));
	Line(TEXT("Удар в корпус"), TEXT("Shift + удар"), TEXT("RT + удар"));
	Line(TEXT("Блок (держать)"), TEXT("Пробел"), TEXT("LT"));
	Line(TEXT("Уклоны"), TEXT("Q / E"), TEXT("правый стик вбок"));
	Line(TEXT("Ноги"), TEXT("WASD / стрелки"), TEXT("левый стик"));
	Line(TEXT("Пивот"), TEXT("Shift + W / S"), TEXT("L3 + стик вверх/вниз"));
	Line(TEXT("Встать на нокдауне"), TEXT("жми удары"), TEXT("жми удары"));
	Line(TEXT("Пауза"), TEXT("Esc"), TEXT("Start"));
	AddV(Ctl, Txt(TEXT("D/A — к сопернику / назад, W/S — обход по дуге вверх / вниз по экрану. F1 — скрыть подсказку в бою."), 14, BoxUi::Muted),
		false, FMargin(0.f, 10.f, 0.f, 16.f));
	AddV(Ctl, Btn(TEXT("Назад"), [this]() { ShowMain(TEXT("Управление")); }, EBoxBtn::Secondary, 20), false, FMargin(0.f), HAlign_Center);
	ControlsPanel = Ctl;
	AddV(Col, Ctl);

	// ---- Подтверждение.
	UVerticalBox* Conf = WidgetTree->ConstructWidget<UVerticalBox>();
	ConfirmTitle = Txt(TEXT(""), 26, BoxUi::Text, true);
	AddV(Conf, ConfirmTitle, false, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Center);
	ConfirmText = Txt(TEXT(""), 17, BoxUi::Muted);
	ConfirmText->SetJustification(ETextJustify::Center);
	ConfirmText->SetAutoWrapText(true);
	AddV(Conf, Sized(ConfirmText, 560.f), false, FMargin(0.f, 0.f, 0.f, 22.f), HAlign_Center);
	UHorizontalBox* YN = WidgetTree->ConstructWidget<UHorizontalBox>();
	UTextBlock* YesL = nullptr;
	AddH(YN, Sized(Btn(TEXT("Да"), [this]()
		{
			ABoxingFightHUD* H = Hud();
			if (!H) return;
			if (Mode == EMode::ConfirmSurrender) H->SurrenderFromPause();
			else H->ExitToMenu();
		}, EBoxBtn::Danger, 20, false, &YesL), 250.f), false, FMargin(0.f, 0.f, 14.f, 0.f));
	ConfirmYesLabel = YesL;
	AddH(YN, Sized(Btn(TEXT("Отмена"), [this]() { ShowMain(Mode == EMode::ConfirmSurrender ? TEXT("Сдаться") : TEXT("В главное меню")); },
		EBoxBtn::Secondary, 20), 250.f));
	AddV(Conf, YN, false, FMargin(0.f), HAlign_Center);
	ConfirmPanel = Conf;
	AddV(Col, Conf);

	AddV(Col, Txt(TEXT("Esc / Start — продолжить · B — назад · ↑↓ / крестовина — выбрать · Enter / A — нажать"), 14, BoxUi::Muted),
		false, FMargin(0.f, 24.f, 0.f, 0.f), HAlign_Center);

	UBorder* Card = Box(Col, BoxUi::WithAlpha(BoxUi::Bg, 0.95f), 18.f, FMargin(48.f, 34.f));
	Card->SetBrush(FSlateRoundedBoxBrush(BoxUi::WithAlpha(BoxUi::Bg, 0.95f), 18.f, BoxUi::Line, 1.f));
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Card);
	CS->SetAnchors(FAnchors(0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	SetMode(EMode::Main);
	return Root;
}

void UBoxingPauseWidget::SetMode(EMode M)
{
	Mode = M;
	MainPanel->SetVisibility(M == EMode::Main ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	ControlsPanel->SetVisibility(M == EMode::Controls ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	const bool bConf = M == EMode::ConfirmSurrender || M == EMode::ConfirmMenu;
	ConfirmPanel->SetVisibility(bConf ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (M == EMode::ConfirmSurrender)
	{
		ConfirmTitle->SetText(FText::FromString(TEXT("Сдаться?")));
		ConfirmText->SetText(FText::FromString(TEXT("Бой будет остановлен: поражение остановкой боя (RSC) в этом раунде.")));
		ConfirmYesLabel->SetText(FText::FromString(TEXT("Сдаться")));
	}
	else if (M == EMode::ConfirmMenu)
	{
		ConfirmTitle->SetText(FText::FromString(TEXT("Выйти в главное меню?")));
		ConfirmText->SetText(FText::FromString(TEXT("Бой не будет засчитан, итог не сохранится.")));
		ConfirmYesLabel->SetText(FText::FromString(TEXT("Выйти")));
	}
	// Фокус — на безопасное действие; кнопки скрытых панелей фокус не держат.
	if (M == EMode::Controls)
	{
		FocusKey(TEXT("Назад"));
	}
	else if (bConf)
	{
		FocusKey(TEXT("Отмена"));
	}
}

void UBoxingPauseWidget::ShowMain(const FString& FocusLabel)
{
	SetMode(EMode::Main);
	if (!FocusKey(FocusLabel))
	{
		FocusKey(TEXT("Продолжить"));
	}
}

void UBoxingPauseWidget::FocusFirst()
{
	ShowMain(TEXT("Продолжить"));
}

void UBoxingPauseWidget::OpenSettings()
{
	APlayerController* PC = GetOwningPlayer();
	UBoxingSettingsWidget* S = PC ? CreateWidget<UBoxingSettingsWidget>(PC, UBoxingSettingsWidget::StaticClass()) : nullptr;
	if (!S)
	{
		return;
	}
	bSettingsOpen = true;
	SetIsEnabled(false); // навигация Slate не должна уходить с настроек на кнопки паузы под ними
	TWeakObjectPtr<UBoxingPauseWidget> Self(this);
	S->OnClose = [Self]()
	{
		if (UBoxingPauseWidget* P = Self.Get())
		{
			P->bSettingsOpen = false;
			P->SetIsEnabled(true);
			P->ShowMain(TEXT("Настройки"));
		}
	};
	S->AddToViewport(40);
	S->FocusFirst();
}

bool UBoxingPauseWidget::HandleBack()
{
	if (Mode == EMode::Main)
	{
		if (ABoxingFightHUD* H = Hud())
		{
			H->ClosePause();
		}
	}
	else
	{
		ShowMain(Mode == EMode::Controls ? TEXT("Управление") : (Mode == EMode::ConfirmSurrender ? TEXT("Сдаться") : TEXT("В главное меню")));
	}
	return true;
}

FReply UBoxingPauseWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Start — как Esc (Esc уже «назад» в навигации Slate).
	if (InKeyEvent.GetKey() == EKeys::Gamepad_Special_Right && HandleBack())
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UBoxingPauseWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UWorld* W = GetWorld();
	const ABoxingFightGameMode* GM = W ? W->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	if (!GM || !SubTitle)
	{
		return;
	}
	const FFightSnapshot& S = GM->GetSnapshot();
	const float Left = S.Phase == EFightPhase::Between ? S.BreakLeft : S.TimeLeft;
	const int32 Sec = FMath::Max(0, FMath::CeilToInt(Left));
	const FString T = FString::Printf(TEXT("%s — %s · раунд %d / %d · %d:%02d"), *GM->RedPreset.Name, *GM->BluePreset.Name, S.Round, S.TotalRounds,
		Sec / 60, Sec % 60);
	if (SubTitle->GetText().ToString() != T)
	{
		SubTitle->SetText(FText::FromString(T));
	}
}
