#include "BoxingFightPlayerController.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "FightFx.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"

namespace
{
	struct FActionSpec
	{
		const TCHAR* Name;
		bool bAxis;
		const TCHAR* Keys[3];
	};

	// ЕДИНАЯ таблица раскладки — продублирована в Tools/EditorScripts/fight_input.py (держать в синхроне).
	const FActionSpec GActionSpecs[] = {
		{TEXT("Jab"), false, {TEXT("J"), TEXT("Gamepad_FaceButton_Left"), nullptr}},
		{TEXT("Cross"), false, {TEXT("K"), TEXT("Gamepad_FaceButton_Top"), nullptr}},
		{TEXT("HookL"), false, {TEXT("U"), TEXT("Gamepad_LeftShoulder"), nullptr}},
		{TEXT("HookR"), false, {TEXT("I"), TEXT("Gamepad_RightShoulder"), nullptr}},
		{TEXT("UpperL"), false, {TEXT("N"), TEXT("Gamepad_FaceButton_Bottom"), nullptr}},
		{TEXT("UpperR"), false, {TEXT("M"), TEXT("Gamepad_FaceButton_Right"), nullptr}},
		{TEXT("Block"), false, {TEXT("SpaceBar"), TEXT("Gamepad_LeftTrigger"), nullptr}},
		{TEXT("SlipL"), false, {TEXT("Q"), nullptr, nullptr}},
		{TEXT("SlipR"), false, {TEXT("E"), nullptr, nullptr}},
		{TEXT("SlipAxis"), true, {TEXT("Gamepad_RightX"), nullptr, nullptr}},
		{TEXT("StepFwd"), false, {TEXT("D"), TEXT("Right"), nullptr}},
		{TEXT("StepBack"), false, {TEXT("A"), TEXT("Left"), nullptr}},
		{TEXT("StepUp"), false, {TEXT("W"), TEXT("Up"), nullptr}},
		{TEXT("StepDown"), false, {TEXT("S"), TEXT("Down"), nullptr}},
		{TEXT("MoveX"), true, {TEXT("Gamepad_LeftX"), nullptr, nullptr}},
		{TEXT("MoveY"), true, {TEXT("Gamepad_LeftY"), nullptr, nullptr}},
		{TEXT("Mod"), false, {TEXT("LeftShift"), TEXT("RightShift"), nullptr}},
		{TEXT("BodyPad"), false, {TEXT("Gamepad_RightTrigger"), nullptr, nullptr}},
		{TEXT("PivotPad"), false, {TEXT("Gamepad_LeftThumbstick"), nullptr, nullptr}},
		{TEXT("Proceed"), false, {TEXT("Enter"), TEXT("Gamepad_Special_Right"), nullptr}},
	};

	constexpr float STICK_ON = 0.5f;  // порог стика/суммы клавиш для шага
	constexpr float SLIP_ON = 0.6f;   // порог правого стика для уклона (по фронту)
}

ABoxingFightPlayerController::ABoxingFightPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	// Камера — после движения бойцов (CharacterMovement тикает до физики).
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bAutoManageActiveCameraTarget = false;
	bShowMouseCursor = false;
}

UInputAction* ABoxingFightPlayerController::Act(const TCHAR* Name) const
{
	const TObjectPtr<UInputAction>* A = Actions.Find(FName(Name));
	return A ? A->Get() : nullptr;
}

