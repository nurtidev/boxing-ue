# Анимации боя: пайплайн и ручная настройка AnimBP (S-41, трек C)

## Что где

| Что | Путь | В git? |
|---|---|---|
| Сырые импорты Mixamo (меш, скелет, 21 клип `A_MX_*`) | `/Game/BoxingLocal/Mixamo` | нет (лицензия Mixamo) |
| IK Rig Mixamo + ретаргетеры | `/Game/BoxingLocal/Mixamo/IK_MixamoBoxer`, `RTG_Mixamo_to_UEFN`, `RTG_Mixamo_to_Manny` | нет |
| Ретаргетнутые клипы `A_BX_*` (+ зеркальный `A_BX_slipL`) | `/Game/BoxingLocal/Retargeted/UEFN`, `/Game/BoxingLocal/Retargeted/Manny` | нет |
| Монтажи `AM_*` (скелет UEFN — логика GASP) | `/Game/BoxingLocal/Anim/AM_*` | нет: ссылаются на клипы Mixamo |
| Монтажи `AM_*` (скелет UE5 Manny) | `/Game/BoxingLocal/Anim/Manny/AM_*` | нет |
| AnimBP бойца/рефери/угловых — урезанная копия AnimBP GASP (S-69) | `/Game/Boxing/Anim/ABP_Boxer` | да (ссылается на GASP и `BoxingLocal/Anim/Loco`) |
| Копии chooser'ов баз, стейт-машины, паркура; `AC_BoxerPreCMCTick`, `AC_BoxerTraversalLogic` (S-69) | `/Game/Boxing/Anim/Loco` | да (1.9 МБ) |
| Копии нужных баз Motion Matching (17), их клипов (246), наборов нормализации, blend space'ов (S-69) | `/Game/BoxingLocal/Anim/Loco/{DB,Seq}` | нет (GASP, ~350 МБ) |
| Физреакция: пружины по костям | `/Game/Boxing/Anim/DT_HitReaction_PhysAnim` | да |
| Уровень для превью-рендера | `/Game/BoxingLocal/Tmp/L_AnimPreview` | нет |

Чтобы монтажи переехали в `/Game/Boxing/Anim` (если решим класть производные Mixamo в git):
`ANIM_MONTAGE_DIR=/Game/Boxing/Anim` перед запуском `anim_montages.py`.

**Какой скелет.** В GASP логика персонажа (`SandboxCharacter_CMC`) крутится на **SKM_UEFN_Mannequin** с
`SandboxCharacter_CMC_ABP` (Motion Matching), а видимый персонаж (Manny, Quinn, MetaHuman) копирует позу
рантайм-ретаргетом (`ABP_GenericRetarget` + `RTG_UEFN_to_*`). Поэтому **монтажи для боя — UEFN-набор**
(`/Game/BoxingLocal/Anim/AM_*`): их играет AnimBP GASP/`ABP_Boxer`, а Manny/Kellan получают позу сами.
Manny-набор (`.../Anim/Manny/AM_*`) — для «голого» Manny со своим AnimBP на `SK_Mannequin` (превью, тесты).

## Пайплайн (повторяемый, всё скриптами)

```
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64"
set P=C:\Users\user\Desktop\boxing-ue
rem 1. импорт FBX (коммандлет; Interchange выключается в скрипте — иначе падает без Slate)
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\anim_import.py -unattended -nosplash -nullrhi
rem 2. IK Rig + ретаргет (пакетному ретаргету нужен Slate -> полный редактор, скрипт закроет его сам)
%UE%\UnrealEditor.exe %P%\BoxingUE.uproject -ExecCmds="py %P%/Tools/EditorScripts/anim_retarget.py" -unattended -nosplash
rem 3. монтажи, зеркальный слип, кадры контакта -> Docs/ANIM_CONTACTS.md
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\anim_montages.py -unattended -nosplash -nullrhi
rem 4. копия AnimBP GASP -> /Game/Boxing/Anim/ABP_Boxer (повторный запуск не трогает ручные правки)
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\anim_abp.py -unattended -nosplash -nullrhi
rem 4б. S-69: урезать локомоцию (ABP_Boxer, BP_Boxer/BP_Referee/BP_CornerCrew) — нужен собранный модуль (UBoxerAssetTools)
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\anim_loco.py -unattended -nosplash -nullrhi
rem 5. таблица пружин физреакции
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\anim_hitreaction.py -unattended -nosplash -nullrhi
rem 6. превью: (а) редактор собирает уровень и позы, (б) игра снимает -> Docs/screens/anim_*.png
%UE%\UnrealEditor.exe %P%\BoxingUE.uproject -ExecCmds="py %P%/Tools/EditorScripts/anim_screens.py" -unattended -nosplash
%UE%\UnrealEditor.exe %P%\BoxingUE.uproject /Game/BoxingLocal/Tmp/L_AnimPreview -game -RenderOffscreen -ResX=960 -ResY=960 -unattended -nosound -ExecCmds="py %P%/Tools/EditorScripts/anim_screens.py"
```

