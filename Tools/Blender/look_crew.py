# -*- coding: utf-8 -*-
# S-68 (угловые: тренер и катмен у углов, стул), шаг 2 — Blender: одежда угловых на скелет MetaHuman
# (metahuman_base_skel) тем же пайплайном, что рефери (Tools/Blender/look_outfits.py — функции берутся оттуда без
# запуска его main) + свои жёсткие вещи (стул, бутылка, полотенце).
# Спецификация — web src/ui/three/CornerCrew.tsx: тренер — куртка в цвет угла (темнее) и тёмные брюки; катмен —
# футболка в цвет угла, полотенце на левом плече, бутылка воды в левой руке; стул — сиденье в цвет угла, хром.
#
# Вход  (Saved/LookWork, выгружает Tools/EditorScripts/look_export.py): body_full.fbx, face.fbx.
# Выход (Saved/LookWork):
#   crew_jacket.fbx       — олимпийка тренера: стойка воротника, молния и манжеты (JacketTrim — манжеты)
#                           (Jacket / JacketTrim / Zip); поверх брюк (низ на бёдрах)
#   crew_tee_towel.fbx    — футболка катмена (короткий рукав, бейка ворота) + полотенце на левом плече одним мешем
#                           (Shirt / ShirtTrim / Towel): у визуального BP четыре слота скин-мешей
#   crew_trousers.fbx     — брюки (Trousers / Belt) — геометрия брюк рефери, но с ключами телосложения
#   crew_body_coach.fbx   — видимая кожа тренера: запястья и кисти (Skin)
#   crew_body_cutman.fbx  — кожа катмена: предплечья + кисти в нитриле + бутылка в левой руке (Skin / Gloves /
#                           Bottle / BottleCap; бутылка жёстко на hand_l)
#   crew_stool.fbx        — стул углового (статика, своя геометрия → /Game/Boxing, в git): StoolSeat / StoolMetal,
#                           опора — центр на полу, верх сиденья — STOOL_H
#   crew_bottle.fbx       — та же бутылка отдельной статикой (SM_CrewBottle: передать бойцу / поставить на апрон)
#   previews: Saved/LookWork/crew_*.png (workbench)
# Ключи телосложения (Heavy/Lean/Muscular/Female, look_morphs.py) — на всём, что лежит на коже: тренер в рантайме
# плотнее (web: вес ×1.12), женщины-угловые — морф Female.
#
# Запуск:
#   "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup ^
#       --python C:\Users\user\Desktop\boxing-ue\Tools\Blender\look_crew.py
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(globals().get("__file__") or "C:/Users/user/Desktop/boxing-ue/Tools/Blender/x.py"))
sys.path.insert(0, HERE)
import look_morphs  # noqa: E402

# функции look_outfits.py (оболочки ткани, веса, экспорт) — без запуска его main()
O = {"__name__": "look_outfits_lib", "__file__": os.path.join(HERE, "look_outfits.py")}
_src = open(os.path.join(HERE, "look_outfits.py"), encoding="utf-8").read()
exec(compile(_src.replace("\nmain()\n", "\n"), "look_outfits.py", "exec"), O)

W = O["W"]
Plane = O["Plane"]

# ---- олимпийка тренера (метры, поза привязки A-pose; лицо — к −Y, левая сторона — +X)
JK_HEM = 0.875            # низ — на бёдрах, поверх брюк (их верх 1.000)
JK_OFF = 0.020            # от кожи (свободнее рубашки рефери)
JK_OFF_HEM = 0.034        # у низа — поверх пояса брюк (их внешняя стенка ≈ 1.6–1.8 см от кожи)
JK_OFF_ARM = 0.018
JK_SLEEVE_BACK = 0.022    # рукав кончается на 2.2 см выше сустава кисти (кожа рук — make_ref_body «pro» до 2.5 см)
JK_NECK = (1.425, 1.475)  # основание воротника
JK_COLLAR = (0.058, 0.064)  # стойка воротника (молния до горла)
JK_COLLAR_OFF = 0.017
JK_CUFF = 0.035           # манжета
JK_ZIP = 0.0085           # полуширина молнии
JK_DRAPE = 0.95

