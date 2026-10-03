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
#include "Misc/CommandLine.h"
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
		// S-65: судей — сколько в итоге (любители — 5 по World Boxing, профи — 3); колонка нокдаунов — после судей.
		const int32 NJ = FMath::Clamp(R.NumJudges, 1, MAX_JUDGES);
		const int32 KdCol = NJ + 1;
		for (int32 J = 0; J < NJ; ++J)
		{
			Cell(Txt(FString::Printf(TEXT("Судья %d"), J + 1), 15, BoxUi::Muted, true), 0, J + 1);
		}
		// S-67: «Нокдауны 1 – 0» читалось двояко (кто падал / кто сбил). Теперь как у судей: слева красный, справа синий,
		// число — сколько раз ОН отправил соперника в нокдаун, цвет — в чью пользу.
		Cell(Txt(TEXT("Нокдаунов нанёс"), 15, BoxUi::Muted, true), 0, KdCol);
		auto Score = [this](int32 A, int32 B, int32 Size, bool bBold)
		{
			const FLinearColor C = A > B ? BoxUi::Red : (B > A ? BoxUi::Blue : BoxUi::Text);
			return Txt(FString::Printf(TEXT("%d – %d"), A, B), Size, FMath::Lerp(C, FLinearColor::White, 0.25f), bBold);
		};
		// S-63: досрочка (KO/RSC/сдача) — раунд остановки не завершён и не судится (было «10 – 10» в недоигранном раунде
		// сдачи); итог — по завершённым раундам.
		const bool bStopped = R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC;
		FJudgeCard Totals[MAX_JUDGES];
		int32 Completed = 0;
		int32 Row = 1;
		for (const FRoundResult& RR : R.Rounds)
		{
			const bool bUnfinished = bStopped && RR.Round == R.StoppedRound;
			Cell(Txt(bUnfinished ? FString::Printf(TEXT("%d · не завершён"), RR.Round) : FString::FromInt(RR.Round), 17,
				bUnfinished ? BoxUi::Muted : BoxUi::Text, true), Row, 0, HAlign_Left);
			for (int32 J = 0; J < NJ; ++J)
			{
				if (bUnfinished)
				{
					Cell(Txt(TEXT("—"), 17, BoxUi::Muted), Row, J + 1);
					continue;
				}
				Cell(Score(RR.JudgeCards[J].Red, RR.JudgeCards[J].Blue, 17, false), Row, J + 1);
				Totals[J].Red += RR.JudgeCards[J].Red;
				Totals[J].Blue += RR.JudgeCards[J].Blue;
			}
			Completed += bUnfinished ? 0 : 1;
			// Knockdowns[i] — сколько раз ПАДАЛ боец i, поэтому нанёс красный = падения синего.
			Cell((RR.Knockdowns[0] || RR.Knockdowns[1]) ? Score(RR.Knockdowns[1], RR.Knockdowns[0], 17, true) : Txt(TEXT("—"), 17, BoxUi::Muted), Row, KdCol);
			++Row;
		}
		Cell(Txt(TEXT("Итог"), 19, BoxUi::Text, true), Row, 0, HAlign_Left);
		for (int32 J = 0; J < NJ; ++J)
		{
			if (bStopped && Completed == 0)
			{
				Cell(Txt(TEXT("—"), 21, BoxUi::Muted, true), Row, J + 1);
			}
			else
			{
				const FJudgeCard& T = bStopped ? Totals[J] : R.JudgeTotals[J];
				static const bool bTieDemo = FParse::Param(FCommandLine::Get(), TEXT("BoxUiTieDemo")); // проверка вёрстки отметки
				const int32 Nom = (bTieDemo && J == 0) ? R.WinnerIndex : R.TieNominee[J];
				if ((bTieDemo && J == 0 && Nom >= 0) || (!bStopped && T.Red == T.Blue && (Nom == 0 || Nom == 1)))
				{
					// S-65 → S-67: равная сумма у судьи (бывает только при 10-8) — по п. 9.1.5 он называет победителя: «28 – 28 → Фамилия».
					FString Surname = Names[Nom];
					int32 Sp = INDEX_NONE;
					if (Surname.FindLastChar(TEXT(' '), Sp))
					{
						Surname = Surname.Mid(Sp + 1);
					}
					UVerticalBox* TieCol = WidgetTree->ConstructWidget<UVerticalBox>();
					AddV(TieCol, Score(T.Red, T.Blue, 21, true), false, FMargin(0.f), HAlign_Center);
					AddV(TieCol, Txt(TEXT("→ ") + Surname, 13, FMath::Lerp(Nom == 0 ? BoxUi::Red : BoxUi::Blue, FLinearColor::White, 0.25f), true),
						false, FMargin(0.f), HAlign_Center);
					Cell(TieCol, Row, J + 1);
				}
				else
				{
					Cell(Score(T.Red, T.Blue, 21, true), Row, J + 1);
				}
			}
		}
		Cell((R.Knockdowns[0] || R.Knockdowns[1]) ? Score(R.Knockdowns[1], R.Knockdowns[0], 21, true) : Txt(TEXT("—"), 21, BoxUi::Muted, true), Row, KdCol);
		if (R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC)
		{
			AddV(Col, Txt(FString::Printf(TEXT("Досрочно: раунд %d не завершён и не судится · итог — по завершённым раундам"), R.StoppedRound), 14, BoxUi::Muted), false, FMargin(0.f, 0.f, 0.f, 6.f), HAlign_Center);
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
