// Рефери в ринге (S-58): чистая логика без UE-акторов — порт web/src/ui/three/refereeBrain.ts 1:1
// (тест BoxingUE.Referee — зеркало web/test/referee*.test.ts). Ест только то, что уже есть у сцены:
// места бойцов, камеру и стадию боя (снимок ядра); ГСЧ не трогает, ядро не меняет. Позу костей по
// кадру FRefFrame строит ABoxingReferee (FightReferee.h).
//
// Поведение:
//  - бой — сбоку от пары на 1.2–2 м, с дальней от камеры стороны (не заслоняет бой), идёт
//    плавно, без рывков (гистерезис цели), в обход бойцов и «просвета» между ними; цель за парой —
//    через торец пары (PairDetour), а не в лоб;
//  - выход из углов — в центре, уходит в сторону, когда бойцы сходятся;
//  - перерыв — в нейтральном углу (дальнем от камеры), камеру перерыва обходит (CAM_CLEAR);
//  - нокдаун — у сбитого со стороны нейтрального угла соперника, но ВНЕ пути стоящего туда
//    (DownSpot): рукой отправляет его в угол и считает — рука поднимается и опускается в такт счёту
//    ядра; путь идущего впереди него — стена, сам идущий (вне боя) — препятствие с упреждением;
//  - досрочка — сперва видит падение (STOP_DELAY), потом разводит руками; решение — встаёт между
//    бойцами, берёт обоих за запястья и поднимает руку победителю (ничья — обе руки).
//
// Координаты — как у ядра и веба: метры в плоскости (X, Z), центр ринга (0, 0); курс yaw — соглашение
// TS (0 — лицом вдоль +X, atan2(−dz, dx)); в UE: X_ue = X·100, Y_ue = Z·100, курс UE (град) = −yaw.
// Руки — в осях модели рефери как в вебе: X — влево от него, Y — вверх, Z — вперёд (единичные векторы).
#pragma once

#include "CoreMinimal.h"

struct FFightSnapshot;
struct FFightResult;

namespace BoxRef
{
	struct FV
	{
		double X = 0;
		double Z = 0;
		FV() = default;
		FV(double InX, double InZ) : X(InX), Z(InZ) {}
	};

	// Вектор в осях модели рефери: X — влево, Y — вверх, Z — вперёд.
	struct FV3
	{
		double X = 0;
		double Y = 0;
		double Z = 0;
		FV3() = default;
		FV3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
	};

	enum class EPhase : uint8
	{
		Out,
		Fight,
		Rest,
		Down,
		Resume,
		Over
	};

	struct FDown
	{
		bool bValid = false;
		int32 Who = 0;
		int32 Count = 0;           // счёт рефери (1..10) — каждый новый — взмах руки
		bool bHasNeutral = false;  // куда идёт стоящий (нейтральный угол)
		FV Neutral;
		TArray<FV> Path;           // его путь туда (точки после места на нокдауне; пусто — прямо в Neutral)
		bool bArrived = false;     // стоящий уже в нейтральном углу
	};

	struct FOver
	{
		bool bValid = false;
		int32 Winner = -1;         // −1 — ничья
		bool bStoppage = false;
		bool bStanding = false;    // остановлен стоящий (RSC без нокаута): развёл руками, потом объявляет
	};

	struct FInput
	{
		EPhase Phase = EPhase::Fight;
		FV Fighters[2];            // места бойцов (центры, как их рисует сцена)
		FV Camera;                 // камера на плане
		// UE (S-58): камера БОЯ (без кадра перерыва) — по ней выбирается нейтральный угол перерыва, чтобы следующий
		// раунд рефери начинал с дальней от камеры боя стороны. Нет — Camera (как в вебе).
		bool bHasFightCam = false;
		// UE (S-58): рост бойцов / 1.78 — обход тел (PASS_R) по габариту (тяж с мухачом разные), 1 — как в вебе.
		double FighterScale[2] = {1, 1};
		FV FightCam;
		// UE (S-62): где ляжет тело сбитого (голова и таз итоговой позы падения, с доворотом от канатов) — известно с
		// первого кадра нокдауна. Нет — оценка веба LyingBody (0.6 м за точкой падения).
		bool bHasLying = false;
		FV LyingHead;
		FV LyingPelvis;
		// UE (S-66): тело целиком — стопы итоговой позы (капсула «стопы → таз → голова») и конечности (кисти, колени).
		// Без них в KO рефери стоял ногой между бёдрами: круги обхода головы/таза и точки падения оставляли «шов»
		// ровно над бёдрами.
		bool bHasLyingFeet = false;
		FV LyingFeet;
		TArray<FV> LyingLimbs;
		FDown Down;
		FOver Over;
		// UE (S-76/S-75): клинч ядра — подходит к паре сбоку (с дальней от камеры стороны); bBreak — «Брейк!»: руки между
		// бойцами и в стороны (разводит).
		bool bClinch = false;
		bool bBreak = false;
	};

