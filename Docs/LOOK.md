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

## S-60: облик по данным бойца (рост, телосложение, кожа, волосы, борода, женская фигура)

Лицо у всех — Kellan (своих лиц MetaHuman без auto-rig нет, см. «Что возможно» ниже), но бойцы теперь различаются
ростом, телосложением, тоном кожи, причёской/бородой, у женщин — фигурой, топом и хвостом/пучком. Галерея:
`Docs/screens/look3_*` (после), `look3_before_*` (до, те же ракурсы), бой высокий × низкий — `look3_fight_*`.

### Данные: `Content/Boxing/Data/Appearance.json` (в git)

`Tools/EditorScripts/look_appearance.py` — обычный Python 3 без UE (например, Python из Blender):
`"C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe" Tools\EditorScripts\look_appearance.py`.
Источник — web `data/appearance/*.ts` (все 522 бойца ростера есть в таблицах веба, 211 — по фото); кого нет в
таблицах (будущие сгенерированные/карьера) — порт `generateAppearance` веба бит-в-бит (FNV-1a + mulberry32 от
`look:<имя>`, сверено с web на node). Ключ записи — `id` ростера (`amateur:Имя`), как `FRosterBoxer::Id`.
Каждая запись: `appearance` (как в вебе: skin 1..6, hairStyle, hairColor, facialHair, tattoos, confidence, source) и
**`look` — готовые значения для рантайма** (C++ ничего не вычисляет):

| поле `look` | что | как применить |
|---|---|---|
| `scale` | рост / 175.3 см (рост модели), 0.84..1.20 | `VisualChild->SetRelativeScale3D(FVector(scale))` |
| `morph` {Heavy, Lean, Muscular} | телосложение (web `bodyMorph` по ИМТ) | `Body->SetMorphTarget(Name, W)` — только на ведущем Body: Legs/Torso/Feet (leader pose) берут морфы по имени сами |
| `female` | женская фигура | `Body->SetMorphTarget("Female", 1)` |
| `skinFaceTex` {слот: текстура} | тон кожи лица (слоты `head_LOD1/3/57_shader_shader`) | `Face->GetMaterialIndex(слот)` → MID → `SetTextureParameterValue("BaseColor", T)` |
| `skinBodyTex` {"Skin": текстура} | тон кожи тела | то же на Body, слот `Skin` |
| `skinCC` [r,g,b] | индивидуальный разброс тона (±7 %) | в те же MID: `SetVectorParameterValue("BaseColor_ColorCorrect", (r,g,b,1))` |
| `hair` {groom, binding, attach, length, melanin, redness, white, dye?} | причёска | компонент `Hair`, см. «Грумы» |
| `brows` {melanin, redness, white} | цвет бровей | MID на всех слотах `Eyebrows` |
| `facial` {groom, binding, attach, stubble{groom, binding}, melanin…} | борода (компонент `Beard`) + щетина Kellan (`Mustache`) | см. «Грумы» |
| `tattoos` | тату (пока не рисуются) | — |

Корень файла: `groomAttach` {socket `FACIAL_C_FacialRoot`, loc, quat} — крепление грумов (то же, что у грумов Kellan
в BP_Kellan).

**Грумы.** `groom == null` → `SetGroomAsset(nullptr)`, `SetVisibility(false)`. Иначе:
1. `AttachToComponent(Face, KeepRelative, groomAttach.socket)` + `SetRelativeTransform(FTransform(quat, loc))` — и для своих
   грумов (`attach` задан, привязки к коже нет), и для грумов Kellan (волны, щетина: это их родное крепление);
2. `SetBindingAsset(binding или nullptr)`, затем `SetGroomAsset(groom)`, `SetVisibility(true)`;
3. только `Hair`: `SetHairLengthScaleEnable(true)`, `SetHairLengthScale(length)`;
4. цвет: на каждый слот — если не MID, `SetMaterial(i, MI_Hair Kellan)` (`/Game/MetaHumans/Kellan/Materials/MI_Hair`, и для
   бороды тоже), затем MID: `hairMelanin`, `hairRedness`, `WhiteAmount`, при `dye` — `hairDye`. Щетина (`Mustache`) — те же
   скаляры на её MID (материал свой, не менять);
