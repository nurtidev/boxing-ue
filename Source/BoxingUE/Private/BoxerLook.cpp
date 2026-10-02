// Порт Tools/EditorScripts/look_apply.py::apply (S-60) — шаги и порядок те же. См. BoxerLook.h.
#include "BoxerLook.h"

#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* MI_HAIR = TEXT("/Game/MetaHumans/Kellan/Materials/MI_Hair.MI_Hair");

	struct FLookDb
	{
		bool bLoaded = false;
		TMap<FString, FBoxerLook> ById;
		TMap<FString, FString> IdByName;
		FName Socket = TEXT("FACIAL_C_FacialRoot");
		FTransform Attach = FTransform::Identity;
	};

	FLookDb& Db()
	{
		static FLookDb D;
		return D;
	}

	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		FString S;
		return (O.IsValid() && O->TryGetStringField(K, S)) ? S : FString();
	}

	float Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, float Def = 0.f)
	{
		double V = Def;
		return (O.IsValid() && O->TryGetNumberField(K, V)) ? static_cast<float>(V) : Def;
	}

	FLinearColor Rgb(const TArray<TSharedPtr<FJsonValue>>* A)
	{
		if (!A || A->Num() < 3)
		{
			return FLinearColor::White;
		}
		return FLinearColor(static_cast<float>((*A)[0]->AsNumber()), static_cast<float>((*A)[1]->AsNumber()), static_cast<float>((*A)[2]->AsNumber()), 1.f);
	}

	void ReadHair(const TSharedPtr<FJsonObject>& O, FBoxerHairLook& H)
	{
		if (!O.IsValid())
		{
			return;
		}
		H.Groom = Str(O, TEXT("groom"));
		H.Binding = Str(O, TEXT("binding"));
		H.Attach = Str(O, TEXT("attach"));
		H.Length = Num(O, TEXT("length"), 1.f);
		H.Melanin = Num(O, TEXT("melanin"), 0.5f);
		H.Redness = Num(O, TEXT("redness"));
		H.White = Num(O, TEXT("white"));
		const TArray<TSharedPtr<FJsonValue>>* Dye = nullptr;
		H.bDye = O->TryGetArrayField(TEXT("dye"), Dye) && Dye && Dye->Num() >= 3;
		if (H.bDye)
		{
			H.Dye = Rgb(Dye);
		}
		const TSharedPtr<FJsonObject>* Stub = nullptr;
		if (O->TryGetObjectField(TEXT("stubble"), Stub) && Stub)
		{
			H.StubbleGroom = Str(*Stub, TEXT("groom"));
			H.StubbleBinding = Str(*Stub, TEXT("binding"));
		}
	}

	void ReadMap(const TSharedPtr<FJsonObject>& O, const TCHAR* K, TMap<FString, FString>& Out)
	{
		const TSharedPtr<FJsonObject>* M = nullptr;
		if (O.IsValid() && O->TryGetObjectField(K, M) && M)
		{
			for (const auto& P : (*M)->Values)
			{
				Out.Add(P.Key, P.Value->AsString());
			}
		}
	}

	void LoadDb()
	{
		FLookDb& D = Db();
		if (D.bLoaded)
		{
			return;
		}
		D.bLoaded = true;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Boxing/Data/Appearance.json");
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("LOOK: нет или битый %s — облик бойцов по умолчанию"), *Path);
			return;
		}
		const TSharedPtr<FJsonObject>* GA = nullptr;
		if (Root->TryGetObjectField(TEXT("groomAttach"), GA) && GA)
		{
			D.Socket = FName(*Str(*GA, TEXT("socket")));
			const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Q = nullptr;
			if ((*GA)->TryGetArrayField(TEXT("loc"), L) && (*GA)->TryGetArrayField(TEXT("quat"), Q) && L->Num() == 3 && Q->Num() == 4)
			{
				D.Attach = FTransform(FQuat((*Q)[0]->AsNumber(), (*Q)[1]->AsNumber(), (*Q)[2]->AsNumber(), (*Q)[3]->AsNumber()),
					FVector((*L)[0]->AsNumber(), (*L)[1]->AsNumber(), (*L)[2]->AsNumber()));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Boxers = nullptr;
		if (!Root->TryGetArrayField(TEXT("boxers"), Boxers))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : *Boxers)
		{
			const TSharedPtr<FJsonObject> B = V->AsObject();
			const TSharedPtr<FJsonObject>* LookP = nullptr;
			if (!B.IsValid() || !B->TryGetObjectField(TEXT("look"), LookP) || !LookP)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& Lk = *LookP;
			FBoxerLook L;
			L.bValid = true;
			L.Id = Str(B, TEXT("id"));
			bool bF = false;
			L.bFemale = Lk->TryGetBoolField(TEXT("female"), bF) && bF;
			L.Scale = Num(Lk, TEXT("scale"), 1.f);
			const TSharedPtr<FJsonObject>* M = nullptr;
			if (Lk->TryGetObjectField(TEXT("morph"), M) && M)
			{
				L.Heavy = Num(*M, TEXT("Heavy"));
				L.Lean = Num(*M, TEXT("Lean"));
				L.Muscular = Num(*M, TEXT("Muscular"));
			}
			L.SkinTone = static_cast<int32>(Num(Lk, TEXT("skinTone"), 3.f));
			ReadMap(Lk, TEXT("skinFaceTex"), L.SkinFaceTex);
			ReadMap(Lk, TEXT("skinBodyTex"), L.SkinBodyTex);
			const TArray<TSharedPtr<FJsonValue>>* CC = nullptr;
			if (Lk->TryGetArrayField(TEXT("skinCC"), CC))
			{
				L.SkinCC = Rgb(CC);
			}
			const TSharedPtr<FJsonObject>* H = nullptr;
			if (Lk->TryGetObjectField(TEXT("hair"), H) && H) ReadHair(*H, L.Hair);
			if (Lk->TryGetObjectField(TEXT("brows"), H) && H) ReadHair(*H, L.Brows);
			if (Lk->TryGetObjectField(TEXT("facial"), H) && H) ReadHair(*H, L.Facial);
			D.IdByName.Add(Str(B, TEXT("name")), L.Id);
			D.ById.Add(L.Id, MoveTemp(L));
		}
		UE_LOG(LogTemp, Log, TEXT("LOOK: облик %d бойцов (%s)"), D.ById.Num(), *Path);
	}

	UMaterialInstanceDynamic* Mid(UMeshComponent* C, int32 Idx)
	{
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(Idx)))
		{
			return M;
		}
		return C->CreateDynamicMaterialInstance(Idx);
	}

	// Цвет волос MetaHuman во все слоты грума; Base — материал-основа для слотов, где ещё не MID.
	void SetHairParams(UMeshComponent* C, const FBoxerHairLook& P, UMaterialInterface* Base)
	{
		for (int32 I = 0; I < C->GetNumMaterials(); ++I)
		{
			if (Base && !Cast<UMaterialInstanceDynamic>(C->GetMaterial(I)))
			{
				C->SetMaterial(I, Base);
			}
			UMaterialInstanceDynamic* M = Mid(C, I);
			if (!M)
			{
				continue;
			}
			M->SetScalarParameterValue(TEXT("hairMelanin"), P.Melanin);
			M->SetScalarParameterValue(TEXT("hairRedness"), P.Redness);
			M->SetScalarParameterValue(TEXT("WhiteAmount"), P.White);
			if (P.bDye)
			{
				M->SetVectorParameterValue(TEXT("hairDye"), P.Dye);
			}
		}
	}

	// Грум на сокете лица (свои — Attach, Kellan — родное крепление; привязка к коже — Binding, если есть).
	bool SetGroom(UGroomComponent* C, const FString& GroomPath, const FString& BindingPath, const FString& Attach, USceneComponent* Face)
	{
		UGroomAsset* Asset = GroomPath.IsEmpty() ? nullptr : LoadObject<UGroomAsset>(nullptr, *GroomPath);
		if (!Asset)
		{
			C->SetGroomAsset(nullptr);
			C->SetVisibility(false);
			return false;
		}
		UGroomBindingAsset* Bind = BindingPath.IsEmpty() ? nullptr : LoadObject<UGroomBindingAsset>(nullptr, *BindingPath);
		if (Face)
		{
			const FName Socket = Attach.IsEmpty() ? Db().Socket : FName(*Attach);
			C->AttachToComponent(Face, FAttachmentTransformRules::KeepRelativeTransform, Socket);
			C->SetRelativeTransform(Db().Attach);
		}
		C->SetBindingAsset(Bind);
		C->SetGroomAsset(Asset);
		C->SetVisibility(true);
		return true;
	}
}

