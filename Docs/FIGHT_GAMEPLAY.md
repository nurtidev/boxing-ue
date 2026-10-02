# Бой в UE: геймплейный слой над ядром (S-41, трек B)

Ядро боя — `FBoxingFightCore` ([FIGHT_CORE_PORT.md](FIGHT_CORE_PORT.md)), его механика не тронута.
Этот слой: режиссёр (GameMode), визуальные бойцы на AnimBP Game Animation Sample + монтажи трека C
([ANIM_SETUP.md](ANIM_SETUP.md), [ANIM_CONTACTS.md](ANIM_CONTACTS.md)), ввод Enhanced Input, камера боя как в вебе,
минимальный HUD.

## Как сыграть самому (владельцу)

1. Собрать модуль (редактор закрыт): `Build.bat BoxingUEEditor Win64 Development -Project=<…>\BoxingUE.uproject -WaitMutex`.
2. Открыть `BoxingUE.uproject` → Content Browser → `Content/Boxing/Maps/L_Ring` → **Play** (Selected Viewport или
   New Editor Window). GameMode уровня уже `BoxingFightGameMode` — ты красный угол (слева), ИИ — синий.
3. Управление — таблица «Ввод» ниже (J/K/U/I/N/M, WASD, Q/E, Пробел; геймпад тоже). Демо без ввода — свойство
   `bAutopilot` у GameMode (World Settings → GameMode Override нельзя править по полям, поэтому проще — запуск
   из командной строки):
   ```
   UnrealEditor.exe "C:\Users\user\Desktop\boxing-ue\BoxingUE.uproject" /Game/Boxing/Maps/L_Ring -game -windowed -ResX=1600 -ResY=900
   UnrealEditor.exe … /Game/Boxing/Maps/L_Ring -game -windowed -BoxAutopilot        (оба под ИИ)
   ```
   `-BoxVisual=none` — манекены вместо MetaHuman Kellan. На `L_Ring` в углу экрана видны предупреждения рендера
   уровня (VSM / Sky Light) — это трек A; спрятать — консоль `DisableAllScreenMessages`.

## Классы (`Source/BoxingUE`)

| Класс | Файлы | Что делает |
|---|---|---|
| `ABoxingFightGameMode` | `BoxingFightGameMode.h/.cpp` | владеет ядром, тикает его **фиксированным шагом** 1/60 с (аккумулятор), применяет ввод игрока на границе шага, спаунит двух бойцов, раздаёт снимок и события |
| `ABoxerCharacter` | `BoxerCharacter.h/.cpp` | визуальный боец: ход через CharacterMovement (для Motion Matching), состояние для AnimBP, монтажи с синхронизацией контакта, стойка, события, физреакция (выкл.) |
| `ABoxingFightPlayerController` | `BoxingFightPlayerController.h/.cpp` | Enhanced Input → действия ядра; камера боя; скрытие ближних канатов |
| `ABoxingFightHUD` | `BoxingFightHUD.h/.cpp` | Canvas: здоровье/стамина, раунд/время, нокдаун, перерыв, итог |
| `EBox*` / `FBoxerPreset` | `BoxingFightBPTypes.h` | UENUM/USTRUCT-зеркала enum'ов ядра для Blueprint (ядро — plain C++ без UHT) |

Ядро получило только **читающие** поля снимка (поведение и паритет не изменились, `Tools/CoreHarness` — OK):
`FFighterState::PunchDuration`, `PunchTimeToContact`, `SlipPhase`, `bStaggered`.

## Подключение к уровню

* **World Settings → GameMode Override = `BoxingFightGameMode`** (сделано в `L_Ring` скриптом
  `Tools/EditorScripts/fight_ring.py` и в `L_FightTest` — `fight_testmap.py`), или в URL карты:
  `<карта>?game=/Script/BoxingUE.BoxingFightGameMode`.
