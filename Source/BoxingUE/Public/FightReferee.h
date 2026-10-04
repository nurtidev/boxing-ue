// ABoxingReferee — рефери в бою (S-58, game feel). Логика — BoxRef::FBrain (RefereeBrain.h, порт web
// refereeBrain.ts), здесь — только тело:
//  * ход — CharacterMovement к месту логики (скорость + входное ускорение), ноги — Motion Matching GASP:
//    BP_Referee = копия SandboxCharacter_CMC, перепривязанная к этому классу (Tools/EditorScripts/feel_referee_bp.py);
//  * облик — child actor BP_RefereeLook_Amateur (любители, Rounds <= 3) / BP_RefereeLook_Pro (Docs/LOOK.md);
//  * жесты — процедурно на костях видимого меша (как web Referee.tsx): направления плеча/предплечья из кадра
//    логики, кисти (счёт пальцами, указательный, раскрыта), наклон корпуса, взгляд; поверх локомоции GASP —
//    свободные руки идут от клипов ходьбы/стойки, жест забирает руку целиком (FFrame::ArmW).
// Ядро боя не трогается: рефери только читает снимок (ABoxingFightGameMode::GetSnapshot), места бойцов и камеру.
//
// Командная строка: -BoxNoRef (без рефери), -BoxRefLook=amateur|pro|<путь класса>|none, -BoxRefLog (лог раз в 1 с +
// сводка), -BoxRefShots (скриншоты Docs/screens/ref_*.png: бой сбоку, счёт, нейтральный угол, перерыв, досрочка,
// рука победителю), -BoxRefDraw (отладочные линии: цель, препятствия, путь стоящего).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoneIndices.h"
#include "RefereeBrain.h"
#include "BoxerFeel.h" // S-70: ступни (FBoxerFootIk)
#include "FightReferee.generated.h"

class ABoxingFightGameMode;
class ABoxingReferee;
class UChildActorComponent;
class UIKRetargeter;
struct FCompactPose;
struct FBoneContainer;

// Кадр позы для анимпотока (мир; копируется в прокси на игровом потоке).
struct FRefereePoseFrame
{
	bool bValid = false;
	FVector Fwd = FVector::ForwardVector; // курс рефери
	FVector Left = -FVector::RightVector;
	FVector Upper[2] = {FVector::DownVector, FVector::DownVector}; // плечо → локоть (мир), [левая, правая]
	FVector Fore[2] = {FVector::DownVector, FVector::DownVector};  // локоть → кисть
	float ArmW[2] = {0.f, 0.f};   // доля жеста в руке
	float Lean = 0.f;             // наклон корпуса вперёд, рад
	float Ready = 0.f;            // боевая стойка 0..1 (колени мягче)
	float HeadYaw = 0.f;          // > 0 — влево, рад
	float HeadPitch = 0.f;        // > 0 — вверх
	float Curl[2][5] = {};        // сгиб пальцев [кисть][большой, указательный, средний, безымянный, мизинец], рад
};

