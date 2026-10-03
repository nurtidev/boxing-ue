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
| `/Game/BoxingLocal/Characters/Grooms/GR_{Short,Buzz,Medium,Ponytail,Bun,Cornrows,Braids,Beard,FullBeard,Goatee,Mustache}` | свои грумы (кривые волос по скальпу/низу лица Kellan) | `Tools/Blender/look_hair.py` → `look_groom_import.py` (полный редактор, `-EnablePlugins=AlembicHairImporter`) |
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
- Тату не рисуются. Длинные распущенные сводятся к хвосту; косички — колоски (S-64, ниже); мужские дреды — к «средним» (прямые) /
  колоскам (курчавые). Брови — грум Kellan (только цвет).
- Женщины: фигура морфом на мужском теле + топ/хвост; лицо мужское (Kellan) — вблизи читается.

### Бюджет
Грумы (пряди/точки): Cornrows 14k/~290k, Braids 15k/~370k (S-64), Short 26k/208k, Buzz 30k/150k, Medium 24k/288k, Ponytail 57k/373k, Bun 56k/364k, бороды 2.5–12k
(uasset 0.2–4.2 МБ, всего 17 МБ). Текстуры кожи — 24 шт. (1024², LOD5 512²; uasset 20 МБ, в сборке BC1 ≈ 12 МБ). Морфы:
+4 ключа на 6 мешах. Для мобильных грумы прядями дороги — нужны карточки (Cards LOD), отдельная работа.

## S-64: косички (braids / cornrows) и правило «без лысых женщин»

Было: стиль `braids` у курчавых (тон 5–6) сводился к волнам Kellan `Hair_S_360Waves` с `HairLengthScale` 0.8 — на голове
почти ничего, Шилдс выглядела лысой; у Маршалл (тон 2) тот же `braids` давал хвост.

Новые грумы (`Tools/Blender/look_hair.py`, процедурно по скальпу лица Kellan, как прошлые):
- **`GR_Cornrows`** — 19 рядов-колосков от линии роста волос (азимут ±122° от середины лба, над ухом — выше уха) к линии
  роста на затылке. Ряд — дуга по коже в плоскости «старт — цель — точка под центром головы» (средние ряды идут через
  макушку, боковые — над ухом назад), каждый ряд — плетёная коса из трёх прядей (34 волокна на прядь, период 1.1 см,
  ширина 1.55 см, к концу тоньше); под рядами — короткий прижатый подшёрсток (проборы издали темнеют, а не светят кожей).
- **`GR_Braids`** — те же ряды, сходящиеся в узел над шеей (`TIE_LOW`), + свисающая коса 24 см (3 пряди × 260 волокон,
  период 3.4 см) и узел. Коса жёстко на голове (как хвост `GR_Ponytail`): при сильных поворотах может касаться спины.

Правило в `look_appearance.py`:

| стиль | мужчины (прямые / курчавые) | женщины (прямые / курчавые) |
|---|---|---|
| `braids` | Cornrows / Cornrows | **Braids / Braids** |
| `dreads` | Medium / Cornrows | Braids / Braids |
| `afro` | Short / волны Kellan | Short / **Bun** (пучок) |
| `short` | Short / волны | Short / **Cornrows** |
| `buzz` | Buzz / волны 0.38 | Buzz / волны **полной длины** (0.72–1.0, не «под ноль») |
| `bald` | нет | Cornrows (в ростере таких нет) |

Итог по ростеру (522): женщин с `GR_Braids` — 17 (Шилдс, Маршалл, Баумгарднер, Дюбуа, Фундора, Грин …), мужчин с
`GR_Cornrows` — 4 (Уайлдер, Дэвис, Ярд, Форд); женщин без грума или с волнами < 1 — 0.
Пересборка: `LOOK_HAIR_ONLY=GR_Cornrows,GR_Braids` + `look_hair.py` → `LOOK_GROOM_ONLY=GR_Cornrows,GR_Braids` +
`look_groom_import.py` → `look_appearance.py`. Снимки: `Docs/screens/look4_*`.

