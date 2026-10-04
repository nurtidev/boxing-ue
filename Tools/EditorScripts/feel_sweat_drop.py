# -*- coding: utf-8 -*-
# S-74 (game feel): материал капли пота M_SweatDrop для брызг в точке контакта (ImpactSpray.cpp, порт web ImpactFx.tsx).
# Непрозрачный, без освещения (капли в свете прожекторов «блестят»); яркость — PerInstanceCustomData[0], гаснет капля
# уменьшением (полупрозрачный вариант с прозрачностью из PerInstanceCustomData в 5.7 не рисовался).
# Запуск: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
# Маркер в логе: SWEATDROP
import unreal

PATH = "/Game/Boxing/FX"
NAME = "M_SweatBead"


def log(*a):
    unreal.log("SWEATDROP " + " ".join(str(x) for x in a))


def main():
    L = unreal.MaterialEditingLibrary
    full = PATH + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        mat = unreal.load_asset(full)
        L.delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    col = L.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -600, -100)
    col.set_editor_property("constant", unreal.LinearColor(0.82, 0.9, 1.0, 1.0))
    cd = L.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -600, 60)
    cd.set_editor_property("data_index", 0)
    gain = L.create_material_expression(mat, unreal.MaterialExpressionConstant, -600, 160)
    gain.set_editor_property("r", 1.6)
    m1 = L.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, -40)
    m2 = L.create_material_expression(mat, unreal.MaterialExpressionMultiply, -220, -40)
    ok = [L.connect_material_expressions(col, "", m1, "A"), L.connect_material_expressions(cd, "", m1, "B"),
          L.connect_material_expressions(m1, "", m2, "A"), L.connect_material_expressions(gain, "", m2, "B"),
          L.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)]
    log("связи", ok)
    L.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    log("готово", full)


main()
