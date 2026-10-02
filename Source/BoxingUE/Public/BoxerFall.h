// Падение внутри канатов (S-62, game feel): чистая логика без акторов — тест BoxingUE.BoxerFall.
//
// Клип нокдауна/нокаута Mixamo роняет бойца назад на ~1.6–1.9 м (с масштабом облика S-60 — больше), а ядро
// держит точку бойца где угодно до RING_HALF (2.77 м от центра): у канатов голова и руки ложились за край
// помоста. Здесь — раскладка «лежащего» в осях бойца (из клипа, см. ABoxerCharacter::FallLayoutFor) и выбор
// ВИЗУАЛЬНОГО доворота курса и сдвига точки, при которых всё тело лежит внутри канатов, не на сопернике/рефери
// и не на пути стоящего в нейтральный угол. Ядро и его ГСЧ не трогаются: сдвиг — только у видимого бойца.
#pragma once

#include "CoreMinimal.h"

namespace BoxerFall
{
	// Предел для точек тела (центры костей) от центра ринга по каждой оси, см: канаты 305 минус запас на толщину.
	constexpr float ROPE_LIMIT_CM = 305.f - 22.f;
	// Цена: 1 м сдвига ≈ 80° доворота (скольжение тела по настилу заметнее, чем падение с разворотом); рядом с
	// соперником/рефери/путём стоящего — штраф.
	constexpr float COST_PER_DEG = 1.f / 60.f;
	constexpr float COST_PER_CM = 1.f / 75.f;
	constexpr float AVOID_COST = 4.f / 100.f; // за см «внутрь» радиуса обхода
	constexpr float MAX_TURN_DEG = 180.f;

	// Раскладка тела лежащего: точки в осях бойца (X — вперёд, Y — вправо, см), уже с масштабом облика.
	struct FLayout
	{
		TArray<FVector2D> Pts;
		FVector2D Head = FVector2D::ZeroVector;
		FVector2D Pelvis = FVector2D::ZeroVector;
		bool bValid = false;
	};

	// Препятствие: точка или отрезок (A→B) с радиусом обхода, см (мир, относительно центра ринга).
	struct FAvoid
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		float R = 0.f;
	};

	struct FPlaceIn
	{
		FVector2D Pos = FVector2D::ZeroVector; // точка бойца (ядро), относительно центра ринга, см
		float YawDeg = 0.f;                    // курс бойца (UE: X вперёд, Y вправо)
		const FLayout* Layout = nullptr;
		float Limit = ROPE_LIMIT_CM;
		TArray<FAvoid> Avoid;
	};

	struct FPlaceOut
	{
		float TurnDeg = 0.f;                   // доворот курса
		FVector2D Offset = FVector2D::ZeroVector; // сдвиг точки, см
		bool bInside = true;                   // всё тело внутри канатов
		float Cost = 0.f;
	};

	// Точка раскладки в мире (относительно центра ринга) при курсе YawDeg и точке Pos.
	BOXINGUE_API FVector2D ToWorld(const FVector2D& Local, const FVector2D& Pos, float YawDeg);
	// Расстояние от точки до отрезка.
	BOXINGUE_API float SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B);
	// Насколько тело выходит за канаты (см, 0 — внутри) при данном курсе/точке.
	BOXINGUE_API float Overhang(const FLayout& L, const FVector2D& Pos, float YawDeg, float Limit);
	// Выбор доворота и сдвига: минимальная цена при теле внутри канатов (перебор курса с шагом 5°).
	BOXINGUE_API FPlaceOut Solve(const FPlaceIn& In);
	// Огибающая сдвига по времени падения: 0 → 1 smoothstep за BlendS.
	BOXINGUE_API float Blend(float T, float BlendS);
}