## S-68: угловые (тренер и катмен) и стул углового — контент для game-feel

Порт web `CornerCrew.tsx` / `crew.ts` (вид, роли, места). В этом спринте — только модели, облик и точки в уровне;
поведение (переход пол → апрон, наклон к бойцу, бутылка, стул, сидячая поза бойца) — game-feel, следующий спринт.
Снимки: `Docs/screens/crew_fight_*` (бой: угловые на полу у помоста), `crew_rest_*` (перерыв: на апроне, стул в углу),
`crew_rest_game_stage_*` (игровая камера перерыва, `-BoxStageShots`), `crew_perf_{base,fight,rest}_30` (один кадр боя
сид 60 без угловых / с угловыми — «до/после» с одного ракурса).

### Что получилось

| | тренер (`BP_CornerCoach_<угол>`) | катмен (`BP_Cutman_<угол>`) |
|---|---|---|
| верх | олимпийка в цвет угла (темнее: `#7a0c12` / `#0d2c78`), стойка воротника, белая молния и манжеты | футболка в цвет угла (`#b5121b` / `#1442b0`), белая бейка ворота |
| полотенце / бутылка | — | полотенце на левом плече (махра, один меш с футболкой); бутылка 0.5 л в левой руке (кузов светлый, крышка в цвет угла) |
| руки | кожа (запястья и кисти) | предплечья + чёрный нитрил на кистях (катмен работает с рассечениями) |
| низ | тёмные брюки, тёмные кеды со светлой подошвой (меш туфель рефери) | то же |
| облик | Appearance-механика: тон кожи, причёска, цвет, борода/щетина, **седина у ~40 % тренеров**, рост, телосложение (тренер плотнее, Heavy) | то же, без седины, Muscular 0.2 |

Стул: `BP_CornerStool_Red/_Blue` (StaticMeshActor, `SM_CornerStool`: круглое сиденье Ø40 см в цвет угла, 4 хромированные
ноги, кольцо-подножка; опора — центр на полу, верх сиденья 52 см; без коллизии). Отдельно `SM_CrewBottle` (та же бутылка
статикой, опора — дно, ось +Z) — если бутылку надо передать бойцу/поставить на апрон (у катмена в руке она частью меша).

### Облик под бойца (детерминированно, как web)

`look_appearance.py` пишет в тот же `Content/Boxing/Data/Appearance.json` (массив `boxers`) записи угловых для КАЖДОГО бойца
ростера: `id` = `crew:<id бойца>:coach` и `crew:<id бойца>:cutman` (1044 записи, `crewCount`), плюс облик по умолчанию
на угол (боец не из ростера — карьера): `crew:Red:coach`, `crew:Red:cutman`, `crew:Blue:coach`, `crew:Blue:cutman`.
Формула — web `buildPerson` бит-в-бит: `generateAppearance("<имя бойца>:<trainer|cutman>", пол бойца, флаг бойца)`
(угловые — земляки), `hash01` того же ключа: тренер с h < 0.4 — седой (`white` 0.65 волосы, 0.7 борода, 0.35 брови),
рост (М 1.72 / Ж 1.62) + 0.12·h, вес (рост − 100) × (тренер 1.12 / катмен 1.0) → морфы, масштаб. Формат записи — тот же
`look`, что у бойцов (+ поле `look.crew` {role, grey, heightCm, weightKg} для справки), поэтому **C++ ничего нового не
читает**:

```cpp
FBoxerLook L;
if (UBoxerLookLibrary::FindLook(FString::Printf(TEXT("crew:%s:coach"), *FighterId), FString(), L)
    || UBoxerLookLibrary::FindLook(FString::Printf(TEXT("crew:%s:coach"), Corner == 0 ? TEXT("Red") : TEXT("Blue")), FString(), L))
{
    UBoxerLookLibrary::ApplyBoxerLook(CrewVisual, CrewVisualChild, L, /*bHeadgear*/ false);
}
```

