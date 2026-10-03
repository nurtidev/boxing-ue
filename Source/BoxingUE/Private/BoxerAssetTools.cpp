// S-69: редакторные инструменты скриптов урезанной локомоции (см. BoxerAssetTools.h).
#include "BoxerAssetTools.h"

#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Serialization/FindReferencersArchive.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "AssetCompilingManager.h"
#include "Animation/AnimSequence.h"
#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#endif

namespace
{
	TArray<UObject*> PackageObjects(UObject* Asset)
	{
		TArray<UObject*> Objs;
		if (!Asset)
		{
			return Objs;
		}
		UPackage* Pkg = Asset->GetOutermost();
		GetObjectsWithOuter(Pkg, Objs, true);
		return Objs;
	}
}

int32 UBoxerAssetTools::ReplaceReferencesInAsset(UObject* Asset, const TArray<UObject*>& From, const TArray<UObject*>& To)
{
#if WITH_EDITOR
	if (!Asset || From.Num() != To.Num())
	{
		return -1;
	}
	TMap<UObject*, UObject*> Map;
	for (int32 i = 0; i < From.Num(); ++i)
	{
		if (From[i])
		{
			Map.Add(From[i], To[i]);
		}
	}
	int64 Count = 0;
	for (UObject* Obj : PackageObjects(Asset))
	{
		FArchiveReplaceObjectRef<UObject> Ar(Obj, Map, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
		Count += Ar.GetCount();
	}
	if (Count > 0)
	{
		Asset->GetOutermost()->MarkPackageDirty();
	}
	return static_cast<int32>(Count);
#else
	return 0;
#endif
}

TArray<FString> UBoxerAssetTools::FindReferencesInAsset(UObject* Asset, const TArray<UObject*>& Targets)
{
	TArray<FString> Out;
#if WITH_EDITOR
	TArray<UObject*> T;
	for (UObject* O : Targets)
	{
		if (O)
		{
			T.Add(O);
		}
	}
	if (!Asset || T.Num() == 0)
	{
		return Out;
	}
	for (UObject* Obj : PackageObjects(Asset))
	{
		FFindReferencersArchive Ar(Obj, T);
		TMap<UObject*, int32> Counts;
		TMultiMap<UObject*, FProperty*> Props;
		Ar.GetReferenceCounts(Counts, Props);
		for (const TPair<UObject*, int32>& C : Counts)
		{
			if (C.Value <= 0)
			{
				continue;
			}
			TArray<FProperty*> PL;
			Props.MultiFind(C.Key, PL);
			FString PS;
			for (const FProperty* P : PL)
			{
				PS += (PS.IsEmpty() ? TEXT("") : TEXT(",")) + (P ? P->GetName() : FString(TEXT("?")));
			}
			Out.Add(FString::Printf(TEXT("%s [%s] -> %s x%d (%s)"), *Obj->GetPathName(), *Obj->GetClass()->GetName(),
				*C.Key->GetPathName(), C.Value, *PS));
		}
	}
#endif
	return Out;
}

FString UBoxerAssetTools::ExportPropertiesText(UObject* Object)
{
	FString Out;
#if WITH_EDITOR
	if (!Object)
	{
		return Out;
	}
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FString V;
		It->ExportTextItem_InContainer(V, Object, nullptr, Object, PPF_None);
		Out += It->GetName() + TEXT("=") + V + TEXT("\n");
	}
#endif
	return Out;
}

bool UBoxerAssetTools::ImportPropertyText(UObject* Object, const FString& PropertyName, const FString& Text)
{
#if WITH_EDITOR
	if (!Object)
	{
		return false;
	}
	FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		return false;
	}
	Object->Modify();
	Object->PreEditChange(Prop);
	const TCHAR* End = Prop->ImportText_InContainer(*Text, Object, Object, PPF_None);
	FPropertyChangedEvent Ev(Prop, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(Ev);
	Object->GetOutermost()->MarkPackageDirty();
	return End != nullptr;
#else
	return false;
#endif
}

