// Автотест «совета угла» (S-71) — зеркало web/test/cornerAdvice.test.ts + прогон на настоящем ядре (5 судей любителей / 3 профи).
// Запуск: UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosound -ExecCmds="Automation RunTests BoxingUE.CornerAdvice;Quit"
#include "CornerAdvice.h"
#include "BoxingFightCore.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace CornerAdvice;

	// Игрок (0) — атакующий: Attacker = 0, Defender = 1.
	FFightEvent CaEv(EFightEventKind K, int32 Att, float Mag = 0.7f, EPunchType P = EPunchType::Jab, EPunchTarget Tg = EPunchTarget::Head)
	{
		FFightEvent E;
		E.Kind = K;
		E.Attacker = Att;
		E.Defender = Att >= 0 ? 1 - Att : -1;
		E.Magnitude = Mag;
		E.Punch = P;
		E.Target = Tg;
		return E;
	}

	FRoundStats CaStats(TFunction<void(FRoundStats&)> F)
	{
		FRoundStats S;
		S.FightTime = 40.f;
		F(S);
		return S;
	}

	FVerdict CaVerdict3(std::initializer_list<FJudgeCard> Cards, FGender G = FGender())
	{
		TArray<FJudgeCard> A(Cards);
		return RoundVerdict(A.GetData(), A.Num(), G);
	}

	bool CaAnyTip(const FTalk& T, const TCHAR* Sub)
	{
		for (const FTip& X : T.Tips)
		{
			if (X.Text.Contains(Sub)) return true;
		}
		return false;
	}

	FFightConfig CaCfg(uint32 Seed, bool bPro)
	{
		FFightConfig C;
		C.Seed = Seed;
		C.Rounds = bPro ? 4 : 3;
		C.RoundSeconds = 40;
		C.BreakSeconds = 3;
		C.bAllowDraw = bPro;
		C.bProRules = bPro;
		C.Fighters[0].Stats = {76, 76, 74, 74, 74, 76, 74};
		C.Fighters[1].Stats = {75, 74, 74, 75, 74, 74, 75};
		C.Fighters[0].Style = EBoxStyle::Balanced;
		C.Fighters[1].Style = EBoxStyle::Pressure;
		C.Fighters[0].bAiControlled = true;
		C.Fighters[1].bAiControlled = true;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCornerAdviceTest, "BoxingUE.CornerAdvice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCornerAdviceTest::RunTest(const FString& Parameters)
{
	// --- копилка: Hit/Blocked/Miss/Slipped — Attacker бил, Defender — по кому ---
	{
		FCornerTally T(0);
		T.Feed(CaEv(EFightEventKind::Hit, 0));                                                // игрок попал
		FFightEvent H = CaEv(EFightEventKind::Hit, 0, 1.4f, EPunchType::HookL, EPunchTarget::Body);
		H.bCounter = true;
		T.Feed(H);                                                                          // тяжело в корпус на контре
		T.Feed(CaEv(EFightEventKind::Blocked, 0, 0.3f, EPunchType::Cross));                   // в блок соперника
		T.Feed(CaEv(EFightEventKind::Slipped, 0, 0.f, EPunchType::Jab));                      // соперник нырнул
		FFightEvent F = CaEv(EFightEventKind::Hit, 1, 1.2f, EPunchType::HookR);
		F.bGuardBreak = true;
		T.Feed(F);                                                                          // соперник тяжело хуком, пробив блок
		T.Feed(CaEv(EFightEventKind::Blocked, 1, 0.2f));                                      // игрок принял в блок
		T.Feed(CaEv(EFightEventKind::Slipped, 1, 0.f, EPunchType::Cross));                    // игрок нырнул
		FFightEvent Kd = CaEv(EFightEventKind::Knockdown, 0, 1.f);                           // соперник упал
		T.Feed(Kd);
		FFightEvent G;
		G.Kind = EFightEventKind::Gassed;
		G.Attacker = 0;
		T.Feed(G);
		const FRoundStats& S = T.Stats();
		TestEqual(TEXT("thrown"), S.Thrown, 4);
		TestEqual(TEXT("landed"), S.Landed, 2);
		TestEqual(TEXT("bodyLanded"), S.BodyLanded, 1);
		TestEqual(TEXT("blocked"), S.Blocked, 1);
		TestEqual(TEXT("headBlocked"), S.HeadBlocked, 1);
		TestEqual(TEXT("missed"), S.Missed, 1);
		TestEqual(TEXT("slippedByFoe"), S.SlippedByFoe, 1);
		TestEqual(TEXT("counters"), S.Counters, 1);
		TestEqual(TEXT("heavyGiven"), S.HeavyGiven, 1);
		TestEqual(TEXT("foeThrown"), S.FoeThrown, 3);
		TestEqual(TEXT("foeLanded"), S.FoeLanded, 1);
		TestEqual(TEXT("foeLandedClose"), S.FoeLandedClose, 1);
		TestEqual(TEXT("heavyTaken"), S.HeavyTaken, 1);
		TestEqual(TEXT("guardBroken"), S.GuardBroken, 1);
		TestEqual(TEXT("blocksTaken"), S.BlocksTaken, 1);
		TestEqual(TEXT("slips"), S.Slips, 1);
		TestEqual(TEXT("kdGiven"), S.KdGiven, 1);
		TestEqual(TEXT("kdTaken"), S.KdTaken, 0);
		TestEqual(TEXT("gassedHits"), S.GassedHits, 1);
		TestEqual(TEXT("byKind jab"), S.ByKind[0], 2);
		TestTrue(TEXT("topPunch — джеб"), S.bHasTop && S.TopKind == EPunchKind::Jab);
	}

	// --- снимки: время только в бою, новый раунд — сброс ---
	{
		FCornerTally T(0);
		FFightSnapshot Snap;
		Snap.Round = 1;
		Snap.Phase = EFightPhase::Fighting;
		Snap.Distance = 1.0f;
		Snap.Fighters[0].RopeLevel = 1;
		Snap.Fighters[0].StaminaPct = 80.f;
		for (int32 I = 0; I < 10; ++I) T.Sample(Snap, 0.1f, I >= 3 && I < 6);
		Snap.Fighters[0].RopeLevel = 0;
		Snap.Distance = 1.3f;
		T.Sample(Snap, 0.1f, false);
		T.Sample(Snap, 0.1f, true); // второй фронт «читает»
		Snap.Phase = EFightPhase::Down;
		Snap.Fighters[0].RopeLevel = 2;
		T.Sample(Snap, 5.f);
		const FRoundStats& S = T.Stats();
		TestTrue(TEXT("fightTime 1.2"), FMath::IsNearlyEqual(S.FightTime, 1.2f, 1e-4f));
		TestTrue(TEXT("ropesTime 1.0"), FMath::IsNearlyEqual(S.RopesTime, 1.0f, 1e-4f));
		TestTrue(TEXT("closeTime 1.0"), FMath::IsNearlyEqual(S.CloseTime, 1.0f, 1e-4f));
		TestEqual(TEXT("readHints"), S.ReadHints, 2);
		TestEqual(TEXT("stamina"), S.Stamina, 80.f);
		T.Feed(CaEv(EFightEventKind::Hit, 0));
		Snap.Round = 2;
		Snap.Phase = EFightPhase::Fighting;
		T.Sample(Snap, 0.1f);
		TestEqual(TEXT("новый раунд"), T.Stats().Round, 2);
		TestEqual(TEXT("сброс попаданий"), T.Stats().Landed, 0);
	}

	// --- итог раунда у судей: 3 и 5 судей ---
	{
		TestTrue(TEXT("3:0 — всех трёх"), CaVerdict3({{10, 9}, {10, 9}, {10, 9}}).Text.Contains(TEXT("всех трёх")));
		TestTrue(TEXT("2 судьи из 3"), CaVerdict3({{10, 9}, {9, 10}, {10, 9}}).Text.Contains(TEXT("2 судьи из 3")));
		TestTrue(TEXT("за ним"), CaVerdict3({{9, 10}, {9, 10}, {10, 10}}).Tone == ETone::Bad);
		TestTrue(TEXT("10–10"), CaVerdict3({{10, 10}, {10, 10}, {10, 10}}).Text.Contains(TEXT("10–10")));
		TestTrue(TEXT("разошлись"), CaVerdict3({{10, 9}, {9, 10}, {10, 10}}).Text.Contains(TEXT("разошлись")));
		TestTrue(TEXT("5 судей: 5:0 — всех пяти"), CaVerdict3({{10, 9}, {10, 9}, {10, 9}, {10, 9}, {10, 9}}).Text.Contains(TEXT("всех пяти")));
		TestTrue(TEXT("5 судей: 3:2 — 3 судьи из 5"), CaVerdict3({{10, 9}, {9, 10}, {10, 9}, {9, 10}, {10, 9}}).Text.Contains(TEXT("3 судьи из 5")));
		FGender Fm;
		Fm.bFoeFemale = true;
		TestTrue(TEXT("за ней 4 судьи из 5"), CaVerdict3({{9, 10}, {9, 10}, {10, 9}, {9, 10}, {9, 10}}, Fm).Text.Contains(TEXT("за ней — 4 судьи из 5")));
		TestFalse(TEXT("пустые карты — не судили"), CaVerdict3({{0, 0}, {0, 0}, {0, 0}}).bValid);
		FJudgeCard Tot[2] = {{29, 28}, {28, 29}}, Prev[2] = {{19, 19}, {19, 19}}, Out[2];
		RoundCards(Tot, Prev, 2, Out);
		TestTrue(TEXT("карты раунда = разность сумм"), Out[0].Red == 10 && Out[0].Blue == 9 && Out[1].Red == 9 && Out[1].Blue == 10);
	}

	// --- советы ---
	{
		const FVerdict Even = CaVerdict3({{10, 10}, {10, 10}, {10, 10}});
		const FTalk Kd = CornerTalk(CaStats([](FRoundStats& S) { S.KdTaken = 1; S.Stamina = 20; }), Even, FGender());
		TestTrue(TEXT("нокдаун — первым"), Kd.Tips[0].Text.Contains(TEXT("на полу")) && Kd.Tips[0].Tone == ETone::Bad);
		TestTrue(TEXT("потом усталость"), Kd.Tips[1].Text.Contains(TEXT("устал")));
		TestTrue(TEXT("устал — дистанция"), CornerTalk(CaStats([](FRoundStats& S) { S.Stamina = 22; }), Even, FGender()).Tips[0].Text.Contains(TEXT("дистанц")));
		TestTrue(TEXT("пустой бак — устал"), CornerTalk(CaStats([](FRoundStats& S) { S.GassedHits = 2; S.Stamina = 60; }), Even, FGender()).Tips[0].Text.Contains(TEXT("устал")));

		FCornerTally T(0);
		for (int32 I = 0; I < 10; ++I) T.Feed(CaEv(EFightEventKind::Slipped, 0, 0.f, EPunchType::Jab));
		FFightSnapshot Snap;
		Snap.Fighters[0].StaminaPct = 80.f;
		Snap.Fighters[0].Health = 90.f;
		T.Sample(Snap, 1.f);
		TestTrue(TEXT("читает твой джеб"), CornerTalk(T.Stats(), Even, FGender()).Tips[0].Text.Contains(TEXT("читает твой джеб")));

		TestTrue(TEXT("бей в корпус"), CaAnyTip(CornerTalk(CaStats([](FRoundStats& S) { S.Thrown = 12; S.HeadThrown = 12; S.HeadBlocked = 7; S.Landed = 3; }), Even, FGender()), TEXT("корпус")));
		TestTrue(TEXT("канаты"), CornerTalk(CaStats([](FRoundStats& S) { S.RopesTime = 20; }), Even, FGender()).Tips[0].Text.Contains(TEXT("канат")));
		TestTrue(TEXT("выдохся — дави"), CornerTalk(CaStats([](FRoundStats& S) { S.FoeStamina = 20; S.Thrown = 20; }), Even, FGender()).Tips[0].Text.Contains(TEXT("выдохся")));

		const FTalk Won = CornerTalk(CaStats([](FRoundStats& S) { S.Thrown = 25; S.Landed = 12; S.FoeLanded = 6; }), CaVerdict3({{10, 9}, {10, 9}, {10, 9}}), FGender());
		TestTrue(TEXT("похвала — одна, good"), Won.Tips.Num() == 1 && Won.Tips[0].Tone == ETone::Good);
		const FTalk Lost = CornerTalk(CaStats([](FRoundStats& S) { S.Thrown = 25; S.Landed = 6; S.FoeLanded = 7; }), CaVerdict3({{9, 10}, {9, 10}, {9, 10}}), FGender());
		TestTrue(TEXT("проиграл — активность"), Lost.Tips[0].Text.Contains(TEXT("активност")));

		const FTalk Many = CornerTalk(CaStats([](FRoundStats& S) { S.Stamina = 20; S.ReadHints = 3; S.RopesTime = 30; S.Thrown = 20; }), CaVerdict3({{10, 9}, {10, 9}, {10, 9}}), FGender());
		int32 Trainer = 0;
		bool bNoGood = true, bCut = false;
		for (const FTip& X : Many.Tips)
		{
			Trainer += X.Who == EWho::Trainer;
			bNoGood &= !(X.Who == EWho::Trainer && X.Tone == ETone::Good);
			bCut |= X.Who == EWho::Cutman;
		}
		TestEqual(TEXT("тренер — не больше MAX_TIPS"), Trainer, MAX_TIPS);
		TestTrue(TEXT("похвала не перебивает тактику"), bNoGood);
		TestFalse(TEXT("катмен — только если побит"), bCut);
		const FTalk Hurt = CornerTalk(CaStats([](FRoundStats& S) { S.Health = 35; S.Thrown = 20; }), Even, FGender());
		TestTrue(TEXT("побит — катмен последним"), Hurt.Tips.Last().Who == EWho::Cutman && Hurt.Tips.Last().Tone == ETone::Bad);

		// род
		FGender FF;
		FF.bMeFemale = true;
		FF.bFoeFemale = true;
		const FTalk Fem = CornerTalk(CaStats([](FRoundStats& S) { S.Stamina = 20; S.FoeStamina = 20; S.Thrown = 20; }), CaVerdict3({{9, 10}, {9, 10}, {9, 10}}, FF), FF);
		TestTrue(TEXT("Ты устала"), Fem.Tips[0].Text.StartsWith(TEXT("Ты устала")));
		TestTrue(TEXT("Она выдохлась"), Fem.Tips[1].Text.StartsWith(TEXT("Она выдохлась")));
		TestTrue(TEXT("за ней"), Fem.Verdict.Text.Contains(TEXT("за ней")));
		FGender MF;
		MF.bFoeFemale = true;
		TestTrue(TEXT("Ты устал (М)"), CornerTalk(CaStats([](FRoundStats& S) { S.Stamina = 20; }), FVerdict(), MF).Tips[0].Text.StartsWith(TEXT("Ты устал ")));
		const FRoundStats Far = CaStats([](FRoundStats& S) { S.Thrown = 20; S.Landed = 2; S.FoeLanded = 8; S.LongTime = 30; S.CloseTime = 5; });
		TestTrue(TEXT("её джеб"), CaAnyTip(CornerTalk(Far, FVerdict(), MF), TEXT("её джеб")));
		TestTrue(TEXT("его джеб"), CaAnyTip(CornerTalk(Far, FVerdict(), FGender()), TEXT("его джеб")));

		const FTalk Empty = CornerTalk(CaStats([](FRoundStats& S) { S.FightTime = 5; }), FVerdict(), FGender());
		TestTrue(TEXT("пустой раунд — совет по умолчанию"), Empty.Tips.Num() == 1 && !Empty.Verdict.bValid);
		TestEqual(TEXT("строка статистики"), StatsLine(CaStats([](FRoundStats& S) { S.Thrown = 10; S.Landed = 4; S.FoeLanded = 3; })), FString(TEXT("попадания 4–3 · точность 40%")));
		TestTrue(TEXT("строка: нокдауны"), StatsLine(CaStats([](FRoundStats& S) { S.Thrown = 10; S.Landed = 4; S.FoeLanded = 3; S.KdGiven = 1; })).Contains(TEXT("нокдауны 1–0")));
	}

	// --- настоящий бой ядра: копилка сходится с событиями, карты раунда (разность сумм) = карты итога ---
	for (int32 Pro = 0; Pro < 2; ++Pro)
	{
		for (const uint32 Seed : {3u, 11u, 29u})
		{
			FBoxingFightCore Core;
			Core.Init(CaCfg(Seed, Pro == 1));
			FCornerTally T(0);
			TArray<TArray<FJudgeCard>> Rounds;
			FJudgeCard Prev[MAX_JUDGES];
			int32 LandedEv = 0, Talks = 0, NumJ = 0;
			bool bInBreak = false;
			for (int32 I = 0; I < 40000; ++I)
			{
				Core.Tick(0.05f);
				for (const FFightEvent& E : Core.PollEvents())
				{
					T.Feed(E);
					LandedEv += E.Kind == EFightEventKind::Hit && E.Attacker == 0;
				}
				const FFightSnapshot S = Core.GetSnapshot();
				T.Sample(S, 0.05f);
				NumJ = S.NumJudges;
				if (S.Phase == EFightPhase::Between && !bInBreak)
				{
					bInBreak = true;
					FJudgeCard Cards[MAX_JUDGES];
					RoundCards(S.JudgeTotals, Prev, S.NumJudges, Cards);
					FMemory::Memcpy(Prev, S.JudgeTotals, sizeof(Prev));
					Rounds.Add(TArray<FJudgeCard>(Cards, S.NumJudges));
					const FVerdict V = RoundVerdict(Cards, S.NumJudges, FGender());
					const FTalk Talk = CornerTalk(T.Stats(), V, FGender());
					TestTrue(TEXT("есть совет"), Talk.Tips.Num() > 0);
					TestTrue(TEXT("есть итог раунда"), V.bValid);
					TestEqual(TEXT("попадания копилки = события ядра"), T.Stats().Landed, LandedEv);
					++Talks;
				}
				if (S.Phase != EFightPhase::Between)
				{
					if (bInBreak) LandedEv = 0;
					bInBreak = false;
				}
				if (S.Phase == EFightPhase::Over) break;
			}
			TestEqual(TEXT("судей: любители 5, профи 3"), NumJ, Pro ? 3 : 5);
			const FFightResult& R = Core.GetResult();
			TestTrue(TEXT("перерывы были"), Talks > 0 || R.StoppedRound == 1);
			for (int32 K = 0; K < Rounds.Num() && K < R.Rounds.Num(); ++K)
			{
				for (int32 J = 0; J < NumJ; ++J)
				{
					TestTrue(*FString::Printf(TEXT("карта раунда %d судьи %d (seed %u)"), K + 1, J + 1, Seed),
						Rounds[K][J].Red == R.Rounds[K].JudgeCards[J].Red && Rounds[K][J].Blue == R.Rounds[K].JudgeCards[J].Blue);
				}
			}
		}
	}
	return true;
}

#endif
