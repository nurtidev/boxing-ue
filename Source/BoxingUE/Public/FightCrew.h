// S-71 (game feel): угловые в бою и сцена перерыва — порт web/src/ui/three/crew.ts + CornerCrew.tsx.
//
//  * BoxCrew — чистая логика без UE-акторов (тест BoxingUE.FightCrew): переход «пол у помоста → апрон за канатами»
//    (crewPlace), реакции угла на бой (crewReaction / CrewMood), поза углового на кадр (crewPose: наклон, руки,
//    взгляд), направления рук из углов веба.
//  * AFightCrewMember — тело одного углового: персонаж GASP (BP_CornerCrew = копия SandboxCharacter_CMC,
//    перепривязанная к этому классу, Tools/EditorScripts/feel_crew_bp.py) — ноги Motion Matching (ход
//    CharacterMovement'ом к месту логики), облик — child actor BP_CornerCoach_<угол> / BP_Cutman_<угол> (tech-artist,
//    Docs/LOOK.md «S-68») с записью Appearance.json `crew:<id бойца>:coach|cutman`; жесты — процедурно на костях
//    видимого меша (как рефери): наклон к бойцу, руки (на край помоста / канат / «да!» / тревога), IK кисти —
//    бутылка катмена к губам сидящего бойца, ладонь к лицу (протирает).
//  * ABoxingCornerCrew — менеджер: спаун 4 угловых и 2 стульев по маркерам L_Ring (CornerCrew_<угол>_<роль>_<Fight|Rest>,
//    CornerStool_<угол>[_Stow]), настроение углов по событиям ядра (GameMode → NotifyEvent), стадия Rest — наверх
//    и стул под севшего бойца (BoxerSit.h), по «Продолжить»/гонгу — стул убирается, угловые спускаются.
// Ядро и его ГСЧ не трогаются: угловые только читают снимок и события.
//
// Командная строка: -BoxNoCrew (без угловых), -BoxCrewLog (лог стадий/мест раз в 1 с), -BoxCrewShots=<префикс>
// (скриншоты Docs/screens/<префикс>_*.png: бой с угловыми, перерыв — идёт, сел, бутылка, разговор, выход),
// -BoxCrewShadow=1 (тени угловых — по умолчанию выкл., PERF.md S-68), -BoxCrewLOD=N (0 — авто; по умолчанию 1).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoneIndices.h"
#include "FightTypes.h"
#include "FightCrew.generated.h"

class ABoxingFightGameMode;
class ABoxerCharacter;
class AFightCrewMember;
class UChildActorComponent;
class UIKRetargeter;
struct FCompactPose;
struct FBoneContainer;

namespace BoxCrew
{
	enum class ERole : uint8 { Coach, Cutman };

	// crew.ts: переход пол ↔ апрон — скорость доли в секунду (вверх / вниз).
	constexpr float UP_RATE = 0.9f;
	constexpr float DOWN_RATE = 1.1f;
	constexpr float LEAN_RATE = 2.2f;
	constexpr float STOOL_RATE = 4.f;
	// Порог «тяжёлого» попадания для реакции угла (медиана mag ≈ 0.7; хит-стоп — с 1.1).
	constexpr float REACT_MAG = 0.95f;
	// Опущенная рука от горизонтали T-позы (веб ARM_HANG).
	constexpr float ARM_HANG = 1.42f;
	// Высота сиденья: таз сидя над настилом минус полтолщины ягодиц (веб SEAT_OF_HEIGHT, доля роста), меш стула — 52 см.
	constexpr float SEAT_OF_HEIGHT = 0.255f;
	constexpr float STOOL_MESH_CM = 52.f;
	// Катмен в перерыве — ближе к столбу, чем маркер CornerCrew_<угол>_Cutman_Rest (см): дотянуться до губ бойца.
	constexpr float CUTMAN_REST_IN = 50.f;

