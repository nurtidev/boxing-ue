#include "FightAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundWave.h"

namespace
{
	const float MASTER = 0.9f;
	const float CROWD_BASE = 0.16f; // фон зала — тихо, под действием
	const TCHAR* MUTE_SECTION = TEXT("BoxingFx");
	const TCHAR* MUTE_KEY = TEXT("bMuted");

	float Clamp01(float X) { return FMath::Clamp(X, 0.f, 1.f); }
	float Jitter(float A) { return 1.f + (FMath::FRand() * 2.f - 1.f) * A; }

	const FName SET_HEAD(TEXT("head"));
	const FName SET_BODY(TEXT("body"));
	const FName SET_HEAVY(TEXT("heavy"));
	const FName SET_BLOCK(TEXT("block"));
	const FName SET_WHIFF(TEXT("whiff"));
	const FName SET_FALL(TEXT("fall"));
	const FName SET_BELL(TEXT("bell"));
	const FName SET_OOH(TEXT("ooh"));
	const FName SET_GASP(TEXT("gasp"));
	const FName SET_CHEER(TEXT("cheer"));
	const FName SET_CROWD(TEXT("crowd_loop"));
}

void UBoxingFightAudio::LoadSet(const FName Set, const TCHAR* Prefix, int32 N)
{
	FSet& S = Sets.FindOrAdd(Set);
	for (int32 I = 1; I <= FMath::Max(1, N); ++I)
	{
		const FString Name = N > 0 ? FString::Printf(TEXT("%s%d"), Prefix, I) : FString(Prefix);
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name);
		if (USoundWave* W = LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			S.Waves.Add(W);
			Keep.Add(W);
		}
	}
}

void UBoxingFightAudio::Init(UWorld* InWorld, bool bInLog)
{
	World = InWorld;
	bLog = bInLog;
	bool bCfgMute = false;
	if (GConfig)
	{
		GConfig->GetBool(MUTE_SECTION, MUTE_KEY, bCfgMute, GGameUserSettingsIni);
	}
	bMuted = bCfgMute || FParse::Param(FCommandLine::Get(), TEXT("BoxMute"));
	LoadSet(SET_HEAD, TEXT("head"), 5);
	LoadSet(SET_BODY, TEXT("body"), 5);
	LoadSet(SET_HEAVY, TEXT("heavy"), 2);
	LoadSet(SET_BLOCK, TEXT("block"), 4);
	LoadSet(SET_WHIFF, TEXT("whiff"), 5);
	LoadSet(SET_FALL, TEXT("fall"), 2);
	LoadSet(SET_BELL, TEXT("bell"), 0);
	LoadSet(SET_OOH, TEXT("ooh"), 3);
	LoadSet(SET_GASP, TEXT("gasp"), 0);
	LoadSet(SET_CHEER, TEXT("cheer"), 0);
	LoadSet(SET_CROWD, TEXT("crowd_loop"), 0);
	Swell = FMath::FRand() * 10.f;
	UE_LOG(LogTemp, Log, TEXT("SFX: загружено наборов %d из %d, сэмплов %d (%s)%s"), NumLoadedSets(), Sets.Num(), Keep.Num(), *Folder,
		bMuted ? TEXT(", звук выкл.") : TEXT(""));
}

int32 UBoxingFightAudio::NumLoadedSets() const
{
	int32 N = 0;
	for (const auto& KV : Sets)
	{
		N += KV.Value.Waves.Num() > 0 ? 1 : 0;
	}
	return N;
}

void UBoxingFightAudio::Shutdown()
{
	StopCrowd();
	Queue.Reset();
}

