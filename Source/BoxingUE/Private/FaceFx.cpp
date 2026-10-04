#include "FaceFx.h"

namespace FaceFxImpl
{
	// Нормировка (damage.ts): к моменту, когда снята ~половина здоровья, лицо заметно побито.
	constexpr float MAG_NORM = 1.25f;
	// Рассечение открывается только на уже «набитой» брови и от сильных ударов.
	constexpr float CUT_EYE_GATE = 0.3f;
	constexpr float CUT_MIN_K = 1.2f;

	void FaceAdd(float& V, float D) { V = FMath::Clamp(V + D, 0.f, 1.f); }

	float FaceApproach(float Cur, float Target, float Rate, float Dt)
	{
		return Cur + (Target - Cur) * (1.f - FMath::Exp(-Rate * Dt));
	}
}
using namespace FaceFxImpl;

// ---------------------------------------------------------------------------------------------
// Повреждения (порт damage.ts)
// ---------------------------------------------------------------------------------------------

void FBoxerFaceDamage::ApplyHit(float Mag, EPunch Punch, bool bRear, bool bBody, bool bBlocked)
{
	const float K = FMath::Max(0.f, Mag) / MAG_NORM;
	if (K <= 0.f)
	{
		return;
	}
	// Сторона лица защитника: передняя рука бьёт в правую половину, задняя — в левую.
	const bool bL = bRear;
	float& Eye = bL ? EyeL : EyeR;
	float& Cheek = bL ? CheekL : CheekR;
	float& Cut = bL ? CutL : CutR;
	float& BodyS = bL ? BodyL : BodyR;
	float& BodyO = bL ? BodyR : BodyL;
	if (bBlocked)
	{
		// Через перчатки проходит немного — лишь лёгкое покраснение.
		if (bBody)
		{
			FaceAdd(BodyS, 0.012f * K);
		}
		else
		{
			FaceAdd(Head, 0.008f * K);
		}
		return;
	}
	if (bBody)
	{
		if (Punch == EPunch::Hook || Punch == EPunch::Uppercut)
		{
			FaceAdd(BodyS, 0.07f * K);
		}
		else
		{
			FaceAdd(BodyS, 0.035f * K);
			FaceAdd(BodyO, 0.02f * K);
		}
		return;
	}
	FaceAdd(Head, 0.022f * K);
	switch (Punch)
	{
	case EPunch::Jab:
	case EPunch::Cross:
	{
		const float W = Punch == EPunch::Jab ? 0.7f : 1.f;
		FaceAdd(Nose, 0.05f * K * W);
		FaceAdd(Eye, 0.035f * K * W);
		FaceAdd(Cheek, 0.03f * K * W);
		FaceAdd(Mouth, 0.015f * K * W);
		break;
	}
	case EPunch::Hook:
		FaceAdd(Cheek, 0.07f * K);
		FaceAdd(Eye, 0.05f * K);
		break;
	case EPunch::Uppercut:
		FaceAdd(Mouth, 0.07f * K);
		FaceAdd(Nose, 0.03f * K);
		break;
	}
	// Рассечение брови: сильный удар (кросс/хук) по уже набитому глазу.
	if ((Punch == EPunch::Cross || Punch == EPunch::Hook) && K > CUT_MIN_K && Eye > CUT_EYE_GATE)
	{
		FaceAdd(Cut, 0.12f * (K - CUT_MIN_K + 0.3f));
	}
}

void FBoxerFaceDamage::ApplyKnockdown()
{
	FaceAdd(Head, 0.15f);
	FaceAdd(Nose, 0.15f);
	FaceAdd(Mouth, 0.12f);
	FaceAdd(CheekL, 0.08f);
	FaceAdd(CheekR, 0.08f);
	FaceAdd(EyeL, 0.05f);
	FaceAdd(EyeR, 0.05f);
}

