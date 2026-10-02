// ABoxingFightPlayerController — ввод (Enhanced Input) и камера боя (S-41, трек B).
//
// Пешки нет: ввод превращается в действия ядра (ABoxingFightGameMode::QueueAction/SetHeldStep).
// Раскладка — как в вебе (CLAUDE.md, «Интерактивный бой»):
//   J/K — джеб/кросс, U/I — хук левой/правой, N/M — апперкот левой/правой, Shift+удар — в корпус;
//   WASD — ноги ОТНОСИТЕЛЬНО ЭКРАНА: D/A — к сопернику/назад, W/S — дуга вверх/вниз по экрану
//   (= влево/вправо от лица бойца), Shift+W/S — пивот; Q/E — уклоны; Пробел (держать) — блок;
//   стрелки дублируют WASD; Enter — следующий раунд (если перерыв не автоматический).
//   Геймпад: X/Y — джеб/кросс, LB/RB — хуки, A/B — апперкоты левой/правой, RT (держать) — в корпус,
//   LT (держать) — блок, левый стик — ноги (L3 + вверх/вниз — пивот), правый стик вбок — уклоны.
//   Во время своего нокдауна любой удар/блок — тап подъёма.
// Ассеты ввода: /Game/Boxing/Input/IA_* и IMC_Fight (Tools/EditorScripts/fight_input.py); если их
// нет — те же действия и маппинг создаются в рантайме.
//
// Камера — как в вебе (InteractiveFight.tsx): игрок слева, вид с его правого плеча (1.5 м за
// серединой пары, 3.05 м вбок), высота 1.72 м, взгляд в 0.15 м впереди середины на 1.05 м;
// курс за осью «игрок → соперник» с мёртвой зоной ±20° (followYaw), середина — плавно.
// Ближние канаты: камера на высоте веба (1.72 м) за рингом смотрит сквозь ближнюю сторону канатов —
// как в вебе (Ring.tsx fadeNearCamera), сторона канатов/столбов, за которой стоит камера, не рисуется
// (SetRenderInMainPass(false): тень остаётся). Канаты — акторы с тегом RingRope или с меткой
// Ring_Rope*/Ring_Post*/Ring_Pad*/Ring_RopeTie* (L_Ring трека A), Rope*/Post* (L_FightTest).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FightTypes.h"
#include "BoxingFightPlayerController.generated.h"

class ACameraActor;
class UInputAction;
class UInputMappingContext;
struct FInputActionInstance;
class ABoxingFightGameMode;

UCLASS()
class BOXINGUE_API ABoxingFightPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABoxingFightPlayerController();

	// Папка ассетов ввода (IA_<Имя>, IMC_Fight).
	UPROPERTY(EditAnywhere, Category = "Boxing|Input")
	FString InputFolder = TEXT("/Game/Boxing/Input");

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Boxing|Input")
	TObjectPtr<UInputMappingContext> FightContext;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInputAction>> Actions;

	// ---------- Камера боя (см, градусы) ----------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float CamBack = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float CamSide = 305.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float CamHeight = 172.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float LookHeight = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float LookAhead = 15.f;

	// Вертикальный FOV как в вебе (46°); горизонтальный для UE считается по аспекту вьюпорта.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float VerticalFov = 46.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float DeadZoneDeg = 20.f;

	// Догон излишка за мёртвой зоной и ленивое доцентровывание внутри неё (1/с).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float FollowRate = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float RecenterRate = 0.35f;

	// Сглаживание середины пары (1/с).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float MidFollowRate = 3.f;

	// Половина ринга внутри канатов (см) — камера за канатами приподнимается.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float RopeHalf = 305.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Boxing|Camera")
	TObjectPtr<ACameraActor> FightCamera;

	// Не рисовать сторону канатов между камерой и парой.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	bool bHideNearRopes = true;

	// Сторона прячется, когда камера дальше этой линии от центра ринга (см, вдоль нормали стороны).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Camera")
	float RopeHideFrom = 255.f;

	// Курс камеры с мёртвой зоной (порт followYaw из web/src/ui/fightFx.ts), радианы.
	static float FollowYaw(float CamYaw, float WantYaw, float Dt, float DeadZone, float FollowRate, float RecenterRate);

	// S-58 (рефери): камера на кадре — Base — камера боя/постановки без эффектов (тряска, кадры нокдауна/перерыва,
	// повтор), Final — итоговая. false — камеры ещё нет.
	bool GetRefereeCamera(FVector& OutBase, FVector& OutFinal) const
	{
		OutBase = RefCamBase;
		OutFinal = RefCamFinal;
		return bRefCamValid;
	}

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void EnsureActions();
	UInputAction* Act(const TCHAR* Name) const;
	float ActValue(const TCHAR* Name) const;
	ABoxingFightGameMode* GetFightMode() const;
	bool IsPlayerDown() const;

	void OnPunch(const FInputActionInstance& Instance, EFightAction Action);
	void OnBlock(const FInputActionInstance& Instance, bool bOn);
	void OnSlip(const FInputActionInstance& Instance, EFightAction Action);
	void OnProceed(const FInputActionInstance& Instance);

	void UpdateHeldInput();
	void UpdateCamera(float DeltaSeconds);
	void CollectRopes();
	void UpdateRopeVisibility(const FVector& Cam, const FVector& Floor);

	struct FRopePart
	{
		TWeakObjectPtr<class UPrimitiveComponent> Comp;
		int32 SideMask = 0; // биты: 0 +X, 1 −X, 2 +Y, 3 −Y
		bool bHidden = false;
	};
	TArray<FRopePart> RopeParts;
	bool bRopesCollected = false;

	bool bCamInit = false;
	// Отладка «ощущения удара»: -BoxCamSide — камера строго сбоку от пары, ближе, на высоте голов (видно, доходит ли кулак).
	bool bSideCam = false;
	FVector CamMid = FVector::ZeroVector;
	float CamYaw = 0.f;
	// Постановка раунда (S-53): доля вида «из-за спины игрока» (stageCamMix веба), пара шире боевой дистанции.
	float CamStageMix = 0.f;
	int32 PrevSlipAxis = 0;
	// S-58: камера для рефери (GetRefereeCamera).
	FVector RefCamBase = FVector::ZeroVector;
	FVector RefCamFinal = FVector::ZeroVector;
	bool bRefCamValid = false;
};
