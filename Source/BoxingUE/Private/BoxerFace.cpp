// S-74: сторона бойца для мимики и повреждений лица (FaceFx.h): события ядра → FBoxerFaceFx, кривые RigLogic лица
// (CTRL_expressions_*) → анимпоток видимого меша, покраснение кожи лица (BaseColor_ColorCorrect). Только визуал.
#include "BoxerCharacter.h"
#include "BoxerAnimInstances.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimCurveMetadata.h"
#include "Animation/Skeleton.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "SceneInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace BoxerFaceImpl
{
	// -BoxFace=0 — без мимики (A/B), -BoxFaceLog — лог каналов и кривых лица раз в 0.5 с, -BoxDamage=0..1 — лицо побито с
	// начала боя (скриншоты, как ?damage= веба).
	bool FaceOn()
	{
		static const bool bOn = [] { int32 V = 1; FParse::Value(FCommandLine::Get(), TEXT("BoxFace="), V); return V != 0; }();
		return bOn;
	}
	bool FaceLog()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("BoxFaceLog"));
		return bOn;
	}

	USkeletalMeshComponent* FindFace(const UChildActorComponent* Child)
	{
		const AActor* Vis = Child ? Child->GetChildActor() : nullptr;
		if (!Vis)
		{
			return nullptr;
		}
		TArray<USkeletalMeshComponent*> Meshes;
		Vis->GetComponents(Meshes);
		for (USkeletalMeshComponent* M : Meshes)
		{
			if (M && M->GetName().Equals(TEXT("Face"), ESearchCase::IgnoreCase))
			{
				return M;
			}
		}
		return nullptr;
	}

	FBoxerFaceDamage::EPunch DamagePunch(EBoxPunchType P)
	{
		switch (P)
		{
		case EBoxPunchType::Cross: return FBoxerFaceDamage::EPunch::Cross;
		case EBoxPunchType::HookL:
		case EBoxPunchType::HookR: return FBoxerFaceDamage::EPunch::Hook;
		case EBoxPunchType::UpperL:
		case EBoxPunchType::UpperR: return FBoxerFaceDamage::EPunch::Uppercut;
		default: return FBoxerFaceDamage::EPunch::Jab;
		}
	}


	// Overlay-материал повреждений (Tools/EditorScripts/feel_face_damage.py): маски по позиции вершины до скиннинга.
	const TCHAR* FACE_DAMAGE_MAT = TEXT("/Game/Boxing/FX/M_FaceWounds.M_FaceWounds");

	// Центры пятен в пространстве меша лица (см, поза привязки): кость + сдвиг; нет кости — значение Kellan.
	struct FSpot
	{
		const TCHAR* Param;
		const TCHAR* Bone;
		FVector Offset;
		FVector Fallback;
	};
	const FSpot SPOTS[] = {
		{TEXT("BruiseL"), TEXT("FACIAL_L_EyesackLower"), FVector(0.f, 0.f, -0.5f), FVector(3.8f, 9.9f, 160.3f)},
		{TEXT("BruiseR"), TEXT("FACIAL_R_EyesackLower"), FVector(0.f, 0.f, -0.5f), FVector(-3.8f, 9.9f, 160.3f)},
		{TEXT("CheekL"), TEXT("FACIAL_L_CheekOuter"), FVector(0.f, 1.f, 0.f), FVector(4.2f, 9.4f, 159.6f)},
		{TEXT("CheekR"), TEXT("FACIAL_R_CheekOuter"), FVector(0.f, 1.f, 0.f), FVector(-4.1f, 9.3f, 159.5f)},
		{TEXT("CutL"), TEXT("FACIAL_L_EyesackUpper"), FVector(1.f, -0.2f, 1.7f), FVector(4.6f, 10.3f, 165.3f)},
		{TEXT("CutR"), TEXT("FACIAL_R_EyesackUpper"), FVector(-1.f, -0.2f, 1.7f), FVector(-4.6f, 10.3f, 165.3f)},
		{TEXT("Nose"), TEXT("FACIAL_C_NoseLower"), FVector(0.f, 0.f, 0.f), FVector(0.f, 12.1f, 157.1f)},
		{TEXT("Mouth"), TEXT("FACIAL_L_LipLowerOuter"), FVector(0.f, 0.3f, 0.f), FVector(1.9f, 11.4f, 154.1f)},
	};
	constexpr int32 NUM_SPOTS = UE_ARRAY_COUNT(SPOTS);
}
using namespace BoxerFaceImpl;

