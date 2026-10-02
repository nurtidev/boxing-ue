// Нативные (C++) AnimInstance бойца — без узлов в AnimGraph (Python их добавлять не умеет), S-41 game feel.
//
// UBoxerLayerAnimInstance — ПОСТ-ПРОЦЕСС логического UEFN-меша (SetOverridePostProcessAnimBP).
//   Вход — поза AnimBP GASP (Motion Matching + полнотелые монтажи в его DefaultSlot: стойка, нокдаун,
//   подъём, финал). Собственный слот DefaultSlot ЭТОГО экземпляра играет монтажи ВЕРХА тела (удары, блок,
//   уклоны, реакции, стойка рук на ходу) и накладывается от spine_01 вверх (поворот spine_01 — в
//   пространстве меша): «руки бьют, ноги ходят». Без визуальной подмены — ещё и FBoxerPoseFx.
//
// UBoxerVisualAnimInstance — основной AnimInstance ВИДИМОГО меша (MetaHuman Body вместо ABP_GenericRetarget):
//   нативный FAnimNode_RetargetPoseFromMesh (тот же IK Retargeter, источник — логический меш) + FBoxerPoseFx
//   (подшаг, наведение кулака с упором, реакция пружинами). Пост-процесс меша (корректоры MetaHuman) остаётся.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoxerFeel.h"
#include "BoxerAnimInstances.generated.h"

class ABoxerCharacter;
class UIKRetargeter;

// Альфы слоя верха тела (игровой поток → анимпоток).
struct FBoxerLayerParams
{
	float TorsoAlpha = 1.f; // корпус/шея/голова из монтажа верха
	float ArmsAlpha = 1.f;  // руки из монтажа верха
	bool bFx = false;       // процедурный слой здесь (нет визуальной подмены)
	bool bMirror = false;   // S-62: левша — поза зеркалится целиком (передняя рука и нога — правые)
	uint8 MirrorAxis = 1;   // ось зеркала в компонентном пространстве логического меша: 1 — X, 2 — Y
};

// ---------- Слой верха тела (пост-процесс логического меша) ----------

struct FBoxerLayerRootNode : public FAnimNode_Base
{
	FAnimNode_LinkedInputPose Input;
	FAnimNode_Slot Slot;
	FBoxerUpperMask Mask;
	FBoxerPoseFx Fx;
	FBoxerFeelFrame Frame;
	FBoxerLayerParams Params;
	FName BlendRoot = TEXT("spine_01");
	FBoxerFeelDebug Debug;
	// Повтор нокаута (S-54): записанная локальная поза меша (индексы костей меша) вместо своей.
	bool bReplay = false;
	TArray<FTransform> ReplayBones;
	// S-62: зеркало левши — пары костей (_l/_r) и компонентные повороты позы привязки, кэш по контейнеру костей.
	TArray<FCompactPoseBoneIndex> MirrorBones;
	TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> MirrorRefRots;
	uint16 MirrorSerial = MAX_uint16;
	const void* MirrorContainer = nullptr;
	void ResolveMirror(const FBoneContainer& Bones);

	FBoxerLayerRootNode();
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

struct FBoxerLayerProxy : public FAnimInstanceProxy
{
	FBoxerLayerProxy() = default;
	explicit FBoxerLayerProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &Root; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	FBoxerLayerRootNode Root;
};

UCLASS(Transient, NotBlueprintable)
class BOXINGUE_API UBoxerLayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Последний отладочный вывод анимпотока (кадр назад; процедурный слой здесь только без визуальной подмены).
	FBoxerFeelDebug LastDebug;
	FBoxerFeelDebug PeekDebug() { return GetProxyOnGameThread<FBoxerLayerProxy>().Root.Debug; }

	// Боец — источник кадра (ищется по владельцу меша, если не задан).
	TWeakObjectPtr<ABoxerCharacter> Boxer;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FBoxerLayerProxy(this); }
	friend struct FBoxerLayerProxy;
};

// ---------- Видимый меш: ретаргет + процедурный слой ----------

struct FBoxerVisualRootNode : public FAnimNode_Base
{
	FAnimNode_RetargetPoseFromMesh Retarget;
	FBoxerPoseFx Fx;
	FBoxerFeelFrame Frame;
	FBoxerFeelDebug Debug;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

struct FBoxerVisualProxy : public FAnimInstanceProxy
{
	FBoxerVisualProxy() = default;
	explicit FBoxerVisualProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &Root; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	FBoxerVisualRootNode Root;
};

UCLASS(Transient, NotBlueprintable)
class BOXINGUE_API UBoxerVisualAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Источник позы (логический меш) и ретаргетер (как в ABP_GenericRetarget). Зовётся сразу после создания.
	void SetupRetarget(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter, const FRetargetProfile* Profile);

	TWeakObjectPtr<ABoxerCharacter> Boxer;

	// Последний отладочный вывод анимпотока (кадр назад).
	FBoxerFeelDebug LastDebug;
	// Вывод последней ДОСЧИТАННОЙ оценки позы (ждёт параллельную задачу анимации, игровой поток).
	FBoxerFeelDebug PeekDebug() { return GetProxyOnGameThread<FBoxerVisualProxy>().Root.Debug; }

	UPROPERTY(Transient)
	TObjectPtr<UIKRetargeter> RetargeterRef;

	// false — оценка позы этого меша на игровом потоке (рычаг на будущее). Для физреакции одного его мало: при выключении
	// только здесь бой с -BoxPhysHits=1 зависал; используется глобальная cvar a.ParallelAnimEvaluation 0 (VERIFY_ON_PC.md, разд. 3).
	bool bAllowParallel = true;
	virtual bool CanRunParallelWork() const override { return bAllowParallel; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FBoxerVisualProxy(this); }
	friend struct FBoxerVisualProxy;
};
