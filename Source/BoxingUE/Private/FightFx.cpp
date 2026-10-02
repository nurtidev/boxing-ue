#include "FightFx.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "FightAudio.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UISettings.h"

// =============================================================================================
// Чистая логика (порт web/src/ui/fightFx.ts)
// =============================================================================================

namespace BoxFx
{
	EKind KindOf(EFightEventKind K)
	{
		switch (K)
		{
		case EFightEventKind::Hit: return EKind::Land;
		case EFightEventKind::Blocked: return EKind::Block;
		case EFightEventKind::Miss:
		case EFightEventKind::Slipped: return EKind::Miss;
		case EFightEventKind::Knockdown: return EKind::Kd;
		default: return EKind::Other;
		}
	}

	float HitStopMs(EKind Kind, float Mag, const FProfile& P)
	{
		if (Kind == EKind::Kd || Kind == EKind::Ko)
		{
			return P.StopKdMs;
		}
		if (Kind != EKind::Land || Mag < P.FreezeMagMin)
		{
			return 0.f;
		}
		const float U = FMath::Min(1.f, (Mag - P.FreezeMagMin) / FMath::Max(1e-3f, P.StopSpanMag));
		return FMath::RoundToFloat(P.StopMinMs + (P.StopMaxMs - P.StopMinMs) * U);
	}

	FCamKick CameraKick(EKind Kind, float Mag)
	{
		FCamKick K;
		if (Kind == EKind::Kd || Kind == EKind::Ko)
		{
			K.Shake = 1.f;
			K.PunchIn = 1.f;
		}
		else if (Kind == EKind::Land)
		{
			const float M = FMath::Max(0.f, Mag);
			K.Shake = FMath::Min(0.7f, M * 0.3f);
			K.PunchIn = M >= HEAVY_MAG ? FMath::Min(0.8f, 0.3f + (M - HEAVY_MAG) * 0.45f) : 0.f;
		}
		else if (Kind == EKind::Block)
		{
			K.Shake = FMath::Min(0.15f, Mag * 0.1f);
		}
		return K;
	}

	FSlowMo SlowMoFor(EKind Kind, float Mag, const FProfile& P)
	{
		FSlowMo S;
		if (Kind == EKind::Kd || Kind == EKind::Ko)
		{
			S.Scale = SLOWMO_KD;
			S.Hold = 0.35f;
			S.Ease = SLOWMO_KD_S - 0.35f;
		}
		else if (Kind == EKind::Land && P.SlowHeavyMag > 0.f && Mag >= P.SlowHeavyMag)
		{
			S.Scale = 0.55f;
			S.Hold = 0.06f;
			S.Ease = 0.18f;
		}
		return S;
	}

	float SlowMoScale(const FSlowMo& S, float T)
	{
		if (!S.IsValid() || T < 0.f)
		{
			return 1.f;
		}
		if (T < S.Hold)
		{
			return S.Scale;
		}
		const float U = (T - S.Hold) / FMath::Max(1e-6f, S.Ease);
		if (U >= 1.f)
		{
			return 1.f;
		}
		const float E = U * U * (3.f - 2.f * U);
		return S.Scale + (1.f - S.Scale) * E;
	}

	float ReplaySpeed(float Rel)
	{
		if (Rel < -0.7f)
		{
			return 0.5f;
		}
		if (Rel < -0.3f)
		{
			return 0.5f - ((Rel + 0.7f) / 0.4f) * 0.22f; // 0.5 → 0.28
		}
		return 0.28f;
	}

	EHaptic HapticFor(EKind Kind, int32 Who, float Mag, int32 Me)
	{
		if (Kind == EKind::Kd || Kind == EKind::Ko)
		{
			return EHaptic::Heavy;
		}
		if (Kind == EKind::Land)
		{
			if (Who == Me)
			{
				return Mag >= HEAVY_MAG ? EHaptic::Heavy : (Mag >= 0.5f ? EHaptic::Medium : EHaptic::Light); // пропустил
			}
			return Mag >= HEAVY_MAG ? EHaptic::Medium : (Mag >= 0.6f ? EHaptic::Light : EHaptic::None); // попал сам
		}
		if (Kind == EKind::Block)
		{
			return Who == Me && Mag >= 0.3f ? EHaptic::Light : EHaptic::None;
		}
		return EHaptic::None;
	}

	float WrapDeg(float A)
	{
		return FMath::UnwindDegrees(A);
	}

