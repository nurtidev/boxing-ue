#include "UIExhibition.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Styling/CoreStyle.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UIMenuGameMode.h"

namespace
{
	constexpr int32 MaxRows = 300; // мужчин-профи 281 — влезают все; длиннее — «уточни поиск»

	const TCHAR* TabLabel(ERosterKind K)
	{
		return K == ERosterKind::Pro ? TEXT("Профи") : (K == ERosterKind::Legend ? TEXT("Легенды") : TEXT("Любители"));
	}

	FString Kg(float W)
	{
		return FMath::IsNearlyEqual(W, FMath::RoundToFloat(W)) ? FString::Printf(TEXT("%d кг"), FMath::RoundToInt(W)) : FString::Printf(TEXT("%.1f кг"), W);
	}

	FString AgeText(int32 A)
	{
		const int32 M10 = A % 10, M100 = A % 100;
		const TCHAR* W = (M10 == 1 && M100 != 11) ? TEXT("год") : ((M10 >= 2 && M10 <= 4 && (M100 < 12 || M100 > 14)) ? TEXT("года") : TEXT("лет"));
		return FString::Printf(TEXT("%d %s"), A, W);
	}

	// Подзаголовок строки ростера: регалии любителя / пояса и рекорд профи.
	FString Credentials(const FRosterBoxer& B)
	{
		if (B.Kind == ERosterKind::Amateur)
		{
			return B.Accolades.IsEmpty() ? TEXT("без крупных медалей") : B.Accolades;
		}
		// Бейдж, повторяющий рекорд («50-0» у Мейвезера при рекорде «50-0-0»), не дублируем.
		FString S = B.Badge;
		if (!B.ProRecord.IsEmpty())
		{
			TArray<FString> Parts;
			B.ProRecord.ParseIntoArray(Parts, TEXT("-"));
			const FString WL = Parts.Num() >= 2 ? Parts[0] + TEXT("-") + Parts[1] : B.ProRecord;
			if (S == WL || S == B.ProRecord)
			{
				S.Reset();
			}
			S += (S.IsEmpty() ? TEXT("") : TEXT(" · ")) + B.ProRecord;
		}
		// Легенды: в вебе — эмодзи 🏛 (шрифт UE его не рисует) → словом.
		if (B.Kind == ERosterKind::Legend)
		{
			S = TEXT("Легенда") + (S.IsEmpty() ? FString() : TEXT(" · ") + S);
		}
		return S;
	}

	// Реализм межвесового боя (realismTag веба): D — макс. отклонение бойца от веса боя (WeightStretch).
	void Realism(float D, FString& Text, FLinearColor& Color)
	{
		if (D <= 4.f) { Text = TEXT("реальный бой"); Color = BoxUi::Good; }
		else if (D <= 8.f) { Text = TEXT("кэтчвейт — натяжка"); Color = BoxUi::Warn; }
		else if (D <= 14.f) { Text = TEXT("бой мечты — почти невозможно"); Color = BoxUi::Gold; }
		else { Text = TEXT("фэнтези — в жизни невозможно"); Color = BoxUi::Bad; }
	}
}