* Центр ринга: актор с тегом **`RingCenter`** (позиция = центр и высота канваса; в `L_Ring` — TargetPoint в (0,0,0),
  добавлен `fight_ring.py`). Нет такого — `RingCenter` из свойств GameMode, высота — трассой вниз.
* Ринг выровнен по осям мира (ядро: X → X_ue, Z → Y_ue, метры → см).
* Пешки у игрока нет (`DefaultPawnClass = None`, `RestartPlayer` пустой): бойцы — ИИ-пешки (`AAIController`, нужен CMC),
  ввод контроллер шлёт в ядро.

Свойства GameMode (`Boxing|Fight`, `Boxing|Ring`): `RedPreset`/`BluePreset` (`FBoxerPreset`), `Seed`, `Rounds`,
`RoundSeconds` (55 — калибровка веба), `BreakSeconds`, `bAllowDraw`, `bAutopilot`, `FixedStep`; `BoxerClass` /
`BoxerClassPath` (BP_Boxer), `VisualOverridePath` (по умолчанию **MetaHuman Kellan**), `PhysHitsOverride`.

Постановка раунда (S-53, [FIGHT_CORE_PORT.md](FIGHT_CORE_PORT.md) «Постановка раунда»): `-BoxNoCorners` (бой с центра),
`-BoxPreGong=S` (пауза в углах до первого гонга, 1 с), `-BoxStageShots` (скриншоты стадий `<префикс>_stage_*.png`).

