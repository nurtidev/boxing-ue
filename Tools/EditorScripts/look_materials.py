# -*- coding: utf-8 -*-
# S-77: фактура материалов из CC0-сканов (Poly Haven / ambientCG, см. Docs/ASSETS.md и Docs/CREDITS.md) — кожа
# перчаток/шлема/боксёрок, атлас трусов, трикотаж маек, ткань одежды угловых и рефери, канвас ринга.
#
# Цепочка:
#   1. Blender: Tools/Blender/mat_textures.py → Saved/MatWork/T_*_NR.png (нормаль RG + нормированная шероховатость B),
#      T_Folds_N.png (мягкие складки)
#   2. UE (коммандлет):
#      UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#
# Что делает:
#   * импорт текстур в /Game/Boxing/Materials/Textures (T_*_NR — BC7 линейные, T_Folds_N — BC5 нормаль);
#   * мастер M_BoxerKit (форма, одежда, стул) и M_BoxingBase (статика ринга/зала) пересобираются тем же графом, что был,
#     плюс статический переключатель Detail (по умолчанию ВЫКЛ — остальные MI компилируются без текстур и не дорожают):
#       трипланар фактуры (M_BoxerKit — по ПРЕДСКИННОВОЙ локальной позиции/нормали: фактура «приклеена» к ткани и не
#       плывёт в анимации и не зависит от UV наших оболочек; M_BoxingBase — по мировым координатам, статика)
#       DetailTex (T_*_NR), DetailSize (см на тайл), NormalStrength, RoughVar (±шероховатость от скана вокруг Roughness),
#       FoldTex (T_Folds_N), FoldSize (см), FoldStrength — складки поверх фактуры;
#   * MI формы/одежды/ринга получают Detail = вкл и свои параметры (таблица KIT / RING ниже).
# Версия графа — метатег S77Graph на мастере; LOOKMAT_REBUILD=1 — пересобрать мастер принудительно.
# Маркер в логе: LOOKMAT
import json
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WORK = os.path.join(PROJECT, "Saved", "MatWork")
TEX_DIR = "/Game/Boxing/Materials/Textures"
KIT_MASTER = "/Game/Boxing/Characters/Materials/M_BoxerKit"
KIT_DIR = "/Game/Boxing/Characters/Materials"
BASE_MASTER = "/Game/Boxing/Materials/M_BoxingBase"
RING_DIR = "/Game/Boxing/Materials"
GRAPH_VERSION = "2"
REBUILD = os.environ.get("LOOKMAT_REBUILD", "0") == "1"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# MI формы: имя → (текстура, DetailSize см, NormalStrength, RoughVar, FoldStrength, FoldSize см)
KIT = {
    # кожа перчаток (новая гладкая: мелкое зерно, лёгкие заломы), шлем и подбивка — тоже кожа
    "Glove_Red": ("Leather", 9.0, 0.9, 0.22, 0.12, 22.0),
    "Glove_Blue": ("Leather", 9.0, 0.9, 0.22, 0.12, 22.0),
    "GloveTrim_Red": ("Leather", 9.0, 0.8, 0.2, 0.0, 22.0),
    "GloveTrim_Blue": ("Leather", 9.0, 0.8, 0.2, 0.0, 22.0),
    "GloveStrap_Red": ("Leather", 9.0, 0.8, 0.2, 0.0, 22.0),
    "GloveStrap_Blue": ("Leather", 9.0, 0.8, 0.2, 0.0, 22.0),
    "Kit_White": ("Leather", 9.0, 0.7, 0.18, 0.0, 22.0),
    "Headgear_Red": ("Leather", 10.0, 0.9, 0.22, 0.1, 25.0),
    "Headgear_Blue": ("Leather", 10.0, 0.9, 0.22, 0.1, 25.0),
    "HeadgearTrim_Red": ("Leather", 10.0, 0.8, 0.2, 0.0, 25.0),
    "HeadgearTrim_Blue": ("Leather", 10.0, 0.8, 0.2, 0.0, 25.0),
    "HeadgearStrap": ("Leather", 10.0, 0.8, 0.2, 0.0, 25.0),
    # боксёрки — мятая кожа с заломами
    "Kit_Boots": ("LeatherCreased", 18.0, 1.0, 0.3, 0.25, 18.0),
    # атлас трусов: тонкое плетение + складки
    "Trunks_Red": ("Satin", 8.0, 0.7, 0.3, 0.55, 28.0),
    "Trunks_Blue": ("Satin", 8.0, 0.7, 0.3, 0.55, 28.0),
    # майка любителя — спортивная сетка
    "Vest_Red": ("SportMesh", 7.0, 0.6, 0.25, 0.4, 30.0),
    "Vest_Blue": ("SportMesh", 7.0, 0.6, 0.25, 0.4, 30.0),
    # рефери: рубашка (поплин / трикотаж), брюки, бабочка, туфли
    "Ref_Shirt_Pro": ("Poplin", 9.0, 0.8, 0.2, 0.5, 30.0),
    "Ref_Shirt_Amateur": ("Jersey", 9.0, 0.8, 0.2, 0.5, 30.0),
    "Ref_Trousers_Pro": ("Poplin", 12.0, 0.7, 0.2, 0.45, 32.0),
    "Ref_Trousers_Amateur": ("Jersey", 12.0, 0.7, 0.2, 0.45, 32.0),
    "Ref_BowTie": ("Satin", 6.0, 0.6, 0.25, 0.0, 28.0),
    "Ref_Shoes_Pro": ("Leather", 10.0, 0.8, 0.25, 0.15, 18.0),
    "Ref_Shoes_Amateur": ("SportMesh", 6.0, 0.5, 0.2, 0.0, 18.0),
    # угловые: олимпийка/брюки — трикотаж «меланж», футболка — джерси, полотенце — махра, обувь — кожа
    "Crew_Jacket_Red": ("Fleece", 12.0, 0.8, 0.25, 0.5, 30.0),
    "Crew_Jacket_Blue": ("Fleece", 12.0, 0.8, 0.25, 0.5, 30.0),
    "Crew_Trousers": ("Fleece", 12.0, 0.8, 0.25, 0.5, 32.0),
    "Crew_Tee_Red": ("Jersey", 9.0, 0.8, 0.2, 0.5, 30.0),
    "Crew_Tee_Blue": ("Jersey", 9.0, 0.8, 0.2, 0.5, 30.0),
    "Crew_Towel": ("Terry", 10.0, 1.0, 0.3, 0.6, 20.0),
    "Crew_Shoes": ("Leather", 10.0, 0.8, 0.25, 0.15, 18.0),
    # стул углового: сиденье — кожзам
    "Stool_Seat_Red": ("Leather", 12.0, 0.7, 0.2, 0.0, 25.0),
    "Stool_Seat_Blue": ("Leather", 12.0, 0.7, 0.2, 0.0, 25.0),
}
# MI ринга (M_BoxingBase, мировой трипланар): канвас — грубое полотно, подушки/канаты — кожзам
RING = {
    "Canvas": ("Canvas", 30.0, 1.4, 0.45, 0.3, 140.0),
    "Canvas_Pro": ("Canvas", 30.0, 1.4, 0.45, 0.3, 140.0),
    "CanvasMark": ("Canvas", 30.0, 1.4, 0.45, 0.3, 140.0),
    "CanvasMark_Pro": ("Canvas", 30.0, 1.4, 0.45, 0.3, 140.0),
    "Skirt": ("Canvas", 45.0, 0.6, 0.25, 0.0, 160.0),
    "Skirt_Pro": ("Canvas", 45.0, 0.6, 0.25, 0.0, 160.0),
    "Pad_Red": ("LeatherCreased", 30.0, 0.8, 0.25, 0.0, 60.0),
    "Pad_Blue": ("LeatherCreased", 30.0, 0.8, 0.25, 0.0, 60.0),
    "Pad_White": ("LeatherCreased", 30.0, 0.8, 0.25, 0.0, 60.0),
    "Pad_Red_Pro": ("LeatherCreased", 30.0, 0.8, 0.25, 0.0, 60.0),
    "Pad_Blue_Pro": ("LeatherCreased", 30.0, 0.8, 0.25, 0.0, 60.0),
    "Pad_Neutral_Pro": ("LeatherCreased", 30.0, 0.8, 0.25, 0.0, 60.0),
    "Rope_Red": ("Leather", 12.0, 0.8, 0.2, 0.0, 60.0),
    "Rope_Blue": ("Leather", 12.0, 0.8, 0.2, 0.0, 60.0),
    "Rope_White": ("Leather", 12.0, 0.8, 0.2, 0.0, 60.0),
    "Rope_Low": ("Leather", 12.0, 0.8, 0.2, 0.0, 60.0),
}
TEXTURES = ["Leather", "LeatherCreased", "Satin", "Jersey", "Poplin", "Fleece", "Terry", "Canvas", "SportMesh"]