// Применение кадра к позе (компонентное пространство, поверх ретаргета позы GASP).
class FRefereePoseFx
{
public:
	void Apply(FCompactPose& Pose, const FRefereePoseFrame& Frame, const FTransform& CompToWorld);
	// S-78: таз не проседает больше PELVIS_SAG_MAX ниже «стоя» (GASP на переступании и развороте приседает на 8–11 см,
	// колени наружу); ступни потом ставит FBoxerFootIk. Dt — шаг анимации.
	void HoldPelvis(FCompactPose& Pose, float Dt);

private:
	void Resolve(const FBoneContainer& Bones);
	uint16 Serial = MAX_uint16;
	const void* ContainerPtr = nullptr;
	FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Spine[3] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex SpineTop = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Neck = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Head = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Clav[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Upper[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Lower[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Hand[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Thigh[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Calf[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Foot[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	// Пальцы: [кисть][палец][фаланга 0..2].
	int32 Finger[2][5][3] = {};
	// S-78: «вперёд» головы и груди и «вверх» груди в осях их костей (из позы привязки) — предел поворота головы.
	bool bTwistCal = false;
	FVector HeadFwdL = FVector::ForwardVector;
	FVector ChestFwdL = FVector::ForwardVector;
	FVector ChestUpL = FVector::UpVector;
	float StandZ = -1.f; // S-78: высота таза «стоя» (компонент), тает вниз медленно
};

namespace BoxRefPose
{
	// S-78: поворот головы относительно груди не больше этого (рад) — GASP разворачивает таз/грудь с запаздыванием за
	// курсом, а голова уже смотрит туда (QA: 118–157° на выходе из углов и в перерыве).
	constexpr float HEAD_TWIST_MAX = 0.85f;
	// S-78: насколько таз может опуститься ниже «стоя» (см) и как быстро «стоя» тает вниз (см/с).
	constexpr float PELVIS_SAG_MAX = 3.5f;
	constexpr float STAND_DECAY = 1.5f;
	// Высота таза: новая «стоя» (растёт сразу, тает медленно) и куда поднять таз (не ниже Stand − SagMax).
	BOXINGUE_API float StandHeight(float Stand, float Z, float Dt);
	BOXINGUE_API float HeldPelvisZ(float Stand, float Z, float SagMax);
	// Знаковый угол (рад) от From к To вокруг оси Axis (оба проецируются ⟂ Axis); 0 — вырождено.
	BOXINGUE_API float TwistAbout(const FVector& Axis, const FVector& From, const FVector& To);
	// Сколько повернуть голову обратно (рад, вокруг той же оси), чтобы |поворот| ≤ Max.
	BOXINGUE_API float TwistExcess(float Twist, float Max);
}

struct FRefereeVisualRootNode : public FAnimNode_Base
{
	FAnimNode_RetargetPoseFromMesh Retarget;
	FRefereePoseFx Fx;
	// S-70: ступни — тот же планировщик и IK ног, что у бойцов (стойка — из позы, ход — попеременным шагом).
	FBoxerFootIk Feet;
	FBoxerFeelFrame FeetFrame;
	float Dt = 0.f;
	FRefereePoseFrame Frame;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

struct FRefereeVisualProxy : public FAnimInstanceProxy
{
	FRefereeVisualProxy() = default;
	explicit FRefereeVisualProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &Root; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	FRefereeVisualRootNode Root;
};

// Видимый меш рефери (MetaHuman Body вместо ABP_GenericRetarget): ретаргет позы логического манекена + жесты.
UCLASS(Transient, NotBlueprintable)
class BOXINGUE_API UFightRefereeAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetupRetarget(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter, const FRetargetProfile* Profile);

	TWeakObjectPtr<ABoxingReferee> Referee;

	UPROPERTY(Transient)
	TObjectPtr<UIKRetargeter> RetargeterRef;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FRefereeVisualProxy(this); }
	friend struct FRefereeVisualProxy;
};

UCLASS(Blueprintable)
class BOXINGUE_API ABoxingReferee : public ACharacter
{
	GENERATED_BODY()

public:
	ABoxingReferee();

	// Спаун для боя (GameMode, после бойцов): класс BP_Referee (иначе нативный), облик по типу боя.
	// nullptr — рефери выключен (-BoxNoRef) или не заспаунился.
	static ABoxingReferee* SpawnFor(ABoxingFightGameMode* GM);

	// Класс актора: копия SandboxCharacter_CMC, перепривязанная к ABoxingReferee (feel_referee_bp.py).
	UPROPERTY(EditAnywhere, Category = "Boxing|Referee")
	FSoftClassPath RefereeClassPath = FSoftClassPath(TEXT("/Game/Boxing/Blueprints/BP_Referee.BP_Referee_C"));

	// Облик (tech-artist, S-56): любители (бой до 3 раундов) / профи. Content/BoxingLocal вне git — нет класса — манекен.
	UPROPERTY(EditAnywhere, Category = "Boxing|Referee")
	FSoftClassPath LookAmateurPath = FSoftClassPath(TEXT("/Game/BoxingLocal/Characters/BP_RefereeLook_Amateur.BP_RefereeLook_Amateur_C"));

	UPROPERTY(EditAnywhere, Category = "Boxing|Referee")
	FSoftClassPath LookProPath = FSoftClassPath(TEXT("/Game/BoxingLocal/Characters/BP_RefereeLook_Pro.BP_RefereeLook_Pro_C"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Referee")
	TSubclassOf<AActor> VisualOverrideClass;

	// Ошибка дальше этой — телепорт, а не ход (см).
	UPROPERTY(EditAnywhere, Category = "Boxing|Referee")
	float TeleportDistance = 120.f;

	const FRefereePoseFrame& GetPoseFrame() const { return Pose; }
	const BoxRef::FRefFrame& GetFrame() const { return Frame; }
	// Итог показан на ринге (для панели итога HUD): рука победителя поднята ≥ 1 с, а над лежащим — развёл руками.
	bool IsResultShown() const
	{
		return Frame.Raised >= 1.0 || (bOverStoppageLying && OverT > BoxRef::STOP_DELAY + BoxRef::WAVE_TIME);
	}
	static ABoxingReferee* Find(const UWorld* World);
	// Место рефери на плане (м ядра).
	BoxRef::FV GetRingPos() const { return Brain.Pos; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void ApplyVisualOverride();
	void SetupVisual();
	void PushGaspInputState();
	void TrackBrain(float DeltaSeconds);
	void BuildPose(const BoxRef::FInput& In, float DeltaSeconds);
	void UpdateMetrics(const BoxRef::FInput& In, float DeltaSeconds);
	void UpdateShots(const BoxRef::FInput& In);
	void LogSummary(const TCHAR* Why);
	void UpdateDebugCamera();
	bool bDebugCamPrereq = false;
	BoxRef::FV CameraForBrain(BoxRef::EPhase Phase) const;

	TWeakObjectPtr<ABoxingFightGameMode> Mode;
	BoxRef::FBrain Brain;
	BoxRef::FRefFrame Frame;
	FRefereePoseFrame Pose;
	bool bBrainInit = false;
	bool bHiddenForReplay = false;
	double OverT = 0.0;           // сек итога (без повтора)
	bool bOverStoppageLying = false;

	UPROPERTY(Transient)
	TObjectPtr<UChildActorComponent> VisualChild;

	UPROPERTY(Transient)
	TObjectPtr<UFightRefereeAnimInstance> VisualAnim;

	bool bVisualReady = false;
	int32 VisualTries = 0;
	float GaspStateTimer = 0.f;
	float HeadYaw = 0.f;
	float HeadPitch = 0.f;
	float Curl[2][5] = {};
	double Clock = 0.0;

	// --- отладка / метрики ---
	bool bLog = false;
	bool bShots = false;
	bool bDraw = false;
	float LogTimer = 0.f;
	int32 FightFrames = 0;      // кадров боя (Phase Fighting, никто не лежит)
	int32 OccludedFrames = 0;   // рефери закрывает бойца в кадре камеры
	float OccludedRun = 0.f;
	float OccludedWorst = 0.f;  // самый длинный подряд, с
	int32 OccludedAny = 0;      // то же во ВСЕХ фазах (постановка, счёт, итог)
	int32 AllFrames = 0;
	int32 SideOk = 0;           // в бою сбоку на 1.2–2 м (±0.15)
	float MinStandCm = 1e6f;    // до стоящего бойца (центры)
	float MinDownBodyCm = 1e6f; // S-62: от оси рефери до костей сбитого (голова, таз, кисти, предплечья, бёдра, голени, стопы) на счёте
	// S-66: лежащий — и на счёте, и после нокаута (итог, проигравший лежит).
	int32 LyingFrames = 0;      // кадров, пока сбитый лежит (тело на настиле)
	int32 OnBodyFrames = 0;     // рефери стоит на теле: ось ближе ON_BODY_CM к кости тела
	int32 LyingOccFrames = 0;   // рефери закрывает ЛЕЖАЩЕГО в кадре камеры
	float LyingOccRun = 0.f;
	float LyingOccWorst = 0.f;  // самый длинный подряд, с
	float MinLyingSettledCm = 1e6f; // до тела — когда тело уже лежит ≥ 1 с (падение на рефери не в счёт)
	float LyingT = 0.f;
	float MaxSpeedSeen = 0.f;   // м/с, ход логики
	float MaxLagCm = 0.f;       // отставание капсулы от места логики
	FVector PrevActorLoc = FVector::ZeroVector;
	bool bPrevLoc = false;
	int32 KdSeen = 0;
	bool bSummaryLogged = false;
	TSet<FString> ShotsTaken;
	double ModeSince = 0.0;
	int32 LastShotPhase = -1;
	double PhaseSince = 0.0;
};
