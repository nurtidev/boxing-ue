# Порт ядра интерактивного боя: TypeScript → C++ (S-41)

Ядро интерактивного (реалтайм) боя из веба (`web/src/engine/interactive.ts` + `web/src/engine/interactive/*.ts`)
перенесено в C++ для прототипа на UE5 в **упрощённом** виде. Это движок-агностичный класс `FBoxingFightCore`:
plain C++, только `CoreMinimal.h`, без `UObject`/Engine. UE-слой (Pawn / AnimBP / GameMode) владеет экземпляром,
тикает его и читает состояние.

## Файлы

| C++ | Что |
|---|---|
| `Source/BoxingUE/Public/FightTypes.h` | enum'ы и структуры: удары, цели, действия, состояние бойца, события, снапшот, итог |
| `Source/BoxingUE/Public/BoxingFightCore.h` | `FBoxingRng` (mulberry32) + класс `FBoxingFightCore` |
| `Source/BoxingUE/Private/BoxingFightCore.cpp` | вся механика: константы, формулы, цикл, ИИ |

### Соответствие TS → C++

| TS (web/src/engine/…) | C++ (`BoxingFightCore.cpp`) |
|---|---|
| `rng.ts` `RNG.next/range` | `FBoxingRng::Next/Range` (в `.h`) — uint32-арифметика, побитово как в TS |
| `interactive/types.ts` | `FightTypes.h` (+ внутренние `FPunchAct`, `FDownState` в `.h`) |
| `interactive/state.ts` `FightState` | поля `FBoxingFightCore` + `Init()` (тот же порядок бросков ГСЧ) |
| `interactive/runtime.ts` `Runtime`, `regenStamina` | `FRuntime` (+ `Fatigue/StamCap/FatigueForm/PlayerForm/Health`), `RegenStamina()` |
| `interactive/mirror.ts` (зеркало `simulate.ts`) | `LandProb`, `DamagePerHit`, `KnockdownChanceF` + константы `SIM_*` |
| `interactive/punches.ts` | таблицы `AI_PUNCH_DUR/TYPE_ACC/TYPE_POW/STAM_COST/RANGE…`, `RangeFactor()`, `ArmFor()` |
| `interactive/style.ts` | `SIM_STYLE_OUTPUT/AI_MIX/AI_PREF_DIST/AI_BODY/AI_GUARD_BONUS`, нормировки `AiAccNorm/AiPowNorm/AiStamNorm` |
| `interactive/ring.ts`, `ringSize.ts` | `ROPE_HALF/RING_HALF/ROPE_ZONE/DIST_MIN/DIST_MAX/DIST_START`, `ClampRing()` |
| `interactive/footwork.ts` | `PlaceFighters/Axis/Tangent/StepTarget/MoveStep/MoveTo/CanRetreat/RopeLevel/RopePen/AngleOf/StartStep/Distance/YawOf/UpdateStep` |
| `interactive/defense.ts` | `ArmsTired/BlockLeakMul/BlockCostMul/GuardPressureOf/GuardBreaks/BlockRegenMul/GuardIntegrity/SlipTiming` |
| `interactive/guard.ts` | `TryRaiseBlock/TrySlip/BlockPunch/UpdateGuard` |
| `interactive/attack.ts` | `StartPunch/ResolvePunches/ResolveContact` |
| `interactive/knockdown.ts` | `TryKnockdown/Knockdown/UpdateCount/RiseUp/HumanRiseTap` |
| `interactive/rounds.ts` | `Proceed/EndRound/ScoreRound/DecideByCards/Finish/BuildResult` |
| `interactive/ai.ts` | `AiThink/IsDiver/AiPickType/AiFootwork/AiReactToPunch`, `PredictableReps` |
| `interactive/player.ts` | `ApplyAction` → `HumanPunch/HumanSlip/HumanBlock/StartStep` |
| `interactive/loop.ts` | `Tick/UpdateFighter` (порядок вызовов = порядок бросков ГСЧ) |
| `interactive/hud.ts` | `GetSnapshot/PollEvents` (без ГСЧ) |
| `interactive/corners.ts`, `mirror.ts` (часть HUD), `hud.ts` (подсказки) | **не перенесены** — см. «Упрощения» |

