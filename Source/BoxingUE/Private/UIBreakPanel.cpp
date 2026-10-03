#include "UIBreakPanel.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

namespace
{
	constexpr float BpPanelW = 560.f;
	constexpr float BpPadX = 24.f;
	constexpr float BpJudgeLabelW = 92.f;
	constexpr float BpStatLabelW = 210.f;
	constexpr float BpSeatSettle = 1.0f;   // без флага «сел»: дошёл до угла + столько секунд (посадка на стул)
	constexpr float BpUnlockSafety = 6.0f; // страховка: «Продолжить» доступна не позже (REST_UNLOCK_MS веба)
	constexpr float BpFadeIn = 0.25f;

	FLinearColor BpToneColor(CornerAdvice::ETone T)
	{
		switch (T)
		{
		case CornerAdvice::ETone::Good: return BoxUi::Good;
		case CornerAdvice::ETone::Bad: return BoxUi::Bad;
		case CornerAdvice::ETone::Warn: return BoxUi::Warn;
		default: return BoxUi::Text;
		}
	}

	FLinearColor BpCardColor(int32 Mine, int32 His)
	{
		return Mine > His ? BoxUi::Good : (His > Mine ? BoxUi::Bad : BoxUi::Muted);
	}

	FString BpSurname(const FString& Name)
	{
		FString L, R;
		if (Name.EndsWith(TEXT("угол")))
		{
			return TEXT("Соперник"); // пресеты карты без Выставки: «Синий угол»
		}
		return Name.TrimStartAndEnd().Split(TEXT(" "), &L, &R, ESearchCase::IgnoreCase, ESearchDir::FromEnd) ? R : Name;
	}

	FString BpClock(float Sec)
	{
		const int32 S = FMath::Max(0, FMath::CeilToInt(Sec));
		return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60);
	}
}

void UBoxingBreakPanelWidget::SetText(UTextBlock* T, const FString& S)
{
	if (T && T->GetText().ToString() != S)
	{
		T->SetText(FText::FromString(S));
	}
}

UWidget* UBoxingBreakPanelWidget::BuildUi()
{
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();

	// Шапка: раунд и вердикт судей.
	RoundText = Txt(TEXT("РАУНД 1 / 3 · ПЕРЕРЫВ"), 15, BoxUi::Muted, true);
	AddV(Col, RoundText, false, FMargin(0.f, 0.f, 0.f, 2.f));
	VerdictText = Txt(TEXT(""), 24, BoxUi::Text, true);
	VerdictText->SetAutoWrapText(true);
	AddV(Col, VerdictText, false, FMargin(0.f, 0.f, 0.f, 10.f));

	// Карты судей: номер, раунд, всего.
	JudgeHead = WidgetTree->ConstructWidget<UHorizontalBox>();
	JudgeRound = WidgetTree->ConstructWidget<UHorizontalBox>();
	JudgeTotal = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBox* Cards = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Cards, JudgeHead, false, FMargin(0.f, 0.f, 0.f, 4.f));
	AddV(Cards, JudgeRound, false, FMargin(0.f, 0.f, 0.f, 4.f));
	AddV(Cards, JudgeTotal);
	AddV(Col, Box(Cards, BoxUi::WithAlpha(BoxUi::Panel, 0.85f), 8.f, FMargin(12.f, 8.f)), false, FMargin(0.f, 0.f, 0.f, 12.f));

	// Статистика раунда.
	StatsBox = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Col, StatsBox, false, FMargin(0.f, 0.f, 0.f, 12.f));

	// Дыхание: светлая — с чем выйдешь на раунд, сплошная — сейчас.
	UHorizontalBox* Br = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Br, Sized(Txt(TEXT("Дыхание"), 16, BoxUi::Muted, true), 96.f));
	UOverlay* Ov = WidgetTree->ConstructWidget<UOverlay>();
	BreathGain = Bar(BoxUi::WithAlpha(BoxUi::Warn, 0.35f), 0.f);
	BreathNow = Bar(BoxUi::Warn, 0.f);
	{
		FProgressBarStyle St = BreathNow->GetWidgetStyle();
		St.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.f));
		BreathNow->SetWidgetStyle(St);
	}
	Cast<UOverlaySlot>(Ov->AddChild(BreathGain))->SetHorizontalAlignment(HAlign_Fill);
	Cast<UOverlaySlot>(Ov->AddChild(BreathNow))->SetHorizontalAlignment(HAlign_Fill);
	AddH(Br, Sized(Ov, 0.f, 16.f), true);
	BreathNum = Txt(TEXT(""), 18, BoxUi::Text, true);
	BreathNum->SetJustification(ETextJustify::Right);
	AddH(Br, Sized(BreathNum, 104.f));
	AddV(Col, Br, false, FMargin(0.f, 0.f, 0.f, 2.f));
	BreathNote = Txt(TEXT(""), 14, BoxUi::Muted);
	BreathNote->SetAutoWrapText(true);
	AddV(Col, BreathNote, false, FMargin(96.f, 0.f, 0.f, 0.f), HAlign_Left);
	AddV(Col, Spacer(1.f, 10.f));

	// Совет угла.
	TipsBox = WidgetTree->ConstructWidget<UVerticalBox>();
	TipsSection = TipsBox;
	AddV(Col, TipsBox, false, FMargin(0.f, 0.f, 0.f, 6.f));

	// «Продолжить» / «Садится в угол…».
	UHorizontalBox* Bt = WidgetTree->ConstructWidget<UHorizontalBox>();
	WaitDot = Box(nullptr, BoxUi::Accent, 5.f, FMargin(0.f));
	AddH(Bt, Sized(WaitDot, 10.f, 10.f), false, FMargin(0.f, 0.f, 12.f, 0.f));
	ButtonText = Txt(TEXT("Садится в угол…"), 20, BoxUi::Text, true);
	AddH(Bt, ButtonText, true);
	ButtonKeys = Txt(TEXT("Enter / A"), 16, BoxUi::Bg, true);
	AddH(Bt, ButtonKeys);
	// Кисть белая, цвет — BrushColor (он умножается на цвет кисти: иначе Accent × PanelHi = почти чёрный).
	ButtonBox = Box(Bt, FLinearColor::White, 10.f, FMargin(18.f, 0.f));
	ButtonBox->SetBrushColor(BoxUi::PanelHi);
	ButtonBox->SetVerticalAlignment(VAlign_Center);
	AddV(Col, Sized(ButtonBox, 0.f, 56.f), false, FMargin(0.f, 6.f, 0.f, 6.f));

	FooterText = Txt(TEXT(""), 15, BoxUi::Muted);
	AddV(Col, FooterText, false, FMargin(0.f), HAlign_Center);

	UBorder* Back = Box(Col, FLinearColor(0.03f, 0.04f, 0.06f, 0.88f), 14.f, FMargin(BpPadX, 18.f, BpPadX, 14.f));
	return Sized(Back, BpPanelW);
}

