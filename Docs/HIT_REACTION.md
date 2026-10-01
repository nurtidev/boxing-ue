# Физреакция на попадание (S-41, трек C → реализация в C++ у трека B)

Цель — как в web (`impact.ts`: голова кивает/поворачивается от удара, корпус сгибается, колени подседают),
но физикой: анимация задаёт позу, тело к ней «привязано» пружинами, импульс удара выбивает кость, пружина
возвращает. Клипы реакций (`AM_HitHead`, `AM_HitBody`, слот UpperBody) — лишь подложка, как в web (0.35).

## Какой меш симулировать

В GASP логика — невидимый UEFN-меш, видимый персонаж (Manny / MetaHuman Kellan) копирует позу
(`ABP_GenericRetarget`). Физику включать на **видимом** меше: у него свой Physics Asset
(Manny — `PA_Mannequin`, MetaHuman — `m_med_nrw_body_Physics`; у UEFN — `PA_UEFN_Mannequin`).
Копирование позы (RetargetPoseFromMesh) идёт в AnimBP видимого меша, физическая смесь применяется после него.

## Подход 1 (рекомендую для прототипа): UPhysicalAnimationComponent

Ассет: `/Game/Boxing/Anim/DT_HitReaction_PhysAnim` — DataTable со строками `FPhysicalAnimationData`
(строка = кость). Применять **по порядку строк** через `ApplyPhysicalAnimationSettingsBelow(Row, Data, true)`:
каждая следующая строка переопределяет своё поддерево.

| Строка (кость и ниже) | OrientationStrength | AngularVelocityStrength | Смысл |
|---|---|---|---|
| pelvis | 6000 | 600 | каркас; ноги физикой не симулировать |
| spine_01 | 2500 | 250 | низ корпуса |
| spine_03 | 1500 | 150 | грудь — заметно качается от удара в корпус |
| clavicle_l / clavicle_r | 2000 | 200 | плечи и руки жёстче корпуса — перчатки остаются у лица |
| hand_l / hand_r | 3000 | 300 | кисти держат кулак |
| neck_01 | 600 | 60 | шея — мягко |
| head | 350 | 35 | голова — мягче всего, «отлетает» |

Все `bIsLocalSimulation = true`, Position/Velocity Strength = 0 (только ориентационные пружины в
локальном пространстве — тело не «тянет» в мир, капсула и Motion Matching не страдают), MaxForce/Torque = 0
(без лимита). Демпфирование ≈ 0.1 × жёсткость — почти критическое затухание, 1–2 качка без дрожи.

Схема в C++ (эскиз, писать будет трек B):
```cpp
// BeginPlay
PhysAnim->SetSkeletalMeshComponent(VisibleMesh);
for (const FName& Row : Table->GetRowNames())
    PhysAnim->ApplyPhysicalAnimationSettingsBelow(Row, *Table->FindRow<FPhysicalAnimationData>(Row, TEXT("")), true);
VisibleMesh->SetAllBodiesBelowSimulatePhysics(TEXT("spine_01"), true, true); // только верх тела
VisibleMesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("spine_01"), 0.f);       // в покое — чистая анимация

// OnHit(Bone, DirWorld, Mag)    — событие ядра в кадре контакта удара соперника
BlendW = FMath::Clamp(0.35f + 0.25f * Mag, 0.f, 0.85f);   // сколько физики подмешать
VisibleMesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("spine_01"), BlendW);
VisibleMesh->AddImpulse(DirWorld * Impulse(Type) * Mag, Bone, /*bVelChange*/ true);
// Tick: BlendW → 0 за ~0.35 с (exp-спад), затем SetAllBodiesBelowPhysicsBlendWeight(spine_01, 0)
```

### Импульс по типу удара (bVelChange = true → см/с, не зависит от массы тел)

| Удар (ядро) | Кость приложения | Направление (от бьющего к цели, мир) | Скорость, см/с при mag 1 |
|---|---|---|---|
| джеб | head | вперёд по линии удара | 180 |
| кросс | head | вперёд + 15° вбок от бьющей руки | 320 |
| хук (голова) | head | **вбок** (поперёк линии, от бьющей руки) 70% + вперёд 30% | 380 |
| апперкот | head | **вверх** 70% + вперёд 30% | 340 |
| в корпус (любой) | spine_03 | вперёд, для хука — вбок 50% | 220 |
| по блоку | lowerarm_l / lowerarm_r | вперёд | 120 (блок «съедает»: Mag × 0.4) |

`mag` — сила попадания из ядра (как `ev.mag` в web; медиана ≈ 0.7, тяжёлые ≥ 1.1).
Тяжёлый (`mag ≥ 1.1`): BlendW до 0.85 и импульс ещё в `spine_01` (× 0.3) — «подсел».

### Нокдаун / нокаут
- Нокдаун: `AM_Knockdown` (DefaultSlot) + на 0.2 с BlendW = 1.0 ниже `spine_01` и импульс как у удара ×1.5,
  затем спад к 0 к моменту касания пола (нотифай `Peak` монтажа ≈ 1.8 с — голова в нижней точке).
- Нокаут: полный рэгдолл на 1–2 с: `SetSimulatePhysics(true)` от `pelvis`, пружины ×0.15 (тело «ватное»,
  но не макаронина), капсулу отключить от меша (GASP: `DetachFromController`/выключить коллизию капсулы),
  потом `AM_Knockout` или оставить лежать.

## Подход 2: Physics Control (плагин PhysicsControl включён)

Гибче (пружины и в мировом, и в родительском пространстве, наборы, профили), но `UPhysicsControlAsset`
из Python не заполнить (профили — protected), поэтому создавать контролы в рантайме:
```cpp
// UPhysicsControlComponent* PC на акторе
TMap<FName, FPhysicsControlLimbSetupData> ... // лимбы: "Spine"(spine_01, +parent), "Head"(neck_01),
                                              // "ArmL"(clavicle_l), "ArmR"(clavicle_r), "Legs" не создаём
PC->CreateControlsAndBodyModifiersFromLimbBones(..., VisibleMesh, LimbSetupDatas, WorldSpaceData, ParentSpaceData, BodyModifierData);
```
Значения parent-space контролов — те же, что в таблице (AngularStrength ≈ OrientationStrength / 1000 в
единицах Physics Control: Spine 2.5, Chest 1.5, Arms 2.0–3.0, Neck 0.6, Head 0.35; AngularDampingRatio 1.0),
world-space контролы выключены (strength 0) — иначе тело тянет к мировой позе и спорит с MM.
Body modifiers: `MovementType = Simulated` для верха, `Kinematic` для таза/ног, `PhysicsBlendWeight`
— та же огибающая, что BlendW выше. Профили на будущее: `Guard` (всё жёстко), `Hit`, `Stunned` (×0.5),
`KO` (×0.15).

## Проверка
`stat unit` / `pxvis collision` в PIE; на ударе голова должна качнуться и вернуться за ≤ 0.4 с, перчатки —
остаться у лица; нокаут — без «вытягивания» конечностей (если тянет — уменьшить Mag, а не ослаблять пружины рук).