void ABoxerCharacter::FaceOnHit(EBoxPunchType Punch, EBoxPunchTarget Target, float Magnitude, bool bBlocked)
{
	FaceFx.OnHit(Magnitude, Target == EBoxPunchTarget::Body, bBlocked);
	FaceFx.Damage.ApplyHit(Magnitude, DamagePunch(Punch), BoxingBP::ArmOf(Punch) == EBoxPunchArm::Rear, Target == EBoxPunchTarget::Body, bBlocked);
}

void ABoxerCharacter::FaceOnKnockdown()
{
	FaceFx.OnKnockdown();
	FaceFx.Damage.ApplyKnockdown();
}

void ABoxerCharacter::UpdateFace(float DeltaSeconds)
{
	static const float PresetDamage = [] { float V = 0.f; FParse::Value(FCommandLine::Get(), TEXT("BoxDamage="), V); return V; }();
	if (PresetDamage > 0.f && FaceFx.Damage.Max() <= 0.f)
	{
		FBoxerFaceDamage& D = FaceFx.Damage;
		const float K = FMath::Clamp(PresetDamage, 0.f, 1.f);
		D.Head = K;
		D.EyeL = K * 0.8f;
		D.EyeR = K * 0.4f;
		D.CutL = K > 0.5f ? K * 0.7f : 0.f;
		D.Nose = K * 0.6f;
		D.Mouth = K * 0.5f;
		D.CheekL = K * 0.6f;
		D.CheekR = K * 0.5f;
	}
	if (bReplayDriven)
	{
		return; // повтор нокаута: кривые лица — как в последнем кадре боя
	}
	// Перерыв: сел в угол — катмен подчистил кровь и красноту (один раз за перерыв).
	const bool bSat = Sit.W > 0.5f;
	if (bSat && !bFaceSat)
	{
		FaceFx.Damage.BetweenRounds();
	}
	bFaceSat = bSat;

	FBoxerFaceInput In;
	In.Stamina = FMath::Clamp(StaminaPct / 100.f, 0.f, 1.f);
	In.bDown = bKnockedDown;
	In.bKO = bKO;
	In.bPunching = bPunching;
	In.PunchPhase = PunchPhase;
	In.ContactFrac = PunchContactFraction;
	In.bPowerPunch = bPunching && CurrentPunch != EBoxPunchType::Jab;
	In.bStunned = bStunned;
	In.bVictory = Victory >= 1.f;
	FaceFx.Update(DeltaSeconds, In);

	USkeletalMeshComponent* Face = FaceOn() ? FindFace(VisualChild) : nullptr;
	if (!Face)
	{
		FaceCurves.Num = 0;
		return;
	}
	if (!FaceMap.IsBuilt() && Face->GetSkeletalMeshAsset())
	{
		// Кривые, которые знает лицо (метаданные скелета и меша): RigLogic берёт CTRL_expressions_* из позы.
		TSet<FName> Known;
		TArray<FName> Names;
		if (const USkeleton* Sk = Face->GetSkeletalMeshAsset()->GetSkeleton())
		{
			Sk->GetCurveMetaDataNames(Names);
			Known.Append(Names);
		}
		if (const UAnimCurveMetaData* Md = Face->GetSkeletalMeshAsset()->GetAssetUserData<UAnimCurveMetaData>())
		{
			Names.Reset();
			Md->GetCurveMetaDataNames(Names);
			Known.Append(Names);
		}
		int32 Ctrl = 0;
		for (const FName& N : Known)
		{
			Ctrl += N.ToString().StartsWith(TEXT("CTRL_expressions_")) ? 1 : 0;
		}
		// Ни одной CTRL-кривой в метаданных — не фильтруем (RigLogic всё равно читает по имени).
		FaceMap.Build(Ctrl > 0 ? Known : TSet<FName>());
		FString Miss;
		for (const FName& N : FaceMap.Missing)
		{
			Miss += N.ToString() + TEXT(" ");
		}
		UE_LOG(LogTemp, Log, TEXT("FACE [%d]: кривых лица %d (CTRL_expressions_* %d), каналы → %d кривых; нет у лица: %s"), FighterIndex, Known.Num(), Ctrl,
			FaceMap.NumCurves(), Miss.IsEmpty() ? TEXT("-") : *Miss);
		if (FaceLog() && FighterIndex == 0)
		{
			// Разведка: кости лица у глаз/носа/губ/бровей/скул — позиции позы привязки (пространство меша, см).
			const FReferenceSkeleton& Ref = Face->GetSkeletalMeshAsset()->GetRefSkeleton();
			TArray<FTransform> CSPose;
			CSPose.SetNum(Ref.GetNum());
			for (int32 B = 0; B < Ref.GetNum(); ++B)
			{
				const int32 P = Ref.GetParentIndex(B);
				CSPose[B] = P >= 0 ? Ref.GetRefBonePose()[B] * CSPose[P] : Ref.GetRefBonePose()[B];
				const FString Nm = Ref.GetBoneName(B).ToString();
				if (Nm.Contains(TEXT("Eye")) && !Nm.Contains(TEXT("lid")) && !Nm.Contains(TEXT("Lash")) ||
					Nm.Contains(TEXT("Nose")) || Nm.Contains(TEXT("LipUpper")) || Nm.Contains(TEXT("LipLower")) || Nm.Contains(TEXT("Brow")) ||
					Nm.Contains(TEXT("Cheek")) || Nm.Contains(TEXT("Jaw")) || Nm == TEXT("head"))
				{
					const FVector L = CSPose[B].GetLocation();
					UE_LOG(LogTemp, Log, TEXT("FACEBONE %s (%.1f, %.1f, %.1f)"), *Nm, L.X, L.Y, L.Z);
				}
			}
		}
	}
	FaceMap.Fill(FaceFx.Frame(), FaceCurves);
	UpdateFaceMaterials();
	// -BoxFaceCheck: раз в 2 с — материалы лица целы (QA S-78 №6: серая голова после попаданий).
	static const bool bCheck = FParse::Param(FCommandLine::Get(), TEXT("BoxFaceCheck"));
	if (bCheck)
	{
		FaceCheckT -= DeltaSeconds;
		if (FaceCheckT <= 0.f)
		{
			FaceCheckT = 2.f;
			FString Why;
			const bool bOk = CheckFaceMaterials(Why);
			++FaceChecks;
			FaceCheckFails += bOk ? 0 : 1;
			UE_LOG(LogTemp, Log, TEXT("FACE ПРОВЕРКА [%d] %s: %s — провалов %d из %d"), FighterIndex, bOk ? TEXT("ок") : TEXT("ОШИБКА"), *Why, FaceCheckFails, FaceChecks);
		}
	}

	if (FaceLog())
	{
		FaceLogT -= DeltaSeconds;
		if (FaceLogT <= 0.f)
		{
			FaceLogT = 0.5f;
			const FBoxerFaceFrame& F = FaceFx.Frame();
			const UAnimInstance* Main = Face->GetAnimInstance();
			const UAnimInstance* Post = Face->GetPostProcessInstance();
			const FName Jaw(TEXT("CTRL_expressions_jawOpen"));
			const FName Blink(TEXT("CTRL_expressions_eyeBlinkL"));
			const UAnimInstance* BodyInst = VisualAnim.Get();
			UE_LOG(LogTemp, Log, TEXT("FACE [%d] t=%.2f челюсть %.2f сжал %.2f гримаса %.2f брови %.2f/%.2f веки %.2f/%.2f прищур %.2f/%.2f нос %.2f выдох %.2f | поплыл %.2f устал %.2f | jawOpen: тело %.2f лицо %.2f пост %.2f; blinkL: тело %.2f лицо %.2f пост %.2f | урон голова %.2f глаз L/R %.2f/%.2f рассеч. %.2f/%.2f нос %.2f губа %.2f"),
				FighterIndex, GetWorld()->GetTimeSeconds(), F[EBoxFaceCh::JawOpen], F[EBoxFaceCh::Clench], F[EBoxFaceCh::Stretch], F[EBoxFaceCh::BrowDown], F[EBoxFaceCh::BrowRaise],
				F[EBoxFaceCh::BlinkL], F[EBoxFaceCh::BlinkR], F[EBoxFaceCh::SquintL], F[EBoxFaceCh::SquintR], F[EBoxFaceCh::NoseWrinkle], F[EBoxFaceCh::Funnel],
				FaceFx.Daze(), FaceFx.Tired(),
				BodyInst ? BodyInst->GetCurveValue(Jaw) : -1.f, Main ? Main->GetCurveValue(Jaw) : -1.f, Post ? Post->GetCurveValue(Jaw) : -1.f,
				BodyInst ? BodyInst->GetCurveValue(Blink) : -1.f, Main ? Main->GetCurveValue(Blink) : -1.f, Post ? Post->GetCurveValue(Blink) : -1.f,
				FaceFx.Damage.Head, FaceFx.Damage.EyeL, FaceFx.Damage.EyeR, FaceFx.Damage.CutL, FaceFx.Damage.CutR, FaceFx.Damage.Nose, FaceFx.Damage.Mouth);
		}
	}
}

