# -*- coding: utf-8 -*-
# S-77: публика зала — импорт людей MakeHuman (Tools/Blender/crowd_people.py → Saved/CrowdWork) и материал M_Crowd.
#
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#
# Создаёт в /Game/Boxing/Environment/Crowd (в git — производные MakeHuman CC0):
#   SM_Crowd_<вариант>  — статичные меши (Nanite, полноточные UV — в UV0.U смещение позы «болеют» по Z), без коллизии;
#   T_CrowdPalette      — палитра 16×8 (кожа, верх, низ, волосы, обувь), без мипов, nearest;
#   MPC_Crowd           — скаляр Excite 0..1: реакция зала (нокдаун/нокаут/финал). C++/BP: UKismetMaterialLibrary::
#                         SetScalarParameterValue(World, MPC_Crowd, "Excite", v) — зал встаёт волной (разброс по
#                         экземплярам), качает руками, подпрыгивает; 0 — сидят и покачиваются;
#   M_Crowd + MI_Crowd_Sit / MI_Crowd_Stand / MI_Crowd_Judge / MI_Crowd_Jury — окраска по палитре и PerInstanceRandom,
#                         WPO: покачивание/дыхание + поза «болеют» × Excite.
# Маркер лога: CROWDIMP. CROWDIMP_MAT_ONLY=1 — только материал. CROWDIMP_PROBE=1 — только вывести значения цвета вершин (калибровка зон).
import json
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WORK = os.path.join(PROJECT, "Saved", "CrowdWork")
DIR = "/Game/Boxing/Environment/Crowd"
MPC_PATH = DIR + "/MPC_Crowd"
MAT_PATH = DIR + "/M_Crowd"
VARIANTS = ["M_Sit_A", "M_Sit_B", "M_Sit_C", "M_Sit_D", "F_Sit_A", "F_Sit_B", "M_Stand", "F_Stand"]
# зоны в цвете вершин (R) так, как их видит материал Nanite-меша: линейные значения из Blender (0, .25, .5, .75, 1).
# Импорт кодирует FColor в sRGB (StaticMeshBuilder ToFColor(true)), Nanite отдаёт материалу декодированное —
# проверено снимком (с sRGB-уровнями верх одежды выходил чёрным, а низ брал палитру верха).
ZONES = [0.0, 0.25, 0.5, 0.75, 1.0]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(m):
    unreal.log("CROWDIMP " + str(m))


def import_mesh(name):
    src = os.path.join(WORK, "SM_Crowd_%s.fbx" % name)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    sd = ui.get_editor_property("static_mesh_import_data")
    sd.set_editor_property("combine_meshes", True)
    sd.set_editor_property("auto_generate_collision", False)
    sd.set_editor_property("generate_lightmap_u_vs", False)
    sd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    sd.set_editor_property("build_nanite", True)
    sd.set_editor_property("convert_scene", True)
    sd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = DIR
    t.destination_name = "SM_Crowd_" + name
    t.replace_existing = True
    t.automated = True
    t.save = False
    t.options = ui
    asset_tools.import_asset_tasks([t])
    sm = eal.load_asset("%s/SM_Crowd_%s" % (DIR, name))
    if not sm:
        raise RuntimeError("импорт не удался: " + src)
    # UV — полная точность (в них смещение позы «болеют» в метрах), Nanite, без коллизии/лайтмапы
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary   # в коммандлете подсистемы нет
    bs = sub.get_lod_build_settings(sm, 0)
    bs.set_editor_property("use_full_precision_u_vs", True)
    bs.set_editor_property("generate_lightmap_u_vs", False)
    sub.set_lod_build_settings(sm, 0, bs)
    ns = sm.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", True)
    if hasattr(sub, "set_nanite_settings"):
        sub.set_nanite_settings(sm, ns, True)
    else:
        sm.set_editor_property("nanite_settings", ns)
    b = sm.get_bounds()
    log("меш %s: bounds o=(%.0f %.0f %.0f) e=(%.0f %.0f %.0f), треуг. %d" % (
        name, b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z,
        sub.get_number_verts(sm, 0)))
    eal.save_loaded_asset(sm)
    return sm