Грабли, на которые наступили (чтобы не повторять):
- FBX через Interchange в коммандлете падает (`SlateApplication.h: CurrentApplication.IsValid()`), а
  `IKRetargetBatchOperation.duplicate_and_retarget` требует Slate всегда → шаг 2 только в полном редакторе.
- `-ExecutePythonScript` закрывает редактор сразу после скрипта (тики/таймеры не успевают) → `-ExecCmds="py …"`
  + скрипт сам вызывает выход через несколько секунд тиков (`anim_common.quit_when_idle`; мгновенный
  `quit_editor` после сохранения ронял редактор на ассерте кэша сжатия анимаций).
- В мире редактора, запущенного из скрипта, поза SkeletalMesh и SceneCapture не обновлялись → снимки в `-game`.
- Python не умеет менять слот у ассета-монтажа (SlotAnimTracks protected) → монтаж строится динамическим
  (`CreateSlotAnimationAsDynamicMontage`, любой слот) и дублируется в ассет; длина секции появляется после
  перезагрузки пакета (скрипт это делает).
- Python не умеет добавлять узлы в AnimGraph → слой верхней части тела сделан в C++ без графа (ниже, «Слой верха — в C++»).

## Слой верха — в C++ (S-41 feel, по умолчанию)

Ручной шаг ниже больше не нужен. `UBoxerLayerAnimInstance` (нативный AnimInstance, `Source/BoxingUE/Public/BoxerAnimInstances.h`)
ставится логическому мешу как ПОСТ-ПРОЦЕСС (`SetOverridePostProcessAnimBP`): вход — поза AnimBP GASP, свой слот `DefaultSlot`
этого экземпляра играет монтажи верха, смешивание от `spine_01` (поворот — в пространстве меша) — ровно то, что делал бы
`Layered blend per bone` из ручного шага. Узлы собраны в коде (`FAnimInstanceProxy::GetCustomRootNode`), ни ассетов, ни
правок скелета (`UpperBody` в Slot Manager не нужен — монтажи остаются в `DefaultSlot`). Подробно — [FIGHT_FEEL.md](FIGHT_FEEL.md).

## Было до S-41 feel (и с `-BoxFeel=0`): удары на всё тело через DefaultSlot

Ручной шаг ниже **отложен** (владелец его не делал). Поэтому `anim_montages.py` по умолчанию кладёт ВСЕ монтажи
(удары, блок, слипы, реакции, стойку `AM_Guard`) в существующий **`DefaultSlot`** — переменная окружения
`ANIM_UB_SLOT=UpperBody` вернёт раскладку «верх отдельно», когда слой появится. AnimClass логического меша бойца —
исходный `SandboxCharacter_CMC_ABP` GASP (`Tools/EditorScripts/fight_blueprints.py`, `FIGHT_ABP=` — назначить другой);
`ABP_Boxer` без ручного шага ничем не лучше. Боевую стойку держит C++ (`ABoxerCharacter`, `AM_Guard` в DefaultSlot, когда
боец почти стоит; на ходу — локомоция GASP). Минус полнотелого слота: во время удара/реакции ноги — из клипа (Root Lock),
шаг ядра в этот момент капсула всё равно делает → короткое скольжение ступней.

## Ручной шаг (НЕ НУЖЕН — заменён слоем в C++, оставлен для справки): слой UpperBody в ABP_Boxer

В AnimBP GASP есть ровно один слот — `DefaultSlot` (полнотелый, ближе к выходу графа). Его оставляем
как есть: через него идут полнотелые монтажи (`AM_Knockdown`, `AM_Knockout`, `AM_GetUp`, `AM_Victory`,
`AM_Defeat` — у них слот `DefaultSlot`, работают **без** ручного шага). Удары/блок/слипы/реакции
(`AM_Jab … AM_HitBody`, `AM_Guard`) — в слоте **`UpperBody`**, его надо добавить:

