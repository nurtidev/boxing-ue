// «Ощущение боя» (S-54, game feel) — порт web/src/ui/fightFx.ts + сцены InteractiveFight.tsx.
//
// BoxFx — чистая логика (без мира/акторов, покрыта автотестом BoxingUE.FightFx): длительность хит-стопа,
// тряска/наезд камеры, slow-mo решающих ударов, вибрация, скорость повтора нокаута.
//
// UBoxingFightFx — подсистема мира. ABoxingFightGameMode отдаёт ей события и снимок ядра; она:
//  * ХИТ-СТОП: замораживает картинку бойцов (CustomTimeDilation ≈ 0 у бойцов и их child actor'ов —
//    видимых MetaHuman) И тики ядра (CoreTimeScale() = 0: GameMode не копит время в аккумулятор шага).
//    Почему так: ядро тикает ФИКСИРОВАННЫМ шагом, удар на экране скрабится по фазе ядра — заморозить одну
//    анимацию нельзя (следующий кадр монтаж догонит фазу ядра рывком), а пауза ядра — это просто «меньше
//    шагов за реальное время»: последовательность (шаг, ввод) та же, ГСЧ ядра не трогается → бой
//    воспроизводим по сиду, ввод, нажатый во время стопа, уходит в ядро на ближайшей границе шага
//    (как в вебе: Scene не тикает движок во время хит-стопа). CustomTimeDilation действует сразу —
//    бойцы тикают ПОСЛЕ GameMode в том же кадре, поэтому застывает сам кадр контакта, а отдача головы
//    (пружины реакции) начинается после заморозки. Глобальная TimeDilation для стопа не годится: она
//    влияет только со следующего кадра (кадр отдачи уже ушёл бы на экран).
//  * SLOW-MO: глобальная TimeDilation мира (×0.3 → 1 за 0.9 с на нокдауне) — замедляет всё: ядро (меньше
//    шагов), анимацию, ход; эффекты камеры/звук считаются в реальном времени.
//  * КАМЕРА: ModifyCamera() (зовёт ABoxingFightPlayerController) — тряска (квадрат силы, мелкие не трясут),
//    «наезд» (камера ближе к паре и чуть ниже), толчок по вектору тяжёлого удара (CAM_JOLT).
//  * ЗВУК: UBoxingFightAudio (FightAudio.h) — удары/блок/промах/падение/гонг/зал.
//  * ПОВТОР НОКАУТА: кольцевой буфер кадров (место/курс бойцов, локальная поза логического меша, кадр
//    процедурного слоя) → при досрочке отрезок −2.0…+1.2 с вокруг последнего нокдауна проигрывается в
//    слоу-мо (ReplaySpeed) с низкой боковой камеры.
//
// Флаги: -BoxFx=0 (всё выкл.), -BoxFxProfile=classic (стоп 50→110 мс с mag 1.1 и slow-mo на mag ≥ 1.9 —
// значения веба ДО S-36), -BoxFxLog (лог хит-стопов/slow-mo/камеры/вибрации), -BoxSfxLog (лог звуков),
// -BoxMute, -BoxNoReplay, -BoxReplayShots=N (скриншоты повтора: Saved/Screenshots/BoxReplay, с -BoxShotPrefix=X — Docs/screens/X_replay_NN.png), -BoxFxShots=N (серии кадров хит-стопа: fx_NN_<вид>_<мс>_tNNN.png), -BoxReplayTest (повтор после каждого нокдауна).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FightTypes.h"
#include "BoxerFeel.h"
#include "ImpactSpray.h"
#include "Blueprint/UserWidget.h"
#include "FightFx.generated.h"

class ABoxerCharacter;
class UTextBlock;
class UBorder;
class UProgressBar;
class ABoxingFightGameMode;
class UBoxingFightAudio;
class APlayerController;

namespace BoxFx
{
	// Вид события для эффектов (как FightEvent.kind веба).
	enum class EKind : uint8
	{
		Land,  // Hit
		Block, // Blocked
		Miss,  // Miss / Slipped
		Kd,    // Knockdown
		Ko,    // досрочка (FightEnd KO/RSC)
		Other,
	};