def probe(sm):
    """Значения R/G цвета вершин после импорта (какими их видит материал) — для калибровки ZONES."""
    try:
        desc = sm.get_static_mesh_description(0)
        n = desc.get_vertex_instance_count()
        rs, gs = {}, []
        for i in range(0, n, max(1, n // 4000)):
            c = desc.get_vertex_instance_attribute_vector4(unreal.VertexInstanceID(i), "VertexInstanceColor") \
                if hasattr(desc, "get_vertex_instance_attribute_vector4") else desc.get_vertex_instance_color(unreal.VertexInstanceID(i))
            r = round(float(c.x if hasattr(c, "x") else c.r), 3)
            rs[r] = rs.get(r, 0) + 1
            gs.append(float(c.y if hasattr(c, "y") else c.g))
        log("probe %s: R (линейно в описании меша) %s; G среднее %.3f" % (sm.get_name(), sorted(rs.items())[:12],
                                                                              sum(gs) / max(1, len(gs))))
    except Exception as e:  # noqa
        log("probe %s: %s" % (sm.get_name(), e))


def import_palette():
    path = DIR + "/T_CrowdPalette"
    t = unreal.AssetImportTask()
    t.filename = os.path.join(WORK, "T_CrowdPalette.png")
    t.destination_path = DIR
    t.destination_name = "T_CrowdPalette"
    t.replace_existing = True
    t.automated = True
    t.save = False
    t.factory = unreal.TextureFactory()
    asset_tools.import_asset_tasks([t])
    tex = eal.load_asset(path)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    eal.save_loaded_asset(tex)
    return tex


def make_mpc():
    if eal.does_asset_exist(MPC_PATH):
        return eal.load_asset(MPC_PATH)
    mpc = asset_tools.create_asset("MPC_Crowd", DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    p = unreal.CollectionScalarParameter()
    p.set_editor_property("parameter_name", "Excite")
    p.set_editor_property("default_value", 0.0)
    mpc.set_editor_property("scalar_parameters", [p])
    eal.save_loaded_asset(mpc)
    log("создан MPC_Crowd")
    return mpc


class G:
    def __init__(self, m):
        self.m = m

    def n(self, cls, x, y, **props):
        e = mel.create_material_expression(self.m, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def c(self, a, b, pin, out=""):
        mel.connect_material_expressions(a, out, b, pin)
        return b

    def op(self, cls, a, b, x, y, a_out="", b_out=""):
        e = self.n(cls, x, y)
        if isinstance(a, float):
            e.set_editor_property("const_a", a)
        else:
            mel.connect_material_expressions(a, a_out, e, "A")
        if isinstance(b, float):
            e.set_editor_property("const_b", b)
        else:
            mel.connect_material_expressions(b, b_out, e, "B")
        return e

    def un(self, cls, a, x, y, a_out=""):
        e = self.n(cls, x, y)
        mel.connect_material_expressions(a, a_out, e, "")
        return e

    def s(self, name, val, x, y, group="Crowd"):
        return self.n(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=val, group=group)


def build_material(palette, mpc):
    m = eal.load_asset(MAT_PATH) if eal.does_asset_exist(MAT_PATH) else \
        asset_tools.create_asset("M_Crowd", DIR, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    g = G(m)
    vc = g.n(unreal.MaterialExpressionVertexColor, -2600, 0)
    rnd = g.n(unreal.MaterialExpressionPerInstanceRandom, -2600, 300)
    # --- цвет: зона (R) → строка палитры, оттенок — хеш PerInstanceRandom
    color = None
    hashes = [1.0, 7.13, 13.7, 29.3, 53.1]
    for zi, level in enumerate(ZONES):
        y = -900 + zi * 260
        h = g.un(unreal.MaterialExpressionFrac, g.op(unreal.MaterialExpressionMultiply, rnd, hashes[zi], -2400, y), -2250, y)
        u = g.op(unreal.MaterialExpressionAdd, g.un(unreal.MaterialExpressionFloor, g.op(unreal.MaterialExpressionMultiply, h, 16.0, -2100, y), -1950, y),
                 0.5, -1800, y)
        u = g.op(unreal.MaterialExpressionDivide, u, 16.0, -1650, y)
        v = g.n(unreal.MaterialExpressionConstant, -1650, y + 80, r=1.0 - (zi + 0.5) / 8.0)
        uv = g.op(unreal.MaterialExpressionAppendVector, u, v, -1500, y)
        smp = g.n(unreal.MaterialExpressionTextureSample, -1350, y, texture=palette,
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        g.c(uv, smp, "UVs")
        col = smp
        if zi == 1:   # верх: у судей/жюри — форма (статический переключатель Uniform + UniformColor)
            uni = g.n(unreal.MaterialExpressionVectorParameter, -1350, y + 150, parameter_name="UniformColor",
                      default_value=unreal.LinearColor(0.9, 0.9, 0.9, 1), group="Crowd")
            sw = g.n(unreal.MaterialExpressionStaticSwitchParameter, -1150, y, parameter_name="Uniform", default_value=False, group="Crowd")
            g.c(uni, sw, "True")
            g.c(smp, sw, "False")
            col = sw
        # маска зоны: 1 − |R − level| × 6 (уровни через 0.25)
        d = g.un(unreal.MaterialExpressionAbs, g.op(unreal.MaterialExpressionSubtract, vc, level, -1350, y + 200, a_out="R"), -1200, y + 200)
        mask = g.un(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, 1.0, g.op(unreal.MaterialExpressionMultiply, d, 6.0, -1050, y + 200), -900, y + 200), -750, y + 200)
        term = g.op(unreal.MaterialExpressionMultiply, col, mask, -600, y)
        color = term if color is None else g.op(unreal.MaterialExpressionAdd, color, term, -450, y)
    # деталь: G (линейно), 0.5 = 1×
    det = g.op(unreal.MaterialExpressionMultiply, vc, 2.0, -1050, 600, a_out="G")
    det = g.op(unreal.MaterialExpressionMultiply, det, g.s("DetailGain", 1.0, -1200, 700), -900, 600)
    base = g.op(unreal.MaterialExpressionMultiply, color, det, -300, 0)
    base = g.op(unreal.MaterialExpressionMultiply, base, g.s("Brightness", 1.0, -450, 120), -150, 0)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # шероховатость: кожа 0.55, остальное 0.8
    skin = g.un(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, 1.0, g.op(unreal.MaterialExpressionMultiply, vc, 6.0, -600, 800, a_out="R"), -450, 800), -300, 800)
    rough = g.n(unreal.MaterialExpressionLinearInterpolate, -150, 800, const_a=0.82, const_b=0.55)
    g.c(skin, rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.n(unreal.MaterialExpressionConstant, -150, 900, r=0.35), "", unreal.MaterialProperty.MP_SPECULAR)

    # --- WPO (локальные оси экземпляра, см)
    lp = g.n(unreal.MaterialExpressionLocalPosition, -2600, 1200)
    lpz = g.un(unreal.MaterialExpressionComponentMask, lp, -2500, 1250)
    lpz.set_editor_property("r", False)
    lpz.set_editor_property("g", False)
    lpz.set_editor_property("b", True)
    hgt = g.un(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionDivide, g.op(unreal.MaterialExpressionAdd, lpz, 45.0, -2400, 1200), 140.0, -2250, 1200), -2100, 1200)
    tm = g.n(unreal.MaterialExpressionTime, -2600, 1400)
    ph = g.op(unreal.MaterialExpressionMultiply, rnd, 61.3, -2400, 1450)
    t = g.op(unreal.MaterialExpressionAdd, tm, ph, -2250, 1400)

    def wave(freq, x, y):
        return g.un(unreal.MaterialExpressionSine, g.op(unreal.MaterialExpressionMultiply, t, freq, x, y), x + 150, y)
    # Sine в UE — период 1 (не 2π): частоты в Гц
    sway_x = g.op(unreal.MaterialExpressionMultiply, wave(0.13, -2100, 1400), g.s("SwayAmp", 1.6, -2100, 1480), -1800, 1400)
    sway_y = g.op(unreal.MaterialExpressionMultiply, wave(0.09, -2100, 1550), g.s("SwayAmpSide", 1.2, -2100, 1630), -1800, 1550)
    breath = g.op(unreal.MaterialExpressionMultiply, wave(0.27, -2100, 1700), 0.35, -1800, 1700)
    sway = g.op(unreal.MaterialExpressionAppendVector, g.op(unreal.MaterialExpressionAppendVector, sway_x, sway_y, -1650, 1450), breath, -1500, 1500)
    sway = g.op(unreal.MaterialExpressionMultiply, sway, hgt, -1350, 1500)
    # Excite (MPC) со сдвигом по экземплярам: e = saturate(Excite × 1.6 − frac(r × 91.7) × 0.6)
    ex = g.n(unreal.MaterialExpressionCollectionParameter, -2600, 1900)
    ex.set_editor_property("collection", mpc)
    ex.set_editor_property("parameter_name", "Excite")
    lag = g.op(unreal.MaterialExpressionMultiply, g.un(unreal.MaterialExpressionFrac, g.op(unreal.MaterialExpressionMultiply, rnd, 91.7, -2400, 2000), -2250, 2000), 0.6, -2100, 2000)
    e = g.un(unreal.MaterialExpressionSaturate, g.op(unreal.MaterialExpressionSubtract, g.op(unreal.MaterialExpressionMultiply, ex, 1.6, -2400, 1900), lag, -1950, 1900), -1800, 1900)
    e = g.op(unreal.MaterialExpressionMultiply, e, g.s("ExciteGain", 1.0, -1800, 1980), -1700, 1900)   # судьи/жюри — 0 (не болеют)
    # руки «качают»: 0.8 + 0.2 sin(2.2 Гц)
    pump = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionMultiply, wave(2.2, -2100, 2150), 0.2, -1800, 2150), 0.8, -1650, 2150)
    # поза «болеют»: dx/dy — цвет вершин A/B (0.5 ± d / 1.5 м), dz — UV0.U (м); × 100 см
    uv0 = g.n(unreal.MaterialExpressionTextureCoordinate, -2600, 2400, coordinate_index=0)
    oz = g.un(unreal.MaterialExpressionComponentMask, uv0, -2400, 2560)
    oz.set_editor_property("r", True)
    oz.set_editor_property("g", False)
    ox = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, vc, 0.5, -2400, 2400, a_out="A"), 1.5, -2250, 2400)
    oy = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, vc, 0.5, -2400, 2480, a_out="B"), 1.5, -2250, 2480)
    off = g.op(unreal.MaterialExpressionAppendVector, g.op(unreal.MaterialExpressionAppendVector, ox, oy, -2100, 2420), oz, -1950, 2450)
    amt = g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionMultiply, e, pump, -1650, 2000), 100.0, -1500, 2000)
    pose = g.op(unreal.MaterialExpressionMultiply, off, amt, -1350, 2400)
    # подпрыгивают: e × |sin(1.8 Гц)| × BounceAmp
    bounce = g.op(unreal.MaterialExpressionMultiply, g.un(unreal.MaterialExpressionAbs, wave(0.9, -2100, 2700), -1800, 2700),
                  g.s("BounceAmp", 3.0, -1800, 2780), -1650, 2700)
    bounce = g.op(unreal.MaterialExpressionMultiply, bounce, e, -1500, 2700)
    zero = g.n(unreal.MaterialExpressionConstant2Vector, -1500, 2800, r=0.0, g=0.0)
    bvec = g.op(unreal.MaterialExpressionAppendVector, zero, bounce, -1350, 2750)
    local = g.op(unreal.MaterialExpressionAdd, g.op(unreal.MaterialExpressionAdd, sway, pose, -1200, 1900), bvec, -1050, 2000)
    tr = g.n(unreal.MaterialExpressionTransform, -900, 2000,
             transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_INSTANCE,   # пространство экземпляра ISM (Local — пространство примитива: смещения не поворачивались по курсу человека)
             transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.c(local, tr, "")
    mel.connect_material_property(tr, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("used_with_nanite", True)
    for prop, val in (("max_world_position_offset_displacement", 160.0),):
        try:
            m.set_editor_property(prop, val)
        except Exception as ex2:  # noqa
            log("материал: %s ? %s" % (prop, ex2))
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_Crowd собран")
    return m


def crowd_mi(name, master, scalars=None, uniform=None):
    path = "%s/MI_%s" % (DIR, name)
    inst = eal.load_asset(path) if eal.does_asset_exist(path) else \
        asset_tools.create_asset("MI_" + name, DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, master)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(inst, k, float(v))
    if uniform:
        mel.set_material_instance_static_switch_parameter_value(inst, "Uniform", True)
        mel.set_material_instance_vector_parameter_value(inst, "UniformColor", uniform)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


def srgb(h):
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return unreal.LinearColor(out[0], out[1], out[2], 1.0)


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    if not eal.does_directory_exist(DIR):
        eal.make_directory(DIR)
    meshes = [] if os.environ.get("CROWDIMP_MAT_ONLY") == "1" else [import_mesh(v) for v in VARIANTS if os.path.exists(os.path.join(WORK, "SM_Crowd_%s.fbx" % v))]
    for sm in meshes[:2]:
        probe(sm)
    if os.environ.get("CROWDIMP_PROBE") == "1":
        return
    pal = import_palette()
    mpc = make_mpc()
    m = build_material(pal, mpc)
    crowd_mi("Crowd_Sit", m, {"BounceAmp": 2.5, "SwayAmp": 1.4})
    crowd_mi("Crowd_Stand", m, {"BounceAmp": 7.0, "SwayAmp": 2.0})
    crowd_mi("Crowd_Judge", m, {"ExciteGain": 0.0, "BounceAmp": 0.0, "SwayAmp": 0.6, "SwayAmpSide": 0.4}, srgb("#f3f4f6"))
    crowd_mi("Crowd_Jury", m, {"ExciteGain": 0.0, "BounceAmp": 0.0, "SwayAmp": 0.6, "SwayAmpSide": 0.4}, srgb("#9fc1e6"))
    meta = os.path.join(WORK, "crowd_people.json")
    if os.path.exists(meta):
        with open(meta, encoding="utf-8") as f:
            log("габариты: %s" % json.dumps(json.load(f), ensure_ascii=False))
    log("готово")


main()
