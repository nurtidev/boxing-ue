// Постановка ступней (S-70, порт web/src/ui/three/footPlant.ts, S-10/S-37): ступни СТОЯТ на настиле в мире, пока
// корпус двигается, а шаг — перенос одной ступни по низкой дуге в новую точку стойки. Motion Matching GASP сам опору
// не фиксирует (ступни скользили 70–150 см/с в опоре, Docs/FIGHT_FEEL.md S-66) — теперь ноги видимого меша ведёт
// этот планировщик + двухзвенная IK (FBoxerFootIk в BoxerFeel.h):
//  - опорная ступня зафиксирована в мире; шаг — когда корпус увёл её точку стойки дальше порога (или нога на пределе);
//  - боксёрский порядок: первой идёт ступня по ходу (вперёд — передняя, назад — задняя, вбок — со стороны шага),
//    вторая «подтягивается» волоком (носок по настилу, пятка поднята) — ровно две ступни на шаг, без «дроби»;
//  - разворот: малый — пивот на подушечке (носок на месте), большой — переставляя;
//  - ходьба постановки — попеременными шагами; шаг движка — ведущая ступня сразу в конец шага (TrackStep);
//  - выпад удара/отдача реакции — порог поглощения коленом/пяткой, сверх — шаг в пик (HoldLunge) и скольжение.
//
// Чистая геометрия на числах: метры, плоскость пола UE (X, Y), курс — радианы от +X к +Y (вперёд = (cos, sin)).
// Это зеркало соглашения веба (x, z = −Y): формулы и константы веба 1:1. Визуал: ГСЧ ядра не трогает.
#pragma once

#include "CoreMinimal.h"

namespace BoxFoot
{
	struct FV2
	{
		float X = 0.f;
		float Y = 0.f;
		FV2() = default;
		FV2(float InX, float InY) : X(InX), Y(InY) {}
		FV2 operator+(const FV2& O) const { return FV2(X + O.X, Y + O.Y); }
		FV2 operator-(const FV2& O) const { return FV2(X - O.X, Y - O.Y); }
		FV2 operator*(float K) const { return FV2(X * K, Y * K); }
		float Len() const { return FMath::Sqrt(X * X + Y * Y); }
	};

	struct FGoal
	{
		float X = 0.f; // щиколотка (мир, м)
		float Y = 0.f;
		float Yaw = 0.f; // курс ступни
	};

	struct FSwing
	{
		float FX = 0.f, FY = 0.f, FYaw = 0.f; // откуда
		float T = 0.f;
		float Dur = 0.f;
		float Lift = 0.f;
		bool bDrag = false; // подтяг второй ступни волоком: носок по настилу, пятка поднята (не шаг-отрыв)
		float Len0 = 0.f;   // длина переноса на старте: слежение за точкой не укорачивает его до «шажка»
	};

	struct FFoot
	{
		float X = 0.f, Y = 0.f, Yaw = 0.f; // где стоит щиколотка (при переносе — куда летит)
		bool bSwing = false;
		FSwing Sw;
		float Pivot = 0.f; // 0..1 — насколько ступня сейчас «на носке» (пивот на подушечке)
	};

	struct FGait
	{
		FFoot Feet[2]; // 0 — передняя, 1 — задняя
		bool bReady = false;
		int32 Follow = -1; // ступня, что должна «подтянуться» за только что шагнувшей
		int32 Last = -1;   // кто шагал последним (ходьба — строго попеременно)
		bool bPrev = false;
		FGoal Prev[2];
		FV2 GV[2];             // скорость точки стойки каждой ступни (м/с) — упреждение по ней
		float SinceLand = 9.f; // с, как давно какая-то ступня встала
		int32 Pair = -1;       // ступня, что «толкнулась» (шаг-приставка): следующий шаг другой — подтяг
		float WY = 0.f;        // рад/с — скорость разворота стойки (сглаженная)
		FV2 Slid[2];           // приложенное скольжение ступней (см. FGaitOpts::Slide)
		int32 Swings = 0;      // отладка: всего переносов (для «переносов в секунду»)
		int32 Drags = 0;       // из них — подтягов волоком
		int32 Why[6] = {0, 0, 0, 0, 0, 0}; // отладка: причина переноса — нога на пределе, подтяг, порог, разворот, шаг ядра, ходьба
	};

