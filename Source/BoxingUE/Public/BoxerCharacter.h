// ABoxerCharacter — визуальный боец (S-41, трек B).
//
// Логики боя здесь НЕТ: место, курс и всё состояние приходят из FBoxingFightCore через
// ABoxingFightGameMode каждый кадр (ApplyFightState / HandleFightEvent). Персонаж только:
//  * доводит капсулу до точки ядра ЧЕРЕЗ CharacterMovement (скорость + входное ускорение), чтобы
//    AnimBP Game Animation Sample (Motion Matching) видел настоящие Velocity/Acceleration;
//  * выставляет состояние для AnimBP (BlueprintReadOnly, категория "Boxing|Anim");
//  * проигрывает монтажи трека C (UEFN-скелет, /Game/BoxingLocal/Anim/AM_*), синхронизируя кадр
//    контакта (нотифай «Contact») с резолюцией удара в ядре;
//  * зовёт события OnPunchStarted / OnHitReceived / OnBlockedPunch / OnKnockdown / OnGetUp (+ делегаты);
//  * физреакция на попадание на ВИДИМОМ меше (MetaHuman/Manny) через UPhysicalAnimationComponent
//    и таблицу пружин DT_HitReaction_PhysAnim (Docs/HIT_REACTION.md).
//
// Ноги (локомоция) — AnimBP GASP: BP_Boxer = копия SandboxCharacter_CMC, перепривязанная к этому
// классу, с AnimClass = ABP_Boxer (Tools/EditorScripts/fight_blueprints.py). Видимый персонаж —
// child actor (BP_Kellan / BP_Manny из GASP), копирует позу ретаргетом (ABP_GenericRetarget).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoxingFightBPTypes.h"
#include "BoxerFeel.h"
#include "BoxerAnimInstances.h"
#include "BoxerCharacter.generated.h"

class UAnimMontage;
class UChildActorComponent;
class UDataTable;
class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBoxerPunchStartedSignature, EBoxPunchType, Punch, EBoxPunchTarget, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FBoxerHitReceivedSignature, EBoxPunchType, Punch, EBoxPunchTarget, Target, float, Magnitude, FVector, Direction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoxerSimpleSignature);

// Какой монтаж сейчас ведёт персонаж (приоритет: финал > нокдаун > удар > уклон > реакция/блок).
UENUM(BlueprintType)
enum class EBoxMontageSlot : uint8
{
	None,
	Punch,
	Block,
	BlockHit,
	Slip,
	Hit,
	Knockdown,
	GetUp,
	Finale,
	Guard,
};

