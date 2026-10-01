# Кадры контакта монтажей (S-41, трек C)

Сгенерировано `Tools/EditorScripts/anim_montages.py` — не править руками, перезапустить скрипт.

Клипы — боксёрские Mixamo, ретаргет на UEFN Mannequin (логический скелет GASP) и UE5 Manny;
времена одинаковые для обоих наборов (ретаргет не меняет тайминг). Монтажи: `/Game/BoxingLocal/Anim/<Имя>` (UEFN),
`/Game/BoxingLocal/Anim/Manny/<Имя>` (Manny). Время — от начала монтажа при PlayRate = 1.

**Как найден кадр** (как `keyTimes` в web `SkinnedFighter.tsx`): удар — максимальный вынос кисти бьющей руки
вперёд от таза (меш смотрит вдоль +Y) в первых 80% клипа; апперкот — максимум «вперёд + вверх»
(у апперкота вынос вперёд пиков раньше — на замахе); блок — обе перчатки выше всего; слип/реакции —
пик смещения головы относительно таза (в сторону + вниз). `t_ext` — для справки: максимум |кисть − плечо| (на хуках/апперкотах приходится на
замах, поэтому не используется).

**Синхронизация с ядром (трек B):** ядро резолвит удар в момент `contact` своей фазы; чтобы кадр контакта
монтажа совпал, `PlayRate = t / T_core_contact` (t — время ниже, T_core_contact — сколько секунд в ядре от
старта удара до резолюции). Нотифай `Contact` (AnimNotify_PlayMontageNotify) приходит в
`UAnimInstance::OnPlayMontageNotifyBegin` (NotifyName = "Contact") — для проверки/эффектов.

| Монтаж | Клип | Слот | Длина, с | Нотифай | Время, с | Доля | t_ext, с | Рука / смещение головы, см |
|---|---|---|---|---|---|---|---|---|
| AM_Jab | jab | DefaultSlot | 1.733 | Contact | 0.433 | 0.25 | 0.433 | рука l |
| AM_Cross | cross | DefaultSlot | 2.133 | Contact | 0.667 | 0.31 | 0.667 | рука r |
| AM_HookL | hookL | DefaultSlot | 2.000 | Contact | 0.467 | 0.23 | 0.467 | рука l |
| AM_HookR | hookR | DefaultSlot | 2.167 | Contact | 0.700 | 0.32 | 0.533 | рука r |
| AM_UpperL | upperL | DefaultSlot | 2.200 | Contact | 0.767 | 0.35 | 0.567 | рука l |
| AM_UpperR | upperR | DefaultSlot | 2.200 | Contact | 0.700 | 0.32 | 0.533 | рука r |
| AM_BodyHook | bodyHook | DefaultSlot | 1.900 | Contact | 0.900 | 0.47 | 0.800 | рука l |
| AM_Block | block | DefaultSlot | 2.867 | GuardUp | 1.733 | 0.60 | — |  |
| AM_BlockHit | blockHit | DefaultSlot | 1.333 | GuardUp | 0.233 | 0.17 | — |  |
| AM_SlipL | slipL | DefaultSlot | 2.233 | Peak | 0.500 | 0.22 | — | голова dx=36 dy=3 dz=-4 |
| AM_SlipR | slip | DefaultSlot | 2.233 | Peak | 0.500 | 0.22 | — | голова dx=-36 dy=3 dz=-4 |
| AM_HitHead | hitHead | DefaultSlot | 1.367 | Peak | 0.633 | 0.46 | — | голова dx=-6 dy=11 dz=-7 |
| AM_HitBody | hitBody | DefaultSlot | 1.600 | Peak | 0.700 | 0.44 | — | голова dx=4 dy=36 dz=-29 |
| AM_Guard | guard | DefaultSlot | 2.200 | — | — | — | — | петля ×1000 (стойка-оверлей) |
| AM_Knockdown | knockdown | DefaultSlot | 2.133 | Peak | 1.800 | 0.84 | — | голова dx=23 dy=-69 dz=-56 |
| AM_Knockout | knockout | DefaultSlot | 4.933 | Peak | 3.000 | 0.61 | — | голова dx=-37 dy=-47 dz=-52 |
| AM_GetUp | getUp | DefaultSlot | 2.700 | Peak | 1.067 | 0.40 | — | голова dx=39 dy=42 dz=-7 |
| AM_Victory | victory | DefaultSlot | 4.500 | Peak | 0.367 | 0.08 | — | голова dx=-1 dy=24 dz=-8 |
| AM_Defeat | defeat | DefaultSlot | 4.767 | Peak | 0.900 | 0.19 | — | голова dx=-3 dy=18 dz=-19 |

Заметки:
- `AM_UpperR` в исходном клипе Mixamo — апперкот **в корпус** (кисть на уровне пояса, корпус в наклоне);
  `AM_UpperL` — в голову. Ретаргет это не меняет (сверено с исходником).
- `AM_SlipL` — производный: осевая цепочка `slip` отзеркалена (корпус уходит влево), руки — как в оригинале.
- Полнотелые (`DefaultSlot`) — нокдаун/подъём/победа/поражение: играют через уже существующий слот AnimBP GASP.
- Клипы «на месте» (Force Root Lock): горизонтальный ход таза Mixamo (выпад джеба ~40 см, шаг назад
  hitHead ~1 м) убран — позицию бойца ведёт ядро.