	EKind KindOf(EFightEventKind K);

	// Порог «тяжёлого» попадания (единицы урона ядра): наезд/толчок камеры, НЧ-слой звука, сильная вибрация.
	constexpr float HEAVY_MAG = 1.1f;
	// Толчок камеры по вектору тяжёлого удара (см при PunchIn = 1), CAM_JOLT веба 0.09 м.
	constexpr float CAM_JOLT_CM = 9.f;
	// Окно повтора нокаута вокруг падения (сек).
	constexpr float REPLAY_BEFORE = 2.0f;
	// S-66: после удара — всё падение (клип нокдауна 1.8 с, таз на настиле с 0.8 с; в slow-mo нокдауна игровое время
	// идёт медленнее) и полсекунды «лежит»: при 1.2 с QA видел в повторе только обмен до падения.
	constexpr float REPLAY_AFTER = 2.2f;

	// Параметры хит-стопа/slow-mo. S36 — текущие веба (после жалобы «подвисание», S-36): стоп-кадр только на
	// по-настоящему тяжёлом (mag ≥ 1.5) и в 1.5–3 кадра (25→45 мс), нокдаун 70 мс; slow-mo — только нокдаун.
	// Classic — исходные S-07: стоп 50→110 мс с mag 1.1 (к mag ≈ 2.3), нокдаун 110 мс, slow-mo ещё и на mag ≥ 1.9.
	struct FProfile
	{
		float FreezeMagMin = 1.5f;
		float StopMinMs = 25.f;
		float StopMaxMs = 45.f;
		float StopSpanMag = 0.8f; // от порога до максимума
		float StopKdMs = 70.f;
		float SlowHeavyMag = 0.f; // 0 — нет slow-mo на обычных попаданиях

		static FProfile S36() { return FProfile(); }
		static FProfile Classic()
		{
			FProfile P;
			P.FreezeMagMin = HEAVY_MAG;
			P.StopMinMs = 50.f;
			P.StopMaxMs = 110.f;
			P.StopSpanMag = 1.2f;
			P.StopKdMs = 110.f;
			P.SlowHeavyMag = 1.9f;
			return P;
		}
	};

	// Сколько мс заморозить картинку (0 — не замораживать).
	float HitStopMs(EKind Kind, float Mag, const FProfile& P);

	// Импульс тряски (0..1) и «наезда» (0..1).
	struct FCamKick
	{
		float Shake = 0.f;
		float PunchIn = 0.f;
	};
	FCamKick CameraKick(EKind Kind, float Mag);

	// Провал времени: Scale в начале, Hold сек на полной глубине, Ease сек возврата к 1 (smoothstep).
	struct FSlowMo
	{
		float Scale = 1.f;
		float Hold = 0.f;
		float Ease = 0.f;
		bool IsValid() const { return Scale < 1.f; }
	};
	constexpr float SLOWMO_KD = 0.3f;
	constexpr float SLOWMO_KD_S = 0.9f;
	FSlowMo SlowMoFor(EKind Kind, float Mag, const FProfile& P);
	float SlowMoScale(const FSlowMo& S, float T);

	// Скорость повтора от времени относительно удара (Rel < 0 — до): подводка 0.5, удар и падение — 0.28.
	float ReplaySpeed(float Rel);

	// Вибрация с точки зрения игрока Me (Who — по кому пришлось событие).
	enum class EHaptic : uint8
	{
		None,
		Light,
		Medium,
		Heavy
	};
	EHaptic HapticFor(EKind Kind, int32 Who, float Mag, int32 Me);

