// Проекция веса боя (порт fightProfile веба) и быстрый прогноз пары ядром (S-61). См. FightProfile.h.
#include "FightProfile.h"
#include "BoxingFightCore.h"

namespace
{
	// clamp веба (stats.ts): статы держатся в 1..99.
	float ClampStat(double V) { return static_cast<float>(FMath::Max(1.0, FMath::Min(99.0, V))); }
}

double BoxingFightProfile::NearestClass(double Kg, const double* Classes, int32 Num)
{
	if (!Classes || Num <= 0) return Kg;
	double Best = Classes[0];
	for (int32 I = 1; I < Num; ++I)
	{
		if (FMath::Abs(Classes[I] - Kg) < FMath::Abs(Best - Kg)) Best = Classes[I];
	}
	return Best;
}

double BoxingFightProfile::Catchweight(double RedKg, double BlueKg, const double* Classes, int32 Num)
{
	return NearestClass((RedKg + BlueKg) / 2, Classes, Num);
}

double BoxingFightProfile::WeightStretch(double RingKg, double RedKg, double BlueKg)
{
	return FMath::Max(FMath::Abs(RingKg - RedKg), FMath::Abs(RingKg - BlueKg));
}

FFighterSetup BoxingFightProfile::ProjectToWeight(const FFighterSetup& Setup, double TargetKg)
{
	FFighterSetup S = Setup;
	const double Natural = Setup.WeightKg;
	const double Target = TargetKg > 0 ? TargetKg : Natural;
	const double Delta = Natural - Target; // > 0 сгонка вниз, < 0 переход вверх
	double MassForPower = Target;
	double DurabilityMass = Target;
	if (Delta > 0)
	{
		// Сгонка: обезвоживание бьёт по кардио и подбородку, но мышцы те же — бьёт как более крупный.
		S.Stats.Stamina = ClampStat(Setup.Stats.Stamina * (1 - 0.02 * Delta));
		S.Stats.Chin = ClampStat(Setup.Stats.Chin * (1 - 0.012 * Delta));
		MassForPower = Target + 0.55 * Delta;
		DurabilityMass = Target + 0.5 * Delta;
	}
	else if (Delta < 0)
	{
		// Переход вверх: массы не прибавилось — против больших тел сила и живучесть ниже натуральных (нет «бесплатного»
		// буста), нести непривычный вес тяжелее, подбородок хуже держит чужой калибр.
		const double Gain = -Delta;
		S.Stats.Stamina = ClampStat(Setup.Stats.Stamina * (1 - 0.004 * Gain));
		S.Stats.Chin = ClampStat(Setup.Stats.Chin * (1 - 0.005 * Gain));
		MassForPower = Natural - 0.3 * Gain;
		DurabilityMass = Natural - 0.35 * Gain;
	}
	S.MassForPower = static_cast<float>(MassForPower);
	S.DurabilityMass = static_cast<float>(DurabilityMass);
	return S;
}

BoxingFightProfile::FOutcomeOdds BoxingFightProfile::PredictOutcome(FFighterSetup Red, FFighterSetup Blue, int32 Rounds, bool bProRules,
	int32 Fights, uint32 Seed)
{
	FOutcomeOdds O;
	Red.bAiControlled = true;
	Blue.bAiControlled = true;
	const int32 N = FMath::Max(1, Fights);
	int32 RedW = 0, BlueW = 0, Draws = 0, RedStop = 0, BlueStop = 0, Kd = 0;
	for (int32 K = 0; K < N; ++K)
	{
		const bool bSwap = (K & 1) == 1; // половина боёв — со сменой углов: ИИ за красного и за синего ходит в разном порядке ГСЧ
		FFightConfig C;
		C.Fighters[bSwap ? 1 : 0] = Red;
		C.Fighters[bSwap ? 0 : 1] = Blue;
		C.Rounds = FMath::Max(1, Rounds);
		C.RoundSeconds = 55.f;
		C.BreakSeconds = 0.f;
		C.bAllowDraw = bProRules;
		C.bProRules = bProRules;
		C.bCorners = false;
		C.Seed = (static_cast<uint32>(K) * 2654435761u + 17u) ^ Seed;
		FBoxingFightCore Core;
		Core.Init(C);
		for (int32 Step = 0; Step < 20 * 60 * 60 && !Core.IsOver(); ++Step)
		{
			Core.Tick(0.05f);
			Core.PollEvents();
		}
		const FFightResult& R = Core.GetResult();
		int32 W = R.WinnerIndex;
		if (bSwap && W >= 0) W = 1 - W;
		const bool bStop = R.Method == EFightMethod::KO || R.Method == EFightMethod::RSC;
		if (W == 0) { ++RedW; RedStop += bStop ? 1 : 0; }
		else if (W == 1) { ++BlueW; BlueStop += bStop ? 1 : 0; }
		else ++Draws;
		Kd += R.Knockdowns[0] + R.Knockdowns[1];
	}
	O.Fights = N;
	O.RedWin = static_cast<float>(RedW) / N;
	O.BlueWin = static_cast<float>(BlueW) / N;
	O.Draw = static_cast<float>(Draws) / N;
	O.RedStoppage = static_cast<float>(RedStop) / N;
	O.BlueStoppage = static_cast<float>(BlueStop) / N;
	O.KnockdownsPerFight = static_cast<float>(Kd) / N;
	return O;
}