void UBoxingFightAudio::SetMuted(bool bMute)
{
	bMuted = bMute;
	if (GConfig)
	{
		GConfig->SetBool(MUTE_SECTION, MUTE_KEY, bMute, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	if (Crowd)
	{
		Crowd->SetVolumeMultiplier(bMute ? 0.f : FMath::Max(0.001f, CrowdLevel * MASTER));
	}
	if (bMute)
	{
		Queue.Reset();
	}
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX mute %d"), bMute ? 1 : 0);
	}
}

USoundWave* UBoxingFightAudio::Pick(const FName Set)
{
	FSet* S = Sets.Find(Set);
	if (!S || S->Waves.Num() == 0)
	{
		return nullptr;
	}
	int32 I = FMath::RandRange(0, S->Waves.Num() - 1);
	if (S->Waves.Num() > 1 && I == S->Last)
	{
		I = (I + 1) % S->Waves.Num(); // без повтора подряд
	}
	S->Last = I;
	return S->Waves[I];
}

void UBoxingFightAudio::PlayNow(USoundWave* W, float Vol, float Pitch, float Lowpass)
{
	UWorld* Wd = World.Get();
	if (!Wd || !W || bMuted)
	{
		return;
	}
	if (Lowpass > 0.f)
	{
		if (UAudioComponent* C = UGameplayStatics::CreateSound2D(Wd, W, Vol * MASTER, Pitch))
		{
			C->SetLowPassFilterEnabled(true);
			C->SetLowPassFilterFrequency(Lowpass);
			C->Play();
		}
		return;
	}
	UGameplayStatics::PlaySound2D(Wd, W, Vol * MASTER, Pitch);
}

void UBoxingFightAudio::Play(USoundWave* W, float Vol, float Pitch, float Delay, float Lowpass, const TCHAR* Tag)
{
	if (!W || bMuted)
	{
		return;
	}
	if (bLog && Tag)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX   %s %s vol=%.2f pitch=%.2f%s%s"), Tag, *W->GetName(), Vol, Pitch,
			Delay > 0.f ? *FString::Printf(TEXT(" +%.2fs"), Delay) : TEXT(""), Lowpass > 0.f ? *FString::Printf(TEXT(" lp=%.0f"), Lowpass) : TEXT(""));
	}
	if (Delay > 0.f)
	{
		Queue.Add({Now + Delay, W, Vol, Pitch, Lowpass});
		return;
	}
	PlayNow(W, Vol, Pitch, Lowpass);
}

void UBoxingFightAudio::Punch(float Mag, bool bBody, float Slow)
{
	const float W = Clamp01(0.25f + Mag / 2.2f);
	const FName Set = bBody ? SET_BODY : SET_HEAD;
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX punch %s mag=%.2f%s%s"), bBody ? TEXT("body") : TEXT("head"), Mag,
			Mag >= 1.1f ? TEXT(" +heavy") : TEXT(""), Mag >= 1.7f ? TEXT(" +ooh?") : TEXT(""));
	}
	if (USoundWave* Wv = Pick(Set))
	{
		// Голова — короткий хлёсткий транзиент (тихий по RMS) → полная шкала; мешок плотнее → тише.
		const float Trim = bBody ? 0.75f : 1.f;
		Play(Wv, Trim * (0.3f + 0.7f * W) * Jitter(0.08f), Jitter(0.06f) / Slow, 0.f, 0.f, TEXT("hit"));
		// Тяжёлый удар — «мясо» снизу (тише, ниже), чтобы акцент ощущался телом.
		if (Mag >= 1.1f)
		{
			if (USoundWave* Hv = Pick(SET_HEAVY))
			{
				Play(Hv, 0.12f + 0.25f * Clamp01((Mag - 1.1f) / 1.5f), (0.9f + FMath::FRand() * 0.08f) / Slow, 0.f, 0.f, TEXT("heavy"));
			}
		}
		if (Mag >= 1.7f)
		{
			React(0.35f + 0.35f * Clamp01((Mag - 1.7f) / 1.5f), 0.12f);
		}
	}
	Bump(FMath::Min(0.5f, Mag * 0.12f));
}

void UBoxingFightAudio::Block(float Mag)
{
	const float W = Clamp01(0.35f + Mag / 2.f);
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX block mag=%.2f"), Mag);
	}
	Play(Pick(SET_BLOCK), (0.1f + 0.22f * W) * Jitter(0.1f), Jitter(0.07f), 0.f, 0.f, TEXT("block")); // плотный сэмпл — тише ударов
}

void UBoxingFightAudio::Whiff()
{
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX whiff"));
	}
	Play(Pick(SET_WHIFF), 0.16f * Jitter(0.2f), Jitter(0.1f), 0.f, 0.f, TEXT("whiff"));
}

void UBoxingFightAudio::Gassed()
{
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX gassed"));
	}
	// Выдох: свист воздуха на две трети высоты, приглушённый.
	Play(Pick(SET_WHIFF), 0.14f, 0.62f * Jitter(0.04f), 0.f, 900.f, TEXT("gassed"));
}

