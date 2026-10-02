#include "BoxerAnimInstances.h"

#include "BoxerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"

// Доступ к FAnimInstanceProxy::DefaultLinkedInstanceInputNode (private, сеттера нет). Меш кладёт входную позу
// пост-процесса только в этот узел (USkeletalMeshComponent::EvaluatePostProcessMeshInstance), а у нативного
// экземпляра без класса AnimBP его заполнить некому. Явная инстанциация шаблона игнорирует проверку доступа
// ([temp.explicit]/12) — стандартный приём; трогаем ровно одно поле.
namespace BoxerProxyAccess
{
	struct FInputNodeTag
	{
		using Type = FAnimNode_LinkedInputPose* FAnimInstanceProxy::*;
		friend Type Get(FInputNodeTag);
	};

	template <typename Tag, typename Tag::Type Member>
	struct TRob
	{
		friend typename Tag::Type Get(Tag) { return Member; }
	};

	template struct TRob<FInputNodeTag, &FAnimInstanceProxy::DefaultLinkedInstanceInputNode>;
}

namespace
{
	FTransform CSOf(const FCompactPose& P, FCompactPoseBoneIndex I)
	{
		FTransform T = P[I];
		FCompactPoseBoneIndex Par = P.GetParentBoneIndex(I);
		while (Par.GetInt() != INDEX_NONE)
		{
			T = T * P[Par];
			Par = P.GetParentBoneIndex(Par);
		}
		return T;
	}

	ABoxerCharacter* FindBoxer(TWeakObjectPtr<ABoxerCharacter>& Cached, const UAnimInstance* Inst)
	{
		if (Cached.IsValid())
		{
			return Cached.Get();
		}
		AActor* A = Inst ? Inst->GetOwningActor() : nullptr;
		// Видимый меш живёт в child actor — боец выше по цепочке владельцев/родителей.
		for (int32 Guard = 0; A && Guard < 4; ++Guard)
		{
			if (ABoxerCharacter* B = Cast<ABoxerCharacter>(A))
			{
				Cached = B;
				return B;
			}
			A = A->GetParentActor() ? A->GetParentActor() : A->GetOwner();
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------------------------
// Слой верха тела
// ---------------------------------------------------------------------------------------------

FBoxerLayerRootNode::FBoxerLayerRootNode()
{
	Slot.SlotName = FName(TEXT("DefaultSlot"));
	Slot.Source.SetLinkNode(&Input);
}

void FBoxerLayerRootNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Slot.Initialize_AnyThread(Context);
}

void FBoxerLayerRootNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Slot.CacheBones_AnyThread(Context);
}

void FBoxerLayerRootNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Slot.Update_AnyThread(Context);
}

void FBoxerLayerRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	if (bReplay && ReplayBones.Num() > 0)
	{
		// Повтор нокаута: поза из записи (уже с процедурным слоем, если он был здесь) — ни входа, ни Fx.
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		for (const FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
		{
			const int32 M = BC.MakeMeshPoseIndex(I).GetInt();
			Output.Pose[I] = ReplayBones.IsValidIndex(M) ? ReplayBones[M] : Output.Pose.GetRefPose(I);
		}
		return;
	}
	// База — поза AnimBP GASP (вход пост-процесса), верх — она же с монтажом слота этого экземпляра.
	FPoseContext Base(Output);
	Input.Evaluate_AnyThread(Base);
	Slot.Evaluate_AnyThread(Output);

	FCompactPose& Out = Output.Pose;
	const FCompactPose& In = Base.Pose;
	Mask.Resolve(Out.GetBoneContainer(), BlendRoot);
	if (Mask.IsValid() && Mask.Kind.Num() == Out.GetNumBones())
	{
		// Корень слоя (spine_01) — поворот в пространстве меша: корпус держит ориентацию клипа поверх таза локомоции.
		const FCompactPoseBoneIndex Root = Mask.Root;
		const FCompactPoseBoneIndex Par = Out.GetParentBoneIndex(Root);
		const FQuat UpperRootCS = CSOf(Out, Root).GetRotation();
		const FQuat BaseParentCS = Par.GetInt() != INDEX_NONE ? CSOf(In, Par).GetRotation() : FQuat::Identity;
		FTransform RootMS = Out[Root];
		RootMS.SetRotation((BaseParentCS.Inverse() * UpperRootCS).GetNormalized());

		for (const FCompactPoseBoneIndex I : Out.ForEachBoneIndex())
		{
			const uint8 K = Mask.Kind[I.GetInt()];
			if (K == 0)
			{
				Out[I] = In[I];
				continue;
			}
			const FTransform Upper = I == Root ? RootMS : Out[I];
			const float A = K == 2 ? Params.ArmsAlpha : Params.TorsoAlpha;
			if (A >= 0.999f)
			{
				Out[I] = Upper;
			}
			else
			{
				FTransform T;
				T.Blend(In[I], Upper, FMath::Clamp(A, 0.f, 1.f));
				Out[I] = T;
			}
		}
	}

	if (Params.bFx)
	{
		Debug = FBoxerFeelDebug();
		Fx.Apply(Out, Frame, Output.AnimInstanceProxy->GetComponentTransform(), &Debug);
	}
}

void FBoxerLayerProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	// Нативный экземпляр без класса AnimBP: сами говорим мешу, куда класть входную позу пост-процесса.
	this->*Get(BoxerProxyAccess::FInputNodeTag()) = &Root.Input;
}

void FBoxerLayerProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&Root);
	OutNodes.Add(&Root.Input);
	OutNodes.Add(&Root.Slot);
}

void FBoxerLayerProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (UBoxerLayerAnimInstance* Inst = Cast<UBoxerLayerAnimInstance>(InAnimInstance))
	{
		Inst->LastDebug = Root.Debug;
		if (ABoxerCharacter* B = FindBoxer(Inst->Boxer, Inst))
		{
			Root.Frame = B->GetFeelFrame();
			Root.Params = B->GetLayerParams();
			Root.bReplay = B->IsReplayDriven();
			if (Root.bReplay)
			{
				Root.ReplayBones = B->GetReplayBones();
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Видимый меш
// ---------------------------------------------------------------------------------------------

void FBoxerVisualRootNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Retarget.Initialize_AnyThread(Context);
}

void FBoxerVisualRootNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Retarget.CacheBones_AnyThread(Context);
}

void FBoxerVisualRootNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Retarget.Update_AnyThread(Context);
}

void FBoxerVisualRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	Retarget.Evaluate_AnyThread(Output);
	Debug = FBoxerFeelDebug();
	Fx.Apply(Output.Pose, Frame, Output.AnimInstanceProxy->GetComponentTransform(), &Debug);
}

void FBoxerVisualProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&Root);
	OutNodes.Add(&Root.Retarget); // PreUpdate: копия позы источника на игровом потоке
}

void FBoxerVisualProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (UBoxerVisualAnimInstance* Inst = Cast<UBoxerVisualAnimInstance>(InAnimInstance))
	{
		Inst->LastDebug = Root.Debug; // прошлый кадр анимпотока уже досчитан
		if (ABoxerCharacter* B = FindBoxer(Inst->Boxer, Inst))
		{
			Root.Frame = B->GetFeelFrame();
		}
	}
}

void UBoxerVisualAnimInstance::SetupRetarget(USkeletalMeshComponent* Source, UIKRetargeter* Retargeter, const FRetargetProfile* Profile)
{
	RetargeterRef = Retargeter;
	FBoxerVisualProxy& Proxy = GetProxyOnGameThread<FBoxerVisualProxy>();
	FAnimNode_RetargetPoseFromMesh& N = Proxy.Root.Retarget;
	N.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	N.SourceMeshComponent = Source;
	N.IKRetargeterAsset = Retargeter;
	if (Profile)
	{
		N.CustomRetargetProfile = *Profile;
	}
	N.bSuppressWarnings = true;
}
