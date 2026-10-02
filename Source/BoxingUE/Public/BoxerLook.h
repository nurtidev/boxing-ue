// Облик бойца по данным ростера (S-60 tech-artist → рантайм, порт Tools/EditorScripts/look_apply.py::apply построчно).
//
// Данные — Content/Boxing/Data/Appearance.json (boxers[].look: готовые значения, C++ ничего не вычисляет, Docs/LOOK.md
// «S-60»): рост (масштаб child actor), телосложение (морфы Heavy/Lean/Muscular/Female на ведущем Body), тон кожи
// (MID по слотам лица/тела: BaseColor + BaseColor_ColorCorrect), грумы Hair/Beard/Mustache на сокете лица
// FACIAL_C_FacialRoot и цвет волос (MID MI_Hair: hairMelanin/hairRedness/WhiteAmount/hairDye), брови.
// Запись ищется по Id ростера («amateur:Имя»), без Id — по имени; нет записи — облик как был (Kellan).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoxerLook.generated.h"

class UChildActorComponent;

struct FBoxerHairLook
{
	FString Groom;   // пусто — без грума
	FString Binding;
	FString Attach;  // сокет своего грума (пусто — грум Kellan, родное крепление)
	float Length = 1.f;
	float Melanin = 0.5f;
	float Redness = 0.f;
	float White = 0.f;
	bool bDye = false;
	FLinearColor Dye = FLinearColor::White;
	// Только борода: щетина Kellan (компонент Mustache).
	FString StubbleGroom;
	FString StubbleBinding;
};

struct FBoxerLook
{
	bool bValid = false;
	FString Id;
	bool bFemale = false;
	float Scale = 1.f;
	float Heavy = 0.f;
	float Lean = 0.f;
	float Muscular = 0.f;
	int32 SkinTone = 3;
	TMap<FString, FString> SkinFaceTex; // слот → текстура
	TMap<FString, FString> SkinBodyTex;
	FLinearColor SkinCC = FLinearColor::White;
	FBoxerHairLook Hair;
	FBoxerHairLook Brows;
	FBoxerHairLook Facial;
};

UCLASS()
class BOXINGUE_API UBoxerLookLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Запись облика: по Id ростера, иначе по точному имени. false — нет записи (Appearance.json не загрузился / боец не в нём).
	static bool FindLook(const FString& Id, const FString& Name, FBoxerLook& Out);

	// Облик на визуальный актёр (копия BP_Kellan: Body/Face/Torso/Legs/Feet + грумы). Child — его ChildActorComponent
	// (масштаб роста ставится на него). bHeadgear — облик со шлемом: волосы не включать.
	static void ApplyBoxerLook(AActor* Visual, UChildActorComponent* Child, const FBoxerLook& Look, bool bHeadgear);

	// Для BP: найти и применить (true — запись найдена).
	UFUNCTION(BlueprintCallable, Category = "Boxing|Look")
	static bool ApplyBoxerLookById(AActor* Visual, UChildActorComponent* Child, const FString& Id, const FString& Name, bool bHeadgear);
};