UCLASS(Blueprintable)
class BOXINGUE_API ABoxerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ABoxerCharacter();

	// ---------- Связка с GameMode ----------

	// Вызывается GameMode каждый кадр после шагов ядра. CoreTime — время боя ядра (сек),
	// WorldTarget — точка ядра в мире (см), YawDeg — курс; DeltaSeconds — кадр игры: тут же
	// выставляются скорость/ввод CharacterMovement (он тикает после GameMode).
	void ApplyFightState(const FFightSnapshot& Snapshot, double CoreTime, const FVector& WorldTarget, float YawDeg, float DeltaSeconds);

	// Событие ядра (GameMode раздаёт обоим, реагирует защищающийся).
	void HandleFightEvent(const FFightEvent& Event, const FVector& AttackerLocation);

	// Мгновенно поставить в точку ядра (старт боя/раунда), без скорости.
	void SnapToFightState(const FVector& WorldTarget, float YawDeg);

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	int32 FighterIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	TObjectPtr<ABoxerCharacter> Opponent;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing")
	FBoxerPreset Preset;

	// ---------- Состояние для AnimBP (TryGetPawnOwner → Cast to BoxerCharacter) ----------

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	bool bPunching = false;

	// Текущий (или последний) удар.
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchType CurrentPunch = EBoxPunchType::Jab;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchTarget PunchTarget = EBoxPunchTarget::Head;

	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	EBoxPunchArm PunchArm = EBoxPunchArm::Lead;

	// 0..1 по времени цикла удара; контакт ядра — на PunchContactFraction (≈ 0.45).
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float PunchPhase = 0.f;

	// 0..1, переложено так, что контакт = 0.5 (скраб клипа «контакт посередине», как в вебе).
	UPROPERTY(BlueprintReadOnly, Category = "Boxing|Anim")
	float PunchPhaseAnim = 0.f;

	// Длина цикла удара в ядре (с).
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

	// ---------- Монтажи (трек C, Docs/ANIM_CONTACTS.md) ----------
	// Пустые слоты на BeginPlay подхватываются по имени AM_<Имя> из MontageFolders (по порядку):
	// Jab, Cross, HookL, HookR, UpperL, UpperR, BodyHook, Block, BlockHit, SlipL, SlipR, HitHead, HitBody,
	// Guard (слот UpperBody), Knockdown, Knockout, GetUp, Victory, Defeat (DefaultSlot).

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TArray<FString> MontageFolders = {TEXT("/Game/BoxingLocal/Anim"), TEXT("/Game/Boxing/Anim")};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TMap<EBoxPunchType, TObjectPtr<UAnimMontage>> PunchMontages;

	// Время кадра контакта (сек от начала монтажа). Нет записи / ≤ 0 — нотифай «Contact» монтажа,
	// иначе — середина монтажа.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TMap<EBoxPunchType, float> PunchContactTimes;

	// Хук в корпус (HookL/HookR + цель Body). Пусто — обычный монтаж хука.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> BodyHookMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> BlockMontage;

	// Попадание в блок (удар принят на руки).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> BlockHitMontage;

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

	// Падение, после которого боец не встаёт (досрочка в финале).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> KnockoutMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> GetUpMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> VictoryMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> DefeatMontage;

	// Боевая стойка (AM_Guard, петля в DefaultSlot): idle GASP — руки вниз, поэтому стоящий боец
	// (скорость < GuardStartSpeed дольше GuardSettleSeconds, нет удара/реакции) переходит в стойку,
	// а на ходу (скорость > GuardStopSpeed) — обратно в локомоцию GASP; бленд GuardBlendSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	TObjectPtr<UAnimMontage> GuardMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	bool bPlayGuardMontage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float GuardStartSpeed = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float GuardStopSpeed = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float GuardSettleSeconds = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float GuardBlendSeconds = 0.2f;

	// Окно уклона ядра (SLIP_WINDOW) — пик монтажа уклона (нотифай «Peak») ставится на середину окна.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float SlipWindowSeconds = 0.36f;

	// Отход после контакта/пика проигрывается не дольше (время до контакта × фактор): хвост клипа
	// Mixamo длинный (джеб 1.3 с после контакта), а ядро даёт на возврат ~0.2 с.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float RecoverFactor = 1.3f;

	// Подъём рук в блок (с до нотифая «GuardUp» монтажа блока).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float BlockRaiseSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float HitMontageRate = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float GetUpMontageRate = 1.3f;

	// Рассинхрон позиции монтажа с фазой ядра, после которого позиция подтягивается рывком (сек).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float MontageSnapTolerance = 0.035f;

	// Реакции на попадание монтажом — только при Magnitude не меньше порога.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Montages")
	float HitMontageMinMagnitude = 0.35f;

	// Время контакта для монтажа удара в голову (сек от начала): PunchContactTimes → «Contact» → середина.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Montages")
	float GetPunchContactTime(EBoxPunchType Punch) const;

	// Время нотифая монтажа по имени (AnimNotify_PlayMontageNotify NotifyName или имя нотифая); −1 — нет.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Montages")
	static float GetMontageNotifyTime(const UAnimMontage* Montage, FName NotifyName);

	// Монтаж для удара с учётом цели (хук в корпус → BodyHookMontage).
	UFUNCTION(BlueprintCallable, Category = "Boxing|Montages")
	UAnimMontage* GetPunchMontage(EBoxPunchType Punch, EBoxPunchTarget Target) const;

	// ---------- События (BP может переопределить; C++-реализация по умолчанию играет монтажи) ----------

	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnPunchStarted(EBoxPunchType Punch, EBoxPunchTarget Target);

	// Direction — единичный вектор в мире от атакующего к защитнику (горизонтальный).
	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnHitReceived(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction);

	// Удар принят в блок (Magnitude — утечка сквозь блок).
	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnBlockedPunch(EBoxPunchType Punch, float Magnitude, FVector Direction);

	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnKnockdown();

	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void OnGetUp();

	// Физическая реакция на попадание (зовётся из OnHitReceived и, с bBlocked, из OnBlockedPunch).
	UFUNCTION(BlueprintNativeEvent, Category = "Boxing|Events")
	void HitReaction(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, FVector Direction, bool bBlocked);

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerPunchStartedSignature OnPunchStartedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerHitReceivedSignature OnHitReceivedDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerSimpleSignature OnKnockdownDelegate;

	UPROPERTY(BlueprintAssignable, Category = "Boxing|Events")
	FBoxerSimpleSignature OnGetUpDelegate;

	// ---------- Физреакция (Docs/HIT_REACTION.md, подход 1) ----------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boxing|Physics")
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	// Физика только на ВИДИМОМ меше подмены (MetaHuman/Manny); без подмены — выкл. (на логическом
	// UEFN-меше GASP симуляция роняла движок, см. Docs/FIGHT_GAMEPLAY.md). Командная строка: -BoxPhysHits=0/1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	bool bPhysicalHitReactions = false;

	// Таблица пружин FPhysicalAnimationData по костям (строки применяются по порядку).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	TSoftObjectPtr<UDataTable> HitReactionTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Boxing/Anim/DT_HitReaction_PhysAnim.DT_HitReaction_PhysAnim")));

	// Корень симулируемой части (ноги и таз — анимация).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	FName PhysRootBone = TEXT("spine_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	FName PhysHeadBone = TEXT("head");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	FName PhysBodyBone = TEXT("spine_03");

	// Огибающая смеси физики: пик = clamp(Base + PerMag × mag, 0, Max), экспоненциальный спад.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysBlendBase = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysBlendPerMag = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysBlendMax = 0.85f;

	// Постоянная времени спада смеси (с): ~0.35 с до нуля.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysBlendTau = 0.1f;

	// Масштаб импульсов таблицы HIT_REACTION.md (см/с при mag 1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Physics")
	float PhysImpulseScale = 1.f;

	// Видимый меш, на котором работает физреакция (после инициализации; иначе null).
	UFUNCTION(BlueprintPure, Category = "Boxing|Physics")
	USkeletalMeshComponent* GetVisibleMesh() const;

	// Текущая доля физики в позе (огибающая) — для отладки.
	UFUNCTION(BlueprintPure, Category = "Boxing|Physics")
	float GetPhysBlend() const { return PhysBlend; }

	// ---------- «Ощущение удара» (Docs/FIGHT_FEEL.md): слой верха тела, наведение кулака, реакция ----------

	// Всё вместе (слой верха, наведение, подшаг, пружины реакции). Командная строка: -BoxFeel=0 — как до S-41 feel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	bool bFeel = true;

	// Удары/блок/уклоны/реакции — только верх тела (пост-процесс логического меша), ноги — локомоция.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	bool bUpperBodyLayer = true;

	// Кость кисти → фронт кулака (см): у голой руки ≈ 9–10, в перчатке +4–5.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float FistReachCm = 10.f;

	// Подшаг корпуса к цели, если рука не достаёт (см, пик в кадре контакта).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float MaxLungeCm = 30.f;

	// Цели: центр головы над костью head, радиусы «поверхностей» (голова, корпус, перчатки в блоке), см.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float HeadCenterUpCm = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float HeadRadiusCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float BodyRadiusCm = 13.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float GuardRadiusCm = 5.f;

	// Множитель пружин реакции (1 — как в вебе; 1.4 — камера UE ближе и Kellan крупнее, при 1 откид головы ~13° почти не читался).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float ReactionGain = 1.4f;

	// Доля корпуса/головы из клипа реакции (AM_HitHead/Body) — клип лишь подложка, как в вебе (0.35).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Feel")
	float HitMontageTorsoAlpha = 0.35f;

	// Кадр для анимпотока (наведение + каналы реакции), обновляется в Tick.
	const FBoxerFeelFrame& GetFeelFrame() const { return Feel; }
	FBoxerLayerParams GetLayerParams() const;

	// Меш, который виден (MetaHuman Body после подмены; иначе логический манекен).
	USkeletalMeshComponent* GetFeelMesh() const;

	// Отладка: зазор «фронт кулака → поверхность цели» в последнем кадре наведения (см; −1 — нет наведения).
	FBoxerFeelDebug GetFeelDebug() const;

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

	// Визуальная подмена (BP_Kellan / BP_Manny из GASP): child actor на меше, ретаргет позы с
	// логического манекена (ABP_GenericRetarget); сам манекен скрывается. Пусто — виден манекен.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boxing|Visual")
	TSubclassOf<AActor> VisualOverrideClass;

	// Последняя точка ядра в мире (см) и курс — для отладки/камеры.
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
	void UpdateMontages(float DeltaSeconds);
	// Скраб «до пика / после пика»: Phase 0..1 цикла ядра, пик ядра на PeakFrac (доля), длина цикла
	// CycleSeconds; пик монтажа — PeakTime (сек). Отход после пика ужат до RecoverFactor × PeakTime.
	void SyncPeakMontage(float Phase, float PeakFrac, float CycleSeconds, float PeakTime);
	void HoldAt(float Time);
	void HoldAtEnd();
	void StartBlockMontage(float Lead);
	float BlockGuardUpTime() const;

	void TrackFightTarget(float DeltaSeconds);
	void PushGaspInputState();
	void ApplyVisualOverride();

	void InitPhysics();
	void UpdatePhysics(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMontage;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> PhysMesh;

	UPROPERTY(Transient)
	TObjectPtr<UChildActorComponent> VisualChild;

	bool bWasDown = false;
	bool bWasPunching = false;
	bool bWasBlocking = false;
	bool bWasFinale = false;
	int32 PrevSlipSide = 0;
	double LastPunchContact = -100.0;
	double CoreNow = 0.0;
	double PrevTargetCoreTime = -1.0;
	FVector PrevTargetPos = FVector::ZeroVector;
	bool bHasTarget = false;
	bool bGaspStateSent = false;
	float GaspStateTimer = 0.f;
	EBoxPunchTarget ActivePunchTarget = EBoxPunchTarget::Head;

	// Физреакция.
	bool bPhysReady = false;
	int32 PhysInitTries = 0;
	float PhysBlend = 0.f;

	// Стойка: сколько боец уже стоит (с).
	float GuardStill = 0.f;

	// ---------- Feel ----------
	void SetupFeel();
	void UpdateFeel(float DeltaSeconds);
	void UpdateGuards(float DeltaSeconds);
	bool CaptureAim(EBoxPunchType Punch, EBoxPunchTarget Target);
	bool AimCenterNow(FVector& OutCenter) const;
	void KickReaction(EBoxFeelEvent Kind, EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, bool bSlipped);
	UAnimInstance* AnimFor(EBoxMontageSlot Slot) const;
	static bool IsUpperSlot(EBoxMontageSlot Slot);

	// Пост-процесс логического меша (слой верха) и AnimInstance видимого меша (ретаргет + процедурный слой).
	UPROPERTY(Transient)
	TObjectPtr<UBoxerLayerAnimInstance> UpperAnim;

	UPROPERTY(Transient)
	TObjectPtr<UBoxerVisualAnimInstance> VisualAnim;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> VisualMesh;

	// Экземпляр, на котором играет ActiveMontage.
	TWeakObjectPtr<UAnimInstance> ActiveAnim;

	FBoxerReactionRig React;
	FBoxerFeelFrame Feel;
	bool bFeelReady = false;
	int32 FeelInitTries = 0;

	// Наведение текущего удара: центр цели в системе СОПЕРНИКА (следует за ним, но не за его нырком/реакцией).
	bool bAimCaptured = false;
	bool bAimLeftArm = true;
	FVector AimCenterLocal = FVector::ZeroVector;
	FVector AimApproach = FVector::ForwardVector;
	float AimRadius = 10.f;
	int32 AimKind = 0; // 0 — голова, 1 — корпус, 2 — перчатки (блок)
};