void ABoxingFightPlayerController::EnsureActions()
{
	if (Actions.Num() > 0)
	{
		return;
	}
	int32 FromAssets = 0;
	for (const FActionSpec& S : GActionSpecs)
	{
		const FString Asset = FString::Printf(TEXT("IA_%s"), S.Name);
		const FString Package = InputFolder / Asset;
		UInputAction* A = nullptr;
		if (FPackageName::DoesPackageExist(Package))
		{
			A = LoadObject<UInputAction>(nullptr, *(Package + TEXT(".") + Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (A)
		{
			++FromAssets;
		}
		else
		{
			A = NewObject<UInputAction>(this, FName(*Asset));
			A->ValueType = S.bAxis ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
		}
		Actions.Add(FName(S.Name), A);
	}

	const FString ImcPackage = InputFolder / TEXT("IMC_Fight");
	if (FromAssets == UE_ARRAY_COUNT(GActionSpecs) && FPackageName::DoesPackageExist(ImcPackage))
	{
		FightContext = LoadObject<UInputMappingContext>(nullptr, *(ImcPackage + TEXT(".IMC_Fight")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!FightContext)
	{
		// Нет ассетов (или не все) — та же раскладка в рантайме.
		FightContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Fight_Runtime"));
		for (const FActionSpec& S : GActionSpecs)
		{
			for (const TCHAR* K : S.Keys)
			{
				if (K)
				{
					FightContext->MapKey(Act(S.Name), FKey(FName(K)));
				}
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("FIGHT INPUT: действий из ассетов %d/%d, контекст %s"), FromAssets,
		static_cast<int32>(UE_ARRAY_COUNT(GActionSpecs)), *GetNameSafe(FightContext));
}

void ABoxingFightPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	EnsureActions();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Warning, TEXT("FIGHT INPUT: InputComponent не Enhanced — проверь DefaultInputComponentClass"));
		return;
	}
	const TPair<const TCHAR*, EFightAction> Punches[] = {
		{TEXT("Jab"), EFightAction::Jab},       {TEXT("Cross"), EFightAction::Cross},
		{TEXT("HookL"), EFightAction::HookL},   {TEXT("HookR"), EFightAction::HookR},
		{TEXT("UpperL"), EFightAction::UpperL}, {TEXT("UpperR"), EFightAction::UpperR},
	};
	for (const TPair<const TCHAR*, EFightAction>& P : Punches)
	{
		EIC->BindAction(Act(P.Key), ETriggerEvent::Started, this, &ABoxingFightPlayerController::OnPunch, P.Value);
	}
	EIC->BindAction(Act(TEXT("Block")), ETriggerEvent::Started, this, &ABoxingFightPlayerController::OnBlock, true);
	EIC->BindAction(Act(TEXT("Block")), ETriggerEvent::Completed, this, &ABoxingFightPlayerController::OnBlock, false);
	EIC->BindAction(Act(TEXT("SlipL")), ETriggerEvent::Started, this, &ABoxingFightPlayerController::OnSlip, EFightAction::SlipLeft);
	EIC->BindAction(Act(TEXT("SlipR")), ETriggerEvent::Started, this, &ABoxingFightPlayerController::OnSlip, EFightAction::SlipRight);
	EIC->BindAction(Act(TEXT("Proceed")), ETriggerEvent::Started, this, &ABoxingFightPlayerController::OnProceed);
	// Удерживаемые (ноги, модификаторы, стики) читаются опросом в Tick.
	for (const TCHAR* Held : {TEXT("StepFwd"), TEXT("StepBack"), TEXT("StepUp"), TEXT("StepDown"), TEXT("MoveX"), TEXT("MoveY"),
			 TEXT("Mod"), TEXT("BodyPad"), TEXT("PivotPad"), TEXT("SlipAxis")})
	{
		EIC->BindActionValue(Act(Held));
	}
}

void ABoxingFightPlayerController::BeginPlay()
{
	Super::BeginPlay();
	EnsureActions();
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			Sub->AddMappingContext(FightContext, 10);
		}
	}
	SetInputMode(FInputModeGameOnly());
	bSideCam = FParse::Param(FCommandLine::Get(), TEXT("BoxCamSide"));

	FActorSpawnParameters P;
	P.Owner = this;
	FightCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	if (FightCamera)
	{
		FightCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		SetViewTarget(FightCamera);
	}
}

ABoxingFightGameMode* ABoxingFightPlayerController::GetFightMode() const
{
	return GetWorld() ? GetWorld()->GetAuthGameMode<ABoxingFightGameMode>() : nullptr;
}

bool ABoxingFightPlayerController::IsPlayerDown() const
{
	const ABoxingFightGameMode* GM = GetFightMode();
	return GM && GM->GetSnapshot().Phase == EFightPhase::Down && GM->GetSnapshot().DownWho == GM->GetPlayerIndex();
}

float ABoxingFightPlayerController::ActValue(const TCHAR* Name) const
{
	const UEnhancedPlayerInput* EPI = Cast<UEnhancedPlayerInput>(PlayerInput);
	const UInputAction* A = Act(Name);
	return (EPI && A) ? EPI->GetActionValue(A).Get<float>() : 0.f;
}

void ABoxingFightPlayerController::OnPunch(const FInputActionInstance& Instance, EFightAction Action)
{
	ABoxingFightGameMode* GM = GetFightMode();
	if (!GM)
	{
		return;
	}
	UBoxingFightFx* Fx = UBoxingFightFx::Get(this);
	if (Fx && Fx->IsReplaying())
	{
		Fx->SkipReplay(); // S-54: любой удар — пропустить повтор нокаута
		return;
	}
	if (IsPlayerDown())
	{
		GM->QueueAction(EFightAction::RiseTap);
		return;
	}
	const bool bBody = ActValue(TEXT("Mod")) > 0.5f || ActValue(TEXT("BodyPad")) > 0.5f;
	GM->QueueAction(Action, bBody ? EPunchTarget::Body : EPunchTarget::Head);
}

void ABoxingFightPlayerController::OnBlock(const FInputActionInstance& Instance, bool bOn)
{
	ABoxingFightGameMode* GM = GetFightMode();
	if (!GM)
	{
		return;
	}
	if (bOn && IsPlayerDown())
	{
		GM->QueueAction(EFightAction::RiseTap);
		return;
	}
	GM->QueueAction(bOn ? EFightAction::BlockStart : EFightAction::BlockEnd);
}

void ABoxingFightPlayerController::OnSlip(const FInputActionInstance& Instance, EFightAction Action)
{
	if (ABoxingFightGameMode* GM = GetFightMode())
	{
		GM->QueueAction(Action);
	}
}

void ABoxingFightPlayerController::OnProceed(const FInputActionInstance& Instance)
{
	// S-59: Start геймпада — меню паузы (ABoxingFightHUD), не «следующий раунд» (в IMC_Fight он ещё на Proceed).
	if (WasInputKeyJustPressed(EKeys::Gamepad_Special_Right))
	{
		return;
	}
	if (ABoxingFightGameMode* GM = GetFightMode())
	{
		GM->QueueAction(EFightAction::Proceed);
	}
}

void ABoxingFightPlayerController::UpdateHeldInput()
{
	ABoxingFightGameMode* GM = GetFightMode();
	if (!GM)
	{
		return;
	}
	// Экранные оси: X — к сопернику (вправо по экрану), Y — вверх по экрану (= влево от лица бойца).
	const float X = ActValue(TEXT("StepFwd")) - ActValue(TEXT("StepBack")) + ActValue(TEXT("MoveX"));
	const float Y = ActValue(TEXT("StepUp")) - ActValue(TEXT("StepDown")) + ActValue(TEXT("MoveY"));
	const bool bPivot = ActValue(TEXT("Mod")) > 0.5f || ActValue(TEXT("PivotPad")) > 0.5f;

	bool bHeld = false;
	EFightAction Step = EFightAction::StepFwd;
	if (FMath::Abs(X) >= FMath::Abs(Y) && FMath::Abs(X) > STICK_ON)
	{
		bHeld = true;
		Step = X > 0.f ? EFightAction::StepFwd : EFightAction::StepBack;
	}
	else if (FMath::Abs(Y) > STICK_ON)
	{
		bHeld = true;
		Step = Y > 0.f ? (bPivot ? EFightAction::PivotL : EFightAction::StepLeft)
					   : (bPivot ? EFightAction::PivotR : EFightAction::StepRight);
	}
	GM->SetHeldStep(Step, bHeld && !IsPlayerDown());

	// Правый стик вбок — уклон по фронту.
	const float SX = ActValue(TEXT("SlipAxis"));
	const int32 SlipNow = SX > SLIP_ON ? 1 : (SX < -SLIP_ON ? -1 : 0);
	if (SlipNow != 0 && SlipNow != PrevSlipAxis)
	{
		GM->QueueAction(SlipNow > 0 ? EFightAction::SlipRight : EFightAction::SlipLeft);
	}
	PrevSlipAxis = SlipNow;
}

float ABoxingFightPlayerController::FollowYaw(float InCamYaw, float WantYaw, float Dt, float DeadZone, float InFollowRate, float InRecenterRate)
{
	const float D = FMath::FindDeltaAngleRadians(InCamYaw, WantYaw);
	const float Excess = FMath::Sign(D) * FMath::Max(0.f, FMath::Abs(D) - DeadZone);
	const float Follow = Excess * (1.f - FMath::Exp(-InFollowRate * Dt));
	const float Recenter = (D - Excess) * (1.f - FMath::Exp(-InRecenterRate * Dt));
	return FMath::UnwindRadians(InCamYaw + Follow + Recenter);
}

void ABoxingFightPlayerController::UpdateCamera(float DeltaSeconds)
{
	const ABoxingFightGameMode* GM = GetFightMode();
	if (!GM || !FightCamera)
	{
		return;
	}
	const ABoxerCharacter* B0 = GM->GetBoxer(0);
	const ABoxerCharacter* B1 = GM->GetBoxer(1);
	if (!B0 || !B1)
	{
		return;
	}
	if (GetViewTarget() != FightCamera)
	{
		SetViewTarget(FightCamera); // камера GASP-персонажа могла перехватить вид
	}
	const FVector Floor = GM->GetRingFloorCenter();
	const FVector P0 = B0->GetActorLocation();
	const FVector P1 = B1->GetActorLocation();
	const FVector Mid((P0.X + P1.X) * 0.5f, (P0.Y + P1.Y) * 0.5f, Floor.Z);
	const float WantYaw = FMath::Atan2(P1.Y - P0.Y, P1.X - P0.X);
	if (!bCamInit)
	{
		bCamInit = true;
		CamMid = Mid;
		CamYaw = WantYaw;
		CamStageMix = FVector::Dist2D(P0, P1) > 500.f ? 1.f : 0.f; // старт в углах — сразу вид постановки (S-53)
	}
	const float K = 1.f - FMath::Exp(-MidFollowRate * DeltaSeconds);
	CamMid += (Mid - CamMid) * K;
	CamYaw = FollowYaw(CamYaw, WantYaw, DeltaSeconds, FMath::DegreesToRadians(DeadZoneDeg), FollowRate, RecenterRate);

	// u — ось пары, r = (−u.y, u.x) — вправо от игрока (UE: X вперёд, Y вправо).
	const FVector U(FMath::Cos(CamYaw), FMath::Sin(CamYaw), 0.f);
	const FVector R(-U.Y, U.X, 0.f);
	// Постановка раунда (S-53, stageCamMix/stageCamOffset веба): пара шире боевой дистанции (выход из углов,
	// уход в угол, нейтральный угол) — камера за спиной игрока вдоль оси и выше, оба в кадре; сошлись — камера боя.
	const float PairDistM = FVector::Dist2D(P0, P1) / 100.f;
	const float StageU = FMath::Clamp((PairDistM - 2.1f) / (5.0f - 2.1f), 0.f, 1.f);
	const float WantStage = StageU * StageU * (3.f - 2.f * StageU);
	CamStageMix = CamStageMix + (WantStage - CamStageMix) * (1.f - FMath::Exp(-2.2f * DeltaSeconds));
	const float Sm = CamStageMix;
	const float StageBack = FMath::Lerp(CamBack, PairDistM * 50.f + 280.f, Sm);
	const float StageSide = FMath::Lerp(CamSide, 80.f, Sm);
	FVector Cam = CamMid - U * StageBack + R * StageSide;
	// Пара у канатов → камера за рингом: поднимаем, чтобы смотреть поверх канатов (на постановке и так высоко).
	const double Out = FMath::Max3(0.0, FMath::Abs(Cam.X - Floor.X) - RopeHalf, FMath::Abs(Cam.Y - Floor.Y) - RopeHalf);
	Cam.Z = Floor.Z + CamHeight + 130.f * Sm + FMath::Min(80.0, Out * 0.35) * (1.f - Sm);
	// S-58: камера боя без постановки и эффектов (на выходе из углов камера за спиной игрока, вдоль оси пары — сторона
	// по ней не определена): по ней рефери выбирает дальнюю сторону.
	RefCamBase = CamMid - U * CamBack + R * CamSide;
	bRefCamValid = true;
	// Точка взгляда на постановке — к игроку (у своего угла он в кадре целиком).
	const FVector Look = CamMid + U * (LookAhead * (1.f - Sm) - PairDistM * 50.f * 0.35f * Sm) + FVector(0.f, 0.f, LookHeight - 10.f * Sm);
	if (bSideCam)
	{
		const FVector PairMid((P0.X + P1.X) * 0.5f, (P0.Y + P1.Y) * 0.5f, Floor.Z);
		const FVector Ax = (P1 - P0).GetSafeNormal2D();
		const FVector Side(-Ax.Y, Ax.X, 0.f);
		Cam = PairMid + Side * 260.f + FVector(0.f, 0.f, 150.f);
		FVector SideLook = PairMid + FVector(0.f, 0.f, 135.f);
		float FxFov = 0.f;
		if (UBoxingFightFx* Fx = UBoxingFightFx::Get(this))
		{
			Fx->ModifyCamera(Cam, SideLook, FxFov); // S-54: тряска/наезд/толчок, камера повтора нокаута
		}
		RefCamFinal = Cam; // S-58
		FightCamera->SetActorLocationAndRotation(Cam, (SideLook - Cam).Rotation());
		UpdateRopeVisibility(Cam, Floor);
	}
	else
	{
		FVector FxLook = Look;
		float FxFov = 0.f;
		if (UBoxingFightFx* Fx = UBoxingFightFx::Get(this))
		{
			Fx->ModifyCamera(Cam, FxLook, FxFov); // S-54: тряска/наезд/толчок, камера повтора нокаута
		}
		RefCamFinal = Cam; // S-58
		FightCamera->SetActorLocationAndRotation(Cam, (FxLook - Cam).Rotation());
		UpdateRopeVisibility(Cam, Floor);
	}

	// Вертикальный FOV веба → горизонтальный FOV UE по аспекту вьюпорта.
	int32 VX = 16, VY = 9;
	GetViewportSize(VX, VY);
	const float Aspect = VY > 0 ? static_cast<float>(VX) / static_cast<float>(VY) : 16.f / 9.f;
	const float HFov = 2.f * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFov * 0.5f)) * Aspect));
	FightCamera->GetCameraComponent()->SetFieldOfView(FMath::Clamp(HFov, 30.f, 120.f));
}

