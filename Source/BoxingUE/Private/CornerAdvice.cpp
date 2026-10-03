// «Совет угла» (S-71) — порт web/src/ui/cornerAdvice.ts. Тексты и порядок правил — как в вебе.
#include "CornerAdvice.h"

namespace CornerAdvice
{
	namespace
	{
		EPunchKind KindOf(EPunchType P)
		{
			switch (P)
			{
			case EPunchType::Jab: return EPunchKind::Jab;
			case EPunchType::Cross: return EPunchKind::Cross;
			case EPunchType::HookL:
			case EPunchType::HookR: return EPunchKind::Hook;
			default: return EPunchKind::Uppercut;
			}
		}

		const TCHAR* PunchRu(EPunchKind K)
		{
			switch (K)
			{
			case EPunchKind::Jab: return TEXT("джеб");
			case EPunchKind::Cross: return TEXT("кросс");
			case EPunchKind::Hook: return TEXT("хук");
			default: return TEXT("апперкот");
			}
		}

		// Грамматика (grammar веба).
		struct FGr
		{
			FString He;  // Он / Она
			FString Him; // за ним / за ней
			FString His; // его / её
			bool bMeF = false;
			bool bFoeF = false;
			FString Past(const TCHAR* M, const TCHAR* F) const { return bMeF ? F : M; }
			FString FoePast(const TCHAR* M, const TCHAR* F) const { return bFoeF ? F : M; }
		};

		FGr Grammar(const FGender& G)
		{
			FGr R;
			R.He = G.bFoeFemale ? TEXT("Она") : TEXT("Он");
			R.Him = G.bFoeFemale ? TEXT("ней") : TEXT("ним");
			R.His = G.bFoeFemale ? TEXT("её") : TEXT("его");
			R.bMeF = G.bMeFemale;
			R.bFoeF = G.bFoeFemale;
			return R;
		}

		// «1 судья», «2–4 судьи», «5 судей».
		FString Judges(int32 N)
		{
			const int32 M10 = N % 10, M100 = N % 100;
			if (M10 == 1 && M100 != 11) return FString::Printf(TEXT("%d судья"), N);
			if (M10 >= 2 && M10 <= 4 && (M100 < 12 || M100 > 14)) return FString::Printf(TEXT("%d судьи"), N);
			return FString::Printf(TEXT("%d судей"), N);
		}

		const TCHAR* AllOf(int32 N)
		{
			return N == 5 ? TEXT("у всех пяти судей") : (N == 3 ? TEXT("у всех трёх судей") : TEXT("у всех судей"));
		}
	}

	// ---------------- копилка ----------------

	void FCornerTally::Reset(int32 Round)
	{
		S = FRoundStats();
		S.Round = Round;
		FMemory::Memzero(Combo);
		bWasRead = false;
	}

	void FCornerTally::ThrownBy(EPunchType Punch, EPunchTarget Target)
	{
		++S.Thrown;
		if (Target == EPunchTarget::Body) ++S.BodyThrown;
		else ++S.HeadThrown;
		const EPunchKind K = KindOf(Punch);
		++S.ByKind[static_cast<int32>(K)];
		++Combo[static_cast<int32>(K) * 2 + (Target == EPunchTarget::Body ? 1 : 0)];
		int32 Best = 0;
		for (int32 I = 1; I < 8; ++I)
		{
			if (Combo[I] > Combo[Best]) Best = I; // при равенстве — первый (как перебор Map веба по порядку вставки — близко)
		}
		S.bHasTop = Combo[Best] > 0;
		S.TopKind = static_cast<EPunchKind>(Best / 2);
		S.TopTarget = (Best % 2) ? EPunchTarget::Body : EPunchTarget::Head;
		S.TopShare = static_cast<float>(Combo[Best]) / FMath::Max(1, S.Thrown);
	}

