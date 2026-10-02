#include "UISettings.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"

// ======================================================================
// Данные
// ======================================================================
namespace BoxSettings
{
	namespace
	{
		const TCHAR* SECTION = TEXT("BoxingSettings");
		FData GData;
		bool bLoaded = false;

		void Load()
		{
			bLoaded = true;
			if (!GConfig)
			{
				return;
			}
			auto ReadF = [](const TCHAR* Key, float& V)
			{
				float X = V;
				if (GConfig->GetFloat(SECTION, Key, X, GGameUserSettingsIni))
				{
					V = FMath::Clamp(X, 0.f, 1.f);
				}
			};
			ReadF(TEXT("MasterVolume"), GData.Master);
			ReadF(TEXT("SfxVolume"), GData.Sfx);
			ReadF(TEXT("CrowdVolume"), GData.Crowd);
			GConfig->GetBool(SECTION, TEXT("bVibration"), GData.bVibration, GGameUserSettingsIni);
			GConfig->GetBool(SECTION, TEXT("bControlsHints"), GData.bControlsHints, GGameUserSettingsIni);
		}
	}

	const FData& Get()
	{
		if (!bLoaded)
		{
			Load();
		}
		return GData;
	}

	void Set(const FData& D)
	{
		GData = D;
		bLoaded = true;
		if (!GConfig)
		{
			return;
		}
		GConfig->SetFloat(SECTION, TEXT("MasterVolume"), D.Master, GGameUserSettingsIni);
		GConfig->SetFloat(SECTION, TEXT("SfxVolume"), D.Sfx, GGameUserSettingsIni);
		GConfig->SetFloat(SECTION, TEXT("CrowdVolume"), D.Crowd, GGameUserSettingsIni);
		GConfig->SetBool(SECTION, TEXT("bVibration"), D.bVibration, GGameUserSettingsIni);
		GConfig->SetBool(SECTION, TEXT("bControlsHints"), D.bControlsHints, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	float SfxGain() { return Get().Master * Get().Sfx; }
	float CrowdGain() { return Get().Master * Get().Crowd; }
	bool Vibration() { return Get().bVibration; }
}

// ======================================================================
// Экран
// ======================================================================
namespace
{
	const TCHAR* QualityName(int32 Q)
	{
		switch (Q)
		{
		case 0: return TEXT("Низкое");
		case 1: return TEXT("Среднее");
		case 2: return TEXT("Высокое");
		case 3: return TEXT("Эпик");
		default: return TEXT("Своё");
		}
	}

	const TCHAR* WindowName(int32 M)
	{
		switch (M)
		{
		case 0: return TEXT("Полный экран");
		case 1: return TEXT("Окно без рамки");
		default: return TEXT("Окно");
		}
	}

	FString Pct(float V)
	{
		return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f));
	}

	// S-63: пресет качества — по группам Scalability БЕЗ разрешения рендера. Штатный GetOverallScalabilityLevel требует ещё
	// и sg.ResolutionQuality, равного значению пресета: в общем ini стояло 0 («по умолчанию движка») — и при всех группах
	// «эпик» экран писал «Своё», а ←/→ переписывал разрешение на значение из PerfIndexValues (2 / 87 %).
	int32 QualityPreset(const UGameUserSettings* G)
	{
		if (!G)
		{
			return -1;
		}
		const int32 L = G->GetViewDistanceQuality();
		const int32 All[] = {G->GetAntiAliasingQuality(), G->GetShadowQuality(), G->GetGlobalIlluminationQuality(), G->GetReflectionQuality(),
			G->GetPostProcessingQuality(), G->GetTextureQuality(), G->GetVisualEffectQuality(), G->GetFoliageQuality(), G->GetShadingQuality()};
		for (const int32 V : All)
		{
			if (V != L)
			{
				return -1;
			}
		}
		return L >= 0 && L <= 3 ? L : -1;
	}