bool UBoxerLookLibrary::FindLook(const FString& Id, const FString& Name, FBoxerLook& Out)
{
	LoadDb();
	const FLookDb& D = Db();
	const FBoxerLook* L = Id.IsEmpty() ? nullptr : D.ById.Find(Id);
	if (!L && !Name.IsEmpty())
	{
		if (const FString* I = D.IdByName.Find(Name))
		{
			L = D.ById.Find(*I);
		}
	}
	if (!L)
	{
		return false;
	}
	Out = *L;
	return true;
}

void UBoxerLookLibrary::ApplyBoxerLook(AActor* Visual, UChildActorComponent* Child, const FBoxerLook& Look, bool bHeadgear)
{
	if (!Visual || !Look.bValid)
	{
		return;
	}
	TMap<FString, UActorComponent*> C;
	for (UActorComponent* Comp : Visual->GetComponents())
	{
		if (Comp && !C.Contains(Comp->GetName()))
		{
			C.Add(Comp->GetName(), Comp);
		}
	}
	USkeletalMeshComponent* Body = Cast<USkeletalMeshComponent>(C.FindRef(TEXT("Body")));
	USkeletalMeshComponent* Face = Cast<USkeletalMeshComponent>(C.FindRef(TEXT("Face")));
	// 1. рост: равномерный масштаб всего визуала (кости, IK и наведение кулака — в пространстве этого же меша)
	if (Child)
	{
		Child->SetRelativeScale3D(FVector(Look.Scale));
	}
	else
	{
		Visual->SetActorRelativeScale3D(FVector(Look.Scale));
	}
	// 2. телосложение: морфы на ведущем Body — ведомые (Legs/Feet/Torso, leader pose) берут их по имени
	const TPair<FName, float> Morphs[4] = {{TEXT("Heavy"), Look.Heavy}, {TEXT("Lean"), Look.Lean}, {TEXT("Muscular"), Look.Muscular},
		{TEXT("Female"), Look.bFemale ? 1.f : 0.f}};
	if (Body)
	{
		for (const auto& M : Morphs)
		{
			Body->SetMorphTarget(M.Key, M.Value, false);
		}
		for (const TCHAR* N : {TEXT("Legs"), TEXT("Feet"), TEXT("Torso")})
		{
			USkeletalMeshComponent* S = Cast<USkeletalMeshComponent>(C.FindRef(N));
			if (S && !S->LeaderPoseComponent.IsValid())
			{
				for (const auto& M : Morphs)
				{
					S->SetMorphTarget(M.Key, M.Value, false);
				}
			}
		}
	}
	// 2б. кроссовки GASP в слоте Feet (старые сборки обликов) — не рисовать
	if (USkeletalMeshComponent* Feet = Cast<USkeletalMeshComponent>(C.FindRef(TEXT("Feet"))))
	{
		if (Feet->GetSkeletalMeshAsset() && Feet->GetSkeletalMeshAsset()->GetName().Contains(TEXT("_shs_")))
		{
			Feet->SetVisibility(false);
		}
	}
	// 3. тон кожи: текстура тона в BaseColor MID по имени слота + индивидуальный BaseColor_ColorCorrect
	const TPair<USkeletalMeshComponent*, const TMap<FString, FString>*> Skins[2] = {{Face, &Look.SkinFaceTex}, {Body, &Look.SkinBodyTex}};
	for (const auto& S : Skins)
	{
		if (!S.Key)
		{
			continue;
		}
		for (const auto& Slot : *S.Value)
		{
			const int32 Idx = S.Key->GetMaterialIndex(FName(*Slot.Key));
			if (Idx < 0)
			{
				continue;
			}
			UTexture* Tex = LoadObject<UTexture>(nullptr, *Slot.Value);
			UMaterialInstanceDynamic* M = Mid(S.Key, Idx);
			if (!M)
			{
				continue;
			}
			if (Tex)
			{
				M->SetTextureParameterValue(TEXT("BaseColor"), Tex);
			}
			M->SetVectorParameterValue(TEXT("BaseColor_ColorCorrect"), Look.SkinCC);
		}
	}
	UMaterialInterface* HairBase = LoadObject<UMaterialInterface>(nullptr, MI_HAIR);
	// 4. волосы (под шлемом — нет)
	if (UGroomComponent* H = Cast<UGroomComponent>(C.FindRef(TEXT("Hair"))))
	{
		if (!bHeadgear && SetGroom(H, Look.Hair.Groom, Look.Hair.Binding, Look.Hair.Attach, Face))
		{
			H->SetHairLengthScaleEnable(true);
			H->SetHairLengthScale(Look.Hair.Length);
			SetHairParams(H, Look.Hair, HairBase);
		}
		else
		{
			SetGroom(H, FString(), FString(), FString(), nullptr);
		}
	}
	// 5. брови
	if (UGroomComponent* Br = Cast<UGroomComponent>(C.FindRef(TEXT("Eyebrows"))))
	{
		SetHairParams(Br, Look.Brows, nullptr);
	}
	// 6. борода (Beard) и щетина Kellan (Mustache)
	if (UGroomComponent* Bd = Cast<UGroomComponent>(C.FindRef(TEXT("Beard"))))
	{
		if (SetGroom(Bd, Look.Facial.Groom, Look.Facial.Binding, Look.Facial.Attach, Face))
		{
			SetHairParams(Bd, Look.Facial, HairBase);
		}
	}
	if (UGroomComponent* Mu = Cast<UGroomComponent>(C.FindRef(TEXT("Mustache"))))
	{
		if (SetGroom(Mu, Look.Facial.StubbleGroom, Look.Facial.StubbleBinding, FString(), Face))
		{
			SetHairParams(Mu, Look.Facial, nullptr);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("LOOK %s: масштаб %.3f, морфы H%.2f L%.2f M%.2f%s, тон %d, волосы %s/%.2f%s, борода %s"), *Look.Id, Look.Scale,
		Look.Heavy, Look.Lean, Look.Muscular, Look.bFemale ? TEXT(" жен.") : TEXT(""), Look.SkinTone,
		Look.Hair.Groom.IsEmpty() ? TEXT("-") : *FPaths::GetExtension(Look.Hair.Groom), Look.Hair.Length, bHeadgear ? TEXT(" (шлем)") : TEXT(""),
		Look.Facial.Groom.IsEmpty() ? TEXT("-") : *FPaths::GetExtension(Look.Facial.Groom));
}

bool UBoxerLookLibrary::ApplyBoxerLookById(AActor* Visual, UChildActorComponent* Child, const FString& Id, const FString& Name, bool bHeadgear)
{
	FBoxerLook L;
	if (!FindLook(Id, Name, L))
	{
		return false;
	}
	ApplyBoxerLook(Visual, Child, L, bHeadgear);
	return true;
}