def log(msg):
    unreal.log("LOOKMAT " + str(msg))


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


# ------------------------------------------------------------------ текстуры
def import_texture(name, normal=False):
    path = "%s/%s" % (TEX_DIR, name)
    src = os.path.join(WORK, name + ".png")
    if eal.does_asset_exist(path) and not os.environ.get("LOOKMAT_REIMPORT"):
        return eal.load_asset(path)
    if not os.path.exists(src):
        raise RuntimeError("нет %s — сначала Tools/Blender/mat_textures.py" % src)
    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = TEX_DIR
    t.destination_name = name
    t.replace_existing = True
    t.automated = True
    t.save = False
    t.factory = unreal.TextureFactory()   # прямой импорт: Interchange в коммандлете синхронизирует Content Browser и падает
    asset_tools.import_asset_tasks([t])
    tex = eal.load_asset(path)
    tex.set_editor_property("srgb", False)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eal.save_loaded_asset(tex)
    log("импорт %s" % path)
    return tex


# ------------------------------------------------------------------ граф
class G:
    """Мелкие помощники построения графа материала."""

    def __init__(self, m):
        self.m = m

    def node(self, cls, x, y, **props):
        e = mel.create_material_expression(self.m, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, a, b, pin, out=""):
        mel.connect_material_expressions(a, out, b, pin)
        return b

    def sparam(self, name, val, x, y, group="Detail"):
        return self.node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=val, group=group)

    def vparam(self, name, val, x, y, group="Base"):
        return self.node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=val, group=group)

    def mul(self, a, b, x, y, a_out="", b_out=""):
        e = self.node(unreal.MaterialExpressionMultiply, x, y)
        mel.connect_material_expressions(a, a_out, e, "A")
        if isinstance(b, float):
            e.set_editor_property("const_b", b)
        else:
            mel.connect_material_expressions(b, b_out, e, "B")
        return e

    def add(self, a, b, x, y, a_out="", b_out=""):
        e = self.node(unreal.MaterialExpressionAdd, x, y)
        mel.connect_material_expressions(a, a_out, e, "A")
        if isinstance(b, float):
            e.set_editor_property("const_b", b)
        else:
            mel.connect_material_expressions(b, b_out, e, "B")
        return e

    def mask(self, a, rgba, x, y):
        e = self.node(unreal.MaterialExpressionComponentMask, x, y, r="R" in rgba, g="G" in rgba, b="B" in rgba, a="A" in rgba)
        mel.connect_material_expressions(a, "", e, "")
        return e