void FBoxerFaceDamage::BetweenRounds()
{
	Nose *= 0.75f;
	Mouth *= 0.8f;
	CheekL *= 0.93f;
	CheekR *= 0.93f;
	BodyL *= 0.95f;
	BodyR *= 0.95f;
	EyeL = FMath::Max(0.f, EyeL - 0.02f); // энсвелл
	EyeR = FMath::Max(0.f, EyeR - 0.02f);
}

float FBoxerFaceDamage::Max() const
{
	return FMath::Max(FMath::Max3(Head, EyeL, EyeR), FMath::Max3(CutL, CutR, Nose), FMath::Max3(Mouth, CheekL, CheekR));
}

// ---------------------------------------------------------------------------------------------
// Мимика
// ---------------------------------------------------------------------------------------------

void FBoxerFaceFx::OnHit(float Mag, bool bBody, bool bBlocked)
{
	// В блок — вполсилы; слабое — без гримасы (джебы в перчатки не дёргают лицо каждый раз).
	const float M = bBlocked ? Mag * 0.5f : Mag;
	if (M < GRIMACE_MIN_MAG)
	{
		return;
	}
	const float S = FMath::Clamp((M - GRIMACE_MIN_MAG) / 1.3f + 0.3f, 0.f, 1.f);
	if (S >= GrimaceA * 0.8f)
	{
		GrimaceA = FMath::Max(GrimaceA, S);
		GrimaceT = 0.f;
		bGrimaceBody = bBody;
	}
	if (!bBlocked && !bBody && Mag >= DAZE_MAG)
	{
		DazeLeft = FMath::Max(DazeLeft, DAZE_S * FMath::Clamp(Mag / DAZE_MAG, 1.f, 1.6f));
	}
}

void FBoxerFaceFx::OnKnockdown()
{
	DazeLeft = FMath::Max(DazeLeft, DAZE_KD_S);
	GrimaceA = 1.f;
	GrimaceT = 0.f;
	bGrimaceBody = false;
}

