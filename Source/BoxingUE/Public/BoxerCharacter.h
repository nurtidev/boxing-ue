// ABoxerCharacter — визуальный боец (S-41, трек B).
//
// Логики боя здесь НЕТ: место, курс и всё состояние приходят из FBoxingFightCore через
// ABoxingFightGameMode::ApplyFightState каждый кадр. Персонаж только:
//  * доводит капсулу до точки ядра ЧЕРЕЗ CharacterMovement (скорость + входное ускорение), чтобы
//    AnimBP Game Animation Sample (Motion Matching) видел настоящие Velocity/Acceleration;
//  * выставляет состояние для AnimBP (BlueprintReadOnly, категория "Boxing|Anim");
//  * проигрывает монтажи ударов/защиты/реакций, синхронизируя кадр контакта с резолюцией в ядре;
//  * зовёт события OnPunchStarted / OnHitReceived / OnKnockdown / OnGetUp (+ делегаты).
//
// Ноги (локомоция) — AnimBP GASP: BP_Boxer = копия SandboxCharacter_CMC, перепривязанная к этому
// классу (Tools/EditorScripts/fight_blueprints.py). AnimBP берёт данные через интерфейс
// BPI_SandboxCharacter_Pawn, который реализован в самом BP; режим «стрейф + шаг» задаётся
// через Set_CharacterInputState (рефлексией, см. PushGaspInputState).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoxingFightBPTypes.h"
#include "BoxerCharacter.generated.h"

class UAnimMontage;
class UPhysicalAnimationComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBoxerPunchStartedSignature, EBoxPunchType, Punch, EBoxPunchTarget, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FBoxerHitReceivedSignature, EBoxPunchType, Punch, EBoxPunchTarget, Target, float, Magnitude, FVector, Direction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoxerSimpleSignature);

// Какой монтаж сейчас ведёт персонаж (приоритет: нокдаун > удар > уклон > реакция > блок).
UENUM(BlueprintType)
enum class EBoxMontageSlot : uint8
{
	None,
	Punch,
	Block,
	Slip,
	Hit,
	Knockdown,
	GetUp,
};