def triplanar(g, tex_obj, pos, nrm, size, x, y):
    """Трипланарная выборка: возвращает выражение RGB (смесь трёх проекций по весам |n|^4)."""
    p = g.node(unreal.MaterialExpressionDivide, x, y)
    g.link(pos, p, "A")
    g.link(size, p, "B")
    a = g.node(unreal.MaterialExpressionAbs, x, y + 160)
    g.link(nrm, a, "")
    w = g.node(unreal.MaterialExpressionPower, x + 150, y + 160, const_exponent=4.0)
    g.link(a, w, "Base")
    s = g.node(unreal.MaterialExpressionDotProduct, x + 300, y + 220)
    g.link(w, s, "A")
    one = g.node(unreal.MaterialExpressionConstant3Vector, x + 150, y + 260, constant=unreal.LinearColor(1, 1, 1, 1))
    g.link(one, s, "B")
    wn = g.node(unreal.MaterialExpressionDivide, x + 450, y + 160)
    g.link(w, wn, "A")
    g.link(s, wn, "B")
    acc = None
    for i, (uv_mask, w_mask) in enumerate((("GB", "R"), ("RB", "G"), ("RG", "B"))):
        uv = g.mask(p, uv_mask, x + 150, y - 120 + i * 60)
        smp = g.node(unreal.MaterialExpressionTextureSample, x + 300, y - 200 + i * 130,
                     sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        g.link(uv, smp, "UVs")
        g.link(tex_obj, smp, "Tex")
        wi = g.mask(wn, w_mask, x + 600, y + 160 + i * 50)
        term = g.mul(smp, wi, x + 750, y - 200 + i * 130)
        acc = term if acc is None else g.add(acc, term, x + 900, y - 200 + i * 130)
    return acc


def build_detail(g, base_rough, pos, nrm, default_tex, folds_tex, x, y):
    """Ветка Detail: возвращает (нормаль, шероховатость). Текстуры — объектами-параметрами (одна на три выборки)."""
    tex = g.node(unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name="DetailTex", texture=default_tex,
                 sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, group="Detail")
    ftex = g.node(unreal.MaterialExpressionTextureObjectParameter, x, y + 700, parameter_name="FoldTex", texture=folds_tex,
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, group="Detail")
    dsize = g.sparam("DetailSize", 10.0, x, y + 200)
    fsize = g.sparam("FoldSize", 30.0, x, y + 900)
    d = triplanar(g, tex, pos, nrm, dsize, x + 250, y)
    f = triplanar(g, ftex, pos, nrm, fsize, x + 250, y + 700)
    # фактура: rg*2−1 → xy × NormalStrength
    drg = g.mask(d, "RG", x + 1150, y)
    dxy = g.add(g.mul(drg, 2.0, x + 1300, y), -1.0, x + 1450, y)
    dxy = g.mul(dxy, g.sparam("NormalStrength", 1.0, x + 1300, y + 80), x + 1600, y)
    # складки: rg*2−1 (несжатый BC7-упаковкой не пользуемся — RGBA PNG) × FoldStrength
    frg = g.mask(f, "RG", x + 1150, y + 700)
    fxy = g.add(g.mul(frg, 2.0, x + 1300, y + 700), -1.0, x + 1450, y + 700)
    fxy = g.mul(fxy, g.sparam("FoldStrength", 0.0, x + 1300, y + 780), x + 1600, y + 700)
    xy = g.add(dxy, fxy, x + 1750, y + 300)
    dot = g.node(unreal.MaterialExpressionDotProduct, x + 1900, y + 400)
    g.link(xy, dot, "A")
    g.link(xy, dot, "B")
    one_minus = g.node(unreal.MaterialExpressionOneMinus, x + 2050, y + 400)
    g.link(dot, one_minus, "")
    sat = g.node(unreal.MaterialExpressionSaturate, x + 2200, y + 400)
    g.link(one_minus, sat, "")
    z = g.node(unreal.MaterialExpressionSquareRoot, x + 2350, y + 400)
    g.link(sat, z, "")
    n = g.node(unreal.MaterialExpressionAppendVector, x + 2500, y + 300)
    g.link(xy, n, "A")
    g.link(z, n, "B")
    # шероховатость: Roughness + (b − 0.5) × RoughVar
    db = g.mask(d, "B", x + 1150, y + 150)
    dev = g.mul(g.add(db, -0.5, x + 1300, y + 150), g.sparam("RoughVar", 0.25, x + 1300, y + 230), x + 1450, y + 150)
    r = g.add(base_rough, dev, x + 1600, y + 150)
    rs = g.node(unreal.MaterialExpressionSaturate, x + 1750, y + 150)
    g.link(r, rs, "")
    return n, rs


def detail_switches(g, normal, rough, base_rough, x, y):
    sw_n = g.node(unreal.MaterialExpressionStaticSwitchParameter, x, y, parameter_name="Detail", default_value=False, group="Detail")
    flat = g.node(unreal.MaterialExpressionConstant3Vector, x - 200, y + 80, constant=unreal.LinearColor(0, 0, 1, 1))
    g.link(normal, sw_n, "True")
    g.link(flat, sw_n, "False")
    sw_r = g.node(unreal.MaterialExpressionStaticSwitchParameter, x, y + 200, parameter_name="Detail", default_value=False, group="Detail")
    g.link(rough, sw_r, "True")
    g.link(base_rough, sw_r, "False")
    mel.connect_material_property(sw_n, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(sw_r, "", unreal.MaterialProperty.MP_ROUGHNESS)


def up_to_date(m):
    return not REBUILD and eal.get_metadata_tag(m, "S77Graph") == GRAPH_VERSION


def rebuild_kit_master(tex_default, folds):
    """M_BoxerKit: прежний граф (Color + Fresnel-блик Sheen, Roughness/Specular/Metallic) + Detail по предскинновой
    позиции (VertexInterpolator: позиция/нормаль привязки считаются в вершинном шейдере)."""
    m = eal.load_asset(KIT_MASTER)
    if up_to_date(m):
        log("M_BoxerKit актуален")
        return m
    mel.delete_all_material_expressions(m)
    g = G(m)
    col = g.vparam("Color", unreal.LinearColor(0.5, 0.5, 0.5, 1), -800, -400)
    sheen = g.sparam("Sheen", 0.0, -800, -260, group="Base")
    fr = g.node(unreal.MaterialExpressionFresnel, -800, -160)
    mul = g.mul(fr, sheen, -560, -200)
    white = g.node(unreal.MaterialExpressionConstant3Vector, -560, -320, constant=unreal.LinearColor(1, 1, 1, 1))
    lerp = g.node(unreal.MaterialExpressionLinearInterpolate, -360, -350)
    g.link(col, lerp, "A")
    g.link(white, lerp, "B")
    g.link(mul, lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = g.sparam("Roughness", 0.6, -360, -60, group="Base")
    mel.connect_material_property(g.sparam("Specular", 0.5, -360, 40, group="Base"), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(g.sparam("Metallic", 0.0, -360, 140, group="Base"), "", unreal.MaterialProperty.MP_METALLIC)
    vp = g.node(unreal.MaterialExpressionVertexInterpolator, -2600, 300)
    g.link(g.node(unreal.MaterialExpressionPreSkinnedPosition, -2800, 300), vp, "VS")
    vn = g.node(unreal.MaterialExpressionVertexInterpolator, -2600, 420)
    g.link(g.node(unreal.MaterialExpressionPreSkinnedNormal, -2800, 420), vn, "VS")
    n, r = build_detail(g, rough, vp, vn, tex_default, folds, -2400, 300)
    detail_switches(g, n, r, rough, 200, 200)
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("used_with_morph_targets", True)
    mel.recompile_material(m)
    eal.set_metadata_tag(m, "S77Graph", GRAPH_VERSION)
    eal.save_loaded_asset(m)
    log("M_BoxerKit пересобран (Detail, предскинновый трипланар)")
    return m


def rebuild_base_master(tex_default, folds):
    """M_BoxingBase: Color/Roughness/Metallic/Specular + Emissive (цвет × сила) + Detail по мировым координатам."""
    m = eal.load_asset(BASE_MASTER)
    if up_to_date(m):
        log("M_BoxingBase актуален")
        return m
    mel.delete_all_material_expressions(m)
    g = G(m)
    mel.connect_material_property(g.vparam("Color", unreal.LinearColor(0.5, 0.5, 0.5, 1), -600, -300), "",
                                  unreal.MaterialProperty.MP_BASE_COLOR)
    rough = g.sparam("Roughness", 0.8, -600, -100, group="Base")
    mel.connect_material_property(g.sparam("Metallic", 0.0, -600, 0, group="Base"), "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(g.sparam("Specular", 0.5, -600, 100, group="Base"), "", unreal.MaterialProperty.MP_SPECULAR)
    ecol = g.vparam("EmissiveColor", unreal.LinearColor(1, 1, 1, 1), -800, 250)
    epow = g.sparam("EmissiveStrength", 0.0, -800, 400, group="Base")
    mel.connect_material_property(g.mul(ecol, epow, -400, 300), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    wp = g.node(unreal.MaterialExpressionWorldPosition, -2800, 600)
    wn = g.node(unreal.MaterialExpressionVertexNormalWS, -2800, 720)
    n, r = build_detail(g, rough, wp, wn, tex_default, folds, -2400, 600)
    detail_switches(g, n, r, rough, 200, 400)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("used_with_nanite", True)
    mel.recompile_material(m)
    eal.set_metadata_tag(m, "S77Graph", GRAPH_VERSION)
    eal.save_loaded_asset(m)
    log("M_BoxingBase пересобран (Detail, мировой трипланар)")
    return m


def apply_detail(path, tex, size, strength, rvar, fold, fsize, folds):
    inst = eal.load_asset(path)
    if inst is None:
        log("нет %s — пропуск" % path)
        return False
    want = {"DetailSize": size, "NormalStrength": strength, "RoughVar": rvar, "FoldStrength": fold, "FoldSize": fsize}
    try:   # уже так — не пересохранять (файл может держать запущенная игра другой роли)
        same = mel.get_material_instance_static_switch_parameter_value(inst, "Detail") and             mel.get_material_instance_texture_parameter_value(inst, "DetailTex") == tex and             all(abs(mel.get_material_instance_scalar_parameter_value(inst, k) - float(v)) < 1e-4 for k, v in want.items())
    except Exception:  # noqa
        same = False
    if same:
        return True
    mel.set_material_instance_static_switch_parameter_value(inst, "Detail", True)
    mel.set_material_instance_texture_parameter_value(inst, "DetailTex", tex)
    mel.set_material_instance_texture_parameter_value(inst, "FoldTex", folds)
    for k, v in (("DetailSize", size), ("NormalStrength", strength), ("RoughVar", rvar), ("FoldStrength", fold),
                 ("FoldSize", fsize)):
        mel.set_material_instance_scalar_parameter_value(inst, k, float(v))
    mel.update_material_instance(inst)
    ok = eal.save_loaded_asset(inst)
    if not ok:
        log("НЕ СОХРАНЁН %s (файл занят игрой/редактором?)" % path)
    return ok


def textures():
    ensure_dir(TEX_DIR)
    tex = {n: import_texture("T_%s_NR" % n) for n in TEXTURES}
    folds = import_texture("T_Folds_N", normal=False)
    return tex, folds


def apply_ring(tex=None, folds=None):
    """Для build_ring.py: Detail на MI ринга (после того как mi() их создал/обновил)."""
    if tex is None:
        tex, folds = textures()
    rebuild_base_master(tex["Canvas"], folds)
    n = 0
    for name, (t, size, s, rv, fs, fz) in RING.items():
        n += apply_detail("%s/MI_%s" % (RING_DIR, name), tex[t], size, s, rv, fs, fz, folds)
    log("MI ринга с фактурой: %d" % n)


def main():
    tex, folds = textures()
    rebuild_kit_master(tex["Jersey"], folds)
    n = 0
    for name, (t, size, s, rv, fs, fz) in KIT.items():
        n += apply_detail("%s/MI_%s" % (KIT_DIR, name), tex[t], size, s, rv, fs, fz, folds)
    log("MI формы с фактурой: %d из %d" % (n, len(KIT)))
    if os.environ.get("LOOKMAT_RING", "1") == "1":
        apply_ring(tex, folds)
    stats = os.path.join(WORK, "mat_textures.json")
    if os.path.exists(stats):
        with open(stats, encoding="utf-8") as f:
            log("статистика сканов: %s" % json.dumps(json.load(f), ensure_ascii=False))
    log("готово")


if __name__ == "__main__":
    main()
