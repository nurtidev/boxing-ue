#include "BoxingFightHUD.h"
#include "FightReferee.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "UIFightHud.h"
#include "UIBreakPanel.h"
#include "UIFightResult.h"
#include "BoxingGameInstanceSubsystem.h"
#include "FightFx.h"
#include "Kismet/GameplayStatics.h"
#include "UIPause.h"
#include "UISettings.h"

namespace
{
	const FLinearColor RedCorner(0.85f, 0.12f, 0.1f);
	const FLinearColor BlueCorner(0.12f, 0.35f, 0.9f);
	const FLinearColor BarBack(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor HealthCol(0.25f, 0.85f, 0.3f);
	const FLinearColor StaminaCol(0.95f, 0.8f, 0.2f);
	const FLinearColor GassedCol(1.f, 0.2f, 0.15f);

	UFont* HudFont()
	{
		return GEngine ? GEngine->GetMediumFont() : nullptr;
	}
}

void ABoxingFightHUD::DrawBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, bool bRightAligned)
{
	DrawRect(BarBack, X, Y, W, H);
	const float FW = W * FMath::Clamp(Frac, 0.f, 1.f);
	DrawRect(Fill, bRightAligned ? X + W - FW : X, Y, FW, H);
}

void ABoxingFightHUD::DrawCenteredText(const FString& Text, float CY, const FLinearColor& Color, float Scale)
{
	float TW = 0.f, TH = 0.f;
	GetTextSize(Text, TW, TH, HudFont(), Scale);
	const float X = (Canvas->ClipX - TW) * 0.5f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 12.f, CY - 6.f, TW + 24.f, TH + 12.f);
	DrawText(Text, Color, X, CY, HudFont(), Scale);
}

void ABoxingFightHUD::DrawFighterPanel(int32 Index, float X, float Y, float W, bool bRightAligned)
{
	const ABoxingFightGameMode* GM = GetWorld()->GetAuthGameMode<ABoxingFightGameMode>();
	const ABoxerCharacter* B = GM ? GM->GetBoxer(Index) : nullptr;
	if (!B)
	{
		return;
	}
	const FLinearColor Corner = Index == 0 ? RedCorner : BlueCorner;
	FString Name = B->Preset.Name;
	if (Index == GM->GetPlayerIndex() && !GM->IsAutopilot())
	{
		Name += TEXT(" (ты)");
	}
	if (B->Knockdowns > 0)
	{
		Name += FString::Printf(TEXT("  нокдаунов: %d"), B->Knockdowns);
	}
	float TW = 0.f, TH = 0.f;
	const float Scale = 1.1f;
	GetTextSize(Name, TW, TH, HudFont(), Scale);
	DrawText(Name, Corner, bRightAligned ? X + W - TW : X, Y, HudFont(), Scale);
	DrawBar(X, Y + TH + 4.f, W, 16.f, B->Health / 100.f, HealthCol, bRightAligned);
	DrawBar(X, Y + TH + 24.f, W, 8.f, B->StaminaPct / 100.f, B->bGassed ? GassedCol : StaminaCol, bRightAligned);
	if (B->bGassed)
	{
		const FString G = TEXT("нет сил — отдышись");
		GetTextSize(G, TW, TH, HudFont(), 0.9f);
		DrawText(G, GassedCol, bRightAligned ? X + W - TW : X, Y + 2.f * TH + 30.f, HudFont(), 0.9f);
	}
}

ABoxingFightHUD::ABoxingFightHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ABoxingFightHUD::BeginPlay()
{
	Super::BeginPlay();
	bCanvasHud = FParse::Param(FCommandLine::Get(), TEXT("BoxCanvasHud"));
	APlayerController* PC = GetOwningPlayerController();
	if (!bCanvasHud && PC && PC->IsLocalController())
	{
		HudWidget = CreateWidget<UBoxingFightHudWidget>(PC, UBoxingFightHudWidget::StaticClass());
		if (HudWidget)
		{
			HudWidget->AddToViewport(0);
			HudWidget->SetControlsVisible(BoxSettings::Get().bControlsHints); // S-59: настройка «подсказки управления»
		}
	}
}

void ABoxingFightHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = GetOwningPlayerController();
	if (HudWidget && PC && PC->WasInputKeyJustPressed(EKeys::F1))
	{
		HudWidget->SetControlsVisible(!HudWidget->AreControlsVisible());
	}
	const ABoxingFightGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	const bool bReplay = Fx && Fx->IsReplaying();
	if (bReplay)
	{
		UpdateReplayInput();
	}
	// Пауза: Esc / Start, пока идёт бой (на повторе — пропуск повтора, после итога — свой экран).
	else if (PC && HudWidget && !PauseWidget && !ResultWidget && GM && GM->IsFightStarted() && !GM->GetCore().IsOver()
		&& (PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)))
	{
		OpenPause();
	}
	// S-71: A геймпада в перерыве — «Продолжить» (Enter идёт через PlayerController → RequestBreakProceed).
	if (PC && !PauseWidget && GM && GM->IsFightStarted() && GM->GetSnapshot().Phase == EFightPhase::Between
		&& PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
	{
		RequestBreakProceed(TEXT("A"));
	}
	if (!HudWidget || ResultWidget || !GM || !GM->IsFightStarted() || !GM->GetCore().IsOver())
	{
		return;
	}
	OverTime += DeltaSeconds;
	if (bReplay)
	{
		OverTime = FMath::Min(OverTime, ResultDelay - 0.8f); // итог — после повтора нокаута, не поверх него
	}
	// S-58: панель итога — когда рефери показал итог на ринге (рука победителю / развёл руками над лежащим), но не позже
	// ResultDelay + 5 с; без рефери — как было.
	const ABoxingReferee* Ref = ABoxingReferee::Find(GetWorld());
	if (OverTime >= ResultDelay && (!Ref || Ref->IsResultShown() || OverTime >= ResultDelay + 5.f))
	{
		ShowResult();
	}
}

bool ABoxingFightHUD::RequestBreakProceed(const TCHAR* Source)
{
	ABoxingFightGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	if (!GM || PauseWidget)
	{
		return false;
	}
	const UBoxingBreakPanelWidget* Panel = HudWidget ? HudWidget->GetBreakPanel() : nullptr;
	if (Panel && GM->GetSnapshot().Phase == EFightPhase::Between && !Panel->CanProceed())
	{
		UE_LOG(LogTemp, Log, TEXT("UI: «Продолжить» (%s) — игрок ещё садится в угол, ждём"), Source);
		return false;
	}
	GM->QueueAction(EFightAction::Proceed);
	if (GM->GetSnapshot().Phase == EFightPhase::Between)
	{
		UE_LOG(LogTemp, Log, TEXT("UI: «Продолжить» (%s) через %.1f с перерыва"), Source, Panel ? Panel->BreakTime() : -1.f);
	}
	return true;
}

void ABoxingFightHUD::UpdateReplayInput()
{
	// «Любая кнопка — пропустить»: клавиатура, кнопки геймпада, клик (оси стиков/мыши не в счёт).
	APlayerController* PC = GetOwningPlayerController();
	UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	if (!PC || !Fx)
	{
		return;
	}
	if (AllKeys.Num() == 0)
	{
		EKeys::GetAllKeys(AllKeys);
		AllKeys.RemoveAll([](const FKey& K) { return K.IsAnalog() || K.IsTouch() || K == EKeys::F1; });
	}
	for (const FKey& K : AllKeys)
	{
		if (PC->WasInputKeyJustPressed(K))
		{
			Fx->SkipReplay();
			UE_LOG(LogTemp, Log, TEXT("UI: повтор пропущен (%s)"), *K.ToString());
			return;
		}
	}
}

void ABoxingFightHUD::OpenPause()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || PauseWidget)
	{
		return;
	}
	PauseWidget = CreateWidget<UBoxingPauseWidget>(PC, UBoxingPauseWidget::StaticClass());
	if (!PauseWidget)
	{
		return;
	}
	// Отпустить удерживаемое (шаг/блок) — на паузе ввод в ядро не идёт.
	PC->FlushPressedKeys();
	UGameplayStatics::SetGamePaused(this, true);
	PauseWidget->AddToViewport(20);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(PauseWidget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	PauseWidget->FocusFirst();
	const ABoxingFightGameMode* GM = GetWorld()->GetAuthGameMode<ABoxingFightGameMode>();
	UE_LOG(LogTemp, Log, TEXT("UI: пауза (время ядра %.3f с)"), GM ? GM->GetCore().GetFightTime() : -1.0);
}