Все числовые константы — те же, что в TS; в `.cpp` они сгруппированы по исходным модулям
(`// ---------- footwork.ts ----------` и т.п.). При правке баланса в вебе синхронизировать оба места.

## Что перенесено

- **Удары**: 4 вида × рука (хук/апперкот любой рукой: lead точнее, rear сильнее) × цель голова/корпус.
  Фазы цикла (`start → contact (45% цикла) → end`), телеграф ИИ (`AI_PUNCH_DUR`), мгновенный старт человека (0.34 с),
  замедление рук от усталости (`EXH_SLOW`), «вялый» удар из пустого бака, отказ удара «нет сил» (событие `Gassed`).
- **`RangeFactor`** — зоны эффективности по дистанции, нормированной на размах (прямые — средняя/дальняя,
  хук/апперкот — вплотную; за `max` — гарантированный промах без броска ГСЧ).
- **Резолюция контакта** по формулам-зеркалам `simulate.ts`: `landProb`, `damagePerHit` (абс. сила `power·√(mass/70)`
  vs подбородок), форма дня ±10%, усталость, «встряска», угол, канаты, контр-окно, «пойман на выходе из нырка».
- **Стамина**: цена ударов/шагов/уклонов, восстановление 3 ед/с, когда не бьёшь; в блоке — вполовину, в долгом блоке —
  таяние; потолок «бака» на дистанцию (`tank`) тает по раундам; удары в корпус выбивают дыхание.
  *Отдельного «восстановления вне дистанции» в текущем TS-коде нет* — восстановление зависит только от того,
  бьёт ли боец и держит ли блок; так и перенесено.
- **Блок с ценой**: усталость рук, давление серий, пробитие силовым при давлении ≥ 2.6, «руки идут вниз»
  после долгого удержания, ватные руки на пустом баке. Кнопка блока запоминается (поднимется, как только станет можно).
- **Уклоны**: окно 0.36 с, кулдаун 0.62 с, тайминг (вовремя/по реакции/поздно/рано) — только у человека,
  апперкот ловит нырок, корпус нырком не спрятать; удачный уклон → провал атакующего + контр-окно 0.62 с.
- **Ноги в 2D-ринге**: шаги вперёд/назад, боковые дуги, пивоты, угол (−15% защиты соперника на ~0.4 с), канаты/угол
  (`RopeLevel` 1/2: шаг назад «съеден», уклон хуже, урон +10%), квадрат ±`RING_HALF` = 3.05 − 0.28 м, дистанция 0.9..2.0 м.
- **Нокдауны**: от давления раунда (шанс simulate «за раунд», выдаваемый по ходу раунда) + flash-нокдаун панчера;
  3 нокдауна = RSC; счёт 0.75 с/счёт до 10; ИИ решает подняться одним броском (шанс KO — формула simulate),
  человек набивает подъём тапами (`RiseTap`), прогресс утекает.
- **Раунды**: таймер, перерыв, восстановление в углу (износ ×0.72, стамина +28% до потолка бака),
  RSC-H у любителей по итогам раунда.
- **3 судьи**: каждый судит каждый раунд (10-балльная система; нокдаун видят одинаково, близкий раунд — каждый
  со своим разбросом ±1.6), решение большинством; ничья только при `bAllowDraw`, иначе — по суммам карт / попаданиям.
- **ИИ**: темп по стилю (∝ `STYLE_OUTPUT` simulate) × форма/усталость/здоровье, выбор удара по миксу стиля и дистанции,
  наказание промаха встречным (контровик/панчер), держит дистанцию своего стиля сериями с гистерезисом, давящие
  режут ринг, мягкие кружат влево и уходят с канатов (из угла — пивотом), реакция на удар человека блоком/уклоном
  с «чтением» однообразия, ломка «черепахи», ловля ныряльщика апперкотами/джебами/в корпус, укороченные удары
  после двух провалов, бережёт дыхание против человека.
