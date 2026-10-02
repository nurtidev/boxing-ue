#include "UIFightHud.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "BoxingGameInstanceSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"

namespace
{
	constexpr float PanelW = 600.f;

	FLinearColor HealthColor(float H)
	{
		return H > 50.f ? BoxUi::Good : (H > 25.f ? BoxUi::Warn : BoxUi::Bad);
	}

	UCanvasPanelSlot* Place(UCanvasPanel* Root, UWidget* W, const FAnchors& A, const FVector2D& Align, const FMargin& Off)
	{
		UCanvasPanelSlot* S = Root->AddChildToCanvas(W);
		S->SetAnchors(A);
		S->SetAlignment(Align);
		S->SetOffsets(Off);
		S->SetAutoSize(true);
		return S;
	}
}

void UBoxingFightHudWidget::SetText(UTextBlock* T, const FString& S)
{
	if (T && T->GetText().ToString() != S)
	{
		T->SetText(FText::FromString(S));
	}
}

UWidget* UBoxingFightHudWidget::MakePanel(int32 Index)
{
	const bool bRight = Index == 1;
	const FLinearColor Corner = bRight ? BoxUi::Blue : BoxUi::Red;
	const EHorizontalAlignment HA = bRight ? HAlign_Right : HAlign_Left;
	FPanel& P = Panels[Index];

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	P.Name = Txt(TEXT(""), 26, BoxUi::Text, true);
	P.Name->SetShadowOffset(FVector2D(1.f, 1.f));
	P.Name->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	P.Origin = Txt(TEXT(""), 16, BoxUi::Muted, true);
	UWidget* Chip = Box(nullptr, Corner, 3.f, FMargin(0.f));
	if (!bRight)
	{
		AddH(NameRow, Sized(Chip, 8.f, 30.f), false, FMargin(0.f, 0.f, 12.f, 0.f));
		AddH(NameRow, P.Name);
		AddH(NameRow, P.Origin, false, FMargin(12.f, 0.f, 0.f, 0.f), VAlign_Center);
	}
	else
	{
		AddH(NameRow, P.Origin, false, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);
		AddH(NameRow, P.Name);
		AddH(NameRow, Sized(Chip, 8.f, 30.f), false, FMargin(12.f, 0.f, 0.f, 0.f));
	}
	AddV(Col, NameRow, false, FMargin(0.f, 0.f, 0.f, 8.f), HA);

	// Здоровье: полоса + число (читается и боковым зрением).
	UHorizontalBox* HRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	P.Health = Bar(BoxUi::Good, 1.f);
	P.Health->SetBarFillType(bRight ? EProgressBarFillType::RightToLeft : EProgressBarFillType::LeftToRight);
	P.HealthNum = Txt(TEXT("100"), 18, BoxUi::Text, true);
	P.HealthNum->SetJustification(bRight ? ETextJustify::Left : ETextJustify::Right);
	if (!bRight)
	{
		AddH(HRow, Sized(P.Health, 0.f, 22.f), true);
		AddH(HRow, Sized(P.HealthNum, 52.f));
	}
	else
	{
		AddH(HRow, Sized(P.HealthNum, 52.f));
		AddH(HRow, Sized(P.Health, 0.f, 22.f), true);
	}
	AddV(Col, HRow, false, FMargin(0.f, 0.f, 0.f, 6.f));

	P.Stamina = Bar(BoxUi::Warn, 1.f);
	P.Stamina->SetBarFillType(bRight ? EProgressBarFillType::RightToLeft : EProgressBarFillType::LeftToRight);
	AddV(Col, Sized(P.Stamina, 0.f, 10.f), false, bRight ? FMargin(52.f, 0.f, 0.f, 6.f) : FMargin(0.f, 0.f, 52.f, 6.f));

	P.Info = Txt(TEXT(""), 16, BoxUi::Muted, true);
	AddV(Col, P.Info, false, FMargin(0.f), HA);

	UBorder* Back = Box(Col, FLinearColor(0.f, 0.f, 0.f, 0.45f), 12.f, FMargin(18.f, 14.f));
	return Sized(Back, PanelW);
}