	struct FArm
	{
		FV3 Upper; // плечо → локоть
		FV3 Fore;  // локоть → кисть
	};

	// Кисть: Relax — пальцы чуть согнуты, Open — раскрыта, Point — указательный (в угол), 1..5 — счёт пальцами.
	enum class EHand : uint8
	{
		Relax = 0,
		One = 1,
		Two = 2,
		Three = 3,
		Four = 4,
		Five = 5,
		Open = 10,
		Point = 11
	};

	struct FRefFrame
	{
		double X = 0;
		double Z = 0;
		double Yaw = 0;     // 0 — лицом вдоль +X
		double Walk = 0;    // 0..1 — насколько идёт
		double Phase = 0;   // фаза шага, рад
		double MoveLat = 0; // направление хода в осях рефери (влево)
		double MoveFwd = 1; // … (вперёд)
		double Lean = 0;    // наклон корпуса вперёд, рад
		FArm Arms[2];       // [левая, правая]
		EHand Hands[2] = {EHand::Relax, EHand::Relax};
		double Ready = 0;   // 0..1 — «боевая» стойка в бою
		double Raised = 0;  // сек, как рука победителя поднята
		// UE: доля жеста в руке (0 — руки локомоции GASP, 1 — поза логики). Свободная рука вне боя — 0,
		// в бою — Ready (руки перед собой), жест (счёт, «в угол», развод, объявление) — 1. Сглажено.
		double ArmW[2] = {0, 0};
	};

	// --- геометрия (те же числа, что в вебе) ---
	constexpr double ROPE_HALF = 3.05;
	constexpr double REF_RING = ROPE_HALF - 0.4;
	constexpr double SIDE_MIN = 1.2;
	constexpr double SIDE_MAX = 2.0;
	constexpr double FIGHTER_CLEAR = 0.95;
	constexpr double ROUTE_CLEAR = 1.0;
	constexpr double MAX_SPEED = 1.9;
	constexpr double PASS_R = 0.82;
	constexpr double DETOUR_R = PASS_R + 0.06;
	constexpr double CAM_CLEAR = 1.8;
	constexpr double STOP_DELAY = 0.6;
	constexpr double WAVE_TIME = 2.0;

	BOXINGUE_API double Hyp(const FV& A, const FV& B);
	BOXINGUE_API FV ClosestOnSeg(const FV& A, const FV& B, const FV& P);
	BOXINGUE_API FV FarPerp(const FV& F0, const FV& F1, const FV& Cam);
	BOXINGUE_API bool BlocksView(const FV& P, const FV& Cam, const FV& F);
	BOXINGUE_API double SideCost(const FV& P, const FV& F0, const FV& F1, const FV& Cam, const FV* Current);
	BOXINGUE_API FV SidePlacement(const FV& F0, const FV& F1, const FV& Cam, const FV* Current);

	struct FObstacle
	{
		FV P;
		double R = 0;
		bool bSoft = false;
	};
	// Шаг к цели (разгон/торможение, обход, выталкивание, канаты); мутирует Pos/Vel/Side.
	BOXINGUE_API void MoveToward(FV& Pos, FV& Vel, const FV& Target, const TArray<FObstacle>& Obs, double Dt, int32& Side);
	BOXINGUE_API double SegDist(const FV& A, const FV& B, const FV& C, const FV& D);
	// Обход пары через торец: true — идти к OutP (торец OutEnd), false — прямо. Prefer: −1 — нет.
	BOXINGUE_API bool PairDetour(const FV& Pos, const FV& Goal, const FV& F0, const FV& F1, int32 Prefer, FV& OutP, int32& OutEnd);
	BOXINGUE_API FV RestSpot(const FV& Cam);
	BOXINGUE_API FV CountSpot(const FV& Down, const FV& Toward, const FV& Cam);
	BOXINGUE_API double RouteDist(const TArray<FV>& Route, const FV& P);
	BOXINGUE_API TArray<FV> RouteAhead(const TArray<FV>& Route, const FV& P);
	// Head (UE, S-62) — голова лежащего: рефери держится и от неё (тело длиннее оценки веба).
	BOXINGUE_API FV DownSpot(const FV& Down, const FV& Body, const FV& Toward, const FV& Cam, const TArray<FV>* Route, const FV* Stand, const FV* Current, const FV* Head = nullptr,
		const FInput* Lying = nullptr, // S-66: тело лежащего целиком (LyingDist) — место счёта не на нём
		const FV* From = nullptr);     // S-66: где рефери сейчас — место за телом дороже на длину обхода
	BOXINGUE_API FV AnnounceSpot(const FV& F0, const FV& F1, const FV& Cam);
	BOXINGUE_API FArm WristArm(double S, const FV& Ref, double Yaw, const FV& F, double H, bool bHigh);
	BOXINGUE_API double CountLift(double Age, double Beat);
	// Направление мира (dx, dz) → оси рефери: Lat — влево, Fwd — вперёд.
	BOXINGUE_API void ToLocal(double Dx, double Dz, double Yaw, double& OutLat, double& OutFwd);
	BOXINGUE_API FArm ArmIdle(double S);
	BOXINGUE_API FArm ArmReady(double S);
	// Центр тела лежащего (порт lyingBody: 0.6 м за точкой падения, от стоящего).
	BOXINGUE_API FV LyingBody(const FV& Down, const FV& Stand);