	// Разрешение рендера пресета: высокое и эпик — 100 % (честная картинка; было 72.9 % — r.ScreenPercentage.Default
	// при sg.ResolutionQuality=0), низкое / среднее — с запасом по FPS.
	float PresetRenderScale(int32 Q)
	{
		return Q >= 2 ? 100.f : (Q == 1 ? 85.f : 70.f);
	}

	// Шаги ползунка «Разрешение рендера»: 0 — авто (по умолчанию движка), дальше проценты.
	const float RenderScales[] = {0.f, 50.f, 60.f, 67.f, 70.f, 75.f, 80.f, 85.f, 90.f, 100.f};

	float CurrentRenderScale(const UGameUserSettings* G)
	{
		if (!G)
		{
			return 100.f;
		}
		float Norm = 0.f, Value = 0.f, Min = 0.f, Max = 0.f;
		G->GetResolutionScaleInformationEx(Norm, Value, Min, Max);
		return Value;
	}
}

UWidget* UBoxingSettingsWidget::BuildUi()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	// Разрешения: полноэкранные + удобные оконные + текущее; без дублей, по возрастанию.
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	TArray<FIntPoint> Win;
	UKismetSystemLibrary::GetConvenientWindowedResolutions(Win);
	for (const FIntPoint& P : Win)
	{
		Resolutions.AddUnique(P);
	}
	const FIntPoint Cur = GUS ? GUS->GetScreenResolution() : FIntPoint(1920, 1080);
	for (const FIntPoint P : {FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), Cur})
	{
		if (Resolutions.Num() < 2 || P == Cur)
		{
			Resolutions.AddUnique(P); // офскрин/без списка от системы — разумный набор
		}
	}
	Resolutions.RemoveAll([](const FIntPoint& P) { return P.X < 1024 || P.Y < 600; });
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	ResIndex = FMath::Max(0, Resolutions.IndexOfByKey(Cur));
	WindowModeSel = GUS ? FMath::Clamp(static_cast<int32>(GUS->GetFullscreenMode()), 0, 2) : 1;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	UBorder* Dim = Box(nullptr, FLinearColor(0.f, 0.f, 0.f, 0.6f), 0.f, FMargin(0.f));
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Col, Txt(TEXT("Настройки"), 36, BoxUi::Text, true), false, FMargin(0.f, 0.f, 0.f, 18.f));

	auto Section = [this, Col](const TCHAR* Title)
	{
		AddV(Col, Txt(Title, 15, BoxUi::Accent, true), false, FMargin(4.f, 14.f, 0.f, 6.f));
	};
	Section(TEXT("ЗВУК"));
	MakeRow(Col, ERow::Master, TEXT("Общая громкость"));
	MakeRow(Col, ERow::Sfx, TEXT("Эффекты (удары, гонг)"));
	MakeRow(Col, ERow::Crowd, TEXT("Зал"));
	Section(TEXT("УПРАВЛЕНИЕ"));
	MakeRow(Col, ERow::Vibration, TEXT("Вибрация геймпада"));
	MakeRow(Col, ERow::Hints, TEXT("Подсказки управления в бою"));
	Section(TEXT("ГРАФИКА"));
	MakeRow(Col, ERow::Quality, TEXT("Качество"));
	MakeRow(Col, ERow::RenderScale, TEXT("Разрешение рендера"));
	MakeRow(Col, ERow::Resolution, TEXT("Разрешение"));
	MakeRow(Col, ERow::WindowMode, TEXT("Режим окна"));
	ScreenNote = Txt(TEXT(""), 14, BoxUi::Warn);
	AddV(Col, ScreenNote, false, FMargin(4.f, 4.f, 0.f, 0.f));
	Section(TEXT("ЯЗЫК"));
	UHorizontalBox* Lang = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Lang, Txt(TEXT("Язык"), 19, BoxUi::Text), true);
	AddH(Lang, Txt(TEXT("Русский"), 19, BoxUi::Muted, true));
	AddV(Col, Box(Lang, BoxUi::WithAlpha(BoxUi::Panel, 0.5f), 8.f, FMargin(18.f, 10.f)), false, FMargin(0.f, 0.f, 0.f, 22.f));

	UHorizontalBox* Btns = WidgetTree->ConstructWidget<UHorizontalBox>();
	ApplyButton = Btn(TEXT("Применить экран"), [this]() { ApplyScreen(); }, EBoxBtn::Secondary, 19);
	AddH(Btns, ApplyButton, false, FMargin(0.f, 0.f, 14.f, 0.f));
	AddH(Btns, Spacer(1.f, 1.f), true);
	AddH(Btns, Sized(Btn(TEXT("Готово"), [this]() { Close(); }, EBoxBtn::Primary, 21), 220.f));
	AddV(Col, Btns);
	UTextBlock* Hint = Txt(TEXT("↑↓ — выбрать · ←→ — изменить · Enter / A — следующее значение · Esc / B — назад"), 14, BoxUi::Muted);
	AddV(Col, Hint, false, FMargin(4.f, 14.f, 0.f, 0.f));

	UBorder* Card = Box(Col, BoxUi::WithAlpha(BoxUi::Bg, 0.97f), 18.f, FMargin(44.f, 30.f));
	Card->SetBrush(FSlateRoundedBoxBrush(BoxUi::WithAlpha(BoxUi::Bg, 0.97f), 18.f, BoxUi::Line, 1.f));
	UCanvasPanelSlot* CS = Root->AddChildToCanvas(Sized(Card, 980.f));
	CS->SetAnchors(FAnchors(0.5f, 0.5f));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	Refresh();
	return Root;
}