	void FCornerTally::Feed(const FFightEvent& E)
	{
		const int32 Foe = 1 - Me;
		switch (E.Kind)
		{
		case EFightEventKind::Hit:
		case EFightEventKind::Blocked:
		{
			const bool bLand = E.Kind == EFightEventKind::Hit;
			const bool bBody = E.Target == EPunchTarget::Body;
			if (E.Attacker == Me)
			{
				ThrownBy(E.Punch, E.Target);
				if (bLand)
				{
					++S.Landed;
					if (bBody) ++S.BodyLanded;
					else ++S.HeadLanded;
					if (E.bCounter) ++S.Counters;
					if (E.Magnitude >= HEAVY) ++S.HeavyGiven;
				}
				else
				{
					++S.Blocked;
					if (!bBody) ++S.HeadBlocked;
				}
			}
			else if (E.Attacker == Foe)
			{
				++S.FoeThrown;
				if (bBody) ++S.FoeBodyThrown;
				if (bLand)
				{
					++S.FoeLanded;
					if (bBody) ++S.FoeBodyLanded;
					const EPunchKind K = KindOf(E.Punch);
					if (K == EPunchKind::Hook || K == EPunchKind::Uppercut) ++S.FoeLandedClose;
					else ++S.FoeLandedLong;
					if (E.Magnitude >= HEAVY) ++S.HeavyTaken;
					if (E.bGuardBreak) ++S.GuardBroken;
					if (E.bCaught) ++S.Caught;
				}
				else
				{
					++S.BlocksTaken;
				}
			}
			break;
		}
		case EFightEventKind::Miss:
		case EFightEventKind::Slipped:
		{
			const bool bSlip = E.Kind == EFightEventKind::Slipped;
			// Нырок ядро не помечает целью (в вебе промах всегда «в голову») — уклон корпусом почти не спасает.
			const EPunchTarget Target = bSlip ? EPunchTarget::Head : E.Target;
			if (E.Attacker == Me)
			{
				ThrownBy(E.Punch, Target);
				++S.Missed;
				if (bSlip) ++S.SlippedByFoe;
			}
			else if (E.Attacker == Foe)
			{
				++S.FoeThrown;
				if (Target == EPunchTarget::Body) ++S.FoeBodyThrown;
				if (bSlip) ++S.Slips;
			}
			break;
		}
		case EFightEventKind::Knockdown:
			if (E.Defender == Me) ++S.KdTaken;
			else if (E.Defender == Foe) ++S.KdGiven;
			break;
		case EFightEventKind::Gassed:
			if (E.Attacker == Me) ++S.GassedHits;
			break;
		default:
			break;
		}
	}

	void FCornerTally::Sample(const FFightSnapshot& Snap, float Dt, bool bRead)
	{
		if (Snap.Round != S.Round)
		{
			Reset(Snap.Round);
		}
		if (bRead && !bWasRead) ++S.ReadHints;
		bWasRead = bRead;
		if (Snap.Phase != EFightPhase::Fighting) return;
		const FFighterState& M = Snap.Fighters[Me];
		const FFighterState& F = Snap.Fighters[1 - Me];
		S.FightTime += Dt;
		if (M.RopeLevel > 0) S.RopesTime += Dt;
		if (Snap.Distance < CLOSE_D) S.CloseTime += Dt;
		else if (Snap.Distance >= LONG_D) S.LongTime += Dt;
		S.Stamina = M.StaminaPct;
		S.Health = M.Health;
		S.FoeStamina = F.StaminaPct;
		S.FoeHealth = F.Health;
	}

	// ---------------- советы ----------------

	void RoundCards(const FJudgeCard* Totals, const FJudgeCard* Prev, int32 Num, FJudgeCard* Out)
	{
		for (int32 J = 0; J < Num; ++J)
		{
			Out[J].Red = Totals[J].Red - (Prev ? Prev[J].Red : 0);
			Out[J].Blue = Totals[J].Blue - (Prev ? Prev[J].Blue : 0);
		}
	}

