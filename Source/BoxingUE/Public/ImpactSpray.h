// S-74 (game feel): брызги пота в точке контакта — порт web/src/ui/three/impactBurst.ts + ImpactFx.tsx. Только визуал:
// свой ГСЧ (FRandomStream с постоянным сидом), ядро и его ГСЧ не трогаются.
//
//  * BoxSpray::ImpactBurst — чистая логика (тест BoxingUE.ImpactSpray): сколько капель, скорость вдоль удара, разброс,
//    подъём, яркость по силе и виду (голова / корпус / блок). Слабые удары — без капель (не спамим эффектами).
//  * FBoxImpactSpray — пул капель на одном UInstancedStaticMeshComponent (сфера движка, материал M_SweatDrop:
//    Tools/EditorScripts/feel_sweat_drop.py): баллистика с гравитацией и сопротивлением, капля вытянута по скорости,
//    гаснет за 0.22–0.42 с. Время — игровое (slow-mo нокдауна замедляет брызги, как в вебе).
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

class UInstancedStaticMeshComponent;
class UWorld;
class AActor;

namespace BoxSpray
{
	enum class EKind : uint8
	{
		Head,
		Body,
		Block
	};

	struct FBurst
	{
		int32 Drops = 0;
		float Speed = 0.f;  // м/с вдоль удара
		float Spread = 0.f; // м/с вбок/вверх
		float Lift = 0.f;   // м/с вверх
		float Bright = 0.f; // 0..1
	};

	// Ниже этой силы — ни капли (web DROPS_MAG_MIN).
	constexpr float DROPS_MAG_MIN = 0.35f;

	FBurst ImpactBurst(float Mag, EKind Kind);
}

class FBoxImpactSpray
{
public:
	// Material — путь материала капли; нет — капли не рисуются (лог один раз).
	void Spawn(UWorld* World, const FVector& At, const FVector& Dir, float Mag, BoxSpray::EKind Kind);
	void Update(float GameDt);
	void Reset();
	int32 Alive() const;

	static constexpr int32 POOL = 160;

private:
	bool Ensure(UWorld* World);

	struct FDrop
	{
		FVector P = FVector::ZeroVector; // см
		FVector V = FVector::ZeroVector; // см/с
		float Life = 0.f;
		float Max = 0.f;
		float Bright = 0.f;
		float Size = 1.f;
	};
	TArray<FDrop> Drops;
	TWeakObjectPtr<AActor> Host;
	TWeakObjectPtr<UInstancedStaticMeshComponent> Ism;
	FRandomStream Rng = FRandomStream(7407);
	int32 Next = 0;
	bool bTried = false;
	bool bDirty = false;
};