UWidget* UBoxingSettingsWidget::MakeRow(UVerticalBox* Col, ERow Kind, const FString& Label)
{
	FRow R;
	R.Kind = Kind;
	UHorizontalBox* In = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(In, Txt(Label, 19, BoxUi::Text), true, FMargin(18.f, 10.f, 12.f, 10.f));
	UHorizontalBox* Val = WidgetTree->ConstructWidget<UHorizontalBox>();
	const bool bVolume = Kind == ERow::Master || Kind == ERow::Sfx || Kind == ERow::Crowd;
	if (bVolume)
	{
		R.Bar = Bar(BoxUi::Accent, 1.f);
		AddH(Val, Sized(R.Bar, 230.f, 12.f), false, FMargin(0.f, 0.f, 14.f, 0.f));
	}
	R.Value = Txt(TEXT(""), 19, BoxUi::Text, true);
	R.Value->SetJustification(bVolume ? ETextJustify::Right : ETextJustify::Center);
	AddH(Val, Sized(R.Value, bVolume ? 66.f : 300.f));
	AddH(In, Sized(Val, 330.f), false, FMargin(0.f, 0.f, 12.f, 0.f));
	R.Btn = BtnWith(In, [this, Kind]() { Change(Kind, 0); }, BoxUi::Panel, BoxUi::PanelHi, 8.f,
		FString::Printf(TEXT("set:%s"), *Label));
	if (!FirstButton)
	{
		FirstButton = R.Btn;
	}

	// Стрелки для мыши (геймпад/клавиатура меняют ←→ на самой строке, на стрелки фокус не уходит).
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddH(Line, R.Btn, true, FMargin(0.f), VAlign_Fill);
	for (const int32 Dir : {-1, 1})
	{
		UBoxingMouseOnlyButton* A = WidgetTree->ConstructWidget<UBoxingMouseOnlyButton>();
		StyleBtn(A, EBoxBtn::Ghost, false);
		UTextBlock* T = Txt(Dir < 0 ? TEXT("‹") : TEXT("›"), 24, BoxUi::Muted, true);
		T->SetJustification(ETextJustify::Center);
		A->SetContent(T);
		UBoxingUiClick* C = NewObject<UBoxingUiClick>(this);
		C->Fn = [this, Kind, Dir]() { Change(Kind, Dir); };
		A->OnClicked.AddDynamic(C, &UBoxingUiClick::Fire);
		Clicks.Add(C);
		AddH(Line, Sized(A, 48.f), false, FMargin(6.f, 0.f, 0.f, 0.f), VAlign_Fill);
	}
	AddV(Col, Line, false, FMargin(0.f, 0.f, 0.f, 6.f));
	Rows.Add(R);
	return Line;
}