	void KnockdownShot(const FVector& Body, const FVector& Stand, const FVector& RingCenter, int32& Side, FVector& OutCam, FVector& OutLook)
	{
		FVector V = Stand - Body;
		V.Z = 0.f;
		V = V.SizeSquared() > 1.f ? V.GetSafeNormal() : FVector(1.f, 0.f, 0.f);
		auto DirAt = [&](int32 Deg) { return V.RotateAngleAxis(static_cast<float>(Deg), FVector::UpVector); };
		auto Inside = [&](const FVector& C, float Slack)
		{
			return FMath::Abs(C.X - RingCenter.X) <= KD_SHOT_LIM + Slack && FMath::Abs(C.Y - RingCenter.Y) <= KD_SHOT_LIM + Slack;
		};
		// Side — выбранный угол (град) + 1000; 0 — не выбран. Держится весь нокдаун (тело при падении уезжает на ~0.6 м —
		// запас 80 см), пока камера не ушла далеко за апрон.
		if (Side == 0 || !Inside(Body - DirAt(Side - 1000) * KD_SHOT_DIST, 80.f))
		{
			// Перебор направлений: камера не дальше апрона, стоящий (нейтральный угол) — сбоку от центра кадра на
			// ~KD_SHOT_STAND_DEG (в кадре, но не под панелью счёта HUD); при прочих равных — ближе к центру ринга.
			int32 Best = static_cast<int32>(KD_SHOT_ANGLE_DEG);
			float BestScore = TNumericLimits<float>::Max();
			for (int32 Deg = -180; Deg < 180; Deg += 5)
			{
				const FVector D = DirAt(Deg);
				const FVector C = Body - D * KD_SHOT_DIST;
				// (ближе к телу не подходим: у угла ринга стоящий может оказаться у края кадра — лежащий важнее)
				const FVector ToStand = (Stand - C).GetSafeNormal2D();
				const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(D, ToStand)), -1.f, 1.f)));
				float Score = FMath::Abs(Off - KD_SHOT_STAND_DEG) + (Off > KD_SHOT_STAND_DEG + 8.f ? 60.f : 0.f);
				Score += Inside(C, 0.f) ? 0.f : 1000.f;
				Score += 0.01f * static_cast<float>(FVector::Dist2D(C, RingCenter));
				if (Score < BestScore)
				{
					BestScore = Score;
					Best = Deg;
				}
			}
			Side = Best + 1000;
		}
		const FVector D = DirAt(Side - 1000);
		FVector C = Body - D * KD_SHOT_DIST;
		C.X = FMath::Clamp(C.X, RingCenter.X - KD_SHOT_LIM - 80.f, RingCenter.X + KD_SHOT_LIM + 80.f);
		C.Y = FMath::Clamp(C.Y, RingCenter.Y - KD_SHOT_LIM - 80.f, RingCenter.Y + KD_SHOT_LIM + 80.f);
		C.Z = RingCenter.Z + KD_SHOT_HEIGHT;
		OutCam = C;
		// Взгляд — чуть за лежащим, низко: тело в нижней половине кадра, под панелью счёта HUD.
		OutLook = FVector(Body.X + D.X * 60.f, Body.Y + D.Y * 60.f, RingCenter.Z + 60.f);
	}

	void RestShot(int32 Corner, float Aspect, const FVector& At, const FVector& RingCenter, FVector& OutCam, FVector& OutLook)
	{
		const float S = Corner == 0 ? -1.f : 1.f;
		const FVector C(RingCenter.X + S * 263.f, RingCenter.Y + S * 263.f, RingCenter.Z);
		const FVector N = (RingCenter - C).GetSafeNormal2D(); // из угла к центру
		const float Portrait = FMath::Clamp((1.25f - Aspect) / (1.25f - 0.46f), 0.f, 1.f);
		const float Wide = FMath::Clamp((Aspect - 1.25f) / 0.35f, 0.f, 1.f);
		const float D = 290.f * (1.f + 0.45f * Portrait);
		const FVector R(N.Y, -N.X, 0.f); // вправо от взгляда «камера → угол»
		FVector Pos = C + N * D + R * 50.f;
		FVector Look = C + N * 25.f + R * (62.f * Wide);
		// Идёт к углу — кадр сдвинут вместе с ним (камера за ним, не навстречу).
		const FVector Dx(At.X - C.X, At.Y - C.Y, 0.f);
		const float Lim = 305.f + 120.f;
		Pos.X = FMath::Clamp(Pos.X + Dx.X, RingCenter.X - Lim, RingCenter.X + Lim);
		Pos.Y = FMath::Clamp(Pos.Y + Dx.Y, RingCenter.Y - Lim, RingCenter.Y + Lim);
		const float Out = FMath::Max3(0.f, static_cast<float>(FMath::Abs(Pos.X - RingCenter.X)) - 305.f, static_cast<float>(FMath::Abs(Pos.Y - RingCenter.Y)) - 305.f);
		Pos.Z = RingCenter.Z + 162.f + 30.f * Portrait + FMath::Min(50.f, Out * 0.45f);
		Look += Dx;
		Look.Z = RingCenter.Z + 88.f - 68.f * Portrait;
		OutCam = Pos;
		OutLook = Look;
	}
}

namespace
{
	const TCHAR* KindName(BoxFx::EKind K)
	{
		switch (K)
		{
		case BoxFx::EKind::Land: return TEXT("land");
		case BoxFx::EKind::Block: return TEXT("block");
		case BoxFx::EKind::Miss: return TEXT("miss");
		case BoxFx::EKind::Kd: return TEXT("kd");
		case BoxFx::EKind::Ko: return TEXT("ko");
		default: return TEXT("other");
		}
	}

	const TCHAR* HapticName(BoxFx::EHaptic H)
	{
		switch (H)
		{
		case BoxFx::EHaptic::Light: return TEXT("light");
		case BoxFx::EHaptic::Medium: return TEXT("medium");
		case BoxFx::EHaptic::Heavy: return TEXT("heavy");
		default: return TEXT("-");
		}
	}