1. Открыть `Content/Boxing/Anim/ABP_Boxer` → вкладка **AnimGraph**.
2. Найти узел **Slot 'DefaultSlot'**. Отцепить провод, входящий в его пин **Source** (это поза локомоции
   Motion Matching). Перед узлом поставить **New Save Cached Pose** (имя `Loco`) и завести провод в него.
3. Добавить **Layered blend per bone**:
   - **Base Pose** ← `Use cached pose 'Loco'`;
   - **Blend Poses 0** ← узел **Slot** со слотом `UpperBody` (Details → Slot Name; если в списке нет —
     Window → **Anim Slot Manager** → группа `DefaultGroup` → **Add Slot** `UpperBody`); его **Source** ←
     `Use cached pose 'Loco'` (или поза стойки — см. п. 5);
   - Details узла: **Layer Setup → [0] → Branch Filters → +** : Bone Name `spine_01`, Blend Depth `0`;
     **Mesh Space Rotation Blend = true** (верх корпуса держит ориентацию клипа поверх поворотов таза),
     Mesh Space Scale Blend = false, Blend Weights 0 = 1.0.
4. Выход Layered blend → в **Source** узла `Slot 'DefaultSlot'`. Compile, Save.
   Итого: `Loco → [Layered: верх из UpperBody] → DefaultSlot (полнотелые поверх всего) → остальное GASP`.
5. **Боевая стойка вместо обычного idle** (выбрать одно):
   - (а) без правок графа: трек B на старте боя и после каждого удара играет `AM_Guard` (слот UpperBody,
     петля ×1000 ≈ 36 мин) — удар в том же слоте плавно перебивает стойку (blend 0.06 с), по окончании
     удара стойку перезапустить (`OnMontageBlendingOut` → `Montage_Play(AM_Guard)`);
   - (б) в графе (надёжнее, стойка и на ходу): **Source** слота `UpperBody` ← узел **Sequence Player**
     `/Game/BoxingLocal/Retargeted/UEFN/A_BX_guard` (Loop Animation = true) вместо `Loco`. Тогда верх тела
     ВСЕГДА в стойке, а монтажи UpperBody играют поверх неё и сами возвращаются в стойку.
     Минус: ABP_Boxer (в git) начнёт ссылаться на клип из BoxingLocal.
6. Назначить `ABP_Boxer` мешу логики бойца (UEFN-меш персонажа GASP: Mesh → Anim Class) — это делает
   трек B в своём Pawn/BP; видимый MetaHuman/Manny получает позу через `ABP_GenericRetarget`, как в GASP.

Добавление слота `UpperBody` в Anim Slot Manager правит скелет `SK_UEFN_Mannequin` (контент GASP, вне git;
изменение — одна строка в списке слотов). Если это нежелательно — вариант без новой записи в скелете:
положить удары в `DefaultSlot` (пересобрать `anim_montages.py`, заменив `UB = "UpperBody"` на
`"DefaultSlot"`), а в п. 3 Layered blend взять **существующий** `Slot 'DefaultSlot'` как Blend Pose 0 —
но тогда полнотелые монтажи (нокдаун) тоже станут «только верх».

## Как играть из C++ (для трека B)

```cpp
UAnimInstance* AI = Mesh->GetAnimInstance();               // меш логики (UEFN) с ABP_Boxer
const float T = Montage->GetPlayLength();
// контакт монтажа (Docs/ANIM_CONTACTS.md, нотифай "Contact") совпадает с резолюцией ядра:
const float Rate = ContactTime / CoreSecondsToContact;
AI->Montage_Play(Montage, Rate);
AI->OnPlayMontageNotifyBegin.AddDynamic(this, &AMyPawn::OnNotify); // NotifyName == "Contact" / "Peak" / "GuardUp"
```
Время контакта можно не хардкодить: пройти `Montage->Notifies`, найти `NotifyName == "Contact"` у
`UAnimNotify_PlayMontageNotify` и взять `GetTriggerTime()`.

## Качество ретаргета (Mixamo → UEFN/Manny)

