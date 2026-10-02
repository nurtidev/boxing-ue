// Бот «человека» (S-57) — порт humanFight/BOTS из web/test/interactive-parity.test.ts. См. FightBot.h.
#include "FightBot.h"

namespace
{
	// Набор ударов бота веба: джеб ×4, кросс ×3, хук ×2, апперкот ×1 (хук — передней, апперкот — дальней: armFor веба).
	constexpr EPunchKind BOT_MIX[10] = {EPunchKind::Jab, EPunchKind::Jab, EPunchKind::Jab, EPunchKind::Jab, EPunchKind::Cross,
		EPunchKind::Cross, EPunchKind::Cross, EPunchKind::Hook, EPunchKind::Hook, EPunchKind::Uppercut};
	constexpr double SEE_PHASE = 0.3;     // замах виден, пока поза удара (контакт = 0.5) не дошла до 0.3
	constexpr double REACT_MIN = 0.1;     // реакция человека, с: 0.1 + 0.1·rnd
	constexpr double REACT_SPREAD = 0.1;
	constexpr double BLOCK_HOLD = 0.45;   // держит блок после реакции, с
	constexpr double COUNTER_DELAY = 0.3; // встречный после уклона, с
	constexpr double STEP_EVERY = 0.35;   // как часто думает о дистанции: 0.35 + 0.3·rnd
	constexpr double STEP_SPREAD = 0.3;
	constexpr double CLOSE_FROM = 1.35;   // дальше — шаг вперёд
	constexpr double RANGE_OK = 0.5;      // удар из набора — только если rangeFactor ≥ 0.5
	constexpr double REST_SKIP = 0.7;     // на низкой стамине пропускает удар с этой вероятностью
	constexpr double REST_PAUSE = 0.5;
	constexpr double RISE_TAPS = 6;       // тапов подъёма в секунду

	EFightAction ActionOf(EPunchKind K)
	{
		switch (K)
		{
		case EPunchKind::Jab: return EFightAction::Jab;
		case EPunchKind::Cross: return EFightAction::Cross;
		case EPunchKind::Hook: return EFightAction::HookL;
		default: return EFightAction::UpperR;
		}
	}
}

FFightBotParams FFightBot::ParamsFor(EFightBotSkill Skill)
{
	FFightBotParams R;
	switch (Skill)
	{
	case EFightBotSkill::Novice:
		R.TempoMin = 1.0; R.TempoMax = 1.8; R.React = 0.12; R.SlipShare = 0.2; R.Rest = 0.2; R.bCounter = false;
		break;
	case EFightBotSkill::Strong:
		R.TempoMin = 0.45; R.TempoMax = 0.8; R.React = 0.6; R.SlipShare = 0.5; R.Rest = 0.3; R.bCounter = true;
		break;
	case EFightBotSkill::Masher:
		R.TempoMin = 0; R.TempoMax = 0.02; R.React = 0; R.SlipShare = 0; R.Rest = 0; R.bCounter = false;
		break;
	default:
		break; // Average — значения по умолчанию
	}
	return R;
}

const char* FFightBot::SkillName(EFightBotSkill Skill)
{
	switch (Skill)
	{
	case EFightBotSkill::Novice: return "novice";
	case EFightBotSkill::Strong: return "strong";
	case EFightBotSkill::Masher: return "masher";
	default: return "average";
	}
}

void FFightBot::Reset(EFightBotSkill InSkill, uint32 FightSeed, int32 InMe)
{
	Skill = InSkill;
	P = ParamsFor(InSkill);
	const uint32 BotSeed = FightSeed ^ 0x5bd1e995u;
	Rng.Seed(BotSeed != 0 ? BotSeed : 1u);
	Me = InMe;
	T = 0;
	NextPunch = 0.5;
	BlockUntil = -1;
	CounterAt = -1;
	SeenFoePunch = -1;
	bReact = false;
	ReactAt = 0;
	bReactSlip = false;
	NextStep = 0;
	bBlockHeld = false;
	PunchesPressed = Blocks = Slips = RiseTaps = 0;
}