	// Минимальная «заморозка» (0 CustomTimeDilation не любит часть компонентов: dt = 0 у них — «не тикать»).
	constexpr float FROZEN_DILATION = 0.001f;
	// Сводка для отчёта (-BoxFxLog).
	struct FFxStats
	{
		int32 Stops = 0;
		float StopMsSum = 0.f;
		float StopMsMax = 0.f;
		int32 Slows = 0;
		int32 Kicks = 0;
		int32 Jolts = 0;
		int32 Haptics = 0;
		int32 Bells = 0;
		int32 Replays = 0;
		int32 ReplayFrames = 0;
	};
	FFxStats GStats;
}

// =============================================================================================
// Подсистема
// =============================================================================================

UBoxingFightFx* UBoxingFightFx::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<UBoxingFightFx>() : nullptr;
}

bool UBoxingFightFx::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UBoxingFightFx::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	int32 On = 1;
	FParse::Value(Cmd, TEXT("BoxFx="), On);
	bEnabled = On != 0;
	FString Prof;
	if (FParse::Value(Cmd, TEXT("BoxFxProfile="), Prof) && Prof.Equals(TEXT("classic"), ESearchCase::IgnoreCase))
	{
		Profile = BoxFx::FProfile::Classic();
	}
	bLog = FParse::Param(Cmd, TEXT("BoxFxLog"));
	bNoReplay = FParse::Param(Cmd, TEXT("BoxNoReplay"));
	bReplayTest = FParse::Param(Cmd, TEXT("BoxReplayTest"));
	FParse::Value(Cmd, TEXT("BoxReplayShots="), ReplayShotsLeft);
	FParse::Value(Cmd, TEXT("BoxFxShots="), FxShotsLeft);
	GStats = FFxStats();
	if (bEnabled)
	{
		Audio = NewObject<UBoxingFightAudio>(this);
		Audio->Init(&InWorld, FParse::Param(Cmd, TEXT("BoxSfxLog")));
	}
	UE_LOG(LogTemp, Log, TEXT("FX: %s, профиль %s (стоп с mag %.2f: %.0f→%.0f мс, нокдаун %.0f мс; slow-mo на попадании %s)"),
		bEnabled ? TEXT("вкл.") : TEXT("ВЫКЛ (-BoxFx=0)"), Profile.StopKdMs > 100.f ? TEXT("classic") : TEXT("s36"), Profile.FreezeMagMin,
		Profile.StopMinMs, Profile.StopMaxMs, Profile.StopKdMs,
		Profile.SlowHeavyMag > 0.f ? *FString::Printf(TEXT("mag ≥ %.1f"), Profile.SlowHeavyMag) : TEXT("нет"));
}

void UBoxingFightFx::Deinitialize()
{
	if (bEnabled && (bLog || GStats.Stops > 0))
	{
		UE_LOG(LogTemp, Log, TEXT("FX СВОДКА: хит-стопов %d (ср. %.0f мс, макс. %.0f мс), slow-mo %d, толчков камеры %d (тяжёлых с наездом %d), вибраций %d, гонгов %d, повторов %d (кадров записи %d)"),
			GStats.Stops, GStats.Stops ? GStats.StopMsSum / GStats.Stops : 0.f, GStats.StopMsMax, GStats.Slows, GStats.Kicks, GStats.Jolts,
			GStats.Haptics, GStats.Bells, GStats.Replays, GStats.ReplayFrames);
	}
	if (Audio)
	{
		Audio->Shutdown();
	}
	Super::Deinitialize();
}

TStatId UBoxingFightFx::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBoxingFightFx, STATGROUP_Tickables);
}

ETickableTickType UBoxingFightFx::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UBoxingFightFx::IsTickable() const
{
	return bEnabled && Mode.IsValid();
}

bool UBoxingFightFx::IsMuted() const
{
	return Audio ? Audio->IsMuted() : true;
}

void UBoxingFightFx::SetMuted(bool bMute)
{
	if (Audio)
	{
		Audio->SetMuted(bMute);
	}
}

float UBoxingFightFx::CoreTimeScale() const
{
	if (!bEnabled)
	{
		return 1.f;
	}
	return (Freeze > 0.f || Replay.bPlaying) ? 0.f : 1.f;
}

void UBoxingFightFx::SetFightersFrozen(bool bFrozen)
{
	bFightersFrozen = bFrozen;
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM)
	{
		return;
	}
	const float D = bFrozen ? FROZEN_DILATION : 1.f;
	for (int32 I = 0; I < 2; ++I)
	{
		if (ABoxerCharacter* B = GM->GetBoxer(I))
		{
			B->CustomTimeDilation = D;
			// Видимый MetaHuman — child actor на меше бойца: у него свой тик.
			TArray<AActor*> Attached;
			B->GetAttachedActors(Attached, true, true);
			for (AActor* A : Attached)
			{
				A->CustomTimeDilation = D;
			}
		}
	}
}

