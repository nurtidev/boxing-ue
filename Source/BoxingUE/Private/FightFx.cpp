#include "FightFx.h"

#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "FightAudio.h"
#include "BoxingFightHUD.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UISettings.h"
#include "UIWidgetBase.h" // S-75: BoxUi (цвета, шрифт) для подписи защиты
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

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
		if (Rel < 1.2f)
		{
			return 0.28f;
		}
		// S-66: тело легло — досматриваем «лежит» быстрее (повтор не затягивается от длинного хвоста).
		return FMath::Min(0.6f, 0.28f + (Rel - 1.2f) * 0.8f);
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

	void KnockdownShot(const FVector& Body, const FVector& Stand, const FVector& RingCenter, int32& Side, FVector& OutCam, FVector& OutLook,
		const FVector* PrefCam)
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
				if (PrefCam)
				{
					// S-78 (QA: на KO камера облетала ринг на полкруга и смотрела в трибуну) — сторона ближе к нынешней камере.
					const FVector Cur = (Body - *PrefCam).GetSafeNormal2D();
					if (!Cur.IsNearlyZero())
					{
						Score += KD_SHOT_TURN_COST * FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(D, Cur)), -1.f, 1.f)));
					}
				}
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

	void RestShot(int32 Corner, float Aspect, const FVector& At, const FVector& RingCenter, FVector& OutCam, FVector& OutLook, float HeightScale, float Seated)
	{
		const float S = Corner == 0 ? -1.f : 1.f;
		const FVector C(RingCenter.X + S * 263.f, RingCenter.Y + S * 263.f, RingCenter.Z);
		const FVector N = (RingCenter - C).GetSafeNormal2D(); // из угла к центру
		const float Portrait = FMath::Clamp((1.25f - Aspect) / (1.25f - 0.46f), 0.f, 1.f);
		const float Wide = FMath::Clamp((Aspect - 1.25f) / 0.35f, 0.f, 1.f);
		// S-62: боец в UE в углу стоит (стула нет) — кадр веба (сидящий) резал голову 198-см под панелью HUD: дальше
		// и выше по росту (не меньше, чем для 178 см).
		// S-71: боец сел на стул (Seated → 1) — снова кадр веба (сидящий ниже, угловые рядом): рост не важен.
		const float Sd = FMath::Clamp(Seated, 0.f, 1.f);
		const float Hs = FMath::Lerp(FMath::Clamp(HeightScale, 1.f, 1.25f), 1.f, Sd);
		const float D = 290.f * (1.f + 0.45f * Portrait) * Hs;
		const FVector R(N.Y, -N.X, 0.f); // вправо от взгляда «камера → угол»
		FVector Pos = C + N * D + R * 50.f;
		FVector Look = C + N * 25.f + R * (62.f * Wide);
		// Идёт к углу — кадр сдвинут вместе с ним (камера за ним, не навстречу).
		const FVector Dx(At.X - C.X, At.Y - C.Y, 0.f);
		const float Lim = 305.f + 120.f;
		Pos.X = FMath::Clamp(Pos.X + Dx.X, RingCenter.X - Lim, RingCenter.X + Lim);
		Pos.Y = FMath::Clamp(Pos.Y + Dx.Y, RingCenter.Y - Lim, RingCenter.Y + Lim);
		const float Out = FMath::Max3(0.f, static_cast<float>(FMath::Abs(Pos.X - RingCenter.X)) - 305.f, static_cast<float>(FMath::Abs(Pos.Y - RingCenter.Y)) - 305.f);
		Pos.Z = RingCenter.Z + (162.f + 30.f * Portrait) * Hs + FMath::Min(50.f, Out * 0.45f);
		Look += Dx;
		// Точка взгляда чуть выше, чем у веба (стоит, а не сидит): голова не под панелью раунда/счёта HUD.
		Look.Z = RingCenter.Z + (88.f + 17.f * (1.f - Portrait) * (1.f - Sd) - 68.f * Portrait) * Hs;
		OutCam = Pos;
		OutLook = Look;
	}

	void ResultShot(const FVector& Winner, const FVector& Loser, const FVector& Cam, const FVector& RingCenter, float HFovDeg, float HeightScale,
		FVector& OutCam, FVector& OutLook, const FVector& WinnerFwd)
	{
		const float Hs = FMath::Clamp(HeightScale, 0.85f, 1.25f);
		const float D = 310.f * Hs; // во весь рост: 16:9, вертикальный FOV ≈ 46°
		const float Lim = 330.f;    // камера внутри апрона (канаты у камеры прячет контроллер)
		FVector Base = (Cam - Winner).GetSafeNormal2D();
		if (Base.IsNearlyZero())
		{
			Base = FVector(1.f, 0.f, 0.f);
		}
		// Сторона — та же, что у нынешней камеры (без облёта через весь ринг); камера внутри апрона, проигравший — не
		// между камерой и победителем.
		FVector Best = Base;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 Deg = -120; Deg <= 120; Deg += 10)
		{
			const FVector Dir = Base.RotateAngleAxis(static_cast<float>(Deg), FVector::UpVector);
			const FVector C = Winner + Dir * D;
			float Score = FMath::Abs(Deg) * 0.01f;
			const float Out = static_cast<float>(FMath::Max(FMath::Abs(C.X - RingCenter.X), FMath::Abs(C.Y - RingCenter.Y))) - Lim;
			Score += Out > 0.f ? 10.f + Out * 0.1f : 0.f;
			FVector ToL = Loser - Winner;
			ToL.Z = 0.f;
			const float Along = static_cast<float>(FVector::DotProduct(ToL, Dir));
			const float Across = static_cast<float>((ToL - Dir * Along).Size());
			if (Along > 30.f && Along < D && Across < 60.f)
			{
				Score += 3.f; // проигравший заслонил бы победителя
			}
			if (!WinnerFwd.IsNearlyZero())
			{
				Score += 0.8f * (1.f - static_cast<float>(FVector::DotProduct(Dir, WinnerFwd.GetSafeNormal2D()))); // лицом к камере, а не спиной
			}
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Dir;
			}
		}
		FVector C = Winner + Best * D;
		C.Z = RingCenter.Z + 125.f * Hs;
		// Победитель — на RESULT_SCREEN_X ширины кадра: взгляд повёрнут вправо от направления на него.
		const float HalfTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HFovDeg, 30.f, 120.f) * 0.5f));
		const float Theta = FMath::RadiansToDegrees(FMath::Atan(HalfTan * (1.f - 2.f * RESULT_SCREEN_X)));
		const FVector ToW = (Winner - C).GetSafeNormal2D();
		const FVector LookDir = ToW.RotateAngleAxis(Theta, FVector::UpVector);
		OutCam = C;
		OutLook = FVector(C.X + LookDir.X * D, C.Y + LookDir.Y * D, RingCenter.Z + 95.f * Hs);
	}
}