void UBoxingBreakPanelWidget::NativeDestruct()
{
	if (ABoxingFightGameMode* GM = BoundGM.Get())
	{
		GM->OnFightEventUi.Remove(EventHandle);
	}
	BoundGM.Reset();
	Super::NativeDestruct();
}

void UBoxingBreakPanelWidget::Bind(ABoxingFightGameMode& GM)
{
	if (BoundGM.Get() == &GM)
	{
		return;
	}
	BoundGM = &GM;
	Tally = CornerAdvice::FCornerTally(GM.GetPlayerIndex());
	FMemory::Memzero(PrevTotals);
	EventHandle = GM.OnFightEventUi.AddLambda([this](const FFightEvent& E) { Tally.Feed(E); });
}

bool UBoxingBreakPanelWidget::PlayerSeated(const ABoxingFightGameMode& GM) const
{
	// Флаг game-feel (S-71): ABoxerCharacter::IsSeatedInCorner() — BlueprintPure, через рефлексию (сборка не зависит от порядка правок).
	ABoxerCharacter* B = GM.GetBoxer(GM.GetPlayerIndex());
	static const FName SeatFn(TEXT("IsSeatedInCorner"));
	UFunction* Fn = B ? B->FindFunction(SeatFn) : nullptr;
	if (!Fn)
	{
		return ArrivedT >= 0.f && BreakT - ArrivedT >= BpSeatSettle;
	}
	TArray<uint8> Buf;
	Buf.SetNumZeroed(FMath::Max<int32>(1, Fn->ParmsSize));
	B->ProcessEvent(Fn, Buf.GetData());
	const FBoolProperty* Ret = CastField<FBoolProperty>(Fn->GetReturnProperty());
	return Ret && Ret->GetPropertyValue_InContainer(Buf.GetData());
}

