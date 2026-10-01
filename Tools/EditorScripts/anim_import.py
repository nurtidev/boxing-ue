# S-41 трек C, шаг 1: импорт боксёрских клипов Mixamo (FBX) в /Game/BoxingLocal/Mixamo.
# Меш + скелет Mixamo — из model.fbx, клипы — на этот скелет.
# Папка BoxingLocal вне git (лицензия Mixamo: сырьё не распространяем).
# Запуск: UnrealEditor-Cmd.exe BoxingUE.uproject -run=pythonscript -script=<абс.путь>/anim_import.py -unattended -nosplash -nullrhi
# Повторяемый: существующие ассеты перезаписываются (replace_existing).
import os
import unreal

FBX_DIR = os.environ.get("MIXAMO_FBX_DIR", r"C:\Users\user\Desktop\boxing\web\tools\mixamo\fbx_h")
DEST = "/Game/BoxingLocal/Mixamo"
MESH_NAME = "SKM_MixamoBoxer"

# Клипы, которые нужны боксу (шаги — для справки/будущего, ноги ведёт Motion Matching GASP)
CLIPS = ["jab", "cross", "hookL", "hookR", "upperL", "upperR", "bodyHook", "block", "blockHit",
         "guard", "idle", "slip", "dodgeRight", "hitHead", "hitBody", "hitSmall", "knockdown",
         "knockout", "getUp", "victory", "defeat"]


def log(msg):
    unreal.log("ANIMIMPORT " + msg)


def make_task(path, dest_name, options):
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = DEST
    t.destination_name = dest_name
    t.automated = True
    t.replace_existing = True
    t.save = True
    t.options = options
    return t


def import_mesh():
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = False
    ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets", False)
    task = make_task(os.path.join(FBX_DIR, "model.fbx"), MESH_NAME, ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    log("mesh imported: %s" % list(task.imported_object_paths))
    return task.imported_object_paths


def find_skeleton():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous([DEST], True)
    for a in reg.get_assets_by_path(DEST, recursive=False):
        if str(a.asset_class_path.asset_name) == "Skeleton":
            return unreal.load_asset(str(a.package_name))
    return None


def import_clip(name, skel):
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.skeleton = skel
    ad = ui.anim_sequence_import_data
    ad.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    ad.set_editor_property("import_bone_tracks", True)
    ad.set_editor_property("remove_redundant_keys", False)
    # knockdown.fbx без фикс. частоты импортировался в 330 fps — приводим всё к 30 fps (как у Mixamo)
    ad.set_editor_property("use_default_sample_rate", False)
    ad.set_editor_property("custom_sample_rate", 30)
    task = make_task(os.path.join(FBX_DIR, name + ".fbx"), "A_MX_" + name, ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    log("clip %s -> %s" % (name, list(task.imported_object_paths)))


def main():
    # Interchange-импорт FBX в коммандлете падает (синхронизация Content Browser без Slate) — берём легаси FBX-импортёр
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    import_mesh()
    skel = find_skeleton()
    if not skel:
        log("ERROR: skeleton not found after mesh import")
        return
    log("skeleton: %s" % skel.get_path_name())
    for c in CLIPS:
        import_clip(c, skel)
    log("done")


main()