# ---- полотенце катмена (на левом плече, концы на грудь и лопатку)
TW_X = (0.075, 0.165)     # поперёк плеча: от шеи к плечевому суставу (левая сторона +X)
TW_Z = 1.13               # концы свисают до этой высоты спереди и сзади
TW_OFF = 0.030            # над футболкой (её 1.4 см + толщина)
TW_THICK = 0.010
TW_TOP = 1.485           # выше — шея (лучи дуги её не видят)

# ---- бутылка (web: цилиндр r 3.2–3.4 см × 21 см, в левой руке вдоль большого пальца, под ладонью)
BOT_R, BOT_L = 0.033, 0.21
BOT_ALONG = 0.068         # от сустава кисти к пальцам — середина ладони
BOT_PALM = 0.047          # от оси кисти в сторону ладони

# ---- стул (web CornerCrew useStoolGeometry: сиденье r 0.2 × 0.06, 4 ноги, кольцо-подножка)
STOOL_H = 0.52            # верх сиденья над полом (рантайм масштабирует по Z под рост бойца, как web seatH)
SEAT_R, SEAT_T = 0.20, 0.06
LEG_TOP_R, LEG_BOT_R = 0.12, 0.19
LEG_R = 0.017
FOOT_RING_Z, FOOT_RING_R, FOOT_RING_T = 0.20, 0.155, 0.011


def log(*a):
    print("CREW", *a)


# ------------------------------------------------------------------ безопасные помощники
def push_out_near(o, skins, gap, reach=0.06):
    """Как look_outfits.push_out, но только у близкой кожи (≤ reach): у открытого меша лица/брюк ближайшая точка
    для далёкой вершины даёт огромное «s < gap» — вершина улетала шипом через полсцены."""
    from mathutils.bvhtree import BVHTree
    trees = []
    for s in skins:
        b = bmesh.new()
        b.from_mesh(s.data)
        trees.append((BVHTree.FromBMesh(b), b))
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.normal_update()
    moved = 0
    for v in bm.verts:
        best = None
        for tree, _ in trees:
            hit = tree.find_nearest(v.co, reach)
            if hit[0] is not None and (best is None or hit[3] < best[3]):
                best = hit
        if best is None or v.normal.dot(best[1]) < 0.2:
            continue
        s = (v.co - best[0]).dot(best[1])
        if s < gap:
            v.co += best[1] * (gap - s)
            moved += 1
    bm.to_mesh(o.data)
    bm.free()
    for _, b in trees:
        b.free()
    log("push_out_near", o.name, moved)


def worst_gap(o, skin):
    """Контроль: самая далёкая от кожи вершина (шипы) и число утопленных под кожу."""
    from mathutils.bvhtree import BVHTree
    b = bmesh.new()
    b.from_mesh(skin.data)
    tree = BVHTree.FromBMesh(b)
    far, under, where = 0.0, 0, None
    for v in o.data.vertices:
        hit = tree.find_nearest(v.co)
        if hit[0] is None:
            continue
        if hit[3] > far:
            far, where = hit[3], tuple(round(c, 3) for c in v.co)
        if (v.co - hit[0]).dot(hit[1]) < -0.002:
            under += 1
    b.free()
    log("контроль %s: дальше всех от кожи %.1f см у %s, под кожей вершин %d" % (o.name, far * 100, where, under))
    return far


def keep_material_slots(o, names):
    """region_shell чистит список материалов, индексы граней (разметка в pre) остаются — вернуть слоты."""
    o.data.materials.clear()
    for n in names:
        o.data.materials.append(bpy.data.materials.get(n) or bpy.data.materials.new(n))


def bisect_some(bm, faces, plane):
    geom = list({v for f in faces for v in f.verts}) + list({e for f in faces for e in f.edges}) + list(faces)
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=plane.co, plane_no=plane.no)