Эталон на Python (так сняты все `crew_*`): `look_crew_shots.apply_looks` → `look_apply.apply(child_actor, rec["look"], child_comp)`.
Морфы Heavy/Lean/Muscular/Female есть на всех мешах угловых (куртка, футболка с полотенцем, брюки, руки) — плотный
тренер и женщины-угловые (морф Female) работают тем же `ApplyBoxerLook`. Женского лица нет (Kellan), как у бойцов.
Без вызова облик — запечённый в BP (записи `crew:<угол>:<роль>`: кожа тона, волосы, седина).

### Классы и ассеты

| Путь | Что | git |
|---|---|---|
| `/Game/BoxingLocal/Characters/BP_CornerCoach_Red` / `_Blue` | тренер (копия BP_Kellan: ретаргет GASP) | нет |
| `/Game/BoxingLocal/Characters/BP_Cutman_Red` / `_Blue` | катмен | нет |
| `/Game/BoxingLocal/Characters/SKM_CrewJacket`, `SKM_CrewTeeTowel`, `SKM_CrewTrousers`, `SKM_CrewBody_Coach`, `SKM_CrewBody_Cutman` | одежда и видимая кожа (производные тела MetaHuman) | нет |
| `/Game/Boxing/Characters/BP_CornerStool_Red` / `_Blue`, `Meshes/SM_CornerStool`, `Meshes/SM_CrewBottle` | стул, бутылка (своя геометрия) | да |
| `/Game/Boxing/Characters/Materials/MI_Crew_*`, `MI_Stool_*` | материалы (`M_BoxerKit`) | да |
| `/Game/BoxingLocal/Tmp/L_RingCrewLook` | копия ринга с угловыми и стульями — только для снимков/замера | нет |

Пути классов: `/Game/BoxingLocal/Characters/BP_CornerCoach_Red.BP_CornerCoach_Red_C` (и `_Blue`, `BP_Cutman_*`),
`/Game/Boxing/Characters/BP_CornerStool_Red.BP_CornerStool_Red_C`. Слоты BP: Body — кожа рук (катмен: + нитрил +
бутылка), Torso — олимпийка / футболка+полотенце, Legs — брюки, Feet — `SKM_RefShoes`. `BoxingLocal` вне git — если класса
нет, угловых просто не ставить (фолбэка не нужно).

Как ставить (так сделан `L_RingCrewLook`, `look_crew_shots.py` → `build_level`): персонаж GASP (`SandboxCharacter_CMC` или
своя копия без ввода) + ChildActorComponent «VisualOverride» = класс облика; логический манекен не рисуется
(`Mesh->SetVisibility(false)`, `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`) — как `ABoxerCharacter`.
Idle Motion Matching на них работает (снимки), рост — масштаб ChildActorComponent (`look.scale`, делает `ApplyBoxerLook`).

### Маркеры в `L_Ring` (`build_ring.py`, папка Markers, TargetPoint)

Точка = **ступни** (Z — пол под ними; центр капсулы GASP = Z + полувысота × масштаб), курс (yaw) — лицом к стулу своего угла.
Теги: `CornerCrew`, `<Red|Blue>`, `<Coach|Cutman>`, `<Fight|Rest>`, полное имя последним.

| маркер | X, Y, Z (см) | что |
|---|---|---|
| `CornerCrew_Red_Coach_Fight` | −407, −270, −110 | бой: тренер на полу арены у помоста (сторона −X угла) |
| `CornerCrew_Red_Cutman_Fight` | −340, −407, −110 | бой: катмен на полу (сторона −Y, у ступеней угла, с угловой стороны) |
| `CornerCrew_Red_Coach_Rest` | −335, −208, 0 | перерыв: тренер на апроне за канатами |
| `CornerCrew_Red_Cutman_Rest` | −208, −335, 0 | перерыв: катмен на апроне за канатами |
| `CornerStool_Red` (теги `CornerStool`, `Red`, `In`) | −263, −263, 0 | стул под бойцом в углу (= `Corner_Red`, CORNER_SPOT) |
| `CornerStool_Red_Stow` (`Stow`) | −343, −343, 0 | откуда стул въезжает / куда убирается: апрон за столбом |