- Сверено численно с исходником (позиции кистей/головы относительно таза, нормированы на рост): поза
  совпадает с точностью до пропорций — у манекенов руки короче (вынос джеба 73 см против 87 см у
  исходного человека), кисти чуть ближе к центру. Руки не выкручены, плечи и локти в норме.
- Стойка (`guard`): перчатки у подбородка, передняя — левая (ортодокс), стопы на полу, колени согнуты.
  Пальцы в полусжатом кулаке (как в клипе Mixamo; перчатки всё равно скроют).
- Клипы «на месте»: корень генерируется из таза цели, клипы на Force Root Lock — горизонтальный ход Mixamo
  (выпады, шаги назад в hitHead/knockdown) убран, позицию ведёт ядро. В слоте UpperBody таз/ноги всё равно
  от Motion Matching.
- Особенности самих клипов Mixamo (не ретаргета): `upperR` — апперкот **в корпус**; `slip` — глубокий
  нырок вправо со шагом; `knockdown`/`knockout` падают назад на ~1–1.4 м (в DefaultSlot с Root Lock падение
  будет на месте).
- Превью: `Docs/screens/anim_<поза>_{front34,side}.png` (Manny), `anim_uefn_*` (UEFN).

## S-69: боксёрская локомоция вместо полного GASP (холодный старт)

**Проблема.** Холодный «В бой» — 16–37 с, из них кусок предзагрузки «BP_Boxer» 15–37 с. Разведка (реестр ассетов, жёсткие
зависимости, `Saved/s69/probe*.py` — одноразовые): BP_Boxer тянул **2617 пакетов** (1241 AnimSequence, 154 базы PoseSearch,
282 текстуры, MetaHuman Kellan, Paragon, Echo…). Не только AnimBP:

| Цепочка | Пакетов (своих) | Время загрузки (коммандлет, по шагам) |
|---|---|---|
| `AC_VisualOverrideManager` → `GM_Sandbox` → `PC_Sandbox`, `SandboxCharacter_Mover` (+ его AnimBP и базы), Echo, Twinblast, Kellan, Manny/Quinn | ~1180 | **9.4 с** |
| `SandboxCharacter_CMC_ABP` → `CHT_PoseSearchDatabases` (Dense/Sparse/ExtremeSparse: 71 база) + `CHT_CMCCharacterAnimations` (стейт-машина, ~390 клипов) | ~440 | 5.8 с |
| `AC_TraversalLogic` → chooser'ы монтажей паркура | ~130 | 1.3 с |
| `AC_PreCMCTick` → **оригинальный** `SandboxCharacter_CMC` → всё выше | — | — |

Данные GASP перепутаны и сами: каждая база ссылается на набор нормализации `PSN_*_All` (все базы уровня, с прыжками и паркуром),
а клипы ходьбы через нотифаи стейт-машины — на базы `PSD_SM_CMC_*`, где клипы бега с `BP_NotifyState_EarlyTransition`, который
ссылается на **оригинальный** AnimBP со всеми chooser'ами. Поэтому «просто урезать chooser» не хватает: одна ссылка — и вся
сеть снова в памяти.

**Что боксу реально нужно** (`anim_loco_probe.py` — проба AnimBP в бою, автопилот 60 с, бойцы + рефери + угловые): Gait всегда
Walk (ядро шлёт WantsToWalk + WantsToStrafe), Stance Stand, MovementMode OnGround, RotationMode Strafe, `MMDatabaseLOD` 0
(Dense), Speed2D ≤ 2.4 м/с. Выбранные базы за бой: `PSD_Dense_Stand_Idles` (~75 %), `Walk_Loops` (~20 %), `Walk_Pivots`,
`TurnInPlace`, `Walk_SpinTransition` — те же, что до урезания (сид 12345: 2513/686/40/3 → 2521/688/42/5 снимков).

**Что сделано** (`Tools/EditorScripts/anim_loco.py`, повторяемо; оригиналы GASP не тронуты):