void FFightBot::Think(const FFightSnapshot& S, double Dt, TArray<FFightBotCmd>& Out, bool& bHeld, EFightAction& HeldStep)
{
	bHeld = false;
	HeldStep = EFightAction::StepBack;
	if (S.Phase == EFightPhase::Over) return;
	if (S.Phase == EFightPhase::Between)
	{
		// Перерыв: руки опущены (блок отпущен, иначе кнопка «запомнилась» бы на следующий раунд).
		if (bBlockHeld) Out.Add({EFightAction::BlockEnd, EPunchTarget::Head});
		bBlockHeld = false;
		BlockUntil = -1;
		CounterAt = -1;
		bReact = false;
		return;
	}
	auto Press = [&Out](EFightAction A, EPunchTarget Tg = EPunchTarget::Head) { Out.Add({A, Tg}); };

	if (S.Phase == EFightPhase::Fighting)
	{
		const FFighterState& Mine = S.Fighters[Me];
		const FFighterState& Foe = S.Fighters[1 - Me];
		// Замах соперника виден по позе — с реакцией человека закрыться или нырнуть (не на каждый).
		if (Foe.bPunching && Foe.PunchPhaseAnim < SEE_PHASE)
		{
			const double Start = T - static_cast<double>(Foe.PunchPhase) * Foe.PunchDuration;
			if (FMath::Abs(Start - SeenFoePunch) > 0.2)
			{
				SeenFoePunch = Start;
				if (Rng.Next() < P.React)
				{
					bReact = true;
					ReactAt = T + REACT_MIN + Rng.Next() * REACT_SPREAD;
					bReactSlip = Rng.Next() < P.SlipShare;
				}
			}
		}
		if (bReact && T >= ReactAt)
		{
			if (bReactSlip)
			{
				Press(EFightAction::SlipRight); // playerSlip() веба — по умолчанию вправо
				++Slips;
				if (P.bCounter) CounterAt = T + COUNTER_DELAY;
			}
			else
			{
				Press(EFightAction::BlockStart);
				bBlockHeld = true;
				++Blocks;
				BlockUntil = T + BLOCK_HOLD;
			}
			bReact = false;
		}
		if (BlockUntil > 0 && T >= BlockUntil)
		{
			Press(EFightAction::BlockEnd);
			bBlockHeld = false;
			BlockUntil = -1;
		}
		if (CounterAt > 0 && T >= CounterAt)
		{
			Press(EFightAction::Cross);
			++PunchesPressed;
			CounterAt = -1;
		}
		// Дистанция: соперник ушёл — шаг вперёд (одно нажатие, не удержание).
		if (T >= NextStep)
		{
			NextStep = T + STEP_EVERY + Rng.Next() * STEP_SPREAD;
			if (S.Distance > CLOSE_FROM) Press(EFightAction::StepFwd);
		}
		// Свой удар — в своём темпе; на низкой стамине чаще ждёт.
		if (BlockUntil < 0 && T >= NextPunch)
		{
			if (Mine.StaminaPct < P.Rest * 100 && Rng.Next() < REST_SKIP)
			{
				NextPunch = T + REST_PAUSE;
			}
			else
			{
				EPunchKind Pool[10];
				int32 N = 0;
				for (int32 K = 0; K < 10; ++K)
				{
					if (FBoxingFightCore::RangeFactor(BOT_MIX[K], S.Distance) >= RANGE_OK) Pool[N++] = BOT_MIX[K];
				}
				if (N == 0)
				{
					for (int32 K = 0; K < 10; ++K) Pool[K] = BOT_MIX[K];
					N = 10;
				}
				const EPunchKind Kind = Pool[FMath::Min(N - 1, static_cast<int32>(Rng.Next() * N))];
				const EPunchTarget Tg = (P.Body > 0 && Rng.Next() < P.Body) ? EPunchTarget::Body : EPunchTarget::Head;
				Press(ActionOf(Kind), Tg);
				++PunchesPressed;
				NextPunch = T + P.TempoMin + Rng.Next() * (P.TempoMax - P.TempoMin);
			}
		}
	}
	else if (S.Phase == EFightPhase::Down && S.DownWho == Me && Rng.Next() < RISE_TAPS * Dt)
	{
		Press(EFightAction::RiseTap); // ~6 тапов в секунду
		++RiseTaps;
	}
	T += Dt;
}
