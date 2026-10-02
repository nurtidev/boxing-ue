# -*- coding: utf-8 -*-
# S-41 (облик боксёра), шаг 1: выгрузка мешей Kellan (тело, голова, одежда GASP) в FBX для Blender —
# по телу строится форма боксёра (Tools/Blender/look_kit.py). Результат — Saved/LookWork/*.fbx (вне git,
# производные лицензированного контента Epic).
# Запуск — ПОЛНЫЙ редактор (FBX-экспорт скин-меша в коммандлете падает на ассерте MeshObject):
#   UnrealEditor.exe <uproject> -ExecCmds="py <этот файл>" -unattended -nosplash
# Скрипт сам закрывает редактор (экспорт синхронный, сохранять нечего).
import os
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "LookWork")
MESHES = {
    "body": "/Game/MetaHumans/Kellan/Male/Medium/NormalWeight/Body/m_med_nrw_body",
    "legs": "/Game/MetaHumans/Common/Male/Medium/NormalWeight/Bottoms/Cargopants/m_med_nrw_btm_cargopants_High",
    "torso": "/Game/MetaHumans/Common/Male/Medium/NormalWeight/Tops/Hoodie/Meshes/m_med_nrw_top_hoodie_nrm_High",
    "feet": "/Game/MetaHumans/Common/Male/Medium/NormalWeight/Shoes/RunningShoes/m_med_nrw_shs_runningshoes_High",
    "face": "/Game/MetaHumans/Kellan/Face/Kellan_FaceMesh",
    # тело Kellan обрезано (торс/руки под худи удалены) — полное тело того же типа (m_med_nrw) из мокап-набора
    "body_full": "/Game/MetaHumans/Common/Common/Mocap/m_med_nrw_body_mocap",
}
ONLY = os.environ.get("LOOK_EXPORT_ONLY", "")


def main():
    os.makedirs(OUT, exist_ok=True)
    for key, path in MESHES.items():
        if ONLY and key not in ONLY.split(","):
            continue
        a = unreal.load_asset(path)
        if not a:
            unreal.log_error("LOOKEXP нет " + path)
            continue
        t = unreal.AssetExportTask()
        t.object = a
        t.filename = os.path.join(OUT, key + ".fbx")
        t.automated = True
        t.prompt = False
        t.replace_identical = True
        opt = unreal.FbxExportOption()
        opt.ascii = False
        opt.export_morph_targets = False
        opt.level_of_detail = False
        opt.collision = False
        opt.vertex_color = True
        t.options = opt
        ok = unreal.Exporter.run_asset_export_task(t)
        unreal.log("LOOKEXP %s -> %s ok=%s" % (path, t.filename, ok))
        if isinstance(a, unreal.SkeletalMesh):
            try:
                unreal.log("LOOKEXP   skeleton %s mats %s" % (a.skeleton.get_path_name(),
                           [m.material_interface.get_path_name() if m.material_interface else None for m in a.materials]))
            except Exception as e:  # noqa
                unreal.log("LOOKEXP   ? %s" % e)


main()
unreal.SystemLibrary.quit_editor()
