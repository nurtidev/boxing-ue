# -*- coding: utf-8 -*-
# S-68 (угловые: тренер и катмен у углов, стул), шаг 3: импорт из Blender (Tools/Blender/look_crew.py) + материалы +
# визуальные BP угловых и BP стула. Пайплайн облика — Docs/LOOK.md (раздел S-68).
#
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#
# Что создаёт (своя геометрия — в git, производные MetaHuman — в BoxingLocal вне git):
#   /Game/Boxing/Characters/Meshes/SM_CornerStool, SM_CrewBottle      — стул углового и бутылка (статика)
#   /Game/Boxing/Characters/Materials/MI_Crew_*, MI_Stool_*           — материалы (M_BoxerKit)
#   /Game/Boxing/Characters/BP_CornerStool_Red / _Blue                — стул: StaticMeshActor, сиденье в цвет угла
#   /Game/BoxingLocal/Characters/SKM_CrewJacket, SKM_CrewTeeTowel, SKM_CrewTrousers, SKM_CrewBody_Coach/_Cutman
#   /Game/BoxingLocal/Characters/BP_CornerCoach_<Red|Blue>, BP_Cutman_<Red|Blue> — копии BP_Kellan (ретаргет GASP),
#       облик по умолчанию запечён из Appearance.json «crew:<угол>:<роль>» (кожа, волосы, седина, борода); облик
#       под конкретного бойца — рантайм ApplyBoxerLook записью «crew:<id бойца>:<роль>» (Docs/LOOK.md S-68)
# Маркер в логе: LOOKCREW
import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Tools", "EditorScripts")
# помощники S-56 (импорт, MI, слоты BP) — без запуска их main()
L = {"__name__": "look_build_outfits_lib", "__file__": os.path.join(HERE, "look_build_outfits.py")}
_src = open(os.path.join(HERE, "look_build_outfits.py"), encoding="utf-8").read()
exec(compile(_src.replace("\nmain()", "\n"), "look_build_outfits.py", "exec"), L)

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
PROJECT = L["PROJECT"]
WORK = L["WORK"]
MESH_DIR, MAT_DIR, LOCAL_DIR, GIT_DIR = L["MESH_DIR"], L["MAT_DIR"], L["LOCAL_DIR"], L["GIT_DIR"]
APPEARANCE = os.path.join(PROJECT, "Content", "Boxing", "Data", "Appearance.json")
KELLAN_HAIR = "/Game/MetaHumans/Kellan/Materials/"

CORNERS = {
    "Red": {"main": "#b5121b", "jacket": "#7a0c12", "seat": "#5f0a0f", "cap": "#c41a22"},
    "Blue": {"main": "#1442b0", "jacket": "#0d2c78", "seat": "#0b2563", "cap": "#1f5fd8"},
}


def log(m):
    unreal.log("LOOKCREW " + str(m))


