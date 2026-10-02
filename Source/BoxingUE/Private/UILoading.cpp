#include "UILoading.h"

#include "BoxingGameInstanceSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Ширина колонки угла (имя переносится, если длинное).
	constexpr float CornerW = 560.f;
	constexpr float CardW = 1320.f;
}

// ======================================================================
// UMG: поверх меню / итога, пока догружаются ассеты боя
// ======================================================================
UWidget* UBoxingLoadingWidget::BuildUi()
{
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	UBorder* Back = Box(nullptr, BoxUi::Bg, 0.f, FMargin(0.f)); // непрозрачный: меню под ним не просвечивает
	UCanvasPanelSlot* BackSlot = Root->AddChildToCanvas(Back);
	BackSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BackSlot->SetOffsets(FMargin(0.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* Info = Txt(S ? S->LoadingInfo() : FString(), 20, BoxUi::Muted);
	Info->SetJustification(ETextJustify::Center);
	AddV(Col, Info, false, FMargin(0.f, 0.f, 0.f, 34.f), HAlign_Center);

	UHorizontalBox* Pair = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto Corner = [this](const FString& Label, const FString& Name, const FLinearColor& C, bool bRight)
	{
		UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
		const EHorizontalAlignment HA = bRight ? HAlign_Right : HAlign_Left;
		AddV(V, Sized(Box(nullptr, C, 2.f, FMargin(0.f)), 120.f, 6.f), false, FMargin(0.f, 0.f, 0.f, 12.f), HA);
		AddV(V, Txt(Label, 16, C, true), false, FMargin(0.f, 0.f, 0.f, 6.f), HA);
		UTextBlock* N = Txt(Name, 40, BoxUi::Text, true);
		N->SetAutoWrapText(true);
		N->SetJustification(bRight ? ETextJustify::Right : ETextJustify::Left);
		AddV(V, N, false, FMargin(0.f), HA);
		return Sized(V, CornerW);
	};
	AddH(Pair, Corner(TEXT("КРАСНЫЙ УГОЛ"), S ? S->LoadingRedName() : FString(), BoxUi::Red, false), false, FMargin(0.f), VAlign_Top);
	UTextBlock* Vs = Txt(TEXT("VS"), 34, BoxUi::Gold, true);
	Vs->SetJustification(ETextJustify::Center);
	AddH(Pair, Sized(Vs, 200.f), false, FMargin(0.f, 34.f, 0.f, 0.f), VAlign_Top);
	AddH(Pair, Corner(TEXT("СИНИЙ УГОЛ"), S ? S->LoadingBlueName() : FString(), BoxUi::Blue, true), false, FMargin(0.f), VAlign_Top);
	AddV(Col, Pair, false, FMargin(0.f, 0.f, 0.f, 70.f), HAlign_Center);

	Progress = Bar(BoxUi::Accent, 0.f);
	AddV(Col, Sized(Progress, 1000.f, 10.f), false, FMargin(0.f, 0.f, 0.f, 12.f), HAlign_Center);
	Stage = Txt(S ? S->LoadingStage() : FString(), 18, BoxUi::Text);
	Stage->SetJustification(ETextJustify::Center);
	AddV(Col, Stage, false, FMargin(0.f, 0.f, 0.f, 46.f), HAlign_Center);

	UVerticalBox* TipCol = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(TipCol, Txt(TEXT("СОВЕТ"), 15, BoxUi::Accent, true), false, FMargin(0.f, 0.f, 0.f, 6.f));
	UTextBlock* Tip = Txt(S ? S->LoadingTip() : FString(), 20, BoxUi::Text);
	Tip->SetAutoWrapText(true);
	AddV(TipCol, Tip);
	AddV(Col, Sized(Box(TipCol, BoxUi::Panel, 12.f, FMargin(26.f, 18.f)), 1000.f), false, FMargin(0.f), HAlign_Center);

	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Sized(Col, CardW));
	CS->SetAnchors(FAnchors(0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	return Root;
}

void UBoxingLoadingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (!S)
	{
		return;
	}
	const float Target = S->IsFightPreloadDone() ? 1.f : S->GetFightPreloadProgress();
	Shown = FMath::Max(Shown, FMath::FInterpTo(Shown, Target, InDeltaTime, 8.f));
	if (Target >= 1.f && Target - Shown < 0.02f)
	{
		Shown = 1.f;
	}
	if (Progress)
	{
		Progress->SetPercent(Shown);
	}
	if (Stage)
	{
		Stage->SetText(FText::FromString(S->LoadingStage()));
	}
}

// ======================================================================
// Slate: экран MoviePlayer на время LoadMap
// ======================================================================
TSharedRef<SWidget> BoxLoading::MakeSlate(const FTexts& T)
{
	// Кисти живут, пока жив виджет экрана (MoviePlayer держит его до конца загрузки).
	struct FBrushes
	{
		FSlateRoundedBoxBrush Back = FSlateRoundedBoxBrush(BoxUi::Bg, 0.f);
		FSlateRoundedBoxBrush Tip = FSlateRoundedBoxBrush(BoxUi::Panel, 12.f);
		FSlateRoundedBoxBrush Red = FSlateRoundedBoxBrush(BoxUi::Red, 2.f);
		FSlateRoundedBoxBrush Blue = FSlateRoundedBoxBrush(BoxUi::Blue, 2.f);
	};
	static FBrushes B; // один экран за раз; статик — кисти переживают виджет, Slate держит на них указатели
	auto Corner = [](const FString& Label, const FString& Name, const FLinearColor& C, const FSlateBrush* Stripe, bool bRight) -> TSharedRef<SWidget>
	{
		const EHorizontalAlignment HA = bRight ? HAlign_Right : HAlign_Left;
		return SNew(SBox).WidthOverride(CornerW)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HA).Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(SBox).WidthOverride(120.f).HeightOverride(6.f)[SNew(SBorder).BorderImage(Stripe)]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HA).Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(BoxUi::Font(16, true)).ColorAndOpacity(C)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HA)
				[
					SNew(STextBlock).Text(FText::FromString(Name)).Font(BoxUi::Font(40, true)).ColorAndOpacity(BoxUi::Text)
					.AutoWrapText(true).Justification(bRight ? ETextJustify::Right : ETextJustify::Left)
				]
			];
	};
	return SNew(SBorder).BorderImage(&B.Back).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(CardW)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 34.f)
				[
					SNew(STextBlock).Text(FText::FromString(T.Info)).Font(BoxUi::Font(20)).ColorAndOpacity(BoxUi::Muted)
					.Justification(ETextJustify::Center)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 70.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[Corner(TEXT("КРАСНЫЙ УГОЛ"), T.Red, BoxUi::Red, &B.Red, false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 34.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(200.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("VS"))).Font(BoxUi::Font(34, true)).ColorAndOpacity(BoxUi::Gold)
							.Justification(ETextJustify::Center)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[Corner(TEXT("СИНИЙ УГОЛ"), T.Blue, BoxUi::Blue, &B.Blue, true)]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(SThrobber).NumPieces(5)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 46.f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Выходим на ринг…"))).Font(BoxUi::Font(18)).ColorAndOpacity(BoxUi::Text)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(1000.f)
					[
						SNew(SBorder).BorderImage(&B.Tip).Padding(FMargin(26.f, 18.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("СОВЕТ"))).Font(BoxUi::Font(15, true)).ColorAndOpacity(BoxUi::Accent)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(FText::FromString(T.Tip)).Font(BoxUi::Font(20)).ColorAndOpacity(BoxUi::Text).AutoWrapText(true)
							]
						]
					]
				]
			]
		];
}
