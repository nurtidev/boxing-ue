# Оболочка игры: меню, Выставка, HUD, итог (S-55)

Порт `web/src/App.tsx` (Выставка) и HUD интерактива на UMG. Все экраны — **C++-виджеты без WBP-ассетов**
(`UUserWidget`, дерево собирается в `NativeOnInitialized` через `WidgetTree->ConstructWidget`): их не нужно
открывать в GUI-редакторе, а сборка и правка — обычный C++.

## Поток

`L_Menu` (стартовая карта, `GameDefaultMap`) → главный экран → «Выставка» → выбор пары и раундов → «В бой!» →
`L_Ring` (бой с выбранными статами) → экран итога → «Реванш» (та же пара, новый сид) / «В меню».
«Быстрый бой» — случайная равная по весу и уровню пара любителей.

## Данные ростера

* `Tools/RosterExport/export.mjs` — Node-скрипт: бандлит `entry.ts` esbuild'ом из `node_modules` веба (алиас
  `@web` → `boxing/web/src`), **импортирует движок web как есть** (`Boxer.fromData`, `buildProRoster`, `buildLegends`,
  `fightProfile`) и пишет `Content/Boxing/Data/Roster.json`. Формулы вывода статов не копируются — правка баланса в
  web + перезапуск скрипта = новые статы в UE.
  ```
  node Tools/RosterExport/export.mjs                      # web рядом: ..\boxing\web
  node Tools/RosterExport/export.mjs --web <путь к web> --out <файл>
  ```
  Нужен `npm install` в web (esbuild). Node 24 подходит; если упрётся в версию — `npx -y -p node@22.23.2 node …`.
* Поля строки: `id` (`<kind>:<имя>`), `name`, `kind` (amateur/pro/legend), `gender`, `countryCode` (ISO из
  эмодзи-флага — шрифты UE эмодзи не рисуют), `country`, `city`, `age`, `weightKg`, `division`, `heightCm`, `reachCm`,
  `stance`, `style`/`styleLabel`, `stats` (7 статов, выведенных из регалий), `overall`, **`proStats`/`proOverall`/
  `proSeasoning`** (проекция `fightProfile(вес, pro=true)`: у любителя — скидка на необстрелянность, идея №11 веба),
  `badge`, `proRecord`, `accolades` (`accSummary`), `notes`. Сейчас 522 бойца: 103 любителя, 413 профи, 6 легенд.
* В рантайме JSON читает `UBoxingGameInstanceSubsystem` (`FRosterBoxer`). DataTable не делался: JSON читается
  без редактора и без переимпорта; для сборки он кладётся как есть (`DirectoriesToAlwaysStageAsNonUFS=Boxing/Data`
  в `DefaultGame.ini`).

## Передача пары в бой

`UBoxingGameInstanceSubsystem` (GameInstance-подсистема, свой GameInstance-класс не нужен) хранит
`FExhibitionSetup` (ид красного/синего, раунды, профи-правила, сид, вкладка/пол). `ABoxingFightGameMode::InitGame`
зовёт `ApplyToFightMode` (единственная правка GameMode): есть выбор — `RedPreset`/`BluePreset`/`Rounds`/
`bAllowDraw`/`Seed` из него, нет (L_Ring открыт напрямую, PIE) — свои пресеты. Командная строка (`-BoxSeed` и др.)
читается после и по-прежнему главнее.

Правила как в web: любители — 3 раунда, ничьих нет, сырые статы; профи/легенды — авто-раунды (чемпион vs
чемпион → 12, иначе 10) или 4/6/8/10/12, ничья возможна; при раундах > 3 бой идёт на `proStats` +
`Seasoning = proSeasoning` (налог на дистанцию у необстрелянного любителя).

Без меню: `-BoxPickRed=<часть имени> -BoxPickBlue=<часть имени> [-BoxPickRounds=N]` — та же пара сразу в L_Ring.

## Экраны (`Source/BoxingUE`)

| Класс | Файлы | Что |
|---|---|---|
| `UBoxingGameInstanceSubsystem`, `FRosterBoxer`, `FExhibitionSetup` | `BoxingGameInstanceSubsystem.*` | ростер, выбор, переходы, сценарий проверки |
| `UBoxingUiWidget` + `BoxUi::` | `UIWidgetBase.*` | база экранов: палитра web, шрифт, кнопки (прокси кликов), полосы, слоты |
| `ABoxingMenuGameMode`, `ABoxingMenuPlayerController` | `UIMenuGameMode.*` | карта меню: арена L_Ring фоном (экземпляр уровня, без копии карты), облёт камерой, режим «только UI» |
| `UBoxingMainMenuWidget` | `UIMainMenu.*` | главный экран |
| `UBoxingExhibitionWidget` | `UIExhibition.*` | Выставка: Любители/Профи/Легенды, М/Ж, поиск (имя/страна/город/дивизион), фильтр веса (±3 кг, перебор), список по уровню, карточки углов (7 статов, регалии, рост/размах, стиль/стойка), вес и «реализм» пары, раунды, досье, «Поменять углы», «В бой!» |
| `UBoxingFightHudWidget` | `UIFightHud.*` | HUD боя |
| `UBoxingFightResultWidget` | `UIFightResult.*` | итог |
| `ABoxingFightHUD` | `BoxingFightHUD.*` | ведёт оба виджета; Canvas-HUD S-41 — запасной (`-BoxCanvasHud`) |

