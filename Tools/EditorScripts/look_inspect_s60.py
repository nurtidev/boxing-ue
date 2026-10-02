# -*- coding: utf-8 -*-
# S-60: разведка для «бойцы разные» — параметры материалов кожи/волос Kellan, грумы и их группы, морфы мешей,
# все грумы/головы в проекте и плагинах. Коммандлет, только читает (плюс экспорт текстур в Saved/LookWork/s60).
# Маркер: LOOKS60
import os

import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "LookWork", "s60")
os.makedirs(OUT, exist_ok=True)


def log(*a):
    unreal.log("LOOKS60 " + " ".join(str(x) for x in a))


def mat_params(path):
    mi = eal.load_asset(path)
    if not mi:
        log("нет", path)
        return
    log("== MAT", path, "parent", mi.get_editor_property("parent").get_path_name() if isinstance(mi, unreal.MaterialInstance) else "-")
    for kind, fn, getter in (
        ("S", mel.get_scalar_parameter_names, mel.get_material_instance_scalar_parameter_value),
        ("V", mel.get_vector_parameter_names, mel.get_material_instance_vector_parameter_value),
        ("T", mel.get_texture_parameter_names, mel.get_material_instance_texture_parameter_value),
    ):
        try:
            names = fn(mi)
        except Exception as e:  # noqa
            log("  ?", kind, e)
            continue
        for n in names:
            try:
                v = getter(mi, n)
                if hasattr(v, "get_path_name"):
                    v = v.get_path_name()
            except Exception as e:  # noqa
                v = "?%s" % e
            log("  %s %s = %s" % (kind, n, v))


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


def export_tex(path, name):
    t = eal.load_asset(path)
    if not t:
        return
    task = unreal.AssetExportTask()
    task.object = t
    task.filename = os.path.join(OUT, name + ".tga")
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    ok = unreal.Exporter.run_asset_export_task(task)
    log("экспорт", path, "→", task.filename, ok, "srgb", t.get_editor_property("srgb"))


def main():
    # 1. материалы кожи и волос
    for p in ("/Game/MetaHumans/Kellan/Face/Materials/MI_HeadSynthesized_Simplified_LOD1",
              "/Game/MetaHumans/Kellan/Body/Materials/MI_BodySynthesized_Simplified",
              "/Game/BoxingLocal/Characters/MI_Skin_FaceLOD1_Blue",
              "/Game/MetaHumans/Kellan/Materials/MI_Hair",
              "/Game/MetaHumans/Kellan/Materials/MI_Facial_Hair",
              "/Game/MetaHumans/Kellan/Materials/MI_Hair_Cards",
              "/Game/MetaHumans/Kellan/Materials/MI_Hair_Helmet"):
        mat_params(p)
    # 2. компоненты BP и их грумы
    bp = eal.load_asset("/Game/BoxingLocal/Characters/BP_BoxerLook_Red")
    for n, c in sorted(bp_components(bp).items()):
        log("comp", n, c.get_class().get_name())
        if isinstance(c, unreal.GroomComponent):
            g = c.get_editor_property("groom_asset")
            log("   groom", g.get_path_name() if g else None)
            for prop in ("groom_groups_desc", "binding_asset", "physics_asset", "simulation_settings"):
                try:
                    v = c.get_editor_property(prop)
                    log("   ", prop, v)
                except Exception as e:  # noqa
                    log("   ", prop, "?", e)
            if g:
                for prop in ("hair_groups_rendering", "hair_groups_interpolation", "hair_groups_lod", "hair_groups_cards",
                             "hair_groups_meshes", "hair_groups_materials", "hair_groups_info"):
                    try:
                        v = g.get_editor_property(prop)
                        log("   asset", prop, len(v), [str(x) for x in v][:3])
                    except Exception as e:  # noqa
                        log("   asset", prop, "?", e)
        if isinstance(c, unreal.SkeletalMeshComponent):
            m = c.get_editor_property("skeletal_mesh_asset")
            if m:
                try:
                    names = m.get_all_morph_target_names()
                except Exception as e:  # noqa
                    names = "?%s" % e
                log("   mesh", m.get_path_name(), "морфы", len(names) if isinstance(names, list) else names,
                    list(names)[:20] if isinstance(names, list) else "")
                for prop in ("leader_pose_component", "anim_class", "animation_mode"):
                    try:
                        log("   ", prop, c.get_editor_property(prop))
                    except Exception as e:  # noqa
                        pass
    # 3. все грумы, головы и меши с «hair»/«beard» в проекте и плагинах MetaHuman
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for cls in ("GroomAsset", "GroomBindingAsset"):
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/HairStrandsCore", cls)], recursive_classes=True)
        for a in ar.get_assets(flt):
            log("asset", cls, a.package_name)
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh")], recursive_classes=True)
    for a in ar.get_assets(flt):
        pn = str(a.package_name)
        low = pn.lower()
        if any(k in low for k in ("face", "head", "hair", "beard", "metahuman", "body")):
            log("asset SkeletalMesh", pn)
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/MetaHumanCharacter", "MetaHumanCharacter")], recursive_classes=True)
    for a in ar.get_assets(flt):
        log("asset MetaHumanCharacter", a.package_name)
    # 4. текстуры кожи Kellan — для замера среднего тона офлайн
    export_tex("/Game/MetaHumans/Kellan/Face/Textures/T_HeadLOD1_BaseColor", "kellan_head_bc")
    export_tex("/Game/MetaHumans/Kellan/Body/Textures/T_Torso_BaseColor", "kellan_torso_bc")
    export_tex("/Game/MetaHumans/Kellan/Body/Textures/T_Body_BaseColor", "kellan_body_bc")
    # 5. настройки плагина MetaHuman Character (модель синтеза текстур)
    try:
        s = unreal.get_default_object(unreal.MetaHumanCharacterEditorSettings)
        log("TS model dir", s.get_editor_property("texture_synthesis_model_dir"))
    except Exception as e:  # noqa
        log("settings ?", e)
    log("готово")


main()
