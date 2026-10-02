// Типы ядра интерактивного боя (порт web/src/engine/interactive/types.ts).
// Plain C++ поверх CoreMinimal: без UObject/USTRUCT, чтобы ядро собиралось и тестировалось
// отдельно от Engine. UE-слой (Pawn/AnimBP) при необходимости оборачивает их в USTRUCT'ы.
//
// Система координат ядра — как в TS-движке (three.js, Y вверх): ринг в плоскости (X, Z),
// метры, центр ринга — (0, 0). В UE: X_ue = X * 100, Y_ue = Z * 100 (см), Z_ue — высота.
// Это перестановка двух осей (правая тройка three.js → левая UE), геометрия не зеркалится.
// Поле FFighterState::YawDegUE уже пересчитано в соглашение UE (градусы, от +X к +Y).
#pragma once

#include "CoreMinimal.h"

// Стиль бойца (web/src/engine/types.ts Style). Порядок значений — индекс в таблицах стиля.
enum class EBoxStyle : uint8
{
	Technical,
	Volume,
	Puncher,
	Pressure,
	Counter,
	Speed,
	Balanced,
};

// Публичный тип удара: тип + рука. Ядро считает стойку ОРТОДОКСАЛЬНОЙ: левая = передняя
// (lead), правая = дальняя (rear). Джеб всегда lead, кросс всегда rear (как armFor в TS).
enum class EPunchType : uint8
{
	Jab,
	Cross,
	HookL,  // хук передней (левой) рукой
	HookR,  // хук дальней (правой) рукой
	UpperL, // апперкот передней (левой)
	UpperR, // апперкот дальней (правой)
};

// Внутренний вид удара (TS PunchType): индекс в таблицах punches.ts.
enum class EPunchKind : uint8
{
	Jab,
	Cross,
	Hook,
	Uppercut,
};

enum class EPunchArm : uint8
{
	Lead,
	Rear,
};

enum class EPunchTarget : uint8
{
	Head,
	Body,
};

// Действия ввода (слой input/actions.ts → applyAction). Удары берут цель из аргумента
// ApplyAction (Head по умолчанию; Shift+удар в вебе = Body).
enum class EFightAction : uint8
{
	Jab,
	Cross,
	HookL,
	HookR,
	UpperL,
	UpperR,
	BlockStart,
	BlockEnd,
	SlipLeft,
	SlipRight,
	StepFwd,
	StepBack,
	StepLeft,  // дуга влево от лица бойца (дистанция сохраняется)
	StepRight,
	PivotL,    // длинная быстрая дуга, кулдаун 0.6 с
	PivotR,
	RiseTap,   // тап подъёма во время собственного нокдауна
	Proceed,   // перерыв окончен — следующий раунд (если не включён авто-переход)
};

// Текущее перемещение бойца (TS StepKind | null).
enum class EStepKind : uint8
{
	None,
	Fwd,
	Back,
	Left,
	Right,
	PivotL,
	PivotR,
};

// Фаза боя (TS Phase). Walkout — в конце, чтобы не сдвинуть значения прежних (UENUM-зеркало EBoxFightPhase).
enum class EFightPhase : uint8
{
	Fighting,
	Down,    // нокдаун, идёт счёт (с углами стоящий уходит в нейтральный угол — стадия Neutral)
	Between, // перерыв между раундами (с углами по гонгу оба идут в свои углы — стадия Rest)
	Over,
	Walkout, // по гонгу (или после счёта) бойцы идут к бою: удары/ноги/защита недоступны, часы раунда стоят
};

// Стадия постановки раунда (corners.ts StageKind). None — постановки нет (идёт бой / бой окончен).
enum class ERingStageKind : uint8
{
	None,
	Out,     // выход из своих углов к точке встречи по гонгу (Phase == Walkout)
	Rest,    // гонг конца раунда: оба в свои углы (Phase == Between)
	Neutral, // нокдаун: стоящий — в нейтральный угол, лежащий на месте (Phase == Down)
	Resume,  // встал: стоящий из нейтрального угла — назад на дистанцию падения (Phase == Walkout)
};

// Снимок постановки (RingStage веба). Цели — метры ядра; у бойца без цели bHasTarget = false и bArrived = true.
struct FRingStage
{
	ERingStageKind Kind = ERingStageKind::None;
	float T = 0.f;                    // сек с начала стадии (идёт и тогда, когда боевое время стоит)
	bool bHasTarget[2] = {false, false};
	float TargetX[2] = {0.f, 0.f};
	float TargetZ[2] = {0.f, 0.f};
	bool bArrived[2] = {true, true};
};