UWidget* UBoxingFightHudWidget::BuildUi()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();

	Place(Root, MakePanel(0), FAnchors(0.f, 0.f), FVector2D(0.f, 0.f), FMargin(32.f, 28.f, 0.f, 0.f));
	Place(Root, MakePanel(1), FAnchors(1.f, 0.f), FVector2D(1.f, 0.f), FMargin(-32.f, 28.f, 0.f, 0.f));

	// Раунд и часы — по центру сверху.
	UVerticalBox* Mid = WidgetTree->ConstructWidget<UVerticalBox>();
	RoundText = Txt(TEXT("РАУНД 1 / 3"), 17, BoxUi::Muted, true);
	ClockText = Txt(TEXT("0:00"), 44, BoxUi::Text, true);
	StatusText = Txt(TEXT(""), 15, BoxUi::Accent, true);
	AddV(Mid, RoundText, false, FMargin(0.f), HAlign_Center);
	AddV(Mid, ClockText, false, FMargin(0.f, -4.f, 0.f, 0.f), HAlign_Center);
	AddV(Mid, StatusText, false, FMargin(0.f), HAlign_Center);
	Place(Root, Box(Mid, FLinearColor(0.f, 0.f, 0.f, 0.5f), 12.f, FMargin(26.f, 10.f, 26.f, 12.f)), FAnchors(0.5f, 0.f), FVector2D(0.5f, 0.f),
		FMargin(0.f, 28.f, 0.f, 0.f));

	// Центральный баннер: нокдаун / перерыв / конец боя.
	UVerticalBox* Ban = WidgetTree->ConstructWidget<UVerticalBox>();
	BannerTitle = Txt(TEXT(""), 34, BoxUi::Gold, true);
	BannerBig = Txt(TEXT(""), 96, BoxUi::Text, true);
	BannerSub = Txt(TEXT(""), 19, BoxUi::Text);
	BannerSub->SetJustification(ETextJustify::Center);
	RiseBar = Bar(BoxUi::Accent, 0.f);
	AddV(Ban, BannerTitle, false, FMargin(0.f), HAlign_Center);
	AddV(Ban, BannerBig, false, FMargin(0.f, -10.f, 0.f, -6.f), HAlign_Center);
	AddV(Ban, BannerSub, false, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Center);
	AddV(Ban, Sized(RiseBar, 420.f, 14.f), false, FMargin(0.f, 4.f, 0.f, 0.f), HAlign_Center);
	Banner = Box(Ban, FLinearColor(0.f, 0.f, 0.f, 0.55f), 16.f, FMargin(40.f, 18.f));
	Place(Root, Banner, FAnchors(0.5f, 0.3f), FVector2D(0.5f, 0.5f), FMargin(0.f));
	Banner->SetVisibility(ESlateVisibility::Collapsed);

	// Подсказка по ситуации — над легендой управления.
	CueText = Txt(TEXT(""), 22, BoxUi::Text, true);
	CueBox = Box(CueText, BoxUi::WithAlpha(BoxUi::Bg, 0.7f), 10.f, FMargin(20.f, 8.f));
	Place(Root, CueBox, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FMargin(0.f, -128.f, 0.f, 0.f));
	CueBox->SetVisibility(ESlateVisibility::Collapsed);

	// Легенда управления.
	UVerticalBox* Ctl = WidgetTree->ConstructWidget<UVerticalBox>();
	auto Line = [this, Ctl](const TCHAR* Dev, const TCHAR* Keys)
	{
		UHorizontalBox* L = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(L, Sized(Txt(Dev, 15, BoxUi::Accent, true), 120.f));
		AddH(L, Txt(Keys, 15, BoxUi::Text));
		AddV(Ctl, L, false, FMargin(0.f, 2.f));
	};
	Line(TEXT("Клавиатура"), TEXT("J/K джеб/кросс · U/I хуки · N/M апперкоты · Shift+удар — в корпус · Пробел — блок · Q/E уклоны · WASD ноги · Shift+W/S пивот"));
	Line(TEXT("Геймпад"), TEXT("X/Y джеб/кросс · LB/RB хуки · A/B апперкоты · RT+удар — в корпус · LT — блок · правый стик — уклоны · левый стик — ноги"));
	AddV(Ctl, Txt(TEXT("F1 — скрыть подсказку · на нокдауне жми удары, чтобы встать"), 13, BoxUi::Muted), false, FMargin(0.f, 4.f, 0.f, 0.f));
	Controls = Box(Ctl, FLinearColor(0.f, 0.f, 0.f, 0.5f), 10.f, FMargin(18.f, 10.f));
	Place(Root, Controls, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FMargin(0.f, -24.f, 0.f, 0.f));

	AutoBadge = Box(Txt(TEXT("АВТОПИЛОТ · оба бойца под ИИ"), 15, BoxUi::Bg, true), BoxUi::Gold, 8.f, FMargin(14.f, 6.f));
	Place(Root, AutoBadge, FAnchors(0.5f, 1.f), FVector2D(0.5f, 1.f), FMargin(0.f, -28.f, 0.f, 0.f));
	AutoBadge->SetVisibility(ESlateVisibility::Collapsed);
	return Root;
}

