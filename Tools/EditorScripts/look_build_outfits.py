# -*- coding: utf-8 -*-
# S-56 (рефери и любительская форма), шаг 3: импорт одежды из Blender в UE + материалы + визуальные BP.
# Полный пайплайн облика — Docs/LOOK.md.
#
# Цепочка (всё скриптами, повторяемо):
#   1. UE:      look_export.py                 — FBX тела/лица Kellan → Saved/LookWork (полный редактор)
#   2. Blender: Tools/Blender/look_kit.py      — форма профи (S-41): тело боксёра, трусы+боксёрки, перчатки
#               Tools/Blender/look_outfits.py  — рефери (рубашка/брюки/туфли/кожа рук), майка, шлем, перчатки любителя
#   3. UE:      look_build.py (S-41: BP_BoxerLook_Red/_Blue), затем ЭТОТ скрипт (коммандлет):
#        UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#
# Что создаёт (своя геометрия — в git, производные MetaHuman — в BoxingLocal вне git):
#   /Game/Boxing/Characters/Meshes/SKM_BoxerHeadgear         — шлем (web headgear.glb, подогнан под голову Kellan)
#   /Game/Boxing/Characters/Meshes/SKM_BoxerGloves_Amateur   — перчатки любителя (липучка + логотип, без шнуровки)
#   /Game/Boxing/Characters/Materials/MI_*                    — материалы майки/шлема/рефери (M_BoxerKit)
#   /Game/BoxingLocal/Characters/SKM_BoxerVest, SKM_BoxerVestHeadgear, SKM_BoxerBody_Amateur (без кожи под майкой)
#   /Game/BoxingLocal/Characters/SKM_RefBody_Am/_Pro, SKM_RefShirt_Am/_Pro, SKM_RefTrousers, SKM_RefShoes
#   /Game/BoxingLocal/Characters/BP_BoxerLook_{Red,Blue}_Amateur       — майка + шлем в цвет угла (волосы под шлемом)
#   /Game/BoxingLocal/Characters/BP_BoxerLook_{Red,Blue}_AmateurElite  — майка без шлема (мужчины-элита, World Boxing)
#   /Game/BoxingLocal/Characters/BP_RefereeLook_Amateur / _Pro         — рефери (копии BP_Kellan: ретаргет позы GASP)
# Маркер в логе: LOOKOUT
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
KELLAN_FACE_MIS = {
    "head_LOD1_shader_shader": "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD1",
    "head_LOD3_shader_shader": "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD3",
    "head_LOD57_shader_shader": "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD5",
}
KELLAN_HAIR = "/Game/MetaHumans/Kellan/Materials/"
SKIN_MASTER = "/Game/MetaHumans/Common/Shared/Materials/M_MetaHumanSkin_Simplified"
BOXER_LOOK = LOCAL_DIR + "/BP_BoxerLook_%s"

CORNERS = {
    "Red": {"main": "#b5121b", "dark": "#4a0709", "strap": "#94101a"},
    "Blue": {"main": "#1442b0", "dark": "#0a1c52", "strap": "#103690"},
}
WHITE = "#ecebe4"
# рефери: цвета — как web tools/referee/build_referee.py
REF = {
    "Pro": {"shirt": "#c9d4e2", "trousers": "#1c1d22", "belt": "#0d0d0f", "shoes": "#111113", "sole": "#1a1a1c"},
    "Amateur": {"shirt": "#f2f2ef", "trousers": "#efeee9", "belt": "#efeee9", "shoes": "#e9e9e6", "sole": "#bdbdb8"},
}
NITRILE = "#6f7fd0"
BOW_TIE = "#0a0a0c"
# Облик рефери не похож на бойцов: тон кожи между красным (Kellan как есть) и синим (×1.6), седина.
REF_SKIN_CC = os.environ.get("LOOK_REF_SKIN", "1.45,1.32,1.18,1")
REF_HAIR_WHITE = float(os.environ.get("LOOK_REF_WHITE", "0.5"))
BODY_MATCH = "0.85,0.82,0.74"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("LOOKOUT " + str(msg))


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


