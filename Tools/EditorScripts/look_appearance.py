# -*- coding: utf-8 -*-
# S-60 (бойцы разные): данные облика для ВСЕХ бойцов ростера → Content/Boxing/Data/Appearance.json.
#
# Обычный Python 3 (без UE и без зависимостей), например Python из Blender:
#   "C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe" Tools\EditorScripts\look_appearance.py
#   [--web <boxing/web>] [--roster <Roster.json>] [--out <Appearance.json>]
#
# Источники (веб — спецификация, формулы НЕ придумываются заново):
#   * web/src/data/appearance/{amateurs,pros1,pros2}.ts — внешность реальных бойцов по публичным фото
#     (тон кожи 1..6, причёска, цвет волос, растительность на лице, тату; confidence/source). Ключ — имя.
#   * нет в таблицах → generateAppearance веба (web/src/data/appearance/index.ts): детерминированно из страны
#     и хеша имени (FNV-1a + mulberry32 — тот же ГСЧ, бит-в-бит; ГСЧ боя не трогается).
#   * телосложение — bodyMorph веба (web/src/ui/three/look.ts) от веса/роста; масштаб — рост / рост модели.
# Выход: на каждого бойца "appearance" (как в вебе) + "look" — ГОТОВЫЕ значения для рантайма UE (C++ их только
# применяет, см. Docs/LOOK.md «S-60: облик по данным бойца»): масштаб, веса морфов тела, множители тона кожи лица
# и тела, грум волос/щетины и его длина, меланин/рыжина/седина/краска волос, класс-суффикс облика.
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, "..", ".."))

# ---------------------------------------------------------------------------------------------- модель UE
# Рост видимой модели (тело m_med_nrw мокап-набора + лицо Kellan, от пола до макушки, без волос), см.
# Tools/Blender/look_kit.py: пол z −1.97 см, макушка лица Kellan 173.3 см.
MODEL_HEIGHT_CM = 175.3
SCALE_MIN, SCALE_MAX = 0.84, 1.20

# Тон кожи 1..6 → свои текстуры кожи (лицо LOD1/LOD3/LOD5 + тело), перекрашенные из текстур Kellan
# (Tools/Blender/look_skin_tones.py: цель тона на шве шеи, пятна сжаты) и импортированные в BoxingLocal
# (look_skin.py). Рантайм ставит их в параметр BaseColor MID кожи; BaseColor_ColorCorrect — лишь индивидуальный
# разброс (±светлота/теплота) вокруг 1.
SKIN_TEX_DIR = "/Game/BoxingLocal/Characters/Skin"
FACE_SLOTS = {"head_LOD1_shader_shader": "LOD1", "head_LOD3_shader_shader": "LOD3", "head_LOD57_shader_shader": "LOD5"}
BODY_SLOT = "Skin"


def skin_textures(tone):
    face = {slot: "%s/T_SkinFace_%s_T%d.T_SkinFace_%s_T%d" % (SKIN_TEX_DIR, lod, tone, lod, tone)
            for slot, lod in FACE_SLOTS.items()}
    body = {BODY_SLOT: "%s/T_SkinBody_T%d.T_SkinBody_T%d" % (SKIN_TEX_DIR, tone, tone)}
    return face, body


# Цвет волос → параметры волос MetaHuman (MI_Hair*: hairMelanin 0 — блонд … 1 — чёрный, hairRedness, WhiteAmount —
# седина, hairDye — краска; brows/stubble — те же, брови не светлее 0.35 меланина).
HAIR_COLOR = {
    "black":       {"melanin": 0.98, "redness": 0.12, "white": 0.0},
    "dark_brown":  {"melanin": 0.80, "redness": 0.22, "white": 0.0},
    "brown":       {"melanin": 0.68, "redness": 0.30, "white": 0.0},
    "light_brown": {"melanin": 0.56, "redness": 0.30, "white": 0.0},
    "blond":       {"melanin": 0.18, "redness": 0.22, "white": 0.0},
    "red":         {"melanin": 0.40, "redness": 0.90, "white": 0.0},
    "gray":        {"melanin": 0.55, "redness": 0.10, "white": 0.65},
    "dyed":        {"melanin": 0.30, "redness": 0.20, "white": 0.0, "dye": (0.76, 0.29, 0.56)},
}

