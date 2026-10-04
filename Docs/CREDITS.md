# Авторы и лицензии импортированных ассетов

Правила — [ASSETS.md](ASSETS.md). Каждый импорт — строка здесь. CC-BY — ещё и в титрах игры.

## Текстуры материалов (S-77)

Сырьё — `Saved/AssetCache/` (вне git); в игре — `/Game/Boxing/Materials/Textures/T_*_NR`, `T_Folds_N`
(переупакованы `Tools/Blender/mat_textures.py`).

| Наш ассет | Источник | Автор | Лицензия | Где применено |
|---|---|---|---|---|
| T_Leather_NR | [Leather Red 03](https://polyhaven.com/a/leather_red_03), Poly Haven | Rob Tuytel | CC0 1.0 | перчатки, шлем, ремешки, туфли рефери/угловых, канаты, сиденье стула |
| T_LeatherCreased_NR | [Leather Red 02](https://polyhaven.com/a/leather_red_02), Poly Haven | Rob Tuytel | CC0 1.0 | боксёрки, подушки углов ринга |
| T_Satin_NR | [Crepe Satin](https://polyhaven.com/a/crepe_satin), Poly Haven | colormass, Rico Cilliers | CC0 1.0 | трусы, бабочка рефери |
| T_Jersey_NR | [Cotton Jersey](https://polyhaven.com/a/cotton_jersey), Poly Haven | colormass, Rico Cilliers | CC0 1.0 | футболки угловых, форма рефери-любителя |
| T_Poplin_NR | [Stretch Poplin](https://polyhaven.com/a/stretch_poplin), Poly Haven | colormass, Rico Cilliers | CC0 1.0 | рубашка и брюки рефери-профи |
| T_Fleece_NR | [Jogging Melange](https://polyhaven.com/a/jogging_melange), Poly Haven | colormass, Rico Cilliers | CC0 1.0 | олимпийки и брюки угловых |
| T_Terry_NR | [Terry Cloth](https://polyhaven.com/a/terry_cloth), Poly Haven | colormass, Rico Cilliers | CC0 1.0 | полотенце катмена |
| T_Canvas_NR | [Rough Linen](https://polyhaven.com/a/rough_linen), Poly Haven | colormass, Rico Cilliers | CC0 1.0 | канвас и юбка ринга |
| T_SportMesh_NR | [Fabric 070](https://ambientcg.com/a/Fabric070), ambientCG | ambientCG (Lennart Demes) | CC0 1.0 | майка любителя, кроссовки рефери |
| T_Folds_N | [Denim Fabric 02](https://polyhaven.com/a/denmin_fabric_02), Poly Haven | Rob Tuytel | CC0 1.0 | складки ткани (размыто) |

## Публика (S-77)

`/Game/Boxing/Environment/Crowd/SM_Crowd_*` — собраны `Tools/Blender/crowd_people.py` из системных ассетов MakeHuman
([makehuman_system_assets_cc0.zip](https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip)):
базовый меш hm08, одежда `male_casualsuit01…05`, `female_casualsuit01/02`, `female_sportsuit01`, обувь `shoes01…06`,
кожа `young_asian_male/female` (только яркость текстуры → «деталь» в цвете вершин), глаза `low-poly`, скелет
`game_engine` MPFB (только для позы, в меш не входит). Правообладатели на момент выпуска в CC0 (сентябрь 2020):
Data Collection AB, Joel Palmius, Jonas Hauquier — **CC0 1.0**. Инструмент — MPFB 2 (GPLv3; на модели не
распространяется). Палитра `T_CrowdPalette` и материалы — свои.

## Ранее импортированное (до S-77, сводка)

| Что | Источник | Лицензия | Подробно |
|---|---|---|---|
| Тела/лица MetaHuman (Kellan), Game Animation Sample (Motion Matching, манекены) | Epic Games (Fab / UE) | UE EULA — только в продукте на Unreal Engine | вне git (`Content/MetaHumans`, GASP) |
| Боксёрские клипы | Mixamo (Adobe) | лицензия Mixamo (коммерция, без распространения сырых файлов) | сырые FBX вне git; [ANIM_SETUP.md](ANIM_SETUP.md) |
| Звуки боя и зала | Freesound и др. (через web/public/audio) | CC0 | [Content/Boxing/Audio/CREDITS.md](../Content/Boxing/Audio/CREDITS.md) |
| Перчатки, шлем, форма, одежда рефери/угловых | своя геометрия (Blender-скрипты Tools/Blender) по телу MetaHuman | свои / производные Epic (`BoxingLocal` вне git) | [LOOK.md](LOOK.md) |
| Шрифт надписей зала `F_ArenaText` | растр (distance field) из системного Arial Bold Windows | **риск для релиза**: право встраивать растр системного Arial (Monotype) в игру не проверено — надёжнее заменить на OFL-шрифт с кириллицей (Roboto, Inter, PT Sans) | `build_ring.py` `build_font` |
