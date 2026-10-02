# Облик бойцов и рефери (пайплайн tech-artist)

Тело и лицо — MetaHuman Kellan (GASP), одежда и экипировка — своя геометрия из Blender на скелете
`metahuman_base_skel`. Визуальные BP — копии `BP_Kellan`: поза приходит рантайм-ретаргетом (`ABP_GenericRetarget`)
с логического манекена GASP, поэтому любой облик работает со всеми клипами боя и Motion Matching без своих анимаций.
Всё собирается скриптами, повторяемо.

## Что где лежит

| Путь | Что | git |
|---|---|---|
| `/Game/Boxing/Characters/Meshes/SKM_BoxerGloves` | перчатки профи (шнуровка + лента), web `glove.glb` | да |
| `/Game/Boxing/Characters/Meshes/SKM_BoxerGloves_Amateur` | перчатки любителя (липучка + белая «мишень», без шнуровки) | да |
| `/Game/Boxing/Characters/Meshes/SKM_BoxerHeadgear` | шлем (web `headgear.glb`, вариант M, подогнан под голову Kellan) | да |
| `/Game/Boxing/Characters/Materials/M_BoxerKit` + `MI_*` | материалы формы: цвет, шероховатость, блик по контуру | да |
| `/Game/BoxingLocal/Characters/SKM_BoxerBody` | тело боксёра профи: без головы (её даёт лицо), кистей, стоп | нет (производная Epic) |
| `/Game/BoxingLocal/Characters/SKM_BoxerBody_Amateur` | то же, но без кожи под майкой (не проходит сквозь майку) | нет |
| `/Game/BoxingLocal/Characters/SKM_BoxerKit` | трусы + боксёрки | нет |
| `/Game/BoxingLocal/Characters/SKM_BoxerVest` / `SKM_BoxerVestHeadgear` | майка / майка + шлем одним мешем | нет |
| `/Game/BoxingLocal/Characters/SKM_Ref*` | рефери: `RefBody_Am/_Pro` (кожа рук + нитрил), `RefShirt_Am/_Pro`, `RefTrousers`, `RefShoes` | нет |
| `/Game/BoxingLocal/Characters/MI_Skin_*`, `MI_Hair_Ref_*` | тон кожи (синий угол, рефери), седина рефери | нет |
| `/Game/BoxingLocal/Tmp/L_RingRefLook` | копия ринга с двумя рефери — только для снимков | нет |

## Классы облика (для GameMode)

Пути классов для `VisualOverridePathRed/Blue` (`FSoftClassPath`) и `-BoxVisualRed=/-BoxVisualBlue=`:

| Облик | Красный угол | Синий угол |
|---|---|---|
| Профи (голый торс, трусы, шнуровка) | `/Game/BoxingLocal/Characters/BP_BoxerLook_Red.BP_BoxerLook_Red_C` | `/Game/BoxingLocal/Characters/BP_BoxerLook_Blue.BP_BoxerLook_Blue_C` |
| Любители со шлемом (женщины-элита, юниоры и юноши) | `/Game/BoxingLocal/Characters/BP_BoxerLook_Red_Amateur.BP_BoxerLook_Red_Amateur_C` | `/Game/BoxingLocal/Characters/BP_BoxerLook_Blue_Amateur.BP_BoxerLook_Blue_Amateur_C` |
| Любители без шлема (мужчины-элита) | `/Game/BoxingLocal/Characters/BP_BoxerLook_Red_AmateurElite.BP_BoxerLook_Red_AmateurElite_C` | `/Game/BoxingLocal/Characters/BP_BoxerLook_Blue_AmateurElite.BP_BoxerLook_Blue_AmateurElite_C` |

Рефери: `/Game/BoxingLocal/Characters/BP_RefereeLook_Amateur.BP_RefereeLook_Amateur_C` и
`/Game/BoxingLocal/Characters/BP_RefereeLook_Pro.BP_RefereeLook_Pro_C`.