	// Место на кадре между боевой точкой A (пол у помоста) и точкой перерыва B (апрон): по горизонтали — smoothstep,
	// по высоте — с опережением (сначала поднялся, потом подошёл к канатам; вниз — наоборот). crewPlace веба.
	BOXINGUE_API FVector PlaceBetween(const FVector& A, const FVector& B, float U);
	// Скорость по пути при шаге доли dU/dt (см/с, мир) — для Motion Matching (ход, а не скольжение).
	BOXINGUE_API FVector PlaceVelocity(const FVector& A, const FVector& B, float U, float DuDt);

	// Места по формулам веба (м, оси ядра X/Z; Y — высота ступней: 0 — настил/апрон, −1.1 — пол арены) — фолбэк, если в
	// уровне нет маркеров; совпадают с маркерами build_ring.py.
	BOXINGUE_API FVector SpotCore(int32 Corner, ERole Role, bool bRest);
	BOXINGUE_API FVector StoolCore(int32 Corner, bool bStow);

	// Всплеск эмоций угла (crewReaction): cheer — свой попал тяжело / уронил, worry — своего бьют тяжело / он на полу.
	struct FKick
	{
		int32 Corner = 0;
		float Cheer = 0.f;
		float Worry = 0.f;
	};
	// До двух всплесков на событие (по углу каждого бойца); 0 — событие угловых не трогает.
	BOXINGUE_API int32 Reaction(const FFightEvent& E, FKick Out[2]);

	// Состояние угла (CrewMood веба).
	struct BOXINGUE_API FMood
	{
		float Cheer = 0.f;
		float Worry = 0.f;
		float CheerT = 99.f;
		float WorryT = 99.f;
		float Up = 0.f;    // 0 — у помоста, 1 — на апроне
		float Lean = 0.f;  // 0 — стоит, 1 — наклонился к сидящему бойцу
		float Stool = 0.f; // 0 — стула нет, 1 — стоит в углу
		float RestT = 0.f; // с с посадки бойца (сценарий катмена: бутылка → протирает)

		void Kick(const FKick& K);
		// bRest — перерыв (наверх), bSeated — боец сел (наклон, стул).
		void Update(float Dt, bool bRest, bool bSeated);
	};

	// Рука (web ArmPose): подъём вперёд/вверх, отведение в сторону, сгиб в локте (рад).
	struct FArm
	{
		float Fwd = 0.08f;
		float Out = 0.f;
		float Elbow = 0.25f;
	};
	BOXINGUE_API FArm MixArm(const FArm& A, const FArm& B, float K);
	// Направления плеча и предплечья в осях углового (F — вперёд, L — влево, U — вверх): Side 0 — левая, 1 — правая.
	BOXINGUE_API void ArmDirs(const FArm& A, int32 Side, const FVector& F, const FVector& L, const FVector& U, FVector& OutUpper, FVector& OutFore);

	// Поза корпуса и рук на кадр (crewPose веба): YawToFighter/PitchToFighter — куда смотреть (рад, + — влево / вниз).
	struct FBody
	{
		float Lean = 0.f;
		float Twist = 0.f;
		float HeadYaw = 0.f;
		float HeadPitch = 0.f;
		FArm Arm[2];
		// Сценарий катмена в перерыве (0..1): бутылка к губам (левая кисть), ладонь к лицу бойца (правая).
		float Bottle = 0.f;
		float Wipe = 0.f;
	};
	BOXINGUE_API FBody Pose(ERole Role, const FMood& M, float T, float Phase, float YawToFighter, float PitchToFighter);

	// Сценарий катмена от посадки бойца (с): доли «бутылка» и «протирает» 0..1 (плавные, не одновременно).
	BOXINGUE_API void CutmanScript(float RestT, float& OutBottle, float& OutWipe);
}

