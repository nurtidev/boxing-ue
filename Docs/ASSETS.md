# Свободные ассеты: откуда брать и как заводить (S-77, tech-artist)

**Решение владельца:** игра — вымышленные бойцы, цель — Steam (ПК), коммерческий релиз. Берём только то, что
разрешает коммерческое использование и модификацию: **CC0**, **CC-BY** (с атрибуцией), **Mixamo** (лицензия Adobe),
**контент Epic для UE** (только в продукте на Unreal Engine), **Fab** — по лицензии конкретного листинга.
**Нельзя:** CC-BY-**NC** (некоммерческое), CC-BY-**ND** (без изменений — а мы всегда меняем: ретоп, перепривязка,
перекраска), «Personal / Editorial», ассеты без явной лицензии, рипы из игр/ТВ/YouTube, логотипы и форма реальных
организаций и брендов, лица реальных людей.

Всё, что импортировано, — строкой в [CREDITS.md](CREDITS.md) (источник, автор, лицензия, ссылка, где лежит у нас).
CC-BY — ещё и в титрах игры (экран «Авторы», задача UI при подготовке к релизу).

## Каталог источников

| Источник | Лицензия | Аккаунт | Что нам полезно | Атрибуция |
|---|---|---|---|---|
| [Poly Haven](https://polyhaven.com) | CC0 (все ассеты) | не нужен | PBR-сканы ткани и кожи (кожа перчаток, атлас, трикотаж, поплин, махра, лён/канвас, деним), металл, HDRI | не обязательна (пишем в CREDITS) |
| [ambientCG](https://ambientcg.com) | CC0 (все ассеты) | не нужен | PBR: ткань (спортивная сетка `Fabric070`, стёганая `Fabric048`), кожа `Leather0xx`, металл `Metal0xx`, ковролин, бетон | не обязательна (пишем в CREDITS) |
| MakeHuman system assets + [MPFB](https://static.makehumancommunity.org/mpfb.html) | ассеты CC0 (с 2020); MPFB — GPLv3 (инструмент, на результат не распространяется) | не нужен | люди любого телосложения/пола/возраста, одежда (casual/sport/work), обувь, причёски, кожа — публика, угловые/судьи/статисты, база для своих мешей | не обязательна (пишем в CREDITS) |
| [Mixamo](https://www.mixamo.com) | лицензия Adobe: персонажи и анимации бесплатно в коммерческих проектах, без распространения сырых файлов | **Adobe ID (владелец)** | анимации боя (уже используем), персонажи для публики/статистов | не требуется; сырые FBX в git не кладём |
| [Sketchfab](https://sketchfab.com) | у каждой модели своя: берём только **Downloadable + CC0 или CC-BY 4.0** | **аккаунт (владелец)** для скачивания | перчатки, шлемы, шорты, лапы, мешки/груши, гонг, стулья, ринговое оборудование | CC-BY: автор, название, ссылка, лицензия — в CREDITS и титрах |
| [Fab](https://www.fab.com) (Epic) | по листингу: Standard License (коммерция, без распространения исходников), CC-BY, CC0; **Personal — нельзя** | **Epic ID (владелец)** | окружение, реквизит, Megascans, MetaHuman/GASP (уже стоят) | по листингу; Epic-контент — только в продукте на UE |
| Контент Epic в UE (GASP, MetaHuman, Starter Content) | UE EULA: только в продуктах на Unreal Engine | Epic ID | тела/лица, Motion Matching (уже используем) | не требуется |
| [Quaternius](https://quaternius.com), [Kenney](https://kenney.nl) | CC0 | не нужен | лёгкие стилизованные персонажи/реквизит (запасной вариант дальних рядов) | не обязательна |
| [Freesound](https://freesound.org) | у звука своя: берём только CC0 (CC-BY — с атрибуцией) | для скачивания — аккаунт | звуки (уже используем CC0, см. Content/Boxing/Audio/CREDITS.md) | по лицензии |

Проверка перед импортом: страница ассета, поле License (у Sketchfab — под моделью, у Fab — «License» листинга), дата;
скрин/ссылку — в CREDITS. Сомнение — не берём.

## Как скачать без браузера (то, что работает без аккаунта)

```
# Poly Haven: список файлов ассета (JSON) → прямые ссылки на карты (1k/2k/4k, jpg/png/exr)
curl https://api.polyhaven.com/files/leather_red_03
curl -O https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/leather_red_03/leather_red_03_nor_dx_1k.jpg
#   поиск: https://api.polyhaven.com/assets?t=textures (категории, теги), сведения/авторы: https://api.polyhaven.com/info/<id>
# ambientCG: zip с картами (NormalDX/NormalGL/Roughness/Color/AO/Displacement)
curl -L -o Fabric070_1K-JPG.zip "https://ambientcg.com/get?file=Fabric070_1K-JPG.zip"
#   поиск: https://ambientcg.com/api/v2/full_json?type=Material&q=leather&limit=50
# MakeHuman: системные ассеты (280 МБ, CC0)
curl -L -O https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip
#   распаковать в %APPDATA%\Blender Foundation\Blender\5.2\extensions\.user\blender_org\mpfb\data\
```

Сырьё кладём в `Saved/AssetCache/<источник>/<id>/` (вне git), в git — только то, что прошло наш пайплайн
(uasset в `Content/Boxing`), плюс скрипты, которые повторяют путь от сырья до uasset.
Mixamo/Sketchfab/Fab требуют входа владельца: скачанные им файлы — туда же, в `Saved/AssetCache`, дальше — скриптами.

## Как это заходит в наш пайплайн

### Текстуры ткани/кожи (Poly Haven, ambientCG) → фактура наших MI

1. `Tools/Blender/mat_textures.py` (Blender, фон): из карт нормали (DirectX) и шероховатости собирает **одну**
   текстуру на материал `T_<Имя>_NR` (RGBA 8 бит): RG — нормаль, нормированная по силе (p95 |xy| = 0.35 — тонкая кожа
   иначе теряется в 8 битах), B — шероховатость, нормированная к средней скана (0.5 = средняя, ±0.5 = ±2σ). Плюс
   `T_Folds_N` — «складки» (нормаль мятого денима, размытая до волн без переплетения). Один семпл вместо двух.
2. `Tools/EditorScripts/look_materials.py` (коммандлет): импорт в `/Game/Boxing/Materials/Textures` (BC7 линейные),
   мастера `M_BoxerKit` (форма/одежда/стул) и `M_BoxingBase` (статика ринга/зала) получают статический
   переключатель **Detail** (выкл. по умолчанию — остальные MI не дорожают): трипланарная выборка (у формы — по
   **предскинновой** позиции/нормали: фактура «приклеена» к ткани и не плывёт в анимации, не зависит от UV наших
   оболочек; у ринга — мировые координаты), параметры `DetailTex`, `DetailSize` (см на тайл), `NormalStrength`,
   `RoughVar`, `FoldTex`, `FoldSize`, `FoldStrength`. Таблица «MI → текстура и параметры» — в начале скрипта.
3. Новый материал из скана: добавить строку в `SETS` (mat_textures.py) и в `TEXTURES`/`KIT`/`RING` (look_materials.py).

### Люди MakeHuman → публика (и статисты)

`Tools/Blender/crowd_people.py` (MPFB): тело + одежда + обувь MakeHuman, поза — запечённая (сидя на ступени / стоя),
кожа под одеждой удалена, ~5 тыс. треугольников (Decimate), один материал. Окраска — не текстурами, а зонами в
цвете вершин (кожа / верх / низ / волосы / обувь + яркость текстуры MakeHuman как «деталь»): один меш даёт сотни
разных людей через палитру × `PerInstanceRandom`. Поза «болеют» (руки вверх) запечена смещением вершин в UV0/UV1 —
материал смешивает её World Position Offset'ом по `MPC_Crowd.Excite` (вертекс-анимация без скелета и без текстур).
Импорт — `Tools/EditorScripts/ring_crowd_import.py` (Nanite, полноточные UV), расстановка — `build_ring.py`.
Подробно и бюджет — [PERF.md](PERF.md) («S-77: живой зал»), облик — [LOOK.md](LOOK.md).

### Модели со Sketchfab / Fab (перчатки, шлем, лапы, мешок) — когда владелец скачает

glTF/FBX → `Saved/AssetCache/sketchfab/<id>/` → Blender: масштаб (см), ретоп/Decimate под бюджет (перчатка ≤ 8 тыс.
треуг.), UV, привязка к скелету `metahuman_base_skel` (как `look_kit.py`: вес 1.0 на `hand_l/r` / `head`, подгонка по
живой голове/кисти) → FBX → `look_build*.py`. Материалы — наши (`M_BoxerKit` + Detail), чужие текстуры — только если
лицензия та же. Атрибуцию CC-BY — в CREDITS сразу при скачивании.

### Персонажи Mixamo (публика)

Требуют входа Adobe (владелец): Mixamo → Characters → Download (FBX Binary, T-pose, без скина не нужно) → в
`Saved/AssetCache/mixamo/`. Дальше тот же путь, что у MakeHuman: Blender — поза, Decimate, зоны цвета → FBX
`SM_Crowd_<имя>` → `ring_crowd_import.py` (добавить имя в `VARIANTS`) и `build_ring.py` (`CROWD_SIT`/`CROWD_STAND`).
Сейчас публика собрана из MakeHuman: аккаунт не нужен, лицензия чище (CC0), телосложение и одежда настраиваются.