Как выбирать (web `look.ts` `needsHeadgear`): любительский бой (3 раунда) → `_Amateur`, если боец — женщина или
моложе 19 лет, иначе `_AmateurElite`; профи → без суффикса. Шлем у элиты-мужчин отменён правилами World Boxing.
Весь контент `BoxingLocal` вне git: если класса на машине нет, GameMode берёт фолбэк (`VisualOverridePath`, Kellan).
Поэтому сначала пробовать класс облика, затем профи-облик угла, затем Kellan.

## Слоты BP

Все облики — копии `BP_Kellan` с теми же компонентами (Body, Face, Torso, Legs, Feet, грумы Hair/Eyebrows/
Eyelashes/Mustache/Beard/Fuzz, LODSync). Меши раскладываются по слотам так:

| Облик | Body | Torso | Legs | Feet | Грумы |
|---|---|---|---|---|---|
| профи | SKM_BoxerBody | SKM_BoxerGloves | SKM_BoxerKit | — | синий угол без волос |
| любитель со шлемом | SKM_BoxerBody_Amateur | SKM_BoxerGloves_Amateur | SKM_BoxerKit | SKM_BoxerVestHeadgear | Hair = нет (под шлемом) |
| любитель без шлема | SKM_BoxerBody_Amateur | SKM_BoxerGloves_Amateur | SKM_BoxerKit | SKM_BoxerVest | как у профи угла |
| рефери | SKM_RefBody_Am/_Pro | SKM_RefShirt_Am/_Pro | SKM_RefTrousers | SKM_RefShoes | седина: MI_Hair_Ref_* |

У BP только четыре слота скин-мешей, поэтому майка и шлем у любителя склеены в один меш. Шлем отдельно тоже есть
(`SKM_BoxerHeadgear`, вес 1.0 на кости `head`) — его можно навесить кодом как ещё один `SkeletalMeshComponent`
с leader pose на Body, если понадобится шлем без майки.

## Пайплайн

```
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64"
set BL="C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
set P=C:\Users\user\Desktop\boxing-ue
rem 1. UE: выгрузка мешей Kellan и мокап-тела в Saved/LookWork (полный редактор, закроется сам)
%UE%\UnrealEditor.exe %P%\BoxingUE.uproject -ExecCmds="py %P%/Tools/EditorScripts/look_export.py" -unattended -nosplash
rem 2. Blender: форма профи (S-41) и одежда рефери/любителей (S-56)
%BL% -b --factory-startup --python %P%\Tools\Blender\look_kit.py
%BL% -b --factory-startup --python %P%\Tools\Blender\look_outfits.py
rem 3. UE: импорт и BP (коммандлет)
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\look_build.py -unattended -nosplash -nullrhi
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\look_build_outfits.py -unattended -nosplash -nullrhi
rem 4. снимки
rem   бойцы в бою, галерея поз (look_shots.py, см. шапку):
%UE%\UnrealEditor.exe %P%\BoxingUE.uproject /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -windowed -ResX=1600 -ResY=900 -unattended -nosound -BoxAutopilot -BoxSeed=7 -BoxQuitAfter=400 -BoxNoCorners -BoxFeel=0 -BoxVisualRed=/Game/BoxingLocal/Characters/BP_BoxerLook_Red_Amateur.BP_BoxerLook_Red_Amateur_C -BoxVisualBlue=/Game/BoxingLocal/Characters/BP_BoxerLook_Blue_Amateur.BP_BoxerLook_Blue_Amateur_C -ExecCmds="DisableAllScreenMessages,py %P%/Tools/EditorScripts/look_shots.py"
rem     (окружение: LOOK_PREFIX=look2_am LOOK_CHEST=1 LOOK_HUD=0 LOOK_EVENTS=0 LOOK_POSES=Guard:0.5,Jab:0.5,...)
rem   рефери: (а) коммандлет строит L_RingRefLook, (б) игра снимает
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\look_referee_shots.py -unattended -nosplash -nullrhi
%UE%\UnrealEditor.exe %P%\BoxingUE.uproject /Game/BoxingLocal/Tmp/L_RingRefLook -game -RenderOffscreen -windowed -ResX=1600 -ResY=900 -unattended -nosound -BoxAutopilot -BoxSeed=7 -BoxQuitAfter=200 -ExecCmds="DisableAllScreenMessages,py %P%/Tools/EditorScripts/look_referee_shots.py"
```

