#include "ImpactSpray.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace ImpactSprayImpl
{
	const TCHAR* DROP_MESH = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* DROP_MAT = TEXT("/Game/Boxing/FX/M_SweatBead.M_SweatBead");
	constexpr float GRAVITY = 980.f;     // см/с²
	constexpr float DRAG = 1.6f;         // 1/с: капля быстро теряет скорость в воздухе
	constexpr float DROP_CM = 1.8f;      // диаметр капли (см): мельче с камеры боя не видно
	constexpr float STREAK_S = 0.012f;   // вытянутость по скорости (с): длина = скорость × STREAK_S
}
using namespace ImpactSprayImpl;

BoxSpray::FBurst BoxSpray::ImpactBurst(float Mag, EKind Kind)
{
	FBurst B;
	const float M = FMath::Max(0.f, Mag);
	if (Kind == EKind::Block)
	{
		// В перчатки: пара капель — видно, что удар встретил блок.
		const float K = FMath::Min(1.f, M / 1.5f);
		B.Drops = M < 0.25f ? 0 : FMath::RoundToInt(2.f + 5.f * K);
		B.Speed = 0.9f + 0.8f * K;
		B.Spread = 0.7f;
		B.Lift = 0.4f;
		B.Bright = 0.45f;
		return B;
	}
	const float U = FMath::Clamp((M - DROPS_MAG_MIN) / 1.8f, 0.f, 1.f);
	const bool bBody = Kind == EKind::Body;
	B.Drops = M < DROPS_MAG_MIN ? 0 : FMath::RoundToInt((bBody ? 4.f : 6.f) + (bBody ? 18.f : 40.f) * U);
	B.Speed = (bBody ? 1.2f : 1.8f) + (bBody ? 1.f : 2.2f) * U;
	B.Spread = (bBody ? 0.6f : 0.8f) + 0.9f * U;
	B.Lift = bBody ? 0.1f : 0.25f + 0.25f * U;
	B.Bright = 0.4f + 0.35f * U;
	return B;
}

bool FBoxImpactSpray::Ensure(UWorld* World)
{
	if (Ism.IsValid())
	{
		return true;
	}
	if (bTried || !World)
	{
		return false;
	}
	bTried = true;
	static const bool bOff = [] { int32 V = 1; FParse::Value(FCommandLine::Get(), TEXT("BoxSpray="), V); return V == 0; }();
	UStaticMesh* Mesh = bOff ? nullptr : LoadObject<UStaticMesh>(nullptr, DROP_MESH);
	static const FString MatPath = [] { FString V = DROP_MAT; FParse::Value(FCommandLine::Get(), TEXT("BoxSprayMat="), V); return V; }(); // отладка
	UMaterialInterface* Mat = Mesh ? LoadObject<UMaterialInterface>(nullptr, *MatPath) : nullptr;
	if (!Mesh || !Mat)
	{
		if (!bOff)
		{
			UE_LOG(LogTemp, Warning, TEXT("FX брызги: нет %s (Tools/EditorScripts/feel_sweat_drop.py) — без капель"), Mesh ? DROP_MAT : DROP_MESH);
		}
		return false;
	}
	FActorSpawnParameters Sp;
	Sp.ObjectFlags |= RF_Transient;
	AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Sp);
	if (!A)
	{
		return false;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(A, TEXT("SweatDrops"));
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, Mat);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetMobility(EComponentMobility::Movable);
	C->NumCustomDataFloats = 1;
	A->SetRootComponent(C);
	C->RegisterComponent();
	Drops.SetNum(POOL);
	TArray<FTransform> Zero;
	Zero.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), POOL);
	C->AddInstances(Zero, false, true);
	C->SetNumCustomDataFloats(1); // выделяет данные под яркость каждой капли (одно поле NumCustomDataFloats их не выделяет)
	Host = A;
	Ism = C;
	return true;
}

void FBoxImpactSpray::Spawn(UWorld* World, const FVector& At, const FVector& Dir, float Mag, BoxSpray::EKind Kind)
{
	const BoxSpray::FBurst B = BoxSpray::ImpactBurst(Mag, Kind);
	if (B.Drops <= 0 || !Ensure(World))
	{
		return;
	}
	const FVector D = Dir.GetSafeNormal().IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
	FVector Side = FVector::CrossProduct(FVector::UpVector, D).GetSafeNormal();
	if (Side.IsNearlyZero())
	{
		Side = FVector::RightVector;
	}
	for (int32 I = 0; I < B.Drops; ++I)
	{
		FDrop& Dr = Drops[Next];
		Next = (Next + 1) % POOL;
		// Вдоль удара (дальше от кулака — с лица/корпуса слетает пот) + конус вбок/вверх (как веб: spread, lift).
		const float Along = B.Speed * Rng.FRandRange(0.45f, 1.f);
		const FVector V = D * Along + Side * (B.Spread * Rng.FRandRange(-1.f, 1.f)) + FVector::UpVector * (B.Lift + B.Spread * 0.6f * Rng.FRandRange(-0.4f, 1.f));
		Dr.P = At + FVector(Rng.FRandRange(-2.f, 2.f), Rng.FRandRange(-2.f, 2.f), Rng.FRandRange(-2.f, 2.f));
		Dr.V = V * 100.f;
		Dr.Life = Dr.Max = Rng.FRandRange(0.22f, 0.42f);
		Dr.Bright = B.Bright;
		Dr.Size = Rng.FRandRange(0.6f, 1.25f);
	}
	bDirty = true;
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("BoxFxLog"));
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("FX брызги: %d капель (mag %.2f, вид %d) в (%.0f, %.0f, %.0f), живых %d"), B.Drops, Mag, static_cast<int32>(Kind), At.X, At.Y, At.Z, Alive());
	}
}

