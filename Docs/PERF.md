# Производительность арены (S-64, tech-artist)

Цель: 60+ FPS на эпике 1920×1080 с запасом (RTX 5060 + i5-13400F), вид — прежний. Всё, что ниже про уровень, делает
`Tools/EditorScripts/build_ring.py` (уровень `L_Ring` руками не правится).

## Как мерить

`Tools/EditorScripts/ring_perf.py` — в игре, автопилот, CSV-профайлер (окно 18–48 с) + `ProfileGPU` в лог:

```
UnrealEditor.exe <uproject> /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -ForceRes ^
  -unattended -nosound -BoxAutopilot -BoxSeed=60 -BoxShots=30 -BoxShotPrefix=perf3_x -BoxQuitAfter=62 ^
  -ExecCmds="stat unit,stat fps,stat rhi,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/ring_perf.py"
```

Окружение: `PERF_SP=100` — `r.ScreenPercentage`; `PERF_PRO=1` — оформление профи (как сделает GameMode);
эксперименты без пересборки уровня: `PERF_NOSHADOW=<подстрока имени света>`, `PERF_OFF=…`, `PERF_FG=<Lumen final gather>`,
`PERF_CONTACT=<подстрока>:<длина>`. CSV — `Saved/Profiling/CSV/*.csv` (FrameTime, GameThreadTime, RenderThreadTime,
GPUTime, RHI/DrawCalls, RHI/PrimitivesDrawn).

**Грабли замера.** Каталог общий: если во время окна CSV на машине идёт чужая игра/редактор (`UnrealEditor.exe`), GPU
делится и цифры врут вдвое (видели 16–27 FPS вместо 60). Замер считать только при единственном `UnrealEditor.exe`;
`UnrealEditor-Cmd -nullrhi` (боты fight-designer) GPU не трогает — допустимо.
«До» мерилось на копии прежнего уровня (`git lfs smudge` от `HEAD:Content/Boxing/Maps/L_Ring.umap` →
`/Game/BoxingLocal/Tmp/L_RingBase`, удалена после замера).

## Итог (сид 60, автопилот, медианы CSV за 30 с)

| | разрешение рендера | FPS (средн.) | кадр, мс (мед. / p95) | GPU, мс | игр. поток, мс | draw calls | примитивов |
|---|---|---|---|---|---|---|---|
| до | 72.9 % (как у QA) | 49.8 | 19.6 / 21.4 | 18.8 | 9.3 | 1209 | 369 тыс. |
| **после** | 72.9 % | **82.3** | 11.8 / 13.5 | 10.8 | 7.1 | **445** | 360 тыс. |
| до | 100 % | 31.9 | 30.0 / 38.4 | 29.5 | 12.6 | 1208 | 369 тыс. |
| **после** | **100 %** | **62.2** | 15.7 / 17.1 | 14.7 | 8.0 | **446** | 360 тыс. |
| после, оформление профи | 100 % | 61.6 | 15.8 / 17.2 | 14.8 | 8.0 | 445 | 360 тыс. |

Предупреждения на экране — нет ни одного (были `[VSM] One Pass Projection max lights overflow` и
`A sky light with real-time capture … requires SkyAtmosphere`): `perf3_after_*.png` сняты без `DisableAllScreenMessages`.

### Разбивка GPU (ProfileGPU, 100 %, один кадр; графическая очередь)

| проход | до, мс | после, мс | что сделано |
|---|---|---|---|
| VSM: маска теней (`VirtualShadowMapProjectionMaskBits`) | **14.4** | 1.3 | теневых источников 17 → 1 |
| VSM: глубина теней (non-Nanite проходов) | 1.2 (754) | 0.5 (59) | Nanite у статики зала, кэш страниц |
| свет без теней / с тенями (Batched / Unbatched) | 1.3 / 2.9 | 1.7 / 0.3 | конус фермы 30° → 22°, трибуны 12 → 4 спота |
| объёмный туман | 1.1 | 0.7 | трибуны не светят в туман |
| Lumen (асинхронно, Screen Probe Gather) | — | −2.5 к GPU | final gather 2.0 → 1.0 |
| волосы (грумы, 3 человека) | 2.5 | 2.4 | не трогал (см. ниже) |
| TSR 1920×1080 | 2.4 | 2.5 | — |
| BasePass | 0.2 | 0.3 (Nanite) | — |

Рендер-поток «19 мс» у QA — ожидание GPU (`Exclusive/RenderThread/Visibility` ≈ 10 мс ожидания), а не своя работа.

## Что сделано в уровне

1. **Тени.** Было 17 теневых прожекторов (12 фермы + ТВ-ключ + 4 заполняющих) → переполнение одно-проходной проекции VSM
   (по умолчанию 16 источников на пиксель) и 7–14 мс GPU. Теперь VSM-тень даёт только верхний ТВ-ключ (бойцы, рефери,
   канаты отбрасывают читаемую тень на канвас); ферма и заполняющие — без теней. Контактные тени у заполняющих пробовал —
   ~1.4 мс за малозаметную разницу, выключены (`RING_FILL_CONTACT`).
2. **Sky Light** — без real-time capture (в закрытом зале неба нет; real-time требовал SkyAtmosphere и давал предупреждение +
   чёрный снимок), `SLS_CAPTURED_SCENE`, Movable — снимок при загрузке.