def kit_mi(name, color, rough, spec=0.5, sheen=0.0):
    """MI от M_BoxerKit (создан look_build.py, S-41): цвет + шероховатость + блик по контуру."""
    path = "%s/MI_%s" % (MAT_DIR, name)
    if eal.does_asset_exist(path) and os.environ.get("LOOK_OUT_FORCE") != "1":
        # уже есть (в т.ч. общие с look_build.py: перчатки, белый) — не пересохраняем: файл может держать
        # запущенная игра/редактор другой роли (MoveFile → «Error saving»). Сменить цвета — LOOK_OUT_FORCE=1.
        return eal.load_asset(path)
    if eal.does_asset_exist(path):
        inst = eal.load_asset(path)
    else:
        inst = asset_tools.create_asset("MI_" + name, MAT_DIR, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, eal.load_asset(MAT_DIR + "/M_BoxerKit"))
    mel.set_material_instance_vector_parameter_value(inst, "Color", srgb(color))
    mel.set_material_instance_scalar_parameter_value(inst, "Roughness", rough)
    mel.set_material_instance_scalar_parameter_value(inst, "Specular", spec)
    mel.set_material_instance_scalar_parameter_value(inst, "Sheen", sheen)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


def child_mi(name, parent_path, vectors=None, scalars=None):
    """MI_* в BoxingLocal — ребёнок материала Kellan (кожа, волосы) с правками параметров."""
    path = "%s/MI_%s" % (LOCAL_DIR, name)
    if eal.does_asset_exist(path):
        inst = eal.load_asset(path)
    else:
        inst = asset_tools.create_asset("MI_" + name, LOCAL_DIR, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, eal.load_asset(parent_path))
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(inst, k, v)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(inst, k, v)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


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
    return [by_slot.get(str(s.material_slot_name)) for s in mesh.materials]


def set_mesh(comps, name, mesh, by_slot):
    comp = comps[name]
    comp.set_editor_property("skeletal_mesh_asset", mesh)
    comp.set_editor_property("override_materials", slot_materials(mesh, by_slot) if mesh else [])
    comp.set_editor_property("visible", mesh is not None)