UWidget* UBoxingExhibitionWidget::BuildUi()
{
	if (const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this))
	{
		// Вернулись из боя — то же место: вкладка, пол, пара, раунды.
		const FExhibitionSetup& E = S->GetExhibition();
		if (E.bValid)
		{
			Tab = E.Tab;
			bFemale = E.bFemale;
			RedId = E.RedId;
			BlueId = E.BlueId;
			const FRosterBoxer* R = S->FindById(RedId);
			const FRosterBoxer* B = S->FindById(BlueId);
			if (IsProRules() && R && B && E.Rounds != UBoxingGameInstanceSubsystem::AutoProRounds(*R, *B))
			{
				ProRoundsSel = E.Rounds;
			}
		}
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>();
	UBorder* PageBox = Box(Page, BoxUi::WithAlpha(BoxUi::Bg, 0.93f), 0.f, FMargin(48.f, 32.f, 48.f, 28.f));
	UCanvasPanelSlot* BackSlot = Root->AddChildToCanvas(PageBox);
	BackSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BackSlot->SetOffsets(FMargin(0.f));

	// Шапка.
	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Head, Btn(TEXT("← Меню"), [this]() { Back(); }, EBoxBtn::Ghost, 16), false, FMargin(0.f, 0.f, 20.f, 0.f));
	AddH(Head, Txt(TEXT("Выставка"), 34, BoxUi::Text, true));
	AddH(Head, Spacer(1.f, 1.f), true);
	AddH(Head, Txt(TEXT("Клик / Enter / A — в красный угол, второй — в синий, повторно — снять · Esc / B — в меню"), 15, BoxUi::Muted));
	AddV(Page, Head, false, FMargin(0.f, 0.f, 0.f, 20.f));

	UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddV(Page, Body, true);

	// ---- Левая колонка: фильтры + список.
	UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>();
	FilterRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddV(Left, FilterRow, false, FMargin(0.f, 0.f, 0.f, 12.f));

	UHorizontalBox* SearchRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	FEditableTextBoxStyle Es = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	Es.SetBackgroundImageNormal(FSlateRoundedBoxBrush(BoxUi::Panel, 8.f, BoxUi::Line, 1.f));
	Es.SetBackgroundImageHovered(FSlateRoundedBoxBrush(BoxUi::PanelHi, 8.f, BoxUi::Line, 1.f));
	Es.SetBackgroundImageFocused(FSlateRoundedBoxBrush(BoxUi::PanelHi, 8.f, BoxUi::Accent, 2.f));
	Es.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(BoxUi::Panel, 8.f));
	Es.SetFont(BoxUi::Font(17));
	Es.SetForegroundColor(FSlateColor(BoxUi::Text));
	Es.SetFocusedForegroundColor(FSlateColor(BoxUi::Text));
	Es.SetPadding(FMargin(14.f, 10.f));
	SearchBox->SetWidgetStyle(Es);
	SearchBox->SetHintText(FText::FromString(TEXT("Поиск по имени или стране…")));
	SearchBox->OnTextChanged.AddDynamic(this, &UBoxingExhibitionWidget::OnSearchChanged);
	AddH(SearchRow, SearchBox, true, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Fill);
	UTextBlock* WL = nullptr;
	WeightButton = Btn(TEXT("Вес: все"), [this]() { CycleWeight(); }, EBoxBtn::Secondary, 16, false, &WL);
	WeightLabel = WL;
	AddH(SearchRow, Sized(WeightButton, 170.f), false, FMargin(0.f), VAlign_Fill);
	AddV(Left, SearchRow, false, FMargin(0.f, 0.f, 0.f, 10.f));

	CountText = Txt(TEXT(""), 14, BoxUi::Muted);
	AddV(Left, CountText, false, FMargin(4.f, 0.f, 0.f, 8.f));
	List = WidgetTree->ConstructWidget<UScrollBox>();
	List->SetScrollbarThickness(FVector2D(8.f, 8.f));
	List->SetAlwaysShowScrollbar(false); // S-63: при одном бойце полоса на всю высоту — только когда есть что листать
	List->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll); // геймпад: строка в фокусе всегда видна
	AddV(Left, List, true);
	AddH(Body, Left, true, FMargin(0.f, 0.f, 28.f, 0.f), VAlign_Fill);

	// ---- Правая колонка: углы, раунды, «В бой».
	Side = WidgetTree->ConstructWidget<UVerticalBox>();
	AddH(Body, Sized(Side, 900.f), false, FMargin(0.f), VAlign_Fill);

	RebuildFilters();
	RebuildList();
	RebuildSide();
	return Root;
}

void UBoxingExhibitionWidget::FocusFirst()
{
	FocusKey(TabLabel(Tab));
}

void UBoxingExhibitionWidget::RebuildFilters()
{
	FilterRow->ClearChildren();
	for (ERosterKind K : {ERosterKind::Amateur, ERosterKind::Pro, ERosterKind::Legend})
	{
		UButton* B = Btn(TabLabel(K), [this, K]() { SetTab(K); }, EBoxBtn::Tab, 17, Tab == K);
		AddH(FilterRow, B, false, FMargin(0.f, 0.f, 8.f, 0.f));
		if (K == Tab)
		{
			FirstTab = B;
		}
	}
	AddH(FilterRow, Spacer(24.f, 1.f));
	AddH(FilterRow, Btn(TEXT("Мужчины"), [this]() { SetFemale(false); }, EBoxBtn::Tab, 17, !bFemale), false, FMargin(0.f, 0.f, 8.f, 0.f));
	AddH(FilterRow, Btn(TEXT("Женщины"), [this]() { SetFemale(true); }, EBoxBtn::Tab, 17, bFemale));
}

TArray<const FRosterBoxer*> UBoxingExhibitionWidget::Filtered() const
{
	TArray<const FRosterBoxer*> Out;
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S)
	{
		return Out;
	}
	const FString Q = Search.TrimStartAndEnd();
	for (const FRosterBoxer& B : S->GetRoster())
	{
		if (B.Kind != Tab || B.bFemale != bFemale)
		{
			continue;
		}
		if (WeightFilter > 0.f && FMath::Abs(B.WeightKg - WeightFilter) > 3.f)
		{
			continue;
		}
		if (!Q.IsEmpty() && !B.Name.Contains(Q, ESearchCase::IgnoreCase) && !B.Country.Contains(Q, ESearchCase::IgnoreCase)
			&& !B.City.Contains(Q, ESearchCase::IgnoreCase) && !B.Division.Contains(Q, ESearchCase::IgnoreCase))
		{
			continue;
		}
		Out.Add(&B);
	}
	Out.Sort([](const FRosterBoxer& A, const FRosterBoxer& B) { return A.Overall > B.Overall; });
	return Out;
}