	// Кадр нокдауна (интерактив): Body — центр тела лежащего, Stand — стоящий, RingCenter — центр ринга на полу (мир, см).
	// Камера сбоку от линии «лежащий → стоящий» на KD_SHOT_ANGLE: лежащий крупно в нижней части кадра, стоящий
	// (нейтральный угол) — в стороне от центра кадра, где панель счёта HUD. Side (±1; 0 — выбрать: камера ближе к центру
	// ринга; хранится как угол + 1000) держится весь нокдаун; камера не дальше края апрона.
	constexpr float KD_SHOT_ANGLE_DEG = 45.f;   // стартовый угол камеры к линии «лежащий → стоящий»
	constexpr float KD_SHOT_STAND_DEG = 24.f;   // стоящий — на столько от центра кадра (16:9: пол-кадра ≈ 37°)
	constexpr float KD_SHOT_DIST = 300.f;   // см от лежащего
	constexpr float KD_SHOT_HEIGHT = 185.f; // над полом
	constexpr float KD_SHOT_LIM = 420.f;    // камера не дальше апрона (канаты 305; сторону канатов у камеры прячет контроллер)
	void KnockdownShot(const FVector& Body, const FVector& Stand, const FVector& RingCenter, int32& Side, FVector& OutCam, FVector& OutLook,
		const FVector* PrefCam = nullptr); // S-78: PrefCam — нынешняя камера: сторона ближе к ней (без облёта ринга на полкруга)
	constexpr float KD_SHOT_TURN_COST = 0.25f; // S-78: цена градуса облёта от нынешней камеры
	// Кадр перерыва (restShot + walkToCornerShot веба): Corner 0 — красный (−,−), 1 — синий; At — где сейчас боец (идёт к углу
	// — кадр едет с ним); Aspect — ширина/высота вьюпорта (портрет — дальше, широкий — угол левее центра).
	// S-62: HeightScale — рост бойца / 178: высокий (198 см) целиком в кадре, голова не под панелью HUD (дальше и выше).
	void RestShot(int32 Corner, float Aspect, const FVector& At, const FVector& RingCenter, FVector& OutCam, FVector& OutLook, float HeightScale = 1.f, float Seated = 0.f);
	// S-62: кадр итога (панель итога в центре экрана — 16:9 закрывает ~29…71 % ширины): победитель (с рефери) во весь рост
	// в свободной полосе слева от панели. Winner/Loser — места бойцов, Cam — нынешняя камера (сторона та же), HFovDeg —
	// горизонтальный FOV, HeightScale — рост победителя / 178. Камера внутри апрона.
	constexpr float RESULT_SCREEN_X = 0.15f; // победитель — на этой доле ширины кадра
	void ResultShot(const FVector& Winner, const FVector& Loser, const FVector& Cam, const FVector& RingCenter, float HFovDeg, float HeightScale,
		FVector& OutCam, FVector& OutLook, const FVector& WinnerFwd = FVector::ZeroVector); // WinnerFwd — куда он смотрит: камера спереди-сбоку

	float WrapDeg(float A);

	// S-75: подсказки защиты по событиям ядра (DEF_CUE веба, InteractiveFight.tsx) — с точки зрения игрока Me.
	enum class EDefCue : uint8
	{
		None,
		Slip,       // мой нырок удался: «Уклон! Бей в ответ» — держится контр-окно ядра
		Counter,    // моя контра прошла
		Broke,      // я пробил блок соперника
		GuardBreak, // мой блок пробит
		Caught,     // пойман на выходе из нырка
		Clinch,     // S-76: клинч (пара сцепилась)
		Break,      // S-76: рефери — «Брейк!»
	};
	EDefCue DefenseCueFor(const FFightEvent& E, int32 Me);
	const TCHAR* DefenseCueText(EDefCue C);
	// Сколько держится (с): уклон — контр-окно ядра 0.62 с, прочие — 1 с (setCue веба).
	float DefenseCueSeconds(EDefCue C);
	bool DefenseCueGood(EDefCue C);
	constexpr float COUNTER_WINDOW_S = 0.62f; // COUNTER_WINDOW ядра
	// Контра — акцент: короткий стоп-кадр даже на среднем попадании и наезд камеры.
	constexpr float COUNTER_STOP_MS = 35.f;
	constexpr float COUNTER_PUNCH_IN = 0.35f;
	float CounterStopMs(float StopMs, bool bCounter);

