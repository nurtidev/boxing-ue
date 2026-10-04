# -*- coding: utf-8 -*-
# S-77: публика зала — лёгкие люди MakeHuman (CC0, MPFB + системные ассеты MakeHuman) вместо цилиндров со сферами.
#
#   "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Tools\Blender\crowd_people.py
#   (CROWD_ONLY=M_Sit_A,F_Stand — собрать только эти варианты; CROWD_PREVIEW=1 — ещё и снимок всех вариантов EEVEE)
#
# Нужны: аддон MPFB и системные ассеты MakeHuman (makehuman_system_assets_cc0.zip → <MPFB user data>/data, см.
# Docs/ASSETS.md). Выход: Saved/CrowdWork/SM_Crowd_<вариант>.fbx (+ crowd_people.json — размеры, треугольники,
# T_CrowdPalette.png — палитра цветов). Дальше UE: Tools/EditorScripts/ring_crowd_import.py, build_ring.py.
#
# Каждый вариант — один статичный меш (~4–6 тыс. треугольников, один материал), «запечённый» в позе:
#   * сидя (ягодицы на ступени трибуны, колени над краем, голени свисают на ряд ниже) или стоя;
#   * лицом по +X UE (в Blender — по +X: человек MPFB смотрит в −Y, поворачиваем на +90° вокруг Z);
#   * начало координат: сидя — точка опоры под тазом на плоскости сиденья, стоя — пол между ступнями.
# Окраска — в материале M_Crowd по палитре и PerInstanceRandom (один меш = сотни разных людей):
#   цвет вершин R — зона: 0 кожа, 0.25 верх одежды, 0.5 низ, 0.75 волосы/глаза, 1 обувь;
#   G — «деталь»: яркость текстуры MakeHuman (складки/AO одежды, черты лица), 0.5 = средняя яркость зоны.
# Анимация — World Position Offset в материале (дёшево, без скелета): покачивание/дыхание от фазы экземпляра и
# поза «болеют» (руки вверх) — смещение вершин до неё: dz — UV0.U (м), dx/dy — цвет вершин A/B (0.5 ± d/1.5 м), оси UE.
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Saved", "CrowdWork")
TARGET_TRIS = int(os.environ.get("CROWD_TRIS", "5000"))
OFF_XY = 0.75   # м: предел смещения позы по X/Y в цвете вершин
ONLY = [x for x in os.environ.get("CROWD_ONLY", "").split(",") if x]

MOD = "bl_ext.blender_org.mpfb"
bpy.ops.preferences.addon_enable(module=MOD)
from bl_ext.blender_org.mpfb.services.assetservice import AssetService  # noqa: E402
from bl_ext.blender_org.mpfb.services.humanservice import HumanService  # noqa: E402

REG_SKIN, REG_TOP, REG_BOTTOM, REG_HAIR, REG_SHOES = 0.0, 0.25, 0.5, 0.75, 1.0


def pheno(male, weight=0.5, muscle=0.5, age=0.5, height=0.5, race=None):
    return {
        "gender": 1.0 if male else 0.0, "age": age, "muscle": muscle, "weight": weight, "proportions": 0.5,
        "height": height, "cupsize": 0.5, "firmness": 0.5,
        "race": race or {"asian": 0.5, "caucasian": 0.3, "african": 0.2},
    }


# вариант: телосложение, одежда (+ обувь), поза, руки, «стрижка» (линия волос: короткая / до шеи)
VARIANTS = [
    dict(name="M_Sit_A", male=True, ph=dict(weight=0.5, muscle=0.5), clothes=["male_casualsuit01", "shoes01"], pose="sit", arms="lap", hair="short"),
    dict(name="M_Sit_B", male=True, ph=dict(weight=0.85, muscle=0.35, age=0.8), clothes=["male_casualsuit03", "shoes02"], pose="sit", arms="clasp", hair="short"),
    dict(name="M_Sit_C", male=True, ph=dict(weight=0.3, muscle=0.6, age=0.35), clothes=["male_casualsuit05", "shoes04"], pose="sit", arms="knees", hair="short"),
    dict(name="M_Sit_D", male=True, ph=dict(weight=0.6, muscle=0.55, age=0.55, height=0.65), clothes=["male_casualsuit04", "shoes06"], pose="sit", arms="lap", hair="short"),
    dict(name="F_Sit_A", male=False, ph=dict(weight=0.45), clothes=["female_casualsuit01", "shoes05"], pose="sit", arms="lap", hair="long"),
    dict(name="F_Sit_B", male=False, ph=dict(weight=0.7, age=0.7), clothes=["female_casualsuit02", "shoes03"], pose="sit", arms="clasp", hair="long"),
    dict(name="M_Stand", male=True, ph=dict(weight=0.55, muscle=0.55), clothes=["male_casualsuit02", "shoes06"], pose="stand", arms="down", hair="short"),
    dict(name="F_Stand", male=False, ph=dict(weight=0.4), clothes=["female_sportsuit01", "shoes01"], pose="stand", arms="down", hair="long"),
]