Разведка: `look_inspect.py` (компоненты/меши BP_Kellan), `look_inspect_outfits.py` (грумы и параметры волос),
`look_refpose.py` (поза привязки наших мешей против тела Kellan).

### Как строится одежда (Tools/Blender)

- Исходник — полное тело `m_med_nrw_body_mocap` (у тела Kellan торс вырезан под худи) в A-позе привязки.
- **Оболочка ткани** (`region_shell`): участок кожи → ровные разрезы плоскостями → отбор граней → срезание «зубцов»
  края и сглаживание кромки (не дальше 1.5 см) → сглаживание поверхности → возврат на кожу (сглаживание
  «усаживает» внутрь) → отступ по нормали → толщина внутрь с кромкой.
- **Драпировка** (`loosen_torso`): рубашка и футболка рефери не обтягивают мышцы — каждый горизонтальный срез торса
  тянется к своей выпуклой оболочке (ложбинка груди, пресс, позвоночник заполнены).
- **Брючины** ниже колена — прямые трубы вокруг оси ноги (а не лосины по икрам); пояс и низ — ровные.
- **Туфли** — выпуклая оболочка стопы + воксельный ремеш, подошва — ровной полосой.
- **Веса** одежды — с кожи (`DATA_TRANSFER`, ближайшая грань), ≤ 8 влияний; затем `push_out` — наружная стенка не
  ближе 4–10 мм к коже тела и шеи лица Kellan.
- **Кожа под одеждой удаляется** (рефери: остаются только руки за рукавом; любитель: торс под майкой с запасом 3 см
  от краёв) — ни один клип не протолкнёт кожу сквозь ткань, и меньше треугольников.
- **Нитриловые перчатки** — отдельный материал на коже кистей до ровной манжеты (3.5 см выше запястья).
- **Бабочка** — свой меш на кости `neck_01`, по фронту стойки воротника.
- **Шлем** (`make_headgear`): как web `headgear.ts` — центр и RMS-радиус «жёсткой» головы Kellan (вершины с весом
  head + FACIAL_*) против эталона `fit_c/fit_r` из GLB, затем радиальная подгонка по живой голове (72×36
  направлений: внутренняя стенка на 4 мм над кожей, сглажено), под подбородком подгонка гаснет; всё, что осталось
  под кожей (ремень), выталкивается по нормали. Контроль в логе: `min signed gap` ≥ 0, `verts under skin 0`.

### Грабли

- Шапки/шлемы: голова MetaHuman по форме не та, что у MakeHuman, — одного масштаба мало (зазоры у висков, ремень
  в подбородке), нужна радиальная подгонка; лучи по шее давали ложную «кожу» — берутся только грани головы.
- Волосы (грум) под шлемом прорастают — у облика со шлемом `Hair` = нет.
- Скрипт сборки НЕ пересохраняет уже существующие `MI_*` в git-папке: их держит запущенная игра/редактор другой роли
  (`MoveFile` → «Error saving»). Сменить цвета — `LOOK_OUT_FORCE=1`.
- Из Git Bash пути `/Game/...` в аргументах превращаются в пути Windows — запускать с `MSYS_NO_PATHCONV=1`.
- `BoxerCharacter` держит ChildActor «VisualOverride», процедурный слой game-feel цепляется к визуалу при старте боя:
  для галереи поз надёжнее `-BoxNoCorners -BoxFeel=0`.

## Рефери (S-56): облик и что нужно для поведения