// S-75: подсказки защиты (порт setCue веба: attack.ts / guard.ts, тексты — DEF_CUE InteractiveFight.tsx).
namespace BoxFx
{
	EDefCue DefenseCueFor(const FFightEvent& E, int32 Me)
	{
		if (Me < 0)
		{
			return EDefCue::None;
		}
		if (E.Kind == EFightEventKind::Slipped)
		{
			return E.Defender == Me ? EDefCue::Slip : EDefCue::None;
		}
		if (E.Kind == EFightEventKind::Clinch)
		{
			return EDefCue::Clinch;
		}
		if (E.Kind == EFightEventKind::Break)
		{
			return EDefCue::Break;
		}
		if (E.Kind != EFightEventKind::Hit)
		{
			return EDefCue::None;
		}
		if (E.bCounter && E.Attacker == Me)
		{
			return EDefCue::Counter;
		}
		if (E.bGuardBreak)
		{
			return E.Defender == Me ? EDefCue::GuardBreak : EDefCue::Broke;
		}
		if (E.bCaught && E.Defender == Me)
		{
			return EDefCue::Caught;
		}
		return EDefCue::None;
	}

	const TCHAR* DefenseCueText(EDefCue C)
	{
		switch (C)
		{
		case EDefCue::Slip: return TEXT("Уклон! Бей в ответ");
		case EDefCue::Counter: return TEXT("Контра!");
		case EDefCue::Broke: return TEXT("Блок пробит — добивай");
		case EDefCue::GuardBreak: return TEXT("Твой блок пробит!");
		case EDefCue::Caught: return TEXT("Пойман на нырке");
		case EDefCue::Clinch: return TEXT("Клинч");
		case EDefCue::Break: return TEXT("Брейк!");
		default: return TEXT("");
		}
	}

	float DefenseCueSeconds(EDefCue C)
	{
		return C == EDefCue::None ? 0.f : (C == EDefCue::Slip ? COUNTER_WINDOW_S : 1.f);
	}

	bool DefenseCueGood(EDefCue C)
	{
		return C == EDefCue::Slip || C == EDefCue::Counter || C == EDefCue::Broke;
	}

	float CounterStopMs(float StopMs, bool bCounter)
	{
		return bCounter ? FMath::Max(StopMs, COUNTER_STOP_MS) : StopMs;
	}