SKIN = {True: "young_asian_male/young_asian_male.mhmat", False: "young_asian_female/young_asian_female.mhmat"}


def D(f, u, l):
    """Направление в осях человека MPFB (смотрит в −Y): f — вперёд, u — вверх, l — влево (+X)."""
    return Vector((l, -f, u)).normalized()


def mirror(d):
    return Vector((-d.x, d.y, d.z))


# позы: кость → направление (для левой стороны; правая — зеркально по X)
LEGS_SIT = {"thigh_l": D(1, -0.1, 0.12), "calf_l": D(0.12, -1, 0.02), "foot_l": D(1, -0.25, 0.05)}
LEGS_STAND = {"thigh_l": D(0.02, -1, 0.06), "calf_l": D(-0.02, -1, 0.02), "foot_l": D(1, -0.3, 0.08)}
SPINE_SIT = {"spine_01": D(-0.04, 1, 0), "spine_02": D(-0.02, 1, 0), "spine_03": D(0.02, 1, 0), "neck_01": D(0.15, 1, 0), "head": D(0.06, 1, 0)}
SPINE_LEAN = {"spine_01": D(0.25, 1, 0), "spine_02": D(0.3, 1, 0), "spine_03": D(0.3, 1, 0), "neck_01": D(0.1, 1, 0), "head": D(-0.05, 1, 0)}
SPINE_STAND = {"spine_01": D(0, 1, 0), "spine_02": D(0, 1, 0), "spine_03": D(0.03, 1, 0), "neck_01": D(0.12, 1, 0), "head": D(0.04, 1, 0)}
ARMS = {
    "lap": {"upperarm_l": D(0.35, -1, 0.12), "lowerarm_l": D(1, -0.2, -0.3), "hand_l": D(1, -0.35, -0.1)},
    "clasp": {"upperarm_l": D(0.3, -1, 0.1), "lowerarm_l": D(0.9, -0.15, -0.6), "hand_l": D(0.7, -0.2, -0.7)},
    "knees": {"upperarm_l": D(0.45, -1, 0.05), "lowerarm_l": D(0.7, 0.55, -0.35), "hand_l": D(0.3, 1, -0.2)},
    "down": {"upperarm_l": D(0.04, -1, 0.15), "lowerarm_l": D(0.25, -1, 0.04), "hand_l": D(0.15, -1, 0.0)},
}
# «болеют»: руки вверх кулаками, корпус чуть вперёд (сидя) — цель смещения вершин
ARMS_CHEER = {"upperarm_l": D(0.2, 1, 0.38), "lowerarm_l": D(0.12, 1, 0.12), "hand_l": D(0.05, 1, 0.0)}
SPINE_CHEER_SIT = {"spine_01": D(0.12, 1, 0), "spine_02": D(0.12, 1, 0), "spine_03": D(0.1, 1, 0), "neck_01": D(0.1, 1, 0), "head": D(0.0, 1, 0)}
ORDER = ["spine_01", "spine_02", "spine_03", "neck_01", "head",
         "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
         "upperarm_l", "lowerarm_l", "hand_l", "upperarm_r", "lowerarm_r", "hand_r"]


def log(m):
    print("CROWD " + str(m))
    sys.stdout.flush()


def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.armatures, bpy.data.materials, bpy.data.images):
        for d in list(coll):
            if d.users == 0:
                coll.remove(d)


def select_only(o):
    for x in bpy.context.selected_objects:
        x.select_set(False)
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