	FVerdict RoundVerdict(const FJudgeCard* Cards, int32 Num, const FGender& G, bool bPlayerRed)
	{
		FVerdict V;
		if (!Cards || Num <= 0) return V;
		V.bValid = true;
		int32 Mine = 0, His = 0;
		bool bAllEven = true;
		bool bEmpty = true;
		for (int32 J = 0; J < Num; ++J)
		{
			FJudgeCard C = Cards[J];
			if (!bPlayerRed) Swap(C.Red, C.Blue);
			V.Cards.Add(C);
			if (C.Red > C.Blue) ++Mine;
			else if (C.Blue > C.Red) ++His;
			if (C.Red != C.Blue) bAllEven = false;
			if (C.Red != 0 || C.Blue != 0) bEmpty = false;
		}
		if (bEmpty)
		{
			V.bValid = false; // раунд не судили (досрочка до гонга)
			return V;
		}
		if (Mine > His)
		{
			V.Text = Mine == Num ? FString::Printf(TEXT("Раунд твой — %s"), AllOf(Num)) : FString::Printf(TEXT("Раунд твой — %s из %d"), *Judges(Mine), Num);
			V.Tone = ETone::Good;
		}
		else if (His > Mine)
		{
			const FGr Gr = Grammar(G);
			V.Text = His == Num ? FString::Printf(TEXT("Раунд за %s — %s"), *Gr.Him, AllOf(Num))
				: FString::Printf(TEXT("Раунд за %s — %s из %d"), *Gr.Him, *Judges(His), Num);
			V.Tone = ETone::Bad;
		}
		else
		{
			V.Text = bAllEven ? TEXT("Раунд равный — у всех 10–10") : TEXT("Раунд равный — судьи разошлись");
			V.Tone = ETone::Even;
		}
		return V;
	}

	FString StatsLine(const FRoundStats& S)
	{
		const int32 Acc = S.Thrown > 0 ? FMath::RoundToInt(100.f * S.Landed / S.Thrown) : 0;
		FString L = FString::Printf(TEXT("попадания %d–%d · точность %d%%"), S.Landed, S.FoeLanded, Acc);
		if (S.KdGiven + S.KdTaken > 0) L += FString::Printf(TEXT(" · нокдауны %d–%d"), S.KdGiven, S.KdTaken);
		return L;
	}

	namespace
	{
		struct FRule
		{
			TFunction<bool(const FRoundStats&, const FVerdict&)> When;
			TFunction<FTip(const FRoundStats&, const FGr&)> Tip;
			bool bVerdictOnly = false; // похвала/упрёк по итогу — только если тактических советов нет
		};

		FTip T(const FString& Text, ETone Tone)
		{
			FTip X;
			X.Text = Text;
			X.Tone = Tone;
			return X;
		}

		FString Lower(const FString& S) { return S.ToLower(); }