UWidget* UBoxingExhibitionWidget::MakeRow(const FRosterBoxer& B)
{
	const bool bRed = B.Id == RedId;
	const bool bBlue = B.Id == BlueId;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

	UTextBlock* Ovr = Txt(FString::Printf(TEXT("%.0f"), B.Overall), 22, BoxUi::StatColor(B.Overall), true);
	Ovr->SetJustification(ETextJustify::Center);
	AddH(Row, Sized(Ovr, 64.f), false, FMargin(8.f, 0.f, 8.f, 0.f));

	UVerticalBox* Mid = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* NameLine = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(NameLine, Txt(B.Name, 19, BoxUi::Text, true));
	AddH(NameLine, Txt(FString::Printf(TEXT("  %s"), *B.CountryCode), 15, BoxUi::Muted, true), false, FMargin(0.f), VAlign_Bottom);
	AddV(Mid, NameLine);
	FString Sub = FString::Printf(TEXT("%s · %s · %s"), *Kg(B.WeightKg), *B.StyleLabel, *AgeText(B.Age));
	if (!B.Division.IsEmpty())
	{
		Sub = B.Division + TEXT(" · ") + Sub;
	}
	const FString Cred = Credentials(B);
	if (!Cred.IsEmpty())
	{
		Sub += TEXT(" · ") + Cred;
	}
	UTextBlock* SubT = Txt(Sub, 14, BoxUi::Muted);
	SubT->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	AddV(Mid, SubT, false, FMargin(0.f, 2.f, 0.f, 0.f));
	AddH(Row, Mid, true, FMargin(0.f, 8.f));

	if (bRed || bBlue)
	{
		UTextBlock* Tag = Txt(bRed ? TEXT("КРАСНЫЙ") : TEXT("СИНИЙ"), 13, FLinearColor::White, true);
		AddH(Row, Box(Tag, bRed ? BoxUi::Red : BoxUi::Blue, 6.f, FMargin(10.f, 4.f)), false, FMargin(8.f, 0.f, 12.f, 0.f));
	}

	const FLinearColor Fill = bRed ? FMath::Lerp(BoxUi::Panel, BoxUi::Red, 0.28f)
		: (bBlue ? FMath::Lerp(BoxUi::Panel, BoxUi::Blue, 0.28f) : BoxUi::Panel);
	const FString Id = B.Id;
	return BtnWith(Row, [this, Id]() { Pick(Id); }, Fill, BoxUi::PanelHi, 8.f, TEXT("row:") + Id);
}

void UBoxingExhibitionWidget::RebuildList()
{
	const float Offset = List->GetScrollOffset();
	List->ClearChildren();

	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S || !S->IsRosterLoaded())
	{
		UTextBlock* E = Txt(S ? S->GetRosterError() : TEXT("Нет подсистемы ростера"), 17, BoxUi::Bad);
		E->SetAutoWrapText(true);
		List->AddChild(E);
		CountText->SetText(FText::GetEmpty());
		return;
	}
	const TArray<const FRosterBoxer*> Rows = Filtered();
	const int32 Shown = FMath::Min(Rows.Num(), MaxRows);
	for (int32 I = 0; I < Shown; ++I)
	{
		UWidget* W = MakeRow(*Rows[I]);
		List->AddChild(W);
		if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(W->Slot))
		{
			SS->SetPadding(FMargin(0.f, 0.f, 12.f, 6.f));
		}
	}
	if (Rows.Num() == 0)
	{
		List->AddChild(Txt(TEXT("Никого не нашлось — измени поиск или фильтр веса"), 17, BoxUi::Muted));
	}
	FString C = FString::Printf(TEXT("%s · %s: %d"), TabLabel(Tab), bFemale ? TEXT("женщины") : TEXT("мужчины"), Rows.Num());
	if (Rows.Num() > Shown)
	{
		C += FString::Printf(TEXT(" (показаны первые %d по уровню — уточни поиск)"), Shown);
	}
	CountText->SetText(FText::FromString(Rows.Num() > Shown ? C : C + TEXT(" · по уровню")));
	WeightLabel->SetText(FText::FromString(WeightFilter > 0.f ? FString::Printf(TEXT("Вес: %s"), *Kg(WeightFilter)) : TEXT("Вес: все")));
	List->SetScrollOffset(Offset);
}

