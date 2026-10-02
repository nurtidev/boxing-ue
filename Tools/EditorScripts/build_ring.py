# -*- coding: utf-8 -*-
# S-41, трек A «Ринг и арена»: собирает уровень /Game/Boxing/Maps/L_Ring СКРИПТОМ.
# Повторяемо: повторный запуск очищает уровень и строит его заново (материалы-инстансы
# обновляются по месту, базовый материал создаётся один раз).
#
# Запуск (headless, без рендера):
#   "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
#     C:\Users\user\Desktop\boxing-ue\BoxingUE.uproject -run=pythonscript ^
#     -script=C:\Users\user\Desktop\boxing-ue\Tools\EditorScripts\build_ring.py -unattended -nosplash -nullrhi
# Лог: Saved/Logs/BoxingUE.log, маркеры строк — «RING».
# Первый запуск ~4 мин (генерация шрифта с кириллицей F_ArenaText), повторные — секунды.
# Скриншоты: Tools/EditorScripts/ring_shots.py (см. его шапку) → Docs/screens/ring_*.png.
# Маркеры: TargetPoint RedStart/BlueStart (X = ∓57.5, лицом друг к другу), Corner_Red/Blue/NeutralA/B
# (±263, ±263 — CORNER_SPOT), PlayerStart_Red/Blue (тег Red/Blue). GameMode уровню НЕ назначается.
#
# Размеры — из web/src/engine/ringSize.ts (метры → сантиметры UE).
# Оси: web (x, y-вверх, z) → UE (X = x, Y = z, Z = y). Знаки углов сохраняются:
#   красный угол web (−x, −z) → UE (−X, −Y); синий (+x, +z) → (+X, +Y);
#   нейтральные (белые) — (+X, −Y) и (−X, +Y).
# (web — правая СК с Y вверх, UE — левая с Z вверх: картинка зеркальна относительно
#  web, но все знаки/расстояния совпадают — движок боя считает в тех же (x, z).)
# Поверхность канваса — Z = 0 (бойцы стоят на Z = 0), пол арены — Z = −110.
#
# S-64 (производительность и оформление, см. Docs/PERF.md):
#   * статика ЗАЛА (стены, пол, трибуны, кресла, публика, ферма, столы, табло) — Nanite-копии базовых фигур
#     (/Game/Boxing/Environment/SM_Nanite*): VSM кэширует страницы, сотни non-Nanite проходов теней ушли; ряды
#     трибун — ISM. РИНГ (канаты/столбы/подушки/стяжки) — обычные меши отдельными акторами: контроллер боя
#     прячет их у камеры по меткам Ring_Rope*/Ring_Post*/Ring_Pad* (SetRenderInMainPass).
#   * тени: VSM отбрасывает только верхний ТВ-ключ (было 17 теневых — переполнение VSM OnePassProjection, 7–14 мс
#     GPU на маске теней). 4 заполняющих с балконов и 12 прожекторов фермы светят (и дают лучи в тумане) без теней;
#     тень бойца на канвасе — от ключа. Контактные тени у заполняющих (RING_FILL_CONTACT) дороги: ~1.4 мс на 1080p.
#   * Lumen final gather quality 1.0 (Epic по умолчанию; 2.0 — «кино», +2.5 мс GPU на 1080p).
#   * Sky Light — без real-time capture (в закрытом зале нет неба; real-time требует SkyAtmosphere, иначе
#     предупреждение на экране); снимок сцены — при загрузке уровня.
#   * два оформления: любители (тег ArenaAmateur, видимо по умолчанию) и профи (тег ArenaPro, hidden in game).
#     Переключение — по тегам (API для GameMode — Docs/PERF.md «Оформление профи»):
#       SetActorHiddenInGame(bPro) у ArenaAmateur, SetActorHiddenInGame(!bPro) у ArenaPro.
#     Канвас профи — без коллизии (пол даёт канвас любителей, он и скрытым держит коллизию).
# Переключатели сборки (окружение): RING_NANITE=0 — без Nanite; RING_TRUSS_SHADOWS=1 — тени у фермы;
#   RING_FILL_SHADOWS=1 — VSM-тени у заполняющих; RING_FILL_CONTACT=<длина контактных теней>;
#   RING_LUMEN_FG=<качество final gather>.
import math
import os
import random
import unreal

# ---------------------------------------------------------------- размеры (см)
ROPE_HALF = 305.0                     # ringSize.ts ROPE_HALF 3.05
ROPE_HEIGHTS = [41.0, 71.0, 102.0, 132.0]
APRON = 60.0
PLATFORM_H = 110.0
FLOOR = -PLATFORM_H                   # пол арены
EDGE = ROPE_HALF + APRON              # край канваса/помоста
POST_OFF = 12.0                       # столбы чуть за линией канатов
POST_TOP = 150.0
POST_R = 5.5
PAD_LO, PAD_HI, PAD_R = 28.0, 144.0, 12.0
ROPE_R = 2.2
CORNER_SPOT = ROPE_HALF - 42.0        # corners.ts CORNER_SPOT = ROPE_HALF − 0.42
START_HALF = 57.5                     # ±DIST_START/2 на оси X

# арена (Arena.tsx)
BOARD = 740.0
STAND0 = 840.0
ROWS = 7
ROW_D = 85.0
ROW_H = 42.0
SEAT_W = 62.0
TRUSS_Z = 580.0
TRUSS_HALF = 370.0
WALL = 1850.0
HALL_H = 1700.0
CEIL = FLOOR + HALL_H

TITLE = u"ЧЕМПИОНАТ ПО БОКСУ"
# оформление профи: нейтральные надписи, без логотипов реальных организаций
TITLE_PRO_SKIRT = u"ПРОФЕССИОНАЛЬНЫЙ БОКС"
TITLE_PRO = u"ВЕЧЕР БОКСА"
TAG_AM, TAG_PRO = "ArenaAmateur", "ArenaPro"

USE_NANITE = os.environ.get("RING_NANITE", "1") != "0"
TRUSS_SHADOWS = os.environ.get("RING_TRUSS_SHADOWS", "0") == "1"
FILL_SHADOWS = os.environ.get("RING_FILL_SHADOWS", "0") == "1"
FILL_CONTACT = float(os.environ.get("RING_FILL_CONTACT", "0"))   # контактные тени заполняющих (доля экрана): 0.05 стоит ~1.4 мс GPU
LUMEN_FG = float(os.environ.get("RING_LUMEN_FG", "1.0"))
# прожекторы фермы: 12 источников с конусом 22° (каждый источник на ринге — проход освещения по пикселям своего пятна;
# с конусом 30° пятно — весь ринг, ~1 мс GPU на 1080p). RING_TRUSS_LIGHTS=4 — светит средний на сторону ×3 (ещё
# −0.5 мс, но лучей в тумане 4 вместо 12).
TRUSS_LIGHTS = int(os.environ.get("RING_TRUSS_LIGHTS", "12"))
TRUSS_CONE = float(os.environ.get("RING_TRUSS_CONE", "22"))     # внешний конус при 12 источниках (было 30: пятно света — экран ринга целиком, 12 полноэкранных проходов)