```json
[
 {
  "montage": "AM_Jab",
  "clip": "jab",
  "slot": "DefaultSlot",
  "length": 1.733,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.433,
  "frac": 0.25,
  "t_ext": 0.433,
  "arm": "l",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_Cross",
  "clip": "cross",
  "slot": "DefaultSlot",
  "length": 2.133,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.667,
  "frac": 0.312,
  "t_ext": 0.667,
  "arm": "r",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_HookL",
  "clip": "hookL",
  "slot": "DefaultSlot",
  "length": 2.0,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.467,
  "frac": 0.233,
  "t_ext": 0.467,
  "arm": "l",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_HookR",
  "clip": "hookR",
  "slot": "DefaultSlot",
  "length": 2.167,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.7,
  "frac": 0.323,
  "t_ext": 0.533,
  "arm": "r",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_UpperL",
  "clip": "upperL",
  "slot": "DefaultSlot",
  "length": 2.2,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.767,
  "frac": 0.348,
  "t_ext": 0.567,
  "arm": "l",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_UpperR",
  "clip": "upperR",
  "slot": "DefaultSlot",
  "length": 2.2,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.7,
  "frac": 0.318,
  "t_ext": 0.533,
  "arm": "r",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_BodyHook",
  "clip": "bodyHook",
  "slot": "DefaultSlot",
  "length": 1.9,
  "kind": "punch",
  "notify": "Contact",
  "t": 0.9,
  "frac": 0.474,
  "t_ext": 0.8,
  "arm": "l",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_Block",
  "clip": "block",
  "slot": "DefaultSlot",
  "length": 2.867,
  "kind": "guard_up",
  "notify": "GuardUp",
  "t": 1.733,
  "frac": 0.605,
  "t_ext": -1,
  "arm": "",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_BlockHit",
  "clip": "blockHit",
  "slot": "DefaultSlot",
  "length": 1.333,
  "kind": "guard_up",
  "notify": "GuardUp",
  "t": 0.233,
  "frac": 0.175,
  "t_ext": -1,
  "arm": "",
  "head": null,
  "loops": 1
 },
 {
  "montage": "AM_SlipL",
  "clip": "slipL",
  "slot": "DefaultSlot",
  "length": 2.233,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 0.5,
  "frac": 0.224,
  "t_ext": -1,
  "arm": "",
  "head": [
   35.9,
   3.4,
   -3.9
  ],
  "loops": 1
 },
 {
  "montage": "AM_SlipR",
  "clip": "slip",
  "slot": "DefaultSlot",
  "length": 2.233,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 0.5,
  "frac": 0.224,
  "t_ext": -1,
  "arm": "",
  "head": [
   -35.9,
   3.4,
   -3.9
  ],
  "loops": 1
 },
 {
  "montage": "AM_HitHead",
  "clip": "hitHead",
  "slot": "DefaultSlot",
  "length": 1.367,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 0.633,
  "frac": 0.463,
  "t_ext": -1,
  "arm": "",
  "head": [
   -5.5,
   10.9,
   -7.1
  ],
  "loops": 1
 },
 {
  "montage": "AM_HitBody",
  "clip": "hitBody",
  "slot": "DefaultSlot",
  "length": 1.6,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 0.7,
  "frac": 0.438,
  "t_ext": -1,
  "arm": "",
  "head": [
   4.3,
   35.9,
   -28.8
  ],
  "loops": 1
 },
 {
  "montage": "AM_Guard",
  "clip": "guard",
  "slot": "DefaultSlot",
  "length": 2.2,
  "kind": "head_peak",
  "notify": "",
  "t": 0.433,
  "frac": 0.197,
  "t_ext": -1,
  "arm": "",
  "head": [
   1.1,
   6.2,
   -0.7
  ],
  "loops": 1000
 },
 {
  "montage": "AM_Knockdown",
  "clip": "knockdown",
  "slot": "DefaultSlot",
  "length": 2.133,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 1.8,
  "frac": 0.844,
  "t_ext": -1,
  "arm": "",
  "head": [
   23.0,
   -68.7,
   -56.3
  ],
  "loops": 1
 },
 {
  "montage": "AM_Knockout",
  "clip": "knockout",
  "slot": "DefaultSlot",
  "length": 4.933,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 3.0,
  "frac": 0.608,
  "t_ext": -1,
  "arm": "",
  "head": [
   -37.3,
   -47.3,
   -52.3
  ],
  "loops": 1
 },
 {
  "montage": "AM_GetUp",
  "clip": "getUp",
  "slot": "DefaultSlot",
  "length": 2.7,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 1.067,
  "frac": 0.395,
  "t_ext": -1,
  "arm": "",
  "head": [
   38.8,
   41.5,
   -7.4
  ],
  "loops": 1
 },
 {
  "montage": "AM_Victory",
  "clip": "victory",
  "slot": "DefaultSlot",
  "length": 4.5,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 0.367,
  "frac": 0.081,
  "t_ext": -1,
  "arm": "",
  "head": [
   -1.4,
   24.0,
   -8.0
  ],
  "loops": 1
 },
 {
  "montage": "AM_Defeat",
  "clip": "defeat",
  "slot": "DefaultSlot",
  "length": 4.767,
  "kind": "head_peak",
  "notify": "Peak",
  "t": 0.9,
  "frac": 0.189,
  "t_ext": -1,
  "arm": "",
  "head": [
   -2.9,
   17.5,
   -19.4
  ],
  "loops": 1
 }
]
```