void ABoxingFightHUD::ClosePause()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PauseWidget)
	{
		return;
	}
	PauseWidget->RemoveFromParent();
	PauseWidget = nullptr;
	UGameplayStatics::SetGamePaused(this, false);
	if (PC)
	{
		PC->FlushPressedKeys();
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	if (HudWidget)
	{
		HudWidget->SetControlsVisible(BoxSettings::Get().bControlsHints); // могли переключить в настройках
	}
	if (ABoxingFightGameMode* GM = GetWorld()->GetAuthGameMode<ABoxingFightGameMode>())
	{
		GM->QueueAction(EFightAction::BlockEnd); // кнопку блока могли отпустить на паузе (в автопилоте — игнор)
		UE_LOG(LogTemp, Log, TEXT("UI: продолжить (время ядра %.3f с)"), GM->GetCore().GetFightTime());
	}
}

void ABoxingFightHUD::SurrenderFromPause()
{
	ClosePause();
	if (ABoxingFightGameMode* GM = GetWorld()->GetAuthGameMode<ABoxingFightGameMode>())
	{
		GM->Surrender();
		OverTime = FMath::Max(0.f, ResultDelay - 1.2f); // нокаута нет — итог почти сразу
	}
}

void ABoxingFightHUD::ExitToMenu()
{
	UGameplayStatics::SetGamePaused(this, false);
	if (UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this))
	{
		S->OpenMenu(this);
	}
}

void ABoxingFightHUD::ShowResult()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	ResultWidget = CreateWidget<UBoxingFightResultWidget>(PC, UBoxingFightResultWidget::StaticClass());
	if (!ResultWidget)
	{
		return;
	}
	ResultWidget->AddToViewport(10);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(ResultWidget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	ResultWidget->FocusFirst();
}

void ABoxingFightHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!HudWidget)
	{
		DrawCanvasHud();
	}
}

void ABoxingFightHUD::DrawCanvasHud()
{
	const ABoxingFightGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
	if (!GM || !Canvas || !GM->IsFightStarted())
	{
		return;
	}
	const FFightSnapshot& S = GM->GetSnapshot();
	const float Margin = 30.f;
	const float PanelW = FMath::Min(420.f, Canvas->ClipX * 0.36f);
	DrawFighterPanel(0, Margin, Margin, PanelW, false);
	DrawFighterPanel(1, Canvas->ClipX - Margin - PanelW, Margin, PanelW, true);

	// Раунд и время.
	const int32 Sec = FMath::CeilToInt(S.Phase == EFightPhase::Between ? S.BreakLeft : S.TimeLeft);
	FString Top = FString::Printf(TEXT("Раунд %d/%d   %d:%02d"), S.Round, S.TotalRounds, Sec / 60, Sec % 60);
	if (GM->IsAutopilot())
	{
		Top += TEXT("   [автопилот]");
	}
	DrawCenteredText(Top, Margin, FLinearColor::White, 1.2f);

	const float MidY = Canvas->ClipY * 0.3f;
	switch (S.Phase)
	{
	case EFightPhase::Down:
	{
		FString Msg = FString::Printf(TEXT("НОКДАУН!  %d"), S.DownCount);
		if (S.DownWho == GM->GetPlayerIndex() && !GM->IsAutopilot())
		{
			Msg += FString::Printf(TEXT("   жми удары/пробел — вставай (%d%%)"), FMath::RoundToInt(S.RiseProgress * 100.f));
		}
		DrawCenteredText(Msg, MidY, FLinearColor(1.f, 0.85f, 0.2f), 2.f);
		break;
	}
	case EFightPhase::Between:
		DrawCenteredText(FString::Printf(TEXT("Перерыв — раунд %d через %d с"), S.Round + 1, Sec), MidY, FLinearColor::White, 1.6f);
		break;
	case EFightPhase::Over:
		DrawCenteredText(GM->GetResultText(), MidY, FLinearColor(1.f, 0.9f, 0.4f), 1.6f);
		break;
	default:
		break;
	}

	// Подсказка управления внизу.
	if (!GM->IsAutopilot())
	{
		const FString Help = TEXT("J/K джеб/кросс · U/I хуки · N/M апперкоты · Shift+удар — корпус · WASD ноги · Shift+W/S пивот · Q/E уклоны · Пробел блок");
		float TW = 0.f, TH = 0.f;
		GetTextSize(Help, TW, TH, HudFont(), 0.8f);
		DrawText(Help, FLinearColor(1.f, 1.f, 1.f, 0.6f), (Canvas->ClipX - TW) * 0.5f, Canvas->ClipY - TH - 16.f, HudFont(), 0.8f);
	}
}
