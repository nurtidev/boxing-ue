# Бой в UE: геймплейный слой над ядром (S-41, трек B)

Ядро боя — `FBoxingFightCore` ([FIGHT_CORE_PORT.md](FIGHT_CORE_PORT.md)), его механика не тронута.
Этот слой: режиссёр (GameMode), визуальные бойцы на AnimBP Game Animation Sample, ввод Enhanced Input,
камера боя как в вебе, минимальный HUD.

## Классы (`Source/BoxingUE`)

| Класс | Файлы | Что делает |
|---|---|---|
| `ABoxingFightGameMode` | `BoxingFightGameMode.h/.cpp` | владеет ядром, тикает его **фиксированным шагом** 1/60 с (аккумулятор), применяет ввод игрока на границе шага, спаунит двух бойцов, раздаёт снимок и события |
| `ABoxerCharacter` | `BoxerCharacter.h/.cpp` | визуальный боец: ход через CharacterMovement (для Motion Matching), состояние для AnimBP, монтажи с синхронизацией контакта, события |
| `ABoxingFightPlayerController` | `BoxingFightPlayerController.h/.cpp` | Enhanced Input → действия ядра; камера боя |
| `ABoxingFightHUD` | `BoxingFightHUD.h/.cpp` | Canvas: здоровье/стамина, раунд/время, нокдаун, перерыв, итог |
| `EBox*` / `FBoxerPreset` | `BoxingFightBPTypes.h` | UENUM/USTRUCT-зеркала enum'ов ядра для Blueprint (ядро — plain C++ без UHT) |

Ядро получило только **читающие** поля снимка (поведение и паритет не изменились, `Tools/CoreHarness` — OK):
`FFighterState::PunchDuration`, `PunchTimeToContact`, `SlipPhase`, `bStaggered`.

## Подключение к уровню

* **World Settings → GameMode Override = `BoxingFightGameMode`** (так сделан `/Game/Boxing/Maps/L_FightTest`),
  или в URL карты: `L_Ring?game=/Script/BoxingUE.BoxingFightGameMode`.
* Центр ринга: актор с тегом **`RingCenter`** (его позиция = центр и высота настила). Нет такого — берётся
  `RingCenter` из свойств GameMode, высота — трассой вниз от точки на 3 м выше.
* Ринг считается выровненным по осям мира (ядро: X → X_ue, Z → Y_ue, метры → см).
* Пешки у игрока нет (`DefaultPawnClass = None`, `RestartPlayer` пустой): бойцы — ИИ-пешки (`AAIController`,
  нужен CMC), ввод контроллер шлёт в ядро.

Свойства GameMode (`Boxing|Fight`): `RedPreset`/`BluePreset` (`FBoxerPreset`: имя, 7 статов, рост/размах/вес, стиль,
seasoning), `Seed`, `Rounds`, `RoundSeconds` (55 — калибровка веба), `BreakSeconds`, `bAllowDraw`, `bAutopilot`,
`FixedStep`; `BoxerClass` / `BoxerClassPath` (по умолчанию `/Game/Boxing/Blueprints/BP_Boxer`).

Командная строка: `-BoxAutopilot`, `-BoxSeed=N`, `-BoxRoundSec=S`, `-BoxBreakSec=S`, `-BoxQuitAfter=S`,
`-BoxLogEvery=S` (позиции/состояние в LogTemp), `-BoxLogEvents`, `-BoxShots=5,9,14` (скриншоты в `Docs/screens/fight_NN.png`).

## Боец: BP_Boxer и AnimBP GASP

AnimBP GASP (`SandboxCharacter_CMC_ABP`) читает пешку через BP-интерфейс `BPI_SandboxCharacter_Pawn`
(`Get_PropertiesForAnimation`: скорость, ускорение, походка, режим вращения…). Из C++ BP-интерфейс не реализовать,
поэтому **`/Game/Boxing/Blueprints/BP_Boxer` = копия `SandboxCharacter_CMC`, перепривязанная (reparent) к
`ABoxerCharacter`** — скрипт `Tools/EditorScripts/fight_blueprints.py`. Меш (UEFN-манекен), AnimBP, компоненты GASP и
реализация интерфейса сохраняются; C++ задаёт место, курс и монтажи.