void FBoxerFaceFx::Update(float Dt, const FBoxerFaceInput& In)
{
	Dt = FMath::Clamp(Dt, 0.f, 0.1f);
	T += Dt;

	// --- гримаса: атака 0.05 с, держится ~0.15 с, спад ~0.6 с ---
	GrimaceT += Dt;
	const float GAtt = FMath::Clamp(GrimaceT / 0.05f, 0.f, 1.f);
	if (GrimaceT > 0.2f)
	{
		GrimaceA = FMath::Max(0.f, GrimaceA - Dt * 1.6f);
	}
	const float G = GrimaceA * GAtt;

	// --- «поплыл»: после тяжёлого / нокдауна, лёжа — всегда ---
	DazeLeft = FMath::Max(0.f, DazeLeft - Dt);
	const float DazeT = (In.bDown || In.bKO) ? 1.f : FMath::Clamp(DazeLeft / 1.2f, 0.f, 1.f) * (In.bStunned ? 1.f : 0.85f);
	DazeW = FaceApproach(DazeW, DazeT, DazeT > DazeW ? 10.f : 1.5f, Dt);

	// --- усталость: рот приоткрыт, дыхание (чаще и глубже на пустом баке) ---
	const float TiredT = FMath::Clamp((TIRED_FROM - In.Stamina) / TIRED_FROM, 0.f, 1.f);
	TiredW = FaceApproach(TiredW, TiredT, 2.f, Dt);
	const float BreathHz = 0.45f + 0.55f * TiredW;
	const float Breath = 0.5f + 0.5f * FMath::Sin(2.f * PI * BreathHz * T);

	// --- свой силовой: сжатые зубы на замахе, выдох «пф» после контакта ---
	const bool bContact = In.bPunching && In.PunchPhase >= In.ContactFrac;
	if (In.bPunching && In.bPowerPunch && bContact && !bPrevContact)
	{
		ExhaleT = 0.f;
	}
	bPrevContact = bContact;
	bWasPunching = In.bPunching;
	const float ClenchT = (In.bPunching && In.bPowerPunch && !bContact) ? 1.f : 0.f;
	ClenchW = FaceApproach(ClenchW, ClenchT, ClenchT > ClenchW ? 18.f : 8.f, Dt);
	float Exhale = 0.f;
	if (ExhaleT >= 0.f)
	{
		ExhaleT += Dt;
		Exhale = ExhaleT < 0.05f ? ExhaleT / 0.05f : FMath::Max(0.f, 1.f - (ExhaleT - 0.05f) / 0.3f);
		if (ExhaleT > 0.4f)
		{
			ExhaleT = -1.f;
		}
	}

	// --- моргание (детерминированно): раз в BLINK_EVERY с, 0.14 с; уставший/поплывший — реже и медленнее ---
	BlinkT += Dt * (1.f - 0.4f * DazeW);
	float Blink = 0.f;
	{
		const float Ph = FMath::Fmod(BlinkT, BLINK_EVERY);
		const float Dur = 0.14f * (1.f + DazeW);
		if (Ph < Dur)
		{
			Blink = FMath::Sin(PI * Ph / Dur);
		}
	}

	// --- сборка каналов ---
	FBoxerFaceFrame F;
	F.bOn = true;
	const float GHead = bGrimaceBody ? 0.f : G;
	const float GBody = bGrimaceBody ? G : 0.f;
	// Победа — лицо отпускает (без боли и усталости, лишь дыхание).
	const float Pain = In.bVictory ? 0.f : 1.f;
	F[EBoxFaceCh::JawOpen] = FMath::Clamp(TiredW * (0.12f + 0.22f * Breath) + DazeW * 0.28f + Exhale * 0.12f + GHead * 0.1f - ClenchW * 0.3f, 0.f, 0.6f);
	F[EBoxFaceCh::Clench] = FMath::Clamp(ClenchW * 0.8f + GBody * 0.9f, 0.f, 1.f) * Pain;
	F[EBoxFaceCh::Stretch] = FMath::Clamp(GBody * 0.7f + GHead * 0.45f + TiredW * 0.12f * Breath, 0.f, 1.f) * Pain;
	F[EBoxFaceCh::BrowDown] = FMath::Clamp(ClenchW * 0.55f + G * 0.6f + TiredW * 0.15f, 0.f, 1.f) * Pain;
	F[EBoxFaceCh::BrowRaise] = FMath::Clamp(DazeW * 0.55f + TiredW * 0.25f * (1.f - G), 0.f, 1.f) * Pain;
	const float Sq = FMath::Clamp(G * 0.75f + ClenchW * 0.35f, 0.f, 1.f) * Pain;
	F[EBoxFaceCh::NoseWrinkle] = FMath::Clamp(G * 0.55f + ClenchW * 0.3f, 0.f, 1.f) * Pain;
	F[EBoxFaceCh::Funnel] = Exhale * 0.55f;
	// Веко сверху: моргание, зажмурился от удара в голову, тяжёлые веки (поплыл), отёк (глаз заплывает).
	const float Lid = FMath::Max(Blink, FMath::Max(GHead * 0.85f, DazeW * 0.45f));
	F[EBoxFaceCh::BlinkL] = FMath::Clamp(FMath::Max(Lid, Damage.EyeL * 0.6f), 0.f, 1.f);
	F[EBoxFaceCh::BlinkR] = FMath::Clamp(FMath::Max(Lid, Damage.EyeR * 0.6f), 0.f, 1.f);
	F[EBoxFaceCh::SquintL] = FMath::Clamp(Sq + Damage.EyeL * 0.5f, 0.f, 1.f);
	F[EBoxFaceCh::SquintR] = FMath::Clamp(Sq + Damage.EyeR * 0.5f, 0.f, 1.f);
	Out = F;
}

