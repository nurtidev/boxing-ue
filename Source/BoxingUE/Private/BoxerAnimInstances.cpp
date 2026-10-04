#include "BoxerAnimInstances.h"

#include "BoxerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "AnimationRuntime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
	Dt = Context.GetDeltaTime();
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
		FQuat UpperRootCS = CSOf(Out, Root).GetRotation();
		// S-78 (блокер QA «корпус скручен до 180°»): GASP на развороте капсулы держит кость root с отставанием (offset root
		// bone) — ноги и таз ещё смотрят по-старому, а корпус из монтажа в пространстве меша уже по капсуле. Поворот корпуса —
		// относительно root ВХОДНОЙ позы (только курс): тело разворачивается целиком, без скрутки. -BoxUpperMeshSpace — как было.
		static const bool bMeshSpace = FParse::Param(FCommandLine::Get(), TEXT("BoxUpperMeshSpace"));
		if (!bMeshSpace)
		{
			const FCompactPoseBoneIndex R0(0);
			const FQuat D = CSOf(In, R0).GetRotation() * CSOf(Out, R0).GetRotation().Inverse();
			const FVector RootFwd = D.RotateVector(FVector::ForwardVector);
			const float Yaw = FMath::Atan2(RootFwd.Y, RootFwd.X);
			if (FMath::Abs(Yaw) > 1e-3f)
			{
				UpperRootCS = (FQuat(FVector::UpVector, Yaw) * UpperRootCS).GetNormalized();
			}
		}
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

	// S-62: левша — вся поза (локомоция, стойка, удары) зеркалится: передняя рука и нога правые. Процедурный слой
	// (наведение, реакция) — после зеркала: он знает, что передняя рука у левши правая.
	if (Params.bMirror)
	{
		ResolveMirror(Out.GetBoneContainer());
		if (MirrorBones.Num() == Out.GetNumBones())
		{
			FAnimationRuntime::MirrorPose(Out, Params.MirrorAxis == 2 ? EAxis::Y : EAxis::X, MirrorBones, MirrorRefRots);
		}
	}

	if (Params.bFx)
	{
		Debug = FBoxerFeelDebug();
		Fx.Apply(Out, Frame, Output.AnimInstanceProxy->GetComponentTransform(), &Debug);
		Feet.Apply(Out, Frame, Output.AnimInstanceProxy->GetComponentTransform(), Dt, &Debug); // S-70: ступни
		Fx.PublishBody(Out, Frame, Output.AnimInstanceProxy->GetComponentTransform(), &Debug); // S-78: тело — сопернику
		Dt = 0.f;
	}
}

void FBoxerLayerRootNode::ResolveMirror(const FBoneContainer& Bones)
{
	if (MirrorSerial == Bones.GetSerialNumber() && MirrorContainer == &Bones)
	{
		return;
	}
	MirrorSerial = Bones.GetSerialNumber();
	MirrorContainer = &Bones;
	const int32 Num = Bones.GetCompactPoseNumBones();
	MirrorBones.Reset(Num);
	MirrorRefRots.SetNum(Num);
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	for (int32 I = 0; I < Num; ++I)
	{
		const FCompactPoseBoneIndex C(I);
		const int32 Mesh = Bones.MakeMeshPoseIndex(C).GetInt();
		// Компонентный поворот позы привязки (родитель в компактной позе всегда раньше ребёнка).
		const FQuat Local = Bones.GetRefPoseTransform(C).GetRotation();
		const FCompactPoseBoneIndex P = Bones.GetParentBoneIndex(C);
		MirrorRefRots[C] = P.GetInt() != INDEX_NONE ? (MirrorRefRots[P] * Local).GetNormalized() : Local;
		// Пара по имени: …_l ↔ …_r; центральная кость зеркалится сама в себя.
		const FString Name = Ref.GetBoneName(Mesh).ToString();
		FString Pair;
		if (Name.EndsWith(TEXT("_l")))
		{
			Pair = Name.LeftChop(2) + TEXT("_r");
		}
		else if (Name.EndsWith(TEXT("_r")))
		{
			Pair = Name.LeftChop(2) + TEXT("_l");
		}
		int32 PairCompact = I;
		if (!Pair.IsEmpty())
		{
			const int32 PairMesh = Bones.GetPoseBoneIndexForBoneName(FName(*Pair));
			const int32 Pc = PairMesh != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PairMesh)).GetInt() : INDEX_NONE;
			PairCompact = Pc >= 0 ? Pc : I;
		}
		MirrorBones.Add(FCompactPoseBoneIndex(PairCompact));
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
	Dt = Context.GetDeltaTime();
}

void FBoxerVisualRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	Retarget.Evaluate_AnyThread(Output);
	Debug = FBoxerFeelDebug();
	Fx.Apply(Output.Pose, Frame, Output.AnimInstanceProxy->GetComponentTransform(), &Debug);
	Feet.Apply(Output.Pose, Frame, Output.AnimInstanceProxy->GetComponentTransform(), Dt, &Debug); // S-70: ступни
	Dt = 0.f; // повторная оценка без обновления (пауза) — стоп-кадр, не шаг
	SitFx.Apply(Output.Pose, SitFrame, Output.AnimInstanceProxy->GetComponentTransform()); // S-71: сидя на стуле в перерыве
	Fx.PublishBody(Output.Pose, Frame, Output.AnimInstanceProxy->GetComponentTransform(), &Debug); // S-78: тело — сопернику
	for (int32 I = 0; I < Face.Num; ++I) // S-74: мимика
	{
		Output.Curve.Set(Face.Names[I], Face.Values[I]);
	}
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
			Root.SitFrame = B->GetSitFrame(); // S-71
			Root.Face = B->GetFaceCurves();   // S-74
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