Синий угол — те же числа с плюсом (`CornerCrew_Blue_*`, `CornerStool_Blue[_Stow]`). Формулы — web `crew.ts`: бой — край
помоста + 0.42 м наружу, 0.35 м от линии канатов вдоль стороны; перерыв — 0.30 м за канатами, 0.55 м от стула к середине
стороны. Ступени угла (`Ring_Step_<угол>_*`) — на стороне ±Y у угла: катмен поднимается по ним (x ≈ ∓250, y ∓381…∓477).

### Что нужно для поведения (game-feel)

- **Переход бой ↔ перерыв** (web `CrewMood.up`, `crewPlace`): горизонталь — smoothstep, вверх — с опережением (`ey = min(1,
  1.6·u)`), вниз — наоборот; скорость 0.9/с вверх, 1.1/с вниз. Катмен может честно идти CharacterMovement по ступеням (высота
  ступени 27.5 см — проверить MaxStepHeight CMC, не мерил); тренеру со стороны ±X ступеней нет — как в web, вести по кривой `crewPlace`
  (капсула без коллизии с помостом / `MOVE_Flying` на время перехода).
- **Стул** (web `stool`): виден в перерыве, когда угловые наверху (`up > 0.6`) или боец сел; въезжает от `_Stow` к
  `CornerStool_<угол>` за ~0.25 с (step 4/с) со «вырастанием» по Z 0.6 → 1; высота сиденья под бойца — масштаб по Z
  (web `seatH`: верх сиденья ≈ высота колена бойца; меш 52 см при масштабе 1).
- **Боец садится** — кости MetaHuman боксёра (не угловых): `pelvis` (опустить на высоту сиденья), `thigh_l/r` вперёд ~1.42 рад,
  `calf_l/r` назад ~1.5, колени врозь 0.2, `spine_01..05` откинуть 0.1 на подушку угла, `upperarm_l/r` назад 0.75 и вниз 0.32
  на верхний канат (132 см), `lowerarm_*` свисают 0.45 (web `sitPose.ts`: процедурно поверх клипов; IK ног на время сидения
  гасить). Клипа «сидеть» в GASP нет.
- **Тренер наклоняется к бойцу** (web `lean`): `spine_03..05` вперёд-вниз к голове сидящего (~0.5 рад), `neck_01/02`, `head`
  смотрят на лицо бойца; руки — на верхний канат или к плечам бойца (`hand_l/r` IK, через канаты).
- **Катмен протягивает бутылку**: бутылка — часть меша `SKM_CrewBody_Cutman`, жёстко на кости **`hand_l`** (в кулаке вдоль
  большого пальца): IK левой руки (`upperarm_l` → `lowerarm_l` → `hand_l`) к точке у рта бойца (`head` + 10 см вперёд, −8 см).
  Отдать/поставить бутылку — `SM_CrewBottle` на сокете/кости рукой (`AttachToComponent(Body, "hand_r")`), а запечённую —
  спрятать нельзя (часть меша): если нужно, сделаю вариант тела без бутылки.
- **Полотенце** — часть меша футболки, веса с кожи (`clavicle_l`, `spine_05`, `neck_01`) — едет с плечом. Махать/вытирать
  им нельзя (не отдельный меш); если нужно — отдельный скин-меш полотенца на `hand_l/r` (сделаю).
- **Реакции на бой** (web `crewReaction`): «да!» (кулак вверх: `upperarm_r` вверх, `lowerarm_r` согнут) и тревога (руки к
  голове/канатам) — процедурно, как жесты рефери (Docs/LOOK.md «Рефери»: кости `clavicle_*`, `upperarm_*`, `lowerarm_*`, `hand_*`).
- **Клипы GASP**: стойка (idle Motion Matching) и ходьба/поворот на месте — работают сами, если вести персонажа
  CharacterMovement (проверено на снимках: idle на полу и на апроне). Жестов (наклон, протянуть, «да!») в GASP нет — процедурно
  поверх MM (Control Rig/AnimBP-слой, как наведение кулака у бойцов) или клипы Mixamo через `anim_retarget.py` + монтаж.
