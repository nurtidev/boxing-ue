// «Совет угла» в перерыве (S-71): порт web/src/ui/cornerAdvice.ts — чистая логика без UObject/Engine и без ГСЧ.
// FCornerTally копит статистику текущего раунда из ПОТОКА СОБЫТИЙ ядра (FFightEvent) и снимков (канаты, дистанция,
// стамина) — ядро не трогается. CornerTalk(...) по статистике раунда и его картам у судей собирает 1–2 совета тренера
// по важности (нокдаун > усталость > «читает» > блок/канаты/размен > корпус > похвала) + реплику катмена, если боец
// побит, и строку итога раунда у судей (любители — 5 судей, профи — 3). Род игрока и соперника — в текстах.
// Индекс игрока — Me (в бою UE игрок всегда красный, 0); соперник — 1 − Me.
#pragma once

#include "CoreMinimal.h"
#include "FightTypes.h"

namespace CornerAdvice
{
	// Тяжёлое попадание (≈ верх распределения Magnitude; хит-стоп — с 1.1, FightFx).
	inline constexpr float HEAVY = 1.1f;
	// Пороги дистанции (подпись HUD веба distLabel).
	inline constexpr float CLOSE_D = 1.08f;
	inline constexpr float LONG_D = 1.5f;
	inline constexpr int32 MAX_TIPS = 2;

	// Статистика одного раунда (RoundStats веба). Виды ударов — EPunchKind: 0 джеб, 1 кросс, 2 хук, 3 апперкот.
	struct FRoundStats
	{
		int32 Round = 1;
		// Удары игрока: брошено / дошло / в блок / мимо (из них — «провалился» в нырок соперника).
		int32 Thrown = 0;
		int32 Landed = 0;
		int32 Blocked = 0;
		int32 Missed = 0;
		int32 SlippedByFoe = 0;
		int32 ByKind[4] = {0, 0, 0, 0};
		int32 HeadThrown = 0;
		int32 HeadLanded = 0;
		int32 HeadBlocked = 0;
		int32 BodyThrown = 0;
		int32 BodyLanded = 0;
		int32 Counters = 0;
		// Соперник по игроку.
		int32 FoeThrown = 0;
		int32 FoeLanded = 0;
		int32 FoeLandedClose = 0; // хуки/апперкоты
		int32 FoeLandedLong = 0;  // джеб/кросс
		int32 FoeBodyThrown = 0;
		int32 FoeBodyLanded = 0;
		int32 HeavyTaken = 0;
		int32 HeavyGiven = 0;
		// Защита игрока.
		int32 Slips = 0;
		int32 BlocksTaken = 0;
		int32 GuardBroken = 0;
		int32 Caught = 0;
		// Нокдауны раунда.
		int32 KdTaken = 0;
		int32 KdGiven = 0;
		// Из снимков.
		int32 ReadHints = 0;  // «читает» (в UE снимок этого не отдаёт — 0, правило держится на доле одного удара)
		int32 GassedHits = 0; // удары, не вышедшие на пустом баке (события Gassed)
		float FightTime = 0.f;
		float RopesTime = 0.f;
		float CloseTime = 0.f;
		float LongTime = 0.f;
		// На конец раунда (последний снимок боя), 0..100.
		float Stamina = 100.f;
		float Health = 100.f;
		float FoeStamina = 100.f;
		float FoeHealth = 100.f;
		// Самый частый удар игрока (вид:цель) и его доля среди брошенных.
		bool bHasTop = false;
		EPunchKind TopKind = EPunchKind::Jab;
		EPunchTarget TopTarget = EPunchTarget::Head;
		float TopShare = 0.f;
	};

	// Копилка раунда. Feed — каждое событие ядра (как пришло), Sample — каждый кадр со снимком (Dt — реальное время кадра).
	// Новый раунд (Snap.Round) — сброс.
	class BOXINGUE_API FCornerTally
	{
	public:
		explicit FCornerTally(int32 InMe = 0) : Me(InMe) {}
		void Reset(int32 Round);
		void Feed(const FFightEvent& E);
		void Sample(const FFightSnapshot& Snap, float Dt, bool bRead = false);
		const FRoundStats& Stats() const { return S; }

	private:
		void ThrownBy(EPunchType Punch, EPunchTarget Target);
		int32 Me = 0;
		FRoundStats S;
		int32 Combo[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // вид*2+цель → брошено
		bool bWasRead = false;
	};

	enum class ETone : uint8 { Good, Warn, Bad, Even };
	enum class EWho : uint8 { Trainer, Cutman };

	struct FTip
	{
		EWho Who = EWho::Trainer;
		FString Text;
		ETone Tone = ETone::Warn;
	};

	// Как раунд увидели судьи (карты ЭТОГО раунда, игрок — первым).
	struct FVerdict
	{
		bool bValid = false;
		FString Text;
		ETone Tone = ETone::Even; // Good / Bad / Even
		TArray<FJudgeCard> Cards; // Red — игрок, Blue — соперник
	};

	struct FTalk
	{
		TArray<FTip> Tips; // по важности, тренер — не больше MAX_TIPS (+ реплика катмена)
		FVerdict Verdict;  // bValid = false — раунд ещё не судили
		FString Line;      // «попадания 14–9 · точность 38%»
	};

	// Род для реплик (TalkGender веба): игрок («ты устал/устала») и соперник («он/она читает»).
	struct FGender
	{
		bool bMeFemale = false;
		bool bFoeFemale = false;
	};

	// Карты раунда (Num судей) → вердикт. bPlayerRed — карты в порядке [красный, синий]; игрок синий — поменять.
	BOXINGUE_API FVerdict RoundVerdict(const FJudgeCard* Cards, int32 Num, const FGender& G, bool bPlayerRed = true);
	BOXINGUE_API FString StatsLine(const FRoundStats& S);
	BOXINGUE_API FTalk CornerTalk(const FRoundStats& S, const FVerdict& Verdict, const FGender& G);
	// Карты раунда — разность накопленных сумм судей (снимок даёт только суммы): Totals − Prev.
	BOXINGUE_API void RoundCards(const FJudgeCard* Totals, const FJudgeCard* Prev, int32 Num, FJudgeCard* Out);
}
