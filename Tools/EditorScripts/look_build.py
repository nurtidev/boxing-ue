# -*- coding: utf-8 -*-
# S-41 (облик боксёра), шаг 3: импорт формы в UE + материалы + визуальные BP бойцов красного/синего угла.
#
# Цепочка (всё скриптами, повторяемо):
#   1. UE:      look_export.py   — FBX тела/одежды Kellan → Saved/LookWork (полный редактор)
#   2. Blender: Tools/Blender/look_kit.py — тело без головы/кистей/стоп, трусы+боксёрки, перчатки → *.fbx
#   3. UE:      этот скрипт (коммандлет):
#        UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#
# Что создаёт:
#   /Game/Boxing/Characters/Meshes/SKM_BoxerGloves         — перчатки (своя геометрия веба) на metahuman_base_skel
#   /Game/Boxing/Characters/Materials/M_BoxerKit + MI_*     — материалы формы (свои)
#   /Game/BoxingLocal/Characters/SKM_BoxerBody              — тело MetaHuman без головы/кистей/стоп (производная Epic)
#   /Game/BoxingLocal/Characters/SKM_BoxerKit               — трусы + боксёрки (оболочки по телу MetaHuman)
#   /Game/BoxingLocal/Characters/MI_Skin_*                  — тон кожи синего бойца (дети материалов Kellan)
#   /Game/BoxingLocal/Characters/BP_BoxerLook_Red / _Blue   — копии BP_Kellan (ретаргет позы GASP, лицо Kellan):
#        Body → SKM_BoxerBody, Torso → SKM_BoxerGloves, Legs → SKM_BoxerKit, Feet → пусто; цвета угла.
#        Синий: светлее тон кожи, без причёски (бритая голова), щетина вместо усов.
# Маркер в логе: LOOKBUILD
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WORK = os.path.join(PROJECT, "Saved", "LookWork")
GIT_DIR = "/Game/Boxing/Characters"
MESH_DIR = GIT_DIR + "/Meshes"
MAT_DIR = GIT_DIR + "/Materials"
LOCAL_DIR = "/Game/BoxingLocal/Characters"
SKEL = "/Game/MetaHumans/Common/Female/Medium/NormalWeight/Body/metahuman_base_skel"
KELLAN_BP = "/Game/MetaHumans/Kellan/BP_Kellan"
KELLAN_BODY = "/Game/MetaHumans/Kellan/Male/Medium/NormalWeight/Body/m_med_nrw_body"
KELLAN_FACE = "/Game/MetaHumans/Kellan/Face/Kellan_FaceMesh"
KELLAN_BODY_MI = "/Game/MetaHumans/Kellan/Body/Materials/MI_BodySynthesized_Simplified"
KELLAN_FACE_MIS = {  # слот меша лица → MI Kellan
    "head_LOD1_shader_shader": "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD1",
    "head_LOD3_shader_shader": "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD3",
    "head_LOD57_shader_shader": "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD5",
}
SKIN_MASTER = "/Game/MetaHumans/Common/Shared/Materials/M_MetaHumanSkin_Simplified"

# Цвета формы (sRGB): атлас/сатин трусов блестит, кожа перчаток — полуглянец.
CORNERS = {
    "Red": {"main": "#b5121b", "dark": "#4a0709"},
    "Blue": {"main": "#1442b0", "dark": "#0a1c52"},
}
WHITE = "#ecebe4"
BOOTS = "#141416"
SOLE = "#8f8f8f"
BODY_MATCH = "0.85,0.82,0.74"
# Тон кожи синего бойца: множитель BaseColor_ColorCorrect (см. лог «ColorCorrect default»).
BLUE_SKIN_CC = os.environ.get("LOOK_BLUE_SKIN", "1.6,1.5,1.4,1")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("LOOKBUILD " + str(msg))


def srgb(hexstr, a=1.0):
    h = hexstr.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return unreal.LinearColor(out[0], out[1], out[2], a)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


# ------------------------------------------------------------------ импорт
def import_skm(fbx, dest_dir, name, skel):
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = skel
    ui.import_materials = False
    ui.import_textures = False
    ui.import_animations = False
    ui.create_physics_asset = False
    d = ui.skeletal_mesh_import_data
    d.set_editor_property("import_morph_targets", False)
    d.set_editor_property("use_t0_as_ref_pose", False)
    d.set_editor_property("update_skeleton_reference_pose", False)
    d.set_editor_property("import_meshes_in_bone_hierarchy", True)
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t = unreal.AssetImportTask()
    t.filename = os.path.join(WORK, fbx)
    t.destination_path = dest_dir
    t.destination_name = name
    t.replace_existing = True
    t.automated = True
    t.save = True
    t.options = ui
    asset_tools.import_asset_tasks([t])
    path = "%s/%s" % (dest_dir, name)
    m = eal.load_asset(path)
    if not m:
        raise RuntimeError("импорт не удался: " + fbx)
    b = m.get_bounds()
    log("импорт %s → %s; слоты %s; bounds o=(%.1f %.1f %.1f) e=(%.1f %.1f %.1f)" % (
        fbx, path, [str(s.material_slot_name) for s in m.materials],
        b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z))
    return m