Карта `L_Menu` создаётся скриптом `Tools/EditorScripts/ui_menu_map.py` (пустой уровень + GameMode Override).
`-BoxMenuBg=0` — меню без арены (тёмный фон).

**HUD боя:** панели углов (полоса угла, имя, ISO-код страны, здоровье с числом и цветом по уровню, стамина —
мигает красным на пустом баке, «нокдаунов: N»), по центру раунд и часы (≤ 10 с — жёлтые), баннер нокдауна
(счёт крупно; свой нокдаун — «жми удары или блок» + полоса подъёма), перерыва и конца боя; подсказки по ситуации
игроку (нет сил, блок слабеет, угол открыт, канаты/угол); легенда управления клавиатура + геймпад (F1 —
скрыть); в автопилоте вместо неё плашка «АВТОПИЛОТ». **Итог:** через 3.5 с после конца боя (`ResultDelay` —
дать доиграть нокаут), «ПОБЕДА / ТЫ ПОБЕДИЛ / ПОРАЖЕНИЕ / НИЧЬЯ», победитель, метод (нокаут/RSC с раундом,
вид решения), карты трёх судей по раундам и итог (цвет — чей раунд), нокдауны по раундам; ввод переключается
на UI, фокус на «Реванш» (Enter / A).

**Масштаб и ввод.** Размеры заданы для 1920×1080; DPI-кривая UMG (по короткой стороне) даёт на 1440p ×1.33 —
пропорции те же (см. скриншоты 1080/1440). Мышь, клавиатура (стрелки/Tab + Enter) и геймпад (крестовина + A):
наведение/фокус — светлая рамка кнопки.

## Проверка (headless-сценарий)

```
UnrealEditor.exe BoxingUE.uproject /Game/Boxing/Maps/L_Menu -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 ^
  -unattended -nosound -BoxUiAuto -BoxUiTab=amateur -BoxUiPick=Ташкенбай,Бибосинов -BoxAutopilot ^
  -BoxRoundSec=12 -BoxBreakSec=3 -BoxUiShotPrefix=ui_1080 [-BoxUiRematch] -ExecCmds=DisableAllScreenMessages
```
меню → Выставка (вкладка/пол, выбор пары через поиск) → бой автопилотом → итог → (`-BoxUiRematch`: реванш →
второй итог) → меню → выход. Снимки `Docs/screens/<prefix>_{menu, exhibition_empty, exhibition_search,
exhibition, hud, hud_break, hud_knockdown, result, menu_after}.png` (второй бой — `_rematch`). Флаги:
`-BoxUiTab=amateur|pro|legend`, `-BoxUiGender=M|F`, `-BoxUiPick=<часть имени>,<часть имени>` (без пробелов —
аргумент режется по пробелу), `-BoxUiRounds=N` (профи), отладка нокдауна — `-BoxUiDebugChin=N` /
`-BoxUiDebugChinRed=N` (подбородок синего/красного). Без `-BoxAutopilot` красный — игрок (виден HUD с легендой).

### Результаты (02.10.2026)

* Сборка — 0 ошибок; ростер 522 бойца; сценарии 1080p (любители, автопилот, реванш), 1440p (профи, 4 р., игрок),
  1080p (женщины, поиск) — проходят целиком, без ошибок в логе.
* Скриншоты: `ui_1080_*`, `ui_1440_*`, `ui_1080_hud_player.png` (HUD игрока с легендой), `ui_1080f_exhibition_search.png`
  (поиск), `ui_1080f_result.png` (итог досрочки RSC).
* Не снят **баннер нокдауна**: во всех прогонах (в т.ч. с подбородком 5–40) ядро заканчивало бой RSC без нокдаунов —
  код баннера есть, на скриншоте не проверен.

## Ограничения / дальше

* Вес боя: ядро UE не проецирует бойца на вес (сгонка/кэтчвейт, `fightProfile(target)` веба) — каждый дерётся
  на своём весе; экран показывает «реализм» пары, как web. Нужна проекция — экспортёру можно добавить статы на
  весах категорий или портировать `fightProfile`.
* Карьера — заглушка «скоро».
* Список ростера — ScrollBox с кнопками (до 300 строк, у мужчин-профи 281); для тысяч строк — `UListView`.
* Тач/мобильная раскладка не делалась (цель — ПК, iOS — решение владельца).