void UBoxingSettingsWidget::Change(ERow Kind, int32 Dir)
{
	// Dir: −1/+1 — стрелки (без перехода по кругу), 0 — Enter/A/клик (следующее значение по кругу).
	BoxSettings::FData D = BoxSettings::Get();
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	auto Vol = [Dir](float& V)
	{
		const int32 Step = FMath::RoundToInt(V * 10.f);
		const int32 Next = Dir == 0 ? (Step >= 10 ? 0 : Step + 1) : FMath::Clamp(Step + Dir, 0, 10);
		V = Next / 10.f;
	};
	switch (Kind)
	{
	case ERow::Master: Vol(D.Master); break;
	case ERow::Sfx: Vol(D.Sfx); break;
	case ERow::Crowd: Vol(D.Crowd); break;
	case ERow::Vibration: D.bVibration = !D.bVibration; break;
	case ERow::Hints: D.bControlsHints = !D.bControlsHints; break;
	case ERow::Quality:
		if (GUS)
		{
			const int32 Q = QualityPreset(GUS); // −1 — «своё»
			const int32 Next = Dir == 0 ? (Q < 0 || Q >= 3 ? 0 : Q + 1) : FMath::Clamp((Q < 0 ? 2 : Q) + Dir, 0, 3);
			GUS->SetOverallScalabilityLevel(Next);
			GUS->SetResolutionScaleValueEx(PresetRenderScale(Next));
			GUS->ApplyNonResolutionSettings();
			GUS->SaveSettings();
			UE_LOG(LogTemp, Log, TEXT("UI: качество графики → %s, разрешение рендера %.0f%%"), QualityName(Next), CurrentRenderScale(GUS));
		}
		break;
	case ERow::RenderScale:
		if (GUS)
		{
			const float Cur = CurrentRenderScale(GUS);
			int32 I = 0;
			for (int32 K = 0; K < UE_ARRAY_COUNT(RenderScales); ++K)
			{
				if (FMath::Abs(RenderScales[K] - Cur) < FMath::Abs(RenderScales[I] - Cur))
				{
					I = K;
				}
			}
			const int32 N = UE_ARRAY_COUNT(RenderScales);
			I = Dir == 0 ? (I + 1) % N : FMath::Clamp(I + Dir, 0, N - 1);
			GUS->SetResolutionScaleValueEx(RenderScales[I]);
			GUS->ApplyNonResolutionSettings();
			GUS->SaveSettings();
			UE_LOG(LogTemp, Log, TEXT("UI: разрешение рендера → %.0f%%"), RenderScales[I]);
		}
		break;
	case ERow::Resolution:
		if (Resolutions.Num() > 0)
		{
			ResIndex = Dir == 0 ? (ResIndex + 1) % Resolutions.Num() : FMath::Clamp(ResIndex + Dir, 0, Resolutions.Num() - 1);
		}
		break;
	case ERow::WindowMode:
		WindowModeSel = Dir == 0 ? (WindowModeSel + 1) % 3 : FMath::Clamp(WindowModeSel + Dir, 0, 2);
		break;
	}
	BoxSettings::Set(D);
	Refresh();
}