# ------------------------------------------------------------------ материалы
def kit_master():
    path = MAT_DIR + "/M_BoxerKit"
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    m = asset_tools.create_asset("M_BoxerKit", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

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

    # Fresnel-подсветка края (атлас трусов / кожа перчатки «блестят» по контуру) — мягкий sheen
    col = vparam("Color", unreal.LinearColor(0.5, 0.5, 0.5, 1), -800, -200)
    sheen = sparam("Sheen", 0.0, -800, -60)
    fr = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -800, 40)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -560, 0)
    mel.connect_material_expressions(fr, "", mul, "A")
    mel.connect_material_expressions(sheen, "", mul, "B")
    lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -360, -150)
    white = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -560, -120)
    white.set_editor_property("constant", unreal.LinearColor(1, 1, 1, 1))
    mel.connect_material_expressions(col, "", lerp, "A")
    mel.connect_material_expressions(white, "", lerp, "B")
    mel.connect_material_expressions(mul, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(sparam("Roughness", 0.6, -360, 60), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(sparam("Specular", 0.5, -360, 160), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(sparam("Metallic", 0.0, -360, 260), "", unreal.MaterialProperty.MP_METALLIC)
    m.set_editor_property("used_with_skeletal_mesh", True)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("создан " + path)
    return m


def kit_mi(name, color, rough, spec=0.5, sheen=0.0):
    path = "%s/MI_%s" % (MAT_DIR, name)
    if eal.does_asset_exist(path):
        inst = eal.load_asset(path)
    else:
        inst = asset_tools.create_asset("MI_" + name, MAT_DIR, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, kit_master())
    mel.set_material_instance_vector_parameter_value(inst, "Color", srgb(color))
    mel.set_material_instance_scalar_parameter_value(inst, "Roughness", rough)
    mel.set_material_instance_scalar_parameter_value(inst, "Specular", spec)
    mel.set_material_instance_scalar_parameter_value(inst, "Sheen", sheen)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


def skin_mi(name, parent_path, cc, scalars=None):
    """MI_Skin_* в BoxingLocal: ребёнок материала Kellan с другим BaseColor_ColorCorrect (+ скаляры)."""
    path = "%s/MI_Skin_%s" % (LOCAL_DIR, name)
    if eal.does_asset_exist(path):
        inst = eal.load_asset(path)
    else:
        inst = asset_tools.create_asset("MI_Skin_" + name, LOCAL_DIR, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, eal.load_asset(parent_path))
    mel.set_material_instance_vector_parameter_value(inst, "BaseColor_ColorCorrect", cc)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(inst, k, v)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


# ------------------------------------------------------------------ BP
def bp_components(bp):
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    out = {}
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        d = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(d))
        if obj and name not in out:
            out[name] = obj
    return out


def slot_materials(mesh, by_slot):
    """override_materials по именам слотов меша."""
    return [by_slot.get(str(s.material_slot_name)) for s in mesh.materials]


def make_look(corner, body, kit, gloves, mats, skin):
    path = "%s/BP_BoxerLook_%s" % (LOCAL_DIR, corner)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    bp = eal.duplicate_asset(KELLAN_BP, path)
    comps = bp_components(bp)
    log("%s: компоненты %s" % (path, sorted(comps)))
    c = CORNERS[corner]

    def set_mesh(name, mesh, by_slot):
        comp = comps[name]
        comp.set_editor_property("skeletal_mesh_asset", mesh)
        comp.set_editor_property("override_materials", slot_materials(mesh, by_slot) if mesh else [])
        if mesh:
            comp.set_editor_property("visible", True)

    set_mesh("Body", body, {"Skin": skin["body"]})
    set_mesh("Torso", gloves, {"GloveLeather": mats["glove_" + corner], "GloveTrim": mats["trim_" + corner],
                               "GloveLaces": mats["white"], "GloveTape": mats["white"]})
    set_mesh("Legs", kit, {"Trunks": mats["trunks_" + corner], "TrunksBand": mats["white"],
                           "Boots": mats["boots"], "BootsTrim": mats["trunks_" + corner], "BootsSole": mats["sole"]})
    feet = comps["Feet"]
    feet.set_editor_property("skeletal_mesh_asset", None)
    feet.set_editor_property("override_materials", [])
    # лицо: тон кожи
    face = comps["Face"]
    fm = face.get_editor_property("skeletal_mesh_asset") or eal.load_asset(KELLAN_FACE)
    if skin.get("face"):
        face.set_editor_property("override_materials", slot_materials(fm, skin["face"]))
    # причёска / растительность
    for gname, groom in skin.get("grooms", {}).items():
        if gname in comps:
            comps[gname].set_editor_property("groom_asset", groom)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("BP %s собран (%s)" % (path, c["main"]))
    return bp


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    for d in (GIT_DIR, MESH_DIR, MAT_DIR, LOCAL_DIR):
        ensure_dir(d)
    skel = eal.load_asset(SKEL)
    body = import_skm("boxer_body.fbx", LOCAL_DIR, "SKM_BoxerBody", skel)
    kit = import_skm("boxer_kit.fbx", LOCAL_DIR, "SKM_BoxerKit", skel)
    gloves = import_skm("boxer_gloves.fbx", MESH_DIR, "SKM_BoxerGloves", skel)

    # тело: физассет и пост-процесс AnimBP (твисты/корректив MetaHuman) — как у тела Kellan
    kb = eal.load_asset(KELLAN_BODY)
    for prop in ("physics_asset", "post_process_anim_blueprint", "lod_settings"):
        try:
            v = kb.get_editor_property(prop)
            body.set_editor_property(prop, v)
            log("тело: %s = %s" % (prop, v.get_path_name() if hasattr(v, "get_path_name") else v))
        except Exception as e:  # noqa
            log("тело: %s ? %s" % (prop, e))
    for m in (body, kit, gloves):
        eal.save_loaded_asset(m)

    mats = {
        "white": kit_mi("Kit_White", WHITE, 0.55, 0.4),
        "boots": kit_mi("Kit_Boots", BOOTS, 0.38, 0.5, 0.15),
        "sole": kit_mi("Kit_Sole", SOLE, 0.8, 0.3),
    }
    for corner, c in CORNERS.items():
        mats["trunks_" + corner] = kit_mi("Trunks_" + corner, c["main"], 0.28, 0.6, 0.35)   # атлас
        mats["glove_" + corner] = kit_mi("Glove_" + corner, c["main"], 0.33, 0.55, 0.2)    # кожа
        mats["trim_" + corner] = kit_mi("GloveTrim_" + corner, c["dark"], 0.5, 0.4)

    master = eal.load_asset(SKIN_MASTER)
    try:
        cc0 = mel.get_material_default_vector_parameter_value(master, "BaseColor_ColorCorrect")
        log("ColorCorrect default %s" % cc0)
    except Exception as e:  # noqa
        log("ColorCorrect default ? %s" % e)
    # Шея: у лица Kellan NeckHideScale = −0.6 (прячет низ шеи под капюшоном худи) — с голым торсом из-за этого
    # по ключицам/трапециям шёл «вырез» из полупрозрачной кожи. Возвращаем значение мастер-материала.
    try:
        neck = mel.get_material_default_scalar_parameter_value(master, "NeckHideScale")
    except Exception as e:  # noqa
        neck = 0.0
        log("NeckHideScale default ? %s" % e)
    neck = float(os.environ.get("LOOK_NECKHIDE", neck))
    log("NeckHideScale лица → %s" % neck)
    kbmi = eal.load_asset(KELLAN_BODY_MI)
    one = unreal.LinearColor(1, 1, 1, 1)
    # Текстура тела Kellan (была под худи) светлее и холоднее текстуры лица: на голом торсе по трапециям
    # читался «вырез». Подгоняем тело к лицу множителем BODY_MATCH (по замеру на снимках).
    bm = [float(x) for x in os.environ.get("LOOK_BODY_MATCH", BODY_MATCH).split(",")]
    red_skin = {"body": skin_mi("Body_Red", KELLAN_BODY_MI, unreal.LinearColor(bm[0], bm[1], bm[2], 1)),
                "face": {slot: skin_mi("Face%s_Red" % slot.split("_")[1], p, one, {"NeckHideScale": neck})
                         for slot, p in KELLAN_FACE_MIS.items()}}
    r, g, b, a = (float(x) for x in BLUE_SKIN_CC.split(","))
    cc = unreal.LinearColor(r, g, b, a)
    blue_skin = {"body": skin_mi("Body_Blue", KELLAN_BODY_MI, unreal.LinearColor(r * bm[0], g * bm[1], b * bm[2], a)),
                 "grooms": {"Hair": None},
                 "face": {slot: skin_mi("Face%s_Blue" % slot.split("_")[1], p, cc, {"NeckHideScale": neck})
                          for slot, p in KELLAN_FACE_MIS.items()}}
    make_look("Red", body, kit, gloves, mats, red_skin)
    make_look("Blue", body, kit, gloves, mats, blue_skin)
    log("готово")


main()