enum class EFightEventKind : uint8
{
	Hit,       // чистое попадание (TS "land")
	Blocked,   // удар принят в блок (TS "block"); Magnitude — урон, просочившийся сквозь блок
	Slipped,   // защищающийся нырнул — атакующий «провалился» (TS "miss" + slipped)
	Miss,      // промах (вне дистанции или мимо по броску)
	Knockdown, // TS "kd"
	Gassed,    // человек пытался ударить на пустом баке — удар не вышел
	RoundEnd,
	FightEnd,
};

enum class EFightMethod : uint8
{
	None,
	Decision,
	KO,
	RSC,
	Draw,
};

// Формулировка решения (TS FightResult.decision — там строкой по-русски).
enum class EDecisionKind : uint8
{
	None,         // досрочка
	Unanimous,    // единогласное
	Majority,     // большинством
	Split,        // раздельное
	TieBreak,     // любители: по доп. показателям (суммы карт / попадания)
	DrawUnanimous,
	DrawMajority,
	DrawSplit,
};

// 7 боевых статов 0..100 — как Stats в web/src/engine/types.ts.
struct FBoxerStats
{
	float Power = 70.f;
	float HandSpeed = 70.f;
	float Footwork = 70.f;
	float Stamina = 70.f;
	float Chin = 70.f;
	float Technique = 70.f;
	float Defense = 70.f;
};

// Входные данные бойца — упрощённый FightProfile (boxer.ts). Проекцию веса (fightProfile) ядро не делает:
// её считает экспорт ростера (Roster.json) или вызывающий. Массы для урона — MassForPower/DurabilityMass
// (кг; 0 — взять WeightKg): сгонка вниз бьёт «как более крупный», переход вверх — легче своей категории (S-57).
struct FFighterSetup
{
	FBoxerStats Stats;
	float HeightCm = 178.f; // ядру не нужен (для масштаба модели в UE)
	float ReachCm = 183.f;
	float WeightKg = 70.f;
	float MassForPower = 0.f;   // FightProfile.massForPower (0 — WeightKg)
	float DurabilityMass = 0.f; // FightProfile.durabilityMass (0 — WeightKg)
	EBoxStyle Style = EBoxStyle::Balanced;
	float Seasoning = 1.f;  // 1 — обстрелян; <1 — «зелёный» на дистанции (налог в судействе/баке)
	bool bAiControlled = false;
};

struct FFightConfig
{
	FFighterSetup Fighters[2];   // [0] — красный угол, [1] — синий
	int32 Rounds = 3;
	// Длина раунда реального времени. В вебе — 55 с (ROUND_SECONDS); баланс счётчиков раунда
	// откалиброван под 55 с, поэтому ядро масштабирует раундовые величины на 55/RoundSeconds
	// (при 55 формулы совпадают с TS один-в-один). См. Docs/FIGHT_CORE_PORT.md.
	float RoundSeconds = 180.f;
	float BreakSeconds = 60.f;   // перерыв между раундами
	bool bAutoProceed = true;    // false — перерыв длится, пока UE-слой не подаст Proceed
	bool bAllowDraw = false;     // профи: ничья возможна; любители — добивается по очкам
	// Постановка раунда (corners.ts): старт и перерыв в своих углах, выход по гонгу, нейтральный угол на
	// нокдауне. false — прежний режим «раунд с центра» (corners:false веба; проверки механики).
	bool bCorners = true;
	// Dev (web `?ko=1`, S-57): «стеклянный» синий — формульных нокдаунов у него нет, падает ровно на здоровье 0 и не
	// встаёт (KO), «форма дня» обоих = 1. Для скриншотов нокаута/повтора; на обычный бой не влияет.
	bool bGlassJaw = false;
	uint32 Seed = 1;
};

// Карта одного судьи: [красный, синий].
struct FJudgeCard
{
	int32 Red = 0;
	int32 Blue = 0;
};

struct FRoundResult
{
	int32 Round = 0;
	float Landed[2] = {0.f, 0.f};     // взвешенные чистые попадания
	FJudgeCard JudgeCards[3];
	int32 Stamina[2] = {0, 0};
	int32 Knockdowns[2] = {0, 0};
	int32 Damage[2] = {0, 0};         // урон, ПОЛУЧЕННЫЙ бойцом
};

struct FFightResult
{
	int32 WinnerIndex = -1;           // -1 — ничья
	EFightMethod Method = EFightMethod::None;
	EDecisionKind Decision = EDecisionKind::None;
	int32 StoppedRound = 0;           // 0 — бой прошёл всю дистанцию
	FJudgeCard JudgeTotals[3];
	int32 Knockdowns[2] = {0, 0};
	float Form[2] = {1.f, 1.f};       // скрытая «форма дня» 0.9..1.1
	TArray<FRoundResult> Rounds;
};

