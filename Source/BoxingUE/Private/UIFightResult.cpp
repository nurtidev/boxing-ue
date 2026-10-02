#include "UIFightResult.h"

#include "BoxingFightGameMode.h"
#include "BoxingGameInstanceSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"

namespace
{
	FString MethodLine(const FFightResult& R)
	{
		auto Dec = [](EDecisionKind D) -> const TCHAR*
		{
			switch (D)
			{
			case EDecisionKind::Unanimous: return TEXT("Единогласное решение судей");
			case EDecisionKind::Majority: return TEXT("Решение большинством судей");
			case EDecisionKind::Split: return TEXT("Раздельное решение судей");
			case EDecisionKind::TieBreak: return TEXT("Решение по дополнительным показателям");
			case EDecisionKind::DrawUnanimous: return TEXT("Единогласная ничья");
			case EDecisionKind::DrawMajority: return TEXT("Ничья большинством");
			case EDecisionKind::DrawSplit: return TEXT("Раздельная ничья");
			default: return TEXT("Решение судей");
			}
		};
		switch (R.Method)
		{
		case EFightMethod::KO: return FString::Printf(TEXT("Нокаут · раунд %d"), R.StoppedRound);
		case EFightMethod::RSC: return FString::Printf(TEXT("Остановка боя (RSC) · раунд %d"), R.StoppedRound);
		default: return FString(Dec(R.Decision));
		}
	}
}