	void BlendView(const FVector& CamA, const FVector& LookA, const FVector& CamB, const FVector& LookB, float T, FVector& OutCam, FVector& OutLook)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		OutLook = FMath::Lerp(LookA, LookB, T);
		const FVector Oa = CamA - LookA;
		const FVector Ob = CamB - LookB;
		const float La = Oa.Size(), Lb = Ob.Size();
		if (La < 1.f || Lb < 1.f)
		{
			OutCam = FMath::Lerp(CamA, CamB, T);
			return;
		}
		const float Ya = FMath::Atan2(Oa.Y, Oa.X), Yb = FMath::Atan2(Ob.Y, Ob.X);
		const float Pa = FMath::Asin(FMath::Clamp(Oa.Z / La, -1.f, 1.f)), Pb = FMath::Asin(FMath::Clamp(Ob.Z / Lb, -1.f, 1.f));
		const float Yaw = Ya + FMath::DegreesToRadians(WrapDeg(FMath::RadiansToDegrees(Yb - Ya))) * T;
		const float Pit = FMath::Lerp(Pa, Pb, T);
		const float Len = FMath::Lerp(La, Lb, T);
		OutCam = OutLook + FVector(FMath::Cos(Pit) * FMath::Cos(Yaw), FMath::Cos(Pit) * FMath::Sin(Yaw), FMath::Sin(Pit)) * Len;
	}

	FVector LimitViewDir(const FVector& Prev, const FVector& Want, float Dt, bool bCut, bool* bOutTurnLimited, bool* bOutPitchLimited)
	{
		const FVector W = Want.GetSafeNormal();
		float Yaw = FMath::RadiansToDegrees(FMath::Atan2(W.Y, W.X));
		float Pit = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(W.Z, -1.f, 1.f)));
		const float PitC = FMath::Clamp(Pit, CAM_PITCH_MIN_DEG, CAM_PITCH_MAX_DEG);
		if (bOutPitchLimited)
		{
			*bOutPitchLimited = PitC != Pit;
		}
		Pit = PitC;
		bool bTurn = false;
		const FVector P = Prev.GetSafeNormal();
		if (!bCut && !P.IsNearlyZero() && Dt > 0.f)
		{
			const float PrevYaw = FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X));
			const float PrevPit = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(P.Z, -1.f, 1.f)));
			const float Max = CAM_MAX_TURN_DPS * Dt;
			const float Dy = WrapDeg(Yaw - PrevYaw);
			const float Dp = Pit - PrevPit;
			if (FMath::Abs(Dy) > Max || FMath::Abs(Dp) > Max)
			{
				bTurn = true;
				Yaw = PrevYaw + FMath::Clamp(Dy, -Max, Max);
				Pit = PrevPit + FMath::Clamp(Dp, -Max, Max);
			}
		}
		if (bOutTurnLimited)
		{
			*bOutTurnLimited = bTurn;
		}
		const float Yr = FMath::DegreesToRadians(Yaw), Pr = FMath::DegreesToRadians(Pit);
		return FVector(FMath::Cos(Pr) * FMath::Cos(Yr), FMath::Cos(Pr) * FMath::Sin(Yr), FMath::Sin(Pr));
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
	// S-75: замер защиты (-BoxFxLog).
	struct FDefStats
	{
		int32 Slips = 0;
		float SlipHeadSum = 0.f;
		float SlipHeadWorst = 1e6f;
		int32 SlipTouch = 0;
		int32 Blocks = 0;
		float BlockGloveSum = 0.f;
		float BlockHeadSum = 0.f;
		int32 BlockHeadFirst = 0;
	};
	FDefStats GDefStats;
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
	FParse::Value(Cmd, TEXT("BoxReplaySkipAt="), ReplaySkipAt);
	FParse::Value(Cmd, TEXT("BoxFxShots="), FxShotsLeft);
	{
		int32 Def = 0;
		FParse::Value(Cmd, TEXT("BoxDefShots="), Def);
		for (int32& L : DefShotsLeft)
		{
			L = Def;
		}
		DefShotPrefix = TEXT("feel6_def");
		FParse::Value(Cmd, TEXT("BoxShotPrefix="), DefShotPrefix);
		bNoCue = FParse::Param(Cmd, TEXT("BoxNoDefCue"));
	}
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
	if (bEnabled && CamStats.Frames > 0)
	{
		// S-78: автопроверка камеры (ориентир QA camcheck: поворот > 240°/с без склейки, наклон).
		UE_LOG(LogTemp, Log, TEXT("FX CAM СВОДКА: кадров %d, склеек %d; ограничитель: поворот %d кадров (запрошено до %.0f°/с), наклон %d кадров (запрошено до %.0f°); итог: поворот > 240°/с вне склеек — %d кадров (макс. %.0f°/с), наклон < −50° — %d (мин. %.0f°)"),
			CamStats.Frames, CamStats.Cuts, CamStats.TurnLimited, CamStats.WantTurnMax, CamStats.PitchLimited, CamStats.WantPitchMin,
			CamStats.FastFinal, CamStats.FinalTurnMax, CamStats.SteepFinal, CamStats.FinalPitchMin);
	}
	if (bLog && (GDefStats.Slips + GDefStats.Blocks) > 0)
	{
		// S-75: нырок — кулак мимо головы (мин. фронт кулака → центр головы); блок — кулак в перчатку, а не в голову.
		UE_LOG(LogTemp, Log, TEXT("FX DEF СВОДКА: нырков %d — кулак→голова мин. ср. %.1f см, худший %.1f см, ближе 17 см (задел голову) %d; блоков %d — кулак→перчатка ср. %.1f см, кулак→голова ср. %.1f см, ближе к голове, чем к перчатке, %d"),
			GDefStats.Slips, GDefStats.Slips ? GDefStats.SlipHeadSum / GDefStats.Slips : 0.f, GDefStats.SlipHeadWorst, GDefStats.SlipTouch,
			GDefStats.Blocks, GDefStats.Blocks ? GDefStats.BlockGloveSum / GDefStats.Blocks : 0.f, GDefStats.Blocks ? GDefStats.BlockHeadSum / GDefStats.Blocks : 0.f,
			GDefStats.BlockHeadFirst);
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

void UBoxingFightFx::QueueDefShots(int32 Slot, const TCHAR* Kind, std::initializer_list<float> Offsets)
{
	if (DefShotsLeft[Slot] <= 0)
	{
		return;
	}
	--DefShotsLeft[Slot];
	const int32 N = ++DefShotIndex[Slot];
	for (const float D : Offsets)
	{
		// S-76: клинч — feel6_clinch_* (как просил продюсер), с -BoxShotPrefix=X — X_clinch_*.
		const FString Pre = Slot >= 5 ? (DefShotPrefix == TEXT("feel6_def") ? FString(TEXT("feel6")) : DefShotPrefix) : DefShotPrefix;
		const TPair<double, FString> Shot(Clock + D, FString::Printf(TEXT("%s_%s_%02d_t%03d"), *Pre, Kind, N, FMath::RoundToInt(D * 1000.f)));
		int32 At = PendingShots.Num();
		while (At > 0 && PendingShots[At - 1].Key > Shot.Key)
		{
			--At;
		}
		PendingShots.Insert(Shot, At);
	}
}

void UBoxingFightFx::OnFightEvent(const FFightEvent& E, ABoxingFightGameMode* GM)
{
	Mode = GM;
	if (!bEnabled || !GM)
	{
		return;
	}
	// S-75: подсказка защиты игроку (крупная подпись, UpdateCue) — новая перебивает старую.
	{
		const BoxFx::EDefCue C = BoxFx::DefenseCueFor(E, GM->GetPlayerIndex());
		if (C != BoxFx::EDefCue::None && !bNoCue)
		{
			Cue = C;
			CueDur = BoxFx::DefenseCueSeconds(C);
			CueLeft = CueDur;
			CueAge = 0.f;
			if (bLog)
			{
				UE_LOG(LogTemp, Log, TEXT("FX cue «%s» t=%.2f"), BoxFx::DefenseCueText(C), E.Time);
			}
		}
	}
	// S-75 (отладка): серии кадров защиты.
	if (E.Kind == EFightEventKind::Blocked)
	{
		QueueDefShots(0, TEXT("block"), {0.f, 0.05f, 0.12f, 0.25f});
	}
	if (bLog && (E.Kind == EFightEventKind::Blocked || E.Kind == EFightEventKind::Slipped) && E.Attacker >= 0 && E.Target == EPunchTarget::Head)
	{
		FDefProbe P;
		P.Att = E.Attacker;
		P.bSlip = E.Kind == EFightEventKind::Slipped;
		P.Left = 0.3f;
		DefProbes.Add(P);
	}
	else if (E.Kind == EFightEventKind::Slipped)
	{
		QueueDefShots(2, TEXT("whiff"), {0.f, 0.08f, 0.18f, 0.32f, 0.5f});
	}
	else if (E.Kind == EFightEventKind::Hit && E.bCounter)
	{
		QueueDefShots(3, TEXT("counter"), {0.f, 0.06f, 0.16f});
	}
	else if (E.Kind == EFightEventKind::Hit && (E.bGuardBreak || E.bCaught))
	{
		QueueDefShots(4, E.bGuardBreak ? TEXT("guardbreak") : TEXT("caught"), {0.f, 0.06f, 0.16f, 0.32f});
	}
	else if (E.Kind == EFightEventKind::Clinch)
	{
		QueueDefShots(5, TEXT("clinch_hold"), {0.25f, 0.7f});
	}
	else if (E.Kind == EFightEventKind::Break)
	{
		QueueDefShots(6, TEXT("clinch_break"), {0.f, 0.2f, 0.45f, 0.8f});
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
	// S-75: контра (удар в контр-окне после удачного уклона) — короткий стоп-кадр и на среднем попадании.
	const bool bCounterLand = Kind == BoxFx::EKind::Land && E.bCounter;
	const float StopMs = BoxFx::CounterStopMs(BoxFx::HitStopMs(Kind, Mag, Profile), bCounterLand);
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
	BoxFx::FCamKick K = BoxFx::CameraKick(Kind, Mag);
	if (bCounterLand)
	{
		K.PunchIn = FMath::Max(K.PunchIn, BoxFx::COUNTER_PUNCH_IN); // S-75: контра — наезд камеры
	}
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
	// --- S-74: брызги пота в точке контакта ---
	if ((Kind == BoxFx::EKind::Land || Kind == BoxFx::EKind::Block) && E.Attacker >= 0 && E.Defender >= 0)
	{
		SpawnSpray(E, GM, Kind == BoxFx::EKind::Block ? BoxSpray::EKind::Block : (bBody ? BoxSpray::EKind::Body : BoxSpray::EKind::Head), Mag);
	}

	// --- звук ---
	switch (Kind)
	{
	case BoxFx::EKind::Land: Audio->Punch(bCounterLand ? FMath::Max(Mag, BoxFx::HEAVY_MAG) : Mag, bBody); break; // S-75: контра — с НЧ-слоем
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

void UBoxingFightFx::UpdateScoreboard(const FFightSnapshot& Snap)
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	if (!bBoardsFound)
	{
		// Уровень строит tech-artist скриптом (build_ring.py: Arena_ScreenRound_±1). Табло — актор с тегом BoxScoreRound;
		// без тега (уровень ещё не пересобран) — TextRender, чей текст начинается с «РАУНД».
		bBoardsFound = true;
		for (TActorIterator<ATextRenderActor> It(W); It; ++It)
		{
			const UTextRenderComponent* T = It->GetTextRender();
			const bool bTag = It->ActorHasTag(TEXT("BoxScoreRound"));
			if (bTag || (T && T->Text.ToString().StartsWith(TEXT("РАУНД"))))
			{
				Boards.Add(*It);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("FX табло арены: %d"), Boards.Num());
	}
	if (Boards.Num() == 0)
	{
		return;
	}
	const float Left = Snap.Phase == EFightPhase::Between ? Snap.BreakLeft : Snap.TimeLeft;
	const int32 Sec = FMath::Max(0, FMath::CeilToInt(Left));
	FString Txt;
	if (Snap.Phase == EFightPhase::Over)
	{
		Txt = TEXT("БОЙ ОКОНЧЕН");
	}
	else if (Snap.Phase == EFightPhase::Between)
	{
		Txt = FString::Printf(TEXT("ПЕРЕРЫВ   %d:%02d"), Sec / 60, Sec % 60);
	}
	else
	{
		Txt = FString::Printf(TEXT("РАУНД %d   %d:%02d"), Snap.Round, Sec / 60, Sec % 60);
	}
	if (Txt == BoardText)
	{
		return;
	}
	BoardText = Txt;
	for (const TWeakObjectPtr<ATextRenderActor>& B : Boards)
	{
		if (B.IsValid() && B->GetTextRender())
		{
			B->GetTextRender()->SetText(FText::FromString(Txt));
		}
	}
}

void UBoxingFightFx::OnSnapshot(const FFightSnapshot& Snap, ABoxingFightGameMode* GM)
{
	Mode = GM;
	UpdateScoreboard(Snap);
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
	// S-75 (отладка): начало нырка — серия кадров (кулак соперника проходит мимо головы). Вне боя — подсказку гасим.
	for (int32 I = 0; I < 2; ++I)
	{
		const float S = Snap.Fighters[I].Slip;
		if (S != 0.f && PrevSlipAmt[I] == 0.f)
		{
			QueueDefShots(1, I == 0 ? TEXT("slip_red") : TEXT("slip_blue"), {0.f, 0.08f, 0.16f, 0.24f, 0.34f});
		}
		PrevSlipAmt[I] = S;
	}
	if (Snap.Phase != EFightPhase::Fighting)
	{
		CueLeft = FMath::Min(CueLeft, 0.2f);
	}
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
	// S-74 (отладка): -BoxFaceCam=БОЕЦ,ДИСТ,УГОЛ — портрет лица: камера перед головой бойца (по курсу, УГОЛ — вбок от лица),
	// крупно мимика и повреждения (камера боя на лицо не смотрит: голова за перчатками).
	static const TArray<float> FaceCam = [] {
		TArray<float> V;
		FString S;
		if (FParse::Value(FCommandLine::Get(), TEXT("BoxFaceCam="), S, false))
		{
			TArray<FString> Parts;
			S.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts) V.Add(FCString::Atof(*P));
		}
		return V;
	}();
	if (FaceCam.Num() >= 3 && Mode.IsValid())
	{
		const ABoxerCharacter* B = Mode->GetBoxer(FMath::Clamp(static_cast<int32>(FaceCam[0]), 0, 1));
		const USkeletalMeshComponent* M = B ? B->GetFeelMesh() : nullptr;
		if (M && M->GetBoneIndex(TEXT("head")) != INDEX_NONE)
		{
			const FVector H = M->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 6.f);
			const FVector F = B->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(FaceCam[2], FVector::UpVector);
			Cam = H + F * FaceCam[1] + FVector(0.f, 0.f, -2.f);
			Look = H;
			return true;
		}
	}
	if (bShotCam)
	{
		Cam = ShotCam; // S-71: кадр скриншота угловых
		Look = ShotLook;
		return true;
	}
	if (!bEnabled)
	{
		return false;
	}
	bool bOverride = false;
	if (Replay.bPlaying && Replay.Clip.Num() > 1)
	{
		// Драматичный ракурс повтора: низко (чуть выше верхнего каната), сбоку от оси пары, медленный облёт.
		const FRecFrame& F = Replay.Clip[FMath::Clamp(Replay.CurIdx, 0, Replay.Clip.Num() - 1)];
		// S-66: центр — по местам И тазам обоих (падающее тело уходит на 1.5 м за точку бойца — иначе падение уходило из
		// кадра), сглажен; разошлись — камера отъезжает.
		const FVector Mid = 0.25f * (F.F[0].Loc + F.F[1].Loc + F.F[0].Pelvis + F.F[1].Pelvis);
		const float Floor = Mode.IsValid() ? Mode->GetRingFloorCenter().Z : Mid.Z - 90.f;
		const float Span = FMath::Max(FVector::Dist2D(F.F[0].Loc, F.F[1].Loc), FMath::Max(FVector::Dist2D(F.F[0].Pelvis, F.F[1].Loc), FVector::Dist2D(F.F[1].Pelvis, F.F[0].Loc)));
		if (!bReplayCamInit)
		{
			bReplayCamInit = true;
			ReplayMid = Mid;
			ReplaySpan = Span;
		}
		const float K = 1.f - FMath::Exp(-3.f * FMath::Min(0.05f, Mode.IsValid() ? Mode->GetWorld()->DeltaRealTimeSeconds : 0.016f));
		ReplayMid += (Mid - ReplayMid) * K;
		ReplaySpan += (Span - ReplaySpan) * K;
		const float Dist = 255.f + FMath::Max(0.f, ReplaySpan - 130.f) * 0.7f;
		const float Ax = FMath::Atan2(F.F[1].Loc.Y - F.F[0].Loc.Y, F.F[1].Loc.X - F.F[0].Loc.X) + HALF_PI - 0.25f + Replay.Orbit;
		// S-78 (QA косм. 15): голова атакующего у верхнего края под плашкой «ПОВТОР» — камера чуть дальше и выше, взгляд выше.
		const float DistR = Dist * 1.12f;
		Cam = FVector(ReplayMid.X + FMath::Cos(Ax) * DistR, ReplayMid.Y + FMath::Sin(Ax) * DistR, Floor + 150.f);
		Look = FVector(ReplayMid.X, ReplayMid.Y, Floor + 100.f);
		bOverride = true;
	}
	else
	{
		// Перерыв: камера ведёт игрока к его углу (restShot веба), нокдаун — мягкий наезд на лежащего.
		// S-78: смешивание «по орбите» вокруг точки взгляда (BoxFx::BlendView), а не прямой линией через бойца.
		if (RestMix > 1e-3f)
		{
			const float R = RestMix * RestMix * (3.f - 2.f * RestMix);
			BoxFx::BlendView(Cam, Look, RestCam, RestLook, R, Cam, Look);
		}
		if (DownMix > 1e-3f)
		{
			const float D = DownMix * DownMix * (3.f - 2.f * DownMix);
			BoxFx::BlendView(Cam, Look, KdCam, KdLook, D, Cam, Look);
		}
		if (ResultMix > 1e-3f)
		{
			const float Rm = ResultMix * ResultMix * (3.f - 2.f * ResultMix);
			BoxFx::BlendView(Cam, Look, ResultCam, ResultLook, Rm, Cam, Look);
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
	LimitCamera(Cam, Look); // S-78
	return bOverride;
}

void UBoxingFightFx::LimitCamera(FVector& Cam, FVector& Look)
{
	static const bool bNoLimit = FParse::Param(FCommandLine::Get(), TEXT("BoxCamNoLimit")); // A/B: без ограничителя
	const float Dt = CamPrevClock < 0.0 ? 0.f : static_cast<float>(Clock - CamPrevClock);
	const bool bFirst = CamPrevDir.IsNearlyZero();
	const bool bCut = bFirst || FVector::Dist(Cam, CamPrevPos) > BoxFx::CAM_CUT_CM;
	const FVector Want = (Look - Cam).GetSafeNormal();
	auto Angles = [](const FVector& D, float& Y, float& P)
	{
		Y = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		P = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Z, -1.f, 1.f)));
	};
	float Wy = 0.f, Wp = 0.f, Py = 0.f, Pp = 0.f;
	Angles(Want, Wy, Wp);
	if (!bFirst)
	{
		Angles(CamPrevDir, Py, Pp);
	}
	bool bTurn = false, bPitch = false;
	FVector Dir = Want;
	if (!bNoLimit && !Want.IsNearlyZero())
	{
		Dir = BoxFx::LimitViewDir(CamPrevDir, Want, Dt, bCut, &bTurn, &bPitch);
		Look = Cam + Dir * FMath::Max(50.f, static_cast<float>(FVector::Dist(Look, Cam)));
	}
	// Автопроверка (как camcheck QA): запрошенное и итоговое.
	++CamStats.Frames;
	CamStats.Cuts += (bCut && !bFirst) ? 1 : 0;
	CamStats.TurnLimited += bTurn ? 1 : 0;
	CamStats.PitchLimited += bPitch ? 1 : 0;
	CamStats.WantPitchMin = FMath::Min(CamStats.WantPitchMin, Wp);
	float Fy = 0.f, Fp = 0.f;
	Angles(Dir, Fy, Fp);
	CamStats.FinalPitchMin = FMath::Min(CamStats.FinalPitchMin, Fp);
	CamStats.SteepFinal += Fp < -50.f ? 1 : 0;
	if (!bFirst && !bCut && Dt > 1e-3f)
	{
		const float WantRate = FMath::Max(FMath::Abs(BoxFx::WrapDeg(Wy - Py)), FMath::Abs(Wp - Pp)) / Dt;
		const float FinalRate = FMath::Max(FMath::Abs(BoxFx::WrapDeg(Fy - Py)), FMath::Abs(Fp - Pp)) / Dt;
		CamStats.WantTurnMax = FMath::Max(CamStats.WantTurnMax, WantRate);
		CamStats.FinalTurnMax = FMath::Max(CamStats.FinalTurnMax, FinalRate);
		CamStats.FastFinal += FinalRate > 240.f ? 1 : 0;
		if (bLog && (bTurn || bPitch) && WantRate > 240.f)
		{
			UE_LOG(LogTemp, Log, TEXT("FX cam-limit: запрошено %.0f°/с, наклон %.0f° → итог %.0f°/с, %.0f°"), WantRate, Wp, FinalRate, Fp);
		}
	}
	if (Dt > 1e-5f || bFirst)
	{
		CamPrevClock = Clock;
	}
	CamPrevPos = Cam;
	CamPrevDir = Dir;
}

// ---------------------------------------------------------------------------------------------
// Тик (реальное время)
// ---------------------------------------------------------------------------------------------

void UBoxingFightFx::UpdateDefProbes(float RealDt)
{
	ABoxingFightGameMode* GM = Mode.Get();
	for (int32 I = DefProbes.Num() - 1; I >= 0; --I)
	{
		FDefProbe& P = DefProbes[I];
		const ABoxerCharacter* A = GM ? GM->GetBoxer(P.Att) : nullptr;
		const ABoxerCharacter* D = GM ? GM->GetBoxer(1 - P.Att) : nullptr;
		const USkeletalMeshComponent* DM = D ? D->GetFeelMesh() : nullptr;
		if (A && DM && DM->GetBoneIndex(TEXT("head")) != INDEX_NONE)
		{
			const FBoxerFeelDebug Dbg = A->GetFeelDebug();
			if (Dbg.AimW > 0.f && !Dbg.FistFront.IsNearlyZero())
			{
				const FVector Head = DM->GetBoneLocation(TEXT("head")) + FVector::UpVector * D->HeadCenterUpCm;
				P.HeadMin = FMath::Min(P.HeadMin, static_cast<float>(FVector::Dist(Dbg.FistFront, Head)));
				for (const TCHAR* Sfx : {TEXT("_l"), TEXT("_r")})
				{
					const FName H(*(FString(TEXT("hand")) + Sfx)), L(*(FString(TEXT("lowerarm")) + Sfx));
					if (DM->GetBoneIndex(H) != INDEX_NONE && DM->GetBoneIndex(L) != INDEX_NONE)
					{
						const FVector Hp = DM->GetBoneLocation(H);
						const FVector Glove = Hp + (Hp - DM->GetBoneLocation(L)).GetSafeNormal() * 6.f;
						P.GloveMin = FMath::Min(P.GloveMin, static_cast<float>(FVector::Dist(Dbg.FistFront, Glove)));
					}
				}
			}
		}
		P.Left -= RealDt;
		if (P.Left > 0.f)
		{
			continue;
		}
		if (P.HeadMin < 1e5f)
		{
			if (P.bSlip)
			{
				++GDefStats.Slips;
				GDefStats.SlipHeadSum += P.HeadMin;
				GDefStats.SlipHeadWorst = FMath::Min(GDefStats.SlipHeadWorst, P.HeadMin);
				GDefStats.SlipTouch += P.HeadMin < 17.f ? 1 : 0;
			}
			else
			{
				++GDefStats.Blocks;
				GDefStats.BlockGloveSum += P.GloveMin;
				GDefStats.BlockHeadSum += P.HeadMin;
				GDefStats.BlockHeadFirst += P.HeadMin < P.GloveMin ? 1 : 0;
			}
			UE_LOG(LogTemp, Log, TEXT("FX def %s [%d]: кулак→голова %.1f см, кулак→перчатка %.1f см"), P.bSlip ? TEXT("нырок") : TEXT("блок"), P.Att,
				P.HeadMin, P.GloveMin);
		}
		DefProbes.RemoveAt(I);
	}
}

void UBoxingFightFx::UpdateCue(float RealDt)
{
	if (CueLeft > 0.f)
	{
		CueLeft = FMath::Max(0.f, CueLeft - RealDt);
		CueAge += RealDt;
	}
	const bool bShow = CueLeft > 0.f && !Replay.bPlaying;
	if (!CueWidget)
	{
		if (!bShow)
		{
			return;
		}
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return;
		}
		CueWidget = CreateWidget<UBoxingDefenseCueWidget>(PC, UBoxingDefenseCueWidget::StaticClass());
		if (!CueWidget)
		{
			return;
		}
		CueWidget->AddToViewport(5); // над HUD боя (0), под паузой (20) и итогом (10)
	}
	if (!bShow)
	{
		CueWidget->Show(FString(), FLinearColor::White, 0.f, 1.f, -1.f);
		return;
	}
	// Появление 0.06 с с «ударом» масштаба 1.25 → 1 за 0.15 с; угасание — последние 0.25 с (уклон — держится всё окно).
	const float In = FMath::Clamp(CueAge / 0.06f, 0.f, 1.f);
	const float Out = FMath::Clamp(CueLeft / (Cue == BoxFx::EDefCue::Slip ? 0.1f : 0.25f), 0.f, 1.f);
	const float PopT = FMath::Clamp(CueAge / 0.15f, 0.f, 1.f);
	const float Pop = 1.f + 0.25f * (1.f - PopT) * (1.f - PopT) * (Cue == BoxFx::EDefCue::Counter ? 1.6f : 1.f);
	const bool bNeutral = Cue == BoxFx::EDefCue::Clinch || Cue == BoxFx::EDefCue::Break;
	const FLinearColor Col = Cue == BoxFx::EDefCue::Counter ? BoxUi::Gold
		: (bNeutral ? BoxUi::Text : (BoxFx::DefenseCueGood(Cue) ? BoxUi::Good : BoxUi::Bad));
	CueWidget->Show(BoxFx::DefenseCueText(Cue), Col, In * Out, Pop, Cue == BoxFx::EDefCue::Slip ? CueLeft / FMath::Max(1e-3f, CueDur) : -1.f);
}

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
	UpdateCue(Real); // S-75
	UpdateDefProbes(Real);
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
	Spray.Update(Game); // S-74: брызги (игровое время: slow-mo замедляет)
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
			const USkeletalMeshComponent* Vm = B[I]->GetFeelMesh();
			R.Pelvis = (Vm && Vm->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE) ? Vm->GetBoneLocation(TEXT("pelvis")) : R.Loc;
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
	bReplayCamInit = false;
	ReplayPlayedReal = 0.f;
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
	if (SkipKeyStage == 1 && GEngine && GEngine->GameViewport)
	{
		SkipKeyStage = 2; // отпустить отладочную клавишу
		UGameViewportClient* Vc = GEngine->GameViewport;
		Vc->InputKey(FInputKeyEventArgs(Vc->Viewport, FInputDeviceId::CreateFromInternalId(0), EKeys::J, IE_Released, FPlatformTime::Cycles64()));
	}
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
	// Отладка (-BoxReplaySkipAt=С): через С с показа «нажать» J настоящим событием вьюпорта — путь клавиатуры целиком
	// (HUD ловит любую клавишу), а не прямой вызов SkipReplay.
	ReplayPlayedReal += RealDt;
	if (ReplaySkipAt >= 0.f && SkipKeyStage == 0 && ReplayPlayedReal >= ReplaySkipAt && GEngine && GEngine->GameViewport)
	{
		SkipKeyStage = 1;
		UGameViewportClient* Vc = GEngine->GameViewport;
		UE_LOG(LogTemp, Log, TEXT("FX отладка: клавиша J посреди повтора (%.2f с показа, t=%+.2f с от удара)"), ReplayPlayedReal, Replay.Cursor - Replay.ClipKd);
		Vc->InputKey(FInputKeyEventArgs(Vc->Viewport, FInputDeviceId::CreateFromInternalId(0), EKeys::J, IE_Pressed, FPlatformTime::Cycles64()));
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
			static const float Every = [] { float V = 1.f; FParse::Value(FCommandLine::Get(), TEXT("BoxReplayShotEvery="), V); return FMath::Max(0.1f, V); }();
			Replay.ShotTimer = Every; // -BoxReplayShotEvery=С (S-66: QA снимал раз в 1 с — 6 кадров кончались до падения)
			--ReplayShotsLeft;
			// S-70: с -BoxShotPrefix=X — Docs/screens/X_replay_NN.png; без него — Saved/Screenshots/BoxReplay (отслеживаемые
			// снимки в Docs/screens больше не перезаписываются).
			static const FString ShotPrefix = [] { FString V; FParse::Value(FCommandLine::Get(), TEXT("BoxShotPrefix="), V); return V; }();
			const FString Path = ShotPrefix.IsEmpty()
				? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/BoxReplay") /
					FString::Printf(TEXT("fx_replay_%02d.png"), ++Replay.ShotIndex))
				: FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/screens") /
					FString::Printf(TEXT("%s_replay_%02d.png"), *ShotPrefix, ++Replay.ShotIndex));
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
		FVector LHead, LPelvis;
		if (D->GetLyingBody(LHead, LPelvis))
		{
			// S-62: где ЛЯЖЕТ тело (итог клипа падения с доворотом от канатов) — кадр верный с первого кадра нокдауна,
			// пока боец ещё валится (раньше угол выбирался по стоящему, и камера упиралась ему в спину).
			Body = 0.5f * (LHead + LPelvis);
			if (!bKdBodyKnown)
			{
				bKdBodyKnown = true;
				KdSide = 0; // угол, выбранный по стоящему телу, — заново
			}
		}
		else if (const USkeletalMeshComponent* M = D->GetFeelMesh())
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
		const APlayerController* KdPc = GetWorld()->GetFirstPlayerController();
		const FVector CurCam = KdPc && KdPc->PlayerCameraManager ? KdPc->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		BoxFx::KnockdownShot(Body, StandRef, RC, KdSide, KdCam, KdLook, KdPc && KdPc->PlayerCameraManager ? &CurCam : nullptr);
		if (bLog && KdSide != WasSide)
		{
			UE_LOG(LogTemp, Log, TEXT("FX kd-cam: лежит %d тело (%.0f, %.0f), стоящий → (%.0f, %.0f), угол %d, камера (%.0f, %.0f, %.0f)"), Dn, Body.X - RC.X, Body.Y - RC.Y,
				StandRef.X - RC.X, StandRef.Y - RC.Y, KdSide - 1000, KdCam.X - RC.X, KdCam.Y - RC.Y, KdCam.Z - RC.Z);
		}
	}
	if (!bDown)
	{
		bKdBodyKnown = false;
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
		const ABoxerCharacter* Pl = GM->GetBoxer(GM->GetPlayerIndex());
		BoxFx::RestShot(GM->GetPlayerIndex(), Aspect, Pl->GetActorLocation(), RC, RestCam, RestLook, Pl->Preset.HeightCm > 0.f ? Pl->Preset.HeightCm / 178.f : 1.f,
			Pl->GetSitWeight());
	}
	RestMix += ((bRest ? 1.f : 0.f) - RestMix) * (1.f - FMath::Exp(-(bRest ? 1.4f : 1.6f) * RealDt)); // S-78: выход из угла плавнее (было 2.6)
	if (!bRest && RestMix < 0.01f)
	{
		RestMix = 0.f;
	}
	// --- итог (S-62): панель итога в центре экрана закрывала бойцов (видны были только ноги) — победитель с рефери во
	// весь рост в свободной полосе слева от панели. Портрет — панель на всю ширину: кадр не трогаем.
	bool bResult = false;
	if (GM->GetCore().IsOver() && GM->GetBoxer(0) && GM->GetBoxer(1))
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const ABoxingFightHUD* Hud = PC ? Cast<ABoxingFightHUD>(PC->GetHUD()) : nullptr;
		FVector2D Vp(16.f, 9.f);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Vp);
		}
		const float Aspect = Vp.Y > 0.f ? Vp.X / Vp.Y : 16.f / 9.f;
		if (Hud && Hud->IsResultOpen() && Aspect >= 1.25f && PC->PlayerCameraManager)
		{
			const int32 Wi = GM->GetCore().GetResult().WinnerIndex >= 0 ? GM->GetCore().GetResult().WinnerIndex : GM->GetPlayerIndex();
			const ABoxerCharacter* W = GM->GetBoxer(Wi);
			const ABoxerCharacter* L = GM->GetBoxer(1 - Wi);
			const bool bFirst = ResultMix <= 0.f;
			if (bFirst)
			{
				ResultCam = PC->PlayerCameraManager->GetCameraLocation(); // сторона — от нынешней камеры, один раз
			}
			BoxFx::ResultShot(W->GetActorLocation(), L->GetActorLocation(), FVector(ResultCam), RC,
				PC->PlayerCameraManager->GetFOVAngle(), W->Preset.HeightCm > 0.f ? W->Preset.HeightCm / 178.f : 1.f, ResultCam, ResultLook,
				bFirst ? W->GetActorForwardVector() : FVector::ZeroVector);
			bResult = true;
			if (bLog && bFirst)
			{
				const FVector Wl = W->GetActorLocation();
				UE_LOG(LogTemp, Log, TEXT("FX result-cam: победитель (%.0f, %.0f), камера (%.0f, %.0f, %.0f), взгляд (%.0f, %.0f, %.0f), FOV %.0f"), Wl.X - RC.X, Wl.Y - RC.Y,
					ResultCam.X - RC.X, ResultCam.Y - RC.Y, ResultCam.Z - RC.Z, ResultLook.X - RC.X, ResultLook.Y - RC.Y, ResultLook.Z - RC.Z, PC->PlayerCameraManager->GetFOVAngle());
			}
		}
	}
	ResultMix += ((bResult ? 1.f : 0.f) - ResultMix) * (1.f - FMath::Exp(-(bResult ? 1.6f : 3.f) * RealDt));
	if (!bResult && ResultMix < 0.01f)
	{
		ResultMix = 0.f;
	}
}

