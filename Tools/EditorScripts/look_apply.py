# -*- coding: utf-8 -*-
# S-60: ЭТАЛОННАЯ реализация «облик по данным бойца» на Python — то, что рантайм (C++, ABoxerCharacter после
# спауна визуала) должен делать с записью Content/Boxing/Data/Appearance.json. Используется скриптами проверки
# (look_gallery.py, look_shots.py) в игре (-game, Python в процессе игры); C++-порт — построчно по этой функции,
# см. Docs/LOOK.md «S-60: облик по данным бойца».
#
#   import look_apply
#   rec = look_apply.find("amateur:Санжар Ташкенбай")         # запись Appearance.json
#   look_apply.apply(visual_actor, rec["look"], child_comp)  # visual_actor — child actor «VisualOverride»
import json
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
APPEARANCE = os.path.join(PROJECT, "Content", "Boxing", "Data", "Appearance.json")

MI_HAIR = "/Game/MetaHumans/Kellan/Materials/MI_Hair.MI_Hair"
MI_FACIAL = "/Game/MetaHumans/Kellan/Materials/MI_Facial_Hair.MI_Facial_Hair"
MORPHS = ("Heavy", "Lean", "Muscular", "Female")
SKIP = [x for x in os.environ.get("LOOK_APPLY_SKIP", "").split(",") if x]   # отладка: scale,morph,skin,hair,brows,facial

_cache = {}


def log(m):
    unreal.log("LOOKAPPLY " + str(m))


def load():
    if "doc" not in _cache:
        with open(APPEARANCE, encoding="utf-8") as f:
            doc = json.load(f)
        _cache["doc"] = doc
        _cache["by_id"] = {b["id"]: b for b in doc["boxers"]}
    return _cache["doc"]


def find(key):
    """Запись по id ростера («amateur:Имя») или по части имени."""
    load()
    if key in _cache["by_id"]:
        return _cache["by_id"][key]
    for b in _cache["doc"]["boxers"]:
        if key.lower() in b["name"].lower() or key.lower() in b["id"].lower():
            return b
    return None


def comps(actor):
    out = {}
    for c in actor.get_components_by_class(unreal.SceneComponent):
        out.setdefault(c.get_name(), c)
    return out


def lc(rgb, a=1.0):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], a)


def mid(comp, idx):
    """Динамический экземпляр материала слота (создаёт, если ещё не MID)."""
    m = comp.get_material(idx)
    if isinstance(m, unreal.MaterialInstanceDynamic):
        return m
    return comp.create_dynamic_material_instance(idx)


def set_hair_params(comp, p, base=None):
    """Цвет волос MetaHuman (MI_Hair/MI_Facial_Hair): меланин, рыжина, седина, краска — во все слоты грума.
    base — материал-основа (у своих грумов и у грума без материала слоты могут быть пустыми)."""
    base_mi = unreal.load_asset(base) if base else None
    for i in range(comp.get_num_materials()):
        if base_mi is not None and not isinstance(comp.get_material(i), unreal.MaterialInstanceDynamic):
            comp.set_material(i, base_mi)
        m = mid(comp, i)
        if not m:
            continue
        m.set_scalar_parameter_value("hairMelanin", p["melanin"])
        m.set_scalar_parameter_value("hairRedness", p["redness"])
        m.set_scalar_parameter_value("WhiteAmount", p.get("white", 0.0))
        if p.get("dye"):
            m.set_vector_parameter_value("hairDye", lc(p["dye"]))


def attach_offset():
    """Относительный трансформ грума на сокете лица (Appearance.json groomAttach) — как у грумов Kellan."""
    r = load()["groomAttach"]
    return unreal.Transform(unreal.Vector(*r["loc"]), unreal.Quat(*r["quat"]).rotator(), unreal.Vector(1, 1, 1))