* **Ход.** Каждый кадр (в тике GameMode, до CharacterMovement) скорость CMC = скорость точки ядра + догон ошибки
  (×12/с), плюс `AddMovementInput` в ту же сторону — Motion Matching видит настоящие Velocity и Acceleration.
  Ошибка > 120 см — телепорт (сброс). Курс — `SetActorRotation` + `ControlRotation` лицом к сопернику.
* **Режим GASP:** через `Set_CharacterInputState` (рефлексией) — `WantsToStrafe` (лицом к сопернику, стрейф-базы MM)
  и `WantsToWalk` (шаговая походка). Флаги — `bGaspWantsToStrafe`, `bGaspWantsToWalk`.
* `MinAnalogWalkSpeed = 0` (у GASP 150 см/с — дёргал бы медленные шаги ядра).
* Визуальная подмена (MetaHuman Kellan / Manny): `VisualOverrideClass` → `ChildActorComponent «VisualOverride»`
  на меше (как в GASP, ретаргет `ABP_GenericRetarget`).

## API для AnimBP и трека C

Читать из AnimBP: `TryGetPawnOwner → Cast to BoxerCharacter` (категория **Boxing|Anim**, все `BlueprintReadOnly`):

| Свойство | Тип | Смысл |
|---|---|---|
| `bPunching` | bool | идёт удар |
| `CurrentPunch` | `EBoxPunchType` (Jab, Cross, HookL, HookR, UpperL, UpperR) | текущий/последний удар; L — передняя рука |
| `PunchTarget` | `EBoxPunchTarget` (Head, Body) | цель удара |
| `PunchArm` | `EBoxPunchArm` (Lead, Rear) | рука |
| `PunchPhase` | float 0..1 | доля цикла удара; контакт ядра — на `PunchContactFraction` (≈ 0.45) |
| `PunchPhaseAnim` | float 0..1 | то же, переложено: контакт = 0.5 (скраб клипа, как в вебе) |
| `PunchDuration` | float, с | длина цикла удара (ИИ 0.44–0.66, человек 0.34, растёт от усталости) |
| `bBlocking`, `GuardIntegrity` | bool, 0..1 | блок; 1 — свежий, 0 — руки забиты / вот-вот пробьют |
| `SlipAmount` | −1..1 | вес нырка со знаком стороны (синус окна) |
| `SlipSide`, `SlipPhase` | −1/0/1, 0..1 | сторона и линейная фаза уклона |
| `bKnockedDown`, `bKO` | bool | лежит (нокдаун/досрочка), лежит в финале |
| `bStunned` | bool | «оглушён» (после тяжёлого попадания/провала/пробития) |
| `Hurt`, `HurtTarget` | 0..1, Head/Body | вес встряски от попадания |
| `StepKind` | `EBoxStepKind` | текущий шаг ядра (None/Fwd/Back/Left/Right/PivotL/PivotR) |
| `RopeLevel` | 0/1/2 | центр / канаты / угол |
| `Victory`, `bDefeated` | 1/0.5/0, bool | финал |
| `ActiveMontageSlot` | `EBoxMontageSlot` | какой монтаж ведёт C++ |
| `Health`, `StaminaPct`, `Knockdowns`, `bGassed` | (Boxing\|HUD) | для HUD |
| `FighterIndex`, `Opponent`, `Preset` | (Boxing) | 0 — красный/игрок, 1 — синий |

