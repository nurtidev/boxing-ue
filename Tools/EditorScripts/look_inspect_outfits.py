# -*- coding: utf-8 -*-
# S-56: разведка для облика рефери — компоненты BP_Kellan (грумы, их материалы и параметры волос).
# Коммандлет, только читает. Маркер: LOOKINS2
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(*a):
    unreal.log("LOOKINS2 " + " ".join(str(x) for x in a))


def comps(bp):
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    out = {}
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        d = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        name = str(unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(d))
        if obj and name not in out:
            out[name] = obj
    return out


bp = eal.load_asset("/Game/BoxingLocal/Characters/BP_BoxerLook_Red")
for n, c in sorted(comps(bp).items()):
    log("comp", n, c.get_class().get_name())
    if isinstance(c, unreal.GroomComponent):
        g = c.get_editor_property("groom_asset")
        log("   groom", g.get_path_name() if g else None)
        if g:
            try:
                for i, gm in enumerate(g.get_editor_property("hair_groups_materials")):
                    mi = gm.get_editor_property("material")
                    log("   groom mat", i, gm.get_editor_property("slot_name"), mi.get_path_name() if mi else None)
                    if mi:
                        for kind, fn in (("S", mel.get_scalar_parameter_names), ("V", mel.get_vector_parameter_names)):
                            try:
                                names = fn(mi)
                                log("     params", kind, [str(x) for x in names])
                            except Exception as e:  # noqa
                                log("     params ?", e)
                        for pn in ("Melanin", "Redness", "Whiteness", "Lightness", "DyeColor", "Hair_Melanin"):
                            try:
                                log("     ", pn, mel.get_material_instance_scalar_parameter_value(mi, pn))
                            except Exception:  # noqa
                                pass
            except Exception as e:  # noqa
                log("   groom mats ?", e)
        try:
            log("   override mats", [m.get_path_name() if m else None for m in c.get_editor_property("override_materials")])
        except Exception as e:  # noqa
            log("   override ?", e)