void ABoxingFightPlayerController::CollectRopes()
{
	bRopesCollected = true;
	const ABoxingFightGameMode* GM = GetFightMode();
	const FVector Floor = GM ? GM->GetRingFloorCenter() : FVector::ZeroVector;
	static const TCHAR* Prefixes[] = {TEXT("Ring_Rope"), TEXT("Ring_Post"), TEXT("Ring_Pad"), TEXT("Rope"), TEXT("Post")};
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* A = *It;
		if (!A || A->IsA<APawn>())
		{
			continue;
		}
		bool bRope = A->ActorHasTag(TEXT("RingRope"));
		if (!bRope)
		{
			const FString Label = A->GetActorNameOrLabel();
			for (const TCHAR* Prefix : Prefixes)
			{
				if (!Label.Contains(TEXT("Process")) && Label.StartsWith(Prefix)) // PostProcessVolume — не столб
				{
					bRope = true;
					break;
				}
			}
		}
		if (!bRope)
		{
			continue;
		}
		// Сторона ринга — по положению актора относительно центра (угловые столбы — сразу двух сторон).
		const FVector P = A->GetActorLocation() - Floor;
		const float Edge = RopeHalf - 60.f;
		int32 Mask = 0;
		if (P.X > Edge) Mask |= 1;
		if (P.X < -Edge) Mask |= 2;
		if (P.Y > Edge) Mask |= 4;
		if (P.Y < -Edge) Mask |= 8;
		if (Mask == 0)
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Prims;
		A->GetComponents(Prims);
		for (UPrimitiveComponent* C : Prims)
		{
			if (C)
			{
				FRopePart Part;
				Part.Comp = C;
				Part.SideMask = Mask;
				RopeParts.Add(Part);
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("FIGHT CAMERA: частей канатов/столбов для скрытия у камеры: %d"), RopeParts.Num());
}

void ABoxingFightPlayerController::UpdateRopeVisibility(const FVector& Cam, const FVector& Floor)
{
	if (!bRopesCollected)
	{
		CollectRopes();
	}
	const FVector C = Cam - Floor;
	int32 Hide = 0;
	if (bHideNearRopes)
	{
		if (C.X > RopeHideFrom) Hide |= 1;
		if (C.X < -RopeHideFrom) Hide |= 2;
		if (C.Y > RopeHideFrom) Hide |= 4;
		if (C.Y < -RopeHideFrom) Hide |= 8;
	}
	for (FRopePart& Part : RopeParts)
	{
		const bool bHide = (Part.SideMask & Hide) != 0;
		if (bHide != Part.bHidden && Part.Comp.IsValid())
		{
			// Ни основной проход, ни depth-prepass (иначе остаётся чёрный силуэт); тень (shadow depth) остаётся.
			Part.Comp->SetRenderInMainPass(!bHide);
			Part.Comp->SetRenderInDepthPass(!bHide);
			Part.bHidden = bHide;
		}
	}
}

void ABoxingFightPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHeldInput();
	UpdateCamera(DeltaSeconds);
}
