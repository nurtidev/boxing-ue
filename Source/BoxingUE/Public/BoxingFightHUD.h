// ABoxingFightHUD — HUD боя (S-55): ведёт UMG-виджеты UBoxingFightHudWidget (бой) и UBoxingFightResultWidget
// (итог: через ResultDelay после конца боя — дать доиграть нокаут/победу; ввод переключается на UI, курсор).
// F1 — скрыть/показать легенду управления. Canvas-HUD S-41 остался запасным: -BoxCanvasHud или нет виджета.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BoxingFightHUD.generated.h"

class UBoxingFightHudWidget;
class UBoxingFightResultWidget;

UCLASS()
class BOXINGUE_API ABoxingFightHUD : public AHUD
{
	GENERATED_BODY()

public:
	ABoxingFightHUD();

	virtual void DrawHUD() override;
	virtual void Tick(float DeltaSeconds) override;

	// Пауза между концом боя и экраном итога (с, реальное время).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|HUD")
	float ResultDelay = 3.5f;

protected:
	virtual void BeginPlay() override;

private:
	void ShowResult();

	UPROPERTY(Transient)
	TObjectPtr<UBoxingFightHudWidget> HudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UBoxingFightResultWidget> ResultWidget;

	bool bCanvasHud = false;
	float OverTime = 0.f;

	// Запасной Canvas-HUD (S-41).
	void DrawCanvasHud();
	void DrawFighterPanel(int32 Index, float X, float Y, float W, bool bRightAligned);
	void DrawBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, bool bRightAligned);
	void DrawCenteredText(const FString& Text, float CY, const FLinearColor& Color, float Scale);
};