UWidget* UBoxingFightResultWidget::BuildUi()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	// Затемнение всей сцены.
	UBorder* Dim = Box(nullptr, FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, FMargin(0.f));
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));

	const UWorld* W = GetWorld();
	const ABoxingFightGameMode* GM = W ? W->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	if (!GM || !GM->GetCore().IsOver())
	{
		AddV(Col, Txt(TEXT("Бой не завершён"), 28, BoxUi::Text, true));
	}
	else
	{
		const FFightResult& R = GM->GetCore().GetResult();
		const FString Names[2] = {GM->RedPreset.Name, GM->BluePreset.Name};
		const FLinearColor Corners[2] = {BoxUi::Red, BoxUi::Blue};
		const bool bHuman = !GM->IsAutopilot();
		const int32 Me = GM->GetPlayerIndex();

		FString Caption = R.WinnerIndex < 0 ? TEXT("НИЧЬЯ") : TEXT("ПОБЕДА");
		if (bHuman && R.WinnerIndex >= 0)
		{
			Caption = R.WinnerIndex == Me ? TEXT("ТЫ ПОБЕДИЛ") : TEXT("ПОРАЖЕНИЕ");
		}
		AddV(Col, Txt(Caption, 22, BoxUi::Gold, true), false, FMargin(0.f), HAlign_Center);
		if (R.WinnerIndex >= 0)
		{
			UHorizontalBox* WinRow = WidgetTree->ConstructWidget<UHorizontalBox>();
			AddH(WinRow, Sized(Box(nullptr, Corners[R.WinnerIndex], 4.f, FMargin(0.f)), 10.f, 44.f), false, FMargin(0.f, 0.f, 14.f, 0.f));
			AddH(WinRow, Txt(Names[R.WinnerIndex], 44, BoxUi::Text, true));
			AddV(Col, WinRow, false, FMargin(0.f, 4.f, 0.f, 0.f), HAlign_Center);
		}
		else
		{
			AddV(Col, Txt(FString::Printf(TEXT("%s — %s"), *Names[0], *Names[1]), 34, BoxUi::Text, true), false, FMargin(0.f, 4.f, 0.f, 0.f), HAlign_Center);
		}
		const FString Method = GM->WasSurrendered() ? FString::Printf(TEXT("Сдача — остановка боя (RSC) · раунд %d"), R.StoppedRound) : MethodLine(R);
		AddV(Col, Txt(Method, 22, BoxUi::Accent, true), false, FMargin(0.f, 6.f, 0.f, 22.f), HAlign_Center);

		// Карты судей по раундам.
		UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>();
		auto Cell = [this, Grid](UWidget* Wd, int32 Row, int32 Column, EHorizontalAlignment HA = HAlign_Center)
		{
			UGridSlot* S = Grid->AddChildToGrid(Wd, Row, Column);
			S->SetPadding(FMargin(14.f, 5.f));
			S->SetHorizontalAlignment(HA);
		};
		Cell(Txt(TEXT("Раунд"), 15, BoxUi::Muted, true), 0, 0, HAlign_Left);
		for (int32 J = 0; J < 3; ++J)
		{
			Cell(Txt(FString::Printf(TEXT("Судья %d"), J + 1), 15, BoxUi::Muted, true), 0, J + 1);
		}
		Cell(Txt(TEXT("Нокдауны"), 15, BoxUi::Muted, true), 0, 4);
		auto Score = [this](int32 A, int32 B, int32 Size, bool bBold)
		{
			const FLinearColor C = A > B ? BoxUi::Red : (B > A ? BoxUi::Blue : BoxUi::Text);
			return Txt(FString::Printf(TEXT("%d – %d"), A, B), Size, FMath::Lerp(C, FLinearColor::White, 0.25f), bBold);
		};
		int32 Row = 1;
		for (const FRoundResult& RR : R.Rounds)
		{
			Cell(Txt(FString::FromInt(RR.Round), 17, BoxUi::Text, true), Row, 0, HAlign_Left);
			for (int32 J = 0; J < 3; ++J)
			{
				Cell(Score(RR.JudgeCards[J].Red, RR.JudgeCards[J].Blue, 17, false), Row, J + 1);
			}
			const FString Kd = (RR.Knockdowns[0] || RR.Knockdowns[1]) ? FString::Printf(TEXT("%d – %d"), RR.Knockdowns[0], RR.Knockdowns[1]) : TEXT("—");
			Cell(Txt(Kd, 17, RR.Knockdowns[0] || RR.Knockdowns[1] ? BoxUi::Gold : BoxUi::Muted), Row, 4);
			++Row;
		}
		Cell(Txt(TEXT("Итог"), 19, BoxUi::Text, true), Row, 0, HAlign_Left);
		for (int32 J = 0; J < 3; ++J)
		{
			Cell(Score(R.JudgeTotals[J].Red, R.JudgeTotals[J].Blue, 21, true), Row, J + 1);
		}
		Cell(Txt(FString::Printf(TEXT("%d – %d"), R.Knockdowns[0], R.Knockdowns[1]), 19, BoxUi::Gold, true), Row, 4);
		if (R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC)
		{
			AddV(Col, Txt(TEXT("Досрочная победа — карты на момент остановки"), 14, BoxUi::Muted), false, FMargin(0.f, 0.f, 0.f, 6.f), HAlign_Center);
		}

		UHorizontalBox* Legend = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(Legend, Sized(Box(nullptr, BoxUi::Red, 3.f, FMargin(0.f)), 12.f, 12.f), false, FMargin(0.f, 0.f, 8.f, 0.f));
		AddH(Legend, Txt(Names[0], 16, BoxUi::Text, true), false, FMargin(0.f, 0.f, 28.f, 0.f));
		AddH(Legend, Sized(Box(nullptr, BoxUi::Blue, 3.f, FMargin(0.f)), 12.f, 12.f), false, FMargin(0.f, 0.f, 8.f, 0.f));
		AddH(Legend, Txt(Names[1], 16, BoxUi::Text, true));
		AddV(Col, Legend, false, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Center);
		AddV(Col, Box(Grid, BoxUi::WithAlpha(BoxUi::Bg, 0.6f), 10.f, FMargin(12.f, 8.f)), false, FMargin(0.f, 0.f, 0.f, 26.f), HAlign_Center);
	}

	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	RematchButton = Btn(TEXT("Реванш"), [this]() { Rematch(); }, EBoxBtn::Primary, 22);
	AddH(Btns, Sized(RematchButton, 260.f), false, FMargin(0.f, 0.f, 16.f, 0.f));
	AddH(Btns, Sized(Btn(TEXT("В меню"), [this]() { ToMenu(); }, EBoxBtn::Secondary, 22), 260.f));
	AddV(Col, Btns, false, FMargin(0.f), HAlign_Center);
	AddV(Col, Txt(TEXT("Реванш — та же пара, новый бой · Enter / A — выбрать"), 14, BoxUi::Muted), false, FMargin(0.f, 10.f, 0.f, 0.f), HAlign_Center);

	UBorder* Card = Box(Col, BoxUi::WithAlpha(BoxUi::Panel, 0.96f), 18.f, FMargin(48.f, 32.f));
	Card->SetBrush(FSlateRoundedBoxBrush(BoxUi::WithAlpha(BoxUi::Panel, 0.96f), 18.f, BoxUi::Line, 1.f));
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Card);
	CS->SetAnchors(FAnchors(0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	return Root;
}

void UBoxingFightResultWidget::FocusFirst()
{
	FocusKey(TEXT("Реванш"));
}

void UBoxingFightResultWidget::Rematch()
{
	if (UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this))
	{
		S->Rematch(this);
	}
}

void UBoxingFightResultWidget::ToMenu()
{
	if (UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this))
	{
		S->OpenMenu(this);
	}
}

void UBoxingFightResultWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S || !S->bAuto)
	{
		return;
	}
	AutoTime += InDeltaTime;
	if (AutoStep == 0 && AutoTime > 1.5f)
	{
		AutoStep = 1;
		S->TakeUiShot(TEXT("result"));
	}
	else if (AutoStep == 1 && AutoTime > 2.5f)
	{
		AutoStep = 2;
		const bool bRematch = S->bAutoRematch && S->AutoFightsDone == 0;
		++S->AutoFightsDone;
		if (bRematch)
		{
			Rematch();
		}
		else
		{
			ToMenu();
		}
	}
}
