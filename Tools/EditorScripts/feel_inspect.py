# -*- coding: utf-8 -*-
# S-41 (game feel): разведка — видимый меш BP_Kellan (AnimClass, пост-процесс AnimBP, кости), логический UEFN.
# Только читает. Запуск:
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
# Маркер в логе: FEELINS
import unreal

BP = "/Game/MetaHumans/Kellan/BP_Kellan"
BOXER = "/Game/Boxing/Blueprints/BP_Boxer"


def log(*a):
    unreal.log("FEELINS " + " ".join(str(x) for x in a))


def describe_mesh(sk):
    if not sk:
        return
    log("   mesh", sk.get_path_name(), "skel", sk.skeleton.get_path_name() if sk.skeleton else None)
    try:
        log("   post_process_anim_bp", sk.get_editor_property("post_process_anim_blueprint"))
    except Exception as e:  # noqa
        log("   ppabp ?", e)
    try:
        log("   physics_asset", sk.get_editor_property("physics_asset"))
    except Exception as e:  # noqa
        log("   pa ?", e)


def walk_bp(path):
    bp = unreal.load_asset(path)
    log("bp", path, bp)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        d = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        name = unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(d)
        log("comp", name, type(obj).__name__)
        if isinstance(obj, unreal.SkeletalMeshComponent):
            for p in ("anim_class", "animation_mode", "leader_pose_component", "disable_post_process_blueprint"):
                try:
                    log("   ", p, obj.get_editor_property(p))
                except Exception as e:  # noqa
                    log("   ", p, "?", e)
            describe_mesh(obj.get_editor_property("skeletal_mesh_asset"))
    gc = bp.generated_class()
    cdo = unreal.get_default_object(gc)
    for p in ("RetargetAsset", "IKRetargeter", "Retargeter", "RTG"):
        try:
            log("cdo", p, cdo.get_editor_property(p))
        except Exception:
            pass


def main():
    walk_bp(BP)
    # переменные ABP_GenericRetarget
    for p in ("/Game/Characters/ABP_GenericRetarget", ):
        a = unreal.load_asset(p)
        log("abp", p, a)
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in reg.get_assets_by_class(unreal.TopLevelAssetPath("/Script/IKRig", "IKRetargeter"), False):
        log("rtg", a.package_name)
    for a in reg.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "AnimBlueprint"), False):
        n = str(a.package_name)
        if "Retarget" in n or "PostProcess" in n or "Kellan" in n:
            log("abp-list", n)


main()
