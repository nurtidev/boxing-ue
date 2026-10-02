// Геометрия углов ринга и постановки раунда — порт web/src/engine/interactive/corners.ts (S-53).
// Чистые функции без состояния и без ГСЧ: ими пользуется ядро (FBoxingFightCore, FightStaging.cpp),
// а UE-слой (рефери, угловые, камера) может звать их сам — та же геометрия, что у ядра.
//
// Координаты — как у ядра: метры в плоскости (X, Z), центр ринга (0, 0); в UE X_ue = X·100, Y_ue = Z·100.
// Углы: красный (−X −Z) и синий (+X +Z) — по диагонали, нейтральные (белые) — (+X −Z) и (−X +Z),
// знаки — CORNER_SIGNS из web/src/engine/ringSize.ts; в L_Ring это маркеры Corner_Red/Blue/NeutralA/B
// на (±263, ±263) см (= CORNER_SPOT).
#pragma once

#include "CoreMinimal.h"

namespace BoxingStaging
{
	// ringSize.ts / ring.ts (дубли констант ядра; совпадение проверяет static_assert в BoxingFightCore.cpp).
	constexpr double ROPE_HALF = 3.05;
	constexpr double RING_HALF = ROPE_HALF - 0.28;
	constexpr double DIST_MIN = 0.9;
	constexpr double DIST_START = 1.15;

	// Центр бойца в углу — в стольких метрах от обоих канатов (спиной к подушке угла).
	constexpr double CORNER_INSET = 0.42;
	constexpr double CORNER_SPOT = ROPE_HALF - CORNER_INSET; // 2.63 м
	// Скорость ходьбы по постановке: из угла к точке встречи ≈ 3.3 м → ~1.85 с (+ пауза гонга).
	constexpr double WALK_SPEED = 1.8;
	// По гонгу — миг, и из угла (и в перерыве — развернуться к своему углу).
	constexpr double GONG_DELAY = 0.25;
	// Пока стоящий идёт в нейтральный угол, до лежащего не ближе этого (обходит, а не идёт сквозь).
	constexpr double PASS_CLEAR = 1.0;
	// На ходу друг мимо друга (выход/перерыв) — не ближе этого (плечом к плечу, не сквозь).
	constexpr double WALK_CLEAR = 0.75;
	// Тело упавшего — на столько за точкой падения (падает назад, от соперника).
	constexpr double BODY_BACK = 0.6;

	struct FRingPoint
	{
		double X = 0;
		double Z = 0;
	};

	// Свой угол бойца: 0 — красный (−,−), 1 — синий (+,+).
	BOXINGUE_API FRingPoint CornerOf(int32 Fighter);
	// Нейтральный угол K: 0 — (+X, −Z), 1 — (−X, +Z).
	BOXINGUE_API FRingPoint NeutralCorner(int32 K);
	// Точка встречи после выхода из угла: центр на DIST_START по оси X (= расстановка без углов).
	BOXINGUE_API FRingPoint MeetPoint(int32 Fighter);
	// Дальний от точки P нейтральный угол.
	BOXINGUE_API FRingPoint FarNeutral(FRingPoint P);
	// Центр тела упавшего в Down, если стоящий — в Stand.
	BOXINGUE_API FRingPoint LyingBody(FRingPoint Down, FRingPoint Stand);
	// Запас пути «стоящий → угол C» до лежащего (м; < 0 — путь через него).
	BOXINGUE_API double NeutralPathClear(FRingPoint Down, FRingPoint Stand, FRingPoint C);
	// В какой нейтральный угол идти стоящему: дальний от лежащего, если путь туда не через тело.
	BOXINGUE_API FRingPoint NeutralFor(FRingPoint Down, FRingPoint Stand);
}