- Камера перерыва (web `restShot`): из ринга на угол — `стул + n·2.9 м + вбок 0.5 м`, высота 1.62, взгляд на 0.88 (снимки
  `crew_rest_<угол>_corner` сняты так).

### Пайплайн

```
rem 1. Blender: одежда, полотенце, бутылка, стул (вход — Saved/LookWork/body_full.fbx, face.fbx из look_export.py)
%BL% -b --factory-startup --python %P%\Tools\Blender\look_crew.py
rem 2. облик угловых в Appearance.json (Python без UE)
"C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe" %P%\Tools\EditorScripts\look_appearance.py
rem 3. UE: импорт, материалы, BP угловых и стула (коммандлет)
%UE%\UnrealEditor-Cmd.exe %P%\BoxingUE.uproject -run=pythonscript -script=%P%\Tools\EditorScripts\look_build_crew.py -unattended -nosplash -nullrhi
rem 4. маркеры — build_ring.py (пересобирает L_Ring); копия ринга с угловыми — look_crew_shots.py коммандлетом
rem 5. снимки: игра на /Game/BoxingLocal/Tmp/L_RingCrewLook + -ExecCmds="DisableAllScreenMessages,py .../look_crew_shots.py"
rem    (CREW_MODE=fight|rest, CREW_RED_ID / CREW_BLUE_ID — облик под бойцов, CREW_GAME="18" — кадры игровой камерой;
rem     перерыв игровой камерой — -BoxRoundSec=15 -BoxStageShots -BoxShotPrefix=crew_rest_game)
```

Одежда — те же функции, что у рефери (`look_outfits.py`: `region_shell`, `transfer_weights`, …). В `region_shell` добавлены
необязательные `pre` (разметка материалов по ровным разрезам ДО сглаживания — молния/манжеты) и `even` (у тонких клиньев после
разрезов `use_even_offset` солидифая стрелял шипом на 70 см); по умолчанию поведение для рефери/формы прежнее.

### Грабли

- `push_out` к открытому мешу лица/брюк без ограничения дальности: для далёкой вершины ближайшая точка даёт огромный
  «s < gap» — шип через полсцены. У угловых — `push_out_near` (только кожа ближе 6 см) + контроль `worst_gap` в логе.
- `materials.clear()` в `region_shell` обнуляет индексы граней — разметку несёт атрибут грани `zone`.
- Полотенце по коже (оболочкой) ползло по шее к уху и рвалось — теперь сетка лучами по телу без шеи (выше 1.485 м не видит).
- Лампас по сетке тела и белый кант воротника рвались зубцами — убраны (воротник в цвет куртки).
- Сохранение `L_Ring` при запущенной чужой игре падает (`Failed to move … to temp directory`) — пересобирать, когда игр нет.

### Бюджет (LOD0)

| меш | треуг. | материалы |
|---|---|---|
| SKM_CrewJacket | 14.8 тыс. | 3 |
| SKM_CrewTeeTowel (футболка 8.2 + полотенце 1.0) | 9.2 тыс. | 3 |
| SKM_CrewTrousers | 7.4 тыс. | 2 |
| SKM_CrewBody_Coach / _Cutman (с бутылкой 0.3) | 5.3 / 7.3 тыс. | 1 / 4 |
| SKM_RefShoes | 5.6 тыс. | 2 |
| SM_CornerStool / SM_CrewBottle | 0.7 / 0.3 тыс. | 2 / 2 |

Угловой ≈ 33–35 тыс. треугольников без лица + лицо Kellan и грумы (как рефери). Сцена с четырьмя угловыми: +160…185 draw
calls, +260 тыс. примитивов, GPU +1.1…1.3 мс, игровой поток +1.2 мс — FPS 62–63 при 100 % (Docs/PERF.md «S-68»).
Размеры uasset: SKM_Crew* 0.85–2.7 МБ (8.4 МБ всего, вне git), BP угловых по 0.36 МБ; в git — стул/бутылка 44 КБ, BP стула
25 КБ ×2, 17 MI. Для мобильных: угловых — LOD1+ и без теней (они мелкие в кадре), полотенце/бутылка не прореживаются.
