#include "UIMainMenu.h"

#include "BoxingGameInstanceSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UIMenuGameMode.h"

UWidget* UBoxingMainMenuWidget::BuildUi()
{
	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();

	// Левая колонка на тёмной полупрозрачной плашке: арена видна справа.
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* Side = Box(Col, BoxUi::WithAlpha(BoxUi::Bg, 0.86f), 0.f, FMargin(96.f, 110.f, 72.f, 60.f));
	UCanvasPanelSlot* SideSlot = Root->AddChildToCanvas(Side);
	SideSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
	SideSlot->SetOffsets(FMargin(0.f, 0.f, 660.f, 0.f));

	UHorizontalBox* Logo = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Logo, Txt(TEXT("QAZAQ"), 60, BoxUi::Text, true));
	AddH(Logo, Txt(TEXT(" BOXING"), 60, BoxUi::Accent, true));
	AddV(Col, Logo);
	AddV(Col, Txt(TEXT("бокс · реальный ростер · Unreal-прототип"), 18, BoxUi::Muted), false, FMargin(4.f, 4.f, 0.f, 64.f));

	FirstButton = Btn(TEXT("Выставка"), [this]() { OpenExhibition(); }, EBoxBtn::Primary, 26);
	AddV(Col, FirstButton, false, FMargin(0.f, 0.f, 0.f, 6.f));
	AddV(Col, Txt(TEXT("Сведи любых двух бойцов: любители, профи, легенды"), 15, BoxUi::Muted), false, FMargin(4.f, 0.f, 0.f, 26.f));

	AddV(Col, Btn(TEXT("Быстрый бой"), [this]() { QuickFight(); }, EBoxBtn::Secondary, 22), false, FMargin(0.f, 0.f, 0.f, 6.f));
	AddV(Col, Txt(TEXT("Случайная равная пара из любителей"), 15, BoxUi::Muted), false, FMargin(4.f, 0.f, 0.f, 26.f));

	UButton* Career = Btn(TEXT("Карьера — скоро"), []() {}, EBoxBtn::Secondary, 22);
	Career->SetIsEnabled(false);
	AddV(Col, Career, false, FMargin(0.f, 0.f, 0.f, 26.f));

	AddV(Col, Btn(TEXT("Выход"), [this]() { UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false); },
		EBoxBtn::Ghost, 20), false, FMargin(0.f));

	AddV(Col, Spacer(1.f, 1.f), true);
	FString Status;
	if (!S || !S->IsRosterLoaded())
	{
		Status = S ? S->GetRosterError() : TEXT("Нет подсистемы ростера");
	}
	else
	{
		Status = FString::Printf(TEXT("Ростер: %d бойцов · статы выведены из реальных регалий"), S->GetRoster().Num());
	}
	UTextBlock* St = Txt(Status, 14, S && S->IsRosterLoaded() ? BoxUi::Muted : BoxUi::Bad);
	St->SetAutoWrapText(true);
	AddV(Col, St, false, FMargin(4.f, 0.f, 0.f, 8.f));
	UTextBlock* Nav = Txt(TEXT("Мышь · Клавиатура: стрелки + Enter · Геймпад: крестовина + A"), 14, BoxUi::Muted);
	Nav->SetAutoWrapText(true);
	AddV(Col, Nav, false, FMargin(4.f, 0.f));
	return Root;
}

void UBoxingMainMenuWidget::FocusFirst()
{
	if (FirstButton)
	{
		FirstButton->SetKeyboardFocus();
	}
}

void UBoxingMainMenuWidget::OpenExhibition()
{
	if (ABoxingMenuPlayerController* PC = Cast<ABoxingMenuPlayerController>(GetOwningPlayer()))
	{
		PC->ShowExhibition();
	}
}

void UBoxingMainMenuWidget::QuickFight()
{
	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S || !S->IsRosterLoaded())
	{
		return;
	}
	// Случайный мужчина-любитель + ближайший по весу и уровню соперник.
	TArray<const FRosterBoxer*> Pool;
	for (const FRosterBoxer& B : S->GetRoster())
	{
		if (B.Kind == ERosterKind::Amateur && !B.bFemale)
		{
			Pool.Add(&B);
		}
	}
	if (Pool.Num() < 2)
	{
		return;
	}
	const FRosterBoxer* A = Pool[FMath::RandRange(0, Pool.Num() - 1)];
	const FRosterBoxer* Best = nullptr;
	float BestScore = 1e9f;
	for (const FRosterBoxer* B : Pool)
	{
		if (B == A)
		{
			continue;
		}
		const float Score = FMath::Abs(B->WeightKg - A->WeightKg) * 3.f + FMath::Abs(B->Overall - A->Overall) + FMath::FRand() * 4.f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = B;
		}
	}
	FExhibitionSetup E;
	E.bValid = true;
	E.RedId = A->Id;
	E.BlueId = Best->Id;
	E.Rounds = 3;
	E.bProRules = false;
	E.Seed = FMath::Rand();
	S->SetExhibition(E);
	S->StartExhibitionFight(this);
}

void UBoxingMainMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S || !S->bAuto)
	{
		return;
	}
	// Сценарий проверки: снимок главного экрана → Выставка (после боя — выход).
	AutoTime += InDeltaTime;
	if (AutoStep == 0 && AutoTime > 4.f)
	{
		AutoStep = 1;
		S->TakeUiShot(S->AutoFightsDone > 0 ? TEXT("menu_after") : TEXT("menu"));
	}
	else if (AutoStep == 1 && AutoTime > 5.f)
	{
		AutoStep = 2;
		if (S->AutoFightsDone > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("UI: сценарий завершён — выход"));
			UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		}
		else
		{
			OpenExhibition();
		}
	}
}
