#include "UIMenuGameMode.h"

#include "BoxingGameInstanceSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "UIExhibition.h"
#include "UIMainMenu.h"

ABoxingMenuGameMode::ABoxingMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ABoxingMenuPlayerController::StaticClass();
	HUDClass = nullptr;
}

void ABoxingMenuGameMode::StartPlay()
{
	int32 Bg = 1;
	FParse::Value(FCommandLine::Get(), TEXT("BoxMenuBg="), Bg);
	if (Bg != 0 && !BackgroundMap.IsEmpty() && FPackageName::DoesPackageExist(BackgroundMap))
	{
		bool bOk = false;
		ULevelStreamingDynamic::LoadLevelInstance(GetWorld(), BackgroundMap, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
		UE_LOG(LogTemp, Log, TEXT("UI: фон меню %s — %s"), *BackgroundMap, bOk ? TEXT("грузится") : TEXT("не загрузился"));
	}
	Super::StartPlay();
}

ABoxingMenuPlayerController::ABoxingMenuPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bShowMouseCursor = true;
	bEnableClickEvents = true;
}

void ABoxingMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MenuCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	if (MenuCamera)
	{
		MenuCamera->GetCameraComponent()->SetFieldOfView(62.f);
		MenuCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		SetViewTarget(MenuCamera);
	}
	Tick(0.f);

	UBoxingGameInstanceSubsystem* S = UBoxingGameInstanceSubsystem::Get(this);
	if (S && S->bOpenExhibitionOnMenu)
	{
		S->bOpenExhibitionOnMenu = false;
		ShowExhibition();
	}
	else
	{
		ShowMain();
	}
}

void ABoxingMenuPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!MenuCamera)
	{
		return;
	}
	OrbitYaw += OrbitSpeed * DeltaSeconds;
	const float Rad = FMath::DegreesToRadians(OrbitYaw);
	const FVector Loc = OrbitCenter + FVector(FMath::Cos(Rad) * OrbitRadius, FMath::Sin(Rad) * OrbitRadius, OrbitHeight - OrbitCenter.Z);
	MenuCamera->SetActorLocationAndRotation(Loc, (OrbitCenter - Loc).Rotation());
}

void ABoxingMenuPlayerController::FocusWidget(UUserWidget* W)
{
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	if (W)
	{
		Mode.SetWidgetToFocus(W->TakeWidget());
	}
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}

void ABoxingMenuPlayerController::ShowMain()
{
	if (ExhibitionWidget)
	{
		ExhibitionWidget->RemoveFromParent();
	}
	if (!MainWidget)
	{
		MainWidget = CreateWidget<UBoxingMainMenuWidget>(this, UBoxingMainMenuWidget::StaticClass());
	}
	if (MainWidget && !MainWidget->IsInViewport())
	{
		MainWidget->AddToViewport(0);
	}
	FocusWidget(MainWidget);
	if (MainWidget)
	{
		MainWidget->FocusFirst();
	}
}

void ABoxingMenuPlayerController::ShowExhibition()
{
	if (MainWidget)
	{
		MainWidget->RemoveFromParent();
	}
	if (!ExhibitionWidget)
	{
		ExhibitionWidget = CreateWidget<UBoxingExhibitionWidget>(this, UBoxingExhibitionWidget::StaticClass());
	}
	if (ExhibitionWidget && !ExhibitionWidget->IsInViewport())
	{
		ExhibitionWidget->AddToViewport(0);
	}
	FocusWidget(ExhibitionWidget);
	if (ExhibitionWidget)
	{
		ExhibitionWidget->FocusFirst();
	}
}