bool UBoxerAssetTools::SwapComponentClass(UBlueprint* Blueprint, FName Variable, UClass* NewClass)
{
#if WITH_EDITOR
	if (!Blueprint || !NewClass || !NewClass->IsChildOf(UActorComponent::StaticClass()) || !Blueprint->SimpleConstructionScript)
	{
		return false;
	}
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (!Node || Node->GetVariableName() != Variable)
		{
			continue;
		}
		Blueprint->Modify();
		Node->Modify();
		UActorComponent* Old = Node->ComponentTemplate;
		UObject* Outer = Old ? Old->GetOuter() : static_cast<UObject*>(Blueprint->GeneratedClass);
		const FName TemplateName = Old ? Old->GetFName() : FName(*(Variable.ToString() + USimpleConstructionScript::ComponentTemplateNameSuffix));
		if (Old)
		{
			// Старый шаблон — в сторону (транзиентный пакет), имя освобождается под новый.
			Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Old->ClearFlags(RF_Public | RF_Standalone | RF_ArchetypeObject);
			Old->MarkAsGarbage();
		}
		Node->ComponentClass = NewClass;
		Node->ComponentTemplate = NewObject<UActorComponent>(Outer, NewClass, TemplateName, RF_ArchetypeObject | RF_Transactional | RF_Public);
		Blueprint->GetOutermost()->MarkPackageDirty();
		return true;
	}
#endif
	return false;
}

TArray<FString> UBoxerAssetTools::RelinkOrphanPins(UBlueprint* Blueprint)
{
	TArray<FString> Out;
#if WITH_EDITOR
	if (!Blueprint)
	{
		return Out;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* G : Graphs)
	{
		if (!G)
		{
			continue;
		}
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N)
			{
				continue;
			}
			TArray<UEdGraphPin*> Orphans;
			for (UEdGraphPin* P : N->Pins)
			{
				if (P && P->bOrphanedPin)
				{
					Orphans.Add(P);
				}
			}
			for (UEdGraphPin* O : Orphans)
			{
				TArray<UEdGraphPin*> Cand;
				for (UEdGraphPin* P : N->Pins)
				{
					if (P && !P->bOrphanedPin && P->Direction == O->Direction && P->PinType.PinCategory == O->PinType.PinCategory
						&& P->LinkedTo.Num() == 0 && !P->bHidden)
					{
						Cand.Add(P);
					}
				}
				const FString Where = G->GetName() + TEXT("/") + N->GetName();
				if (Cand.Num() != 1)
				{
					Out.Add(FString::Printf(TEXT("НЕ НАЙДЕН %s: %s (кандидатов %d)"), *Where, *O->PinName.ToString(), Cand.Num()));
					continue;
				}
				UEdGraphPin* C = Cand[0];
				N->Modify();
				const TArray<UEdGraphPin*> Links = O->LinkedTo;
				for (UEdGraphPin* L : Links)
				{
					if (L)
					{
						C->MakeLinkTo(L);
					}
				}
				if (Links.Num() == 0)
				{
					C->DefaultValue = O->DefaultValue;
					C->DefaultObject = O->DefaultObject;
					C->DefaultTextValue = O->DefaultTextValue;
				}
				const FString OldName = O->PinName.ToString();
				O->BreakAllPinLinks();
				N->Pins.Remove(O);
				O->MarkAsGarbage();
				Out.Add(FString::Printf(TEXT("%s: %s → %s (связей %d)"), *Where, *OldName, *C->PinName.ToString(), Links.Num()));
			}
		}
	}
	if (Out.Num() > 0)
	{
		Blueprint->GetOutermost()->MarkPackageDirty();
	}
#endif
	return Out;
}

int32 UBoxerAssetTools::FinishAnimCompression(const TArray<UObject*>& Assets)
{
	int32 N = 0;
#if WITH_EDITOR
	for (UObject* O : Assets)
	{
		if (UAnimSequence* Seq = Cast<UAnimSequence>(O))
		{
			Seq->WaitOnExistingCompression(true);
			Seq->CacheDerivedDataForCurrentPlatform();
			++N;
		}
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
#endif
	return N;
}