	// S-78 (QA: камера пролетала над сидящим бойцом, ныряла сверху на нокдауне/KO). Смешивание двух кадров камеры «по орбите»
	// вокруг точки взгляда: точки взгляда — линейно, смещение камеры — курс (кратчайший путь), наклон и расстояние — линейно.
	// Прямая интерполяция мест камеры вела её над головой бойца (наклон до −82°, 2300°/с).
	void BlendView(const FVector& CamA, const FVector& LookA, const FVector& CamB, const FVector& LookB, float T, FVector& OutCam, FVector& OutLook);
	// Ограничитель итогового кадра: наклон взгляда в [CAM_PITCH_MIN, CAM_PITCH_MAX], поворот (курс и наклон отдельно — не через
	// «макушку») не быстрее CAM_MAX_TURN_DPS; камера прыгнула дальше CAM_CUT_CM за кадр — склейка, ограничения поворота нет.
	constexpr float CAM_PITCH_MIN_DEG = -45.f;
	constexpr float CAM_PITCH_MAX_DEG = 20.f;
	constexpr float CAM_MAX_TURN_DPS = 160.f;
	constexpr float CAM_CUT_CM = 60.f;
	// Prev — прошлое направление (нулевой — нет); возвращает единичное направление. bCut — склейка (только наклон).
	FVector LimitViewDir(const FVector& Prev, const FVector& Want, float Dt, bool bCut, bool* bOutTurnLimited = nullptr, bool* bOutPitchLimited = nullptr);
}

// S-75: крупная подпись защиты по событию (порт DEF_CUE веба) — сверху по центру, над бойцами, не на HUD-панелях;
// у «Уклон! Бей в ответ» — полоска контр-окна. Собирается в C++ (без WBP), ввод не принимает. Ведёт UBoxingFightFx.
UCLASS()
class BOXINGUE_API UBoxingDefenseCueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Text пусто — скрыть. Alpha 0..1 (появление/угасание), Pop — масштаб «удара» (1 — покой), Window — доля контр-окна (< 0 — без полоски).
	void Show(const FString& Text, const FLinearColor& Color, float Alpha, float Pop, float Window);

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient) TObjectPtr<UBorder> Panel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> WindowBar;
	FString Shown;
};

// Запись одного бойца в кадре повтора.
struct FBoxReplayFighter
{
	FVector Loc = FVector::ZeroVector;
	float Yaw = 0.f;
	TArray<FTransform> Bones; // локальные (bone space) трансформы логического меша
	FBoxerFeelFrame Feel;     // кадр процедурного слоя (реакция, наведение) — для видимого меша
	FVector Pelvis = FVector::ZeroVector; // S-66: таз видимого меша (мир) — кадр повтора держит в кадре и падающее тело
};

UCLASS()
class BOXINGUE_API UBoxingFightFx : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UBoxingFightFx* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;

	// ---------- от GameMode ----------
	// Событие ядра (до раздачи бойцам). AttLoc/DefLoc — места атакующего/защищающегося (мир).
	void OnFightEvent(const FFightEvent& E, ABoxingFightGameMode* GM);
	// Снимок ядра после шагов кадра: гонг, счёт, итог.
	void OnSnapshot(const FFightSnapshot& Snap, ABoxingFightGameMode* GM);
	// Множитель времени ядра: 0 во время хит-стопа и повтора (картинку и ядро держим вместе).
	float CoreTimeScale() const;

	// ---------- от PlayerController ----------
	// Тряска/наезд/толчок поверх камеры боя; во время повтора — своя камера (true — кадр задан повтором).
	bool ModifyCamera(FVector& Cam, FVector& Look, float& HFovDeg);
	// S-71 (отладка/скриншоты угловых, ABoxingCornerCrew -BoxCrewShots): камера в заданную точку на эти кадры.
	bool bShotCam = false;
	FVector ShotCam = FVector::ZeroVector;
	FVector ShotLook = FVector::ZeroVector;

	// ---------- чтение (HUD/BP) ----------
	UFUNCTION(BlueprintPure, Category = "Boxing|Fx")
	bool IsReplaying() const { return Replay.bPlaying; }
	UFUNCTION(BlueprintPure, Category = "Boxing|Fx")
	bool IsHitStop() const { return Freeze > 0.f; }
	UFUNCTION(BlueprintPure, Category = "Boxing|Fx")
	bool IsMuted() const;
	UFUNCTION(BlueprintCallable, Category = "Boxing|Fx")
	void SetMuted(bool bMute);
	// Пропустить повтор (тап/клавиша).
	UFUNCTION(BlueprintCallable, Category = "Boxing|Fx")
	void SkipReplay();

	UPROPERTY(Transient)
	TObjectPtr<UBoxingFightAudio> Audio;
	// S-75: подпись защиты (создаётся при первой подсказке).
	UPROPERTY(Transient)
	TObjectPtr<UBoxingDefenseCueWidget> CueWidget;

	// S-75: текущая подсказка защиты (для HUD/отладки): вид и сколько осталось (с).
	BoxFx::EDefCue GetDefenseCue(float& OutLeft) const { OutLeft = CueLeft; return CueLeft > 0.f ? Cue : BoxFx::EDefCue::None; }

	bool bEnabled = true;
	BoxFx::FProfile Profile;