def mhmat_texture(mhmat, key):
    if not mhmat or not os.path.exists(mhmat):
        return None
    with open(mhmat, encoding="utf-8", errors="ignore") as f:
        for line in f:
            p = line.strip().split(None, 1)
            if len(p) == 2 and p[0] == key:
                path = os.path.join(os.path.dirname(mhmat), p[1].strip())
                return path if os.path.exists(path) else None
    return None


IMG_CACHE = {}


def image_lum(path):
    """Яркость текстуры (h, w), с учётом альфы (прозрачное = средняя)."""
    if path in IMG_CACHE:
        return IMG_CACHE[path]
    img = bpy.data.images.load(path)
    w, h = img.size
    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    bpy.data.images.remove(img)
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    IMG_CACHE[path] = lum
    return lum


def sample(lum, uvs):
    h, w = lum.shape
    x = np.clip((uvs[:, 0] % 1.0) * (w - 1), 0, w - 1).astype(np.int32)
    y = np.clip((uvs[:, 1] % 1.0) * (h - 1), 0, h - 1).astype(np.int32)
    return lum[y, x]


def loop_uvs(me):
    if not me.uv_layers:
        return None
    uv = np.zeros(len(me.loops) * 2, dtype=np.float32)
    me.uv_layers.active.data.foreach_get("uv", uv)
    return uv.reshape(-1, 2)


def set_attrs(o, region_per_loop, detail_per_loop):
    me = o.data
    attr = me.color_attributes.new("Crowd", "FLOAT_COLOR", "CORNER")
    col = np.zeros((len(me.loops), 4), dtype=np.float32)
    col[:, 0] = region_per_loop
    col[:, 1] = detail_per_loop
    col[:, 3] = 1.0
    attr.data.foreach_set("color", col.reshape(-1))


def detail_from(o, tex_paths):
    """Деталь по углам: яркость диффуза × AO, нормированная к 0.5 по среднему объекта."""
    me = o.data
    uvs = loop_uvs(me)
    n = len(me.loops)
    if uvs is None or not tex_paths:
        return np.full(n, 0.5, dtype=np.float32)
    v = np.ones(n, dtype=np.float32)
    for p in tex_paths:
        if p:
            v *= sample(image_lum(p), uvs)
    m = float(v.mean()) or 1.0
    return np.clip(0.5 * v / m, 0.1, 1.0)


def loop_vertex_index(me):
    vi = np.zeros(len(me.loops), dtype=np.int32)
    me.loops.foreach_get("vertex_index", vi)
    return vi