Облик: Kellan с другим тоном кожи (`LOOK_REF_SKIN`, между красным и синим углом), седина волос/щетины/бровей
(`WhiteAmount`), профи — светло-голубая рубашка с длинным рукавом, стойка воротника, чёрная бабочка, чёрные брюки,
чёрные туфли с блеском; любители — белые футболка с коротким рукавом и брюки, белые туфли. Нитриловые перчатки —
сиреневато-синие (`#6f7fd0`, как в web). Своего лица нет — Kellan (своё лицо MetaHuman — после auto-rig, роадмап №5).

**GASP-клипы на рефери работают:** проверено в игре — `SandboxCharacter_CMC` с `VisualOverride` = `BP_RefereeLook_*`
стоит в idle Motion Matching, видимый меш крутит `ABP_GenericRetarget` (снимки `look2_ref_*`). Ходьба/шаги вбок —
тот же Motion Matching, если вести персонажа CharacterMovement'ом (как бойцов на постановке).

Для поведения в следующем спринте (web `referee.ts`/`Referee.tsx`):
- **Актор**: копия `SandboxCharacter_CMC` (или `BoxerCharacter` без ядра) + `VisualOverride` = облик рефери по типу
  боя; движение — `AddMovementInput`/`MoveTo` к точке `RefFrame` (сбоку от пары, обход бойцов).
- **Кости для наведения** (Control Rig / `AnimBP` слоем поверх MM, как наведение кулака у бойцов): `head`,
  `neck_01/02` (взгляд на пару/сбитого), `clavicle_*`, `upperarm_*`, `lowerarm_*`, `hand_*` (указать угол, развести,
  поднять руку победителю), пальцы `index_01..03_*`, `thumb_01..03_*`, `middle/ring/pinky_01..03_*` (счёт 1..5,
  указательный жест). Сокеты не нужны — кости MetaHuman есть в BP (Body); если удобнее — `hand_r`/`hand_l`
  с сокетами `Point_*` добавить в скелет можно скриптом.
- **Анимации**: стойка и ходьба — GASP (уже работает). Нужны жесты: «бокс!» (развести руки), указать в угол, счёт
  (взмах на каждый счёт), «стоп» (развести руками — досрочка), поднять руку победителю (обе руки — ничья), «брейк».
  Их проще делать процедурно (как в web: наведение костей на направления), чем искать клипы; клипы Mixamo
  ретаргетятся пайплайном `anim_retarget.py` на UEFN-манекен и играются монтажами на логическом меше.
- **Параметр C++ (не трогал)**: GameMode нужен способ выбрать облик рефери и бойцов по типу боя — например
  `FSoftClassPath RefereeVisualPathAmateur/Pro` и выбор `…_Amateur`/`…_AmateurElite`/профи для бойцов
  (см. «Классы облика»).

## Бюджет (треугольники LOD0, слоты материалов)

| Меш | треуг. | материалы |
|---|---|---|
| SKM_BoxerVest | 4.4 тыс. | 1 |
| SKM_BoxerVestHeadgear (майка + шлем) | 13.7 тыс. | 5 |
| SKM_BoxerHeadgear | 9.4 тыс. | 4 |
| SKM_BoxerGloves_Amateur | 7.5 тыс. | 5 |
| SKM_BoxerBody_Amateur | 5.3 тыс. | 1 |
| SKM_RefShirt_Am / _Pro | 8.9 / 12.4 тыс. | 1 / 3 |
| SKM_RefBody_Am / _Pro | 7.0 / 5.3 тыс. | 2 |
| SKM_RefTrousers | 7.4 тыс. | 2 |
| SKM_RefShoes | 5.6 тыс. | 2 |

Рефери ≈ 33–38 тыс. треугольников без лица (лицо Kellan — как у бойцов), 8–10 секций материалов. Для мобильных
кандидаты на прореживание: шлем (у web 3.2 тыс.), перчатки, рубашка профи.