// Событие боя для звука/VFX/камеры. Читается PollEvents() (очередь очищается).
struct FFightEvent
{
	EFightEventKind Kind = EFightEventKind::Miss;
	int32 Attacker = -1;              // кто бил (−1 — не применимо)
	int32 Defender = -1;              // по кому (для Knockdown — кто упал; FightEnd — проигравший)
	float Magnitude = 0.f;            // Hit — урон по здоровью; Blocked — утечка сквозь блок
	EPunchType Punch = EPunchType::Jab;
	EPunchArm Arm = EPunchArm::Lead;
	EPunchTarget Target = EPunchTarget::Head;
	bool bCounter = false;            // попадание на контр-окне после удачного уклона
	bool bPerfect = false;            // Slipped: нырок «вовремя»
	bool bGuardBreak = false;         // Hit: блок пробит этим ударом
	bool bCaught = false;             // Hit: пойман на выходе из нырка
	int32 Round = 0;
	float Time = 0.f;                 // внутреннее боевое время (сек)
};

// Состояние бойца для анимации/HUD. Всё — производные, читать каждый кадр.
struct FFighterState
{
	float X = 0.f;                    // м, центр ринга (0,0)
	float Z = 0.f;
	float Yaw = 0.f;                  // рад, соглашение TS/three.js: 0 — лицом к +X, atan2(-dz, dx)
	float YawDegUE = 0.f;             // градусы, соглашение UE (X_ue=X, Y_ue=Z): atan2(dz, dx)
	float Health = 100.f;             // 0..100 (производное от износа)
	float StaminaPct = 100.f;         // 0..100 от максимума
	bool bPunching = false;
	EPunchType Punch = EPunchType::Jab;
	EPunchTarget PunchTarget = EPunchTarget::Head;
	float PunchPhase = 0.f;           // 0..1 по времени цикла; контакт на CONTACT_FRAC = 0.45
	float PunchPhaseAnim = 0.f;       // 0..1, переложено так, что контакт = 0.5 (как poseOf в hud.ts)
	float PunchDuration = 0.f;        // с, длина всего цикла удара (End − Start) — для синхронизации монтажа
	float PunchTimeToContact = 0.f;   // с до момента резолюции контакта (≤ 0 — контакт уже прошёл)
	bool bBlocking = false;
	float GuardIntegrity = 1.f;       // 1 — свежий блок, 0 — вот-вот пробьют / руки забиты
	float Slip = 0.f;                 // −1..1: синус-кривая нырка со знаком стороны (0 — не в нырке)
	float SlipPhase = 0.f;            // 0..1 линейная доля окна уклона (0 — не в нырке)
	bool bStaggered = false;          // «оглушён»: после тяжёлого попадания/провала/пробития блока руки не слушаются
	float Hurt = 0.f;                 // 0..1 «встряска» от попадания
	EPunchTarget HurtTarget = EPunchTarget::Head;
	bool bDown = false;               // лежит (нокдаун или проигрыш досрочкой)
	bool bKO = false;                 // лежит в финале боя
	int32 Knockdowns = 0;
	EStepKind Step = EStepKind::None;
	int32 RopeLevel = 0;              // 0 — центр, 1 — канаты, 2 — угол
	bool bAngle = false;              // у бойца открыт угол (соперник не довернулся)
	bool bGassed = false;             // HUD: мигнуть стаминой (отказ удара на пустом баке)
	float Victory = 0.f;              // 1 — победил, 0.5 — ничья, 0 — нет/бой идёт
	bool bDefeated = false;
	// Постановка (pose.walk веба): идёт по стадии — скорость (м/с, 0 — стоит) и её вектор в осях ядра.
	// Курс (Yaw/YawDegUE) на ходу в угол — по ходу движения; к бою и стоя — на соперника.
	float WalkSpeed = 0.f;
	float WalkVelX = 0.f;
	float WalkVelZ = 0.f;
};

struct FFightSnapshot
{
	EFightPhase Phase = EFightPhase::Fighting;
	int32 Round = 1;
	int32 TotalRounds = 3;
	float TimeLeft = 0.f;             // до конца раунда
	float BreakLeft = 0.f;            // до конца перерыва (Phase == Between)
	float Distance = 0.f;             // м между бойцами
	FFighterState Fighters[2];
	int32 DownWho = -1;               // кто лежит (Phase == Down)
	int32 DownCount = 0;              // счёт рефери 1..10
	float RiseProgress = 0.f;         // 0..1 набитый тапами подъём (человек)
	FJudgeCard JudgeTotals[3];        // накопительные суммы судей
	bool bHasResult = false;          // итог — GetResult()
	// Постановка раунда (S-53): стадия, цели, кто дошёл. Stage.Kind == None — идёт бой (или бой окончен).
	FRingStage Stage;
	// Центр тела лежащего (нокдаун с углами): путь стоящего в нейтральный угол его обходит.
	bool bHasLyingBody = false;
	float LyingX = 0.f;
	float LyingZ = 0.f;
	bool bCorners = true;             // постановка включена (FFightConfig::bCorners)
};
