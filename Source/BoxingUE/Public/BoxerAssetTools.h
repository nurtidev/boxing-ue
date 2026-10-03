// S-69 (tech-artist): редакторные инструменты для скриптов Tools/EditorScripts/anim_loco.py — урезанная локомоция
// боксёра (копии AnimBP/chooser'ов GASP без прыжков, спринта, паркура, приседа…).
//
// Python не умеет: (1) перепривязать ссылки ВНУТРИ одного ассета (EditorAssetLibrary.consolidate_assets меняет их во
// всех ассетах проекта и удаляет оригинал — оригиналы GASP трогать нельзя), (2) читать/писать скрытые свойства
// (UChooserTable::ResultsStructs и прочие UPROPERTY без EditAnywhere/BlueprintVisible). Здесь — ровно это, точечно,
// только в редакторе (в игре функции ничего не делают). Ни рантайм боя, ни ядро не трогаются.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoxerAssetTools.generated.h"

UCLASS()
class BOXINGUE_API UBoxerAssetTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Заменить ссылки From[i] → To[i] (To[i] = None — обнулить) во всех объектах пакета Asset (граф BP, узлы, шаблоны
	// компонентов, сгенерированный класс…). Пакет помечается изменённым; BP после этого надо скомпилировать.
	// Возвращает число заменённых ссылок.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static int32 ReplaceReferencesInAsset(UObject* Asset, const TArray<UObject*>& From, const TArray<UObject*>& To);

	// Кто внутри пакета Asset ссылается на Targets: строки «объект-внутри-пакета → цель (свойство)».
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static TArray<FString> FindReferencesInAsset(UObject* Asset, const TArray<UObject*>& Targets);

	// Все свойства объекта (и скрытые) текстом: «Имя=значение» построчно.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static FString ExportPropertiesText(UObject* Object);

	// Записать свойство объекта из текста (формат ExportTextItem). true — успех.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static bool ImportPropertyText(UObject* Object, const FString& PropertyName, const FString& Text);

	// Сменить класс компонента BP (узел SimpleConstructionScript с именем переменной Variable) на NewClass, не трогая
	// узлы графа (удаление компонента через SubobjectDataSubsystem рвёт связи узлов, читающих эту переменную).
	// Шаблон компонента пересоздаётся (значения по умолчанию — из NewClass). BP после этого надо скомпилировать.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static bool SwapComponentClass(UBlueprint* Blueprint, FName Variable, UClass* NewClass);

	// После компиляции: «осиротевшие» контакты узлов (контакт переименовался — например, вход контекста у узла
	// Evaluate Chooser назван по классу AnimBP, а контекст chooser'а теперь — копия AnimBP) переносят связи на
	// ЕДИНСТВЕННЫЙ свободный контакт того же узла с тем же направлением и категорией; сирота удаляется.
	// Возвращает строки «граф/узел: старый → новый (связей N)» и «НЕ НАЙДЕН …». BP после этого надо скомпилировать.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static TArray<FString> RelinkOrphanPins(UBlueprint* Blueprint);

	// Досжать клипы (у свежих копий AnimSequence сжатие асинхронное) и дождаться всей фоновой компиляции ассетов:
	// иначе сохранение копии базы Motion Matching строит индекс по несжатым клипам и падает на ассерте.
	// Возвращает число клипов, для которых сжатие запускалось.
	UFUNCTION(BlueprintCallable, Category = "Boxing|Editor")
	static int32 FinishAnimCompression(const TArray<UObject*>& Assets);
};