void FBoxImpactSpray::Update(float GameDt)
{
	UInstancedStaticMeshComponent* C = Ism.Get();
	if (!C || !bDirty)
	{
		return;
	}
	const float Dt = FMath::Clamp(GameDt, 0.f, 0.05f);
	bool bAny = false;
	for (int32 I = 0; I < Drops.Num() && I < C->GetInstanceCount(); ++I)
	{
		FDrop& Dr = Drops[I];
		if (Dr.Life <= 0.f)
		{
			if (Dr.Max > 0.f)
			{
				Dr.Max = 0.f;
				C->UpdateInstanceTransform(I, FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), true, false, true);
			}
			continue;
		}
		bAny = true;
		Dr.Life -= Dt;
		Dr.V *= FMath::Exp(-DRAG * Dt);
		Dr.V.Z -= GRAVITY * Dt;
		Dr.P += Dr.V * Dt;
		const float U = FMath::Clamp(Dr.Life / FMath::Max(1e-3f, Dr.Max), 0.f, 1.f);
		const float Speed = static_cast<float>(Dr.V.Size());
		static const float DbgK = [] { float V = 1.f; FParse::Value(FCommandLine::Get(), TEXT("BoxSprayScale="), V); return V; }(); // отладка: крупнее
		const float Fade = FMath::Min(1.f, U * 1.6f); // капля гаснет — уменьшается (материал непрозрачный)
		const float D = DROP_CM * Dr.Size * DbgK * Fade / 100.f; // масштаб сферы движка (100 см)
		const float Len = FMath::Max(D, (DROP_CM * Dr.Size * Fade + Speed * STREAK_S) / 100.f);
		const FQuat Q = Speed > 1.f ? FRotationMatrix::MakeFromX(Dr.V / Speed).ToQuat() : FQuat::Identity;
		C->UpdateInstanceTransform(I, FTransform(Q, Dr.P, FVector(Len, D, D)), true, false, true);
		C->SetCustomDataValue(I, 0, Dr.Bright, false);
	}
	C->MarkRenderStateDirty();
	C->UpdateBounds();
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("BoxFxLog"));
	if (bLog && bAny && GFrameCounter % 10 == 0)
	{
		FTransform T0;
		C->GetInstanceTransform(Next > 0 ? Next - 1 : POOL - 1, T0, true);
		UE_LOG(LogTemp, Log, TEXT("FX брызги кадр: экземпляров %d, данных %d, видим %d, границы %s, последняя (%.0f, %.0f, %.0f) масштаб %.3f"), C->GetInstanceCount(),
			C->PerInstanceSMCustomData.Num(), C->IsVisible() ? 1 : 0, *C->Bounds.BoxExtent.ToString(), T0.GetLocation().X, T0.GetLocation().Y, T0.GetLocation().Z, T0.GetScale3D().Y);
	}
	bDirty = bAny;
}

void FBoxImpactSpray::Reset()
{
	for (FDrop& Dr : Drops)
	{
		Dr.Life = 0.f;
	}
	bDirty = true;
	Update(0.f);
}

int32 FBoxImpactSpray::Alive() const
{
	int32 N = 0;
	for (const FDrop& Dr : Drops)
	{
		N += Dr.Life > 0.f ? 1 : 0;
	}
	return N;
}

// ---------------------------------------------------------------------------------------------
// UBoxingFightFx: брызги по событию ядра (точка — фронт кулака атакующего в кадре контакта)
// ---------------------------------------------------------------------------------------------

#include "FightFx.h"
#include "BoxerCharacter.h"
#include "BoxingFightGameMode.h"
#include "Components/SkeletalMeshComponent.h"

void UBoxingFightFx::SpawnSpray(const FFightEvent& E, ABoxingFightGameMode* GM, BoxSpray::EKind SprayKind, float Mag)
{
	const ABoxerCharacter* A = GM ? GM->GetBoxer(E.Attacker) : nullptr;
	const ABoxerCharacter* D = GM ? GM->GetBoxer(E.Defender) : nullptr;
	if (!A || !D)
	{
		return;
	}
	const USkeletalMeshComponent* M = D->GetFeelMesh();
	const FName Bone = SprayKind == BoxSpray::EKind::Body ? FName(TEXT("spine_03")) : FName(TEXT("head"));
	FVector Target = M && M->GetBoneIndex(Bone) != INDEX_NONE ? M->GetBoneLocation(Bone) : D->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	if (SprayKind == BoxSpray::EKind::Head)
	{
		Target.Z += 6.f; // кость головы — у основания черепа; лицо выше
	}
	// Фронт кулака атакующего (кадр анимпотока): у цели — там и контакт, иначе — поверхность цели.
	const FBoxerFeelDebug Dbg = A->GetFeelDebug();
	const FVector Fist = Dbg.FistFront;
	const bool bFist = !Fist.IsNearlyZero() && FVector::Dist(Fist, Target) < 45.f;
	FVector Dir = D->GetActorLocation() - A->GetActorLocation();
	Dir.Z = 0.f;
	Dir = Dir.GetSafeNormal();
	const FVector At = bFist ? Fist : Target - Dir * (SprayKind == BoxSpray::EKind::Body ? 14.f : 10.f);
	Spray.Spawn(GetWorld(), At, Dir, Mag, SprayKind);
}
