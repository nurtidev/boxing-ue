// BoxingFightProfile — проекция бойца на вес боя и «честный» прогноз пары (S-61).
//
// Порт части web/src/engine/boxer.ts `Boxer.fightProfile(target_kg, pro)`, отвечающей за ВЕС (идея №2 CLAUDE.md веба):
// сгонка вниз — падают кардио/подбородок, но мышцы сохранены (massForPower > target, бьёшь как более крупный);
// переход вверх — реальной массы не прибавилось: абсолютная сила/живучесть против больших тел падают, подбородок держит
// хуже чужой калибр. Скидка amateur→pro (GREEN_CRAFT_PENALTY × STYLE_TRANSFER) уже лежит в ProStats ростера (экспорт
// веба), здесь не повторяется: обе поправки мультипликативные, порядок не важен.
//
// Вес боя Выставки — как App.tsx веба: кэтчвейт = середина пары, привязанная к ближайшей РЕАЛЬНОЙ категории (любители —
// WEIGHT_CLASSES веба, профи/легенды — веса дивизионов ростера), при равенстве — меньшая.
//
// Только CoreMinimal (без Engine/UObject): модуль собирается и в харнессе ядра (Tools/CoreHarness).
#pragma once

#include "CoreMinimal.h"
#include "FightTypes.h"

namespace BoxingFightProfile
{
	// WEIGHT_CLASSES веба (stats.ts): любительские категории, кг.
	inline constexpr double AMATEUR_CLASSES_M[] = {50, 55, 60, 65, 70, 75, 80, 85, 90, 92, 100};
	inline constexpr double AMATEUR_CLASSES_F[] = {48, 51, 54, 57, 60, 65, 70, 75, 80, 81};

	// Ближайшая категория (reduce веба: строго ближе — заменяет, при равенстве остаётся меньшая). Classes — по возрастанию.
	BOXINGUE_API double NearestClass(double Kg, const double* Classes, int32 Num);
	// Кэтчвейт пары: NearestClass((a + b) / 2).
	BOXINGUE_API double Catchweight(double RedKg, double BlueKg, const double* Classes, int32 Num);

	// fightProfile(target): Setup — боец на РОДНОМ весе (WeightKg — натуральный, статы уже нужного контекста: Stats или
	// ProStats). Возвращает копию со статами после сгонки/набора и MassForPower/DurabilityMass. WeightKg не меняется
	// (это тело бойца — масштаб модели в UE), масса для урона — в MassForPower/DurabilityMass.
	BOXINGUE_API FFighterSetup ProjectToWeight(const FFighterSetup& Setup, double TargetKg);

	// «Насколько натянута» пара (realismTag веба): макс. отклонение веса бойца от веса боя, кг.
	// ≤ 4 — реальный бой, ≤ 8 — кэтчвейт-натяжка, ≤ 14 — бой мечты, дальше — фэнтези.
	BOXINGUE_API double WeightStretch(double RingKg, double RedKg, double BlueKg);

	// Ожидание исхода пары — быстрое моделирование ядром: Fights боёв ИИ против ИИ (автопилот), раунд 55 с, без постановки,
	// шаг 0.05 с, сиды фиксированы (результат детерминирован), каждый второй бой — углы меняются местами (как тест паритета
	// веба: нет преимущества угла). Сетапы — уже спроецированные (ProjectToWeight). ≈ 0.5 мс на бой (10 р.) — 60 боёв ≈ 30–70 мс.
	struct FOutcomeOdds
	{
		float RedWin = 0.f;       // доля побед красного 0..1
		float BlueWin = 0.f;
		float Draw = 0.f;         // только профи (ничья возможна)
		float RedStoppage = 0.f;  // доля боёв, где красный победил досрочно (KO/RSC)
		float BlueStoppage = 0.f;
		float KnockdownsPerFight = 0.f;
		int32 Fights = 0;
	};
	BOXINGUE_API FOutcomeOdds PredictOutcome(FFighterSetup Red, FFighterSetup Blue, int32 Rounds, bool bProRules, int32 Fights = 60,
		uint32 Seed = 0x5eedu);
}
