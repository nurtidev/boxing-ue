// FFightBot — бот «человека» для автоплейтестов (S-57). Порт бота из web/test/interactive-parity.test.ts
// (humanFight + BOTS): видит ЗАМАХ соперника по снимку (как человек — по анимации), с реакцией 0.1–0.2 с
// закрывается/ныряет (и, если умеет, отвечает кроссом), бьёт в своём темпе набором 40/30/20/10 (только то, что
// достаёт с текущей дистанции), сокращает дистанцию шагом, на низкой стамине чаще пропускает удар, на своём
// нокдауне тапает подъём ~6 раз/с. Ошибки — промахи реакции (доля react), «не тот» ответ (блок вместо уклона),
// поздний старт и удары невпопад.
//
// Только CoreMinimal (как ядро): тот же код гоняют GameMode (-BoxBot=…, ввод через QueueAction/SetHeldStep —
// путь клавиатуры) и Tools/CoreHarness (таблица побед). Свой ГСЧ (mulberry32 от сида боя ^ 0x5bd1e995, как в
// вебе) — ядро и его сидируемость не трогаются: один сид боя + один бот → один бой.
#pragma once

#include "CoreMinimal.h"
#include "FightTypes.h"
#include "BoxingFightCore.h"

enum class EFightBotSkill : uint8
{
	Novice,  // web BOTS.newbie
	Average, // web BOTS.average — «средний игрок»
	Strong,  // web BOTS.good
	Masher,  // web BOTS.spam — молотит одну за другой, без защиты и отдыха
	// S-76: абьюз-боты (проверка ИИ стилей: «одна кнопка», «черепаха», «бегун» не должны побеждать равного).
	JabSpam, // одна кнопка: только джеб, как только можно, без защиты; далеко — шаг вперёд
	Turtle,  // вечный блок: держит блок весь бой, раз в 1.5–2.5 с опускает руки на один кросс (когда соперник не бьёт)
	Runner,  // бегун: держит дальнюю дистанцию (≥ 1.6 м) шагами назад и по дуге, с края — джеб и снова назад
};

struct FFightBotParams
{
	double TempoMin = 0.6, TempoMax = 1.1; // пауза между своими ударами, с
	double React = 0.35;                   // доля замахов соперника, на которые реагирует
	double SlipShare = 0.3;                // доля уклонов среди реакций (остальное — блок)
	double Rest = 0.25;                    // ниже этой доли стамины — чаще пропускает удар (отдыхает)
	bool bCounter = true;                  // после уклона — встречный кросс
	double Body = 0;                       // доля ударов в корпус
};

struct FFightBotCmd
{
	EFightAction Action = EFightAction::Jab;
	EPunchTarget Target = EPunchTarget::Head;
};

class BOXINGUE_API FFightBot
{
public:
	static FFightBotParams ParamsFor(EFightBotSkill Skill);
	static const char* SkillName(EFightBotSkill Skill);

	// Новый бой: Me — индекс бойца бота (0 — красный), FightSeed — сид боя (бот сидирует свой ГСЧ от него).
	void Reset(EFightBotSkill InSkill, uint32 FightSeed, int32 InMe = 0);

	// Один шаг ядра (Dt — шаг): читает снимок, кладёт нажатия в Out (удары/блок/уклон/подъём — как QueueAction)
	// и сообщает удержание ног (как SetHeldStep: bHeld + действие). Вызывать ДО шага ядра.
	void Think(const FFightSnapshot& S, double Dt, TArray<FFightBotCmd>& Out, bool& bHeld, EFightAction& HeldStep);

	EFightBotSkill GetSkill() const { return Skill; }

	// Счётчики бота (для сводки): ударов нажато, реакций (блок/уклон), подъёмных тапов.
	int32 PunchesPressed = 0;
	int32 Blocks = 0;
	int32 Slips = 0;
	int32 RiseTaps = 0;

private:
	EFightBotSkill Skill = EFightBotSkill::Average;
	FFightBotParams P;
	FBoxingRng Rng;
	int32 Me = 0;
	double T = 0;
	double NextPunch = 0.5;
	double BlockUntil = -1;
	double CounterAt = -1;
	double SeenFoePunch = -1;
	bool bReact = false;
	double ReactAt = 0;
	bool bReactSlip = false;
	double NextStep = 0;
	bool bBlockHeld = false;
	int32 RunDir = 1; // бегун: сторона дуги (меняется у канатов)

	// S-76: абьюз-боты (JabSpam/Turtle/Runner) — свои простые правила вместо реакции «человека».
	void ThinkAbuser(const FFightSnapshot& S, TArray<FFightBotCmd>& Out, bool& bHeld, EFightAction& HeldStep);
};
