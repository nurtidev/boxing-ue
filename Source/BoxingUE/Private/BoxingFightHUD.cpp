#include "BoxingFightHUD.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"

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

void ABoxingFightHUD::DrawHUD()
{
	Super::DrawHUD();
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