void UBoxingSettingsWidget::Refresh()
{
	const BoxSettings::FData& D = BoxSettings::Get();
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	for (FRow& R : Rows)
	{
		FString V;
		FLinearColor C = BoxUi::Text;
		switch (R.Kind)
		{
		case ERow::Master: V = Pct(D.Master); break;
		case ERow::Sfx: V = Pct(D.Sfx); break;
		case ERow::Crowd: V = Pct(D.Crowd); break;
		case ERow::Vibration: V = D.bVibration ? TEXT("Вкл") : TEXT("Выкл"); C = D.bVibration ? BoxUi::Good : BoxUi::Muted; break;
		case ERow::Hints: V = D.bControlsHints ? TEXT("Вкл") : TEXT("Выкл"); C = D.bControlsHints ? BoxUi::Good : BoxUi::Muted; break;
		case ERow::Quality: V = FString::Printf(TEXT("‹  %s  ›"), QualityName(QualityPreset(GUS))); break;
		case ERow::RenderScale:
		{
			const float RS = CurrentRenderScale(GUS);
			V = RS <= 0.f ? TEXT("‹  Авто (движок)  ›") : FString::Printf(TEXT("‹  %d %%  ›"), FMath::RoundToInt(RS));
			break;
		}
		case ERow::Resolution:
			V = Resolutions.IsValidIndex(ResIndex) ? FString::Printf(TEXT("‹  %d × %d  ›"), Resolutions[ResIndex].X, Resolutions[ResIndex].Y) : TEXT("—");
			break;
		case ERow::WindowMode: V = FString::Printf(TEXT("‹  %s  ›"), WindowName(WindowModeSel)); break;
		}
		R.Value->SetText(FText::FromString(V));
		R.Value->SetColorAndOpacity(FSlateColor(C));
		if (R.Bar)
		{
			const float P = R.Kind == ERow::Master ? D.Master : (R.Kind == ERow::Sfx ? D.Sfx : D.Crowd);
			R.Bar->SetPercent(P);
			R.Bar->SetFillColorAndOpacity(P > 0.f ? BoxUi::Accent : BoxUi::Muted);
		}
	}
	const FIntPoint CurRes = GUS ? GUS->GetScreenResolution() : FIntPoint::ZeroValue;
	const int32 CurMode = GUS ? static_cast<int32>(GUS->GetFullscreenMode()) : WindowModeSel;
	const bool bPending = (Resolutions.IsValidIndex(ResIndex) && Resolutions[ResIndex] != CurRes) || CurMode != WindowModeSel;
	ScreenNote->SetText(FText::FromString(bPending ? TEXT("Экран изменится после «Применить экран»") : TEXT("")));
	ScreenNote->SetVisibility(bPending ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ApplyButton->SetIsEnabled(bPending);
}

void UBoxingSettingsWidget::ApplyScreen()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS || !Resolutions.IsValidIndex(ResIndex))
	{
		return;
	}
	GUS->SetScreenResolution(Resolutions[ResIndex]);
	GUS->SetFullscreenMode(static_cast<EWindowMode::Type>(WindowModeSel));
	GUS->ApplyResolutionSettings(false);
	GUS->SaveSettings();
	UE_LOG(LogTemp, Log, TEXT("UI: экран → %dx%d, %s"), Resolutions[ResIndex].X, Resolutions[ResIndex].Y, WindowName(WindowModeSel));
	Refresh();
	FocusKey(TEXT("Готово")); // «Применить» стала неактивной — фокус на «Готово»
}

UBoxingSettingsWidget::FRow* UBoxingSettingsWidget::FocusedRow()
{
	for (FRow& R : Rows)
	{
		if (R.Btn && R.Btn->HasKeyboardFocus())
		{
			return &R;
		}
	}
	return nullptr;
}

FReply UBoxingSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// ←→ / крестовина вбок на строке — изменить значение. Перехват на спуске (preview): иначе кнопка в фокусе сама
	// превращает стрелку в навигацию (SWidget::OnKeyDown) и до экрана она не доходит.
	if (FSlateApplication::IsInitialized())
	{
		const EUINavigation Nav = FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent);
		if (Nav == EUINavigation::Left || Nav == EUINavigation::Right)
		{
			if (FRow* R = FocusedRow())
			{
				Change(R->Kind, Nav == EUINavigation::Left ? -1 : 1);
				return FReply::Handled();
			}
		}
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UBoxingSettingsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}

bool UBoxingSettingsWidget::HandleBack()
{
	Close();
	return true;
}

void UBoxingSettingsWidget::FocusFirst()
{
	FocusKey(TEXT("set:Общая громкость"));
}

void UBoxingSettingsWidget::Close()
{
	RemoveFromParent();
	if (OnClose)
	{
		OnClose();
	}
}
