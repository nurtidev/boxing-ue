# BoxingUE — прототип на Unreal Engine 5 (S-41)

Цель — **решение о движке**: одна сцена (ринг + 2 бойца MetaHuman), Motion Matching для ног,
4–6 ударов с физической реакцией на попадание, Lumen, камера боя, клавиатура/геймпад. Сравнить вживую
с web-версией (`nurtidev/boxing`, Vite + three.js) и решить, переезжать ли.

Машина: стационарный владельца — RTX 5060 8 ГБ, i5-13400F, 32 ГБ RAM, Windows 11. iOS сейчас не цель.

## Окружение (один раз)

1. **Visual Studio 2022 Community** — workloads «Game development with C++» + «Desktop development
   with C++», Windows 11 SDK (ставится winget'ом).
2. **Epic Games Launcher** → войти в аккаунт → Unreal Engine → Library → **UE 5.7** (или новее, если
   Game Animation Sample в Fab требует новее; тогда поправить `EngineAssociation` в `BoxingUE.uproject`).
   Компоненты: Core + Starter Content не нужны, Editor symbols — по желанию (≈ 30 ГБ лишних).
3. **Fab** (в Launcher или fab.com) → добавить в библиотеку бесплатно:
   - **Game Animation Sample Project** (Epic) — 500+ анимаций, Motion Matching, персонаж UE5 Manny/Quinn.
   - **MetaHuman** — в 5.6+ MetaHuman Creator встроен в редактор (плагин MetaHuman); двух бойцов
     собираем прямо в проекте.
4. Драйвер NVIDIA — свежий Game Ready (для DX12/RT).

> Контент Fab (`Content/GameAnimationSample/`, `Content/MetaHumans/`) **в git не кладётся**: лицензия
> Epic + квота GitHub LFS (бесплатно 1 ГБ). Каждая машина ставит его из Fab сама.

## План прототипа

| Шаг | Что | Где |
|---|---|---|
| 0 | Окружение (выше) | владелец |
| 1 | Ядро боя на C++ — порт `web/src/engine/interactive/` (упрощённый ИИ), сидируемое | `Source/BoxingUE/` — [Docs/FIGHT_CORE_PORT.md](Docs/FIGHT_CORE_PORT.md) |
| 2 | Миграция Game Animation Sample в проект: персонаж с Motion Matching, ретаргет на MetaHuman (IK Retargeter) | редактор |
| 3 | Ринг 6.1 м (размеры из `web/src/engine/ringSize.ts`), свет арены, Lumen | `Content/Boxing/Maps/L_Ring` |
| 4 | Боевой Pawn: Enhanced Input (раскладка как в вебе: J/K, U/I, N/M, WASD, Q/E, пробел), ядро → AnimBP (стойка-локомоция через Motion Matching, удары монтажами со скраббингом фазы) | C++ + AnimBP |
| 5 | Реакция на попадание: Physical Animation / Physics Control — импульс в кость по вектору удара, частичная рэгдолл-смесь, нокдаун | AnimBP + PhysicsControl |
| 6 | Камера боя — как в вебе: со стороны правого плеча игрока, мёртвая зона ±20° | C++ |
| 7 | Замеры и сравнение с web: FPS/VRAM (stat unit, stat gpu), вес сборки, время итерации, ощущение удара | `Docs/COMPARISON.md` |

Анимации ударов: 4–6 клипов (джеб/кросс/хуки/апперкоты). Источник — боксёрские клипы Mixamo, уже
используемые в web-версии (ретаргет на скелет UE через IK Retargeter; сырые файлы Mixamo в git не кладём).

## Сборка

```
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" BoxingUEEditor Win64 Development -Project="%CD%\BoxingUE.uproject" -WaitMutex
```

Или правый клик по `BoxingUE.uproject` → Generate Visual Studio project files → открыть `.sln`.