void UBoxingFightFx::ApplyHaptic(BoxFx::EHaptic H)
{
	if (!BoxSettings::Vibration())
	{
		return; // выключена в настройках (S-59)
	}
	if (H == BoxFx::EHaptic::None || Clock - LastHapticAt < 0.07)
	{
		return; // антиспам 70 мс
	}
	LastHapticAt = Clock;
	++GStats.Haptics;
	float Intensity = 0.25f, Duration = 0.06f;
	if (H == BoxFx::EHaptic::Medium)
	{
		Intensity = 0.5f;
		Duration = 0.1f;
	}
	else if (H == BoxFx::EHaptic::Heavy)
	{
		Intensity = 1.f;
		Duration = 0.18f;
	}
	// Геймпад: вибромоторы (на iOS/Android — та же точка входа ForceFeedback, если устройство умеет).
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->PlayDynamicForceFeedback(Intensity, Duration, true, true, true, true);
	}
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("FX haptic %s"), HapticName(H));
	}
}

void UBoxingFightFx::OnFightEvent(const FFightEvent& E, ABoxingFightGameMode* GM)
{
	Mode = GM;
	if (!bEnabled || !GM)
	{
		return;
	}
	BoxFx::EKind Kind = BoxFx::KindOf(E.Kind);
	const bool bBody = E.Target == EPunchTarget::Body;

	if (E.Kind == EFightEventKind::RoundEnd)
	{
		++GStats.Bells;
		Audio->Bell(3);
		return;
	}
	if (E.Kind == EFightEventKind::Gassed)
	{
		Audio->Gassed();
		return;
	}
	if (E.Kind == EFightEventKind::FightEnd)
	{
		const FFightResult& R = GM->GetCore().GetResult();
		if (R.Method != EFightMethod::KO && R.Method != EFightMethod::RSC)
		{
			return; // по очкам — гонг уже дал RoundEnd
		}
		const int32 Loser = R.WinnerIndex >= 0 ? 1 - R.WinnerIndex : -1;
		const bool bLoserDown = Loser >= 0 && GM->GetSnapshot().Fighters[Loser].bDown;
		// Досрочка в том же кадре, что и нокдаун (третий нокдаун / KO с удара) — стоп/провал уже дал нокдаун.
		if (KdFrame == GFrameCounter)
		{
			Audio->Cheer();
		}
		else
		{
			Kind = BoxFx::EKind::Ko;
			Audio->Cheer();
			Shake = FMath::Min(1.f, Shake + 0.5f);
		}
		// Повтор — только если проигравший лежит (сняли на ногах — повторять нечего).
		if (bLoserDown && !bNoReplay && (Replay.KdAt >= 0.0 || Replay.bClipReady))
		{
			Replay.bWanted = true;
			Replay.StartAt = Clock + 1.2; // дать досмотреть падение/счёт и реакцию зала
		}
		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("FX ko: метод %d, проигравший %d лежит %d, повтор %s"), static_cast<int32>(R.Method), Loser,
				bLoserDown ? 1 : 0, Replay.bWanted ? TEXT("будет") : TEXT("нет"));
		}
		if (Kind != BoxFx::EKind::Ko)
		{
			return;
		}
	}
	if (Kind == BoxFx::EKind::Other)
	{
		return;
	}

	// Who — по кому пришлось (нокдаун — кто упал).
	const int32 Who = E.Defender;
	const float Mag = E.Magnitude;

	// --- хит-стоп ---
	const float StopMs = BoxFx::HitStopMs(Kind, Mag, Profile);
	if (StopMs > 0.f)
	{
		Freeze = FMath::Max(Freeze, StopMs / 1000.f);
		SetFightersFrozen(true);
		++GStats.Stops;
		GStats.StopMsSum += StopMs;
		GStats.StopMsMax = FMath::Max(GStats.StopMsMax, StopMs);
		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("FX hitstop %.0f ms %s mag=%.2f%s t=%.2f"), StopMs, KindName(Kind), Mag, bBody ? TEXT(" body") : TEXT(""), E.Time);
		}
		if (FxShotsLeft > 0)
		{
			// Серия кадров: контакт (стоп-кадр), середина стопа, сразу после, отдача (реальное время).
			--FxShotsLeft;
			++FxShotIndex;
			for (const float D : {0.f, StopMs * 0.0005f, StopMs * 0.001f + 0.03f, StopMs * 0.001f + 0.15f, StopMs * 0.001f + 0.4f})
			{
				PendingShots.Add({Clock + D, FString::Printf(TEXT("fx_%02d_%s_%.0fms_t%03d"), FxShotIndex, KindName(Kind), StopMs, FMath::RoundToInt(D * 1000.f))});
			}
		}
	}
	// --- slow-mo ---
	const BoxFx::FSlowMo Sm = BoxFx::SlowMoFor(Kind, Mag, Profile);
	if (Sm.IsValid() && (!Slow.IsValid() || Sm.Scale <= BoxFx::SlowMoScale(Slow, SlowT)))
	{
		Slow = Sm;
		SlowT = 0.f;
		++GStats.Slows;
		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("FX slowmo x%.2f hold %.2f ease %.2f (%s mag=%.2f)"), Sm.Scale, Sm.Hold, Sm.Ease, KindName(Kind), Mag);
		}
	}
	// --- камера ---
	const BoxFx::FCamKick K = BoxFx::CameraKick(Kind, Mag);
	if (K.Shake > 0.f || K.PunchIn > 0.f)
	{
		++GStats.Kicks;
		Shake = FMath::Min(1.f, Shake + K.Shake);
		PunchIn = FMath::Max(PunchIn, K.PunchIn);
		if (K.PunchIn > 0.f && Kind == BoxFx::EKind::Land && E.Attacker >= 0)
		{
			// Камеру толкает туда, куда летит удар (от атакующего к защищающемуся).
			const ABoxerCharacter* A = GM->GetBoxer(E.Attacker);
			const ABoxerCharacter* D = GM->GetBoxer(E.Defender);
			if (A && D)
			{
				const FVector Dir = (D->GetActorLocation() - A->GetActorLocation()).GetSafeNormal2D();
				Jolt = Dir * BoxFx::CAM_JOLT_CM * K.PunchIn;
				++GStats.Jolts;
			}
		}
		if (bLog && (K.PunchIn > 0.f || Kind == BoxFx::EKind::Kd))
		{
			UE_LOG(LogTemp, Log, TEXT("FX cam shake+%.2f punchIn %.2f jolt %.1f cm (%s mag=%.2f)"), K.Shake, K.PunchIn, Jolt.Size(), KindName(Kind), Mag);
		}
	}
	// --- вибрация ---
	ApplyHaptic(BoxFx::HapticFor(Kind, Who, Mag, GM->GetPlayerIndex()));

	// --- звук ---
	switch (Kind)
	{
	case BoxFx::EKind::Land: Audio->Punch(Mag, bBody); break;
	case BoxFx::EKind::Block: Audio->Block(Mag); break;
	case BoxFx::EKind::Miss: Audio->Whiff(); break;
	case BoxFx::EKind::Kd:
		Audio->Knockdown(false);
		KdFrame = GFrameCounter;
		Replay.KdAt = RecT;
		break;
	default: break;
	}
}