MAP_PATH = "/Game/Boxing/Maps/L_Ring"
MAT_DIR = "/Game/Boxing/Materials"
BASE_MAT = MAT_DIR + "/M_BoxingBase"

CUBE = "/Engine/BasicShapes/Cube.Cube"          # 100 см, центр в середине
CYL = "/Engine/BasicShapes/Cylinder.Cylinder"   # Ø100 × 100 по Z, центр в середине
SPHERE = "/Engine/BasicShapes/Sphere.Sphere"    # Ø100

SIDES = [((0, -1), (1, 0)), ((1, 0), (0, 1)), ((0, 1), (-1, 0)), ((-1, 0), (0, -1))]  # (нормаль, касательная) в UE XY


def log(msg):
    unreal.log("RING " + msg)


def srgb(hexstr, a=1.0):
    """'#rrggbb' (sRGB) → линейный LinearColor."""
    h = hexstr.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return unreal.LinearColor(out[0], out[1], out[2], a)


asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
actors_ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


# ---------------------------------------------------------------- материалы
def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


def build_base_material():
    """M_BoxingBase: Color/Roughness/Metallic/Specular + Emissive (цвет × сила)."""
    if eal.does_asset_exist(BASE_MAT):
        m = eal.load_asset(BASE_MAT)
        if not m.get_editor_property("used_with_nanite"):   # S-64: статика зала — Nanite
            m.set_editor_property("used_with_nanite", True)
            mel.recompile_material(m)
            eal.save_loaded_asset(m)
            log("M_BoxingBase: used_with_nanite")
        return m
    m = asset_tools.create_asset("M_BoxingBase", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    def vparam(name, val, x, y):
        e = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", val)
        return e

    def sparam(name, val, x, y):
        e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", val)
        return e

    col = vparam("Color", unreal.LinearColor(0.5, 0.5, 0.5, 1), -600, -200)
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = sparam("Roughness", 0.8, -600, 0)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = sparam("Metallic", 0.0, -600, 100)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    spec = sparam("Specular", 0.5, -600, 200)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    ecol = vparam("EmissiveColor", unreal.LinearColor(1, 1, 1, 1), -800, 350)
    epow = sparam("EmissiveStrength", 0.0, -800, 500)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 400)
    mel.connect_material_expressions(ecol, "", mul, "A")
    mel.connect_material_expressions(epow, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("used_with_nanite", True)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("создан базовый материал " + BASE_MAT)
    return m


ENV_DIR = "/Game/Boxing/Environment"
FONT_PATH = ENV_DIR + "/F_ArenaText"
TEXT_MAT = MAT_DIR + "/M_BoxingText"


def build_font():
    """Офлайн-шрифт с кириллицей (движковый RobotoDistanceField — только Latin-1, 256 символов).
    Генерируется из системного Arial Bold (distance field) один раз — это медленно (~минуты)."""
    if eal.does_asset_exist(FONT_PATH):
        return eal.load_asset(FONT_PATH)
    f = unreal.TrueTypeFontFactory()
    opts = f.get_editor_property("import_options")
    d = opts.get_editor_property("data")
    d.set_editor_property("font_name", "Arial")
    d.set_editor_property("height", 64.0)
    d.set_editor_property("enable_bold", True)
    d.set_editor_property("use_distance_field_alpha", True)
    d.set_editor_property("unicode_range", "0020-007E,00AB,00BB,0401,0410-044F,0451,2014")
    d.set_editor_property("texture_page_width", 1024)
    d.set_editor_property("texture_page_max_height", 1024)
    opts.set_editor_property("data", d)
    f.set_editor_property("import_options", opts)
    font = asset_tools.create_asset("F_ArenaText", ENV_DIR, unreal.Font, f)
    eal.save_loaded_asset(font)
    log("создан шрифт " + FONT_PATH)
    return font


def build_text_material(font):
    """M_BoxingText: неосвещённый текст (цвет вершин TextRender × Strength), маска — альфа шрифта."""
    if eal.does_asset_exist(TEXT_MAT):
        return eal.load_asset(TEXT_MAT)
    m = asset_tools.create_asset("M_BoxingText", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    fs = mel.create_material_expression(m, unreal.MaterialExpressionFontSampleParameter, -700, 200)
    fs.set_editor_property("parameter_name", "Font")
    fs.set_editor_property("font", font)
    fs.set_editor_property("font_texture_page", 0)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -700, -100)
    st = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 50)
    st.set_editor_property("parameter_name", "Strength")
    st.set_editor_property("default_value", 2.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 0)
    mel.connect_material_expressions(vc, "", mul, "A")
    mel.connect_material_expressions(st, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(fs, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("создан материал текста " + TEXT_MAT)
    return m


def mi_unchanged(inst, parent, vecs, scals):
    """Инстанс уже такой (родитель и параметры) — не трогать и не пересохранять: файлы MI держит запущенная
    игра/редактор другой роли (сохранение падает «MoveFile … Error Code 32»)."""
    try:
        if inst.get_editor_property("parent") != parent:
            return False
        for n, v in vecs.items():
            c = mel.get_material_instance_vector_parameter_value(inst, n)
            if max(abs(c.r - v.r), abs(c.g - v.g), abs(c.b - v.b)) > 1e-4:
                return False
        for n, v in scals.items():
            if abs(mel.get_material_instance_scalar_parameter_value(inst, n) - v) > 1e-4:
                return False
        return True
    except Exception:  # noqa
        return False


def load_or_create_mi(name):
    path = "%s/MI_%s" % (MAT_DIR, name)
    if eal.does_asset_exist(path):
        return eal.load_asset(path), False
    return asset_tools.create_asset("MI_" + name, MAT_DIR, unreal.MaterialInstanceConstant,
                                    unreal.MaterialInstanceConstantFactoryNew()), True


def text_mi(name, strength):
    inst, new = load_or_create_mi(name)
    if not new and mi_unchanged(inst, TEXTM, {}, {"Strength": strength}):
        return inst
    mel.set_material_instance_parent(inst, TEXTM)
    mel.set_material_instance_scalar_parameter_value(inst, "Strength", strength)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


def mi(name, color, rough=0.8, metal=0.0, spec=0.5, emissive=None, estrength=0.0):
    """Материал-инстанс /Game/Boxing/Materials/MI_<name> (создаётся или обновляется)."""
    inst, new = load_or_create_mi(name)
    vecs = {"Color": srgb(color)}
    if emissive:
        vecs["EmissiveColor"] = srgb(emissive)
    if not new and mi_unchanged(inst, BASE, vecs, {"Roughness": rough, "Metallic": metal, "Specular": spec,
                                                   "EmissiveStrength": estrength}):
        return inst
    mel.set_material_instance_parent(inst, BASE)
    mel.set_material_instance_vector_parameter_value(inst, "Color", srgb(color))
    mel.set_material_instance_scalar_parameter_value(inst, "Roughness", rough)
    mel.set_material_instance_scalar_parameter_value(inst, "Metallic", metal)
    mel.set_material_instance_scalar_parameter_value(inst, "Specular", spec)
    if emissive:
        mel.set_material_instance_vector_parameter_value(inst, "EmissiveColor", srgb(emissive))
    mel.set_material_instance_scalar_parameter_value(inst, "EmissiveStrength", estrength)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


# ---------------------------------------------------------------- уровень
def open_or_create_level():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if eal.does_asset_exist(MAP_PATH):
        les.load_level(MAP_PATH)
        n = 0
        for a in actors_ss.get_all_level_actors():
            if isinstance(a, unreal.WorldSettings):
                continue
            if a.get_class().get_name() in ("Brush", "DefaultPhysicsVolume"):
                continue
            actors_ss.destroy_actor(a)
            n += 1
        log("уровень открыт, удалено акторов: %d" % n)
    else:
        les.new_level(MAP_PATH)
        log("создан новый уровень " + MAP_PATH)


MESHES = {}


def mesh(path):
    if path not in MESHES:
        MESHES[path] = unreal.load_asset(path)
    return MESHES[path]


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def R(pitch=0.0, yaw=0.0, roll=0.0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def nanite_copy(src, name):
    """Копия движковой фигуры с Nanite в /Game/Boxing/Environment (сам движковый ассет не трогаем)."""
    if not USE_NANITE:
        return src
    path = "%s/%s" % (ENV_DIR, name)
    if not eal.does_asset_exist(path):
        eal.duplicate_asset(src.split(".")[0], path)
    sm = eal.load_asset(path)
    try:
        ns = sm.get_editor_property("nanite_settings")
        if not ns.get_editor_property("enabled"):
            ns.set_editor_property("enabled", True)
            sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            if sub is not None:
                sub.set_nanite_settings(sm, ns, True)
            else:  # коммандлет: подсистемы редактора нет; set_editor_property сам зовёт PostEditChange → пересборка
                sm.set_editor_property("nanite_settings", ns)
            eal.save_loaded_asset(sm)
            log("Nanite включён: " + path)
    except Exception as e:  # noqa
        log("Nanite для %s не включился (%s) — обычный меш" % (path, e))
    return path


def add_tags(a, *tags):
    a.tags = list(a.tags) + [unreal.Name(t) for t in tags]
    return a


def variant(a, pro):
    """Актор одного из оформлений: профи по умолчанию скрыт в игре (GameMode показывает по Rounds > 3)."""
    if a is None:
        return None
    add_tags(a, TAG_PRO if pro else TAG_AM)
    if pro:
        a.set_actor_hidden_in_game(True)
    return a


def place(label, mesh_path, mat, loc, size=None, rot=None, folder="Ring", shadows=True, scale=None):
    """StaticMeshActor из базового меша. size — габарит в см (для 100-см базовых фигур)."""
    a = actors_ss.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot or R())
    a.set_actor_label(label)
    a.set_folder_path(folder)
    smc = a.static_mesh_component
    smc.set_static_mesh(mesh(mesh_path))
    smc.set_material(0, mat)
    smc.set_editor_property("cast_shadow", shadows)
    if size is not None:
        a.set_actor_scale3d(V(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    elif scale is not None:
        a.set_actor_scale3d(scale)
    return a


def box(label, mat, center, size, yaw=0.0, folder="Ring", shadows=True, mesh_path=None):
    return place(label, mesh_path or CUBE, mat, V(*center), size=size, rot=R(yaw=yaw), folder=folder, shadows=shadows)


def nbox(label, mat, center, size, yaw=0.0, folder="Arena", shadows=True):
    """Статика зала — Nanite-куб."""
    return box(label, mat, center, size, yaw=yaw, folder=folder, shadows=shadows, mesh_path=NCUBE)


SDS = None


def ism(label, mesh_path, mat, transforms, folder="Arena", shadows=False):
    """Один актор с InstancedStaticMeshComponent на все экземпляры (через SubobjectDataSubsystem)."""
    global SDS
    if not transforms:
        return None
    a = actors_ss.spawn_actor_from_class(unreal.StaticMeshActor, V(0, 0, 0), R())
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    try:
        if SDS is None:
            SDS = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = SDS.k2_gather_subobject_data_for_instance(a)
        params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.InstancedStaticMeshComponent)
        h, fail = SDS.add_new_subobject(params)
        comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
        comp.set_static_mesh(mesh(mesh_path))
        comp.set_material(0, mat)
        comp.set_editor_property("cast_shadow", shadows)
        comp.add_instances(transforms, False, False)
        return a
    except Exception as e:  # запасной путь: отдельные акторы
        log("ISM недоступен (%s) — %s отдельными акторами" % (e, label))
        actors_ss.destroy_actor(a)
        for i, t in enumerate(transforms):
            b = place("%s_%d" % (label, i), mesh_path, mat, t.translation, rot=t.rotation.rotator(),
                      folder=folder, shadows=shadows, scale=t.scale3d)
        return None


def T(loc, size=(100, 100, 100), yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Transform(V(*loc), R(pitch, yaw, roll), V(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))


def text(label, s, loc, yaw, size, color, folder="Arena", mat=None):
    """TextRender лицом по yaw (текст читается со стороны +X актора)."""
    a = actors_ss.spawn_actor_from_class(unreal.TextRenderActor, V(*loc), R(yaw=yaw))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    tc = a.text_render
    tc.set_font(FONT)
    tc.set_text_material(mat or TXT_PRINT)
    tc.set_editor_property("cast_shadow", False)
    tc.set_text(s)
    tc.set_editor_property("world_size", size)
    tc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    tc.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
    tc.set_editor_property("text_render_color", color)
    return a


# ================================================================ сборка
log("старт сборки")
ensure_dir("/Game/Boxing/Maps")
ensure_dir(MAT_DIR)
ensure_dir("/Game/Boxing/Environment")
BASE = build_base_material()
FONT = build_font()
TEXTM = build_text_material(FONT)
TXT_PRINT = text_mi("Text_Print", 60.0)    # надписи на юбке/бортах (печать, чуть светится)
TXT_SCREEN = text_mi("Text_Screen", 500.0)  # табло (светящийся экран)

M = {
    "canvas": mi("Canvas", "#8ea3c2", rough=0.95, spec=0.3),
    "canvas_ring": mi("CanvasMark", "#6f84a6", rough=0.95, spec=0.3),
    "skirt": mi("Skirt", "#141a2a", rough=0.7),
    "skirt_band": mi("SkirtBand", "#1d3f8f", rough=0.6),
    "rope_white": mi("Rope_White", "#eef1f5", rough=0.55),
    "rope_low": mi("Rope_Low", "#c9ced6", rough=0.6),
    "rope_red": mi("Rope_Red", "#c01822", rough=0.55),
    "rope_blue": mi("Rope_Blue", "#1a3fb0", rough=0.55),
    "post": mi("Post", "#9aa4b2", rough=0.35, metal=0.9),
    "pad_red": mi("Pad_Red", "#c01822", rough=0.45, spec=0.6),
    "pad_blue": mi("Pad_Blue", "#1a3fb0", rough=0.45, spec=0.6),
    "pad_white": mi("Pad_White", "#f2f4f7", rough=0.45, spec=0.6),
    "steps": mi("Steps", "#2a3346", rough=0.7),
    "floor": mi("ArenaFloor", "#15171c", rough=0.6),
    "field": mi("Field", "#1a2a52", rough=0.8),
    "boards": mi("Boards", "#0f1d44", rough=0.6),
    "wall": mi("Wall", "#0d1119", rough=0.9),
    "tier_a": mi("Tier_A", "#1a1e28", rough=0.85),
    "tier_b": mi("Tier_B", "#151821", rough=0.85),
    "seat_blue": mi("Seat_Blue", "#13295f", rough=0.6),
    "seat_red": mi("Seat_Red", "#5e1820", rough=0.6),
    "table": mi("JudgeTable", "#16254f", rough=0.7),
    "table_top": mi("JudgeTableTop", "#e9edf3", rough=0.5),
    "truss": mi("Truss", "#2a2e36", rough=0.45, metal=0.8),
    "can": mi("LightCan", "#1b1e24", rough=0.5, metal=0.6),
    "lens": mi("LightLens", "#fff6e0", rough=0.2, emissive="#fff1d6", estrength=3000.0),
    "screen": mi("Screen", "#05070c", rough=0.3, emissive="#16337a", estrength=600.0),
    "screen_frame": mi("ScreenFrame", "#0a0b0e", rough=0.5, metal=0.5),
    # --- оформление профи: графитовый канвас с золотым кругом, чёрная юбка с золотой полосой, подушки глубже
    # тоном (нейтральные — чёрные), борта/кресла — бордо, табло — тёмно-красное с золотыми надписями
    "canvas_pro": mi("Canvas_Pro", "#3a3d44", rough=0.9, spec=0.35),
    "canvas_ring_pro": mi("CanvasMark_Pro", "#b8922e", rough=0.6, spec=0.5),
    "skirt_pro": mi("Skirt_Pro", "#0b0b0d", rough=0.55),
    "skirt_band_pro": mi("SkirtBand_Pro", "#b8922e", rough=0.35, metal=0.6),
    "pad_red_pro": mi("Pad_Red_Pro", "#9a0c16", rough=0.3, spec=0.7),
    "pad_blue_pro": mi("Pad_Blue_Pro", "#0f2a7a", rough=0.3, spec=0.7),
    "pad_white_pro": mi("Pad_Neutral_Pro", "#18181b", rough=0.3, spec=0.7),
    "field_pro": mi("Field_Pro", "#221214", rough=0.8),
    "boards_pro": mi("Boards_Pro", "#3d0b12", rough=0.55),
    "seat_pro": mi("Seat_Pro", "#4a0d16", rough=0.6),
    "screen_pro": mi("Screen_Pro", "#05070c", rough=0.3, emissive="#6a1018", estrength=600.0),
}
SHIRTS = ["#e9e9e9", "#1c1c1f", "#2b2f3a", "#b3242c", "#2346a6", "#3a6fd1", "#6c7686", "#d9c9a3",
          "#1f6b4a", "#e0b42a", "#8a1d2b", "#394a73", "#503a2c", "#00a3c4", "#c45a1c"]
SKINS = ["#e8c3a0", "#d9aa82", "#c68f66", "#a8734e", "#7a5236", "#f0cfb0"]
M_SHIRT = [mi("Crowd_Shirt%02d" % i, c, rough=0.85) for i, c in enumerate(SHIRTS)]
M_SKIN = [mi("Crowd_Skin%d" % i, c, rough=0.6) for i, c in enumerate(SKINS)]
M_JUDGE = mi("Crowd_Judge", "#f3f4f6", rough=0.8)
M_JURY = mi("Crowd_Jury", "#9fc1e6", rough=0.8)
log("материалы готовы")

NCUBE = nanite_copy(CUBE, "SM_NaniteCube")
NCYL = nanite_copy(CYL, "SM_NaniteCylinder")
NSPHERE = nanite_copy(SPHERE, "SM_NaniteSphere")

open_or_create_level()


def no_collision(a):
    a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return a


# ---------------------------------------------------------------- ринг
H = ROPE_HALF
SKIRT_TXT = unreal.Color(r=235, g=238, b=245, a=255)
GOLD_TXT = unreal.Color(r=232, g=196, b=110, a=255)
for pro, sfx in ((False, ""), (True, "_Pro")):
    k = "_pro" if pro else ""
    c = variant(box("Ring_Canvas" + sfx, M["canvas" + k], (0, 0, -5), (EDGE * 2 + 4, EDGE * 2 + 4, 10)), pro)
    if pro:
        no_collision(c)
    variant(box("Ring_Skirt" + sfx, M["skirt" + k], (0, 0, FLOOR + (PLATFORM_H - 10) / 2), (EDGE * 2, EDGE * 2, PLATFORM_H - 10)), pro)
    # полоса вдоль верха юбки (любители — синяя, профи — золотая)
    variant(box("Ring_SkirtBand" + sfx, M["skirt_band" + k], (0, 0, -22), (EDGE * 2 + 2, EDGE * 2 + 2, 16)), pro)
    # центральное кольцо на канвасе (тонкий диск поверх диска цвета канваса)
    for lbl, mk, d, z in (("Ring_CenterMark", "canvas_ring", 186, 0.15), ("Ring_CenterMarkIn", "canvas", 170, 0.2)):
        a = variant(place(lbl + sfx, CYL, M[mk + k], V(0, 0, z), size=(d, d, z * 2), shadows=False), pro)
        no_collision(a)
    # надписи по 4 сторонам юбки
    for i, ((nx, ny), _t) in enumerate(SIDES):
        yaw = math.degrees(math.atan2(ny, nx))
        variant(text("Ring_SkirtTitle%s_%d" % (sfx, i), TITLE_PRO_SKIRT if pro else TITLE,
                     (nx * (EDGE + 1.5), ny * (EDGE + 1.5), FLOOR + 55), yaw, 24 if pro else 26,
                     GOLD_TXT if pro else SKIRT_TXT, folder="Ring"), pro)

CORNERS = [((-1, -1), "Red", "pad_red"), ((1, 1), "Blue", "pad_blue"),
           ((1, -1), "NeutralA", "pad_white"), ((-1, 1), "NeutralB", "pad_white")]
for (sx, sy), name, pad in CORNERS:
    px, py = sx * (H + POST_OFF), sy * (H + POST_OFF)
    place("Ring_Post_" + name, CYL, M["post"], V(px, py, POST_TOP / 2), size=(POST_R * 2, POST_R * 2, POST_TOP))
    for pro, sfx in ((False, ""), (True, "_Pro")):
        variant(place("Ring_Pad_" + name + sfx, CYL, M[pad + ("_pro" if pro else "")],
                      V(sx * (H + POST_OFF * 0.4), sy * (H + POST_OFF * 0.4), (PAD_LO + PAD_HI) / 2),
                      size=(PAD_R * 2, PAD_R * 2, PAD_HI - PAD_LO)), pro)
    # шайба-«набалдашник» столба
    place("Ring_PostCap_" + name, CYL, M["post"], V(px, py, POST_TOP + 2), size=(POST_R * 2.6, POST_R * 2.6, 4))
    # растяжки к краю апрона (тросы в угол)
    place("Ring_PostBase_" + name, CUBE, M["post"], V(px, py, 2), size=(22, 22, 4))

# канаты: 4 высоты × 4 стороны; цилиндр Ø100×100 по Z → кладём на бок (pitch 90 вдоль X)
ROPE_LEN = 2 * (H + POST_OFF)
for k, z in enumerate(ROPE_HEIGHTS):
    mat = M["rope_low"] if k == 0 else M["rope_white"]
    for sy in (-1, 1):
        place("Ring_Rope_%d_Y%+d" % (k, sy), CYL, mat, V(0, sy * H, z), size=(ROPE_R * 2, ROPE_R * 2, ROPE_LEN), rot=R(pitch=90))
    for sx in (-1, 1):
        place("Ring_Rope_%d_X%+d" % (k, sx), CYL, mat, V(sx * H, 0, z), size=(ROPE_R * 2, ROPE_R * 2, ROPE_LEN), rot=R(pitch=90, yaw=90))
# вертикальные стяжки канатов (по 2 на сторону) — цвет углов на ближних к ним
for (nx, ny), (tx, ty) in SIDES:
    for u in (-H / 3, H / 3):
        x, y = nx * H + tx * u, ny * H + ty * u
        place("Ring_RopeTie_%d_%d" % (round(x), round(y)), CUBE, M["rope_white"], V(x, y, (ROPE_HEIGHTS[0] + ROPE_HEIGHTS[-1]) / 2),
              size=(2.2, 2.2, ROPE_HEIGHTS[-1] - ROPE_HEIGHTS[0] + 4), shadows=False)

# ступени: 4 ступени у красного угла (сторона −Y) и у синего (+Y)
n_steps = 4
rise = PLATFORM_H / n_steps
for sgn, name in ((-1, "Red"), (1, "Blue")):
    for k in range(n_steps):
        hgt = rise * (k + 1)
        depth = 32.0
        y = sgn * (EDGE + 16 + (n_steps - 1 - k) * depth)
        box("Ring_Step_%s_%d" % (name, k), M["steps"], (sgn * (H - 55), y, FLOOR + hgt / 2), (90, depth, hgt))
log("ринг готов")

# ---------------------------------------------------------------- маркеры
def target_point(label, loc, yaw, tags):
    a = actors_ss.spawn_actor_from_class(unreal.TargetPoint, V(*loc), R(yaw=yaw))
    a.set_actor_label(label)
    a.set_folder_path("Markers")
    a.tags = [unreal.Name(t) for t in tags]
    return a


target_point("RedStart", (-START_HALF, 0, 0), 0.0, ["RedStart", "Red", "FightStart"])
target_point("BlueStart", (START_HALF, 0, 0), 180.0, ["BlueStart", "Blue", "FightStart"])
for (sx, sy), name, _p in CORNERS:
    yaw = math.degrees(math.atan2(-sy, -sx))  # лицом к центру ринга
    target_point("Corner_" + name, (sx * CORNER_SPOT, sy * CORNER_SPOT, 0), yaw, ["Corner", "Corner_" + name])
# PlayerStart'ы (для игры по умолчанию; капсула стоит на канвасе → центр на +92)
for label, x, yaw, tag in (("PlayerStart_Red", -START_HALF, 0.0, "Red"), ("PlayerStart_Blue", START_HALF, 180.0, "Blue")):
    ps = actors_ss.spawn_actor_from_class(unreal.PlayerStart, V(x, 0, 92), R(yaw=yaw))
    ps.set_actor_label(label)
    ps.set_folder_path("Markers")
    ps.set_editor_property("player_start_tag", tag)
    ps.tags = [unreal.Name(tag)]

# ---------------------------------------------------------------- зал
nbox("Arena_Floor", M["floor"], (0, 0, FLOOR - 10), (WALL * 2, WALL * 2, 20))
variant(nbox("Arena_Field", M["field"], (0, 0, FLOOR + 0.5), (BOARD * 2, BOARD * 2, 1)), False)
no_collision(variant(nbox("Arena_Field_Pro", M["field_pro"], (0, 0, FLOOR + 0.5), (BOARD * 2, BOARD * 2, 1)), True))
nbox("Arena_Ceiling", M["wall"], (0, 0, CEIL + 10), (WALL * 2, WALL * 2, 20))
for (nx, ny), (tx, ty) in SIDES:
    along_x = abs(tx) > 0.5
    sz = (WALL * 2, 20, HALL_H) if along_x else (20, WALL * 2, HALL_H)
    nbox("Arena_Wall_%+d%+d" % (nx, ny), M["wall"], (nx * (WALL + 10), ny * (WALL + 10), FLOOR + HALL_H / 2), sz)
    # борта поля + надпись (два оформления)
    bsz = (BOARD * 2 - 240, 8, 90) if along_x else (8, BOARD * 2 - 240, 90)
    yaw_in = math.degrees(math.atan2(-ny, -nx))  # надпись к рингу
    for pro, sfx in ((False, ""), (True, "_Pro")):
        b = variant(nbox("Arena_Board%s_%+d%+d" % (sfx, nx, ny), M["boards_pro" if pro else "boards"],
                         (nx * BOARD, ny * BOARD, FLOOR + 45), bsz), pro)
        if pro:
            no_collision(b)
        variant(text("Arena_BoardTitle%s_%+d%+d" % (sfx, nx, ny), TITLE_PRO if pro else TITLE,
                     (nx * (BOARD - 5), ny * (BOARD - 5), FLOOR + 45), yaw_in, 34,
                     GOLD_TXT if pro else unreal.Color(r=230, g=236, b=250, a=255)), pro)

# трибуны, кресла, публика (детерминированно, свой RNG — как в web)
rng = random.Random(2026 ^ 0x51a7d5)
tiers, seats_b, seats_r = [], [], []
bodies = {i: [] for i in range(len(SHIRTS))}
heads = {i: [] for i in range(len(SKINS))}
for (nx, ny), (tx, ty) in SIDES:
    yaw = math.degrees(math.atan2(-ny, -nx))  # лицом к рингу
    along_x = abs(tx) > 0.5
    for r in range(ROWS):
        d = STAND0 + r * ROW_D
        top = FLOOR + ROW_H * (r + 1)
        length = 2 * d - 320
        cx, cy = nx * (d + ROW_D / 2), ny * (d + ROW_D / 2)
        size = (length, ROW_D, top - FLOOR) if along_x else (ROW_D, length, top - FLOOR)
        tiers.append((r % 2, T((cx, cy, (FLOOR + top) / 2), size)))
        n_seats = int(length // SEAT_W)
        for k in range(n_seats):
            u = (k - (n_seats - 1) / 2.0) * SEAT_W
            if abs(u) < 45:
                continue
            sx = nx * (d + ROW_D - 12) + tx * u
            sy = ny * (d + ROW_D - 12) + ty * u
            # спинка кресла: ширина вдоль ряда, тонкая к рингу
            seat = T((sx, sy, top + 22), (SEAT_W * 0.78, 10, 44), yaw=yaw + 90)
            (seats_r if rng.random() < 0.12 else seats_b).append(seat)
            centre = 1 - min(1.0, abs(u) / (length / 2))
            fill = 0.42 + 0.4 * centre - r * 0.02
            if rng.random() > fill:
                continue
            px = nx * (d + ROW_D * 0.52) + tx * (u + rng.uniform(-6, 6))
            py = ny * (d + ROW_D * 0.52) + ty * (u + rng.uniform(-6, 6))
            s = rng.uniform(0.9, 1.08)
            pyaw = yaw + rng.uniform(-14, 14)
            standing = rng.random() < 0.08
            zc = top + (36 + (30 if standing else 0)) * s
            bodies[rng.randrange(len(SHIRTS))].append(T((px, py, zc), (42 * s, 26 * s, (62 + (30 if standing else 0)) * s), yaw=pyaw + 90))
            heads[rng.randrange(len(SKINS))].append(T((px, py, zc + (43 + (15 if standing else 0)) * s), (21 * s, 21 * s, 24 * s), yaw=pyaw))
ism("Arena_Tiers_A", NCUBE, M["tier_a"], [t for o, t in tiers if o == 0], folder="Arena/Stands", shadows=True)
ism("Arena_Tiers_B", NCUBE, M["tier_b"], [t for o, t in tiers if o == 1], folder="Arena/Stands", shadows=True)
variant(ism("Arena_Tiers_SeatsBlue", NCUBE, M["seat_blue"], seats_b, folder="Arena/Stands"), False)
variant(ism("Arena_Tiers_SeatsRed", NCUBE, M["seat_red"], seats_r, folder="Arena/Stands"), False)
variant(ism("Arena_Tiers_SeatsMain_Pro", NCUBE, M["seat_pro"], seats_b, folder="Arena/Stands"), True)
variant(ism("Arena_Tiers_SeatsAccent_Pro", NCUBE, M["pad_white_pro"], seats_r, folder="Arena/Stands"), True)
n_people = 0
for i, lst in bodies.items():
    ism("Crowd_Body_%02d" % i, NCYL, M_SHIRT[i], lst, folder="Arena/Crowd")
    n_people += len(lst)
for i, lst in heads.items():
    ism("Crowd_Head_%d" % i, NSPHERE, M_SKIN[i], lst, folder="Arena/Crowd")
log("публика: %d человек, кресел %d" % (n_people, len(seats_b) + len(seats_r)))

# судьи и жюри у помоста
tD, pD = EDGE + 100, EDGE + 155
judges, jury, judge_heads = [], [], []


def officials(n, t, u, w, who, lst):
    nx, ny = n
    tx, ty = t
    yaw = math.degrees(math.atan2(-ny, -nx))
    along_x = abs(tx) > 0.5
    cx, cy = nx * tD + tx * u, ny * tD + ty * u
    nbox("Arena_Table_%d_%d" % (round(cx), round(cy)), M["table"], (cx, cy, FLOOR + 39), (w, 62, 78) if along_x else (62, w, 78), folder="Arena/Officials")
    nbox("Arena_TableTop_%d_%d" % (round(cx), round(cy)), M["table_top"], (cx, cy, FLOOR + 78.5), (w + 4, 66, 2) if along_x else (66, w + 4, 2), folder="Arena/Officials")
    for i in range(who):
        v = u + (0 if who == 1 else (i - (who - 1) / 2.0) * 80)
        px, py = nx * pD + tx * v, ny * pD + ty * v
        lst.append(T((px, py, FLOOR + 78), (42, 26, 62), yaw=yaw + 90))
        judge_heads.append(T((px, py, FLOOR + 121), (21, 21, 24), yaw=yaw))


officials((1, 0), (0, 1), 0, 90, 1, judges)
officials((-1, 0), (0, -1), 0, 90, 1, judges)
officials((0, 1), (-1, 0), 0, 90, 1, judges)
officials((0, -1), (1, 0), -230, 90, 1, judges)
officials((0, -1), (1, 0), 230, 90, 1, judges)
officials((0, -1), (1, 0), 0, 200, 2, jury)
ism("Officials_Judges", NCYL, M_JUDGE, judges, folder="Arena/Officials")
ism("Officials_Jury", NCYL, M_JURY, jury, folder="Arena/Officials")
ism("Officials_Heads", NSPHERE, M_SKIN[1], judge_heads, folder="Arena/Officials")

# табло на торцевых стенах (±Y)
for sgn in (-1, 1):
    y = sgn * (WALL - 6)
    yaw_in = 90.0 if sgn < 0 else -90.0
    nbox("Arena_ScreenFrame_%+d" % sgn, M["screen_frame"], (0, y, FLOOR + 1070), (1000, 10, 460), folder="Arena/Screens")
    ty = y - sgn * 10
    for pro, sfx in ((False, ""), (True, "_Pro")):
        no_collision(variant(nbox("Arena_Screen%s_%+d" % (sfx, sgn), M["screen_pro" if pro else "screen"],
                                  (0, y - sgn * 6, FLOOR + 1070), (960, 4, 420), folder="Arena/Screens", shadows=False), pro))
        variant(text("Arena_ScreenTitle%s_%+d" % (sfx, sgn), TITLE_PRO if pro else TITLE, (0, ty, FLOOR + 1210), yaw_in, 60,
                     GOLD_TXT if pro else unreal.Color(r=255, g=255, b=255, a=255), folder="Arena/Screens", mat=TXT_SCREEN), pro)
    text("Arena_ScreenRed_%+d" % sgn, u"КРАСНЫЙ УГОЛ", (-250, ty, FLOOR + 1050), yaw_in, 46, unreal.Color(r=255, g=70, b=70, a=255), folder="Arena/Screens", mat=TXT_SCREEN)
    text("Arena_ScreenBlue_%+d" % sgn, u"СИНИЙ УГОЛ", (250, ty, FLOOR + 1050), yaw_in, 46, unreal.Color(r=90, g=140, b=255, a=255), folder="Arena/Screens", mat=TXT_SCREEN)
    text("Arena_ScreenRound_%+d" % sgn, u"РАУНД 1   3:00", (0, ty, FLOOR + 930), yaw_in, 50, unreal.Color(r=255, g=214, b=90, a=255), folder="Arena/Screens", mat=TXT_SCREEN)
log("зал готов")

# ---------------------------------------------------------------- ферма и свет
TL = TRUSS_HALF * 2 + 36
for (nx, ny), (tx, ty) in SIDES:
    along_x = abs(tx) > 0.5
    cx, cy = nx * TRUSS_HALF, ny * TRUSS_HALF
    nbox("Truss_Beam_%+d%+d" % (nx, ny), M["truss"], (cx, cy, TRUSS_Z), (TL, 34, 34) if along_x else (34, TL, 34), folder="Lights/Truss")
    for u in (-TRUSS_HALF, TRUSS_HALF):
        hx, hy = cx + tx * u, cy + ty * u
        nbox("Truss_Hanger_%d_%d" % (round(hx), round(hy)), M["truss"], (hx, hy, (TRUSS_Z + CEIL) / 2), (3, 3, CEIL - TRUSS_Z), folder="Lights/Truss", shadows=False)

cans, lenses = [], []
spot_i = 0
for (nx, ny), (tx, ty) in SIDES:
    cx, cy = nx * TRUSS_HALF, ny * TRUSS_HALF
    for u in (-220, 0, 220):
        lx, ly, lz = cx + tx * u, cy + ty * u, TRUSS_Z - 36
        # цель — канвас ближе к центру: лучи сходятся на ринг
        gx, gy, gz = lx * 0.3, ly * 0.3, 60.0
        dx, dy, dz = gx - lx, gy - ly, gz - lz
        yaw = math.degrees(math.atan2(dy, dx))
        pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
        # корпус прожектора: цилиндр вдоль Z → ось на цель (pitch + 90)
        cans.append(T((lx, ly, lz), (36, 36, 34), yaw=yaw, pitch=pitch - 90))
        ln = math.sqrt(dx * dx + dy * dy + dz * dz)
        ux, uy, uz = dx / ln, dy / ln, dz / ln
        lenses.append(T((lx + ux * 18, ly + uy * 18, lz + uz * 18), (32, 32, 2), yaw=yaw, pitch=pitch - 90))
        if TRUSS_LIGHTS == 4 and u != 0:
            continue    # S-64: светит средний прожектор стороны за троих (корпуса и линзы — все 12)
        k3 = 3.0 if TRUSS_LIGHTS == 4 else 1.0
        sp = actors_ss.spawn_actor_from_class(unreal.SpotLight, V(lx + ux * 22, ly + uy * 22, lz + uz * 22), R(pitch=pitch, yaw=yaw))
        sp.set_actor_label("Light_TrussSpot_%02d" % spot_i)
        sp.set_folder_path("Lights")
        lc = sp.spot_light_component
        lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        lc.set_editor_property("intensity", 9000.0 * k3)
        lc.set_editor_property("light_color", unreal.Color(r=255, g=244, b=226, a=255))
        lc.set_editor_property("attenuation_radius", 1600.0)
        lc.set_editor_property("inner_cone_angle", 20.0 if k3 > 1 else min(14.0, TRUSS_CONE * 0.5))
        lc.set_editor_property("outer_cone_angle", 36.0 if k3 > 1 else TRUSS_CONE)
        lc.set_editor_property("source_radius", 14.0)
        lc.set_editor_property("soft_source_radius", 20.0)
        lc.set_editor_property("cast_shadows", TRUSS_SHADOWS)
        lc.set_editor_property("volumetric_scattering_intensity", 4.0 / k3 * 1.4 if k3 > 1 else 4.0)
        lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        spot_i += 1
ism("Truss_Cans", NCYL, M["can"], cans, folder="Lights/Truss")
ism("Truss_Lenses", NCYL, M["lens"], lenses, folder="Lights/Truss")

# «верхний» телевизионный ключ над центром (как spotLight в Arena.tsx)
top = actors_ss.spawn_actor_from_class(unreal.SpotLight, V(0, 0, 1100), R(pitch=-90))
top.set_actor_label("Light_TopKey")
top.set_folder_path("Lights")
lc = top.spot_light_component
lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
lc.set_editor_property("intensity", 30000.0)
lc.set_editor_property("light_color", unreal.Color(r=255, g=244, b=226, a=255))
lc.set_editor_property("attenuation_radius", 2000.0)
lc.set_editor_property("inner_cone_angle", 16.0)
lc.set_editor_property("outer_cone_angle", 24.0)
lc.set_editor_property("source_radius", 40.0)
lc.set_editor_property("cast_shadows", True)
lc.set_editor_property("volumetric_scattering_intensity", 1.0)

def aim(frm, to):
    dx, dy, dz = to[0] - frm[0], to[1] - frm[1], to[2] - frm[2]
    return R(pitch=math.degrees(math.atan2(dz, math.hypot(dx, dy))), yaw=math.degrees(math.atan2(dy, dx)))


def spot(label, frm, to, cd, inner, outer, color, shadows, vol, folder, radius=3500.0, src=10.0):
    sp = actors_ss.spawn_actor_from_class(unreal.SpotLight, V(*frm), aim(frm, to))
    sp.set_actor_label(label)
    sp.set_folder_path(folder)
    lc = sp.spot_light_component
    lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    lc.set_editor_property("intensity", cd)
    lc.set_editor_property("light_color", color)
    lc.set_editor_property("attenuation_radius", radius)
    lc.set_editor_property("inner_cone_angle", inner)
    lc.set_editor_property("outer_cone_angle", outer)
    lc.set_editor_property("source_radius", src)
    lc.set_editor_property("cast_shadows", shadows)
    lc.set_editor_property("volumetric_scattering_intensity", vol)
    return sp


# свет трибун (приглушённый «зал»): широкие споты с потолка на каждую сторону
WARM = unreal.Color(r=255, g=222, b=186, a=255)
for (nx, ny), (tx, ty) in SIDES:
    # S-64: один широкий спот на сторону вместо трёх (пиксель трибун освещали 2–3 перекрытых источника)
    for u in (0,):
        frm = (nx * 650 + tx * u, ny * 650 + ty * u, CEIL - 150)
        to = (nx * (STAND0 + 300) + tx * u, ny * (STAND0 + 300) + ty * u, FLOOR + 160)
        spot("Light_Stands_%+d%+d_%d" % (nx, ny, u), frm, to, 21000.0, 45.0, 72.0, WARM, False, 0.0, "Lights/Stands")
# «заполняющий» ТВ-свет на ринг с балконов (по диагоналям): освещает внешние стороны подушек/канатов
for sx, sy in ((-1, -1), (1, 1), (1, -1), (-1, 1)):
    frm = (sx * 1300, sy * 1300, 900)
    f = spot("Light_RingFill_%+d%+d" % (sx, sy), frm, (0, 0, 60), 45000.0, 12.0, 20.0,
             unreal.Color(r=245, g=246, b=255, a=255), FILL_SHADOWS, 0.6, "Lights", radius=4000.0, src=30.0)
    f.spot_light_component.set_editor_property("contact_shadow_length", FILL_CONTACT)

sky = actors_ss.spawn_actor_from_class(unreal.SkyLight, V(0, 0, CEIL - 300), R())
sky.set_actor_label("SkyLight")
sky.set_folder_path("Lights")
slc = sky.light_component
slc.set_editor_property("intensity", 0.15)
slc.set_editor_property("lower_hemisphere_is_black", False)
slc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
# real-time capture требует SkyAtmosphere/VolumetricCloud/IsSky (иначе предупреждение на экране и чёрный снимок);
# в закрытом зале неба нет — снимок сцены один раз при загрузке (Lumen берёт его как «небо» для рассеянного света).
slc.set_editor_property("real_time_capture", False)
slc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)

fog = actors_ss.spawn_actor_from_class(unreal.ExponentialHeightFog, V(0, 0, FLOOR), R())
fog.set_actor_label("HallFog")
fog.set_folder_path("Lights")
fc = fog.component
fc.set_editor_property("fog_density", 0.035)
fc.set_editor_property("fog_height_falloff", 0.02)
fc.set_editor_property("fog_inscattering_luminance", srgb("#05070c"))
fc.set_editor_property("enable_volumetric_fog", True)
fc.set_editor_property("volumetric_fog_scattering_distribution", 0.6)
fc.set_editor_property("volumetric_fog_extinction_scale", 3.0)
fc.set_editor_property("volumetric_fog_albedo", unreal.Color(r=255, g=255, b=255, a=255))
fc.set_editor_property("volumetric_fog_distance", 4000.0)

ppv = actors_ss.spawn_actor_from_class(unreal.PostProcessVolume, V(0, 0, 0), R())
ppv.set_actor_label("PostProcess")
ppv.set_folder_path("Lights")
ppv.set_editor_property("unbound", True)
ps = ppv.settings
# фиксированная экспозиция: гистограмма с min = max (EV100) — не «дышит» между кадрами
ps.set_editor_property("override_auto_exposure_method", True)
ps.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
EV100 = 8.5
ps.set_editor_property("override_auto_exposure_min_brightness", True)
ps.set_editor_property("auto_exposure_min_brightness", EV100)
ps.set_editor_property("override_auto_exposure_max_brightness", True)
ps.set_editor_property("auto_exposure_max_brightness", EV100)
ps.set_editor_property("override_auto_exposure_bias", True)
ps.set_editor_property("auto_exposure_bias", 0.0)
ps.set_editor_property("override_bloom_intensity", True)
ps.set_editor_property("bloom_intensity", 0.35)
ps.set_editor_property("override_vignette_intensity", True)
ps.set_editor_property("vignette_intensity", 0.45)
ps.set_editor_property("override_dynamic_global_illumination_method", True)
ps.set_editor_property("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.LUMEN)
ps.set_editor_property("override_reflection_method", True)
ps.set_editor_property("reflection_method", unreal.ReflectionMethod.LUMEN)
ps.set_editor_property("override_lumen_final_gather_quality", True)
ps.set_editor_property("lumen_final_gather_quality", LUMEN_FG)
ppv.settings = ps
log("свет готов: прожекторов %d" % spot_i)

# ---------------------------------------------------------------- камеры
def look_rot(frm, to):
    dx, dy, dz = to[0] - frm[0], to[1] - frm[1], to[2] - frm[2]
    return R(pitch=math.degrees(math.atan2(dz, math.hypot(dx, dy))), yaw=math.degrees(math.atan2(dy, dx)))


def cine(label, frm, to, focal):
    c = actors_ss.spawn_actor_from_class(unreal.CineCameraActor, V(*frm), look_rot(frm, to))
    c.set_actor_label(label)
    c.set_folder_path("Cameras")
    cc = c.get_cine_camera_component()
    cc.set_editor_property("current_focal_length", focal)
    fs = cc.focus_settings
    fs.set_editor_property("focus_method", unreal.CameraFocusMethod.DISABLE)
    cc.set_editor_property("focus_settings", fs)
    return c


cine("PreviewCam", (-1150, -1300, 520), (0, 0, 40), 20.0)
cine("SideCam", (0, -290, 165), (0, 300, 125), 20.0)  # сбоку, на высоте глаз бойца у канатов
cine("ArenaCam", (0, 1000, 330), (0, -1850, 420), 18.0)  # через ринг на табло и трибуны

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
log("уровень сохранён: " + MAP_PATH)
log("ГОТОВО")