void ABoxerCharacter::UpdateFaceMaterials()
{
	USkeletalMeshComponent* Face = FindFace(VisualChild);
	if (!Face)
	{
		return;
	}
	// Пятна повреждений — overlay-материал лица (появляется с первым заметным повреждением, до того — без лишнего прохода).
	const FBoxerFaceDamage& D = FaceFx.Damage;
	const float Spot[NUM_SPOTS] = {D.EyeL, D.EyeR, D.CheekL, D.CheekR, D.CutL, D.CutR, D.Nose, D.Mouth};
	float SpotMax = 0.f;
	for (const float V : Spot) { SpotMax = FMath::Max(SpotMax, V); }
	if (!FaceDamageMid.IsValid() && SpotMax > 0.02f && !bFaceDamageTried)
	{
		bFaceDamageTried = true;
		// -BoxFaceDmgMat=<путь> — другой материал (итерации, пока ассет занят запущенным боем другой роли).
		static const FString MatPath = [] { FString V = FACE_DAMAGE_MAT; FParse::Value(FCommandLine::Get(), TEXT("BoxFaceDmgMat="), V); return V; }();
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, *MatPath);
		// Не скомпилировался (материал по умолчанию — серая голова поверх лица, QA S-78 №6) — без overlay.
		const UMaterial* BaseMat = Base ? Base->GetMaterial() : nullptr;
		if (BaseMat && GetWorld() && GetWorld()->Scene && const_cast<UMaterial*>(BaseMat)->IsCompilingOrHadCompileError(GetWorld()->Scene->GetShaderPlatform()) && !BaseMat->IsCompiling())
		{
			UE_LOG(LogTemp, Warning, TEXT("FACE [%d]: материал %s не скомпилирован — overlay повреждений выключен"), FighterIndex, *MatPath);
			Base = nullptr;
			FaceDamageBroken = true;
		}
		if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
			const FReferenceSkeleton& Ref = Face->GetSkeletalMeshAsset()->GetRefSkeleton();
			auto RefCS = [&Ref](int32 B)
			{
				FTransform T = Ref.GetRefBonePose()[B];
				for (int32 P = Ref.GetParentIndex(B); P != INDEX_NONE; P = Ref.GetParentIndex(P))
				{
					T = T * Ref.GetRefBonePose()[P];
				}
				return T;
			};
			// Центры — в осях кости головы (шейдер переводит в них позицию пикселя: HeadO/X/Y/Z каждый кадр).
			FaceHeadBone = Ref.FindBoneIndex(FName(TEXT("head")));
			const FTransform HeadRef = FaceHeadBone != INDEX_NONE ? RefCS(FaceHeadBone) : FTransform::Identity;
			FaceHeadRefQ = HeadRef.GetRotation();
			for (int32 I = 0; I < NUM_SPOTS; ++I)
			{
				FVector C = SPOTS[I].Fallback;
				const int32 B = Ref.FindBoneIndex(FName(SPOTS[I].Bone));
				if (B != INDEX_NONE)
				{
					C = RefCS(B).GetLocation() + SPOTS[I].Offset;
				}
				FaceSpotPos[I] = C - HeadRef.GetLocation(); // оси меша лица (шейдер: x — вбок, y — вперёд, z — вверх), от кости головы
			}
			FaceDamageMid = Mid;
			Face->SetOverlayMaterial(Mid);
			UE_LOG(LogTemp, Log, TEXT("FACE [%d]: overlay повреждений включён"), FighterIndex);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FACE [%d]: нет материала %s (Tools/EditorScripts/feel_face_damage.py)"), FighterIndex, *MatPath);
		}
	}
	if (UMaterialInstanceDynamic* Mid = FaceDamageMid.Get())
	{
		// Оси кости головы в мире (каждый кадр): позиция пикселя → оси головы = dot(P − O, ось / масштаб).
		const FTransform H = FaceHeadBone != INDEX_NONE ? Face->GetBoneTransform(FaceHeadBone) : Face->GetComponentTransform();
		const float Sc = FMath::Max(0.01f, static_cast<float>(H.GetScale3D().X));
		// Оси меша лица, «приклеенные» к кости головы: поворот головы от позы привязки (мир) × оси меша.
		const FQuat Q = H.GetRotation() * FaceHeadRefQ.Inverse();
		const FVector O = H.GetLocation();
		const FVector Ax[3] = {Q.GetAxisX() / Sc, Q.GetAxisY() / Sc, Q.GetAxisZ() / Sc};
		Mid->SetVectorParameterValue(TEXT("HeadO"), FLinearColor(O.X, O.Y, O.Z, 0.f));
		Mid->SetVectorParameterValue(TEXT("HeadX"), FLinearColor(Ax[0].X, Ax[0].Y, Ax[0].Z, 0.f));
		Mid->SetVectorParameterValue(TEXT("HeadY"), FLinearColor(Ax[1].X, Ax[1].Y, Ax[1].Z, 0.f));
		Mid->SetVectorParameterValue(TEXT("HeadZ"), FLinearColor(Ax[2].X, Ax[2].Y, Ax[2].Z, 0.f));
		if (FaceLog() && FaceLogT <= 0.f)
		{
			// Сверка: центр пятна по осям головы → мир против живой кости лица (без сдвига SPOTS) — см.
			const FVector Wb = Face->GetBoneLocation(FName(SPOTS[6].Bone));
			const FVector Wm = O + Q.RotateVector(FaceSpotPos[6]) * Sc;
			UE_LOG(LogTemp, Log, TEXT("FACEDMG [%d] нос: оси головы → мир (%.1f, %.1f, %.1f), кость (%.1f, %.1f, %.1f), голова (%.1f, %.1f, %.1f) масштаб %.2f, кость головы %d"),
				FighterIndex, Wm.X, Wm.Y, Wm.Z, Wb.X, Wb.Y, Wb.Z, O.X, O.Y, O.Z, Sc, FaceHeadBone);
		}
		static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("BoxFaceDmgProbe"));
		if (bProbe)
		{
			// Отладка: оси — мир, все пятна — в мировых точках костей (проверка, что overlay рисуется и позиция пикселя — мировая).
			Mid->SetVectorParameterValue(TEXT("HeadO"), FLinearColor(0.f, 0.f, 0.f, 0.f));
			Mid->SetVectorParameterValue(TEXT("HeadX"), FLinearColor(1.f, 0.f, 0.f, 0.f));
			Mid->SetVectorParameterValue(TEXT("HeadY"), FLinearColor(0.f, 1.f, 0.f, 0.f));
			Mid->SetVectorParameterValue(TEXT("HeadZ"), FLinearColor(0.f, 0.f, 1.f, 0.f));
			for (int32 I = 0; I < NUM_SPOTS; ++I)
			{
				const FVector W = Face->GetBoneLocation(FName(SPOTS[I].Bone));
				Mid->SetVectorParameterValue(SPOTS[I].Param, FLinearColor(W.X, W.Y, W.Z, 1.f));
			}
			return;
		}
		float Sum = 0.f;
		for (const float V : Spot) { Sum += V; }
		if (FMath::Abs(Sum - FaceSpotSum) > 0.005f)
		{
			FaceSpotSum = Sum;
			for (int32 I = 0; I < NUM_SPOTS; ++I)
			{
				const FVector& C = FaceSpotPos[I];
				Mid->SetVectorParameterValue(SPOTS[I].Param, FLinearColor(C.X, C.Y, C.Z, Spot[I]));
			}
		}
	}
	// Покраснение кожи (BaseColor_ColorCorrect) убрано: материал головы и тела разные — давало шов на шее (QA S-78 №6).
}