def import_static(fbx, name):
    path = "%s/%s" % (MESH_DIR, name)
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    ui.import_materials = False
    ui.import_textures = False
    ui.static_mesh_import_data.set_editor_property("combine_meshes", True)
    ui.static_mesh_import_data.set_editor_property("auto_generate_collision", False)
    ui.static_mesh_import_data.set_editor_property("normal_import_method",
                                                   unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t = unreal.AssetImportTask()
    t.filename = os.path.join(WORK, fbx)
    t.destination_path = MESH_DIR
    t.destination_name = name
    t.replace_existing = True
    t.automated = True
    t.save = True
    t.options = ui
    asset_tools.import_asset_tasks([t])
    m = eal.load_asset(path)
    if not m:
        raise RuntimeError("импорт не удался: " + fbx)
    b = m.get_bounds()
    log("статика %s: слоты %s, bounds o=(%.1f %.1f %.1f) e=(%.1f %.1f %.1f)" % (
        path, [str(s.material_slot_name) for s in m.static_materials], b.origin.x, b.origin.y, b.origin.z,
        b.box_extent.x, b.box_extent.y, b.box_extent.z))
    return m


def set_static_mats(mesh, by_slot):
    for i, s in enumerate(mesh.static_materials):
        mi = by_slot.get(str(s.material_slot_name))
        if mi:
            mesh.set_material(i, mi)
    eal.save_loaded_asset(mesh)


def load_looks():
    with open(APPEARANCE, encoding="utf-8") as f:
        doc = json.load(f)
    return {b["id"]: b["look"] for b in doc["boxers"] if b["id"].startswith("crew:")}


def skin_mis(tag, look):
    """Кожа тона по умолчанию: дети материалов Kellan + текстура тона (как рантайм ApplyBoxerLook)."""
    cc = look["skinCC"]
    col = unreal.LinearColor(cc[0], cc[1], cc[2], 1.0)
    body_tex = eal.load_asset(list(look["skinBodyTex"].values())[0].split(".")[0])
    body = L["child_mi"]("Skin_Body_Crew%s" % tag, L["KELLAN_BODY_MI"], {"BaseColor_ColorCorrect": col})
    if body_tex:
        mel.set_material_instance_texture_parameter_value(body, "BaseColor", body_tex)
        mel.update_material_instance(body)
        eal.save_loaded_asset(body)
    face = {}
    for slot, parent in L["KELLAN_FACE_MIS"].items():
        mi = L["child_mi"]("Skin_Face%s_Crew%s" % (slot.split("_")[1], tag), parent, {"BaseColor_ColorCorrect": col})
        tex = eal.load_asset(look["skinFaceTex"][slot].split(".")[0])
        if tex:
            mel.set_material_instance_texture_parameter_value(mi, "BaseColor", tex)
            mel.update_material_instance(mi)
            eal.save_loaded_asset(mi)
        face[slot] = mi
    return body, face


def hair_mi(tag, p, base="MI_Hair"):
    vals = {"hairMelanin": p["melanin"], "hairRedness": p["redness"], "WhiteAmount": p.get("white", 0.0)}
    return L["child_mi"]("Hair_Crew%s" % tag, KELLAN_HAIR + base, None, vals)


def groom_slots(groom):
    try:
        return max(1, len(groom.get_editor_property("hair_groups_materials")))
    except Exception:  # noqa
        return 1


def set_groom(comp, g, mi):
    asset = eal.load_asset(g["groom"].split(".")[0]) if g and g.get("groom") else None
    comp.set_editor_property("groom_asset", asset)
    comp.set_editor_property("binding_asset", eal.load_asset(g["binding"].split(".")[0]) if asset and g.get("binding") else None)
    comp.set_editor_property("override_materials", [mi] * groom_slots(asset) if asset else [])
    comp.set_editor_property("visible", asset is not None)


def make_crew_bp(kind, corner, meshes, mats, look):
    path = "%s/BP_%s_%s" % (LOCAL_DIR, "CornerCoach" if kind == "coach" else "Cutman", corner)
    bp = L["fresh_copy"](L["KELLAN_BP"], path)
    comps = L["bp_components"](bp)
    tag = "%s_%s" % (kind.capitalize(), corner)
    body_mi, face_mis = skin_mis(tag, look)
    c = corner
    if kind == "coach":
        L["set_mesh"](comps, "Body", meshes["body_coach"], {"Skin": body_mi})
        L["set_mesh"](comps, "Torso", meshes["jacket"], {"Jacket": mats["jacket_" + c], "JacketTrim": mats["white"],
                                                         "Zip": mats["zip"]})
    else:
        L["set_mesh"](comps, "Body", meshes["body_cutman"], {"Skin": body_mi, "Gloves": mats["nitrile"],
                                                             "Bottle": mats["bottle"], "BottleCap": mats["cap_" + c]})
        L["set_mesh"](comps, "Torso", meshes["tee"], {"Shirt": mats["tee_" + c], "ShirtTrim": mats["white"],
                                                      "Towel": mats["towel"]})
    L["set_mesh"](comps, "Legs", meshes["trousers"], {"Trousers": mats["trousers"], "Belt": mats["belt"]})
    L["set_mesh"](comps, "Feet", meshes["shoes"], {"Shoes": mats["shoes"], "ShoesSole": mats["sole"]})
    face = comps["Face"]
    fm = face.get_editor_property("skeletal_mesh_asset") or eal.load_asset(L["KELLAN_FACE"])
    face.set_editor_property("override_materials", L["slot_materials"](fm, face_mis))
    # волосы, борода, щетина, брови — по записи облика (седина тренера — WhiteAmount)
    h, fh = look["hair"], look["facial"]
    set_groom(comps["Hair"], h, hair_mi(tag, h))
    if "Beard" in comps:
        set_groom(comps["Beard"], fh, hair_mi(tag + "_Beard", fh))
    if "Mustache" in comps:
        st = fh.get("stubble") or {}
        set_groom(comps["Mustache"], st, hair_mi(tag + "_Stubble", fh, "MI_Hair3"))
    if "Eyebrows" in comps:
        comps["Eyebrows"].set_editor_property("override_materials", [
            hair_mi(tag + "_Brows", look["brows"], "MI_Hair1"), eal.load_asset(KELLAN_HAIR + "MI_Facial_Hair")])
    L["finish"](bp, path)
    log("%s: кожа T%d, волосы %s (седина %.2f), борода %s" % (path, look["skinTone"], (h["groom"] or "-").split(".")[-1],
                                                             h.get("white", 0), (fh["groom"] or "-").split(".")[-1]))


def make_stool_bp(corner, mesh, seat_mi, metal_mi):
    path = "%s/BP_CornerStool_%s" % (GIT_DIR, corner)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    f = unreal.BlueprintFactory()
    f.set_editor_property("parent_class", unreal.StaticMeshActor)
    bp = asset_tools.create_asset("BP_CornerStool_" + corner, GIT_DIR, unreal.Blueprint, f)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    comp = None
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        o = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
        if isinstance(o, unreal.StaticMeshComponent):
            comp = o
            break
    comp.set_editor_property("static_mesh", mesh)
    comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    comp.set_editor_property("override_materials", [seat_mi if str(s.material_slot_name) == "StoolSeat" else metal_mi
                                                    for s in mesh.static_materials])
    comp.set_collision_profile_name("NoCollision")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("BP %s собран" % path)


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    skel = eal.load_asset(L["SKEL"])
    kb = eal.load_asset(L["KELLAN_BODY"])
    kit = L["kit_mi"]
    mats = {"white": kit("Kit_White", L["WHITE"], 0.55, 0.4),
            "zip": kit("Crew_Zip", "#c9cbd0", 0.3, 0.7, 0.2),
            "towel": kit("Crew_Towel", "#f1f1ec", 0.95, 0.3),
            "bottle": kit("Crew_Bottle", "#d9ecf5", 0.2, 0.6, 0.35),
            "nitrile": kit("Crew_Nitrile", "#1d1f25", 0.35, 0.5, 0.1),
            "trousers": kit("Crew_Trousers", "#1b1e26", 0.75, 0.4, 0.05),
            "belt": kit("Crew_Belt", "#0d0d0f", 0.4, 0.5, 0.1),
            "shoes": kit("Crew_Shoes", "#2a2d33", 0.55, 0.45, 0.1),
            "sole": kit("Crew_Sole", "#d9d9d4", 0.8, 0.3),
            "metal": kit("Stool_Metal", "#8d939c", 0.3, 0.9, 0.3)}
    for c, col in CORNERS.items():
        mats["jacket_" + c] = kit("Crew_Jacket_" + c, col["jacket"], 0.6, 0.45, 0.15)
        mats["tee_" + c] = kit("Crew_Tee_" + c, col["main"], 0.8, 0.4, 0.08)
        mats["cap_" + c] = kit("Crew_BottleCap_" + c, col["cap"], 0.4, 0.5)
        mats["seat_" + c] = kit("Stool_Seat_" + c, col["seat"], 0.45, 0.5, 0.15)

    stool = import_static("crew_stool.fbx", "SM_CornerStool")
    bottle = import_static("crew_bottle.fbx", "SM_CrewBottle")
    set_static_mats(stool, {"StoolSeat": mats["seat_Red"], "StoolMetal": mats["metal"]})
    set_static_mats(bottle, {"Bottle": mats["bottle"], "BottleCap": mats["cap_Blue"]})
    for c in CORNERS:
        make_stool_bp(c, stool, mats["seat_" + c], mats["metal"])

    imp = L["import_skm"]
    meshes = {
        "jacket": imp("crew_jacket.fbx", LOCAL_DIR, "SKM_CrewJacket", skel),
        "tee": imp("crew_tee_towel.fbx", LOCAL_DIR, "SKM_CrewTeeTowel", skel),
        "trousers": imp("crew_trousers.fbx", LOCAL_DIR, "SKM_CrewTrousers", skel),
        "body_coach": imp("crew_body_coach.fbx", LOCAL_DIR, "SKM_CrewBody_Coach", skel),
        "body_cutman": imp("crew_body_cutman.fbx", LOCAL_DIR, "SKM_CrewBody_Cutman", skel),
        "shoes": eal.load_asset(LOCAL_DIR + "/SKM_RefShoes"),
    }
    for k in ("body_coach", "body_cutman"):
        L["copy_body_setup"](meshes[k], kb)
    for k, m in meshes.items():
        if k != "shoes":
            eal.save_loaded_asset(m)
    looks = load_looks()
    for c in CORNERS:
        for kind in ("coach", "cutman"):
            make_crew_bp(kind, c, meshes, mats, looks["crew:%s:%s" % (c, kind)])
    log("готово")


main()
