// UE-обёртки типов ядра боя для Blueprint/AnimBP (S-41, трек B).
// Ядро (FightTypes.h) — plain C++ без UHT, поэтому его enum'ы нельзя выставить в UPROPERTY.
// Здесь — зеркальные UENUM/USTRUCT с тем же порядком значений + конвертеры.
#pragma once

#include "CoreMinimal.h"
#include "FightTypes.h"
#include "BoxingFightBPTypes.generated.h"

// Удар: тип + рука (зеркало EPunchType). Стойка ортодоксальная: L — передняя (lead), R — дальняя (rear).
UENUM(BlueprintType)
enum class EBoxPunchType : uint8
{
	Jab,
	Cross,
	HookL,
	HookR,
	UpperL,
	UpperR,
};

UENUM(BlueprintType)
enum class EBoxPunchTarget : uint8
{
	Head,
	Body,
};

UENUM(BlueprintType)
enum class EBoxPunchArm : uint8
{
	Lead,
	Rear,
};

// Текущее перемещение бойца (зеркало EStepKind).
UENUM(BlueprintType)
enum class EBoxStepKind : uint8
{
	None,
	Fwd,
	Back,
	Left,
	Right,
	PivotL,
	PivotR,
};

// Стиль бойца (зеркало EBoxStyle ядра).
UENUM(BlueprintType)
enum class EBoxerStyle : uint8
{
	Technical,
	Volume,
	Puncher,
	Pressure,
	Counter,
	Speed,
	Balanced,
};

// Фаза боя (зеркало EFightPhase).
UENUM(BlueprintType)
enum class EBoxFightPhase : uint8
{
	Fighting,
	Down,
	Between,
	Over,
};

// Пресет бойца для GameMode: 7 статов 0..100 (как Stats веба) + физика + стиль.
USTRUCT(BlueprintType)
struct FBoxerPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing")
	FString Name = TEXT("Боец");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float Power = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float HandSpeed = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float Footwork = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float Stamina = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float Chin = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float Technique = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Stats", meta = (ClampMin = "0", ClampMax = "100"))
	float Defense = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Body")
	float HeightCm = 178.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Body")
	float ReachCm = 183.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Body")
	float WeightKg = 71.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing")
	EBoxerStyle Style = EBoxerStyle::Balanced;

	// 1 — обстрелян; < 1 — «зелёный» на дистанции (налог в баке и у судей).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing", meta = (ClampMin = "0", ClampMax = "1"))
	float Seasoning = 1.f;

	FFighterSetup ToSetup(bool bAi) const
	{
		FFighterSetup S;
		S.Stats.Power = Power;
		S.Stats.HandSpeed = HandSpeed;
		S.Stats.Footwork = Footwork;
		S.Stats.Stamina = Stamina;
		S.Stats.Chin = Chin;
		S.Stats.Technique = Technique;
		S.Stats.Defense = Defense;
		S.HeightCm = HeightCm;
		S.ReachCm = ReachCm;
		S.WeightKg = WeightKg;
		S.Style = static_cast<EBoxStyle>(Style);
		S.Seasoning = Seasoning;
		S.bAiControlled = bAi;
		return S;
	}
};

// Конвертеры ядро → BP (порядок значений совпадает).
namespace BoxingBP
{
	inline EBoxPunchType Punch(EPunchType P) { return static_cast<EBoxPunchType>(P); }
	inline EBoxPunchTarget Target(EPunchTarget T) { return static_cast<EBoxPunchTarget>(T); }
	inline EBoxPunchArm Arm(EPunchArm A) { return static_cast<EBoxPunchArm>(A); }
	inline EBoxStepKind Step(EStepKind S) { return static_cast<EBoxStepKind>(S); }
	inline EBoxFightPhase Phase(EFightPhase P) { return static_cast<EBoxFightPhase>(P); }

	// Рука удара: джеб и «L»-удары — передняя, остальные — дальняя (armFor в TS).
	inline EBoxPunchArm ArmOf(EBoxPunchType P)
	{
		return (P == EBoxPunchType::Jab || P == EBoxPunchType::HookL || P == EBoxPunchType::UpperL)
			? EBoxPunchArm::Lead : EBoxPunchArm::Rear;
	}
}