void UBoxingFightFx::OnSnapshot(const FFightSnapshot& Snap, ABoxingFightGameMode* GM)
{
	Mode = GM;
	if (!bEnabled)
	{
		return;
	}
	if (bFirstSnap)
	{
		bFirstSnap = false;
		Audio->StartCrowd();
	}
	// Гонг начала раунда: выход из углов (стадия Out) или — без постановки — начало боевой фазы после перерыва/старта.
	const bool bOut = Snap.Stage.Kind == ERingStageKind::Out;
	const bool bStartNoStage = Snap.Phase == EFightPhase::Fighting && Snap.Stage.Kind == ERingStageKind::None &&
		(PrevPhase == EFightPhase::Between || PrevRound == 0);
	if ((bOut && PrevStage != ERingStageKind::Out) || (bStartNoStage && !bOut))
	{
		++GStats.Bells;
		Audio->Bell(1);
	}
	PrevPhase = Snap.Phase;
	PrevStage = Snap.Stage.Kind;
	PrevRound = Snap.Round;
}

void UBoxingFightFx::UpdateTimeDilation()
{
	UWorld* W = GetWorld();
	AWorldSettings* WS = W ? W->GetWorldSettings() : nullptr;
	if (!WS)
	{
		return;
	}
	const float Want = Replay.bPlaying ? 1.f : BoxFx::SlowMoScale(Slow, SlowT);
	if (FMath::Abs(Want - AppliedDilation) > 1e-3f)
	{
		AppliedDilation = Want;
		WS->SetTimeDilation(Want);
	}
}

bool UBoxingFightFx::ModifyCamera(FVector& Cam, FVector& Look, float& HFovDeg)
{
	if (!bEnabled)
	{
		return false;
	}
	bool bOverride = false;
	if (Replay.bPlaying && Replay.Clip.Num() > 1)
	{
		// Драматичный ракурс повтора: низко (чуть выше верхнего каната), сбоку от оси пары, медленный облёт.
		const FRecFrame& F = Replay.Clip[FMath::Clamp(Replay.CurIdx, 0, Replay.Clip.Num() - 1)];
		const FVector Mid = 0.5f * (F.F[0].Loc + F.F[1].Loc);
		const float Floor = Mode.IsValid() ? Mode->GetRingFloorCenter().Z : Mid.Z - 90.f;
		const float Ax = FMath::Atan2(F.F[1].Loc.Y - F.F[0].Loc.Y, F.F[1].Loc.X - F.F[0].Loc.X) + HALF_PI - 0.25f + Replay.Orbit;
		Cam = FVector(Mid.X + FMath::Cos(Ax) * 255.f, Mid.Y + FMath::Sin(Ax) * 255.f, Floor + 142.f);
		Look = FVector(Mid.X, Mid.Y, Floor + 95.f);
		bOverride = true;
	}
	else
	{
		// Перерыв: камера ведёт игрока к его углу (restShot веба), нокдаун — мягкий наезд на лежащего.
		if (RestMix > 1e-3f)
		{
			const float R = RestMix * RestMix * (3.f - 2.f * RestMix);
			Cam = FMath::Lerp(Cam, RestCam, R);
			Look = FMath::Lerp(Look, RestLook, R);
		}
		if (DownMix > 1e-3f)
		{
			const float D = DownMix * DownMix * (3.f - 2.f * DownMix);
			Cam = FMath::Lerp(Cam, KdCam, D);
			Look = FMath::Lerp(Look, KdLook, D);
		}
		// Наезд: камера ближе к паре (от точки взгляда) и чуть ниже; толчок по вектору удара гаснет вместе с наездом.
		const float Zoom = 1.f - 0.12f * PunchIn * PunchIn;
		FVector Off = Cam - Look;
		Off.X *= Zoom;
		Off.Y *= Zoom;
		Cam = Look + Off;
		Cam.Z -= 6.f * PunchIn;
		Cam += Jolt * PunchIn;
	}
	// Тряска: квадрат силы — мелкие удары почти не трясут.
	const float S = Shake * Shake;
	if (S > 1e-4f)
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, (Look - Cam).GetSafeNormal()).GetSafeNormal();
		const float Jx = FMath::Sin(Clock * 57.0) * 6.f * S;
		const float Jy = FMath::Sin(Clock * 83.0) * 4.5f * S;
		Cam += Right * Jx + FVector(0.f, 0.f, Jy);
		Look += Right * (Jx * 0.5f);
	}
	return bOverride;
}

