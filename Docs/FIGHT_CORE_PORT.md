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
| `Source/BoxingUE/Public/FightStaging.h` | геометрия углов ринга (namespace `BoxingStaging`): константы и чистые функции — их зовут ядро, рефери, камера (S-53) |
| `Source/BoxingUE/Private/FightStaging.cpp` | постановка раунда: стадии выхода/перерыва/нейтрального угла/возврата + отдых в углу (порт `corners.ts`, S-53) |

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
| `interactive/corners.ts`, `rounds.ts` `beginCornerRest/cornerRecover` | `FightStaging.cpp` (стадии) + `FightStaging.h` (`BoxingStaging::CornerOf/NeutralCorner/MeetPoint/FarNeutral/LyingBody/NeutralPathClear/NeutralFor`) — S-53 |
| `mirror.ts` (часть HUD), `hud.ts` (подсказки) | **не перенесены** — см. «Упрощения» |

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

1. ~~Нет постановки углов~~ — **перенесена в S-53**, см. «Постановка раунда» ниже. Прежний режим «раунд с центра»
   (`corners:false` веба) остался: `FFightConfig::bCorners = false`.
2. **Перерыв по таймеру**: в вебе перерыв ждёт `proceed()` от UI; здесь — `BreakSeconds` (по умолчанию 60 с)
   с авто-переходом, либо ручной `Proceed` при `bAutoProceed = false`. С углами авто-переход ждёт ещё и того, что оба
   дошли до своих углов (на исход не влияет: ходьба не тратит боевое время).
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

## Постановка раунда (S-53)

Порт `web/src/engine/interactive/corners.ts` (+ отдых в углу из `rounds.ts`, S-43). Включена по умолчанию
(`FFightConfig::bCorners = true`).

- **Углы.** Центр бойца в углу — `CORNER_SPOT = ROPE_HALF − 0.42 = 2.63` м по обеим осям. Красный (−,−), синий (+,+),
  нейтральные (+X −Z) и (−X +Z) — знаки `CORNER_SIGNS` web `ringSize.ts`; в UE это (±263, ±263) см — ровно маркеры
  `Corner_Red/Blue/NeutralA/B` в `L_Ring`. Геометрия — `FightStaging.h` (`BoxingStaging::`), без ядра и без ГСЧ.
- **Стадии** (`ERingStageKind`, фаза):
  - `Out` (`Walkout`) — старт боя и каждый раунд: оба в СВОИХ углах; через `GONG_DELAY` 0.25 с идут к точке встречи
    `MeetPoint(i)` = (∓DIST_START/2, 0) — ровно прежняя стартовая расстановка — со скоростью `WALK_SPEED` 1.8 м/с
    (≈ 2.1 с); удары/ноги/уклоны недоступны (`ApplyAction` → `false`), кнопка блока запоминается; дошли оба — бой.
  - `Rest` (`Between`) — гонг конца раунда: оба идут в свои углы лицом по ходу; дошёл — разворот к сопернику (в ринг).
    Отдых в углу (износ ×0.72, стамина +28% до потолка бака) набирается видимо за 3 с с гонга, `Proceed` доводит до итога.
  - `Neutral` (`Down`) — нокдаун: стоящий идёт в нейтральный угол — дальний от лежащего, а если путь туда через тело
    (`LyingBody` — 0.6 м за точкой падения, падает назад) — в другой; в пути обходит тело/лежащего (`PASS_CLEAR` 1 м).
    Лежащий — на месте, его курс замёрз на миг падения. Счёт идёт как раньше.
  - `Resume` (`Walkout`) — встал: стоящий из нейтрального угла идёт к вставшему на дистанцию падения
    (`ResumeGap` = clamp(дистанция, 0.9, 1.15)) по линии «вставший → он»; вставший ждёт; дошёл — «Бокс!».
  - На ходу друг мимо друга (выход/перерыв) — не ближе `WALK_CLEAR` 0.75 м. Досрочка на счёте — постановка снимается,
    стоящий остаётся, где был.
- **Часы честные.** Постановка — чистая ходьба: на `Out/Rest/Resume` не идут ни боевое время `T`, ни часы раунда, ни ИИ,
  ни стамина, ни ГСЧ; на `Neutral` `T` идёт (счёт рефери), часы раунда стоят (как и было). Поэтому **бой без нокдаунов
  с углами бит-в-бит совпадает с боем без углов** (харнесс: 362 из 362 боёв), а после нокдауна бой меняется только тем,
  где стоят бойцы.

### Поля снимка для UE-слоя (рефери, камера, угловые, HUD)