UWidget* UBoxingExhibitionWidget::MakeCard(const FRosterBoxer* B, bool bRed, bool bProFight)
{
	const FLinearColor Corner = bRed ? BoxUi::Red : BoxUi::Blue;
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* Cap = Txt(bRed ? TEXT("КРАСНЫЙ УГОЛ · ты") : TEXT("СИНИЙ УГОЛ · соперник"), 14, Corner, true);
	AddV(Col, Cap, false, FMargin(0.f, 0.f, 0.f, 8.f));
	if (!B)
	{
		UTextBlock* E = Txt(bRed ? TEXT("Выбери бойца в списке слева") : TEXT("Выбери соперника"), 19, BoxUi::Muted);
		E->SetAutoWrapText(true);
		AddV(Col, E, false, FMargin(0.f, 30.f));
		return Box(Col, BoxUi::Panel, 12.f, FMargin(20.f));
	}
	UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBox* Who = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* Name = Txt(B->Name, 24, BoxUi::Text, true);
	Name->SetAutoWrapText(true);
	AddV(Who, Name);
	AddV(Who, Txt(B->Origin(), 15, BoxUi::Muted), false, FMargin(0.f, 2.f, 0.f, 0.f));
	AddH(Top, Who, true, FMargin(0.f), VAlign_Top);
	const float Ovr = bProFight ? B->ProOverall : B->Overall;
	UVerticalBox* OvrCol = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(OvrCol, Txt(FString::Printf(TEXT("%.0f"), Ovr), 40, BoxUi::StatColor(Ovr), true), false, FMargin(0.f), HAlign_Right);
	AddV(OvrCol, Txt(TEXT("уровень"), 12, BoxUi::Muted), false, FMargin(0.f), HAlign_Right);
	AddH(Top, OvrCol, false, FMargin(12.f, 0.f, 0.f, 0.f), VAlign_Top);
	AddV(Col, Top, false, FMargin(0.f, 0.f, 0.f, 8.f));

	FString Body = FString::Printf(TEXT("%s · %s · рост %d · размах %d"), *AgeText(B->Age), *Kg(B->WeightKg),
		FMath::RoundToInt(B->HeightCm), FMath::RoundToInt(B->ReachCm));
	AddV(Col, Txt(Body, 15, BoxUi::Text), false, FMargin(0.f, 0.f, 0.f, 2.f));
	FString StyleLine = B->StyleLabel;
	if (B->Stance == TEXT("southpaw")) StyleLine += TEXT(" · левша");
	else if (B->Stance == TEXT("orthodox")) StyleLine += TEXT(" · правша");
	AddV(Col, Txt(StyleLine, 15, BoxUi::Accent, true), false, FMargin(0.f, 0.f, 0.f, 4.f));
	const FString Cred = Credentials(*B);
	// Регалии — до 3 строк в фиксированной высоте (карточки углов одной высоты), лишнее обрезается.
	UTextBlock* CredT = Txt(Cred.IsEmpty() ? TEXT(" ") : Cred, 13, BoxUi::Gold);
	CredT->SetAutoWrapText(true);
	UWidget* CredBox = Sized(CredT, 0.f, 54.f);
	CredBox->SetClipping(EWidgetClipping::ClipToBounds);
	AddV(Col, CredBox, false, FMargin(0.f, 0.f, 0.f, 8.f));

	const TArray<float>& St = bProFight ? B->ProStats : B->Stats;
	for (int32 I = 0; I < BoxStat::Num && I < St.Num(); ++I)
	{
		UHorizontalBox* L = WidgetTree->ConstructWidget<UHorizontalBox>();
		// Подпись — с запасом под «Подбородок» (на 118 px упиралась в полосу), полоса — с отступом.
		AddH(L, Sized(Txt(BoxStat::Label(I), 15, BoxUi::Muted), 128.f), false, FMargin(0.f, 0.f, 10.f, 0.f));
		AddH(L, Sized(Bar(BoxUi::StatColor(St[I]), St[I] / 100.f), 0.f, 10.f), true);
		UTextBlock* V = Txt(FString::Printf(TEXT("%.0f"), St[I]), 15, BoxUi::Text, true);
		V->SetJustification(ETextJustify::Right);
		AddH(L, Sized(V, 40.f));
		AddV(Col, L, false, FMargin(0.f, 3.f));
	}
	if (bProFight && B->Kind == ERosterKind::Amateur)
	{
		UTextBlock* N = Txt(TEXT("Профи-проекция: необстрелянный любитель теряет в технике, защите и кардио"), 13, BoxUi::Muted);
		N->SetAutoWrapText(true);
		AddV(Col, N, false, FMargin(0.f, 8.f, 0.f, 0.f));
	}
	UBorder* Card = Box(Col, BoxUi::Panel, 12.f, FMargin(20.f));
	Card->SetBrush(FSlateRoundedBoxBrush(BoxUi::Panel, 12.f, BoxUi::WithAlpha(Corner, 0.8f), 2.f));
	return Card;
}

int32 UBoxingExhibitionWidget::EffectiveRounds() const
{
	if (!IsProRules())
	{
		return 3;
	}
	if (ProRoundsSel > 0)
	{
		return ProRoundsSel;
	}
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	const FRosterBoxer* R = S ? S->FindById(RedId) : nullptr;
	const FRosterBoxer* B = S ? S->FindById(BlueId) : nullptr;
	return (R && B) ? UBoxingGameInstanceSubsystem::AutoProRounds(*R, *B) : 10;
}