void BoxFace::CurvesFor(EBoxFaceCh Ch, TArray<FName>& OutNames, TArray<float>& OutWeights)
{
	OutNames.Reset();
	OutWeights.Reset();
	auto Both = [&](const TCHAR* Base, float W)
	{
		OutNames.Add(FName(*FString::Printf(TEXT("CTRL_expressions_%sL"), Base)));
		OutWeights.Add(W);
		OutNames.Add(FName(*FString::Printf(TEXT("CTRL_expressions_%sR"), Base)));
		OutWeights.Add(W);
	};
	auto One = [&](const TCHAR* Name, float W)
	{
		OutNames.Add(FName(*FString::Printf(TEXT("CTRL_expressions_%s"), Name)));
		OutWeights.Add(W);
	};
	switch (Ch)
	{
	case EBoxFaceCh::JawOpen: One(TEXT("jawOpen"), 1.f); break;
	case EBoxFaceCh::Clench:
		Both(TEXT("jawClench"), 1.f);
		One(TEXT("mouthLipsPressL"), 0.7f);
		One(TEXT("mouthLipsPressR"), 0.7f);
		Both(TEXT("mouthUpperLipRaise"), 0.35f); // оскал: зубы видны
		break;
	case EBoxFaceCh::Stretch:
		Both(TEXT("mouthStretch"), 1.f);
		Both(TEXT("mouthCornerDepress"), 0.5f);
		break;
	case EBoxFaceCh::BrowDown:
		Both(TEXT("browDown"), 1.f);
		Both(TEXT("browLateral"), 0.6f);
		break;
	case EBoxFaceCh::BrowRaise: Both(TEXT("browRaiseIn"), 1.f); break;
	case EBoxFaceCh::SquintL:
		One(TEXT("eyeSquintInnerL"), 1.f);
		One(TEXT("eyeCheekRaiseL"), 0.8f);
		break;
	case EBoxFaceCh::SquintR:
		One(TEXT("eyeSquintInnerR"), 1.f);
		One(TEXT("eyeCheekRaiseR"), 0.8f);
		break;
	case EBoxFaceCh::BlinkL: One(TEXT("eyeBlinkL"), 1.f); break;
	case EBoxFaceCh::BlinkR: One(TEXT("eyeBlinkR"), 1.f); break;
	case EBoxFaceCh::NoseWrinkle: Both(TEXT("noseWrinkle"), 1.f); break;
	case EBoxFaceCh::Funnel:
		One(TEXT("mouthFunnelUL"), 1.f);
		One(TEXT("mouthFunnelUR"), 1.f);
		One(TEXT("mouthFunnelDL"), 1.f);
		One(TEXT("mouthFunnelDR"), 1.f);
		break;
	default: break;
	}
}

void FBoxerFaceMap::Build(const TSet<FName>& Known)
{
	Unique.Reset();
	Entries.Reset();
	Missing.Reset();
	TArray<FName> Names;
	TArray<float> Ws;
	for (int32 C = 0; C < BOX_FACE_NUM; ++C)
	{
		BoxFace::CurvesFor(static_cast<EBoxFaceCh>(C), Names, Ws);
		for (int32 I = 0; I < Names.Num(); ++I)
		{
			if (Known.Num() > 0 && !Known.Contains(Names[I]))
			{
				Missing.AddUnique(Names[I]);
				continue;
			}
			int32 U = Unique.IndexOfByKey(Names[I]);
			if (U == INDEX_NONE)
			{
				if (Unique.Num() >= FBoxerFaceCurves::MAX)
				{
					continue;
				}
				U = Unique.Add(Names[I]);
			}
			FEntry E;
			E.Curve = U;
			E.Ch = C;
			E.W = Ws[I];
			Entries.Add(E);
		}
	}
	bBuilt = true;
}

void FBoxerFaceMap::Fill(const FBoxerFaceFrame& F, FBoxerFaceCurves& Out) const
{
	Out.Num = Unique.Num();
	for (int32 U = 0; U < Out.Num; ++U)
	{
		Out.Names[U] = Unique[U];
		Out.Values[U] = 0.f;
	}
	if (!F.bOn)
	{
		Out.Num = 0;
		return;
	}
	for (const FEntry& E : Entries)
	{
		Out.Values[E.Curve] = FMath::Min(1.f, Out.Values[E.Curve] + F.V[E.Ch] * E.W);
	}
}