5. облик со шлемом (`*_Amateur`) — `Hair` не включать (под шлемом).

**Эталонная реализация** — `Tools/EditorScripts/look_apply.py::apply(visual_actor, look, child_comp)` (Python в игре; по
ней сняты все галереи и бой). C++-порт — построчно. Предлагаемое API: `UFUNCTION(BlueprintCallable) static void
UBoxerLookLibrary::ApplyBoxerLook(AActor* Visual, UChildActorComponent* Child, const FBoxerLook& Look)`; загрузка
`Appearance.json` в `UBoxingGameInstanceSubsystem` рядом с Roster.json (FBoxerLook — поля таблицы выше); вызов из
`ABoxerCharacter` сразу после того, как `VisualChild->GetChildActor()` появился. Масштаб и морфы ретаргет, наведение кулака
и упор не ломают (бой 206 см × 158 см, `look3_fight_*`). В `FBoxerPreset` нужен `Id` бойца (сейчас только `Name`) — или
искать запись по имени.
Ещё C++: в `ABoxingFightGameMode::VisualPathFor` профи-женщине (`bFemale && Rounds > 3`) — класс
`/Game/BoxingLocal/Characters/BP_BoxerLook_<Red|Blue>_Female.BP_BoxerLook_<Red|Blue>_Female_C` (топ в цвет угла + тело без
кожи под ним), фолбэк — профи угла. В любителях женщины уже идут в `_Amateur` (майка + шлем).
Старые сборки облика: если в слоте `Feet` меш `m_med_nrw_shs_*` (кроссовки GASP) — `SetVisibility(false)` (в новых сборках
Feet скрыт в BP).

### Ассеты и пайплайн

| Путь | Что | Скрипт |
|---|---|---|
| морфы `Heavy/Lean/Muscular/Female` на `SKM_BoxerBody(_Amateur/_Top)`, `SKM_BoxerKit`, `SKM_BoxerVest(Headgear)`, `SKM_BoxerTop` | телосложение и женская фигура; одежда морфится вместе с кожей | `Tools/Blender/look_morphs.py` (зовут look_kit / look_outfits перед экспортом) |
| `SKM_BoxerTop`, `SKM_BoxerBody_Top`, `BP_BoxerLook_<угол>_Female` | топ профи-женщины, тело без кожи под ним | look_outfits (`make_vest(bot=TOP_BOT)`), look_build_outfits |
| `/Game/BoxingLocal/Characters/Skin/T_SkinFace_LOD{1,3,5}_T<1..6>`, `T_SkinBody_T<1..6>` | кожа по тонам: перекраска текстур Kellan (цель тона на шве шеи, пятна сжаты) | `look_skin.py` (export) → `Tools/Blender/look_skin_tones.py` → `look_skin.py` (import) |
| `/Game/BoxingLocal/Characters/Grooms/GR_{Short,Buzz,Medium,Ponytail,Bun,Beard,FullBeard,Goatee,Mustache}` | свои грумы (кривые волос по скальпу/низу лица Kellan) | `Tools/Blender/look_hair.py` → `look_groom_import.py` (полный редактор, `-EnablePlugins=AlembicHairImporter`) |
| `Content/Boxing/Data/Appearance.json` | облик 522 бойцов | `look_appearance.py` |
| `/Game/BoxingLocal/Tmp/L_LookGallery` | галерея (копия ринга, 10 персонажей GASP) | `look_gallery.py` |

