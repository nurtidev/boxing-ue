# -*- coding: utf-8 -*-
# S-41 (облик боксёра): разведка — компоненты BP_Kellan, меши, материалы, скелеты. Только читает.
# Запуск: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
# Маркер в логе: LOOKINS
import unreal

BP = "/Game/MetaHumans/Kellan/BP_Kellan"


def log(*a):
    unreal.log("LOOKINS " + " ".join(str(x) for x in a))


def describe_mesh(sk):
    if not sk:
        return
    log("   mesh", sk.get_path_name())
    try:
        log("   skeleton", sk.skeleton.get_path_name() if sk.skeleton else None)
    except Exception as e:  # noqa
        log("   skeleton ?", e)
    try:
        for i, m in enumerate(sk.materials):
            log("   mat[%d]" % i, m.material_slot_name, m.material_interface.get_path_name() if m.material_interface else None)
    except Exception as e:  # noqa
        log("   materials ?", e)
    try:
        b = sk.get_bounds()
        log("   bounds", b.origin, b.box_extent)
    except Exception as e:  # noqa
        log("   bounds ?", e)
    try:
        log("   morphs", len(sk.get_all_morph_target_names()))
    except Exception as e:  # noqa
        log("   morphs ?", e)


def main():
    bp = unreal.load_asset(BP)
    log("bp", bp, bp.generated_class())
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sds.k2_gather_subobject_data_for_blueprint(bp)
    for h in handles:
        d = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        name = unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(d)
        log("comp", name, type(obj).__name__, obj.get_path_name() if obj else None)
        if isinstance(obj, unreal.SkeletalMeshComponent):
            sk = obj.get_editor_property("skeletal_mesh_asset") if hasattr(obj, "get_editor_property") else None
            try:
                log("   anim_class", obj.get_editor_property("anim_class"), "mode", obj.get_editor_property("animation_mode"))
            except Exception as e:  # noqa
                log("   anim ?", e)
            try:
                log("   override_materials", [m.get_path_name() if m else None for m in obj.get_editor_property("override_materials")])
            except Exception as e:  # noqa
                log("   ovr ?", e)
            describe_mesh(sk)
        if isinstance(obj, unreal.GroomComponent):
            try:
                log("   groom", obj.get_editor_property("groom_asset"))
            except Exception as e:  # noqa
                log("   groom ?", e)
    # функции BP (что делает конструкция)
    try:
        for g in unreal.BlueprintEditorLibrary.get_graphs(bp) if hasattr(unreal.BlueprintEditorLibrary, "get_graphs") else []:
            log("graph", g.get_name())
    except Exception as e:  # noqa
        log("graphs ?", e)
    # MetaHuman Creator API доступен?
    for n in ("MetaHumanCharacter", "MetaHumanCharacterEditorSubsystem", "MetaHumanCharacterFactoryNew"):
        log("api", n, hasattr(unreal, n))
    # материалы кожи — параметры
    for p in ("/Game/MetaHumans/Kellan/Body/Materials/MI_BodySynthesized_Simplified",
              "/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD1",
              "/Game/MetaHumans/Kellan/Shared/Materials/MI_Fabric_Legs_Simplified"):
        mi = unreal.load_asset(p)
        if not mi:
            continue
        log("mi", p, "parent", mi.get_editor_property("parent").get_path_name())
        for kind, arr in (("s", "scalar_parameter_values"), ("v", "vector_parameter_values"), ("t", "texture_parameter_values")):
            for pv in mi.get_editor_property(arr):
                info = pv.get_editor_property("parameter_info").get_editor_property("name")
                val = pv.get_editor_property("parameter_value")
                log("   ", kind, info, val.get_path_name() if hasattr(val, "get_path_name") else val)
        par = unreal.load_asset("/Game/MetaHumans/Common/Shared/Materials/M_MetaHumanSkin_Simplified")
    mel = unreal.MaterialEditingLibrary
    for p in ("/Game/MetaHumans/Common/Shared/Materials/M_MetaHumanSkin_Simplified",
              "/Game/MetaHumans/Common/Shared/Materials/M_MetaHumanFabric_Simplified"):
        m = unreal.load_asset(p)
        log("master", p, "scalars", [str(x) for x in mel.get_scalar_parameter_names(m)])
        log("master", p, "vectors", [str(x) for x in mel.get_vector_parameter_names(m)])
        log("master", p, "textures", [str(x) for x in mel.get_texture_parameter_names(m)])


main()