// Кадр позы углового для анимпотока (мир).
struct FCrewPoseFrame
{
	bool bValid = false;
	FVector Fwd = FVector::ForwardVector;
	FVector Left = -FVector::RightVector;
	float Lean = 0.f;
	float Twist = 0.f;
	float HeadYaw = 0.f;
	float HeadPitch = 0.f;
	FVector Upper[2] = {FVector::DownVector, FVector::DownVector};
	FVector Fore[2] = {FVector::DownVector, FVector::DownVector};
	float ArmW[2] = {0.f, 0.f};        // доля жеста (0 — руки локомоции GASP: идёт)
	FVector IkTarget[2] = {FVector::ZeroVector, FVector::ZeroVector}; // кисть (кость hand_*) — сюда (мир)
	float IkW[2] = {0.f, 0.f};
	float BodyW = 1.f;                 // доля наклона/поворота корпуса (на ходу — меньше)
};

class FCrewPoseFx
{
public:
	void Apply(FCompactPose& Pose, const FCrewPoseFrame& Frame, const FTransform& CompToWorld);

private:
	void Resolve(const FBoneContainer& Bones);
	uint16 Serial = MAX_uint16;
	const void* ContainerPtr = nullptr;
	FCompactPoseBoneIndex Spine[3] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Neck = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Head = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Upper[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Lower[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex Hand[2] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
};

struct FCrewVisualRootNode : public FAnimNode_Base
{
	FAnimNode_RetargetPoseFromMesh Retarget;
	FCrewPoseFx Fx;
	FCrewPoseFrame Frame;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

struct FCrewVisualProxy : public FAnimInstanceProxy
{
	FCrewVisualProxy() = default;
	explicit FCrewVisualProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &Root; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	FCrewVisualRootNode Root;
};

// Видимый меш углового: ретаргет позы логического манекена GASP + жесты.
UCLASS(Transient, NotBlueprintable)
class BOXINGUE_API UFightCrewAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetupRetarget(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter, const FRetargetProfile* Profile);

	TWeakObjectPtr<AFightCrewMember> Member;

	UPROPERTY(Transient)
	TObjectPtr<UIKRetargeter> RetargeterRef;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCrewVisualProxy(this); }
	friend struct FCrewVisualProxy;
};

// Что менеджер задаёт угловому на кадр.
struct FCrewDrive
{
	FVector Feet = FVector::ZeroVector;     // где стоять (мир, ступни)
	FVector Vel = FVector::ZeroVector;      // ход точки (см/с) — упреждение и Motion Matching
	FVector FaceTo = FVector::ZeroVector;   // куда корпусом
	FVector Fighter = FVector::ZeroVector;  // голова своего бойца (мир) — взгляд
	FVector FighterFwd = FVector::ForwardVector;
	bool bClimb = false;                    // переход пол ↔ апрон: без коллизии с помостом (полёт)
	const BoxCrew::FMood* Mood = nullptr;
	float Time = 0.f;
};

UCLASS(Blueprintable)
class BOXINGUE_API AFightCrewMember : public ACharacter
{
	GENERATED_BODY()

public:
	AFightCrewMember();

	int32 Corner = 0;
	BoxCrew::ERole Role = BoxCrew::ERole::Coach;
	float Phase = 0.f; // сдвиг «дыхания»/жестов, чтобы четверо не качались в такт

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Crew")
	TSubclassOf<AActor> VisualOverrideClass;

	// Облик (Appearance.json): основная запись и запасная (угол), см. Docs/LOOK.md «S-68».
	FString LookId;
	FString LookFallbackId;
	bool bShadows = false;
	int32 ForcedLod = 1;

	void Drive(const FCrewDrive& D, float DeltaSeconds);
	void SnapTo(const FVector& Feet, const FVector& FaceTo);
	void SetHiddenAll(bool bHide);
	const FCrewPoseFrame& GetPoseFrame() const { return PoseFrame; }
	bool IsVisualReady() const { return bVisualReady && VisualAnim != nullptr; }
	// Ступни (мир): центр капсулы − полувысота.
	FVector GetFeet() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ApplyVisualOverride();
	void SetupVisual();
	float HalfHeight() const;

