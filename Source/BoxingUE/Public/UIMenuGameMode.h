// Оболочка игры (S-55): GameMode и контроллер карты меню L_Menu.
//
// ABoxingMenuGameMode — без пешки; фоном подгружает арену L_Ring как экземпляр уровня (одна правда об арене,
// без копии карты) — её GameMode при этом не участвует. Нет арены — тёмный фон виджетов.
// ABoxingMenuPlayerController — медленный облёт ринга камерой, режим ввода «только UI», экраны:
// главный (UBoxingMainMenuWidget) и Выставка (UBoxingExhibitionWidget).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "UIMenuGameMode.generated.h"

class ACameraActor;
class UBoxingMainMenuWidget;
class UBoxingExhibitionWidget;

UCLASS()
class BOXINGUE_API ABoxingMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABoxingMenuGameMode();

	// Арена фоном (пусто — без фона). Командная строка: -BoxMenuBg=0 — не грузить.
	UPROPERTY(EditAnywhere, Category = "Boxing|Menu")
	FString BackgroundMap = TEXT("/Game/Boxing/Maps/L_Ring");

protected:
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
};

UCLASS()
class BOXINGUE_API ABoxingMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABoxingMenuPlayerController();

	void ShowMain();
	void ShowExhibition();

	// Облёт: центр (см), радиус, высота, скорость (град/с).
	UPROPERTY(EditAnywhere, Category = "Boxing|Menu")
	FVector OrbitCenter = FVector(0.f, 0.f, 120.f);

	UPROPERTY(EditAnywhere, Category = "Boxing|Menu")
	float OrbitRadius = 820.f;

	UPROPERTY(EditAnywhere, Category = "Boxing|Menu")
	float OrbitHeight = 330.f;

	UPROPERTY(EditAnywhere, Category = "Boxing|Menu")
	float OrbitSpeed = 4.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void FocusWidget(class UUserWidget* W);

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> MenuCamera;

	UPROPERTY(Transient)
	TObjectPtr<UBoxingMainMenuWidget> MainWidget;

	UPROPERTY(Transient)
	TObjectPtr<UBoxingExhibitionWidget> ExhibitionWidget;

	float OrbitYaw = 35.f;
};
