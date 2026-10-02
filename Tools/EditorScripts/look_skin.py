# -*- coding: utf-8 -*-
# S-60: текстуры кожи по тонам 1..6 (лицо Kellan LOD1/3/5 + тело) — шаги UE вокруг Tools/Blender/look_skin_tones.py.
#
#   LOOK_SKIN_STEP=export  — исходные BaseColor Kellan → Saved/LookWork/skin/src_*.tga (коммандлет)
#   LOOK_SKIN_STEP=import  — Saved/LookWork/skin/out/T_Skin*_T<n>.tga → /Game/BoxingLocal/Characters/Skin (коммандлет)
#     UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
# Производные текстур Epic (Kellan) → только BoxingLocal (вне git).
# Маркер в логе: LOOKSKIN
import glob
import os

import unreal

eal = unreal.EditorAssetLibrary
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WORK = os.path.join(PROJECT, "Saved", "LookWork", "skin")
DEST = "/Game/BoxingLocal/Characters/Skin"
SRC = {
    "face_lod1": "/Game/MetaHumans/Kellan/Face/Textures/T_HeadLOD1_BaseColor",
    "face_lod3": "/Game/MetaHumans/Kellan/Face/Textures/T_HeadLOD3_BaseColor",
    "face_lod5": "/Game/MetaHumans/Kellan/Face/Textures/T_HeadLOD5_BaseColor",
    "body": "/Game/MetaHumans/Kellan/Body/Textures/T_Body_BaseColor",
}


def log(*a):
    unreal.log("LOOKSKIN " + " ".join(str(x) for x in a))


def export():
    os.makedirs(WORK, exist_ok=True)
    for key, path in SRC.items():
        t = eal.load_asset(path)
        task = unreal.AssetExportTask()
        task.object = t
        task.filename = os.path.join(WORK, "src_%s.tga" % key)
        task.automated = True
        task.replace_identical = True
        task.prompt = False
        ok = unreal.Exporter.run_asset_export_task(task)
        log("экспорт", path, "→", task.filename, ok, "srgb", t.get_editor_property("srgb"),
            "размер", t.blueprint_get_size_x(), t.blueprint_get_size_y(),
            "сжатие", t.get_editor_property("compression_settings"), "lod group", t.get_editor_property("lod_group"))


def do_import():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.TGA 0")
    if not eal.does_directory_exist(DEST):
        eal.make_directory(DEST)
    files = sorted(glob.glob(os.path.join(WORK, "out", "T_Skin*.tga")))
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = f
        t.destination_path = DEST
        t.destination_name = os.path.splitext(os.path.basename(f))[0]
        t.replace_existing = True
        t.automated = True
        t.save = False
        t.factory = unreal.TextureFactory()   # без Interchange: в коммандлете он тянет Slate и падает
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    src_body = eal.load_asset(SRC["body"])
    for t in tasks:
        path = "%s/%s" % (DEST, t.destination_name)
        tex = eal.load_asset(path)
        if not tex:
            log("НЕ импортирован", t.filename)
            continue
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("lod_group", src_body.get_editor_property("lod_group"))
        eal.save_loaded_asset(tex)
        log("импорт", path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y())


STEP = os.environ.get("LOOK_SKIN_STEP", "export")
if STEP == "export":
    export()
else:
    do_import()
log("готово", STEP)