// ---------------------------------------------------------------------------------------------
// Тик (реальное время)
// ---------------------------------------------------------------------------------------------

void UBoxingFightFx::Tick(float DeltaTime)
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	const float Real = FMath::Clamp(static_cast<float>(W->DeltaRealTimeSeconds), 0.f, 0.1f);
	const float Game = W->GetDeltaSeconds();
	Clock += Real;
	// Отладочные серии скриншотов (-BoxFxShots=N): не больше одного запроса за кадр.
	if (PendingShots.Num() > 0 && Clock >= PendingShots[0].Key)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") / (PendingShots[0].Value + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Log, TEXT("FX скриншот %s"), *Path);
		PendingShots.RemoveAt(0);
	}

	// Хит-стоп: время картинки бойцов — реальное.
	if (Freeze > 0.f)
	{
		Freeze -= Real;
		if (Freeze <= 0.f)
		{
			Freeze = 0.f;
			SetFightersFrozen(false);
		}
	}
	else if (bFightersFrozen && !Replay.bPlaying)
	{
		SetFightersFrozen(false);
	}
	// Slow-mo.
	if (Slow.IsValid())
	{
		SlowT += Real;
		if (BoxFx::SlowMoScale(Slow, SlowT) >= 1.f)
		{
			Slow = BoxFx::FSlowMo();
		}
	}
	UpdateTimeDilation();
	UpdateShots(Real);
	// Тряска и наезд — в реальном времени (во время хит-стопа камера «вздрагивает»).
	Shake = FMath::Max(0.f, Shake - Real * (Replay.bPlaying ? 1.5f : 3.2f));
	PunchIn = FMath::Max(0.f, PunchIn - Real * 4.5f);

	if (Audio)
	{
		Audio->Update(Real);
	}
	if (Replay.bPlaying)
	{
		UpdateReplay(Real);
	}
	else if (Freeze <= 0.f)
	{
		RecordFrame(Game);
	}
}

// ---------------------------------------------------------------------------------------------
// Повтор нокаута
// ---------------------------------------------------------------------------------------------

void UBoxingFightFx::RecordFrame(float GameDt)
{
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM || bNoReplay || GameDt <= 0.f)
	{
		return;
	}
	ABoxerCharacter* B[2] = {GM->GetBoxer(0), GM->GetBoxer(1)};
	if (!B[0] || !B[1] || !B[0]->GetMesh() || !B[1]->GetMesh())
	{
		return;
	}
	RecT += GameDt;
	const int32 Cap = 300; // 5 с при 60 к/с; окно повтора 3.2 с
	// Запись не чаще 60 к/с (без vsync кадров сотни — буфер покрывал бы доли секунды); между кадрами повтор интерполирует.
	if (RecT - LastRecT >= 1.0 / 60.0 - 1e-4)
	{
		LastRecT = RecT;
		if (Replay.Ring.Num() < Cap)
		{
			Replay.Ring.AddDefaulted(Cap - Replay.Ring.Num());
		}
		FRecFrame& Fr = Replay.Ring[Replay.Head];
		Fr.T = RecT;
		for (int32 I = 0; I < 2; ++I)
		{
			FBoxReplayFighter& R = Fr.F[I];
			R.Loc = B[I]->GetActorLocation();
			R.Yaw = B[I]->GetActorRotation().Yaw;
			R.Bones = B[I]->GetMesh()->GetBoneSpaceTransforms();
			R.Feel = B[I]->GetFeelFrame();
		}
		Replay.Head = (Replay.Head + 1) % Cap;
		Replay.Num = FMath::Min(Replay.Num + 1, Cap);
	}

	// Отрезок последнего нокдауна дописан — фиксируем клип.
	if (Replay.KdAt >= 0.0 && RecT >= Replay.KdAt + BoxFx::REPLAY_AFTER)
	{
		Replay.Clip.Reset();
		for (int32 K = 0; K < Replay.Num; ++K)
		{
			const FRecFrame& F = Replay.Ring[(Replay.Head - Replay.Num + K + Cap * 2) % Cap];
			if (F.T >= Replay.KdAt - BoxFx::REPLAY_BEFORE && F.T <= Replay.KdAt + BoxFx::REPLAY_AFTER)
			{
				Replay.Clip.Add(F);
			}
		}
		Replay.ClipKd = Replay.KdAt;
		Replay.KdAt = -1.0;
		Replay.bClipReady = Replay.Clip.Num() > 10;
		if (bReplayTest && Replay.bClipReady)
		{
			Replay.bWanted = true; // отладка: повтор после КАЖДОГО нокдауна (ядро на время повтора стоит, бой продолжится)
			Replay.StartAt = Clock;
		}
		if (bLog)
		{
			UE_LOG(LogTemp, Log, TEXT("FX replay clip: %d кадров, %.2f..%.2f с (нокдаун %.2f)"), Replay.Clip.Num(),
				Replay.Clip.Num() ? Replay.Clip[0].T : 0.0, Replay.Clip.Num() ? Replay.Clip.Last().T : 0.0, Replay.ClipKd);
		}
	}
	// Досрочка: клип готов и пауза прошла — повтор.
	if (Replay.bWanted && Replay.KdAt < 0.0 && Clock >= Replay.StartAt)
	{
		Replay.bWanted = false;
		if (Replay.bClipReady)
		{
			BeginReplay();
		}
	}
}