def set_groom(comp, g, face=None):
    """g = {"groom", "binding", "attach"?}: грум + привязка к коже (Kellan) или крепление к кости head (свои)."""
    asset = unreal.load_asset(g["groom"]) if g and g.get("groom") else None
    if asset is None:
        comp.set_groom_asset(None)
        comp.set_visibility(False)
        return False
    bind = unreal.load_asset(g["binding"]) if g.get("binding") else None
    if g.get("attach") and face is not None and os.environ.get("LOOK_GROOM_ATTACH", "1") != "0":
        comp.attach_to_component(face, g["attach"], unreal.AttachmentRule.KEEP_RELATIVE,
                                    unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE, False)
        comp.set_relative_transform(attach_offset(), False, False)
    elif face is not None:      # грумы Kellan (волны, щетина) — их родное крепление, как в BP_Kellan
        comp.attach_to_component(face, load()["groomAttach"]["socket"], unreal.AttachmentRule.KEEP_RELATIVE,
                                 unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE, False)
        comp.set_relative_transform(attach_offset(), False, False)
    comp.set_binding_asset(bind)
    comp.set_groom_asset(asset)
    comp.set_visibility(True)
    return True


def apply(actor, look, child_comp=None, corner_kit=True):
    """Облик бойца на визуальный актёр (копия BP_Kellan: Body/Face/Torso/Legs/Feet + грумы)."""
    c = comps(actor)
    body, face = c.get("Body"), c.get("Face")
    # 1. рост: равномерный масштаб всего визуала (кости, IK и наведение кулака — в пространстве этого же меша)
    s = float(look["scale"]) if "scale" not in SKIP else 1.0
    if child_comp is not None:
        child_comp.set_relative_scale3d(unreal.Vector(s, s, s))
    else:
        actor.set_actor_relative_scale3d(unreal.Vector(s, s, s))
    # 2. телосложение: морфы на ведущем меше Body — ведомые (Legs/Feet/Torso, leader pose) берут их по имени
    m = dict(look["morph"])
    m["Female"] = 1.0 if look.get("female") else 0.0
    if body and "morph" not in SKIP:
        for k in MORPHS:
            body.set_morph_target(k, float(m.get(k, 0.0)), False)
        # ведомые без leader pose (на всякий случай) — те же морфы
        for name in ("Legs", "Feet", "Torso"):
            comp = c.get(name)
            if comp and comp.get_editor_property("leader_pose_component") is None:
                for k in MORPHS:
                    comp.set_morph_target(k, float(m.get(k, 0.0)), False)
    # 2б. кроссовки GASP в слоте Feet (остались от BP_Kellan) — не рисовать (старые сборки обликов)
    feet = c.get("Feet")
    if feet:
        fm = feet.get_editor_property("skeletal_mesh_asset")
        if fm and "_shs_" in fm.get_name():
            feet.set_visibility(False)
    # 3. тон кожи: текстура тона в параметр BaseColor MID по имени слота (лицо — 3 LOD-слота, тело — «Skin»)
    #    + индивидуальный множитель BaseColor_ColorCorrect
    for comp, by_slot in ((face, look["skinFaceTex"]), (body, look["skinBodyTex"])):
        if not comp or "skin" in SKIP:
            continue
        for slot, tex_path in by_slot.items():
            idx = comp.get_material_index(slot)
            if idx < 0:
                continue
            tex = unreal.load_asset(tex_path)
            mm = mid(comp, idx)
            if tex:
                mm.set_texture_parameter_value("BaseColor", tex)
            mm.set_vector_parameter_value("BaseColor_ColorCorrect", lc(look["skinCC"]))
    # 4. волосы (под шлемом — нет)
    hair = look["hair"]
    if c.get("Hair") and "hair" not in SKIP:
        h = c["Hair"]
        if not look.get("headgear") and set_groom(h, hair, face):
            h.set_hair_length_scale_enable(True)
            h.set_hair_length_scale(float(hair["length"]))
            set_hair_params(h, hair, MI_HAIR)
        else:
            set_groom(h, None)
    # 5. брови
    if c.get("Eyebrows") and "brows" not in SKIP:
        set_hair_params(c["Eyebrows"], look["brows"])
    # 6. борода (компонент Beard) и щетина (Mustache)
    fh = look["facial"]
    if "facial" not in SKIP:
        if c.get("Beard"):
            if set_groom(c["Beard"], fh, face):
                set_hair_params(c["Beard"], fh, MI_HAIR)
        if c.get("Mustache"):
            if set_groom(c["Mustache"], fh["stubble"], face):
                set_hair_params(c["Mustache"], fh)
    log("облик: масштаб %.3f, морфы %s, тон %s, волосы %s/%.2f, борода %s" % (
        s, look["morph"], look["skinTone"], (hair["groom"] or "-").split(".")[-1], hair["length"],
        (fh["groom"] or "-").split(".")[-1]))