void UBoxingExhibitionWidget::RebuildSide()
{
	Side->ClearChildren();
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	const FRosterBoxer* R = S ? S->FindById(RedId) : nullptr;
	const FRosterBoxer* B = S ? S->FindById(BlueId) : nullptr;
	const int32 Rounds = EffectiveRounds();
	const bool bProFight = Rounds > 3;

	UHorizontalBox* Cards = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Cards, MakeCard(R, true, bProFight), true, FMargin(0.f), VAlign_Fill);
	AddH(Cards, Txt(TEXT("VS"), 28, BoxUi::Muted, true), false, FMargin(14.f, 0.f), VAlign_Center);
	AddH(Cards, MakeCard(B, false, bProFight), true, FMargin(0.f), VAlign_Fill);
	AddV(Side, Cards, false, FMargin(0.f, 0.f, 0.f, 16.f));

	// Карточка пары (S-63 ← S-61): вес боя с проекцией (сгонка/переход вверх) и «честный» прогноз исхода ядром.
	if (R && B)
	{
		const FPairForecast& F = S->ForecastPair(*R, *B, Rounds, IsProRules());
		FString RT;
		FLinearColor RC;
		Realism(F.Stretch, RT, RC);
		UVerticalBox* PairCol = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* W = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(W, Txt(FString::Printf(TEXT("Вес боя %s"), *Kg(F.RingKg)), 17, BoxUi::Text, true), false, FMargin(0.f, 0.f, 14.f, 0.f));
		AddH(W, Txt(RT, 16, RC, true));
		AddV(PairCol, W, false, FMargin(0.f, 0.f, 0.f, 4.f));
		auto Shift = [](float Delta) -> FString
		{
			if (FMath::Abs(Delta) < 0.25f) return TEXT("в своём весе");
			return Delta > 0.f ? FString::Printf(TEXT("сгонка −%.1f кг"), Delta) : FString::Printf(TEXT("вверх +%.1f кг"), -Delta);
		};
		UHorizontalBox* Wd = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(Wd, Txt(FString::Printf(TEXT("%s: %s, %s"), *R->Name, *Kg(R->WeightKg), *Shift(F.RedDelta)), 14, BoxUi::Muted), true);
		UTextBlock* BW = Txt(FString::Printf(TEXT("%s: %s, %s"), *B->Name, *Kg(B->WeightKg), *Shift(F.BlueDelta)), 14, BoxUi::Muted);
		BW->SetJustification(ETextJustify::Right);
		AddH(Wd, BW, true);
		AddV(PairCol, Wd, false, FMargin(0.f, 0.f, 0.f, 10.f));
		if (F.bValid)
		{
			// S-67 ← S-65: два честных прогноза. Сверху — «при твоей игре» (красного ведёт бот «человека», фоновый расчёт:
			// блок заполнит FillPlayerOdds), ниже — нейтральный «ИИ против ИИ» (кто сильнее по ядру).
			PlayerOddsBox = WidgetTree->ConstructWidget<UVerticalBox>();
			AddV(PairCol, PlayerOddsBox, false, FMargin(0.f, 0.f, 0.f, 6.f));
			bPlayerOddsPending = true;
			FillPlayerOdds();
			// Нейтральный прогноз — одной строкой (карточка не должна выталкивать «В бой!» за край на 1080p).
			auto P = [](float V) { return FMath::RoundToInt(V * 100.f); };
			UHorizontalBox* Ai = WidgetTree->ConstructWidget<UHorizontalBox>();
			AddH(Ai, Txt(TEXT("По силам (ИИ против ИИ):"), 14, BoxUi::Muted, true), false, FMargin(0.f, 0.f, 8.f, 0.f), VAlign_Center);
			AddH(Ai, Txt(FString::Printf(TEXT("красный %d%%"), P(F.Odds.RedWin)), 14, FMath::Lerp(BoxUi::Red, FLinearColor::White, 0.3f), true),
				false, FMargin(0.f, 0.f, 8.f, 0.f), VAlign_Center);
			if (IsProRules())
			{
				AddH(Ai, Txt(FString::Printf(TEXT("ничья %d%%"), P(F.Odds.Draw)), 14, BoxUi::Muted), false, FMargin(0.f, 0.f, 8.f, 0.f), VAlign_Center);
			}
			AddH(Ai, Txt(FString::Printf(TEXT("синий %d%%"), P(F.Odds.BlueWin)), 14, FMath::Lerp(BoxUi::Blue, FLinearColor::White, 0.3f), true),
				false, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Center);
			UTextBlock* AiMore = Txt(FString::Printf(TEXT("· досрочно %d%% / %d%% · нокдаунов за бой %.1f"), P(F.Odds.RedStoppage),
				P(F.Odds.BlueStoppage), F.Odds.KnockdownsPerFight), 13, BoxUi::Muted);
			AiMore->SetAutoWrapText(true); // узкое окно — перенос, а не обрезка за край карточки
			AddH(Ai, AiMore, true, FMargin(0.f), VAlign_Center);
			AddV(PairCol, Ai);
		}
		else
		{
			PlayerOddsBox = nullptr;
			bPlayerOddsPending = false;
		}
		AddV(Side, Box(PairCol, BoxUi::WithAlpha(BoxUi::Panel, 0.7f), 10.f, FMargin(16.f, 12.f)), false, FMargin(0.f, 0.f, 0.f, 14.f));
	}

	// Раунды.
	UHorizontalBox* RoundsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(RoundsRow, Txt(TEXT("Раунды:"), 18, BoxUi::Text, true), false, FMargin(4.f, 0.f, 14.f, 0.f));
	if (!IsProRules())
	{
		AddH(RoundsRow, Txt(TEXT("3 — любительский бой (ничьих нет)"), 16, BoxUi::Muted));
	}
	else
	{
		const int32 AutoR = (R && B) ? UBoxingGameInstanceSubsystem::AutoProRounds(*R, *B) : 10;
		AddH(RoundsRow, Btn(FString::Printf(TEXT("авто (%d)"), AutoR), [this]() { ProRoundsSel = 0; RebuildSide(); }, EBoxBtn::Tab, 15,
			ProRoundsSel == 0), false, FMargin(0.f, 0.f, 6.f, 0.f));
		for (int32 N : {4, 6, 8, 10, 12})
		{
			AddH(RoundsRow, Btn(FString::FromInt(N), [this, N]() { ProRoundsSel = N; RebuildSide(); }, EBoxBtn::Tab, 15, ProRoundsSel == N),
				false, FMargin(0.f, 0.f, 6.f, 0.f));
		}
	}
	AddV(Side, RoundsRow, false, FMargin(0.f, 0.f, 0.f, 8.f));
	if (IsProRules())
	{
		AddV(Side, Txt(TEXT("Профи-правила: ничья возможна · 12 — титульный формат (авто: чемпион против чемпиона)"), 14, BoxUi::Muted),
			false, FMargin(4.f, 0.f, 0.f, 8.f));
	}

	// Досье: реальные заметки ростера (откуда регалии/пояса).
	UVerticalBox* Dossier = WidgetTree->ConstructWidget<UVerticalBox>();
	for (const FRosterBoxer* X : {R, B})
	{
		if (X && !X->Notes.IsEmpty())
		{
			UTextBlock* N = Txt(FString::Printf(TEXT("%s — %s"), *X->Name, *X->Notes), 14, BoxUi::Muted);
			N->SetAutoWrapText(true);
			AddV(Dossier, N, false, FMargin(0.f, 2.f));
		}
	}
	if (Dossier->GetChildrenCount() > 0)
	{
		AddV(Side, Txt(TEXT("Досье"), 15, BoxUi::Text, true), false, FMargin(4.f, 10.f, 0.f, 4.f));
		AddV(Side, Dossier, false, FMargin(4.f, 0.f));
	}

	AddV(Side, Spacer(1.f, 1.f), true);
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* SwapB = Btn(TEXT("Поменять углы"), [this]() { Swap(); }, EBoxBtn::Secondary, 18);
	SwapB->SetIsEnabled(R || B);
	AddH(Actions, SwapB, false, FMargin(0.f, 0.f, 14.f, 0.f), VAlign_Fill);
	UButton* Go = Btn(R && B ? FString::Printf(TEXT("В БОЙ!  ·  %d р."), Rounds) : TEXT("Выбери двух бойцов"), [this]() { Fight(); },
		EBoxBtn::Primary, 24);
	Go->SetIsEnabled(R && B);
	AddH(Actions, Go, true, FMargin(0.f), VAlign_Fill);
	AddV(Side, Actions);
}