# ------------------------------------------------------------------ олимпийка
def make_jacket(full, rig):
    neck = JK_NECK
    top_hi = (neck[0] + JK_COLLAR[0], neck[1] + JK_COLLAR[1])
    planes = {s: rig.arm_plane(s, back=JK_SLEEVE_BACK) for s in "lr"}
    ncy = rig.neck.y - 0.005

    def in_neck(c, grow=0.0):
        return (c.x / (O["NECK_RX"] + grow)) ** 2 + ((c.y - ncy) / (O["NECK_RY"] + grow)) ** 2 < 1.0

    def beyond(c):
        return any(pl.d(c) > 0 and rig.on_arm(s, c) for s, pl in planes.items())

    def keep(c):
        if c.z < JK_HEM or c.z > O["SHIRT_CAP"]:
            return False
        if in_neck(c) and c.z > O["neck_z"](c.y, top_hi):
            return False
        return not beyond(c)

    cuts = [Plane((0, 0, JK_HEM), (0, 0, 1)), O["neck_plane"](top_hi), O["neck_plane"](neck)] + list(planes.values())
    def on_arm_face(f, s):
        return rig.on_arm(s, f.calc_center_median(), 0.1)

    def zone(c):
        # воротник — в цвет куртки: граница «стойка/плечи» по сетке тела рвалась зубцами белым кантом
        for s in "lr":
            if rig.on_arm(s, c, 0.11) and planes[s].d(c) > -JK_CUFF:
                return 1                                    # манжета
        if abs(c.x) < JK_ZIP and c.y < rig.neck.y - 0.02:
            return 2                                        # молния спереди
        return 0

    def front_faces(bm):
        return [f for f in bm.faces if f.calc_center_median().y < rig.neck.y - 0.01 and abs(f.calc_center_median().x) < 0.06]

    def pre(bm):
        # ровные края манжет и молнии (лампас пробовал — по сетке тела рвётся пятнами, убран): разрезы ТОЛЬКО по граням руки / переда, до сглаживания
        for s in "lr":
            pl = planes[s]
            bisect_some(bm, [f for f in bm.faces if on_arm_face(f, s)], Plane(pl.co - pl.no * JK_CUFF, pl.no))
        for sg in (-1, 1):
            bisect_some(bm, front_faces(bm), Plane((sg * JK_ZIP, 0, 0), (1, 0, 0)))
        lay = bm.faces.layers.int.get("zone") or bm.faces.layers.int.new("zone")
        for f in bm.faces:
            f[lay] = zone(f.calc_center_median())

    def off(co):
        base = JK_OFF
        if co.z < 1.03:
            t = max(0.0, min(1.0, (1.03 - co.z) / (1.03 - JK_HEM)))
            base = JK_OFF + (JK_OFF_HEM - JK_OFF) * t
        elif any(rig.on_arm(s, co, 0.1) and planes[s].d(co) > -0.25 for s in "lr"):
            base = JK_OFF_ARM
        if co.z > O["neck_z"](co.y, neck) - 0.004 and in_neck(co, 0.02):
            base = JK_COLLAR_OFF
        return base

    headw = O["group_sums"](full, ("FACIAL_",), exact=("head",))

    def drape(bm):
        O["loosen_torso"](bm, rig, JK_HEM + 0.02, O["SHIRT_DRAPE_TOP"], JK_DRAPE)

    o = O["region_shell"](full, "jacket", lambda c: c.z > JK_HEM - 0.03 and c.z < 1.6, cuts, keep, off,
                          0.005, smooth_iters=24, rim_iters=10, drop_w=headw, post=drape, pre=pre, even=False)
    keep_material_slots(o, ["Jacket", "JacketTrim", "Zip"])
    z = o.data.attributes.get("zone")
    for p in o.data.polygons:
        p.material_index = z.data[p.index].value if z else 0
    if z:
        o.data.attributes.remove(z)
    log("jacket zones", {k: sum(1 for p in o.data.polygons if p.material_index == k) for k in range(3)})
    return o