| Поле | Что |
|---|---|
| `S.Phase == EFightPhase::Walkout` | идут к бою (выход из углов / возврат после счёта); `Walkout` добавлен в КОНЕЦ enum (у `EBoxFightPhase` тоже) |
| `S.Stage.Kind` | `None` — идёт бой (или бой окончен), `Out` / `Rest` / `Neutral` / `Resume` |
| `S.Stage.T` | сек с начала стадии (реальное время постановки; первые 0.25 с — «гонг», стоят) |
| `S.Stage.bHasTarget[i]`, `TargetX/Z[i]` | куда идёт боец i (м ядра); без цели (лежащий, вставший на `Resume`) — `bHasTarget = false` |
| `S.Stage.bArrived[i]` | дошёл (без цели — всегда `true`); на `Rest` дошедший стоит ровно в своём углу |
| `S.bHasLyingBody`, `LyingX/Z` | центр тела лежащего (только `Phase == Down` с углами) — для обхода/места рефери |
| `F.WalkSpeed`, `F.WalkVelX/Z` | идёт по постановке: скорость (м/с, 0 — стоит) и вектор (оси ядра) — для Motion Matching |
| `F.Yaw/YawDegUE` | на ходу в угол (`Rest`/`Neutral`) — по ходу движения; к бою и стоя — на соперника; лежащий — замёрзший |
| `S.bCorners` | постановка включена |

Рефери (следующий спринт, порт `referee.ts`): нейтральный угол стоящего — `S.Stage.TargetX/Z[стоящий]` на `Neutral`,
путь — от его позиции к цели (стоять вне пути, `ROUTE_CLEAR` веба), `LyingX/Z` — тело; `BoxingStaging::NeutralFor` и
`CornerOf` можно звать и самому.

UE-слой (S-53): `ABoxerCharacter` берёт скорость точки на постановке из `WalkVel` (боевое время стоит — оценка по `T`
не работает) и доворачивает курс 420°/с (`bStaging`); `ABoxingFightGameMode` — `bCorners` (`-BoxNoCorners`),
`PreGongHold` 1 с до первого гонга (`-BoxPreGong=S`, ядро не шагает — сид тот же), лог `FIGHT STAGE`, скриншоты
стадий `-BoxStageShots` (`Docs/screens/<префикс>_stage_{corners,out,neutral_walk,neutral,resume,rest_walk,rest}.png`);
камера (`ABoxingFightPlayerController`) — вид постановки `stageCamMix` веба: пара шире 2.1 м — за спиной игрока и выше.

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
- **Постановка.** `S.Stage` (стадия, цели, кто дошёл), `F.WalkSpeed`/`WalkVelX/Z` — см. «Постановка раунда».
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

C++ при `RoundSeconds = 55`, `BreakSeconds = 0`, обоих `bAiControlled` и тике `1.f/60.f` должен дать те же числа — и
без углов (`bCorners = false`, плюс прежний хеш харнесса бит-в-бит: `417ec113 / af91feed / e7ad2b05`), и с углами
(те же попадания и тот же хеш событий/снимков боевого времени). Эталон снят с веба ДО S-35…S-46 (веб с тех пор ушёл:
текущий web на seed 42 даёт 56-27) — сверять с текущим вебом бой целиком нельзя; постановку сверяем по геометрии
(8 случаев `neutralFor/lyingBody/farNeutral/neutralPathClear`, сняты vite-node) и по выходу из углов (бой с тика 126 в
(∓0.575, 0) — как в вебе).

## Статус проверки

- Ядро собрано и запущено вне UE (заглушка `Tools/CoreHarness/CoreMinimal.h`): clang 18 и gcc 13 на Linux,
  `-std=c++20 -Wall -Wextra -Wshadow -Werror` — 0 предупреждений.
- Результат прогона (`Tools/CoreHarness/build.sh`): mulberry32(12345) совпадает с эталоном; детерминизм seed 42 — OK;
  200 сидов доиграны до конца (red 134, blue 66, ничьих 0, досрочных 0); паритет с TS по всем трём сидам — OK.
- CI: `.github/workflows/core-harness.yml` гоняет harness (clang + gcc) на каждое изменение ядра или harness.
- MSVC (`build.cmd`, /W4) — 0 предупреждений; модуль собирается в UE 5.7.
- **S-53 (постановка)**, `build.cmd` → `ИТОГ: OK`:
  - 200 сидов, 180 с, перерыв 60 с: с углами red 134 / blue 66, досрочек 0 (нокдаунов 4); без углов — 134 / 66 (нокдаунов 5).
  - Паритет (55 с, перерыв 0): без углов — прежние хеши бит-в-бит; с углами — те же попадания/итог/хеш боя; бой с тика 126.
  - Бой без нокдаунов, с углами == без углов бит-в-бит: 362 из 362 (2 пары × 55/180 с × 100 сидов; 38 боёв с нокдаунами
    отличаются — после счёта бойцы стоят иначе).
  - Панчер 92 vs «стеклянный» объёмник (подбородок 58), 200 сидов, 55 с, перерыв 3 с: стадий out 598, rest 398, neutral 40,
    resume 40; нарушений 0 (старт в углах, выход на точку встречи, перерыв — ровно в своём углу, нейтральный угол — цель,
    часы раунда стоят на постановке и счёте, шаг за тик ≤ WALK_SPEED·dt = 3 см — нет телепортов, путь в нейтральный угол
    не ближе допуска к телу, запас ≥ 0.53 м), детерминизм — OK. Исходы почти не меняются: победы панчера 143/200 и там, и там,
    досрочек 4 → 3, нокдаунов 43 → 40 (0.21 → 0.20 за бой) — в пределах шума (±0.06 на 200 боях).
  - `sim.exe findkd <раунд, с>` — сиды пары GameMode с нокдауном (для скриншотов: `-BoxSeed=60 -BoxRoundSec=55` —
    нокдаун на 13.4 с боя).