- **ИИ против ИИ** (оба `bAiControlled`) — режим автопилота TS: без поправок темпа «против человека» и без
  замедления рук от усталости (как в вебе, ради паритета с simulate).

## Упрощения (осознанные)

1. **Нет постановки углов** (`corners.ts`): ни выхода из углов по гонгу, ни ухода в угол на перерыв, ни нейтрального
   угла на нокдауне. Бой и каждый раунд стартуют с центра на `DIST_START` (это режим `corners:false` веба).
   Фазы `walkout` нет. На нокдауне стоящий остаётся на месте.
2. **Перерыв по таймеру**: в вебе перерыв ждёт `proceed()` от UI; здесь — `BreakSeconds` (по умолчанию 60 с)
   с авто-переходом, либо ручной `Proceed` при `bAutoProceed = false`.
3. **Длина раунда — параметр** (по умолчанию 180 с). Баланс веба откалиброван под 55-секундный раунд
   (`SIM_ROUND_SCALE`: попаданий за 55 с ≈ попаданий раунда simulate). Чтобы 3-минутный раунд не давал втрое больше
   износа/нокдаунов/очков, все **раундовые** величины умножаются на `RoundK = 55 / RoundSeconds`: износ за попадание,
   давление нокдауна и тяжесть KO, RSC-H, перевес у судей и `CLEAR_MARGIN`, расход бака; flash-шанс размазывается
   на `FLASH_SPREAD / RoundK` попаданий. Посекундная экономика (стамина, кулдауны, тайминги) не масштабируется.
   **При `RoundSeconds = 55` формулы совпадают с TS один-в-один.** На 180 с баланс не выверен плейтестом —
   возможно, понадобится подстройка (например, темпа ИИ или регена).
4. **Нет проекции веса** (`Boxer.fightProfile`): `MassForPower = DurabilityMass = WeightKg`. Сгонку/переход
   по весу и скидку amateur→pro считать снаружи и подавать уже готовые статы. `Seasoning` поддержан так же,
   как в интерактиве веба: налог на «бак» по раундам (`GREEN_FADE_K`) и смещение близких раундов у судей.
5. **Нет HUD-подсказок**: `cue` (уклон/контра/пробит/пойман/руки устают), `read` («ИИ читает»), `message`
   («Раунд N»). Аналоги выводятся из событий (`bCounter`, `bGuardBreak`, `bCaught`, `bPerfect`) и снапшота
   (`GuardIntegrity`, `bGassed`, `bAngle`).
6. **Нет хит-стопа, повтора нокаута, звука, камеры, зеркал/рига** — это визуальный слой UE.
7. **Нет dev-режимов** `passiveAi`, `aiFootworkOnly`, `glassJaw`.
8. **Стойка только ортодоксальная**: `HookL/UpperL` = передняя (lead) рука, `HookR/UpperR` = дальняя (rear).
   Левша — задача UE-слоя (зеркалить анимации и маппинг клавиш).
9. **Имена/строки** не хранятся: решение — `EDecisionKind` вместо русской строки, имена бойцов знает UE-слой.
10. **Обобщение «игрок = красный угол»**: в TS человек всегда индекс 0. Здесь любой боец может быть человеком
    (`bAiControlled = false`); ИИ реагирует на удары человека-соперника, тайминг уклона — у любого человека.
    Для стандартной схемы (0 — человек, 1 — ИИ) поведение то же, что в TS.
11. **Мелкая защита от бага TS**: если досрочка случилась на первом из двух ударов одного тика, второй удар уже
    не резолвится (в TS он мог резолвиться после `over`). На сидированный ход обычных боёв не влияет.

## Публичный API для UE-слоя