void UBoxingBreakPanelWidget::EnterBreak(ABoxingFightGameMode& GM, bool bHuman)
{
	using namespace CornerAdvice;
	const FFightSnapshot& S = GM.GetSnapshot();
	const int32 Me = GM.GetPlayerIndex();
	const int32 N = FMath::Clamp(S.NumJudges, 1, MAX_JUDGES);
	bInBreak = true;
	bReady = false;
	bHumanBreak = bHuman;
	BreakRound = S.Round;
	BreakT = 0.f;
	ArrivedT = -1.f;
	StartStam = S.Fighters[Me].StaminaPct;

	FGender G;
	G.bMeFemale = (Me == 0 ? GM.RedPreset : GM.BluePreset).bFemale;
	G.bFoeFemale = (Me == 0 ? GM.BluePreset : GM.RedPreset).bFemale;
	FJudgeCard Cards[MAX_JUDGES];
	RoundCards(S.JudgeTotals, PrevTotals, N, Cards);
	FMemory::Memcpy(PrevTotals, S.JudgeTotals, sizeof(PrevTotals));
	FVerdict V = RoundVerdict(Cards, N, G, Me == 0);
	if (!bHuman && V.bValid)
	{
		// Автопилот: игрока нет — вердикт от лица зрителя, по углам.
		int32 R = 0, B = 0;
		for (const FJudgeCard& C : V.Cards) { R += C.Red > C.Blue; B += C.Blue > C.Red; }
		V.Text = R > B ? FString::Printf(TEXT("Раунд красному — %d:%d у судей"), R, B) : (B > R ? FString::Printf(TEXT("Раунд синему — %d:%d у судей"), B, R) : TEXT("Раунд равный"));
	}
	const FTalk Talk = CornerTalk(Tally.Stats(), V, G);

	SetText(RoundText, FString::Printf(TEXT("РАУНД %d / %d · ПЕРЕРЫВ"), S.Round, S.TotalRounds));
	SetText(VerdictText, V.bValid ? V.Text : TEXT("Раунд не судили"));
	VerdictText->SetColorAndOpacity(FSlateColor(V.bValid ? (bHuman ? BpToneColor(V.Tone) : BoxUi::Text) : BoxUi::Muted));

	// Карты судей.
	const float ColW = (BpPanelW - 2.f * BpPadX - 24.f - BpJudgeLabelW) / N;
	JudgeHead->ClearChildren();
	JudgeRound->ClearChildren();
	JudgeTotal->ClearChildren();
	AddH(JudgeHead, Sized(Txt(TEXT("Судья"), 14, BoxUi::Muted, true), BpJudgeLabelW));
	AddH(JudgeRound, Sized(Txt(FString::Printf(TEXT("Раунд %d"), S.Round), 16, BoxUi::Text, true), BpJudgeLabelW));
	AddH(JudgeTotal, Sized(Txt(TEXT("Всего"), 16, BoxUi::Muted, true), BpJudgeLabelW));
	for (int32 J = 0; J < N; ++J)
	{
		const int32 MineR = Me == 0 ? Cards[J].Red : Cards[J].Blue, HisR = Me == 0 ? Cards[J].Blue : Cards[J].Red;
		const int32 MineT = Me == 0 ? S.JudgeTotals[J].Red : S.JudgeTotals[J].Blue, HisT = Me == 0 ? S.JudgeTotals[J].Blue : S.JudgeTotals[J].Red;
		UTextBlock* H = Txt(FString::FromInt(J + 1), 14, BoxUi::Muted, true);
		H->SetJustification(ETextJustify::Center);
		AddH(JudgeHead, Sized(H, ColW));
		// Автопилот: цвет — чей раунд по углам (красный/синий); игрок — его/не его.
		const FLinearColor RC = bHuman ? BpCardColor(MineR, HisR) : (MineR > HisR ? BoxUi::Red : (HisR > MineR ? BoxUi::Blue : BoxUi::Muted));
		const FLinearColor TC = bHuman ? BpCardColor(MineT, HisT) : (MineT > HisT ? BoxUi::Red : (HisT > MineT ? BoxUi::Blue : BoxUi::Muted));
		UTextBlock* R = Txt(V.bValid ? FString::Printf(TEXT("%d–%d"), MineR, HisR) : TEXT("—"), 18, RC, true);
		R->SetJustification(ETextJustify::Center);
		AddH(JudgeRound, Sized(R, ColW));
		UTextBlock* T = Txt(FString::Printf(TEXT("%d–%d"), MineT, HisT), 16, BoxUi::Dim(TC, 0.85f), true);
		T->SetJustification(ETextJustify::Center);
		AddH(JudgeTotal, Sized(T, ColW));
	}

	// Статистика раунда: ты / соперник.
	const FRoundStats& St = Tally.Stats();
	const FString MeName = bHuman ? FString(TEXT("Ты")) : BpSurname((Me == 0 ? GM.RedPreset : GM.BluePreset).Name);
	const FString FoeName = BpSurname((Me == 0 ? GM.BluePreset : GM.RedPreset).Name);
	StatsBox->ClearChildren();
	auto Row = [this](const FString& Label, const FString& A, const FString& B, const FLinearColor& CA, const FLinearColor& CB, bool bHead)
	{
		UHorizontalBox* L = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(L, Sized(Txt(Label, bHead ? 14 : 16, BoxUi::Muted, bHead), BpStatLabelW));
		UTextBlock* TA = Txt(A, bHead ? 15 : 18, CA, true);
		UTextBlock* TB = Txt(B, bHead ? 15 : 18, CB, true);
		TA->SetJustification(ETextJustify::Center);
		TB->SetJustification(ETextJustify::Center);
		AddH(L, TA, true);
		AddH(L, TB, true);
		AddV(StatsBox, L, false, FMargin(0.f, 1.f));
	};
	const FLinearColor MeC = Me == 0 ? BoxUi::Red : BoxUi::Blue, FoeC = Me == 0 ? BoxUi::Blue : BoxUi::Red;
	Row(TEXT("Статистика раунда"), MeName, FoeName, FMath::Lerp(MeC, BoxUi::Text, 0.35f), FMath::Lerp(FoeC, BoxUi::Text, 0.35f), true);
	auto Acc = [](int32 L, int32 T) { return T > 0 ? FMath::RoundToInt(100.f * L / T) : 0; };
	Row(TEXT("Попадания / удары"), FString::Printf(TEXT("%d / %d"), St.Landed, St.Thrown), FString::Printf(TEXT("%d / %d"), St.FoeLanded, St.FoeThrown),
		BoxUi::Text, BoxUi::Text, false);
	Row(TEXT("Точность"), FString::Printf(TEXT("%d%%"), Acc(St.Landed, St.Thrown)), FString::Printf(TEXT("%d%%"), Acc(St.FoeLanded, St.FoeThrown)), BoxUi::Text, BoxUi::Text, false);
	Row(TEXT("В корпус"), FString::Printf(TEXT("%d / %d"), St.BodyLanded, St.BodyThrown), FString::Printf(TEXT("%d / %d"), St.FoeBodyLanded, St.FoeBodyThrown),
		BoxUi::Text, BoxUi::Text, false);
	Row(TEXT("Нокдауны (уронил)"), FString::FromInt(St.KdGiven), FString::FromInt(St.KdTaken), St.KdGiven > 0 ? BoxUi::Good : BoxUi::Text,
		St.KdTaken > 0 ? BoxUi::Bad : BoxUi::Text, false);

	// Совет угла — только игроку.
	TipsBox->ClearChildren();
	TipsSection->SetVisibility(bHuman ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bHuman)
	{
		for (int32 I = 0; I < Talk.Tips.Num(); ++I)
		{
			const FTip& Tip = Talk.Tips[I];
			UVerticalBox* TC = WidgetTree->ConstructWidget<UVerticalBox>();
			// Кто говорит — один раз на подряд идущие реплики одного углового.
			if (I == 0 || Talk.Tips[I - 1].Who != Tip.Who)
			{
				AddV(TC, Txt(Tip.Who == EWho::Trainer ? TEXT("ТРЕНЕР") : TEXT("КАТМЕН"), 13, BoxUi::Muted, true), false, FMargin(0.f, 0.f, 0.f, 2.f));
			}
			UTextBlock* Line = Txt(Tip.Text, 18, BoxUi::Text);
			Line->SetAutoWrapText(true);
			AddV(TC, Line);
			UHorizontalBox* Item = WidgetTree->ConstructWidget<UHorizontalBox>();
			AddH(Item, Sized(Box(nullptr, BpToneColor(Tip.Tone), 2.f, FMargin(0.f)), 4.f), false, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Fill);
			AddH(Item, TC, true);
			AddV(TipsBox, Box(Item, BoxUi::WithAlpha(BoxUi::Panel, 0.7f), 8.f, FMargin(10.f, 8.f, 12.f, 8.f)), false, FMargin(0.f, 0.f, 0.f, 6.f));
		}
	}
	ButtonBox->GetParent()->SetVisibility(bHuman ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	SetRenderOpacity(0.f);
	UE_LOG(LogTemp, Log, TEXT("UI: перерыв после раунда %d — %s; советов %d; %s"), S.Round, *V.Text, Talk.Tips.Num(), *Talk.Line);
	for (const FTip& Tip : Talk.Tips)
	{
		UE_LOG(LogTemp, Log, TEXT("UI:   %s: %s"), Tip.Who == EWho::Trainer ? TEXT("тренер") : TEXT("катмен"), *Tip.Text);
	}
}

void UBoxingBreakPanelWidget::UpdateFrom(ABoxingFightGameMode& GM, float Dt, bool bHuman)
{
	Bind(GM);
	const FFightSnapshot& S = GM.GetSnapshot();
	Tally.Sample(S, Dt);

	if (S.Phase != EFightPhase::Between)
	{
		if (bInBreak)
		{
			bInBreak = false;
			UE_LOG(LogTemp, Log, TEXT("UI: перерыв окончен через %.1f с (%s)"), BreakT, bReady ? TEXT("игрок готов") : TEXT("по таймеру"));
		}
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (!bInBreak || BreakRound != S.Round)
	{
		EnterBreak(GM, bHuman);
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	BreakT += Dt;
	const float Fade = FMath::Clamp(BreakT / BpFadeIn, 0.f, 1.f);
	SetRenderOpacity(Fade);
	SetRenderTranslation(FVector2D(24.f * (1.f - Fade) * (1.f - Fade), 0.f));

	const int32 Me = GM.GetPlayerIndex();
	if (S.Stage.Kind == ERingStageKind::None || (S.Stage.Kind == ERingStageKind::Rest && S.Stage.bArrived[Me]))
	{
		if (ArrivedT < 0.f) ArrivedT = BreakT;
	}
	if (!bReady && (PlayerSeated(GM) || BreakT >= BpUnlockSafety))
	{
		bReady = true;
		UE_LOG(LogTemp, Log, TEXT("UI: «Продолжить» доступна через %.2f с перерыва (%s)"), BreakT,
			BreakT >= BpUnlockSafety ? TEXT("страховка") : TEXT("игрок сел"));
	}

	// Дыхание.
	const float Now = S.Fighters[Me].StaminaPct;
	const float Target = GM.GetCore().GetCornerRestTargetPct(Me);
	const float Goal = Target >= 0.f ? FMath::Max(Now, Target) : Now;
	BreathNow->SetPercent(Now / 100.f);
	BreathGain->SetPercent(Goal / 100.f);
	// Итог угла ниже нынешнего — «бак» на дистанцию стал меньше (ScoreRound режет потолок): честно показываем, куда сядет.
	const int32 NowI = FMath::RoundToInt(Now), TargetI = Target >= 0.f ? FMath::RoundToInt(Target) : NowI;
	SetText(BreathNum, TargetI != NowI ? FString::Printf(TEXT("%d → %d"), NowI, TargetI) : FString::FromInt(NowI));
	BreathNum->SetColorAndOpacity(FSlateColor(TargetI < NowI ? BoxUi::Muted : BoxUi::Text));
	const bool bCapped = Target >= 0.f && Target < StartStam - 0.5f;
	SetText(BreathNote, bCapped ? FString::Printf(TEXT("Силы на дистанцию тают — выше %d уже не отдышаться"), TargetI)
		: (TargetI > NowI ? TEXT("Отдыхаешь — силы возвращаются") : TEXT("")));
	BreathNote->SetVisibility(BreathNote->GetText().IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	BreathNow->SetFillColorAndOpacity(Now < 25.f ? BoxUi::Dim(BoxUi::Warn, 0.8f) : BoxUi::Warn);

	// Кнопка и подвал.
	const FString Next = S.Round < S.TotalRounds ? FString::Printf(TEXT("Раунд %d"), S.Round + 1) : FString(TEXT("Следующий раунд"));
	if (bHumanBreak)
	{
		if (bReady)
		{
			SetText(ButtonText, TEXT("Продолжить →"));
			ButtonText->SetColorAndOpacity(FSlateColor(BoxUi::Bg));
			ButtonBox->SetBrushColor(BoxUi::Accent);
			ButtonKeys->SetVisibility(ESlateVisibility::HitTestInvisible);
			WaitDot->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			SetText(ButtonText, TEXT("Садится в угол…"));
			ButtonText->SetColorAndOpacity(FSlateColor(BoxUi::Text));
			ButtonBox->SetBrushColor(BoxUi::PanelHi);
			ButtonKeys->SetVisibility(ESlateVisibility::Collapsed);
			WaitDot->SetVisibility(ESlateVisibility::HitTestInvisible);
			WaitDot->SetRenderOpacity(0.35f + 0.65f * (0.5f + 0.5f * FMath::Sin(BreakT * PI / 0.8f)));
		}
		SetText(FooterText, FString::Printf(TEXT("%s через %s — или раньше, когда отдышишься"), *Next, *BpClock(S.BreakLeft)));
	}
	else
	{
		SetText(FooterText, FString::Printf(TEXT("%s через %d с"), *Next, FMath::Max(0, FMath::CeilToInt(S.BreakLeft))));
	}
}
