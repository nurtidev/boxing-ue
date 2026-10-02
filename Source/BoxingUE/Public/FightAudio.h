// Звук боя (S-54) — порт web/src/ui/audio/fightAudio.ts на USoundWave.
//
// Реальные записи CC0 из веба (web/public/audio → /Game/Boxing/Audio, импорт —
// Tools/EditorScripts/feel_audio_import.py, авторы — Content/Boxing/Audio/CREDITS.md):
// head1..5 (удар по голове), body1..5 (мешок), heavy1..2 (НЧ-слой на mag ≥ 1.1), block1..4, whiff1..5,
// fall1..2, bell (1 удар — старт раунда, 3 через 0.3 с — конец), ooh1..3 («у-ух» зала на mag ≥ 1.7 не
// чаще 2.5 с), gasp (нокдаун), cheer (нокаут), crowd_loop (16-с петля гула зала, громкость ≤ 10 раз/с,
// всплески на ударах). Каждый звук — случайный вариант без повтора подряд + разброс высоты/громкости,
// громкость — от силы удара (mag). Звук не пространственный (2D), как в вебе.
// Случайность — FMath::FRand (НЕ ГСЧ ядра: сидируемость боя не затрагивается).
// Задержки (падение тела через 0.28 с, гонг ×3) — своя очередь в реальном времени.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FightAudio.generated.h"

class USoundWave;
class UAudioComponent;

UCLASS(Transient)
class BOXINGUE_API UBoxingFightAudio : public UObject
{
	GENERATED_BODY()

public:
	// Папка сэмплов.
	FString Folder = TEXT("/Game/Boxing/Audio");

	void Init(UWorld* InWorld, bool bInLog);
	void Shutdown();

	bool IsMuted() const { return bMuted; }
	void SetMuted(bool bMute);

	// Попадание: вес по mag; голова — хлёсткий удар, корпус — глухой мешок; тяжёлые — + НЧ-слой; Slow > 1 — ниже/медленнее.
	void Punch(float Mag, bool bBody, float Slow = 1.f);
	void Block(float Mag);
	void Whiff();
	// «Нет сил» — тяжёлый выдох (в вебе синтез; здесь — свист вниз по высоте, тихо).
	void Gassed();
	// Нокдаун: вздох зала сразу, тело о настил чуть позже; KO — ещё и овация.
	void Knockdown(bool bKO);
	// Овация (досрочка без нового нокдауна).
	void Cheer();
	// Гонг: N ударов (1 — начало раунда, 3 — конец).
	void Bell(int32 N);
	void StartCrowd();
	void StopCrowd();
	// Возбуждение зала 0..1 (спадает).
	void Bump(float Amt);
	// Раз в кадр (реальное время).
	void Update(float RealDt);

	// Сколько наборов загружено (лог/проверка).
	int32 NumLoadedSets() const;

private:
	struct FSet
	{
		TArray<TObjectPtr<USoundWave>> Waves;
		int32 Last = -1;
	};
	USoundWave* Pick(const FName Set);
	// Проиграть: громкость, высота (=скорость), задержка (с), НЧ-фильтр (0 — без).
	void Play(USoundWave* W, float Vol, float Pitch = 1.f, float Delay = 0.f, float Lowpass = 0.f, const TCHAR* Tag = nullptr);
	void PlayNow(USoundWave* W, float Vol, float Pitch, float Lowpass);
	void React(float Amt, float Delay, bool bForce = false);
	void LoadSet(const FName Set, const TCHAR* Prefix, int32 N);

	TMap<FName, FSet> Sets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundWave>> Keep; // держим от GC

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Crowd;

	TWeakObjectPtr<UWorld> World;
	bool bLog = false;
	bool bMuted = false;
	bool bCrowdWanted = false;
	float Excite = 0.f;
	float Swell = 0.f;
	float SinceCrowdUpd = 0.f;
	float CrowdLevel = 0.f;
	double Now = 0.0;
	double LastReact = -10.0;

	struct FDelayed
	{
		double At = 0.0;
		TObjectPtr<USoundWave> Wave;
		float Vol = 1.f;
		float Pitch = 1.f;
		float Lowpass = 0.f;
	};
	TArray<FDelayed> Queue;
};