1. `ABP_Boxer` (прежняя полная копия AnimBP GASP из `anim_abp.py`, ручной шаг в ней так и не делался) стал урезанным: его chooser'ы
   → копии `CHT_BoxerMM(+_Dense/_Sparse/_ExtremeSparse)` — **строки и колонки те же** (интерфейс chooser'ов не менялся), ссылки на
   базы, до которых бокс не доходит (бег, спринт, присед, прыжки, приземления, паркур, слайды), обнулены; стейт-машина →
   `CHT_BoxerStateMachine` с обнулёнными клипами (режим выключен). Оставлены базы `Stand_Idles`, `Stand_TurnInPlace`,
   `Stand_Walk_{Starts,Loops,Pivots,Stops,SpinTransition}` и `Stand_Run_Stops` (строка «стойки» по скорости ≥ 1 м/с) —
   17 баз на трёх уровнях плотности вместо 71.
2. Эти 17 баз, их 246 клипов, наборы нормализации (`PSN_Boxer_*` — только оставленные базы) и blend space'ы AnimBP —
   копии в `/Game/BoxingLocal/Anim/Loco` (вне git, ~350 МБ); в копиях клипов ссылки на базы стейт-машины обнулены, пустые
   нотифаи `PoseSearchBranchIn` убраны (иначе ошибка в лог при сборке индекса).
3. `BP_Boxer`, `BP_Referee`, `BP_CornerCrew` (все — копии `SandboxCharacter_CMC`): компонент `AC_VisualOverrideManager` удалён
   (граф BP его не использует, облик ставит C++), `AC_PreCMCTick` → `AC_BoxerPreCMCTick` (владелец — `Character` вместо
   оригинального `SandboxCharacter_CMC`), `AC_TraversalLogic` → `AC_BoxerTraversalLogic` (chooser'ы монтажей паркура — пустые
   копии; прыжка у боксёра нет), AnimClass логического меша → `ABP_Boxer`.

Итог: BP_Boxer **2617 → 682 пакета** (AnimSequence 1241 → 247, базы 154 → 17), загрузка в коммандлете 13.3 → 2.0–2.6 с.

**C++ (точечно, только редакторные инструменты):** `UBoxerAssetTools` (`Source/BoxingUE/Public/BoxerAssetTools.h`) — Python
не умеет: перепривязать ссылки внутри одного ассета (`consolidate_assets` меняет их во всём проекте и удаляет оригинал),
читать скрытые свойства chooser'ов, сменить класс компонента BP, не порвав связи графа (удаление+добавление через
`SubobjectDataSubsystem` роняет компиляцию: AddDelegate теряет Target), перенести связи «осиротевшего» контакта узла
`Evaluate Chooser` (вход контекста назван по классу AnimBP) и дождаться сжатия свежих копий клипов (иначе сохранение копии
базы падает на ассерте `bEnforceCompressedDataSampling`). В игре функции ничего не делают; рантайм боя и ядро не тронуты.

**Грабли:**
* Каталог общий: пока чужой `-game` держит ассеты, сохранение падает (`MoveFile … Error Code 32`). Скрипт сохраняет только
  изменённое; при сбое — просто перезапустить (идемпотентен).
* Новый персонаж из копии `SandboxCharacter_CMC` (как `feel_crew_bp.py`) вернёт всю загрузку GASP — после его скрипта прогнать
  `anim_loco.py` (путь — в `BPS` или `LOCO_EXTRA_BPS`). Проверка: `LOCO …: ссылок на оригиналы GASP: 0` в логе.
* Первый запуск на другой машине строит индексы 17 баз-копий и сжимает 246 клипов-копий в DDC (однократно, ~10 с).
* Отладка: `LOCO_SKIP_BPS=1` — только ассеты анимации (BP, которыми пользуются другие роли, не трогаются).

**Качество ног — не хуже** (A/B на одном сиде 12345, автопилот 75 с, `-BoxFootLog -BoxFootLock=0`, т.е. чистый Motion
Matching без фиксации S-70):

| Скольжение ступни в опоре, медиана (ср.), см/с | До (полный GASP) | После |
|---|---|---|
| красный, боковой ход | 126.9 (140.8) | 133.3 (144.6) |
| красный, прочее | 52.4 (97.6) | 50.9 (95.1) |
| синий, боковой ход | 131.7 (151.5) | 131.6 (147.7) |
| синий, прочее | 86.4 (121.6) | 86.5 (121.5) |

Шагов ядра 77/66 в обоих прогонах; кадры `s69_before_34.png` / `s69_after_34.png`, `_58` (камера сбоку на ноги красного) —
стойка и ступни совпадают. Бот `-BoxBot=average` 65 с — бой идёт, ошибок PoseSearch нет, 90 FPS при 1280×720 (`s69_bot_55.png`).