	// S-66: тело лежащего для обхода — капсула «стопы → таз → голова» (радиус LYING_R) и конечности (LIMB_R).
	constexpr double LYING_R = 0.5;     // от оси тела до оси рефери: полтела ~0.2 + рефери ~0.22 + запас
	constexpr double LIMB_R = 0.32;     // кисть/колено → ось рефери
	constexpr double LYING_CLEAR = 0.72; // место счёта — не ближе к оси тела
	// Расстояние от точки до оси тела лежащего (min по отрезкам «стопы → таз → голова») и до конечностей за вычетом
	// разницы радиусов (LYING_R − LIMB_R) — т. е. «эквивалент» дистанции до оси. Тела нет (bHasLying) — 1e9.
	BOXINGUE_API double LyingDist(const FInput& In, const FV& P);
	// S-66: путь Pos → Goal через тело лежащего (они по разные стороны оси и прямая проходит у тела) — обход через
	// торец тела (за стопами или за головой, OutEnd 0/1; Prefer — прежний, −1 — нет). true — идти к OutP; false — прямо.
	// Торец у канатов, где не пройти, не выбирается; нет ни одного — false (идёт прямо, обход препятствиями).
	BOXINGUE_API bool LyingDetour(const FInput& In, const FV& Pos, const FV& Goal, int32 Prefer, FV& OutP, int32& OutEnd);

	// Вход из снимка ядра (порт interactiveRefInput). Result — итог боя (Phase == Over), At — места бойцов,
	// Cam — камера на плане, bStanding — досрочка, а проигравший на ногах.
	BOXINGUE_API FInput InputFromSnapshot(const FFightSnapshot& S, const FFightResult* Result, const FV At[2], const FV& Cam, bool bStanding);

	// Рефери: состояние между кадрами + шаг логики. Детерминирован (одни входы → те же кадры).
	class BOXINGUE_API FBrain
	{
	public:
		FV Pos;
		FV Vel;
		double Yaw = UE_DOUBLE_PI / 2;

		FRefFrame Update(const FInput& In, double Dt);
		// Поставить сразу на место (без хода).
		void Place(const FV& P, double InYaw);
		// Нынешняя цель хода (тесты: «стоит вдали от своей цели» = застрял); false — цели нет.
		bool GetTarget(FV& Out) const
		{
			Out = Goal;
			return bHasGoal;
		}

	private:
		enum class EMode : uint8
		{
			Center,
			Side,
			Corner,
			Count,
			Stop,
			Announce,
			Clinch // S-76/S-75: подходит к сцепке, «Брейк!» — разводит
		};
		double BreakT = -1; // с с команды «Брейк!» (−1 — нет)
		int32 ClinchSide = 0; // сторона от оси пары (знак), выбирается на входе в клинч
		struct FGest
		{
			FArm Arms[2];
			double Rates[2];
			bool Free[2];
			double Lean = 0;
			EHand Hands[2];
		};

		EMode PickMode(const FInput& In);
		FGest Gestures(const FInput& In, double Dt);

		bool bHasGoal = false;
		FV Goal;
		EMode Mode = EMode::Center;
		double ModeT = 0;
		int32 Side = 1;
		double StepPhase = 0;
		double WalkK = 0;
		FArm ArmsNow[2] = {ArmIdle(1), ArmIdle(-1)};
		double ReadyK = 0;
		int32 LastCount = 0;
		int32 DetourEnd = -1;
		int32 LyingEnd = -1; // S-66: обход лежащего через торец (0 — стопы, 1 — голова)
		double RaisedT = 0;
		double AnnounceT = -1;
		bool bStopDone = false;
		double CountSide = -1;
		double BeatAge = 99;
		double Beat = 0.75;
		bool bHasDownAt = false;
		FV DownAt;
		FV StandAt;
		bool bHasToward = false;
		FV TowardAt;
		bool bHasRoute = false;
		TArray<FV> Route;
		bool bHasPrevF = false;
		FV PrevF[2];
		FV FVel[2];
		bool bInit = false;
		double ArmWNow[2] = {0, 0};
	};
}