void UBoxingFightAudio::Knockdown(bool bKO)
{
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX knockdown%s"), bKO ? TEXT(" KO") : TEXT(""));
	}
	Play(Pick(SET_FALL), 0.85f, Jitter(0.04f), 0.28f, 3200.f, TEXT("fall"));
	if (USoundWave* G = Pick(SET_GASP))
	{
		Play(G, bKO ? 0.36f : 0.3f, Jitter(0.03f), 0.05f, 0.f, TEXT("gasp"));
	}
	else
	{
		React(0.7f, 0.05f, true);
	}
	if (bKO)
	{
		Play(Pick(SET_CHEER), 0.36f, 1.f, 0.9f, 0.f, TEXT("cheer"));
	}
	LastReact = Now;
	Bump(1.f);
}

void UBoxingFightAudio::Cheer()
{
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX cheer"));
	}
	Play(Pick(SET_CHEER), 0.36f, 1.f, 0.2f, 0.f, TEXT("cheer"));
	LastReact = Now;
	Bump(1.f);
}

void UBoxingFightAudio::React(float Amt, float Delay, bool bForce)
{
	if (!bForce && Now - LastReact < 2.5)
	{
		return; // «у-ух» не чаще раза в 2.5 с
	}
	USoundWave* W = Pick(SET_OOH);
	if (!W)
	{
		return;
	}
	LastReact = Now;
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX react %.2f"), Amt);
	}
	Play(W, 0.42f * Amt, Jitter(0.04f), Delay, 0.f, TEXT("ooh"));
}

void UBoxingFightAudio::Bell(int32 N)
{
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("SFX bell x%d"), N);
	}
	USoundWave* B = Pick(SET_BELL);
	for (int32 K = 0; K < N; ++K)
	{
		Play(B, 0.7f * (K == 0 ? 1.f : 0.85f), Jitter(0.004f), K * 0.3f, 0.f, TEXT("bell"));
	}
}

void UBoxingFightAudio::StartCrowd()
{
	bCrowdWanted = true;
	UWorld* Wd = World.Get();
	if (!Wd || Crowd)
	{
		return;
	}
	USoundWave* W = Pick(SET_CROWD);
	if (!W)
	{
		return; // нет сэмпла — тишина, не шипение
	}
	W->bLooping = true; // на случай старого импорта без флага
	Crowd = UGameplayStatics::CreateSound2D(Wd, W, 1.f, 1.f, 0.f, nullptr, false, false);
	if (Crowd)
	{
		CrowdLevel = 0.001f;
		Crowd->SetVolumeMultiplier(bMuted ? 0.f : CrowdLevel);
		Crowd->Play(FMath::FRand() * W->Duration);
		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("SFX crowd start"));
		}
	}
}

void UBoxingFightAudio::StopCrowd()
{
	bCrowdWanted = false;
	if (Crowd)
	{
		Crowd->Stop();
		Crowd->DestroyComponent();
		Crowd = nullptr;
	}
}

void UBoxingFightAudio::Bump(float Amt)
{
	Excite = FMath::Min(1.f, Excite + Amt);
}

void UBoxingFightAudio::Update(float RealDt)
{
	Now += RealDt;
	// Отложенные звуки (падение, гонг ×3, реакции).
	for (int32 I = Queue.Num() - 1; I >= 0; --I)
	{
		if (Now >= Queue[I].At)
		{
			const FDelayed D = Queue[I];
			Queue.RemoveAtSwap(I);
			PlayNow(D.Wave, D.Vol, D.Pitch, D.Lowpass);
		}
	}
	// Фон зала: медленные волны + спад возбуждения; громкость узла — не чаще 10 раз/с, сглаженно (τ 0.25 с).
	Excite = FMath::Max(0.f, Excite - RealDt * 0.3f);
	Swell += RealDt;
	SinceCrowdUpd += RealDt;
	if (!Crowd || SinceCrowdUpd < 0.1f)
	{
		return;
	}
	const float Dt = SinceCrowdUpd;
	SinceCrowdUpd = 0.f;
	const float Wave = 0.5f + 0.3f * FMath::Sin(Swell * 0.23f) + 0.2f * FMath::Sin(Swell * 0.61f + 1.3f);
	const float Level = CROWD_BASE * (0.8f + 0.35f * Wave) + 0.3f * Excite;
	CrowdLevel += (Level - CrowdLevel) * (1.f - FMath::Exp(-Dt / 0.25f));
	Crowd->SetVolumeMultiplier(bMuted ? 0.f : FMath::Max(0.001f, CrowdLevel * MASTER));
}