		const TArray<FRule>& Rules()
		{
			static const TArray<FRule> R = {
				{[](const FRoundStats& S, const FVerdict&) { return S.KdTaken > 0; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("Ты %s на полу — руки выше, не размен. Джеб и ноги, пока голова не прояснится."), *G.Past(TEXT("был"), TEXT("была"))), ETone::Bad); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.Stamina < 30.f || S.GassedHits > 0; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("Ты %s — держи дистанцию, дыши. Джеб и ноги, без размашистых."), *G.Past(TEXT("устал"), TEXT("устала"))), ETone::Warn); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.ReadHints >= 2 || (S.bHasTop && S.Thrown >= 8 && S.TopShare >= 0.6f); },
					[](const FRoundStats& S, const FGr& G)
					{
						return T(FString::Printf(TEXT("%s читает твой %s%s — меняй удары, мешай уровни."), *G.He, S.bHasTop ? PunchRu(S.TopKind) : TEXT("удар"),
							S.bHasTop && S.TopTarget == EPunchTarget::Body ? TEXT(" в корпус") : TEXT("")), ETone::Warn);
					}},
				{[](const FRoundStats& S, const FVerdict&) { return S.GuardBroken > 0 || S.BlocksTaken >= 10; },
					[](const FRoundStats&, const FGr&) { return T(TEXT("Не стой в глухом блоке — его пробивают. Ныряй и отвечай сразу."), ETone::Warn); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.FightTime > 8.f && S.RopesTime / S.FightTime > 0.3f; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("Ты %s у канатов — уходи по дуге, не пяться назад."), *G.Past(TEXT("завис"), TEXT("зависла"))), ETone::Warn); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.FoeLanded >= 5 && S.FoeLanded > S.Landed * 1.3f && S.FoeLandedClose > S.FoeLandedLong; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("Вблизи %s ловит тебя хуками — не стой на месте, работай с дальней."), *Lower(G.He)), ETone::Warn); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.FoeLanded >= 5 && S.FoeLanded > S.Landed * 1.3f && S.LongTime > S.CloseTime; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("На дальней %s джеб быстрее — сокращай дистанцию шагом и бей серией."), *G.His), ETone::Warn); }},
				{[](const FRoundStats& S, const FVerdict&)
					{
						return S.HeadThrown >= 6 && static_cast<float>(S.HeadBlocked + S.SlippedByFoe) / S.HeadThrown > 0.45f
							&& static_cast<float>(S.BodyThrown) / FMath::Max(1, S.Thrown) < 0.25f;
					},
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("%s закрывает голову — бей в корпус, руки опустятся."), *G.He), ETone::Warn); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.FoeStamina < 30.f; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("%s %s — дави, бросай серии!"), *G.He, *G.FoePast(TEXT("выдохся"), TEXT("выдохлась"))), ETone::Good); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.FoeHealth < 45.f || S.KdGiven > 0; },
					[](const FRoundStats&, const FGr& G) { return T(FString::Printf(TEXT("%s %s — дожимай, но не открывайся."), *G.He, *G.FoePast(TEXT("поплыл"), TEXT("поплыла"))), ETone::Good); }},
				{[](const FRoundStats& S, const FVerdict&) { return S.Slips >= 2 && S.Counters == 0; },
					[](const FRoundStats&, const FGr& G)
					{
						return T(FString::Printf(TEXT("Нырки работают — отвечай сразу после уклона, там %s %s."), *Lower(G.He), *G.FoePast(TEXT("открыт"), TEXT("открыта"))), ETone::Good);
					}},
				{[](const FRoundStats& S, const FVerdict&) { return S.FightTime > 15.f && S.Thrown < 8; },
					[](const FRoundStats&, const FGr&) { return T(TEXT("Мало бросаешь — судьи не видят работы. Больше джеба."), ETone::Warn); }},
				{[](const FRoundStats&, const FVerdict& V) { return V.bValid && V.Tone == ETone::Bad; },
					[](const FRoundStats&, const FGr&) { return T(TEXT("Раунд ушёл — нужна активность: джеб, связки, первым номером."), ETone::Warn); }, true},
				{[](const FRoundStats&, const FVerdict& V) { return V.bValid && V.Tone == ETone::Good; },
					[](const FRoundStats&, const FGr&) { return T(TEXT("Хороший раунд — так держать, не расслабляйся."), ETone::Good); }, true},
			};
			return R;
		}

		// Реплика катмена: только если бойца ощутимо побили — что он делает в углу.
		bool CutmanTip(const FRoundStats& S, FTip& Out)
		{
			if (S.Health >= 60.f && S.HeavyTaken < 3) return false;
			static const TCHAR* Lines[] = {TEXT("Лёд на скулу — держись, отёк остановили."), TEXT("Бровь держится, вазелин на лицо."), TEXT("Вода, дыши носом — отёк под контролем.")};
			Out.Who = EWho::Cutman;
			Out.Text = Lines[FMath::Max(0, S.Round - 1) % 3];
			Out.Tone = S.Health < 40.f ? ETone::Bad : ETone::Warn;
			return true;
		}
	}

	FTalk CornerTalk(const FRoundStats& S, const FVerdict& Verdict, const FGender& G)
	{
		FTalk Talk;
		Talk.Verdict = Verdict;
		const FGr Gr = Grammar(G);
		TSet<FString> Seen;
		for (const FRule& R : Rules())
		{
			if (Talk.Tips.Num() >= MAX_TIPS) break;
			if (!R.When(S, Verdict)) continue;
			if (R.bVerdictOnly && Talk.Tips.Num() > 0) continue;
			FTip Tip = R.Tip(S, Gr);
			if (Seen.Contains(Tip.Text)) continue;
			Seen.Add(Tip.Text);
			Tip.Who = EWho::Trainer;
			Talk.Tips.Add(MoveTemp(Tip));
		}
		if (Talk.Tips.Num() == 0)
		{
			Talk.Tips.Add(T(TEXT("Работай джебом и двигайся — раунд близкий."), ETone::Warn));
		}
		FTip Cut;
		if (CutmanTip(S, Cut)) Talk.Tips.Add(Cut);
		Talk.Line = StatsLine(S);
		return Talk;
	}
}