void UBoxingFightHudWidget::SetControlsVisible(bool bVisible)
{
	bControls = bVisible;
}

void UBoxingFightHudWidget::UpdatePanel(int32 Index, const ABoxingFightGameMode& GM, float Time)
{
	FPanel& P = Panels[Index];
	const FFighterState& F = GM.GetSnapshot().Fighters[Index];
	P.Health->SetPercent(F.Health / 100.f);
	P.Health->SetFillColorAndOpacity(HealthColor(F.Health));
	SetText(P.HealthNum, FString::FromInt(FMath::CeilToInt(FMath::Max(0.f, F.Health))));
	P.Stamina->SetPercent(F.StaminaPct / 100.f);
	// Пустой бак: полоса мигает красным (как FighterHud.gassed веба).
	const bool bBlink = F.bGassed && FMath::Fmod(Time, 0.5f) < 0.25f;
	P.Stamina->SetFillColorAndOpacity(F.bGassed ? (bBlink ? BoxUi::Bad : BoxUi::Dim(BoxUi::Warn, 0.7f)) : (F.StaminaPct < 25.f ? BoxUi::Dim(BoxUi::Warn, 0.75f) : BoxUi::Warn));

	FString Info;
	if (F.Knockdowns > 0)
	{
		Info = FString::Printf(TEXT("нокдаунов: %d"), F.Knockdowns);
	}
	if (F.bGassed)
	{
		Info += (Info.IsEmpty() ? TEXT("") : TEXT(" · ")) + FString(TEXT("нет сил — отдышись"));
	}
	SetText(P.Info, Info);
	P.Info->SetColorAndOpacity(FSlateColor(F.bGassed ? BoxUi::Bad : BoxUi::Muted));
}

void UBoxingFightHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UWorld* W = GetWorld();
	const ABoxingFightGameMode* GM = W ? W->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	if (!GM || !GM->IsFightStarted())
	{
		return;
	}
	Clock += InDeltaTime;
	const FFightSnapshot& S = GM->GetSnapshot();
	const bool bHuman = !GM->IsAutopilot();
	const int32 Me = GM->GetPlayerIndex();

	if (!bNamesSet)
	{
		bNamesSet = true;
		const UBoxingGameInstanceSubsystem* Sub = UBoxingGameInstanceSubsystem::Get(this);
		const FExhibitionSetup* E = Sub && Sub->GetExhibition().bValid ? &Sub->GetExhibition() : nullptr;
		for (int32 I = 0; I < 2; ++I)
		{
			const FBoxerPreset& Pr = I == 0 ? GM->RedPreset : GM->BluePreset;
			SetText(Panels[I].Name, Pr.Name + (I == Me && bHuman ? TEXT(" (ты)") : TEXT("")));
			const FRosterBoxer* RB = E && Sub ? Sub->FindById(I == 0 ? E->RedId : E->BlueId) : nullptr;
			SetText(Panels[I].Origin, RB && RB->Name == Pr.Name ? RB->CountryCode : TEXT(""));
		}
	}
	UpdatePanel(0, *GM, Clock);
	UpdatePanel(1, *GM, Clock);

	// Раунд и часы.
	const float Left = S.Phase == EFightPhase::Between ? S.BreakLeft : S.TimeLeft;
	const int32 Sec = FMath::Max(0, FMath::CeilToInt(Left));
	SetText(RoundText, FString::Printf(TEXT("РАУНД %d / %d"), S.Round, S.TotalRounds));
	SetText(ClockText, FString::Printf(TEXT("%d:%02d"), Sec / 60, Sec % 60));
	ClockText->SetColorAndOpacity(FSlateColor(S.Phase == EFightPhase::Fighting && Left <= 10.f ? BoxUi::Warn : BoxUi::Text));
	const TCHAR* Status = S.Phase == EFightPhase::Between ? TEXT("перерыв") : (S.Phase == EFightPhase::Down ? TEXT("счёт") : (S.Phase == EFightPhase::Over ? TEXT("бой окончен") : TEXT("")));
	SetText(StatusText, Status);
	StatusText->SetVisibility(*Status ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// Баннер.
	ESlateVisibility BanVis = ESlateVisibility::HitTestInvisible;
	switch (S.Phase)
	{
	case EFightPhase::Down:
	{
		const bool bMine = S.DownWho == Me && bHuman;
		const FBoxerPreset& Down = S.DownWho == 1 ? GM->BluePreset : GM->RedPreset;
		SetText(BannerTitle, TEXT("НОКДАУН"));
		SetText(BannerBig, FString::FromInt(FMath::Max(1, S.DownCount)));
		SetText(BannerSub, bMine ? TEXT("Жми удары или блок — вставай!") : FString::Printf(TEXT("%s на настиле"), *Down.Name));
		RiseBar->SetVisibility(bMine ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		RiseBar->SetPercent(S.RiseProgress);
		break;
	}
	case EFightPhase::Between:
		SetText(BannerTitle, FString::Printf(TEXT("КОНЕЦ РАУНДА %d"), S.Round));
		SetText(BannerBig, FString::FromInt(Sec));
		SetText(BannerSub, FString::Printf(TEXT("Раунд %d через %d с"), S.Round + 1, Sec));
		RiseBar->SetVisibility(ESlateVisibility::Collapsed);
		break;
	default:
		BanVis = ESlateVisibility::Collapsed;
		break;
	}
	Banner->SetVisibility(BanVis);

	// Подсказка по ситуации (только человеку и только в бою).
	FString Cue;
	FLinearColor CueCol = BoxUi::Text;
	if (bHuman && S.Phase == EFightPhase::Fighting)
	{
		const FFighterState& F = S.Fighters[Me];
		if (F.bGassed) { Cue = TEXT("Нет сил — отойди и отдышись"); CueCol = BoxUi::Bad; }
		else if (F.bBlocking && F.GuardIntegrity < 0.35f) { Cue = TEXT("Руки устают — блок вот-вот пробьют"); CueCol = BoxUi::Warn; }
		else if (F.bAngle) { Cue = TEXT("Угол открыт — бей!"); CueCol = BoxUi::Good; }
		else if (F.RopeLevel >= 2) { Cue = TEXT("Зажат в углу — уходи пивотом (Shift+W/S)"); CueCol = BoxUi::Warn; }
		else if (F.RopeLevel == 1) { Cue = TEXT("Спиной в канатах — уходи по дуге (W/S)"); CueCol = BoxUi::Warn; }
	}
	SetText(CueText, Cue);
	CueText->SetColorAndOpacity(FSlateColor(CueCol));
	CueBox->SetVisibility(Cue.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	const bool bShowControls = bHuman && bControls && S.Phase != EFightPhase::Over;
	Controls->SetVisibility(bShowControls ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	AutoBadge->SetVisibility(!bHuman && S.Phase != EFightPhase::Over ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// Сценарий проверки: снимки боя, нокдауна, перерыва.
	const UBoxingGameInstanceSubsystem* Sub = UBoxingGameInstanceSubsystem::Get(this);
	if (Sub && Sub->bAuto)
	{
		FightTime = S.Phase == EFightPhase::Fighting ? FightTime + InDeltaTime : FightTime;
		DownTime = S.Phase == EFightPhase::Down ? DownTime + InDeltaTime : 0.f;
		BreakTime = S.Phase == EFightPhase::Between ? BreakTime + InDeltaTime : 0.f;
		if (!bShotHud && FightTime > 6.f) { bShotHud = true; Sub->TakeUiShot(TEXT("hud")); }
		if (!bShotKd && DownTime > 0.8f) { bShotKd = true; Sub->TakeUiShot(TEXT("hud_knockdown")); }
		if (!bShotBreak && BreakTime > 1.f) { bShotBreak = true; Sub->TakeUiShot(TEXT("hud_break")); }
	}
}