# ------------------------------------------------------------------ полотенце
def make_towel(full):
    """Полоса махры через левое плечо (web towelGeometry: дуга сверху, концы спереди и сзади), по форме тела:
    сетка «поперёк плеча × вдоль пути» — точки поверхности лучами снаружи к коже, затем отступ и толщина."""
    from mathutils.bvhtree import BVHTree
    b = bmesh.new()
    b.from_mesh(full.data)
    # без шеи и головы: лучи сверху ложатся на трапецию, а не ползут по шее к уху
    bmesh.ops.delete(b, geom=[f for f in b.faces if f.calc_center_median().z > TW_TOP], context="FACES")
    tree = BVHTree.FromBMesh(b)
    NU = 6
    zc = 1.36            # центр дуги над плечом
    path = []            # спереди снизу вверх (луч +Y), дуга через верх (лучи к центру), сзади сверху вниз (луч −Y)
    for i in range(10):
        path.append(("f", TW_Z + (zc - TW_Z) * i / 10))
    for i in range(24):
        path.append(("a", -math.pi / 2 + math.pi * i / 23))
    for i in range(1, 11):
        path.append(("b", zc - (zc - TW_Z) * i / 10))
    rows = []
    for j in range(NU):
        x = TW_X[0] + (TW_X[1] - TW_X[0]) * j / (NU - 1)
        row = []
        for kind, t in path:
            if kind == "f":
                org, d = Vector((x, -0.6, t)), Vector((0, 1, 0))
            elif kind == "b":
                org, d = Vector((x, 0.6, t)), Vector((0, -1, 0))
            else:
                dirv = Vector((0, math.sin(t), math.cos(t)))
                org, d = Vector((x, -0.01, zc)) + dirv * 0.6, -dirv
            hit = tree.ray_cast(org, d, 1.2)
            if hit[0] is None:
                row.append(None)
                continue
            n = -d
            off = TW_OFF + 0.010 * max(0.0, min(1.0, (1.30 - hit[0].z) / 0.13))
            row.append(hit[0] + n * off)
        rows.append(row)
    b.free()
    for row in rows:
        for k in range(len(row)):
            if row[k] is None:
                nb = [row[q] for q in (k - 1, k + 1) if 0 <= q < len(row) and row[q] is not None]
                row[k] = sum(nb, Vector()) / len(nb) if nb else Vector((TW_X[0], 0, zc))
    for _ in range(4):
        for row in rows:
            row[:] = [row[0]] + [(row[k - 1] + row[k] * 2 + row[k + 1]) / 4 for k in range(1, len(row) - 1)] + [row[-1]]
    me = bpy.data.meshes.new("towel")
    o = bpy.data.objects.new("towel", me)
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    vs = [[bm.verts.new(p) for p in row] for row in rows]
    for j in range(NU - 1):
        for k in range(len(path) - 1):
            bm.faces.new((vs[j][k], vs[j + 1][k], vs[j + 1][k + 1], vs[j][k + 1]))
    bm.normal_update()
    top = [f for f in bm.faces if f.calc_center_median().z > zc + 0.05]
    if top and sum(f.normal.z for f in top) < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    m = o.modifiers.new("sol", "SOLIDIFY")
    m.thickness = TW_THICK
    m.offset = -1.0
    m.use_rim = True
    O["apply_mod"](o, m)
    for p in me.polygons:
        p.use_smooth = True
    O["set_mats"](o, ["Towel"], lambda p: 0)
    log("towel tris", O["tris"](o))
    return o