Командная строка: `-BoxAutopilot`, `-BoxSeed=N`, `-BoxRoundSec=S`, `-BoxBreakSec=S`, `-BoxQuitAfter=S`,
`-BoxLogEvery=S` (позиции/состояние в LogTemp), `-BoxLogEvents`, `-BoxShots=6,12` (скриншоты по времени),
`-BoxHitShots=N` (скриншоты в кадре контакта первых N попаданий/блоков, раз в ≥ 1.5 с), `-BoxShotPrefix=имя`
(файлы `Docs/screens/<имя>_NN.png`, `<имя>_hitK_<Удар>_<land|block>.png`), `-BoxVisual=<класс>|none`, `-BoxPhysHits=0|1`;
«ощущение удара» и отладка камеры/реакции — `-BoxFeel=0`, `-BoxMinSep=СМ`, `-BoxVisualRed=`/`-BoxVisualBlue=`, `-BoxCamSide`,
`-BoxFeelDraw`, `-BoxHitShotDelay=…` и др. — [FIGHT_FEEL.md](FIGHT_FEEL.md). Свойства GameMode `VisualOverridePathRed/Blue`
(своя подмена угла, фолбэк — `VisualOverridePath`), `FeelOverride`, `VisMinSepCm`.|block>.png`), `-BoxVisual=<класс>|none`, `-BoxPhysHits=0|1`.

## Боец: BP_Boxer, AnimBP GASP, MetaHuman

AnimBP GASP читает пешку через BP-интерфейс `BPI_SandboxCharacter_Pawn`. Из C++ его не реализовать, поэтому
**`/Game/Boxing/Blueprints/BP_Boxer` = копия `SandboxCharacter_CMC`, перепривязанная к `ABoxerCharacter`**
(`Tools/EditorScripts/fight_blueprints.py`). Логический меш — UEFN-манекен с **исходным `SandboxCharacter_CMC_ABP`**
(в нём есть `DefaultSlot`, куда идут все монтажи боя; `FIGHT_ABP=/Game/Boxing/Anim/ABP_Boxer` — назначить AnimBP трека C).

* **Видимый боец — MetaHuman Kellan** (`VisualOverridePath`): child actor GASP `BP_Kellan` на меше, копирует позу
  ретаргетом (`ABP_GenericRetarget`); логический манекен скрыт (`SetVisibility(false)`), но позу считает
  (`AlwaysTickPoseAndRefreshBones`). Стабильно в прогонах; `-BoxVisual=none` — манекен.
* **Ход.** Каждый кадр (в тике GameMode, до CharacterMovement — пререквизит) скорость CMC = скорость точки ядра +
  догон ошибки (×12/с), плюс `AddMovementInput` — Motion Matching видит Velocity/Acceleration (`abpV` в логе ≈ скорость
  CMC). Ошибка > 120 см — телепорт (сброс раунда). Курс — `SetActorRotation` + `ControlRotation` лицом к сопернику.
* **Режим GASP** через `Set_CharacterInputState` (рефлексией): `WantsToStrafe` + `WantsToWalk`; `MinAnalogWalkSpeed = 0`.

## Монтажи (трек C) и стойка

Все монтажи — UEFN-набор `/Game/BoxingLocal/Anim/AM_*` (вне git), все в слоте `DefaultSlot`. **С S-41 feel** удары, блок,
уклоны, реакции и стойка рук играют на НАТИВНОМ пост-процессе логического меша (`UBoxerLayerAnimInstance`, свой
`DefaultSlot`, накладывается от `spine_01` вверх), полнотелые (нокдаун/нокаут/подъём/финал, стойка ног) — на AnimBP GASP;
подробно — [FIGHT_FEEL.md](FIGHT_FEEL.md). С `-BoxFeel=0` — как раньше, всё на всё тело. Поиск: `MontageFolders` = `/Game/BoxingLocal/Anim`, затем
`/Game/Boxing/Anim`; в логе `монтажей найдено 19 из 19 … контакт джеба 0.433 с`.

| Свойство | Монтаж | Как играется |
|---|---|---|
| `PunchMontages[Jab…UpperR]` | AM_Jab, Cross, HookL, HookR, UpperL, UpperR | скраб по фазе ядра: кадр нотифая **Contact** = резолюция удара в ядре (`SyncPeakMontage`, кусочно-линейно); отход после контакта ужат до `RecoverFactor` × время до контакта (хвосты Mixamo ~1.3 с не влезают в цикл ядра ~0.3 с); цикл ядра кончился — бленд-аут 0.18 с |
| `BodyHookMontage` | AM_BodyHook | HookL/HookR с целью Body |
| `BlockMontage` | AM_Block | старт за `BlockRaiseSeconds` до нотифая **GuardUp**, держит этот кадр, пока блок |
| `BlockHitMontage` | AM_BlockHit | удар принят в блок (`OnBlockedPunch`), потом снова блок |
| `SlipLeftMontage` / `SlipRightMontage` | AM_SlipL / AM_SlipR | нотифай **Peak** — на середину окна уклона ядра (0.36 с) |
| `HitHeadMontage` / `HitBodyMontage` | AM_HitHead / AM_HitBody | `Magnitude ≥ 0.35`, ×1.6, не поверх своего удара/уклона/нокдауна |
| `KnockdownMontage` / `KnockoutMontage` | AM_Knockdown / AM_Knockout | нокдаун; если в том же кадре досрочка (`bKO`) — нокаут; держит последний кадр |
| `GetUpMontage` | AM_GetUp | подъём, ×1.3 |
| `VictoryMontage` / `DefeatMontage` | AM_Victory / AM_Defeat | финал (не лёжа), держит последний кадр |
| `GuardMontage` | AM_Guard | **боевая стойка**: idle GASP — руки вниз, поэтому стоящий боец (скорость < `GuardStartSpeed` 20 см/с дольше `GuardSettleSeconds` 0.12 с, слот свободен, не блок) уходит в стойку, а на ходу (> `GuardStopSpeed` 40 см/с) — бленд `GuardBlendSeconds` 0.2 с обратно в локомоцию; удары/реакции перебивают стойку, по окончании она возвращается. Флаг `bPlayGuardMontage` |

Хелперы: `GetMontageNotifyTime(Montage, Name)` (NotifyName события или `GetNotifyName()` у `AnimNotify_PlayMontageNotify`,
время — `GetTriggerTime()`), `GetPunchMontage(Punch, Target)`, `GetPunchContactTime(Punch)` (`PunchContactTimes` →
Contact → середина). Приоритет: финал > нокдаун > удар > уклон > реакция/блок > стойка.

**Минус полнотелого слота** (только с `-BoxFeel=0`): во время удара/реакции ноги — из клипа (Root Lock), а капсула
продолжает шаг ядра — короткое скольжение ступней. Слой верха (по умолчанию) это убрал: ноги всегда — Motion Matching.

## API для AnimBP

`TryGetPawnOwner → Cast to BoxerCharacter` (категория **Boxing|Anim**, `BlueprintReadOnly`): `bPunching`, `CurrentPunch`
(`EBoxPunchType`), `PunchTarget`, `PunchArm`, `PunchPhase`, `PunchPhaseAnim` (контакт = 0.5), `PunchDuration`,
`PunchContactFraction`, `bBlocking`, `GuardIntegrity`, `SlipAmount`, `SlipSide`, `SlipPhase`, `bKnockedDown`, `bKO`,
`bStunned`, `Hurt`, `HurtTarget`, `StepKind`, `RopeLevel`, `Victory`, `bDefeated`, `ActiveMontageSlot` (`EBoxMontageSlot`:
None/Punch/Block/BlockHit/Slip/Hit/Knockdown/GetUp/Finale/Guard); HUD: `Health`, `StaminaPct`, `Knockdowns`, `bGassed`;
`FighterIndex`, `Opponent`, `Preset`.

**События** (`BlueprintNativeEvent`, C++ по умолчанию играет монтажи) и делегаты: `OnPunchStarted(Punch, Target)` /
`OnPunchStartedDelegate`; `OnHitReceived(Punch, Target, Magnitude, Direction)` / `OnHitReceivedDelegate`;
`OnBlockedPunch(Punch, Magnitude, Direction)`; `OnKnockdown()` / `OnKnockdownDelegate`; `OnGetUp()` / `OnGetUpDelegate`;
`HitReaction(Punch, Target, Magnitude, Direction, bBlocked)`.

## Физреакция — ВЫКЛЮЧЕНА (причина падения найдена; реакцию дают пружины костей)

**Обновление S-41 feel:** реакция на попадание — аддитивные пружины костей (порт веба, [FIGHT_FEEL.md](FIGHT_FEEL.md)).
Причина падения физики — гонка тика `UPhysicalAnimationComponent` с параллельной оценкой позы (VERIFY_ON_PC.md, разд. 3);
с `-BoxPhysHits=1` код теперь ставит `a.ParallelAnimEvaluation 0`, и падения нет (90 с, 37 попаданий). Ниже — исходное описание.

Реализована по [HIT_REACTION.md](HIT_REACTION.md) (подход 1) на **видимом** меше Kellan (`Body`, 19 тел физассета):
`UPhysicalAnimationComponent` + строки `DT_HitReaction_PhysAnim` по порядку, тела ниже `spine_01` симулируются без
контактов, огибающая смеси 0.35 + 0.25·mag (до 0.85, спад τ = 0.1 с), импульсы по таблице (джеб/кросс/хук вбок/апперкот
вверх/корпус → `spine_03`/блок → предплечье ×0.4), тяжёлый — ещё в `spine_01`. Инициализация проходит
(`физреакция на BP_Kellan_C_0.Body (тел 19, строк пружин 9)`), **но через кадр после включения симуляции движок падает**:
`Array index out of bounds: 1 into an array of size 0` (на логическом UEFN-меше раньше — то же, `64 into 0`; без
`PhysicalAnimation`-пружин — зависание). Все вызовы нашего кода проходят — падает смешивание физики с позой внутри Engine
(вероятно, связка «анимация с ретаргет-копией позы / leader pose + симуляция тел»); символов редактора нет — точнее не
локализовано. Поэтому `bPhysicalHitReactions = false` (и в BP_Boxer); `-BoxPhysHits=1` — включить для отладки.
Дальше: поставить Editor symbols и посмотреть стек, или Physics Control / RigidBody-узел в AnimBP видимого меша.

## Ввод

Ассеты: `/Game/Boxing/Input/IA_*` + `IMC_Fight` (`Tools/EditorScripts/fight_input.py`); нет ассетов — та же раскладка
в рантайме (`GActionSpecs` в `BoxingFightPlayerController.cpp` = `SPECS` в скрипте).

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

Во время своего нокдауна любой удар/блок — тап подъёма. Ввод попадает в ядро на границе следующего фиксированного шага.

## Камера

Порт `InteractiveFight.tsx`: середина пары (сглаживание 3/с), курс за осью «игрок → соперник» с мёртвой зоной ±20°
(`FollowYaw` = `followYaw` из `fightFx.ts`), 1.5 м за серединой и 3.05 м вправо от игрока на высоте 1.72 м над канвасом,
взгляд на 0.15 м вперёд на 1.05 м; вертикальный FOV 46° → горизонтальный UE по аспекту.

**Ближние канаты.** Выбор — **скрывать, а не поднимать камеру** (высота как в вебе, кадровка не меняется): сторона
канатов/столбов/подушек, за линией которой стоит камера (дальше `RopeHideFrom` = 255 см от центра вдоль нормали стороны),
не рисуется — `SetRenderInMainPass(false)` + `SetRenderInDepthPass(false)` (тень остаётся). Аналог `fadeNearCamera` веба.
Части — акторы с тегом `RingRope` или метками `Ring_Rope*`/`Ring_Post*`/`Ring_Pad*` (L_Ring: 40 компонентов), `Rope*`/`Post*`
(L_FightTest). Флаг `bHideNearRopes`.

## Проверка

```
UnrealEditor-Cmd.exe BoxingUE.uproject /Game/Boxing/Maps/L_Ring -game -nullrhi -unattended -nosound ^
  -BoxAutopilot -BoxQuitAfter=30 -BoxLogEvery=3 -BoxLogEvents