3. **Nanite** у статики зала: копии базовых фигур `/Game/Boxing/Environment/SM_NaniteCube|Cylinder|Sphere` (движковые не
   трогаем), `M_BoxingBase.used_with_nanite`. Стены, пол, потолок, трибуны, кресла, публика, судьи, столы, табло, ферма,
   корпуса прожекторов. Ряды трибун (28 акторов) → 2 ISM. **Ринг не на Nanite**: канаты/столбы/подушки прячутся у камеры
   через `SetRenderInMainPass` (контроллер боя, по меткам `Ring_Rope*/Ring_Post*/Ring_Pad*`) — оставлены обычными мешами.
4. **Свет.** Прожекторы фермы: 12, конус 30° → 22° (пятно каждого — не весь ринг; лучи в тумане тоньше, все 12 на месте).
   Свет трибун: 12 спотов → 4 широких (один на сторону), без вклада в объёмный туман.
5. **Lumen final gather 2.0 → 1.0** (1.0 — значение эпика по умолчанию; 2.0 — «кино»), −2.5 мс GPU на 1080p.

Вид: `arena3_before_*` / `arena3_after_am_*` (общие планы), `perf3_before_{15,30,45}` / `perf3_after_{15,30,45}` (кадры боя,
100 %), `perf3_stat_{before,after}_{100,729}` (с `stat unit` + `stat rhi`). Отличия: у бойцов одна тень вместо пяти слабых
веерных, края ринга чуть темнее центра (ТВ-пятно), лучи фермы тоньше.

## Что ещё можно (не в моей зоне — решение продюсера)

- **TSR 2.5 мс** при 100 % — `r.TSR.*`/качество сглаживания — пункт «Качество» (ux-mobile).
- **Грумы 2.4 мс** (волосы, брови, ресницы, щетина Kellan на бойцах и рефери; `CommitHairRayTracingGeometryUpdates`
  ≈ 0.8 мс) — `r.HairStrands.RayTracing 0` (Config) или LOD/карточки грумов (отдельная задача).
- **VSM SMRT** ключа (1.3 мс): `r.Shadow.Virtual.SMRT.RayCountLocal` 7 → 4 (Config).
- **MegaLights** (UE 5.7) вернули бы тени всем прожекторам по фиксированной цене — экспериментально, нужен замер.
- Игровой поток 8 мс (анимация на игровом потоке из-за `a.ParallelAnimEvaluation 0` при физреакции) — game-feel.

## Оформление профи (API для C++)

В `L_Ring` два набора акторов оформления (по 28):
- тег **`ArenaAmateur`** — канвас, юбка, полоса, центральный круг, надписи «ЧЕМПИОНАТ ПО БОКСУ», подушки углов, поле зала,
  борта с надписями, кресла, экраны табло с заголовком. Видимы по умолчанию.
- тег **`ArenaPro`** — то же для профи: графитовый канвас с золотым кругом, чёрная юбка с золотой полосой, надписи
  «ПРОФЕССИОНАЛЬНЫЙ БОКС» (юбка) и «ВЕЧЕР БОКСА» (борта, табло), подушки углов глубже тоном (нейтральные — чёрные), бордовые
  борта/кресла/поле, тёмно-красное табло. Сохранены с `bHidden = true` (скрыты в игре). Без логотипов реальных организаций.

Коллизия: пол держит канвас любителей (он и скрытым коллидирует); у профи-канваса, поля, бортов и экранов коллизия
выключена — переключение физику не меняет. Маркеры, канаты, столбы, свет — общие.

Вызов (GameMode, как только известен тип боя и **до первого кадра** — например, в `ABoxingFightGameMode::BeginPlay`
после чтения `FExhibitionSetup`/`-BoxPickRounds`; профи = `Rounds > 3`, как везде):

```cpp
// BoxingFightGameMode.h
UFUNCTION(BlueprintCallable, Category = "Boxing|Arena")
void ApplyArenaDress(bool bPro);

// BoxingFightGameMode.cpp
#include "Kismet/GameplayStatics.h"
void ABoxingFightGameMode::ApplyArenaDress(bool bPro)
{
	static const FName AmTag(TEXT("ArenaAmateur")), ProTag(TEXT("ArenaPro"));
	TArray<AActor*> Am, Pro;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), AmTag, Am);
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), ProTag, Pro);
	for (AActor* A : Am)  { A->SetActorHiddenInGame(bPro); }
	for (AActor* A : Pro) { A->SetActorHiddenInGame(!bPro); }
}
// ... в BeginPlay (или там, где выставляются раунды): ApplyArenaDress(Rounds > 3);
```

`GetAllActorsWithTag` ищет по всем загруженным уровням мира. Меню (`ABoxingMenuGameMode`) грузит `L_Ring` экземпляром
уровня — там ничего вызывать не нужно (фон — любительский); если захочется профи-фон для экрана выбора профи, тот же вызов
после `OnLevelShown` экземпляра. Контроллер боя собирает подушки по меткам `Ring_Pad*` — в список попадут подушки обоих
оформлений, это безвредно (скрытые не рисуются). Проверено скриптом (`PERF_PRO=1` в `ring_perf.py` делает ровно это):
`perf3_after_pro_*.png`, производительность та же (61.6 FPS при 100 %).

Снимки уровня в редакторе: `ring_shots.py` + `RING_SHOT_VARIANT=pro` (`arena3_after_pro_*`).