def fresh_copy(src, path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    bp = eal.duplicate_asset(src, path)
    if not bp:
        raise RuntimeError("не скопировался %s → %s" % (src, path))
    return bp


def finish(bp, path):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("BP %s собран" % path)


# ------------------------------------------------------------------ любители
def make_amateur(corner, variant, feet_mesh, gloves, mats, body=None):
    """Копия BP_BoxerLook_<угол> (лицо/кожа/трусы S-41) + перчатки любителя + майка (и шлем)."""
    path = "%s/BP_BoxerLook_%s_%s" % (LOCAL_DIR, corner, variant)
    bp = fresh_copy(BOXER_LOOK % corner, path)
    comps = bp_components(bp)
    c = corner
    if body:
        # тело без кожи под майкой (кожа не проходит сквозь майку в замахах); материал кожи — тот же, что у угла
        skin = comps["Body"].get_editor_property("override_materials")
        skin_mi = skin[0] if skin else None
        comps["Body"].set_editor_property("skeletal_mesh_asset", body)
        comps["Body"].set_editor_property("override_materials", [skin_mi] if skin_mi else [])
    set_mesh(comps, "Torso", gloves, {
        "GloveLeather": mats["glove_" + c], "GloveTrim": mats["trim_" + c], "GloveStrap": mats["gstrap_" + c],
        "GloveLogo": mats["white"], "GloveTape": mats["white"], "GloveLaces": mats["white"]})
    set_mesh(comps, "Feet", feet_mesh, {
        "Vest": mats["vest_" + c], "VestTrim": mats["vest_" + c],
        "HeadgearShell": mats["hg_" + c], "HeadgearTrim": mats["hgtrim_" + c],
        "HeadgearStrap": mats["hgstrap"], "HeadgearLogo": mats["hglogo"]})
    if variant == "Amateur":
        # волосы под шлемом не видны (и иначе прорастали бы сквозь него) — как web headgear.ts
        comps["Hair"].set_editor_property("groom_asset", None)
    finish(bp, path)


# ------------------------------------------------------------------ рефери
def make_referee(kind, meshes, mats, skin):
    path = "%s/BP_RefereeLook_%s" % (LOCAL_DIR, kind)
    bp = fresh_copy(KELLAN_BP, path)
    comps = bp_components(bp)
    k = kind
    set_mesh(comps, "Body", meshes["body_" + k], {"Skin": skin["body"], "Gloves": mats["nitrile"]})
    set_mesh(comps, "Torso", meshes["shirt_" + k], {"Shirt": mats["shirt_" + k], "ShirtTrim": mats["shirt_" + k],
                                                    "BowTie": mats["bowtie"]})
    set_mesh(comps, "Legs", meshes["trousers"], {"Trousers": mats["trousers_" + k], "Belt": mats["belt_" + k]})
    set_mesh(comps, "Feet", meshes["shoes"], {"Shoes": mats["shoes_" + k], "ShoesSole": mats["sole_" + k]})
    face = comps["Face"]
    fm = face.get_editor_property("skeletal_mesh_asset") or eal.load_asset(KELLAN_FACE)
    face.set_editor_property("override_materials", slot_materials(fm, skin["face"]))
    # седина: волосы и щетина — дети материалов Kellan с WhiteAmount
    for gname, mis in skin["grooms"].items():
        g = comps.get(gname)
        if g:
            g.set_editor_property("override_materials", mis)
    finish(bp, path)


def copy_body_setup(mesh, kb):
    """Главный меш BP (Body): физассет, пост-процесс AnimBP (твисты/корректив MetaHuman), LOD — как у тела Kellan."""
    for prop in ("physics_asset", "post_process_anim_blueprint", "lod_settings"):
        try:
            mesh.set_editor_property(prop, kb.get_editor_property(prop))
        except Exception as e:  # noqa
            log("%s: %s ? %s" % (mesh.get_name(), prop, e))


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    for d in (GIT_DIR, MESH_DIR, MAT_DIR, LOCAL_DIR):
        ensure_dir(d)
    skel = eal.load_asset(SKEL)
    only = [x for x in os.environ.get("LOOK_OUT_ONLY", "").split(",") if x]
    kb = eal.load_asset(KELLAN_BODY)

    mats = {"white": kit_mi("Kit_White", WHITE, 0.55, 0.4)}
    for c, col in CORNERS.items():
        mats["glove_" + c] = kit_mi("Glove_" + c, col["main"], 0.33, 0.55, 0.2)
        mats["trim_" + c] = kit_mi("GloveTrim_" + c, col["dark"], 0.5, 0.4)
        mats["gstrap_" + c] = kit_mi("GloveStrap_" + c, col["strap"], 0.7, 0.35)
        mats["vest_" + c] = kit_mi("Vest_" + c, col["main"], 0.62, 0.45, 0.12)      # трикотаж, лёгкий блеск
        mats["hg_" + c] = kit_mi("Headgear_" + c, col["main"], 0.42, 0.5, 0.15)     # кожзам (web: 0.42)
        mats["hgtrim_" + c] = kit_mi("HeadgearTrim_" + c, col["dark"], 0.6, 0.4)
    mats["hgstrap"] = kit_mi("HeadgearStrap", "#16161a", 0.75, 0.35)
    mats["hglogo"] = kit_mi("HeadgearLogo", "#f4f4ef", 0.5, 0.4)
    mats["nitrile"] = kit_mi("Ref_Nitrile", NITRILE, 0.35, 0.5, 0.1)
    mats["bowtie"] = kit_mi("Ref_BowTie", BOW_TIE, 0.45, 0.45, 0.1)
    for k, col in REF.items():
        mats["shirt_" + k] = kit_mi("Ref_Shirt_" + k, col["shirt"], 0.82, 0.4, 0.05)
        mats["trousers_" + k] = kit_mi("Ref_Trousers_" + k, col["trousers"], 0.75, 0.4, 0.05)
        mats["belt_" + k] = kit_mi("Ref_Belt_" + k, col["belt"], 0.4, 0.5, 0.1)
        mats["shoes_" + k] = kit_mi("Ref_Shoes_" + k, col["shoes"], 0.3 if k == "Pro" else 0.6, 0.5, 0.15)
        mats["sole_" + k] = kit_mi("Ref_Sole_" + k, col["sole"], 0.8, 0.3)

    if not only or "am" in only:
        hg = import_skm("boxer_headgear.fbx", MESH_DIR, "SKM_BoxerHeadgear", skel)
        gloves = import_skm("boxer_gloves_am.fbx", MESH_DIR, "SKM_BoxerGloves_Amateur", skel)
        vest = import_skm("boxer_vest.fbx", LOCAL_DIR, "SKM_BoxerVest", skel)
        vest_hg = import_skm("boxer_vest_headgear.fbx", LOCAL_DIR, "SKM_BoxerVestHeadgear", skel)
        body_am = import_skm("boxer_body_am.fbx", LOCAL_DIR, "SKM_BoxerBody_Amateur", skel)
        copy_body_setup(body_am, kb)
        for m in (hg, gloves, vest, vest_hg, body_am):
            eal.save_loaded_asset(m)
        for corner in CORNERS:
            make_amateur(corner, "Amateur", vest_hg, gloves, mats, body_am)
            make_amateur(corner, "AmateurElite", vest, gloves, mats, body_am)

    if not only or "ref" in only:
        meshes = {
            "body_Amateur": import_skm("ref_body_am.fbx", LOCAL_DIR, "SKM_RefBody_Am", skel),
            "body_Pro": import_skm("ref_body_pro.fbx", LOCAL_DIR, "SKM_RefBody_Pro", skel),
            "shirt_Amateur": import_skm("ref_shirt_am.fbx", LOCAL_DIR, "SKM_RefShirt_Am", skel),
            "shirt_Pro": import_skm("ref_shirt_pro.fbx", LOCAL_DIR, "SKM_RefShirt_Pro", skel),
            "trousers": import_skm("ref_trousers.fbx", LOCAL_DIR, "SKM_RefTrousers", skel),
            "shoes": import_skm("ref_shoes.fbx", LOCAL_DIR, "SKM_RefShoes", skel),
        }
        # «тело» рефери (руки) — главный меш BP: физассет, пост-процесс AnimBP (твисты MetaHuman), LOD — как у Kellan
        for key in ("body_Amateur", "body_Pro"):
            copy_body_setup(meshes[key], kb)
        for m in meshes.values():
            eal.save_loaded_asset(m)
        r, g, b, a = (float(x) for x in REF_SKIN_CC.split(","))
        bm = [float(x) for x in BODY_MATCH.split(",")]
        master = eal.load_asset(SKIN_MASTER)
        try:
            neck = mel.get_material_default_scalar_parameter_value(master, "NeckHideScale")
        except Exception:  # noqa
            neck = 0.0
        skin = {
            "body": child_mi("Skin_Body_Ref", KELLAN_BODY_MI,
                             {"BaseColor_ColorCorrect": unreal.LinearColor(r * bm[0], g * bm[1], b * bm[2], a)}),
            "face": {slot: child_mi("Skin_Face%s_Ref" % slot.split("_")[1], p,
                                    {"BaseColor_ColorCorrect": unreal.LinearColor(r, g, b, a)}, {"NeckHideScale": neck})
                     for slot, p in KELLAN_FACE_MIS.items()},
            "grooms": {
                "Hair": [child_mi("Hair_Ref_%d" % i, KELLAN_HAIR + n, None,
                                  {"WhiteAmount": REF_HAIR_WHITE, "hairMelanin": 0.85, "hairRedness": 0.05})
                         for i, n in enumerate(("MI_Hair", "MI_Hair_Cards", "MI_Hair_Helmet"))],
                "Mustache": [child_mi("Hair_Ref_Stubble", KELLAN_HAIR + "MI_Hair3", None,
                                      {"WhiteAmount": REF_HAIR_WHITE * 0.8, "hairMelanin": 0.85, "hairRedness": 0.05})],
                "Eyebrows": [child_mi("Hair_Ref_Brows", KELLAN_HAIR + "MI_Hair1", None,
                                      {"WhiteAmount": REF_HAIR_WHITE * 0.4}),
                             eal.load_asset(KELLAN_HAIR + "MI_Facial_Hair")],
            },
        }
        for kind in ("Amateur", "Pro"):
            make_referee(kind, meshes, mats, skin)
    log("готово")


main()