void UBoxingFightFx::BeginReplay()
{
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM || !GM->GetBoxer(0) || !GM->GetBoxer(1))
	{
		return;
	}
	Freeze = 0.f;
	SetFightersFrozen(false);
	Slow = BoxFx::FSlowMo();
	Replay.bPlaying = true;
	Replay.Cursor = static_cast<float>(Replay.Clip[0].T);
	Replay.CurIdx = 0;
	Replay.bImpactFired = false;
	Replay.Orbit = 0.f;
	Replay.ShotTimer = 0.f;
	++GStats.Replays;
	GStats.ReplayFrames = Replay.Clip.Num();
	for (int32 I = 0; I < 2; ++I)
	{
		GM->GetBoxer(I)->BeginReplayDrive();
	}
	ApplyReplayFrame(Replay.Cursor);
	UE_LOG(LogTemp, Log, TEXT("FX ПОВТОР нокаута: %d кадров, %.2f с записи (нокдаун на %.2f с от начала)"), Replay.Clip.Num(),
		Replay.Clip.Last().T - Replay.Clip[0].T, Replay.ClipKd - Replay.Clip[0].T);
}

void UBoxingFightFx::EndReplay()
{
	Replay.bPlaying = false;
	Replay.bClipReady = false;
	Replay.Clip.Reset();
	if (ABoxingFightGameMode* GM = Mode.Get())
	{
		for (int32 I = 0; I < 2; ++I)
		{
			if (ABoxerCharacter* B = GM->GetBoxer(I))
			{
				B->EndReplayDrive(); // живое финальное состояние (лежит / празднует) без повторного падения
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("FX повтор окончен"));
}

void UBoxingFightFx::SkipReplay()
{
	if (Replay.bPlaying)
	{
		EndReplay();
	}
	Replay.bWanted = false;
}

void UBoxingFightFx::UpdateReplay(float RealDt)
{
	if (Replay.Clip.Num() < 2)
	{
		EndReplay();
		return;
	}
	const float Speed = BoxFx::ReplaySpeed(Replay.Cursor - static_cast<float>(Replay.ClipKd));
	Replay.Cursor += FMath::Min(0.05f, RealDt) * Speed;
	Replay.Orbit += RealDt * 0.05f; // ≈ 0.45 рад за весь повтор
	if (!Replay.bImpactFired && Replay.Cursor >= Replay.ClipKd)
	{
		Replay.bImpactFired = true;
		Audio->Punch(3.2f, false, 1.8f); // замедленный «тук» в момент удара
		Audio->Knockdown(true);
		Shake = 0.6f;
	}
	if (Replay.Cursor >= Replay.Clip.Last().T)
	{
		EndReplay();
		return;
	}
	ApplyReplayFrame(Replay.Cursor);
	if (ReplayShotsLeft > 0)
	{
		Replay.ShotTimer -= RealDt;
		if (Replay.ShotTimer <= 0.f)
		{
			Replay.ShotTimer = 1.0f;
			--ReplayShotsLeft;
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") /
				FString::Printf(TEXT("fx_replay_%02d.png"), ++Replay.ShotIndex));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogTemp, Log, TEXT("FX скриншот повтора %s (t=%+.2f с от удара)"), *Path, Replay.Cursor - Replay.ClipKd);
		}
	}
}