**Монтажи** (категория `Boxing|Montages`): `PunchMontages` (`TMap<EBoxPunchType, UAnimMontage*>`), `BlockMontage`,
`SlipLeftMontage`, `SlipRightMontage`, `HitHeadMontage`, `HitBodyMontage`, `KnockdownMontage`, `GetUpMontage`.
Пустые слоты на BeginPlay подхватываются по имени из `MontageFolder` (`/Game/Boxing/Anim`):
`AM_Jab, AM_Cross, AM_HookL, AM_HookR, AM_UpperL, AM_UpperR, AM_Block, AM_SlipL, AM_SlipR, AM_HitHead, AM_HitBody,
AM_Knockdown, AM_GetUp` (в лог: «монтажей найдено N из 13»). Слот монтажа — **`DefaultSlot`** AnimBP GASP
(полное тело поверх локомоции; для «руки бьют — ноги ходят» нужен layered blend в AnimBP — работа трека C).

* **Синхронизация удара:** `PunchContactTimes` (`TMap<EBoxPunchType, float>`, сек от начала монтажа) → иначе
  AnimNotify с именем, содержащим `Contact` (или `Hit`) → иначе середина монтажа. Скорость монтажа кусочно-линейная:
  до контакта `ContactTime / (доля_контакта × PunchDuration)`, после — остаток монтажа на остаток цикла; рассинхрон
  > `MontageSnapTolerance` (0.035 с) подтягивается `Montage_SetPosition`. Кадр контакта совпадает с резолюцией в ядре.
* Уклон растягивается на окно ядра `SlipWindowSeconds` (0.36 с) и скраббится по `SlipPhase`.
* Блок и нокдаун держат последний кадр (пауза до blend out), пока состояние длится; реакция — при `Magnitude ≥
  HitMontageMinMagnitude` и не поверх своего удара/уклона/нокдауна. Приоритет: нокдаун > удар > уклон > реакция > блок.

**События** (`BlueprintNativeEvent` — переопредели в BP, C++-реализация по умолчанию играет монтажи; плюс делегаты):

| Функция | Делегат | Когда |
|---|---|---|
| `OnPunchStarted(EBoxPunchType Punch, EBoxPunchTarget Target)` | `OnPunchStartedDelegate` | новый цикл удара (фронт по времени старта) |
| `OnHitReceived(Punch, Target, float Magnitude, FVector Direction)` | `OnHitReceivedDelegate` | чистое попадание по этому бойцу; Direction — от атакующего, горизонтальный единичный; Magnitude ≥ 1.1 — тяжёлое |
| `OnBlockedPunch(Punch, float Magnitude, FVector Direction)` | — | удар принят в блок (Magnitude — утечка) |
| `OnKnockdown()` | `OnKnockdownDelegate` | упал (фронт `bKnockedDown`) |
| `OnGetUp()` | `OnGetUpDelegate` | встал после нокдауна |
| `HitReaction(Punch, Target, Magnitude, Direction)` | — | физреакция (зовётся из `OnHitReceived`) |

`HitReaction` по умолчанию — заглушка: `UPhysicalAnimationComponent` (`PhysicalAnimation`, флаг
`bUsePhysicalAnimationDrive`) с пружинами к позе ниже `PhysHeadBone` (`neck_01`) / `PhysBodyBone` (`spine_04`),
частичная смесь физики `PhysBlendPeak` (0.45) гаснет за `PhysHitDuration` (0.35 с), импульс
`Direction × Magnitude × PhysImpulsePerMagnitude` в кость. **Выключена по умолчанию (`bPhysicalHitReactions = false`)**:
на меше GASP (UEFN-манекен + `SandboxCharacter_CMC_ABP`) после включения симуляции тел движок падает в следующем
тике — `Array index out of bounds: 64 into an array of size 0` (64 — индекс кости `neck_01`; все вызовы заглушки
проходят, падает смешивание физики с позой внутри Engine; символов движка нет, точнее не локализовано; с рендером и
без — одинаково). Трек C: физреакцию делать через PhysicsControl / RigidBody-узел в AnimBP (переопределив
`HitReaction`), либо разбираться с этим путём при установленных символах редактора.

## Ввод

