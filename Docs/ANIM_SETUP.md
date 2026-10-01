# Анимации боя: пайплайн и ручная настройка AnimBP (S-41, трек C)

## Что где

| Что | Путь | В git? |
|---|---|---|
| Сырые импорты Mixamo (меш, скелет, 21 клип `A_MX_*`) | `/Game/BoxingLocal/Mixamo` | нет (лицензия Mixamo) |
| IK Rig Mixamo + ретаргетеры | `/Game/BoxingLocal/Mixamo/IK_MixamoBoxer`, `RTG_Mixamo_to_UEFN`, `RTG_Mixamo_to_Manny` | нет |
| Ретаргетнутые клипы `A_BX_*` (+ зеркальный `A_BX_slipL`) | `/Game/BoxingLocal/Retargeted/UEFN`, `/Game/BoxingLocal/Retargeted/Manny` | нет |
| Монтажи `AM_*` (скелет UEFN — логика GASP) | `/Game/BoxingLocal/Anim/AM_*` | нет: ссылаются на клипы Mixamo |
| Монтажи `AM_*` (скелет UE5 Manny) | `/Game/BoxingLocal/Anim/Manny/AM_*` | нет |
| AnimBP бойца (копия AnimBP GASP) | `/Game/Boxing/Anim/ABP_Boxer` | да (ссылается только на контент GASP) |
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
- Python не умеет добавлять узлы в AnimGraph → слой верхней части тела — руками (ниже).

## Ручной шаг: слой UpperBody в ABP_Boxer (~5 минут)

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