void UBoxingFightFx::ApplyReplayFrame(float T)
{
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM)
	{
		return;
	}
	TArray<FRecFrame>& C = Replay.Clip;
	while (Replay.CurIdx < C.Num() - 2 && C[Replay.CurIdx + 1].T <= T)
	{
		++Replay.CurIdx;
	}
	const FRecFrame& A = C[Replay.CurIdx];
	const FRecFrame& B = C[Replay.CurIdx + 1];
	const double Span = B.T - A.T;
	const float U = Span > 1e-6 ? FMath::Clamp(static_cast<float>((T - A.T) / Span), 0.f, 1.f) : 0.f;
	for (int32 I = 0; I < 2; ++I)
	{
		ABoxerCharacter* Bx = GM->GetBoxer(I);
		if (!Bx)
		{
			continue;
		}
		const FBoxReplayFighter& Ra = A.F[I];
		const FBoxReplayFighter& Rb = B.F[I];
		TArray<FTransform>& Out = ReplayScratch[I];
		Out = Ra.Bones;
		if (Rb.Bones.Num() == Out.Num())
		{
			for (int32 K = 0; K < Out.Num(); ++K)
			{
				Out[K].Blend(Ra.Bones[K], Rb.Bones[K], U); // слоу-мо без «ступенек»
			}
		}
		FBoxerFeelFrame Feel = U < 0.5f ? Ra.Feel : Rb.Feel;
		for (int32 Ch = 0; Ch < BOX_REACT_NUM; ++Ch)
		{
			Feel.React[Ch] = FMath::Lerp(Ra.Feel.React[Ch], Rb.Feel.React[Ch], U);
		}
		if (Ra.Feel.bAim && Rb.Feel.bAim)
		{
			Feel.AimSurface = FMath::Lerp(Ra.Feel.AimSurface, Rb.Feel.AimSurface, U);
			Feel.AimWeight = FMath::Lerp(Ra.Feel.AimWeight, Rb.Feel.AimWeight, U);
			Feel.ReachWeight = FMath::Lerp(Ra.Feel.ReachWeight, Rb.Feel.ReachWeight, U);
		}
		const FVector Loc = FMath::Lerp(Ra.Loc, Rb.Loc, U);
		const float Yaw = Ra.Yaw + BoxFx::WrapDeg(Rb.Yaw - Ra.Yaw) * U;
		Bx->SetReplayFrame(Loc, Yaw, Out, Feel);
	}
}

void UBoxingFightFx::UpdateShots(float RealDt)
{
	ABoxingFightGameMode* GM = Mode.Get();
	if (!GM || Replay.bPlaying)
	{
		return;
	}
	const FFightSnapshot& Snap = GM->GetSnapshot();
	const FVector RC = GM->GetRingFloorCenter();
	// --- нокдаун: мягкий наезд на лежащего ---
	const int32 Dn = Snap.DownWho;
	const bool bDown = Snap.Phase == EFightPhase::Down && Dn >= 0 && Dn < 2 && GM->GetBoxer(0) && GM->GetBoxer(1);
	if (bDown)
	{
		const ABoxerCharacter* D = GM->GetBoxer(Dn);
		const ABoxerCharacter* S = GM->GetBoxer(1 - Dn);
		FVector Body = D->GetActorLocation();
		if (const USkeletalMeshComponent* M = D->GetFeelMesh())
		{
			const FName Pelvis(TEXT("pelvis")), Head(TEXT("head"));
			if (M->GetBoneIndex(Pelvis) != INDEX_NONE && M->GetBoneIndex(Head) != INDEX_NONE)
			{
				Body = 0.5f * (M->GetBoneLocation(Pelvis) + M->GetBoneLocation(Head)); // тело лежит, а не капсула
			}
		}
		// Стоящий — по ЦЕЛИ его хода (нейтральный угол, S-53), а не по текущему месту: кадр выбирается сразу и не
		// поворачивается, пока он идёт через ринг.
		const int32 Si = 1 - Dn;
		const FVector StandRef = (Snap.Stage.Kind == ERingStageKind::Neutral && Snap.Stage.bHasTarget[Si])
			? GM->FightToWorld(Snap.Stage.TargetX[Si], Snap.Stage.TargetZ[Si]) : S->GetActorLocation();
		const int32 WasSide = KdSide;
		BoxFx::KnockdownShot(Body, StandRef, RC, KdSide, KdCam, KdLook);
		if (bLog && KdSide != WasSide)
		{
			UE_LOG(LogTemp, Log, TEXT("FX kd-cam: лежит %d тело (%.0f, %.0f), стоящий → (%.0f, %.0f), угол %d, камера (%.0f, %.0f, %.0f)"), Dn, Body.X - RC.X, Body.Y - RC.Y,
				StandRef.X - RC.X, StandRef.Y - RC.Y, KdSide - 1000, KdCam.X - RC.X, KdCam.Y - RC.Y, KdCam.Z - RC.Z);
		}
	}
	DownMix += ((bDown ? 1.f : 0.f) - DownMix) * (1.f - FMath::Exp(-(bDown ? 1.8f : 2.4f) * RealDt));
	if (!bDown && DownMix < 0.01f)
	{
		DownMix = 0.f;
		KdSide = 0;
	}
	// --- перерыв: к углу игрока ---
	const bool bRest = Snap.Phase == EFightPhase::Between && Snap.Stage.Kind == ERingStageKind::Rest && GM->GetBoxer(0);
	if (bRest)
	{
		FVector2D Vp(16.f, 9.f);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Vp);
		}
		const float Aspect = Vp.Y > 0.f ? Vp.X / Vp.Y : 16.f / 9.f;
		BoxFx::RestShot(GM->GetPlayerIndex(), Aspect, GM->GetBoxer(GM->GetPlayerIndex())->GetActorLocation(), RC, RestCam, RestLook);
	}
	RestMix += ((bRest ? 1.f : 0.f) - RestMix) * (1.f - FMath::Exp(-(bRest ? 1.4f : 2.6f) * RealDt));
	if (!bRest && RestMix < 0.01f)
	{
		RestMix = 0.f;
	}
}