Ассеты: `/Game/Boxing/Input/IA_*` + `IMC_Fight` (`Tools/EditorScripts/fight_input.py`); нет ассетов — та же раскладка
создаётся в рантайме (таблица `GActionSpecs` в `BoxingFightPlayerController.cpp` = `SPECS` в скрипте).
Модификаторы Enhanced Input не используются: каждое направление — своё булево действие, стики — Axis1D, пороги и
«экранные» оси считает контроллер.

| Действие | Клавиатура | Геймпад |
|---|---|---|
| джеб / кросс | J / K | X / Y |
| хук левой / правой | U / I | LB / RB |
| апперкот левой / правой | N / M | A / B |
| в корпус (с ударом) | Shift | RT (держать) |
| блок (держать) | Пробел | LT |
| уклон влево / вправо | Q / E | правый стик влево / вправо |
| к сопернику / назад | D / A (→ / ←) | левый стик вправо / влево |
| дуга вверх / вниз по экрану (влево / вправо от лица) | W / S (↑ / ↓) | левый стик вверх / вниз |
| пивот | Shift + W/S | L3 + стик вверх/вниз |
| следующий раунд (если перерыв не авто) | Enter | Start |

Во время своего нокдауна любой удар/блок — тап подъёма (`RiseTap`). Ввод попадает в ядро на границе следующего
фиксированного шага (`QueueAction`), удержание ног — `SetHeldStep` (повтор каждый шаг, кулдаун держит ядро).

## Камера

Порт `InteractiveFight.tsx`: середина пары (сглаживание 3/с), курс за осью «игрок → соперник» с мёртвой зоной ±20°
(`FollowYaw` = `followYaw` из `fightFx.ts`: догон излишка 2.4/с, доцентровывание 0.35/с), камера 1.5 м за серединой
и 3.05 м вправо от игрока на высоте 1.72 м над настилом, взгляд на 0.15 м вперёд на 1.05 м; за канатами — подъём до
0.8 м. Вертикальный FOV 46° переводится в горизонтальный UE по аспекту вьюпорта. Без тряски/наезда (это визуальный
слой веба, не перенесён).

## Проверка

```
UnrealEditor-Cmd.exe BoxingUE.uproject /Game/Boxing/Maps/L_FightTest -game -nullrhi -unattended -nosound ^
  -BoxAutopilot -BoxQuitAfter=20 -BoxLogEvery=1 -BoxLogEvents
```
(в Git Bash — `MSYS_NO_PATHCONV=1`, иначе `/Game/...` превращается в путь Windows). Лог — `Saved/Logs/BoxingUE.log`,
строки `FIGHT …`, `BOXER …`, итоговая `FIGHT СВОДКА`.

MetaHuman вместо манекена: `-BoxVisual=/Game/MetaHumans/Kellan/BP_Kellan.BP_Kellan_C` (или свойство GameMode
`VisualOverridePath` / `ABoxerCharacter::VisualOverrideClass`) — child actor GASP с ретаргетом позы, базовый манекен
скрывается, но продолжает считать позу (монтажи трека C играют на нём и переносятся ретаргетом).

### Результат прогона (01.10.2026)

* Сборка `BoxingUEEditor Win64 Development` — 0 ошибок, 0 предупреждений; паритет ядра (`Tools/CoreHarness`) — OK
  (seed 42/7/1001 совпадают с эталоном TS).
* `L_FightTest`, автопилот, 3 раунда × 12 с (перерыв 2 с), 40 с — без падений и предупреждений от модуля:
  19 попаданий, 39 промахов, переходы раундов со сбросом в центр; расхождение капсулы с точкой ядра в ходе боя ≤ 1 см
  (17 см — только первый кадр спауна/телепорт); AnimBP GASP видит скорость (`abpV` ≈ скорость CMC, 77–96 см/с на шагах).
* Скриншоты: `Docs/screens/fight_06/12/18.png` (манекен), `fight_kellan_10.png` (Kellan) — камера, HUD, бойцы лицом
  друг к другу.
* Монтажей трека C на момент прогона не было («найдено 0 из 13») — удары пока видны только в логе/снимке.