// =============================================================================================
// S-75: подпись защиты (UBoxingDefenseCueWidget)
// =============================================================================================

void UBoxingDefenseCueWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
	Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetFont(BoxUi::Font(40, true));
	Label->SetJustification(ETextJustify::Center);
	Label->SetShadowOffset(FVector2D(2.f, 2.f));
	Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Label))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}
	WindowBar = WidgetTree->ConstructWidget<UProgressBar>();
	WindowBar->SetFillColorAndOpacity(BoxUi::Good);
	FProgressBarStyle St = WindowBar->GetWidgetStyle();
	St.BackgroundImage = FSlateRoundedBoxBrush(BoxUi::WithAlpha(BoxUi::Bg, 0.6f), 3.f);
	St.FillImage = FSlateRoundedBoxBrush(FLinearColor::White, 3.f);
	WindowBar->SetWidgetStyle(St);
	if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(WindowBar))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetPadding(FMargin(24.f, 8.f, 24.f, 0.f));
	}
	Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(FSlateRoundedBoxBrush(BoxUi::WithAlpha(BoxUi::Bg, 0.55f), 12.f));
	Panel->SetPadding(FMargin(28.f, 10.f, 28.f, 12.f));
	Panel->SetContent(Col);
	Panel->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	// Сверху по центру, под панелями бойцов и табло раунда HUD: над головами бойцов, не на них.
	UCanvasPanelSlot* PS = Root->AddChildToCanvas(Panel);
	PS->SetAnchors(FAnchors(0.5f, 0.2f));
	PS->SetAlignment(FVector2D(0.5f, 0.5f));
	PS->SetAutoSize(true);
	PS->SetOffsets(FMargin(0.f));
	Panel->SetVisibility(ESlateVisibility::Collapsed);
}

void UBoxingDefenseCueWidget::Show(const FString& Text, const FLinearColor& Color, float Alpha, float Pop, float Window)
{
	if (!Panel || !Label || !WindowBar)
	{
		return;
	}
	if (Text.IsEmpty() || Alpha <= 0.01f)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (Shown != Text)
	{
		Shown = Text;
		Label->SetText(FText::FromString(Text));
	}
	Label->SetColorAndOpacity(FSlateColor(Color));
	Panel->SetRenderOpacity(Alpha);
	Panel->SetRenderScale(FVector2D(Pop, Pop));
	if (Window >= 0.f)
	{
		WindowBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		WindowBar->SetPercent(FMath::Clamp(Window, 0.f, 1.f));
		WindowBar->SetFillColorAndOpacity(Color);
	}
	else
	{
		WindowBar->SetVisibility(ESlateVisibility::Collapsed);
	}
}