Полная пересборка (после look_export.py), по порядку:
1. Blender: `look_kit.py`; `OUTFITS_ONLY=am` + `look_outfits.py`.
2. UE-коммандлет: `look_build.py`; `LOOK_OUT_ONLY=am` + `look_build_outfits.py`.
3. Кожа: `LOOK_SKIN_STEP=export` + `look_skin.py` (коммандлет) → Blender `look_skin_tones.py` → `LOOK_SKIN_STEP=import` + `look_skin.py`.
4. Грумы: Blender `look_hair.py` → `UnrealEditor.exe <uproject> -EnablePlugins=AlembicHairImporter -ExecCmds="py Tools/EditorScripts/look_groom_import.py" -unattended -nosplash`.
5. `look_appearance.py`.
6. Галерея: коммандлет `look_gallery.py` (уровень), затем игра `/Game/BoxingLocal/Tmp/L_LookGallery -game ... -ExecCmds="DisableAllScreenMessages,py Tools/EditorScripts/look_gallery.py"`
   (`LOOK_FIGHTERS="id;id;…"`, `LOOK_PREFIX`, `LOOK_APPLY=0` — «до», `LOOK_FEET=1`, отладка `LOOK_DEBUG`, `LOOK_DEBUG_IDENT`, `LOOK_APPLY_SKIP`).
   Бой с обликом: `look_shots.py` + `LOOK_RED_ID` / `LOOK_BLUE_ID` и `-BoxPickRed=` / `-BoxPickBlue=`.

### Грабли S-60
- Материал формы `M_BoxerKit` обязан иметь `used_with_morph_targets` — без флага в игре серый материал по умолчанию.
- Повторный FBX-импорт берёт настройки из `asset_import_data` меша: флаг морфов ставится и там (look_build*).
- Грум из Blender: кривые — `POLY` (кубические в Alembic ломают импорт UE: «knots exceed»); ширина пряди в Alembic не
  масштабируется `global_scale`; оси экспорта — `(x, −z, −y)` (меш лица повёрнут на −90° к актёру; замер
  `LOOK_DEBUG_IDENT`). Привязка грума к коже лица (`GroomBindingAsset`) строится 10–30 мин на грум (все LOD лица) — не
  нужна: лицо не анимируется, грум жёстко на сокете `FACIAL_C_FacialRoot`.
- Кожа: множитель `BaseColor_ColorCorrect` ×4–10 к тёмной текстуре Kellan даёт «грязь» на коленях и полосу на шее —
  поэтому свои текстуры по тонам.
- В слоте Feet копий BP_Kellan в игре оставались кроссовки GASP (подошва/шнурки торчали из боксёрок) — скрыты.
- Боксёрки: носок — выпуклая оболочка + сглаживание (были «таби» с пальцами).

### Что возможно / невозможно (честно)
- **Свои лица без входа в Epic — нет.** Плагин MetaHuman Creator (MetaHumanCharacter) в UE 5.7 установлен, но в проекте
  выключен; сборка персонажа (`BuildMetaHuman`) требует риг лица (`CanBuildMetaHuman`: «Character is not rigged» →
  облачный auto-rig) и текстур высокого разрешения (облачная загрузка); локальный синтез кожи требует модели
  `Content/Optional/TextureSynthesis` — её в установке нет (ставится лаунчером). Пресетов лиц в плагине нет — только
  архетип `SKM_Face`; в проекте голов, кроме Kellan, нет (Content/MetaHumans: Common + Kellan).
- Возможный следующий шаг без облака: морфы лица на копии `Kellan_FaceMesh` (разрез глаз, нос, губы; женское лицо) —
  геометрия в Blender, DNA/RigLogic остаются. В S-60 не делал (риск для рига лица, нужна отдельная задача).
- Тату не рисуются. Длинные распущенные, косички, дреды сводятся к хвосту / волнам Kellan. Брови — грум Kellan (только цвет).
- Женщины: фигура морфом на мужском теле + топ/хвост; лицо мужское (Kellan) — вблизи читается.

### Бюджет
Грумы (пряди/точки): Short 26k/208k, Buzz 30k/150k, Medium 24k/288k, Ponytail 57k/373k, Bun 56k/364k, бороды 2.5–12k
(uasset 0.2–4.2 МБ, всего 17 МБ). Текстуры кожи — 24 шт. (1024², LOD5 512²; uasset 20 МБ, в сборке BC1 ≈ 12 МБ). Морфы:
+4 ключа на 6 мешах. Для мобильных грумы прядями дороги — нужны карточки (Cards LOD), отдельная работа.