	FCrewPoseFrame PoseFrame;
	FCrewDrive Last;
	bool bHasDrive = false;
	bool bClimbing = false;
	float GaspStateTimer = 0.f;
	float HeadYaw = 0.f;
	float HeadPitch = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UChildActorComponent> VisualChild;

	UPROPERTY(Transient)
	TObjectPtr<UFightCrewAnimInstance> VisualAnim;

	bool bVisualReady = false;
	int32 VisualTries = 0;
};

UCLASS()
class BOXINGUE_API ABoxingCornerCrew : public AActor
{
	GENERATED_BODY()

public:
	ABoxingCornerCrew();

	// Спаун на бой (GameMode, после бойцов и рефери). nullptr — -BoxNoCrew или нет классов (Content/BoxingLocal вне git).
	static ABoxingCornerCrew* SpawnFor(ABoxingFightGameMode* GM);
	// Событие ядра (GameMode::DispatchEvents) — реакции углов.
	static void NotifyEvent(const UObject* WorldContext, const FFightEvent& E);
	static ABoxingCornerCrew* Find(const UWorld* World);

	UPROPERTY(EditAnywhere, Category = "Boxing|Crew")
	FSoftClassPath CrewClassPath = FSoftClassPath(TEXT("/Game/Boxing/Blueprints/BP_CornerCrew.BP_CornerCrew_C"));

	// Облик: {c} — Red/Blue.
	UPROPERTY(EditAnywhere, Category = "Boxing|Crew")
	FString CoachLookPath = TEXT("/Game/BoxingLocal/Characters/BP_CornerCoach_{c}.BP_CornerCoach_{c}_C");

	UPROPERTY(EditAnywhere, Category = "Boxing|Crew")
	FString CutmanLookPath = TEXT("/Game/BoxingLocal/Characters/BP_Cutman_{c}.BP_Cutman_{c}_C");

	UPROPERTY(EditAnywhere, Category = "Boxing|Crew")
	FString StoolPath = TEXT("/Game/Boxing/Characters/BP_CornerStool_{c}.BP_CornerStool_{c}_C");

	const BoxCrew::FMood& GetMood(int32 Corner) const { return Mood[FMath::Clamp(Corner, 0, 1)]; }
	AFightCrewMember* GetMember(int32 InCorner, BoxCrew::ERole InRole) const;

	void OnFightEvent(const FFightEvent& E);

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Setup(ABoxingFightGameMode* GM);
	void ReadMarkers();
	FVector MarkerOr(const FString& Name, const FVector& Fallback) const;
	void UpdateStool(int32 C, float DeltaSeconds);
	void UpdateShots(float DeltaSeconds);
	void Shot(const FString& Name);

	TWeakObjectPtr<ABoxingFightGameMode> Mode;
	BoxCrew::FMood Mood[2];

	UPROPERTY(Transient)
	TObjectPtr<AFightCrewMember> Members[4]; // [угол*2 + роль]

	UPROPERTY(Transient)
	TObjectPtr<AActor> Stools[2];

	// Точки (мир): [угол][роль][0 — бой, 1 — перерыв] (ступни), стул [угол][0 — в углу, 1 — убран].
	FVector Spot[2][2][2];
	FVector StoolAt[2][2];
	float SeatCm[2] = {45.f, 45.f};
	float Clock = 0.f;
	bool bHidden = false;
	bool bLog = false;
	float LogTimer = 0.f;
	FString ShotPrefix;
	TSet<FString> ShotsTaken;
	float RestFor = 0.f;
	float SeatedFor = 0.f;
	float OutFor = -1.f;
	int32 RestsSeen = 0;
	bool bWasRest = false;
	float FightFor = 0.f;
};