def vcoords(me):
    co = np.zeros(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get("co", co)
    return co.reshape(-1, 3)


def group_weights(o, name):
    gi = o.vertex_groups.get(name)
    w = np.zeros(len(o.data.vertices), dtype=np.float32)
    if gi is None:
        return w
    idx = gi.index
    for v in o.data.vertices:
        for g in v.groups:
            if g.group == idx:
                w[v.index] = g.weight
    return w


def build_variant(var):
    clear_scene()
    info = {
        "phenotype": pheno(var["male"], **var["ph"]), "rig": "game_engine",
        "eyes": "low-poly/low-poly.mhclo", "eyebrows": "", "eyelashes": "", "tongue": "", "teeth": "", "hair": "",
        "proxy": "", "targets": [], "clothes": [], "skin_mhmat": SKIN[var["male"]], "skin_material_type": "GAMEENGINE",
        "eyes_material_type": "MAKESKIN", "skin_material_settings": {}, "eyes_material_settings": {}, "expressions": [],
        "name": var["name"],
    }
    s = HumanService.get_default_deserialization_settings()
    s["subdiv_levels"] = 0
    s["load_clothes"] = False
    body = HumanService.deserialize_from_dict(info, s)
    rig = body.parent if body.parent and body.parent.type == "ARMATURE" else None
    if rig is None:
        rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    parts = []   # (объект, вид, текстуры)
    for c in var["clothes"]:
        path = AssetService.find_asset_absolute_path("%s/%s.mhclo" % (c, c), "clothes")
        o = HumanService.add_mhclo_asset(path, body, asset_type="Clothes", subdiv_levels=0, material_type="NONE")
        mhmat = path.replace(".mhclo", ".mhmat")
        parts.append((o, "shoes" if c.startswith("shoes") else "clothes",
                      [mhmat_texture(mhmat, "diffuseTexture"), mhmat_texture(mhmat, "aomapTexture")]))
    eyes = [o for o in bpy.data.objects if o.type == "MESH" and "low-poly" in o.name]
    skin_mhmat = AssetService.find_asset_absolute_path(SKIN[var["male"]], "skins")
    parts.insert(0, (body, "body", [mhmat_texture(skin_mhmat, "diffuseTexture")]))
    for e in eyes:
        parts.append((e, "eyes", []))

    # запечь шейп-ключи и маски (хелперы), удалить кожу под одеждой (группы Delete.*)
    for o, kind, _ in parts:
        select_only(o)
        if o.data.shape_keys:
            bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
        for m in list(o.modifiers):
            if m.type in ("MASK", "SUBSURF"):
                bpy.ops.object.modifier_apply(modifier=m.name)
    dels = [g.name for g in body.vertex_groups if g.name.startswith("Delete.")]
    if dels:
        bm = bmesh.new()
        bm.from_mesh(body.data)
        dl = bm.verts.layers.deform.verify()
        ids = {body.vertex_groups[n].index for n in dels}
        kill = [v for v in bm.verts if any(v[dl].get(i, 0.0) > 0.5 for i in ids)]
        bmesh.ops.delete(bm, geom=kill, context="VERTS")
        bm.to_mesh(body.data)
        bm.free()
        log("%s: под одеждой удалено вершин тела %d" % (var["name"], len(kill)))

    # зоны и деталь (поза покоя, мир)
    pelvis_z = (rig.matrix_world @ rig.data.bones["pelvis"].head_local).z
    waist = pelvis_z + 0.04
    for o, kind, tex in parts:
        me = o.data
        vi = loop_vertex_index(me)
        co = np.array([(o.matrix_world @ v.co)[:] for v in me.vertices], dtype=np.float32)
        if kind == "body":
            reg = np.full(len(me.vertices), REG_SKIN, dtype=np.float32)
            hw = group_weights(o, "head")
            hv = co[hw > 0.5]
            if len(hv):
                z0, z1 = float(hv[:, 2].min()), float(hv[:, 2].max())
                yc = float(np.median(hv[:, 1]))
                h = z1 - z0
                front_line = z0 + 0.84 * h                 # лоб: линия волос спереди (выше бровей)
                back_line = z0 + (0.18 if var["hair"] == "long" else 0.4) * h
                back = co[:, 1] > yc + 0.01               # затылок (человек смотрит в −Y)
                side = co[:, 1] > yc - 0.035
                hair = (hw > 0.3) & ((co[:, 2] > front_line) | (back & (co[:, 2] > back_line)) | (side & (co[:, 2] > z0 + 0.62 * h)))
                if var["hair"] == "long":
                    neck = group_weights(o, "neck_01")
                    hair |= (neck > 0.3) & (co[:, 1] > yc + 0.03) & (co[:, 2] > z0 - 0.08)
                reg[hair] = REG_HAIR
        elif kind == "clothes":
            reg = np.where(co[:, 2] > waist, REG_TOP, REG_BOTTOM).astype(np.float32)
        elif kind == "shoes":
            reg = np.full(len(me.vertices), REG_SHOES, dtype=np.float32)
        else:  # глаза: тёмные пятна кожи (не цвет волос — седина давала белую полосу)
            reg = np.full(len(me.vertices), REG_SKIN, dtype=np.float32)
        det = detail_from(o, [t for t in tex if t]) if kind != "eyes" else np.full(len(me.loops), 0.1, dtype=np.float32)
        set_attrs(o, reg[vi], det)
        for slot in list(o.material_slots):
            slot.material = None

    # один меш: присоединить всё к телу (веса по именам костей совпадают)
    select_only(body)
    for o, kind, _ in parts[1:]:
        o.select_set(True)
    bpy.ops.object.join()
    obj = body
    obj.data.materials.clear()
    for m in list(obj.modifiers):
        obj.modifiers.remove(m)
    tris0 = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    dec = obj.modifiers.new("Dec", "DECIMATE")
    dec.ratio = min(1.0, TARGET_TRIS / max(1, tris0))
    dec.use_collapse_triangulate = True
    select_only(obj)
    bpy.ops.object.modifier_apply(modifier="Dec")
    tris = len(obj.data.polygons)
    log("%s: треугольников %d → %d" % (var["name"], tris0, tris))
    arm = obj.modifiers.new("Arm", "ARMATURE")
    arm.object = rig

    def pose(spec):
        for pb in rig.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
        full = {}
        for k, d in spec.items():
            full[k] = d
            if k.endswith("_l"):
                full[k[:-2] + "_r"] = mirror(d)
        inv = rig.matrix_world.to_3x3().inverted()
        for name in ORDER:
            if name not in full or name not in rig.pose.bones:
                continue
            pb = rig.pose.bones[name]
            cur = (pb.tail - pb.head).normalized()
            want = (inv @ full[name]).normalized()
            q = cur.rotation_difference(want)
            hm = Matrix.Translation(pb.head)
            pb.matrix = hm @ q.to_matrix().to_4x4() @ hm.inverted() @ pb.matrix
            bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        ev = obj.evaluated_get(dg)
        me = ev.to_mesh()
        co = np.array([(obj.matrix_world @ v.co)[:] for v in me.vertices], dtype=np.float32)
        ev.to_mesh_clear()
        return co

    sit = var["pose"] == "sit"
    base = {}
    base.update(LEGS_SIT if sit else LEGS_STAND)
    base.update(SPINE_LEAN if var["arms"] == "knees" else SPINE_SIT if sit else SPINE_STAND)
    base.update(ARMS[var["arms"]])
    cheer = dict(base)
    cheer.update(ARMS_CHEER)
    if sit:
        cheer.update(SPINE_CHEER_SIT)
    A = pose(base)
    B = pose(cheer)
    pelvis = rig.matrix_world @ rig.pose.bones["pelvis"].head
    obj.modifiers.remove(arm)

    # поворот +90° вокруг Z (лицом в +X), начало координат
    def rot(c):
        return np.stack([-c[:, 1], c[:, 0], c[:, 2]], axis=1)
    A, B = rot(A), rot(B)
    px, py = -pelvis.y, pelvis.x
    if sit:
        # опора — низ ягодиц: вершины за тазом (позади коленей) около оси таза
        m = (np.abs(A[:, 0] - px) < 0.12) & (np.abs(A[:, 1] - py) < 0.16)
        z0 = float(A[m, 2].min())
        org = np.array([px, py, z0], dtype=np.float32)
    else:
        org = np.array([px, py, float(A[:, 2].min())], dtype=np.float32)
    A -= org
    B -= org
    # меш в позе A
    me = obj.data
    me.vertices.foreach_set("co", A.reshape(-1))
    for vg in list(obj.vertex_groups):
        obj.vertex_groups.remove(vg)
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
    me.update()
    # смещение позы «болеют» (оси UE: X = x, Y = −y, Z = z; метры): dz — в U канала UV0 (полная точность; V не трогаем —
    # его переворачивает/не переворачивает импорт), dx/dy — в цвете вершин A/B (8 бит на ±OFF_XY м, шаг ~0.6 см)
    off = B - A
    vi = loop_vertex_index(me)
    while len(me.uv_layers) > 1:
        me.uv_layers.remove(me.uv_layers[-1])
    u0 = np.stack([off[vi, 2], np.zeros(len(vi), dtype=np.float32)], axis=1)
    me.uv_layers[0].data.foreach_set("uv", u0.astype(np.float32).reshape(-1))
    attr = me.color_attributes["Crowd"]
    col = np.zeros(len(me.loops) * 4, dtype=np.float32)
    attr.data.foreach_get("color", col)
    col = col.reshape(-1, 4)
    clip = float(max(np.abs(off[:, 0]).max(), np.abs(off[:, 1]).max()))
    col[:, 3] = np.clip(0.5 + off[vi, 0] / (2 * OFF_XY), 0, 1)
    col[:, 2] = np.clip(0.5 + (-off[vi, 1]) / (2 * OFF_XY), 0, 1)
    attr.data.foreach_set("color", col.reshape(-1))
    log("%s: |dx|,|dy| max %.2f м (предел %.2f)" % (var["name"], clip, OFF_XY))
    for p in me.polygons:
        p.use_smooth = True
    # габариты для build_ring.py
    lo, hi = A.min(axis=0), A.max(axis=0)
    knee = float(A[:, 0].max())             # вперёд от опоры (колени/носки)
    meta = {"tris": tris, "bbox_min_cm": [round(float(x) * 100, 1) for x in lo], "bbox_max_cm": [round(float(x) * 100, 1) for x in hi],
            "front_cm": round(knee * 100, 1), "max_offset_cm": round(float(np.abs(off).max()) * 100, 1)}
    # оставить только меш
    for o in list(bpy.data.objects):
        if o is not obj:
            bpy.data.objects.remove(o, do_unlink=True)
    obj.name = "SM_Crowd_" + var["name"]
    obj.data.name = obj.name
    select_only(obj)
    path = os.path.join(OUT, obj.name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, mesh_smooth_type="FACE",
                             colors_type="LINEAR", add_leaf_bones=False, bake_anim=False, apply_unit_scale=True,
                             axis_forward="-Z", axis_up="Y", use_mesh_modifiers=True)
    log("%s → %s %s" % (var["name"], path, meta))
    return meta


# палитра (sRGB), 16 оттенков на строку: кожа, верх, низ, волосы, обувь
PALETTE = {
    0: ["#f1d3b8", "#e8c3a0", "#e2b48f", "#d9aa82", "#d4a27a", "#c99872", "#c68f66", "#bb855c",
        "#b37a52", "#a8734e", "#9a6644", "#8a5a3c", "#7a5236", "#e5bd96", "#d6a77d", "#c08a60"],
    1: ["#e9e9e9", "#1c1c1f", "#2b2f3a", "#b3242c", "#2346a6", "#3a6fd1", "#6c7686", "#d9c9a3",
        "#1f6b4a", "#e0b42a", "#8a1d2b", "#394a73", "#503a2c", "#00a3c4", "#c45a1c", "#f2f2f2"],
    2: ["#2a3550", "#1d2433", "#3b4a6b", "#1a1a1c", "#2b2b2e", "#4a4a4f", "#6b6255", "#3e3a33",
        "#283a5c", "#33415e", "#202024", "#5a5048", "#1f2a40", "#2c2c30", "#46506a", "#7a6d5a"],
    3: ["#0e0c0b", "#17120f", "#1c1511", "#231912", "#2b1e15", "#120f0d", "#0b0a0a", "#3a2a1c",
        "#4a3524", "#15110e", "#6b6b6b", "#8e8e8e", "#100d0b", "#1e1a17", "#5e4026", "#0f0f10"],
    4: ["#1a1a1c", "#f0f0f0", "#2a2a2e", "#3b2a1e", "#121214", "#d8d8d8", "#4a3a2e", "#202024",
        "#e6e6e6", "#151517", "#5a4636", "#2e2e33", "#ececec", "#1c1c1e", "#36302a", "#101012"],
}


def write_palette():
    w, h = 16, 8
    img = bpy.data.images.new("T_CrowdPalette", w, h, alpha=True)
    px = np.ones((h, w, 4), dtype=np.float32)
    for row, cols in PALETTE.items():
        for i, c in enumerate(cols):
            c = c.lstrip("#")
            rgb = [int(c[k:k + 2], 16) / 255.0 for k in (0, 2, 4)]
            px[row, i, :3] = rgb           # строка 0 — низ картинки в Blender (v = 0) → в UE v = 1 − (row+0.5)/8
    img.pixels[:] = px.reshape(-1).tolist()
    img.filepath_raw = os.path.join(OUT, "T_CrowdPalette.png")
    img.file_format = "PNG"
    img.save()
    log("палитра → T_CrowdPalette.png")


def main():
    os.makedirs(OUT, exist_ok=True)
    meta_path = os.path.join(OUT, "crowd_people.json")
    meta = {}
    if os.path.exists(meta_path):
        with open(meta_path, encoding="utf-8") as f:
            meta = json.load(f)
    for var in VARIANTS:
        if ONLY and var["name"] not in ONLY:
            continue
        meta[var["name"]] = build_variant(var)
    write_palette()
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
    log("готово")


main()
