# -*- coding: utf-8 -*-
# S-74 (game feel): материал повреждений лица M_FaceWounds — overlay-материал меша лица MetaHuman (Face->SetOverlayMaterial).
# Без текстур: маски считаются в шейдере по позиции пикселя в осях кости головы (HeadO/X/Y/Z — каждый кадр из кода):
# пятна едут вместе с головой. Центры (кости лица позы привязки, в осях головы) и сила приходят параметрами из
# ABoxerCharacter::UpdateFaceMaterials (BoxerFace.cpp) по FBoxerFaceDamage (порт web damage.ts):
#   BruiseL/R — синяк под глазом (отёк), CheekL/R — гематома скулы, CutL/R — рассечение брови (+ струйка крови вниз),
#   Nose — кровь из носа до губы, Mouth — разбитая губа. xyz — центр (см, меш лица), w — сила 0..1.
# Запуск: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
# Маркер в логе: FACEDMG
import unreal

PATH = "/Game/Boxing/FX"
import os
NAME = os.environ.get("FACE_DMG_NAME", "M_FaceWounds")  # имя для итераций, пока старый ассет занят чужим запущенным боем

HLSL = r"""
float3 q = P - HeadO.xyz;
P = float3(dot(q, HeadX.xyz), dot(q, HeadY.xyz), dot(q, HeadZ.xyz));
float3 c = float3(0, 0, 0);
float a = 0;
float k;
float3 d;
// синяк под глазом: тёмно-фиолетовый, мягкое пятно (глазное яблоко не задевает — центр ниже века)
d = P - BruiseL.xyz; k = saturate(1.6 * (1 - length(d * float3(1, 1.4, 1.3)) / 2.3)) * BruiseL.w;
c = lerp(c, float3(0.2, 0.04, 0.13), saturate(k * 1.6)); a = max(a, k * 0.85);
d = P - BruiseR.xyz; k = saturate(1.6 * (1 - length(d * float3(1, 1.4, 1.3)) / 2.3)) * BruiseR.w;
c = lerp(c, float3(0.2, 0.04, 0.13), saturate(k * 1.6)); a = max(a, k * 0.85);
// гематома скулы: красно-багровая, шире
d = P - CheekL.xyz; k = saturate(1 - length(d) / 2.8) * CheekL.w;
c = lerp(c, float3(0.45, 0.07, 0.07), saturate(k * 1.5)); a = max(a, k * 0.6);
d = P - CheekR.xyz; k = saturate(1 - length(d) / 2.8) * CheekR.w;
c = lerp(c, float3(0.45, 0.07, 0.07), saturate(k * 1.5)); a = max(a, k * 0.6);
// рассечение брови: узкая щель вдоль брови + струйка вниз по виску/веку
d = P - CutL.xyz; k = saturate(1.5 * (1 - length(d * float3(0.75, 0.45, 2.6)) / 1.2)) * step(0.02, CutL.w);
c = lerp(c, float3(0.22, 0.0, 0.0), saturate(k * 2)); a = max(a, k * min(1, 0.6 + CutL.w));
k = saturate(1 - abs(d.x - 0.6 * d.z / 3) / 0.35) * step(d.z, 0) * saturate(1 + d.z / (0.6 + 3.5 * CutL.w)) * step(-2.5, d.y) * CutL.w;
c = lerp(c, float3(0.42, 0.015, 0.015), saturate(k * 2)); a = max(a, k * 0.9);
d = P - CutR.xyz; k = saturate(1.5 * (1 - length(d * float3(0.75, 0.45, 2.6)) / 1.2)) * step(0.02, CutR.w);
c = lerp(c, float3(0.22, 0.0, 0.0), saturate(k * 2)); a = max(a, k * min(1, 0.6 + CutR.w));
k = saturate(1 - abs(d.x + 0.6 * d.z / 3) / 0.35) * step(d.z, 0) * saturate(1 + d.z / (0.6 + 3.5 * CutR.w)) * step(-2.5, d.y) * CutR.w;
c = lerp(c, float3(0.42, 0.015, 0.015), saturate(k * 2)); a = max(a, k * 0.9);
// кровь из носа: из ноздрей (±0.6 см) вниз к губе, только передняя поверхность
d = P - Nose.xyz;
k = saturate(1 - abs(abs(d.x) - 0.55) / 0.45) * step(d.z, 0.4) * saturate(1 + d.z / (0.4 + 2.6 * Nose.w)) * step(-1.0, d.y) * Nose.w;
c = lerp(c, float3(0.42, 0.015, 0.015), saturate(k * 2)); a = max(a, k * 0.9);
// разбитая губа: пятно у угла рта + капля вниз
d = P - Mouth.xyz; k = saturate(1 - length(d * float3(1, 1.5, 1.5)) / 0.9) * Mouth.w;
c = lerp(c, float3(0.4, 0.01, 0.02), saturate(k * 2)); a = max(a, k * 0.9);
k = saturate(1 - abs(d.x) / 0.3) * step(d.z, 0) * saturate(1 + d.z / (0.2 + 1.8 * Mouth.w)) * step(-0.8, d.y) * Mouth.w;
c = lerp(c, float3(0.4, 0.01, 0.02), saturate(k * 2)); a = max(a, k * 0.9);
return float4(c, saturate(a));
"""

HEAD = ["HeadO", "HeadX", "HeadY", "HeadZ"]
PARAMS = ["BruiseL", "BruiseR", "CheekL", "CheekR", "CutL", "CutR", "Nose", "Mouth"]


def log(*a):
    unreal.log("FACEDMG " + " ".join(str(x) for x in a))


def main():
    L = unreal.MaterialEditingLibrary
    full = PATH + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        # Пересборка: тот же ассет, граф заново.
        mat = unreal.load_asset(full)
        L.delete_all_material_expressions(mat)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mat = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    try:
        mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    except Exception as e:  # noqa
        log("tlm ?", e)
    custom = L.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("code", HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "FaceDamage")
    ins = []
    for n in ["P"] + HEAD + PARAMS:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    custom.set_editor_property("inputs", ins)
    # Позиция — мировая (PreSkinnedPosition в 5.7 в пиксельный шейдер не пробросить), переводится в оси кости головы
    # параметрами HeadO/HeadX/HeadY/HeadZ (их каждый кадр ставит BoxerFace.cpp; оси уже поделены на масштаб).
    wp = L.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1100, -300)
    L.connect_material_expressions(wp, "", custom, "P")
    for i, n in enumerate(HEAD):
        p = L.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1100, -200 + i * 100)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))
        L.connect_material_expressions(p, "RGBA", custom, n)
    for i, n in enumerate(PARAMS):
        p = L.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, i * 120)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))
        L.connect_material_expressions(p, "RGBA", custom, n)
    rgb = L.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -200, -60)
    rgb.set_editor_property("r", True)
    rgb.set_editor_property("g", True)
    rgb.set_editor_property("b", True)
    rgb.set_editor_property("a", False)
    alpha = L.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -200, 80)
    alpha.set_editor_property("r", False)
    alpha.set_editor_property("g", False)
    alpha.set_editor_property("b", False)
    alpha.set_editor_property("a", True)
    L.connect_material_expressions(custom, "", rgb, "")
    L.connect_material_expressions(custom, "", alpha, "")
    L.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    rough = L.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, 200)
    rough.set_editor_property("r", 0.22)  # кровь влажная
    L.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    log("готово", full)


main()