void UBoxingExhibitionWidget::SetTab(ERosterKind Kind)
{
	if (Tab == Kind)
	{
		return;
	}
	Tab = Kind;
	RedId.Reset();
	BlueId.Reset();
	ProRoundsSel = 0;
	WeightFilter = 0.f;
	RebuildFilters();
	RebuildList();
	RebuildSide();
	List->ScrollToStart();
}

void UBoxingExhibitionWidget::SetFemale(bool bInFemale)
{
	if (bFemale == bInFemale)
	{
		return;
	}
	bFemale = bInFemale;
	RedId.Reset();
	BlueId.Reset();
	WeightFilter = 0.f;
	RebuildFilters();
	RebuildList();
	RebuildSide();
	List->ScrollToStart();
}

void UBoxingExhibitionWidget::CycleWeight()
{
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S)
	{
		return;
	}
	TArray<float> Weights;
	for (const FRosterBoxer& B : S->GetRoster())
	{
		if (B.Kind == Tab && B.bFemale == bFemale)
		{
			Weights.AddUnique(B.WeightKg);
		}
	}
	Weights.Sort();
	const int32 Cur = Weights.IndexOfByKey(WeightFilter);
	WeightFilter = (Cur == INDEX_NONE) ? (Weights.Num() ? Weights[0] : 0.f) : (Cur + 1 < Weights.Num() ? Weights[Cur + 1] : 0.f);
	RebuildList();
	List->ScrollToStart();
}

void UBoxingExhibitionWidget::OnSearchChanged(const FText& Text)
{
	Search = Text.ToString();
	RebuildList();
	List->ScrollToStart();
}

void UBoxingExhibitionWidget::Pick(const FString& Id)
{
	// Логика pick() веба: повторный клик снимает, иначе — в свободный угол, оба заняты — заменить синего.
	if (RedId == Id) { RedId.Reset(); }
	else if (BlueId == Id) { BlueId.Reset(); }
	else if (RedId.IsEmpty()) { RedId = Id; }
	else { BlueId = Id; }
	RebuildList();
	RebuildSide();
}