# Причёска → грум (+ ассет привязки к лицу Kellan). Свои грумы — Tools/Blender/look_hair.py → look_groom_import.py
# (/Game/BoxingLocal/Characters/Grooms): прямые короткие/ёжик/средние, женские хвост и пучок, бороды. Афро-волны
# Kellan (Hair_S_360Waves) — для курчавых волос (тон кожи 5–6: короткие/афро/косички/дреды); его длина режется
# HairLengthScale. Чего нет (длинные распущенные, косички, ирокез) — ближайшее: в боксе длинные волосы собраны.
GROOMS = "/Game/BoxingLocal/Characters/Grooms"
KELLAN_HAIR = "/Game/MetaHumans/Kellan/MaleHair"


def _g(name):
    # свои грумы — без привязки к коже: крепятся к сокету FACIAL_C_FacialRoot лица (см. GROOM_ATTACH)
    return {"groom": "%s/GR_%s.GR_%s" % (GROOMS, name, name), "binding": None, "attach": "FACIAL_C_FacialRoot"}


# Крепление своего грума (точки — в пространстве меша лица Kellan, см) к лицу: сокет FACIAL_C_FacialRoot (ребёнок
# head, жёстко следует голове) с ТЕМ ЖЕ относительным трансформом, что у грумов Kellan в BP_Kellan (брови/волосы):
# компенсирует позу привязки кости. Снято в игре с компонента Eyebrows (look_gallery.py LOOK_DEBUG=1).
GROOM_ATTACH = {"socket": "FACIAL_C_FacialRoot", "loc": [-0.000022, 159.750986, 0.555346],
                "quat": [0.707107, 0.0, 0.0, 0.707107]}


WAVES = {"groom": KELLAN_HAIR + "/Hair/Hair_S_360Waves.Hair_S_360Waves",
         "binding": KELLAN_HAIR + "/GroomBinding/Hair_S_360Waves_m_med_nrw_head_skmesh_Face_Archetype_Binding."
                                  "Hair_S_360Waves_m_med_nrw_head_skmesh_Face_Archetype_Binding"}
STUBBLE = {"groom": KELLAN_HAIR + "/Hair/Mustache_S_Stubble.Mustache_S_Stubble",
           "binding": KELLAN_HAIR + "/GroomBinding/Mustache_S_Stubble_m_head_Archetype_Binding."
                                    "Mustache_S_Stubble_m_head_Archetype_Binding"}
NONE = {"groom": None, "binding": None}
# стиль → (грум для прямых волос, грум для курчавых, длина)
HAIR_STYLE = {
    "bald":     (NONE, NONE, 0.0),
    "buzz":     (_g("Buzz"), WAVES, 1.0),        # у волн «под машинку» = длина 0.38 (ниже)
    "short":    (_g("Short"), WAVES, 1.0),
    "medium":   (_g("Medium"), WAVES, 1.0),
    "long":     (_g("Ponytail"), WAVES, 1.0),
    "afro":     (_g("Short"), WAVES, 1.0),
    "braids":   (_g("Cornrows"), _g("Cornrows"), 1.0),   # S-64: колоски (у женщин — GR_Braids, см. FEMALE_HAIR)
    "dreads":   (_g("Medium"), _g("Cornrows"), 1.0),
    "mohawk":   (_g("Short"), WAVES, 0.6),
    "ponytail": (_g("Ponytail"), _g("Ponytail"), 1.0),
    "bun":      (_g("Bun"), _g("Bun"), 1.0),
}
# S-64: женщинам — без «лысых» вариантов. Волны Kellan (Hair_S_360Waves) под HairLengthScale < 1 и пустой грум
# на женской фигуре читаются лысиной (Шилдс). Стиль → грум для прямых / курчавых волос (None — как в HAIR_STYLE).
FEMALE_HAIR = {
    "braids": (_g("Braids"), _g("Braids")),          # колоски в узел + коса (Шилдс, Маршалл)
    "dreads": (_g("Braids"), _g("Braids")),          # локи в боксе собраны назад — ближе всего коса
    "afro":   (None, _g("Bun")),                     # курчавые собраны в пучок («афро-пуф»)
    "short":  (None, _g("Cornrows")),                # короткие курчавые — колоски
    "bald":   (_g("Cornrows"), _g("Cornrows")),      # лысых женщин в ростере нет — на всякий случай
    "mohawk": (_g("Short"), _g("Cornrows")),
}

# Растительность на лице: грум бороды (компонент Beard) + щетина Kellan (компонент Mustache) под ней.
FACIAL_HAIR = {
    "none":       (NONE, False),
    "stubble":    (NONE, True),
    "mustache":   (_g("Mustache"), True),
    "goatee":     (_g("Goatee"), True),
    "beard":      (_g("Beard"), True),
    "full_beard": (_g("FullBeard"), True),
}