```cpp
#include "BoxingFightCore.h"

FBoxingFightCore Core;            // член Pawn/GameMode (не UPROPERTY: plain C++)

FFightConfig Cfg;
Cfg.Fighters[0].Stats = {78, 80, 76, 74, 75, 82, 79}; // Power, HandSpeed, Footwork, Stamina, Chin, Technique, Defense
Cfg.Fighters[0].ReachCm = 185; Cfg.Fighters[0].WeightKg = 71;
Cfg.Fighters[0].Style = EBoxStyle::Technical; Cfg.Fighters[0].bAiControlled = false; // игрок
Cfg.Fighters[1] = ...;            Cfg.Fighters[1].bAiControlled = true;               // ИИ
Cfg.Rounds = 3; Cfg.RoundSeconds = 180; Cfg.BreakSeconds = 60; Cfg.Seed = 12345;
Core.Init(Cfg);

// Ввод (Enhanced Input → действие):
Core.ApplyAction(0, EFightAction::Jab);                       // в голову
Core.ApplyAction(0, EFightAction::HookR, EPunchTarget::Body); // Shift+хук правой — в корпус
Core.ApplyAction(0, EFightAction::BlockStart);  /* … */  Core.ApplyAction(0, EFightAction::BlockEnd);
Core.ApplyAction(0, EFightAction::SlipLeft);
Core.ApplyAction(0, EFightAction::StepLeft);    // по удержанию — повторять, ядро само держит кулдаун
Core.ApplyAction(0, EFightAction::RiseTap);     // во время своего нокдауна

// Тик — фиксированным шагом (Tick внутри зажимает Dt в [0, 0.05], как loop.ts):
Accum += DeltaSeconds;
while (Accum >= 1.f / 60.f) { Core.Tick(1.f / 60.f); Accum -= 1.f / 60.f; }

const FFightSnapshot S = Core.GetSnapshot();     // каждый кадр
for (const FFightEvent& E : Core.PollEvents()) { /* звук, VFX, камера, хит-реакции */ }
if (Core.IsOver()) { const FFightResult& R = Core.GetResult(); }
```

`ApplyAction` возвращает `true`, если действие принято (удар вышел, шаг начат…). Действия для бойца под ИИ
игнорируются. `Proceed` работает для любого индекса.

### Как AnimBP читать состояние

Всё — в `S.Fighters[i]` (`FFighterState`), читать в `NativeUpdateAnimation` (или прокинуть из Pawn):

- **Позиция и курс.** Ядро — метры в плоскости (X, Z), центр ринга (0, 0).
  В UE: `Location = FVector(F.X * 100, F.Z * 100, FloorZ)`, `Rotation = FRotator(0, F.YawDegUE, 0)`.
  `YawDegUE` уже в соглашении UE (лицом к сопернику). `Yaw` — исходный радианный курс TS/three.js (на случай отладки).
  Позиция меняется рывками только во время шага — сглаживать (`VInterpTo`) не обязательно, но доворот можно.
- **Удар.** `bPunching`, `Punch` (`EPunchType`: какой клип), `PunchTarget` (голова/корпус: вариант клипа),
  `PunchPhaseAnim` 0..1 — **скраб клипа**: контакт = 0.5 (кадр попадания в клипе ставить на середину,
  как делает `SkinnedFighter` в вебе); `PunchPhase` — сырая доля времени цикла (контакт = 0.45).
  Длительность цикла ИИ разная по удару (0.44..0.66 с), человека — 0.34 с, обе растягиваются усталостью —
  поэтому скраб по фазе, а не проигрывание с фиксированной скоростью.
- **Защита.** `bBlocking` (поза блока), `GuardIntegrity` (дрожь/опускание рук), `Slip` −1..1 (вес аддитивного
  нырка влево/вправо, уже синус-кривая окна уклона).
- **Реакции.** `Hurt` 0..1 + `HurtTarget` (голова/корпус) — вес хит-реакции; `bDown`/`bKO` — нокдаун / нокаут
  (поза лёжа); `Victory`/`bDefeated` — финальные позы.