void UBoxingExhibitionWidget::Swap()
{
	::Swap(RedId, BlueId);
	RebuildList();
	RebuildSide();
}

void UBoxingExhibitionWidget::Fight()
{
	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S || !S->FindById(RedId) || !S->FindById(BlueId))
	{
		return;
	}
	FExhibitionSetup E;
	E.bValid = true;
	E.RedId = RedId;
	E.BlueId = BlueId;
	E.Rounds = EffectiveRounds();
	E.bProRules = IsProRules();
	E.Seed = FMath::Rand();
	E.Tab = Tab;
	E.bFemale = bFemale;
	S->SetExhibition(E);
	S->StartExhibitionFight(this);
}

void UBoxingExhibitionWidget::Back()
{
	if (ABoxingMenuPlayerController* PC = Cast<ABoxingMenuPlayerController>(GetOwningPlayer()))
	{
		PC->ShowMain();
	}
}

UWidget* UBoxingExhibitionWidget::MakeOddsBar(const BoxingFightProfile::FOutcomeOdds& O, float Height)
{
	// Полоса шансов: красный | ничья | синий (доли — веса заполнения слотов).
	UHorizontalBox* Split = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto Part = [this, Split, Height](float Share, const FLinearColor& C)
	{
		if (Share <= 0.005f)
		{
			return;
		}
		UHorizontalBoxSlot* PS = Split->AddChildToHorizontalBox(Sized(Box(nullptr, C, 3.f, FMargin(0.f)), 0.f, Height));
		FSlateChildSize Fill(ESlateSizeRule::Fill);
		Fill.Value = Share;
		PS->SetSize(Fill);
		PS->SetPadding(FMargin(1.f, 0.f));
	};
	Part(O.RedWin, BoxUi::Red);
	Part(O.Draw, BoxUi::Muted);
	Part(O.BlueWin, BoxUi::Blue);
	return Split;
}

void UBoxingExhibitionWidget::FillPlayerOdds()
{
	UVerticalBox* Box_ = PlayerOddsBox.Get();
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	const FRosterBoxer* R = S ? S->FindById(RedId) : nullptr;
	const FRosterBoxer* B = S ? S->FindById(BlueId) : nullptr;
	if (!Box_ || !R || !B)
	{
		bPlayerOddsPending = false;
		return;
	}
	// S-65 (fight-designer): красного (игрока) ведёт бот «человека» трёх уровней; пока считается в фоне (≈ 0.3–1.4 с) —
	// «считаем…» без цифры, той же высоты (карточка не прыгает). Подписи — Docs/UI.md «Шансы при твоей игре».
	const FPlayerForecast* PF = S->PlayerForecast(*R, *B, EffectiveRounds(), IsProRules());
	const bool bReady = PF && PF->bValid;
	if (bReady && !bPlayerOddsPending && Box_->GetChildrenCount() > 0)
	{
		return; // уже показан
	}
	if (!bReady && Box_->GetChildrenCount() > 0)
	{
		return; // заглушка уже стоит
	}
	Box_->ClearChildren();
	auto P = [](float V) { return FMath::RoundToInt(V * 100.f); };
	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Head, Txt(TEXT("Твои шансы при обычной игре"), 16, BoxUi::Text, true), false, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Bottom);
	AddH(Head, Txt(bReady ? FString::Printf(TEXT("≈ %d%%"), P(PF->Odds.Average.RedWin)) : FString(TEXT("считаем…")), bReady ? 20 : 16,
		bReady ? FMath::Lerp(BoxUi::Red, FLinearColor::White, 0.25f) : BoxUi::Muted, true), true, FMargin(0.f), VAlign_Bottom);
	if (bReady)
	{
		FString Right = FString::Printf(TEXT("соперник %d%%"), P(PF->Odds.Average.BlueWin));
		if (IsProRules())
		{
			Right = FString::Printf(TEXT("ничья %d%% · "), P(PF->Odds.Average.Draw)) + Right;
		}
		AddH(Head, Txt(Right, 15, FMath::Lerp(BoxUi::Blue, FLinearColor::White, 0.25f), true), false, FMargin(0.f), VAlign_Bottom);
	}
	AddV(Box_, Head, false, FMargin(0.f, 0.f, 0.f, 4.f));
	if (bReady)
	{
		AddV(Box_, MakeOddsBar(PF->Odds.Average, 12.f), false, FMargin(0.f, 0.f, 0.f, 4.f));
		UTextBlock* Range = Txt(FString::Printf(TEXT("от %d%% (новичок) до %d%% (сильная игра) · ты в красном углу, бот-игрок против ИИ"),
			P(PF->Odds.Novice.RedWin), P(PF->Odds.Strong.RedWin)), 13, BoxUi::Muted);
		Range->SetAutoWrapText(true);
		AddV(Box_, Range);
		bPlayerOddsPending = false;
	}
	else
	{
		AddV(Box_, Sized(Box(nullptr, BoxUi::WithAlpha(BoxUi::Line, 0.5f), 3.f, FMargin(0.f)), 0.f, 12.f), false, FMargin(1.f, 0.f, 1.f, 4.f));
		AddV(Box_, Txt(TEXT("ты в красном углу · бот-игрок против ИИ"), 13, BoxUi::Muted));
	}
}

void UBoxingExhibitionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bPlayerOddsPending)
	{
		FillPlayerOdds(); // фоновый прогноз «при твоей игре» готов — показать
	}
	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S || !S->bAuto || S->AutoFightsDone > 0)
	{
		return;
	}
	// Сценарий проверки: вкладка/пол → поиск и выбор пары → снимок → «В бой!».
	AutoTime += InDeltaTime;
	if (AutoStep == 0 && AutoTime > 0.8f)
	{
		AutoStep = 1;
		if (S->AutoTab == TEXT("pro")) SetTab(ERosterKind::Pro);
		else if (S->AutoTab == TEXT("legend")) SetTab(ERosterKind::Legend);
		else SetTab(ERosterKind::Amateur);
		SetFemale(S->AutoGender.Equals(TEXT("F"), ESearchCase::IgnoreCase));
	}
	else if (AutoStep == 1 && AutoTime > 1.6f)
	{
		AutoStep = 2;
		S->TakeUiShot(TEXT("exhibition_empty"));
	}
	else if (AutoStep == 2 && AutoTime > 2.4f)
	{
		AutoStep = 3;
		// QA S-71: пол — по найденному бойцу, если -BoxUiGender не задан, а на этом поле его нет («Балкибекова» без -BoxUiGender=F
		// уходила в «нет в ростере — беру сильнейшего»). Пара — одного пола, поэтому решает первый найденный.
		if (S->AutoGender.IsEmpty())
		{
			for (int32 I = 0; I < 2 && S->AutoPick.IsValidIndex(I); ++I)
			{
				if (S->FindByName(S->AutoPick[I], -1, bFemale ? 1 : 0))
				{
					break;
				}
				if (const FRosterBoxer* Other = S->FindByName(S->AutoPick[I], -1, bFemale ? 0 : 1))
				{
					UE_LOG(LogTemp, Log, TEXT("UI: сценарий — «%s» найден(а) среди %s, переключаю пол"), *S->AutoPick[I],
						Other->bFemale ? TEXT("женщин") : TEXT("мужчин"));
					SetFemale(Other->bFemale);
					break;
				}
			}
		}
		for (int32 I = 0; I < 2; ++I)
		{
			const FRosterBoxer* B = S->AutoPick.IsValidIndex(I) ? S->FindByName(S->AutoPick[I], static_cast<int32>(Tab), bFemale ? 1 : 0) : nullptr;
			if (!B && S->AutoPick.IsValidIndex(I))
			{
				// S-67: нет на вкладке — ищем по всем (Головкин — в «Легендах», а не в «Профи»); честно пишем в лог.
				B = S->FindByName(S->AutoPick[I], -1, bFemale ? 1 : 0);
				UE_LOG(LogTemp, Warning, TEXT("UI: сценарий — «%s» нет на вкладке, %s"), *S->AutoPick[I],
					B ? *FString::Printf(TEXT("взят с другой: %s"), *B->Name) : TEXT("нет в ростере — беру сильнейшего из списка"));
			}
			if (!B || B->Id == RedId)
			{
				// Не нашёлся (или совпал с красным) — сильнейший свободный из списка.
				B = nullptr;
				for (const FRosterBoxer* Row : Filtered())
				{
					if (Row->Id != RedId)
					{
						B = Row;
						break;
					}
				}
			}
			if (B)
			{
				Pick(B->Id);
			}
		}
		if (S->AutoRounds > 0 && IsProRules())
		{
			ProRoundsSel = S->AutoRounds;
			RebuildSide();
		}
		// Показать поиск в деле: вписать фамилию синего.
		if (S->AutoPick.IsValidIndex(1))
		{
			SearchBox->SetText(FText::FromString(S->AutoPick[1]));
			OnSearchChanged(SearchBox->GetText()); // программный SetText не шлёт OnTextChanged
		}
	}
	else if (AutoStep == 3 && AutoTime > 3.6f)
	{
		AutoStep = 4;
		S->TakeUiShot(TEXT("exhibition_search"));
	}
	else if (AutoStep == 4 && AutoTime > 4.2f)
	{
		// Очистка — отдельным кадром: снимок пишется в конце кадра запроса.
		AutoStep = 5;
		SearchBox->SetText(FText::GetEmpty());
		OnSearchChanged(FText::GetEmpty());
	}
	else if (AutoStep == 5 && AutoTime > 5.0f)
	{
		AutoStep = 6;
		S->TakeUiShot(TEXT("exhibition"));
	}
	else if (AutoStep == 6 && AutoTime > 5.8f)
	{
		AutoStep = 7;
		Fight();
	}
	else if (AutoStep == 7 && AutoTime > 120.f) // с экраном загрузки (S-63) первый бой может ждать предзагрузку
	{
		// Бой не стартовал (пара не собралась) — не висеть.
		AutoStep = 8;
		UE_LOG(LogTemp, Warning, TEXT("UI: сценарий — бой не начался, выход"));
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
	}
}