bool ABoxerCharacter::CheckFaceMaterials(FString& OutWhy) const
{
	// S-74 (автопроверка QA S-78 №6): лицо не «посерело» — слоты кожи головы не стали материалом по умолчанию, у них есть
	// текстура BaseColor, а overlay (если включён) — наш скомпилированный материал.
	const USkeletalMeshComponent* Face = FindFace(VisualChild);
	if (!Face)
	{
		OutWhy = TEXT("нет лица");
		return true;
	}
	const UMaterial* Def = UMaterial::GetDefaultMaterial(MD_Surface);
	const TArray<FName> Slots = Face->GetMaterialSlotNames();
	for (int32 I = 0; I < Face->GetNumMaterials(); ++I)
	{
		if (!Slots.IsValidIndex(I) || !Slots[I].ToString().StartsWith(TEXT("head_LOD")))
		{
			continue;
		}
		const UMaterialInterface* M = Face->GetMaterial(I);
		if (!M || M->GetMaterial() == Def)
		{
			OutWhy = FString::Printf(TEXT("слот %s — материал по умолчанию"), *Slots[I].ToString());
			return false;
		}
		UTexture* T = nullptr;
		if (!M->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), T) || !T)
		{
			OutWhy = FString::Printf(TEXT("слот %s — нет текстуры BaseColor"), *Slots[I].ToString());
			return false;
		}
	}
	if (const UMaterialInterface* Ov = Face->GetOverlayMaterial())
	{
		const UMaterial* M = Ov->GetMaterial();
		if (!M || M == Def || (GetWorld() && GetWorld()->Scene && const_cast<UMaterial*>(M)->IsCompilingOrHadCompileError(GetWorld()->Scene->GetShaderPlatform()) && !M->IsCompiling()))
		{
			OutWhy = TEXT("overlay — материал по умолчанию / не скомпилирован");
			return false;
		}
	}
	OutWhy = FString::Printf(TEXT("ок (overlay %s, повреждения %.2f)"), Face->GetOverlayMaterial() ? TEXT("есть") : TEXT("нет"), FaceFx.Damage.Max());
	return true;
}