- **Ноги.** `Step` (`Fwd/Back/Left/Right/PivotL/PivotR`) — какой клип шага играть; `RopeLevel` — у канатов/в углу.
- **Бой.** `S.Phase`, `S.Round`, `S.TimeLeft`, `S.BreakLeft`, `S.DownWho`/`S.DownCount`/`S.RiseProgress`,
  `S.JudgeTotals`, `S.Distance`.

### События (`PollEvents`)

| Kind | Attacker / Defender | Поля |
|---|---|---|
| `Hit` | кто бил / по кому | `Magnitude` (урон по здоровью; ≥ 1.1 — «тяжёлый», порог хит-стопа веба), `Punch`, `Arm`, `Target`, `bCounter`, `bGuardBreak`, `bCaught` |
| `Blocked` | кто бил / кто закрылся | `Magnitude` — утечка сквозь блок |
| `Slipped` | кто провалился / кто нырнул | `bPerfect` |
| `Miss` | кто бил / — | вне дистанции или мимо |
| `Knockdown` | кто уронил / кто упал | |
| `Gassed` | человек | удар не вышел (пустой бак) |
| `RoundEnd` | — | `Round` |
| `FightEnd` | победитель / проигравший (−1 при ничьей) | итог — `GetResult()` |

## Детерминизм

- ГСЧ — mulberry32 с uint32-арифметикой, тот же алгоритм и сид (`Seed == 0 → 1`), что `rng.ts`.
  Эталон `seed 12345`: `0.9797282677609473, 0.3067522644996643, 0.484205421525985` (сверено эмуляцией uint32 в node).
- Внутренние величины — `double` (как `number` в JS). Чтение (`GetSnapshot`/`PollEvents`) ГСЧ не трогает.
- Один и тот же сид + одна и та же последовательность `Dt` + один и тот же ввод → тот же бой. Для воспроизводимости
  в UE тикать фиксированным шагом (см. выше): переменный кадр даёт другую последовательность `Dt`.
- Межплатформенная бит-в-бит гарантия не даётся (`pow/sqrt/atan2/sin` из CRT могут различаться в последнем бите).

### Эталон паритета с TS

Снят с веб-движка (`InteractiveFight`, `corners:false`, `autopilot:true`, `roundSeconds:55`, `rounds:3`,
`dt = Math.fround(1/60)`, перерыв — сразу `proceed()`), бойцы:
A = `{78,80,76,74,75,82,79}`, reach 185, 71 кг, technical; B = `{84,74,70,78,77,74,72}`, reach 180, 71 кг, pressure;
`seasoning 1`, `massForPower = durabilityMass = 71`.

| seed | итог | судьи | чистые попадания A-B |
|---|---|---|---|
| 42 | A, DEC | 30-27 30-27 30-27 | 50-26 |
| 7 | A, DEC | 30-27 30-27 30-27 | 62-39 |
| 1001 | A, DEC | 28-29 29-28 29-29 | 41-39 |

C++ при `RoundSeconds = 55`, `BreakSeconds = 0`, обоих `bAiControlled` и тике `1.f/60.f` должен дать те же числа.

## Статус проверки

- Компилятора C++ (MSVC/clang/gcc) и UE на машине пока нет — **ядро не собиралось и не запускалось**.
- Синтаксис и семантика проверены фронтендом clang 19 (clang-tidy из Visual Studio 2022) с заглушкой
  `CoreMinimal.h` (int32/uint32/uint8, `FMath`, мини-`TArray` без STL), таргеты `x86_64-pc-windows-msvc` и
  `x86_64-pc-linux-gnu`, `-std=c++20 -Wall -Wextra -Wshadow-all -Wconversion` + clang static analyzer — 0 ошибок,
  0 предупреждений (кроме оптимизационной подсказки о порядке полей/паддинге).
- Тестовый драйвер ИИ-vs-ИИ (детерминизм по сиду, 200 сидов до конца, паритет с эталоном TS выше) написан, но
  не запускался. Первым делом после установки компилятора: собрать его и сверить таблицу паритета.