private:
	// S-74: брызги пота в точке контакта (ImpactSpray.h; реализация SpawnSpray — ImpactSpray.cpp).
	FBoxImpactSpray Spray;
	void SpawnSpray(const FFightEvent& E, ABoxingFightGameMode* GM, BoxSpray::EKind SprayKind, float Mag);
	void SetFightersFrozen(bool bFrozen);
	void ApplyHaptic(BoxFx::EHaptic H);
	void UpdateTimeDilation();
	void UpdateShots(float RealDt);
	void RecordFrame(float RealDt);
	void UpdateReplay(float RealDt);
	void BeginReplay();
	void EndReplay();
	void ApplyReplayFrame(float T);

	TWeakObjectPtr<ABoxingFightGameMode> Mode;
	bool bLog = false;
	bool bNoReplay = false;
	bool bReplayTest = false; // -BoxReplayTest: повтор после каждого нокдауна (проверка без KO)
	int32 ReplayShotsLeft = 0;
	int32 FxShotsLeft = 0; // -BoxFxShots=N: серии скриншотов на первых N хит-стопах/нокдаунах
	int32 FxShotIndex = 0;
	TArray<TPair<double, FString>> PendingShots;
	// S-75 (отладка): -BoxDefShots=N — серии кадров защиты: N на вид (блок, начало нырка, провал, контра, пробит/пойман)
	// → Docs/screens/<-BoxShotPrefix, иначе feel6_def>_<вид>_NN_tNNN.png.
	int32 DefShotsLeft[7] = {0, 0, 0, 0, 0, 0, 0};
	int32 DefShotIndex[7] = {0, 0, 0, 0, 0, 0, 0};
	FString DefShotPrefix;
	float PrevSlipAmt[2] = {0.f, 0.f};
	void QueueDefShots(int32 Slot, const TCHAR* Kind, std::initializer_list<float> Offsets);
	// S-75: подсказка защиты (реальное время).
	BoxFx::EDefCue Cue = BoxFx::EDefCue::None;
	float CueLeft = 0.f;
	float CueAge = 0.f;
	float CueDur = 0.f;
	bool bNoCue = false; // -BoxNoDefCue
	void UpdateCue(float RealDt);
	// S-75 (замер, -BoxFxLog): после нырка/блока 0.3 с — мин. расстояние фронта кулака атакующего до центра головы и до
	// ближайшей перчатки защищающегося (видимый меш). «FX DEF СВОДКА» при выходе.
	struct FDefProbe
	{
		int32 Att = 0;
		bool bSlip = false;
		float Left = 0.f;
		float HeadMin = 1e6f;
		float GloveMin = 1e6f;
	};
	TArray<FDefProbe> DefProbes;
	void UpdateDefProbes(float RealDt);
	// S-78: ограничитель кадра и автопроверка камеры («FX CAM СВОДКА» в логе при выходе; -BoxFxLog — эпизоды).
	FVector CamPrevPos = FVector::ZeroVector;
	FVector CamPrevDir = FVector::ZeroVector;
	double CamPrevClock = -1.0;
	struct FCamStats
	{
		int32 Frames = 0;
		int32 Cuts = 0;
		int32 TurnLimited = 0;
		int32 PitchLimited = 0;
		float WantTurnMax = 0.f;
		float WantPitchMin = 0.f;
		int32 FastFinal = 0;
		int32 SteepFinal = 0;
		float FinalTurnMax = 0.f;
		float FinalPitchMin = 0.f;
	} CamStats;
	void LimitCamera(FVector& Cam, FVector& Look);

	// хит-стоп
	float Freeze = 0.f; // сек реального времени
	bool bFightersFrozen = false;
	// slow-mo
	BoxFx::FSlowMo Slow;
	float SlowT = 0.f;
	float AppliedDilation = 1.f;
	// камера
	float Shake = 0.f;
	float PunchIn = 0.f;
	FVector Jolt = FVector::ZeroVector;
	// кадры нокдауна и перерыва (смешиваются поверх камеры боя)
	float DownMix = 0.f;
	float RestMix = 0.f;
	int32 KdSide = 0;
	bool bKdBodyKnown = false; // S-62: угол выбран уже по раскладке лежащего (а не по стоящему)
	FVector KdCam = FVector::ZeroVector, KdLook = FVector::ZeroVector;
	FVector RestCam = FVector::ZeroVector, RestLook = FVector::ZeroVector;
	// S-62: кадр итога (панель итога открыта) — победитель во весь рост слева от панели.
	float ResultMix = 0.f;
	FVector ResultCam = FVector::ZeroVector, ResultLook = FVector::ZeroVector;
	double Clock = 0.0; // реальное время (с)
	// вибрация
	double LastHapticAt = -10.0;
	// постановка/гонг
	EFightPhase PrevPhase = EFightPhase::Between;
	int32 PrevRound = 0;
	ERingStageKind PrevStage = ERingStageKind::None;
	double RecT = 0.0; // время записи повтора (игровое, без кадров хит-стопа)
	double LastRecT = -1.0;
	bool bFirstSnap = true;
	TArray<FTransform> ReplayScratch[2];
	// S-66: кадр повтора — сглаженный центр/разлёт; отладка пропуска повтора «клавишей» (-BoxReplaySkipAt=С).
	// S-66: табло арены (TextRender уровня «РАУНД 1   3:00») — живые раунд и время, а не статика.
	void UpdateScoreboard(const FFightSnapshot& Snap);
	bool bBoardsFound = false;
	TArray<TWeakObjectPtr<class ATextRenderActor>> Boards;
	FString BoardText;
	bool bReplayCamInit = false;
	FVector ReplayMid = FVector::ZeroVector;
	float ReplaySpan = 0.f;
	float ReplaySkipAt = -1.f;
	float ReplayPlayedReal = 0.f;
	int32 SkipKeyStage = 0;
	uint64 KdFrame = 0;

	// ---------- повтор нокаута ----------
	struct FRecFrame
	{
		double T = 0.0; // время записи (игровое: с замедлением, без кадров хит-стопа)
		FBoxReplayFighter F[2];
	};
	struct FReplay
	{
		TArray<FRecFrame> Ring; // кольцевой буфер
		int32 Head = 0;
		int32 Num = 0;
		double KdAt = -1.0;  // время записи последнего нокдауна (момент удара); −1 — отрезок уже снят
		TArray<FRecFrame> Clip; // зафиксированный отрезок вокруг нокдауна
		bool bClipReady = false;
		bool bWanted = false;      // досрочка — повтор нужен
		double StartAt = 0.0;      // когда начать (реальное время)
		bool bPlaying = false;
		float Cursor = 0.f;        // время внутри клипа (сек записи от начала)
		bool bImpactFired = false;
		float Orbit = 0.f;
		int32 ShotIndex = 0;
		float ShotTimer = 0.f;
		double ClipKd = 0.0;        // время нокдауна в клипе
		int32 CurIdx = 0;
	} Replay;
};