# ------------------------------------------------------------------ бутылка и стул
def lathe(bm, profile, seg, xf):
    """Тело вращения: profile — [(r, z)] снизу вверх; xf(Vector) → точка в мире. Возвращает грани."""
    rings = []
    for r, z in profile:
        ring = []
        for k in range(seg):
            a = 2 * math.pi * k / seg
            ring.append(bm.verts.new(xf(Vector((r * math.cos(a), r * math.sin(a), z)))))
        rings.append(ring)
    faces = []
    for i in range(len(rings) - 1):
        for k in range(seg):
            a, b = rings[i][k], rings[i][(k + 1) % seg]
            c, d = rings[i + 1][(k + 1) % seg], rings[i + 1][k]
            faces.append(bm.faces.new((a, b, c, d)))
    faces.append(bm.faces.new(list(reversed(rings[0]))))
    faces.append(bm.faces.new(rings[-1]))
    return faces


BOTTLE_PROFILE = [(BOT_R * 0.92, 0.0), (BOT_R, 0.008), (BOT_R, 0.13), (BOT_R * 0.96, 0.15), (BOT_R * 0.62, 0.178),
                  (BOT_R * 0.42, 0.188)]
CAP_PROFILE = [(BOT_R * 0.46, 0.186), (BOT_R * 0.50, 0.19), (BOT_R * 0.50, 0.205), (BOT_R * 0.40, BOT_L)]


def bottle_mesh(name, xf):
    """Бутылка 0.5 л: корпус (Bottle) и крышка-«спорт» (BottleCap); ось z профиля → xf."""
    me = bpy.data.meshes.new(name)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    body = lathe(bm, BOTTLE_PROFILE, 16, xf)
    cap = lathe(bm, CAP_PROFILE, 12, xf)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    for m in ("Bottle", "BottleCap"):
        me.materials.append(bpy.data.materials.get(m) or bpy.data.materials.new(m))
    nb = len(body)
    for i, p in enumerate(me.polygons):
        p.material_index = 0 if i < nb else 1
        p.use_smooth = True
    return o


def make_hand_bottle(arm):
    """Бутылка в левом кулаке (web CornerCrew: вдоль большого пальца, под ладонью). Ось — к большому пальцу."""
    wrist, x, y, z = O["hand_basis"](arm, "l")
    # сторона ладони: от кисти к большому пальцу и «вниз» к ладони — нормаль z или −z (та, что ближе к пальцам-
    # подушечкам): ладонь там, куда загибаются пальцы — проверяем по средней фаланге среднего пальца
    b = arm.data.bones
    mid1 = arm.matrix_world @ b["middle_01_l"].head_local
    mid3 = arm.matrix_world @ b["middle_03_l"].head_local
    bend = (mid3 - mid1) - y * (mid3 - mid1).dot(y)
    palm = z if bend.dot(z) >= 0 else -z
    centre = wrist + y * BOT_ALONG + palm * BOT_PALM
    axis = x.normalized()
    side = palm.cross(axis).normalized()
    base = centre - axis * (BOT_L * 0.55)
    log("bottle: wrist", tuple(round(v, 3) for v in wrist), "palm", tuple(round(v, 2) for v in palm),
        "axis", tuple(round(v, 2) for v in axis))

    def xf(p):
        return base + side * p.x + palm * p.y + axis * p.z

    o = bottle_mesh("hand_bottle", xf)
    O["rigid"](o, "hand_l")
    return o, centre


def make_static_bottle():
    o = bottle_mesh("bottle_static", lambda p: p)
    return o