	struct FGaitOpts
	{
		float Dt = 0.f;
		float VX = 0.f, VY = 0.f; // скорость корпуса (мир, м/с) — для порядка ног
		bool bWalking = false;    // ходьба постановки (попеременный шаг)
		float Stretch[2] = {0.f, 0.f}; // насколько нога вытянута к своей ступне (1 — предел)
		float ToeLen = 0.12f;     // щиколотка → подушечка (м)
		float Lead = 0.f;         // упреждение (с)
		float FollowTh = -1.f;    // порог «подтягивания» второй ступни (< 0 — FOLLOW_TH)
		bool bAhead = false;      // ритм шагов движка: точки стойки сдвинуты на остаток шага корпуса
		FV2 Ahead;
		FV2 Lunge[2];             // выпад: сдвиг точки стойки для ШАГА (мир)
		FV2 Slide[2];             // выпад: куда стоящую ступню довезти скольжением (мир)
	};

	// Пороги и константы (веб 1:1).
	constexpr float STEP_TH = 0.085f;
	constexpr float STEP_TH_MOVING = 0.045f;
	constexpr float STEP_TH_STEP = 0.07f;
	constexpr float FOLLOW_TH = 0.022f;
	constexpr float DRAG_LIFT = 0.12f;
	constexpr float SNAP_DIST = 1.4f;

	// --- выпад удара и отдача реакции (S-37 r2) --- оси бойца: X — к сопернику, Y — вбок.
	struct FAbsorb
	{
		float Fwd, Back, Side;
	};
	constexpr FAbsorb LEAD_ABSORB = {0.13f, 0.18f, 0.1f};
	constexpr FAbsorb REAR_ABSORB = {0.2f, 0.12f, 0.1f};
	constexpr float SLIDE_MAX = 0.22f;
	constexpr float SLIDE_V = 2.2f;
	constexpr float LUNGE_RELEASE = 0.5f;

	// Часть выпада, которую ноге не взять коленом/пяткой (м, оси бойца), на рост Rs.
	FV2 OverAbsorb(float X, float Y, const FAbsorb& A, float Rs = 1.f);
	// Выпад ступни I → шаг и скольжение: передняя (0) скользит назад, задняя (1) — вперёд.
	void SplitLunge(int32 I, float X, float Y, float Rs, FV2& OutStep, FV2& OutSlide);

	struct FLungeHold
	{
		float X = 0.f, Y = 0.f;
		float Pk = 0.f; // пик текущего выпада (м); 0 — выпада нет
	};
	// Удержание шага выпада: растёт — сразу к новому пику; спадает — держится до LUNGE_RELEASE пика, потом в 0.
	// bLock — ступня в переносе за этим выпадом: не отпускать посреди шага.
	void HoldLunge(FLungeHold& H, const FV2& Want, bool bLock = false);

	// --- предсказание шага движка (S-37) ---
	// У ядра UE фазы шага в снимке нет (только вид, FFighterState::Step) — как 3D-реплей веба: длительность шага
	// учится на лету по виду, остаток = скорость × оставшееся время (дуга — по повороту скорости).
	constexpr float STEP_DUR_GUESS = 0.24f;
	constexpr float STEP_RHYTHM_GAP = 0.3f;
	struct FStepTrack
	{
		bool bOn = false;
		float T = 0.f;
		float Since = 9.f;
		float Dur = STEP_DUR_GUESS;
		int32 Kind = 0;
		float Durs[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // выученные длительности по виду шага (0 — нет)
		float AnyDur = 0.f;                       // общий (вид неизвестен)
		float VX = 0.f, VY = 0.f;
		float W = 0.f; // рад/с — поворот направления скорости
	};
	bool InStepRhythm(const FStepTrack& Tr);
	// Step — вид идущего шага ядра (0 — не идёт, 1..7 — EStepKind); D — перемещение корпуса за кадр (м);
	// → сколько корпусу ещё ехать до конца шага (м). Кадры с Dt = 0 прогноз не меняют.
	FV2 TrackStep(FStepTrack& Tr, int32 Step, float DX, float DY, float Dt);

	float WrapAngle(float A);
	// Длительность и высота переноса ступни на Dist (м): бой — короткие низкие шаги; ходьба — длиннее и выше.
	void SwingShape(float Dist, bool bWalking, float& OutDur, float& OutLift);

	struct FFootNow
	{
		float X = 0.f, Y = 0.f, Lift = 0.f, Yaw = 0.f;
		float U = -1.f; // фаза переноса (−1 — стоит)
		bool bDrag = false;
	};
	FFootNow FootNow(const FFoot& F);

	// Кто шагает первым: в движении — ступня, чья точка стойки дальше по ходу, стоя — та, что дальше от своей точки.
	int32 LeaderOf(const FGoal Goals[2], float VX, float VY, const float Errs[2]);

	// Шаг планировщика на кадр: Raw — точки стойки.
	void UpdateGait(FGait& G, const FGoal Raw[2], const FGaitOpts& O);
}
