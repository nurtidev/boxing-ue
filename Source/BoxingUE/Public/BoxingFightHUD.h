// ABoxingFightHUD — минимальный HUD боя на Canvas (S-41, трек B): здоровье/стамина обоих,
// раунд и время, счёт нокдауна, перерыв, итог. Без UMG.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BoxingFightHUD.generated.h"

UCLASS()
class BOXINGUE_API ABoxingFightHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawFighterPanel(int32 Index, float X, float Y, float W, bool bRightAligned);
	void DrawBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, bool bRightAligned);
	void DrawCenteredText(const FString& Text, float CY, const FLinearColor& Color, float Scale);
};