UCLASS(Blueprintable)
class BOXINGUE_API ABoxerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ABoxerCharacter();

	// ---------- Связка с GameMode ----------

	// Вызывается GameMode каждый кадр после шагов ядра. Snapshot — весь снимок (нужен соперник/фаза),
	// CoreTime — внутреннее время боя ядра (сек). WorldTarget — точка ядра в мире (см), YawDeg — курс.
	// DeltaSeconds — кадр игры: тут же выставляются скорость/ввод CharacterMovement (до его тика).
	void ApplyFightState(const FFightSnapshot& Snapshot, double CoreTime, const FVector& WorldTarget, float YawDeg, float DeltaSeconds);

	// Событие ядра, касающееся этого бойца (GameMode раздаёт обоим участникам).
	void HandleFightEvent(const FFightEvent& Event, const FVector& AttackerLocation);

	// Мгновенно поставить в точку ядра (старт боя/раунда), без скорости.
	void SnapToFightState(const FVector& WorldTarget, float YawDeg);

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	int32 FighterIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	TObjectPtr<ABoxerCharacter> Opponent;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	FBoxerPreset Preset;

	// ---------- Состояние для AnimBP (читать в Event Blueprint Update Animation через TryGetPawnOwner) ----------

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bPunching = false;

	// Текущий (или последний) удар.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchType CurrentPunch = EBoxPunchType::Jab;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchTarget PunchTarget = EBoxPunchTarget::Head;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchArm PunchArm = EBoxPunchArm::Lead;

	// 0..1 по времени цикла удара; контакт ядра — PunchContactFraction (≈ 0.45).
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float PunchPhase = 0.f;

	// 0..1, переложено так, что контакт = 0.5 (скраб клипа «контакт посередине», как в вебе).
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float PunchPhaseAnim = 0.f;

	// Длина цикла удара в ядре (с) и доля цикла, на которой ядро резолвит контакт.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float PunchDuration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float PunchContactFraction = 0.45f;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bBlocking = false;

	// 1 — свежий блок, 0 — руки забиты / вот-вот пробьют.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float GuardIntegrity = 1.f;

	// −1..1: вес нырка со знаком стороны (синус окна уклона); 0 — не в нырке.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float SlipAmount = 0.f;

	// −1 — влево, +1 — вправо, 0 — нет нырка.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	int32 SlipSide = 0;

	// 0..1 линейная фаза окна уклона.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float SlipPhase = 0.f;

	// Лежит (нокдаун или проигрыш досрочкой).
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bKnockedDown = false;

	// Лежит в финале боя (KO/RSC).
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bKO = false;

	// «Оглушён»: после тяжёлого попадания / провала / пробитого блока руки не слушаются.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bStunned = false;

	// 0..1 «встряска» от попадания (вес хит-реакции) и куда попали.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float Hurt = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchTarget HurtTarget = EBoxPunchTarget::Head;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxStepKind StepKind = EBoxStepKind::None;

	// 0 — центр, 1 — канаты, 2 — угол.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	int32 RopeLevel = 0;

	// 1 — победил, 0.5 — ничья, 0 — нет / бой идёт.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float Victory = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bDefeated = false;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|HUD")
	float Health = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|HUD")
	float StaminaPct = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|HUD")
	int32 Knockdowns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|HUD")
	bool bGassed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxMontageSlot ActiveMontageSlot = EBoxMontageSlot::None;

	// ---------- Монтажи (трек C: /Game/Boxing/Anim/AM_*) ----------
	// Пустые слоты при BeginPlay подхватываются по имени из MontageFolder (AM_Jab, AM_Cross, AM_HookL,
	// AM_HookR, AM_UpperL, AM_UpperR, AM_Block, AM_SlipL, AM_SlipR, AM_HitHead, AM_HitBody, AM_Knockdown, AM_GetUp).
	// Монтажи играют в слоте DefaultSlot AnimBP GASP (полное тело поверх локомоции).

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TMap<EBoxPunchType, TObjectPtr<UAnimMontage>> PunchMontages;

	// Время кадра контакта в монтаже удара (сек от начала монтажа). Нет записи / ≤ 0 — ищется
	// AnimNotify с именем «Contact» (или «Hit») в монтаже, иначе — середина монтажа (как в вебе).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TMap<EBoxPunchType, float> PunchContactTimes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> BlockMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> SlipLeftMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> SlipRightMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> HitHeadMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> HitBodyMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> KnockdownMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> GetUpMontage;

	// Папка автоподхвата монтажей (долгое имя пакета).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	FString MontageFolder = TEXT("/Game/Boxing/Anim");

	// Окно уклона ядра (SLIP_WINDOW) — монтаж уклона растягивается на него целиком.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float SlipWindowSeconds = 0.36f;

	// Рассинхрон позиции монтажа с фазой ядра, после которого позиция подтягивается рывком (сек).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float MontageSnapTolerance = 0.035f;

	// Реакции на попадание монтажом — только при Magnitude не меньше порога.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float HitMontageMinMagnitude = 0.35f;

	// Время контакта для монтажа удара (сек от начала монтажа): PunchContactTimes → notify «Contact» → середина.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Montages")
	float GetPunchContactTime(EBoxPunchType Punch) const;

	// ---------- События (BP может переопределить; C++-реализация по умолчанию играет монтажи) ----------

	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnPunchStarted(EBoxPunchType Punch, EBoxPunchTarget Target);

	// Direction — единичный вектор в мире от атакующего к защитнику (горизонтальный).
	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnHitReceived(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction);

	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnKnockdown();

	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnGetUp();

	// Удар принят в блок (Magnitude — утечка сквозь блок).
	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnBlockedPunch(EBoxPunchType Punch, float Magnitude, FVector Direction);

	// Физическая реакция на попадание (трек C). C++-заглушка: импульс в кость через
	// UPhysicalAnimationComponent с частичной смесью физики, гаснет за PhysHitDuration.
	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void HitReaction(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction);

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerPunchStartedSignature OnPunchStartedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerHitReceivedSignature OnHitReceivedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerSimpleSignature OnKnockdownDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerSimpleSignature OnGetUpDelegate;

	// ---------- Физреакция (заглушка) ----------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boxing|Physics")
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	// ВЫКЛ по умолчанию: на меше GASP (UEFN-манекен + SandboxCharacter_CMC_ABP) частичная симуляция тел
	// роняет движок через кадр после включения (Array index out of bounds: 64 into an array of size 0 в
	// смешивании физики с позой) — см. Docs/FIGHT_GAMEPLAY.md. Трек C: физреакция через PhysicsControl/
	// RigidBody-узел в AnimBP, либо разобраться с этим путём.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	bool bPhysicalHitReactions = false;

	// Пружины UPhysicalAnimationComponent к анимационной позе (иначе — чистая симуляция с частичной смесью).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	bool bUsePhysicalAnimationDrive = true;

	// Кость, ниже которой включается физика при попадании в голову / в корпус.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	FName PhysHeadBone = TEXT("neck_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	FName PhysBodyBone = TEXT("spine_04");

	// Импульс (изменение скорости, см/с) на единицу Magnitude.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysImpulsePerMagnitude = 220.f;

	// Пиковая доля физики в позе и время её угасания (с).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysBlendPeak = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysHitDuration = 0.35f;

	// ---------- Локомоция ----------

	// Шаг GASP «ходьба» (иначе бег) и стрейф (лицом к сопернику) — через Set_CharacterInputState.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Locomotion")
	bool bGaspWantsToWalk = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Locomotion")
	bool bGaspWantsToStrafe = true;

	// Ошибка дальше этой — телепорт (сброс раунда), а не ход (см).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Locomotion")
	float TeleportDistance = 120.f;

	// Потолок скорости слежения за точкой ядра (см/с).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Locomotion")
	float MaxTrackSpeed = 700.f;

	// Опциональная «визуальная подмена» (BP_Kellan / BP_Manny из GASP): child actor на меше,
	// ретаргет позы с базового манекена (ABP_GenericRetarget). Пусто — виден сам манекен.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Visual")
	TSubclassOf<AActor> VisualOverrideClass;

	// Последняя точка ядра в мире (см) — для отладки/камеры.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	FVector FightTarget = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	float FightYaw = 0.f;

	// Скорость точки ядра (см/с), оценка по двум последним снимкам — упреждение слежения.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	FVector FightTargetVelocity = FVector::ZeroVector;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;

private:
	void LoadDefaultMontages();
	UAnimMontage* FindMontage(const TCHAR* Name) const;
	UAnimInstance* GetAnimInst() const;

	// Монтажи: старт/слежение/стоп.
	bool PlaySlotMontage(EBoxMontageSlot Slot, UAnimMontage* Montage, float Rate, float StartPos = 0.f);
	void StopSlotMontage(float BlendOut);
	void UpdateMontages();
	void SyncPunchMontage();
	void SyncSlipMontage();
	void HoldAtEnd();

	void TrackFightTarget(float DeltaSeconds);
	void PushGaspInputState();
	void UpdatePhysicalReaction(float DeltaSeconds);
	void ApplyVisualOverride();

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMontage;

	bool bWasDown = false;
	bool bWasPunching = false;
	bool bWasBlocking = false;
	int32 PrevSlipSide = 0;
	double LastPunchStart = -100.0;
	double CoreNow = 0.0;
	double PrevTargetCoreTime = -1.0;
	FVector PrevTargetPos = FVector::ZeroVector;
	bool bHasTarget = false;
	bool bGaspStateSent = false;
	float GaspStateTimer = 0.f;

	// Физреакция.
	FName PhysBone = NAME_None;
	float PhysTimeLeft = 0.f;
};