UnrealEditor-Cmd.exe BoxingUE.uproject /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -windowed -ResX=1280 -ResY=720 ^
  -BoxAutopilot -BoxHitShots=6 -BoxShotPrefix=ring_fight -BoxQuitAfter=30 -ExecCmds=DisableAllScreenMessages
```
(в Git Bash — `MSYS_NO_PATHCONV=1`). Лог — `Saved/Logs/BoxingUE.log`: `FIGHT …`, `BOXER …`, `FIGHT СВОДКА`.

### Результаты (01.10.2026)

* Сборка — 0 ошибок, 0 предупреждений; паритет ядра — OK.
* `L_Ring`, автопилот, 30 с: без падений, 14 попаданий / 36 промахов; 19/19 монтажей, контакт джеба 0.433 с из нотифая;
  монтажи в логе: удар (`mont=1`), реакция (`5`), стойка (`9`); расхождение капсулы с ядром ≤ 1–3 см (17 см — кадр спауна).
* Скриншоты `Docs/screens/ring_fight_03.png`, `ring_fight_hit1..6_*.png` — в кадре контакта: джеб/кросс/хук читаются
  (рука выпрямлена к голове соперника, у получившего — откид), ближние канаты скрыты, Kellan чистый; стоящий в стойке
  (перчатки у подбородка), идущий — руки вниз (локомоция GASP).
* Проблемы: на ближней дистанции (ядро 0.95–1.0 м) торсы и руки перекрываются (клипы Mixamo с наклоном вперёд,
  контакта/упора кулака как в вебе нет); реакция `AM_HitHead` даёт сильный откид корпуса; скольжение ступней во время удара
  (полнотелый слот); физреакция выключена (см. выше).