def clamp01(x):
    return max(0.0, min(1.0, x))


def body_morph(weight_kg, height_cm):
    """web look.ts bodyMorph + веса морфов UE. heavy/lean — как в вебе; Muscular у веба 0.45..0.8 «всегда»
    (база three.js-модели худая), у тела MetaHuman база уже спортивная — берём только «пик» около ИМТ 24."""
    bmi = weight_kg / (height_cm / 100.0) ** 2
    heavy = clamp01((bmi - 25.5) / 7)
    lean = clamp01((22.5 - bmi) / 3)
    muscular = clamp01(0.45 + 0.35 * (1 - abs(bmi - 24) / 6))
    return bmi, {"Heavy": round(heavy, 3), "Lean": round(lean, 3),
                 "Muscular": round(clamp01((muscular - 0.45) / 0.35), 3)}


# ------------------------------------------------------------------------- порт generateAppearance (web)
def _imul(a, b):
    return ((a & 0xFFFFFFFF) * (b & 0xFFFFFFFF)) & 0xFFFFFFFF


def hash_rng(seed):
    """FNV-1a по UTF-16 кодам (как charCodeAt) + mulberry32 — бит-в-бит с web index.ts hashRng."""
    h = 0x811C9DC5
    units = seed.encode("utf-16-le")
    for i in range(0, len(units), 2):
        h = _imul(h ^ (units[i] | (units[i + 1] << 8)), 0x01000193)
    state = [h]

    def r():
        state[0] = (state[0] + 0x6D2B79F5) & 0xFFFFFFFF
        t = state[0]
        t = _imul(t ^ (t >> 15), t | 1)
        t = (t ^ ((t + _imul(t ^ (t >> 7), t | 61)) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return ((t ^ (t >> 14)) & 0xFFFFFFFF) / 4294967296.0
    return r


def pick(r, table):
    total = sum(w for _, w in table)
    x = r() * total
    for v, w in table:
        x -= w
        if x < 0:
            return v
    return table[-1][0]


T_EAST_ASIAN = {"tones": [(2, 3), (3, 5), (4, 1)], "hair": [("black", 8), ("dark_brown", 2)]}
T_SE_ASIAN = {"tones": [(3, 4), (4, 5)], "hair": [("black", 1)]}
T_EUROPEAN = {"tones": [(1, 3), (2, 6), (3, 1)],
              "hair": [("dark_brown", 4), ("brown", 3), ("light_brown", 2), ("blond", 1.2), ("black", 1.5), ("red", 0.3)]}
T_AFRICAN = {"tones": [(5, 5), (6, 3), (4, 1)], "hair": [("black", 1)], "afro": True}
T_LATIN = {"tones": [(2, 2), (3, 5), (4, 3)], "hair": [("black", 6), ("dark_brown", 3)]}
T_MIDEAST = {"tones": [(2, 3), (3, 5)], "hair": [("black", 6), ("dark_brown", 4)]}
T_SOUTH_ASIAN = {"tones": [(3, 3), (4, 5)], "hair": [("black", 1)]}
PROFILES = {
    "kz": [(T_EAST_ASIAN, 0.75), (T_EUROPEAN, 0.2), (T_MIDEAST, 0.05)],
    "centralAsia": [(T_EAST_ASIAN, 0.55), (T_MIDEAST, 0.35), (T_EUROPEAN, 0.1)],
    "eastAsia": [(T_EAST_ASIAN, 1)],
    "seAsia": [(T_SE_ASIAN, 1)],
    "europe": [(T_EUROPEAN, 0.92), (T_AFRICAN, 0.05), (T_MIDEAST, 0.03)],
    "westMixed": [(T_EUROPEAN, 0.5), (T_AFRICAN, 0.35), (T_LATIN, 0.15)],
    "africa": [(T_AFRICAN, 1)],
    "caribbean": [(T_AFRICAN, 0.7), (T_LATIN, 0.3)],
    "latam": [(T_LATIN, 0.85), (T_EUROPEAN, 0.1), (T_AFRICAN, 0.05)],
    "cuba": [(T_LATIN, 0.5), (T_AFRICAN, 0.4), (T_EUROPEAN, 0.1)],
    "mideast": [(T_MIDEAST, 1)],
    "northAfrica": [(T_MIDEAST, 0.8), (T_AFRICAN, 0.2)],
    "southAsia": [(T_SOUTH_ASIAN, 1)],
    "oceania": [(T_EUROPEAN, 0.75), (T_SE_ASIAN, 0.25)],
}
REGION_OF = {}
for codes, reg in (("KZ", "kz"), ("UZ KG TJ TM MN", "centralAsia"), ("JP KR KP CN TW", "eastAsia"),
                   ("PH TH ID VN MY KH LA MM", "seAsia"), ("US GB CA FR", "westMixed"),
                   ("NG GH ZA CM CD CG KE UG TZ ZM ZW NA SN CI BJ TG ET ER SD AO MZ BW GA", "africa"),
                   ("JM TT BS HT BB", "caribbean"),
                   ("MX PR DO VE CO AR NI CR PA BR PE CL EC UY GT HN SV PY BO", "latam"), ("CU", "cuba"),
                   ("TR IR AZ AM GE IQ SY JO LB SA AE IL KW", "mideast"), ("MA DZ TN EG LY", "northAfrica"),
                   ("IN PK BD LK NP", "southAsia"), ("AU NZ", "oceania")):
    for c in codes.split():
        REGION_OF[c] = reg


def generate_appearance(name, gender, country_code):
    r = hash_rng("look:" + name)
    t = pick(r, PROFILES[REGION_OF.get(country_code, "europe")])
    skin = pick(r, t["tones"])
    hair_color = pick(r, t["hair"])
    facial = "none"
    if gender == "F":
        style = (pick(r, [("braids", 4), ("afro", 2), ("bun", 2), ("short", 1)]) if t.get("afro")
                 else pick(r, [("ponytail", 4), ("bun", 3), ("braids", 1.5), ("short", 1.5)]))
    else:
        style = (pick(r, [("buzz", 5), ("bald", 3), ("afro", 1), ("braids", 1)]) if t.get("afro")
                 else pick(r, [("buzz", 4), ("short", 4), ("medium", 1), ("bald", 1)]))
        facial = pick(r, [("none", 6), ("stubble", 2.5), ("beard", 1), ("mustache", 0.5)])
    return {"skin": skin, "hairStyle": style, "hairColor": hair_color, "facialHair": facial, "confidence": "inferred"}


# ------------------------------------------------------------------------- таблицы веба (по фото)
ENTRY = re.compile(r'^\s*"(?P<name>[^"]+)"\s*:\s*\{(?P<body>.*)\}\s*,?\s*$')
FIELD = re.compile(r'(\w+)\s*:\s*("(?:[^"\\]|\\.)*"|-?\d+(?:\.\d+)?)')


def load_tables(web):
    out = {}
    for fn in ("amateurs.ts", "pros1.ts", "pros2.ts"):
        path = os.path.join(web, "src", "data", "appearance", fn)
        with open(path, encoding="utf-8") as f:
            for line in f:
                m = ENTRY.match(line)
                if not m:
                    continue
                rec = {}
                for k, v in FIELD.findall(m.group("body")):
                    rec[k] = json.loads(v) if v.startswith('"') else int(v)
                if "skin" in rec and m.group("name") not in out:   # как lookupAppearance: первая таблица главнее
                    out[m.group("name")] = rec
    return out


# Индивидуальный разброс внутри одного типа (у сборной КЗ почти у всех «тон 3, короткие чёрные»): свой хеш имени
# (НЕ ГСЧ боя и не хеш generateAppearance) → ±светлота/теплота кожи, длина коротких волос. Узнаваемо разные, но
# в пределах своего тона.
SKIN_JITTER_L = 0.07      # ± доля светлоты
SKIN_JITTER_WARM = 0.05   # ± тёплый/холодный (R вверх — B вниз)
HAIR_LEN_JITTER = (0.72, 1.0)


def look_for(boxer, app):
    """Готовые параметры рантайма UE для бойца."""
    female = boxer["gender"] == "F"
    h, w = float(boxer["heightCm"]), float(boxer["weightKg"])
    bmi, morph = body_morph(w, h)
    face_tex, body_tex = skin_textures(int(app["skin"]))
    jr = hash_rng("uelook:" + boxer["name"])
    lj = 1.0 + SKIN_JITTER_L * (2 * jr() - 1)
    wj = SKIN_JITTER_WARM * (2 * jr() - 1)
    skin_cc = [round(lj * (1 + wj), 3), round(lj, 3), round(lj * (1 - wj), 3)]
    len_j = HAIR_LEN_JITTER[0] + (HAIR_LEN_JITTER[1] - HAIR_LEN_JITTER[0]) * jr()
    hc = HAIR_COLOR.get(app.get("hairColor", "black"), HAIR_COLOR["black"])
    style = app.get("hairStyle", "short")
    straight, curly, length = HAIR_STYLE.get(style, HAIR_STYLE["short"])
    is_curly = int(app["skin"]) >= 5 and style in ("short", "afro", "braids", "dreads", "buzz", "medium", "mohawk")
    if female and style in FEMALE_HAIR:
        fs, fc = FEMALE_HAIR[style]
        straight, curly = fs or straight, fc or curly
    g = curly if is_curly else straight
    if g is WAVES:
        # волны «под машинку» — только у мужчин; у женщин полная длина (короткие волосы, не лысина)
        length = 0.38 if (style == "buzz" and not female) else (round(len_j, 3) if length >= 1.0 else length)
    elif g["groom"]:
        length = round(0.85 + 0.15 * (len_j - HAIR_LEN_JITTER[0]) / (HAIR_LEN_JITTER[1] - HAIR_LEN_JITTER[0]), 3)             if style in ("short", "medium") else 1.0
    hair = {"groom": g["groom"], "binding": g["binding"], "attach": g.get("attach"), "length": length, "melanin": hc["melanin"],
            "redness": hc["redness"], "white": hc["white"]}
    if "dye" in hc:
        hair["dye"] = list(hc["dye"])
    fg, stubble = FACIAL_HAIR.get("none" if female else app.get("facialHair", "none"), FACIAL_HAIR["none"])
    brows_mel = max(0.35, hc["melanin"]) if hc["white"] < 0.5 else hc["melanin"]
    look = {
        "female": female,
        "scale": round(max(SCALE_MIN, min(SCALE_MAX, h / MODEL_HEIGHT_CM)), 4),
        "bmi": round(bmi, 2),
        "morph": morph,
        "skinTone": int(app["skin"]),
        "skinFaceTex": face_tex,
        "skinBodyTex": body_tex,
        "skinCC": skin_cc,
        "hair": hair,
        "brows": {"melanin": round(brows_mel, 3), "redness": hc["redness"], "white": hc["white"]},
        "facial": {"groom": fg["groom"], "binding": fg["binding"], "attach": fg.get("attach"),
                   "stubble": STUBBLE if stubble else NONE,
                   "melanin": hc["melanin"], "redness": hc["redness"], "white": hc["white"]},
        "tattoos": app.get("tattoos", "none"),
    }
    return look


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:  # noqa
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--web", default=os.environ.get("BOXING_WEB", os.path.join(PROJECT, "..", "boxing", "web")))
    ap.add_argument("--roster", default=os.path.join(PROJECT, "Content", "Boxing", "Data", "Roster.json"))
    ap.add_argument("--out", default=os.path.join(PROJECT, "Content", "Boxing", "Data", "Appearance.json"))
    a = ap.parse_args()
    with open(a.roster, encoding="utf-8") as f:
        roster = json.load(f)["boxers"]
    tables = load_tables(a.web)
    out, n_photo, n_table, n_gen = [], 0, 0, 0
    for b in roster:
        app = tables.get(b["name"])
        if app:
            n_table += 1
            n_photo += app.get("confidence") == "photo"
        else:
            app = generate_appearance(b["name"], b["gender"], b.get("countryCode", ""))
            n_gen += 1
        app = {k: app[k] for k in ("skin", "hairStyle", "hairColor", "facialHair", "tattoos", "confidence", "source")
               if k in app}
        out.append({"id": b["id"], "name": b["name"], "gender": b["gender"], "countryCode": b.get("countryCode", ""),
                    "heightCm": b["heightCm"], "weightKg": b["weightKg"], "age": b.get("age"),
                    "appearance": app, "look": look_for(b, app)})
    doc = {
        "source": "boxing/web data/appearance (фото) + generateAppearance (страна/хеш имени); look — "
                  "Tools/EditorScripts/look_appearance.py (S-60)",
        "modelHeightCm": MODEL_HEIGHT_CM,
        "groomAttach": GROOM_ATTACH,
        "count": len(out),
        "boxers": out,
    }
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
    print("Appearance.json: %d бойцов (из таблиц веба %d, из них по фото %d; сгенерировано %d) → %s"
          % (len(out), n_table, n_photo, n_gen, a.out))


if __name__ == "__main__":
    sys.exit(main())