def make_stool():
    me = bpy.data.meshes.new("stool")
    o = bpy.data.objects.new("stool", me)
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    seat_lo = STOOL_H - SEAT_T
    # сиденье: подушка со скруглённым краем (профиль вращения)
    seat = lathe(bm, [(0.0001, seat_lo), (SEAT_R - 0.012, seat_lo), (SEAT_R, seat_lo + 0.012), (SEAT_R, STOOL_H - 0.015),
                      (SEAT_R - 0.02, STOOL_H - 0.002), (SEAT_R * 0.6, STOOL_H), (0.0001, STOOL_H)], 24, lambda p: p)
    n_seat = len(seat)
    metal = []
    # 4 ноги — наклонные трубы от-под сиденья наружу к полу
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        top = Vector((LEG_TOP_R * math.cos(a), LEG_TOP_R * math.sin(a), seat_lo))
        bot = Vector((LEG_BOT_R * math.cos(a), LEG_BOT_R * math.sin(a), 0.0))
        d = (top - bot)
        L = d.length
        q = d.normalized().to_track_quat("Z", "Y")
        m = q.to_matrix()
        metal += lathe(bm, [(LEG_R, 0.0), (LEG_R * 0.85, L)], 8, lambda p, m=m, bot=bot: bot + m @ p)
    # кольцо-подножка (тор)
    seg, tseg = 24, 6
    rings = []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        cdir = Vector((math.cos(a), math.sin(a), 0))
        ring = []
        for j in range(tseg):
            b = 2 * math.pi * j / tseg
            p = cdir * (FOOT_RING_R + FOOT_RING_T * math.cos(b)) + Vector((0, 0, FOOT_RING_Z + FOOT_RING_T * math.sin(b)))
            ring.append(bm.verts.new(p))
        rings.append(ring)
    for i in range(seg):
        for j in range(tseg):
            a, b = rings[i][j], rings[(i + 1) % seg][j]
            c, d = rings[(i + 1) % seg][(j + 1) % tseg], rings[i][(j + 1) % tseg]
            metal.append(bm.faces.new((a, b, c, d)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    for m in ("StoolSeat", "StoolMetal"):
        me.materials.append(bpy.data.materials.get(m) or bpy.data.materials.new(m))
    for i, p in enumerate(me.polygons):
        p.material_index = 0 if i < n_seat else 1
        p.use_smooth = True
    log("stool tris", O["tris"](o))
    return o


def export_static(o, fname):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.export_scene.fbx(
        filepath=W + fname, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, axis_forward="-Z", axis_up="Y",
        mesh_smooth_type="FACE", use_tspace=False, use_mesh_modifiers=False, bake_anim=False)
    log("export static", fname, O["tris"](o), "tris")


# ------------------------------------------------------------------ превью
COLS = {"Jacket": (0.30, 0.03, 0.04, 1), "JacketTrim": (0.92, 0.92, 0.9, 1), "Zip": (0.75, 0.75, 0.78, 1),
        "Shirt": (0.70, 0.05, 0.06, 1), "ShirtTrim": (0.95, 0.95, 0.95, 1), "Towel": (0.95, 0.95, 0.92, 1),
        "Bottle": (0.80, 0.90, 0.96, 1), "BottleCap": (0.10, 0.35, 0.85, 1), "Trousers": (0.08, 0.09, 0.12, 1),
        "Belt": (0.02, 0.02, 0.02, 1), "Skin": (0.62, 0.45, 0.35, 1), "Gloves": (0.05, 0.05, 0.07, 1),
        "FaceSkin": (0.62, 0.45, 0.35, 1), "Shoes": (0.15, 0.16, 0.18, 1), "ShoesSole": (0.85, 0.85, 0.83, 1),
        "StoolSeat": (0.35, 0.03, 0.04, 1), "StoolMetal": (0.55, 0.57, 0.6, 1)}


def preview(sets):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.light = "STUDIO"
    sc.render.resolution_x = 800
    sc.render.resolution_y = 1000
    for n, c in COLS.items():
        m = bpy.data.materials.get(n)
        if m:
            m.diffuse_color = c
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    for name, (objs, shots) in sets.items():
        for o in bpy.data.objects:
            if o.type == "MESH":
                o.hide_render = o not in objs
        for sname, loc, look, ortho in shots:
            co.location = Vector(loc)
            co.rotation_euler = (Vector(look) - co.location).to_track_quat("-Z", "Y").to_euler()
            cam.type = "ORTHO"
            cam.ortho_scale = ortho
            sc.render.filepath = W + "crew_%s_%s.png" % (name, sname)
            bpy.ops.render.render(write_still=True)


BODY_SHOTS = [("front", (0, -4, 0.95), (0, 0, 0.95), 2.0), ("side", (4, 0, 0.95), (0, 0, 0.95), 2.0),
              ("back", (0, 4, 0.95), (0, 0, 0.95), 2.0), ("q34", (2.4, -3.0, 1.5), (0, 0, 1.15), 1.4),
              ("neck", (0.5, -1.2, 1.5), (0, 0, 1.42), 0.6)]


def main():
    os.makedirs(W, exist_ok=True)
    O["reset"]()
    arm, full = O["import_body"]()
    face = O["import_face"]()
    fm = bpy.data.materials.new("FaceSkin")
    face.data.materials.clear()
    face.data.materials.append(fm)
    rig = O["Rig"](arm)
    field = look_morphs.Field(full)

    # брюки и туфли (геометрия рефери), брюки — с ключами телосложения (футболка заправлена, куртка поверх)
    trousers = O["make_trousers"](full, rig, "crew")
    O["transfer_weights"](trousers, full)
    O["push_out"](trousers, [full], 0.010)
    shoes = O["make_shoes"](full)
    O["transfer_weights"](shoes, full)

    # тренер
    jacket = make_jacket(full, rig)
    O["transfer_weights"](jacket, full)
    push_out_near(jacket, [full, face, trousers], 0.008)
    worst_gap(jacket, full)
    body_coach = O["make_ref_body"](full, rig, "pro")
    O["set_mats"](body_coach, ["Skin"], lambda p: 0)

    # катмен: футболка + полотенце, предплечья + нитрил + бутылка
    tee = O["make_shirt"](full, rig, "am")
    O["transfer_weights"](tee, full)
    push_out_near(tee, [full, face], 0.006)
    worst_gap(tee, full)
    towel = make_towel(full)
    O["transfer_weights"](towel, full)
    push_out_near(towel, [tee], 0.010)
    worst_gap(towel, full)
    tee_towel = O["join"]([tee, towel], "tee_towel")
    body_cut = O["make_ref_body"](full, rig, "am")
    bottle, bcentre = make_hand_bottle(arm)
    body_cut = O["join"]([body_cut, bottle], "body_cutman")

    # ключи телосложения: всё, что на коже (бутылку не трогаем — она в кулаке, а не на коже)
    for o in (jacket, tee_towel, trousers, body_coach):
        look_morphs.add_keys(o, field)
    look_morphs.add_keys(body_cut, field, scale_fn=lambda p: 0.0 if (p - bcentre).length < BOT_L * 0.65 else 1.0)

    stool = make_stool()
    sbottle = make_static_bottle()

    sets = {
        "coach": ([jacket, trousers, shoes, body_coach, face], BODY_SHOTS),
        "cutman": ([tee_towel, trousers, shoes, body_cut, face], BODY_SHOTS + [
            ("hand", (bcentre.x + 0.35, bcentre.y - 0.45, bcentre.z + 0.1), tuple(bcentre), 0.4),
            ("towel", (0.9, -0.9, 1.6), (0.1, 0, 1.35), 0.7)]),
        "stool": ([stool, sbottle], [("q34", (1.6, -2.0, 1.0), (0, 0, 0.3), 0.9)]),
    }
    # бутылка-статика — рядом со стулом для превью
    sbottle.location = (0.35, 0, 0)
    preview(sets)
    sbottle.location = (0, 0, 0)

    skinned = [(jacket, "crew_jacket.fbx"), (tee_towel, "crew_tee_towel.fbx"), (trousers, "crew_trousers.fbx"),
               (body_coach, "crew_body_coach.fbx"), (body_cut, "crew_body_cutman.fbx")]
    objs = [o for o, _ in skinned]
    for o in objs:
        O["bind"](o, arm)
        log(o.name, "tris", O["tris"](o), [m.name for m in o.data.materials])
    O["to_cm"](arm, objs)
    for o, fn in skinned:
        O["export"](arm, o, fn)
    export_static(stool, "crew_stool.fbx")
    export_static(sbottle, "crew_bottle.fbx")
    bpy.ops.wm.save_as_mainfile(filepath=W + "crew.blend")
    log("done")


main()
