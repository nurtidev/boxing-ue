# -*- coding: utf-8 -*-
# S-56 (рефери и любительская форма), шаг 2 — Blender: одежда рефери и любительская форма боксёра на скелет
# MetaHuman (metahuman_base_skel). Тот же подход, что Tools/Blender/look_kit.py (S-41): оболочки по телу
# мокап-набора MetaHuman (веса скиннинга — с кожи), жёсткие вещи — на одной кости.
#
# Вход  (Saved/LookWork, выгружает Tools/EditorScripts/look_export.py из UE):
#   body_full.fbx — полное тело m_med_nrw (мокап-набор MetaHuman, с головой)
#   face.fbx      — меш лица Kellan (по нему садится шлем, от его шеи/«хомута» отталкивается одежда)
#   + web/public/models/headgear.glb, glove.glb — свои модели веба (tools/headgear, tools/gloves)
# Выход (Saved/LookWork):
#   рефери (спецификация — web Referee.tsx / tools/referee: рубашка, брюки, туфли, нитриловые перчатки):
#     ref_body_am.fbx / ref_body_pro.fbx — видимая кожа рефери: предплечья (любители, короткий рукав) или
#                       только запястья (профи) + кисти; материалы Skin / Gloves (нитрил — на кистях до манжеты)
#     ref_shirt_am.fbx  — футболка с коротким рукавом, вырез «под горло» (Shirt / ShirtTrim — бейка ворота)
#     ref_shirt_pro.fbx — рубашка с длинным рукавом, стойка воротника и бабочка (Shirt / ShirtTrim / BowTie)
#     ref_trousers.fbx  — брюки с поясом (Trousers / Belt); рубашка заправлена
#     ref_shoes.fbx     — туфли (Shoes / ShoesSole)
#   любительская форма боксёра (World Boxing: майка и шлем в цвет угла):
#     boxer_vest.fbx          — майка-синглет (Vest / VestTrim) — заправлена в трусы SKM_BoxerKit
#     boxer_vest_headgear.fbx — майка + шлем одним мешем (у визуального BP четыре слота, см. Docs/LOOK.md)
#     boxer_headgear.fbx      — шлем отдельно (своя геометрия веба, в git): HeadgearShell / HeadgearTrim /
#                               HeadgearStrap / HeadgearLogo, жёстко на кости head
#     boxer_gloves_am.fbx     — любительские перчатки: липучка + белая «мишень»-логотип, без шнуровки
#   previews: Saved/LookWork/out_*.png (workbench)
#
# Запуск:
#   "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup ^
#       --python C:\Users\user\Desktop\boxing-ue\Tools\Blender\look_outfits.py
# Производные тела MetaHuman (одежда, кожа, майка) → /Game/BoxingLocal (вне git); шлем и перчатки —
# своя геометрия → /Game/Boxing/Characters (в git).
import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(globals().get("__file__") or "C:/Users/user/Desktop/boxing-ue/Tools/Blender/x.py")))
import look_morphs  # noqa: E402  S-60: ключи телосложения Heavy/Lean/Muscular

ROOT = "C:/Users/user/Desktop/boxing-ue/"
W = ROOT + "Saved/LookWork/"
WEB = "C:/Users/user/Desktop/boxing/web/public/models/"
GLOVE_GLB = WEB + "glove.glb"
HEADGEAR_GLB = WEB + "headgear.glb"
ONLY = [x for x in os.environ.get("OUTFITS_ONLY", "").split(",") if x]   # ref,am — для быстрых итераций

ARM_LIMB = ("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky", "wrist")

# ---- рефери (метры, поза привязки MetaHuman A-pose; пол ≈ z −0.02, лицо — к −Y, левая сторона — +X) ----
SHIRT_HEM = 0.935          # заправлена в брюки
SHIRT_OFF = 0.014          # рубашка от кожи
SHIRT_OFF_TUCK = 0.004  # у заправленного края — плотнее (не выходит сквозь пояс брюк)
SHIRT_DRAPE = 0.9          # доля «добора» до выпуклой оболочки среза торса (0 — облегает мышцы)
SHIRT_DRAPE_TOP = 1.40     # выше — плечи/шея, не трогаем
TORSO_HALF_X = 0.185       # срез торса для «драпировки» — без рук (в A-позе руки дальше)
SLEEVE_AM_T = 0.48         # короткий рукав: доля плеча от плечевого сустава к локтю
SLEEVE_AM_FLARE = 0.016    # рукав футболки свободнее
SLEEVE_PRO_BACK = 0.025    # длинный рукав кончается на 2.5 см выше сустава кисти
NECK_AM = (1.405, 1.465)   # вырез футболки «под горло»: z спереди (y<0) и сзади
NECK_PRO = (1.425, 1.475)  # основание воротника рубашки
COLLAR_H = (0.042, 0.050)  # высота стойки воротника спереди/сзади
COLLAR_OFF = 0.013
TRIM_H = 0.016             # бейка ворота футболки
NECK_RX, NECK_RY = 0.085, 0.075   # вырез «под горло»: полуоси эллипса вокруг шеи (в плане)
SHIRT_CAP = 1.50           # плечи — не выше
TROUSER_TOP = 1.000
TROUSER_HEM = 0.098        # низ брючин — по верху туфель
TROUSER_OFF_TOP = 0.016
TROUSER_OFF_HEM = 0.030
BELT_H = 0.035
TUBE_TOP, TUBE_FULL = 0.72, 0.52   # от колена вниз брючина — прямая труба
TUBE_R_KNEE, TUBE_R_HEM = 0.078, 0.086
SHOE_TOP = 0.125
SHOE_OFF = 0.007
SOLE_Z = 0.012             # верх подошвы (пол ≈ −0.02)
GLOVE_CUFF = 0.035         # нитрил на 3.5 см выше сустава кисти
SKIN_OVERLAP = 0.03        # кожа заходит под край рукава

# ---- любительская майка (как web tools/human vest_pred) ----
VEST_BOT = 1.000           # заправлена в трусы (их верх 1.075)
VEST_OFF = 0.006
VEST_OFF_TUCK = 0.003
VEST_THICK = 0.004
VEST_SKIN_MARGIN = 0.03    # кожа под майкой удаляется не ближе 3 см к её краям
TOP_BOT = 1.135            # S-60: низ спортивного топа (под грудью ключа Female, look_morphs.FEM_BREAST_*)
GLOVE_SCALE = 1.0

# ---- шлем ----
HG_GAP = 0.004             # внутренняя стенка шлема — над кожей
HG_SLACK = 1.02


def log(*a):
    print("OUTFITS", *a)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def bake_world(o):
    """Меш в мировые координаты без родителя (кости здесь не нужны — поза привязки)."""
    mw = o.matrix_world.copy()
    o.parent = None
    o.matrix_world = Matrix.Identity(4)
    o.data.transform(mw)
    for m in list(o.modifiers):
        o.modifiers.remove(m)


def import_body():
    bpy.ops.import_scene.fbx(filepath=W + "body_full.fbx")
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.type == "MESH")
    top = arm.parent
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    if top is not None and top.type == "EMPTY":
        bpy.data.objects.remove(top, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    log("armature", arm.name, "mesh", mesh.name, len(mesh.data.vertices))
    return arm, mesh


def import_face():
    """Кожа лица Kellan (без глаз/зубов) в координатах тела; веса — для выбора «жёсткой» головы."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=W + "face.fbx")
    new = [o for o in bpy.data.objects if o not in before]
    fm = next(o for o in new if o.type == "MESH")
    bake_world(fm)
    for o in new:
        if o is not fm:
            bpy.data.objects.remove(o, do_unlink=True)
    cnt = {}
    for p in fm.data.polygons:
        cnt[p.material_index] = cnt.get(p.material_index, 0) + 1
    skin_idx = max(cnt, key=cnt.get)       # кожа головы — самая большая секция LOD0
    bm = bmesh.new()
    bm.from_mesh(fm.data)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != skin_idx], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(fm.data)
    bm.free()
    fm.name = "face_skin"
    log("face skin verts", len(fm.data.vertices), "material", skin_idx)
    return fm


def dup(obj, name):
    o = obj.copy()
    o.data = obj.data.copy()
    o.name = name
    o.data.name = name
    bpy.context.scene.collection.objects.link(o)
    return o


def group_sums(obj, prefixes, exact=()):
    idx = {g.index for g in obj.vertex_groups if g.name.startswith(prefixes) or g.name in exact}
    return [sum(g.weight for g in v.groups if g.group in idx) for v in obj.data.vertices]


def apply_mod(o, m):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier=m.name)


def bone_head(arm, name):
    return arm.matrix_world @ arm.data.bones[name].head_local


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length


# ------------------------------------------------------------------ оболочки
class Plane:
    def __init__(self, co, no):
        self.co = Vector(co)
        self.no = Vector(no).normalized()

    def d(self, p):
        return (p - self.co).dot(self.no)


def bisect(bm, plane):
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=plane.co, plane_no=plane.no)


def smooth(bm, iters, factor=0.5, pin_boundary=True):
    for _ in range(iters):
        verts = [v for v in bm.verts if not (pin_boundary and v.is_boundary)]
        bmesh.ops.smooth_vert(bm, verts=verts, factor=factor, use_axis_x=True, use_axis_y=True, use_axis_z=True)


def region_shell(src, name, coarse, cuts, keep, offset_fn, thick, smooth_iters=8, horiz=1.0, drop_w=None,
                 rim_iters=0, post=None):
    """Оболочка по участку кожи: грубый отбор → ровные разрезы плоскостями → точный отбор по центрам граней →
    сглаживание (складки ткани не повторяют мышцы) → отступ по нормали → толщина внутрь с кромкой.
    drop_w — веса вершин исходника: грани со средним весом > 0.5 отбрасываются (руки у майки)."""
    o = dup(src, name)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    for g in list(o.vertex_groups):
        o.vertex_groups.remove(g)

    def drop(f):
        if not coarse(f.calc_center_median()):
            return True
        return drop_w is not None and sum(drop_w[v.index] for v in f.verts) / len(f.verts) > 0.5

    bmesh.ops.delete(bm, geom=[f for f in bm.faces if drop(f)], context="FACES")
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-4)      # швы UV
    for pl in cuts:
        bisect(bm, pl)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not keep(f.calc_center_median())], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    # мелкие острова (куски у края разреза) — прочь
    drop_small(bm, 30)
    if rim_iters:
        # край по граням — «лесенкой» по сетке тела: сперва срезаем «зубцы» (грани с 2+ граничными рёбрами),
        # потом сглаживаем саму кромку вдоль неё (1D Лаплас по граничным соседям)
        for _ in range(3):
            teeth = [f for f in bm.faces if sum(1 for e in f.edges if e.is_boundary) >= 2]
            if not teeth:
                break
            bmesh.ops.delete(bm, geom=teeth, context="FACES")
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
        # мелкие дыры внутри участка (подмышка, пупок) — закрыть, иначе их кромка схлопнется в «шип»
        bmesh.ops.holes_fill(bm, edges=bm.edges[:], sides=16)
        loops = boundary_loops(bm)
        rim = {v for vs, perim in loops if perim > 0.25 for v in vs}
        orig = {v: v.co.copy() for v in rim}
        for _ in range(rim_iters):
            new = {}
            for v in rim:
                nb = [e.other_vert(v) for e in v.link_edges if e.is_boundary]
                if len(nb) == 2:
                    co = v.co * 0.5 + (nb[0].co + nb[1].co) * 0.25
                    dv = co - orig[v]
                    if dv.length > RIM_MAX_MOVE:
                        co = orig[v] + dv.normalized() * RIM_MAX_MOVE
                    new[v] = co
            for v, co in new.items():
                v.co = co
    smooth(bm, smooth_iters, 0.5)
    # сглаживание «усаживает» поверхность внутрь тела — возвращаем на кожу (иначе push_out потом выдавливает
    # отдельные вершины и у края появляются ступеньки)
    tree = skin_tree(src)
    for v in bm.verts:
        hit = tree.find_nearest(v.co)
        if hit[0] is not None:
            s = (v.co - hit[0]).dot(hit[1])
            if s < 0:
                v.co -= hit[1] * s
    bm.normal_update()
    for v in bm.verts:
        n = v.normal.copy()
        n.z *= horiz
        if n.length > 1e-6:
            n.normalize()
        v.co += n * offset_fn(v.co)
    if post:
        post(bm)
    bm.to_mesh(o.data)
    bm.free()
    o.data.materials.clear()
    if thick > 0:
        m = o.modifiers.new("sol", "SOLIDIFY")
        m.thickness = thick
        m.offset = -1.0
        m.use_rim = True
        m.use_even_offset = True
        apply_mod(o, m)
    for p in o.data.polygons:
        p.use_smooth = True
    return o


RIM_MAX_MOVE = 0.015      # сглаживание кромки сдвигает вершину не дальше 1.5 см


def boundary_loops(bm):
    """Связные контуры граничных рёбер: [(вершины, периметр)]."""
    seen = set()
    out = []
    for e in bm.edges:
        if not e.is_boundary or e in seen:
            continue
        verts, perim, stack = set(), 0.0, [e]
        seen.add(e)
        while stack:
            x = stack.pop()
            perim += x.calc_length()
            for v in x.verts:
                verts.add(v)
                for y in v.link_edges:
                    if y.is_boundary and y not in seen:
                        seen.add(y)
                        stack.append(y)
        out.append((verts, perim))
    return out


_TREES = {}


def skin_tree(src):
    if src.name not in _TREES:
        b = bmesh.new()
        b.from_mesh(src.data)
        _TREES[src.name] = (BVHTree.FromBMesh(b), b)
    return _TREES[src.name][0]


def drop_small(bm, min_verts):
    seen = set()
    kill = []
    for v in bm.verts:
        if v in seen:
            continue
        comp, stack = [], [v]
        seen.add(v)
        while stack:
            x = stack.pop()
            comp.append(x)
            for e in x.link_edges:
                y = e.other_vert(x)
                if y not in seen:
                    seen.add(y)
                    stack.append(y)
        if len(comp) < min_verts:
            kill += comp
    if kill:
        bmesh.ops.delete(bm, geom=kill, context="VERTS")


def push_out(o, skins, gap):
    """Наружная стенка не ближе gap к коже (тело мокап-набора и шея/«хомут» лица Kellan)."""
    trees = []
    for s in skins:
        bm_s = bmesh.new()
        bm_s.from_mesh(s.data)
        trees.append((BVHTree.FromBMesh(bm_s), bm_s))
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.normal_update()
    moved = 0
    for v in bm.verts:
        best = None
        for tree, _ in trees:
            hit = tree.find_nearest(v.co)
            if hit[0] is not None and (best is None or hit[3] < best[3]):
                best = hit
        if best is None:
            continue
        p, n = best[0], best[1]
        if v.normal.dot(n) < 0.2:
            continue                    # внутренняя стенка
        s = (v.co - p).dot(n)
        if s < gap:
            v.co += n * (gap - s)
            moved += 1
    bm.to_mesh(o.data)
    bm.free()
    for _, b in trees:
        b.free()
    log("push_out", o.name, moved)


def transfer_weights(dst, src):
    for g in list(dst.vertex_groups):
        dst.vertex_groups.remove(g)
    m = dst.modifiers.new("dt", "DATA_TRANSFER")
    m.object = src
    m.use_vert_data = True
    m.data_types_verts = {"VGROUP_WEIGHTS"}
    m.vert_mapping = "POLYINTERP_NEAREST"
    m.layers_vgroup_select_src = "ALL"
    m.layers_vgroup_select_dst = "NAME"
    bpy.ops.object.select_all(action="DESELECT")
    dst.select_set(True)
    bpy.context.view_layer.objects.active = dst
    bpy.ops.object.datalayout_transfer(modifier=m.name)
    apply_mod(dst, m)
    bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=8)
    bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)


def rigid(o, bone):
    for g in list(o.vertex_groups):
        o.vertex_groups.remove(g)
    vg = o.vertex_groups.new(name=bone)
    vg.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")


def set_mats(o, names, pick):
    o.data.materials.clear()
    for n in names:
        o.data.materials.append(bpy.data.materials.get(n) or bpy.data.materials.new(n))
    for p in o.data.polygons:
        p.material_index = pick(p)


def join(objs, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.name = name
    o.data.name = name
    return o


def remesh(o, voxel, faces, smooth_iters=3):
    m = o.modifiers.new("rm", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.adaptivity = 0.0
    apply_mod(o, m)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    smooth(bm, smooth_iters, 0.5, pin_boundary=False)
    bm.to_mesh(o.data)
    bm.free()
    n = len(o.data.polygons)
    if n > faces:
        m = o.modifiers.new("dc", "DECIMATE")
        m.ratio = faces / n
        apply_mod(o, m)


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


# ------------------------------------------------------------------ геометрия тела
class Rig:
    def __init__(self, arm):
        self.arm = arm
        h = lambda n: bone_head(arm, n)   # noqa: E731
        self.neck = h("neck_01")
        self.head = h("head")
        self.front_y = h("spine_04").y
        self.side = {}
        for s, sx in (("l", 1.0), ("r", -1.0)):
            self.side[s] = {"sx": sx, "shoulder": h("upperarm_" + s), "elbow": h("lowerarm_" + s),
                            "wrist": h("hand_" + s), "tip": h("middle_01_" + s)}

    def arm_plane(self, s, t=None, back=None):
        """Плоскость поперёк руки: t — доля плеча от сустава к локтю; back — метры до сустава кисти."""
        a = self.side[s]
        if t is not None:
            d = (a["elbow"] - a["shoulder"]).normalized()
            return Plane(a["shoulder"] + (a["elbow"] - a["shoulder"]) * t, d)
        d = (a["wrist"] - a["elbow"]).normalized()
        return Plane(a["wrist"] - d * back, d)

    def on_arm(self, s, p, r=0.085):
        a = self.side[s]
        return min(seg_dist(p, a["shoulder"], a["elbow"]), seg_dist(p, a["elbow"], a["wrist"]),
                   seg_dist(p, a["wrist"], a["tip"] + (a["tip"] - a["wrist"]) * 0.6)) < r


def neck_z(y, lo_hi):
    """Линия выреза/воротника: спереди (−Y) ниже, сзади выше — плавно по y."""
    lo, hi = lo_hi
    return (lo + hi) * 0.5 + (hi - lo) / 0.16 * y


def neck_plane(lo_hi):
    """Та же линия выреза как плоскость (ровный край разрезом): z = a + k·y."""
    lo, hi = lo_hi
    k = (hi - lo) / 0.16
    return Plane((0, 0, (lo + hi) * 0.5), (0, -k, 1))


# ------------------------------------------------------------------ рефери
def make_shirt(full, rig, kind):
    pro = kind == "pro"
    neck = NECK_PRO if pro else NECK_AM
    planes = {s: (rig.arm_plane(s, back=SLEEVE_PRO_BACK) if pro else rig.arm_plane(s, t=SLEEVE_AM_T)) for s in "lr"}
    top_hi = (neck[0] + COLLAR_H[0], neck[1] + COLLAR_H[1]) if pro else neck

    def beyond(c):
        for s, pl in planes.items():
            if pl.d(c) > 0 and rig.on_arm(s, c):
                return True
        return False

    ncy = rig.neck.y - 0.005

    def in_neck(c, grow=0.0):
        """Внутри «цилиндра» шеи (эллипс в плане): здесь вырез/воротник; плечи снаружи — до SHIRT_CAP."""
        return (c.x / (NECK_RX + grow)) ** 2 + ((c.y - ncy) / (NECK_RY + grow)) ** 2 < 1.0

    def keep(c):
        if c.z < SHIRT_HEM or c.z > SHIRT_CAP:
            return False
        if in_neck(c) and c.z > neck_z(c.y, top_hi):
            return False
        return not beyond(c)

    # разрезы по вырезу — ломаная из плоскостей по y (воротник — поверхность, ровный край ему не нужен)
    cuts = [Plane((0, 0, SHIRT_HEM), (0, 0, 1)), neck_plane(top_hi)] + list(planes.values())
    if pro:
        cuts.append(neck_plane(neck))
    else:
        cuts.append(neck_plane((neck[0] - TRIM_H, neck[1] - TRIM_H)))

    def off(co):
        if co.z < 1.0:
            t = max(0.0, min(1.0, (co.z - SHIRT_HEM) / (1.0 - SHIRT_HEM)))
            base = SHIRT_OFF_TUCK + (SHIRT_OFF - SHIRT_OFF_TUCK) * t
        else:
            base = SHIRT_OFF
        for s in "lr":
            if rig.on_arm(s, co, 0.1) and planes[s].d(co) > -0.25:
                if not pro:
                    # к краю короткого рукава — свободнее
                    base = max(base, SHIRT_OFF + SLEEVE_AM_FLARE * max(0.0, min(1.0, 1 + planes[s].d(co) / 0.08)))
                else:
                    base = max(base, 0.012)
        if pro and co.z > neck_z(co.y, neck) - 0.004 and in_neck(co, 0.02):
            base = COLLAR_OFF
        return base

    headw = group_sums(full, ("FACIAL_",), exact=("head",))

    def drape(bm):
        loosen_torso(bm, rig, 1.02, SHIRT_DRAPE_TOP, SHIRT_DRAPE)

    o = region_shell(full, "shirt_" + kind, lambda c: c.z > SHIRT_HEM - 0.03 and c.z < 1.6, cuts, keep, off,
                     0.004, smooth_iters=40, rim_iters=10, drop_w=headw, post=drape)
    if pro:
        set_mats(o, ["Shirt", "ShirtTrim", "BowTie"],
                 lambda p: 1 if (p.center.z > neck_z(p.center.y, neck) - 0.002 and in_neck(Vector(p.center), 0.02)) else 0)
    else:
        near = rim_near(o, 1.35)
        set_mats(o, ["Shirt", "ShirtTrim"], lambda p: 1 if near(Vector(p.center), TRIM_H) else 0)
    return o


def rim_near(o, zmin):
    """Проверка «грань у края выреза» (бейка/окантовка): расстояние до граничных вершин выше zmin."""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    pts = [v.co.copy() for v in bm.verts if v.co.z > zmin and any(len(e.link_faces) == 1 for e in v.link_edges)]
    bm.free()
    if not pts:
        return lambda c, w: False
    kd = KDTree(len(pts))
    for i, p in enumerate(pts):
        kd.insert(p, i)
    kd.balance()
    return lambda c, w: c.z > zmin - w and kd.find(c)[2] < w


def hull2d(pts):
    """Выпуклая оболочка точек на плоскости (монотонная цепь), против часовой."""
    pts = sorted(set(pts))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]


def ray_hull(c, d, hull):
    """Расстояние от c по направлению d до границы выпуклого многоугольника (c внутри)."""
    best = None
    n = len(hull)
    for i in range(n):
        a, b = hull[i], hull[(i + 1) % n]
        ex, ey = b[0] - a[0], b[1] - a[1]
        den = d[0] * ey - d[1] * ex
        if abs(den) < 1e-9:
            continue
        t = ((a[0] - c[0]) * ey - (a[1] - c[1]) * ex) / den
        u = ((a[0] - c[0]) * d[1] - (a[1] - c[1]) * d[0]) / den
        if t > 0 and -1e-6 <= u <= 1 + 1e-6 and (best is None or t < best):
            best = t
    return best


def loosen_torso(bm, rig, z0, z1, amount):
    """Ткань не лезет во впадины (ложбинка между грудными, пресс, желобок позвоночника): каждый горизонтальный
    срез торса (без рук) тянется к своей выпуклой оболочке — рубашка/футболка висит, а не обтягивает мышцы."""
    BIN = 0.02
    torso = [v for v in bm.verts if z0 < v.co.z < z1 and abs(v.co.x) < TORSO_HALF_X
             and not (rig.on_arm("l", v.co, 0.1) or rig.on_arm("r", v.co, 0.1))]
    bins = {}
    for v in torso:
        bins.setdefault(int(v.co.z / BIN), []).append(v)
    moved = 0
    for k, vs in bins.items():
        pts = [(v.co.x, v.co.y) for v in vs]
        nb = bins.get(k - 1, []) + bins.get(k + 1, [])
        pts += [(v.co.x, v.co.y) for v in nb]
        if len(pts) < 8:
            continue
        hull = hull2d(pts)
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        w = min(1.0, max(0.0, (z1 - vs[0].co.z) / 0.06))     # к плечам — мягко на нет
        for v in vs:
            dx, dy = v.co.x - cx, v.co.y - cy
            r = math.hypot(dx, dy)
            if r < 1e-6:
                continue
            rh = ray_hull((cx, cy), (dx / r, dy / r), hull)
            if rh and rh > r:
                f = (rh - r) * amount * w
                v.co.x += dx / r * f
                v.co.y += dy / r * f
                moved += 1
    smooth(bm, 6, 0.5)
    log("loosen torso: вершин", moved)


def make_bowtie(collar_front, neck_mid_z):
    """Бабочка: узел + два «крыла» (трапеции), плоско по фронту воротника."""
    me = bpy.data.meshes.new("bowtie")
    bm = bmesh.new()
    knot = bmesh.ops.create_cube(bm, size=1.0)["verts"]
    for v in knot:
        v.co = Vector((v.co.x * 0.020, v.co.y * 0.012, v.co.z * 0.017))
    for sgn in (-1, 1):
        res = bmesh.ops.create_cube(bm, size=1.0)["verts"]
        for v in res:
            x = (v.co.x + 0.5) * 0.050          # 0..5 см от узла
            h = 0.011 + 0.012 * (x / 0.050)       # к концу шире
            z = v.co.z * 2 * h
            # «бантовая» вогнутость: середина крыла чуть тоньше по высоте
            v.co = Vector((sgn * (0.008 + x), v.co.y * 0.010 - 0.001 * (x / 0.05), z))
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
    smooth(bm, 2, 0.4, pin_boundary=False)
    for v in bm.verts:
        v.co += Vector((0, collar_front.y - 0.009, neck_mid_z))
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new("bowtie", me)
    bpy.context.scene.collection.objects.link(o)
    for p in o.data.polygons:
        p.use_smooth = True
    return o


def collar_front_point(shirt, z):
    best = None
    for v in shirt.data.vertices:
        if abs(v.co.x) < 0.012 and abs(v.co.z - z) < 0.01:
            if best is None or v.co.y < best.y:
                best = v.co.copy()
    return best


def make_trousers(full, rig, kind):
    def keep(c):
        return TROUSER_HEM <= c.z <= TROUSER_TOP and abs(c.x) < 0.3 and not (rig.on_arm("l", c) or rig.on_arm("r", c))

    def off(co):
        t = max(0.0, min(1.0, (TROUSER_TOP - co.z) / (TROUSER_TOP - TROUSER_HEM)))
        return TROUSER_OFF_TOP + (TROUSER_OFF_HEM - TROUSER_OFF_TOP) * (t ** 1.3)

    cuts = [Plane((0, 0, TROUSER_TOP), (0, 0, 1)), Plane((0, 0, TROUSER_HEM), (0, 0, 1)),
            Plane((0, 0, TROUSER_TOP - BELT_H), (0, 0, 1))]
    axis = {}
    for s, sx in (("l", 1), ("r", -1)):
        axis[sx] = [bone_head(rig.arm, b + "_" + s) for b in ("thigh", "calf", "foot")]

    def centre(sx, z):
        a, b, c = axis[sx]
        if z >= b.z:
            t = (z - b.z) / (a.z - b.z)
            return b + (a - b) * t
        t = (z - c.z) / (b.z - c.z)
        return c + (b - c) * t

    def tube(bm):
        """Брючины — прямые трубы ниже колена (а не лосины по икрам): радиус от оси ноги не меньше R(z)."""
        for v in bm.verts:
            z = v.co.z
            if z > TUBE_TOP:
                continue
            w = 1.0 if z < TUBE_FULL else (TUBE_TOP - z) / (TUBE_TOP - TUBE_FULL)
            w = w * w * (3 - 2 * w)
            sx = 1 if v.co.x > 0 else -1
            cc = centre(sx, z)
            d = Vector((v.co.x - cc.x, v.co.y - cc.y, 0))
            r = d.length
            if r < 1e-5:
                continue
            k = max(0.0, min(1.0, (z - TROUSER_HEM) / (TUBE_FULL - TROUSER_HEM)))
            rr = TUBE_R_HEM + (TUBE_R_KNEE - TUBE_R_HEM) * k
            if r < rr:
                v.co += d / r * (rr - r) * w
        smooth(bm, 6, 0.5)
        # пояс и низ брючин — ровные: граничные вершины обратно на плоскости разреза (сдвиг по нормали их разносил)
        for v in bm.verts:
            if v.is_boundary:
                for zc in (TROUSER_TOP, TROUSER_HEM):
                    if abs(v.co.z - zc) < 0.025:
                        v.co.z = zc

    o = region_shell(full, "trousers_" + kind, lambda c: TROUSER_HEM - 0.03 < c.z < TROUSER_TOP + 0.03, cuts, keep, off,
                     0.010, smooth_iters=24, horiz=0.6, post=tube)
    set_mats(o, ["Trousers", "Belt"], lambda p: 1 if p.center.z > TROUSER_TOP - BELT_H else 0)
    return o


def make_shoes(full):
    shoes = []
    for sx in (1, -1):
        o = dup(full, "shoe")
        bm = bmesh.new()
        bm.from_mesh(o.data)
        keep = [v for v in bm.verts if v.co.z < SHOE_TOP and v.co.x * sx > 0.02]
        pts = [v.co.copy() for v in keep]
        bm.free()
        bm = bmesh.new()
        for p in pts:
            bm.verts.new(p)
        bmesh.ops.convex_hull(bm, input=bm.verts[:], use_existing_faces=False)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        bm.normal_update()
        for v in bm.verts:
            v.co += v.normal * SHOE_OFF
        bm.to_mesh(o.data)
        bm.free()
        for g in list(o.vertex_groups):
            o.vertex_groups.remove(g)
        remesh(o, 0.005, 1400, smooth_iters=4)
        shoes.append(o)
    o = join(shoes, "shoes")
    # подошва — ровной полосой: разрез по плоскости SOLE_Z, всё ниже — ShoesSole
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bisect(bm, Plane((0, 0, SOLE_Z), (0, 0, 1)))
    bm.to_mesh(o.data)
    bm.free()
    set_mats(o, ["Shoes", "ShoesSole"], lambda p: 1 if p.center.z < SOLE_Z else 0)
    return o


def make_ref_body(full, rig, kind):
    """Видимая кожа рефери — только руки за краем рукава (с заходом под него), кисти — нитрил."""
    pro = kind == "pro"
    o = dup(full, "refbody_" + kind)
    planes = {}
    for s in "lr":
        pl = rig.arm_plane(s, back=SLEEVE_PRO_BACK) if pro else rig.arm_plane(s, t=SLEEVE_AM_T)
        planes[s] = Plane(pl.co - pl.no * SKIN_OVERLAP, pl.no)
    glove = {s: rig.arm_plane(s, back=GLOVE_CUFF) for s in "lr"}
    bm = bmesh.new()
    bm.from_mesh(o.data)
    uv = bm.loops.layers.uv.active

    def visible(f):
        c = f.calc_center_median()
        if sum(l[uv].uv.x for l in f.loops) / len(f.loops) < 1.0:
            return False                           # тайл головы (её даёт меш лица)
        return any(planes[s].d(c) > 0 and rig.on_arm(s, c, 0.11) for s in "lr")

    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not visible(f)], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    for s in "lr":                                   # ровная манжета нитрила — разрез по плоскости
        bisect(bm, glove[s])
    bm.to_mesh(o.data)
    bm.free()

    def pick(p):
        c = Vector(p.center)
        return 1 if any(glove[s].d(c) > 0 and rig.on_arm(s, c, 0.11) for s in "lr") else 0

    set_mats(o, ["Skin", "Gloves"], pick)
    return o


# ------------------------------------------------------------------ любительская форма
def make_vest(full, rig, bot=None, name="vest", mat="Vest"):
    """Майка любителя (bot=None → VEST_BOT, заправлена в трусы) или спортивный топ профи-женщины (bot — под грудью,
    S-60). Возвращает меш и функцию «кожа целиком под ней»."""
    VEST_BOT = bot if bot is not None else globals()["VEST_BOT"]
    sh = rig.side["l"]["shoulder"]
    shoulder_x = abs(sh.x)
    armpit_z = sh.z - 0.07
    neck = rig.neck.z
    strap_in, strap_out = shoulder_x * 0.36, shoulder_x * 0.62
    neck_f, neck_b, strap_top = neck - 0.15, neck - 0.07, neck + 0.03
    front_y = rig.front_y
    limb = group_sums(full, ARM_LIMB)

    sx_full = shoulder_x * 1.02
    hole_z0 = armpit_z - 0.05            # низ проймы
    neck_w = strap_in * 1.2              # полуширина выреза горловины

    def neck_line(x, y):
        t = max(0.0, min(1.0, (y - front_y + 0.03) / 0.06))
        base = neck_f + (neck_b - neck_f) * t * t * (3 - 2 * t)
        if x >= neck_w:
            return 9.0
        return base + (strap_top + 0.03 - base) * (x / neck_w) ** 2.2

    def hole_x(z):
        if z <= hole_z0:
            return sx_full
        s = min(1.0, (z - hole_z0) / (strap_top - hole_z0))
        return strap_out + (sx_full - strap_out) * math.sqrt(max(0.0, 1 - s * s))

    def keep(c):
        x, z = abs(c.x), c.z
        if z < VEST_BOT or z > strap_top + 0.03:
            return False
        if x > hole_x(z):
            return False
        return z <= neck_line(x, c.y)

    cuts = [Plane((0, 0, VEST_BOT), (0, 0, 1))]

    def off(co):
        if co.z < VEST_BOT + 0.09:
            t = max(0.0, min(1.0, (co.z - VEST_BOT) / 0.09))
            return VEST_OFF_TUCK + (VEST_OFF - VEST_OFF_TUCK) * t
        return VEST_OFF

    o = region_shell(full, name, lambda c: c.z > VEST_BOT - 0.03 and c.z < neck + 0.08, cuts, keep, off,
                     VEST_THICK, smooth_iters=6, drop_w=limb, rim_iters=30)
    set_mats(o, [mat], lambda p: 0)

    def under(c, w):
        """Кожа целиком под майкой (с запасом VEST_SKIN_MARGIN от краёв) — её можно не рисовать."""
        x, z = abs(c.x), c.z
        m = VEST_SKIN_MARGIN
        if w > 0.3 or z < VEST_BOT + 0.02 or z > strap_top - m:
            return False
        return x <= hole_x(z + m) - m and z <= neck_line(x + m, c.y) - m

    return o, under


def make_boxer_body_am(full, under):
    """Тело боксёра-любителя: как SKM_BoxerBody (look_kit.py: без головы, кистей, стоп), но без кожи под майкой —
    в замахах и уклонах кожа (широчайшие, грудь) больше не проходит сквозь майку."""
    ns = {"__name__": "look_kit_lib"}
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "look_kit.py"), encoding="utf-8").read()
    exec(compile(src.replace("\nmain()\n", "\n"), "look_kit.py", "exec"), ns)      # функции S-41 без запуска main
    o = dup(full, "boxer_body_am")
    limb = group_sums(o, ARM_LIMB)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    kill = [f for f in bm.faces if under(f.calc_center_median(), max(limb[v.index] for v in f.verts))]
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(o.data)
    bm.free()
    log("boxer body am: под майкой убрано граней", len(kill))
    ns["strip_head"](o)
    ns["drop_islands"](o, 60)
    ns["trim_body"](o)
    return o


def hand_basis(arm, side):
    b = arm.data.bones
    mw = arm.matrix_world
    wrist = mw @ b["hand_" + side].head_local
    mid = mw @ b["middle_01_" + side].head_local
    thumb = mw @ b["thumb_01_" + side].head_local
    y = (mid - wrist).normalized()
    t = thumb - wrist
    x = (t - y * t.dot(y)).normalized()
    if side == "l":
        x = -x
    z = x.cross(y).normalized()
    return wrist, x, y, z


def make_gloves_am(arm):
    """Перчатки любителя (web glove.ts: kit amateur — липучка strap + логотип, без шнуровки laces)."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=GLOVE_GLB)
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    parts = [o for o in new if not o.name.startswith("glove_laces")]
    for o in new:
        if o not in parts:
            bpy.data.objects.remove(o, do_unlink=True)
    rename = {"leather": "GloveLeather", "trim": "GloveTrim", "laces": "GloveLaces", "tape": "GloveTape",
              "strap": "GloveStrap", "logo": "GloveLogo"}
    sides = []
    for side in ("r", "l"):
        wrist, x, y, z = hand_basis(arm, side)
        for p in parts:
            o = p.copy()
            o.data = p.data.copy()
            bpy.context.scene.collection.objects.link(o)
            me = o.data
            mw = p.matrix_world.copy()
            for v in me.vertices:
                bw = mw @ v.co
                g = Vector((bw.x, bw.z, -bw.y)) * GLOVE_SCALE
                if side == "l":
                    g.x = -g.x
                v.co = wrist + x * g.x + y * g.y + z * g.z
            o.matrix_world = Matrix.Identity(4)
            o.parent = None
            if side == "l":
                me.flip_normals()
            for i, m in enumerate(me.materials):
                nm = rename.get(m.name.split(".")[0], "GloveLeather")
                me.materials[i] = bpy.data.materials.get(nm) or bpy.data.materials.new(nm)
            rigid(o, "hand_" + side)
            sides.append(o)
    for p in parts:
        bpy.data.objects.remove(p, do_unlink=True)
    for o in list(bpy.data.objects):
        if o not in before and o not in sides and o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    return join(sides, "gloves_am")


def make_headgear(face):
    """Шлем веба (headgear_M) на голову Kellan: как web headgear.ts — центр/RMS-радиус «жёсткой» головы против
    эталона fit_c/fit_r из GLB, затем радиальная подгонка по живой голове (внутренняя стенка — на HG_GAP над
    кожей во всех направлениях): голова MetaHuman по форме не та, что у MakeHuman."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=HEADGEAR_GLB)
    new = [o for o in bpy.data.objects if o not in before]
    root = next(o for o in new if o.name == "headgear_M")
    fc = root["fit_c"]
    fit_c = Vector((fc[0], -fc[2], fc[1]))           # glTF (Y вверх, +Z лицо) → Blender (Z вверх, −Y лицо)
    fit_r = float(root["fit_r"])
    parts = [o for o in new if o.type == "MESH" and o.parent is root]
    for o in parts:
        bake_world(o)
    for o in new:
        if o not in parts:
            bpy.data.objects.remove(o, do_unlink=True)
    rename = {"shell": "HeadgearShell", "trim": "HeadgearTrim", "strap": "HeadgearStrap", "logo": "HeadgearLogo"}
    for o in parts:
        for i, m in enumerate(o.data.materials):
            nm = rename.get(m.name.split(".")[0], "HeadgearShell")
            o.data.materials[i] = bpy.data.materials.get(nm) or bpy.data.materials.new(nm)
    hg = join(parts, "headgear")

    # «жёсткая» голова Kellan: вершины кожи, целиком на head + FACIAL_* (без шеи)
    ws = group_sums(face, ("FACIAL_",), exact=("head",))
    pts = [v.co.copy() for v, w in zip(face.data.vertices, ws) if w >= 0.99]
    c = sum(pts, Vector()) / len(pts)
    r = math.sqrt(sum((p - c).length_squared for p in pts) / len(pts))
    s = r / fit_r * HG_SLACK
    log("headgear fit: kellan c", tuple(round(x, 4) for x in c), "r", round(r, 4), "| glb c", tuple(round(x, 4) for x in fit_c),
        "r", round(fit_r, 4), "scale", round(s, 3), "pts", len(pts))
    for v in hg.data.vertices:
        v.co = c + (v.co - fit_c) * s

    # радиальная подгонка: δ(направление) = (кожа + зазор) − внутренняя стенка, гладко по сфере
    bm_f = bmesh.new()
    bm_f.from_mesh(face.data)
    skin_tree = BVHTree.FromBMesh(bm_f)
    rigid_face = [all(ws[v.index] >= 0.95 for v in f.verts) for f in bm_f.faces]
    bm_h = bmesh.new()
    bm_h.from_mesh(hg.data)
    hg_tree = BVHTree.FromBMesh(bm_h)
    NA, NE = 72, 36

    def dirv(ia, ie):
        az = ia / NA * 2 * math.pi
        el = -math.pi / 2 + (ie + 0.5) / NE * math.pi
        return Vector((math.cos(el) * math.sin(az), -math.cos(el) * math.cos(az), math.sin(el)))

    delta = [[None] * NE for _ in range(NA)]
    hits = 0
    for ia in range(NA):
        for ie in range(NE):
            d = dirv(ia, ie)
            # внутренняя стенка шлема: первое пересечение изнутри
            hi = hg_tree.ray_cast(c, d, 0.4)
            if hi[0] is None:
                continue
            r_in = (hi[0] - c).length
            # кожа: самое внешнее пересечение (луч снаружи внутрь)
            hs = skin_tree.ray_cast(c + d * 0.4, -d, 0.4)
            if hs[0] is None:
                continue
            if not rigid_face[hs[2]]:
                continue                 # шея/хомут — не голова: подгонку берём у соседей
            r_sk = (hs[0] - c).length
            delta[ia][ie] = max(-0.02, min(0.02, r_sk + HG_GAP - r_in))
            hits += 1
    # дыры (открытое лицо, низ) — заполнить средним соседей, затем сгладить
    for _ in range(60):
        changed = False
        nd = [row[:] for row in delta]
        for ia in range(NA):
            for ie in range(NE):
                if delta[ia][ie] is not None:
                    continue
                nb = [delta[(ia + da) % NA][ie + de] for da, de in ((1, 0), (-1, 0), (0, 1), (0, -1))
                      if 0 <= ie + de < NE and delta[(ia + da) % NA][ie + de] is not None]
                if nb:
                    nd[ia][ie] = sum(nb) / len(nb)
                    changed = True
        delta = nd
        if not changed:
            break
    delta = [[x if x is not None else 0.0 for x in row] for row in delta]
    for _ in range(10):
        nd = [row[:] for row in delta]
        for ia in range(NA):
            for ie in range(NE):
                acc, n = delta[ia][ie] * 2, 2
                for da, de in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if 0 <= ie + de < NE:
                        acc += delta[(ia + da) % NA][ie + de]
                        n += 1
                nd[ia][ie] = acc / n
        delta = nd

    def sample(d):
        el = math.asin(max(-1.0, min(1.0, d.z)))
        az = math.atan2(d.x, -d.y) % (2 * math.pi)
        fa = az / (2 * math.pi) * NA
        fe = (el + math.pi / 2) / math.pi * NE - 0.5
        a0 = int(math.floor(fa)) % NA
        a1 = (a0 + 1) % NA
        ta = fa - math.floor(fa)
        e0 = max(0, min(NE - 1, int(math.floor(fe))))
        e1 = max(0, min(NE - 1, e0 + 1))
        te = max(0.0, min(1.0, fe - math.floor(fe)))
        return ((delta[a0][e0] * (1 - ta) + delta[a1][e0] * ta) * (1 - te) +
                (delta[a0][e1] * (1 - ta) + delta[a1][e1] * ta) * te)

    mx = 0.0
    for v in hg.data.vertices:
        d = (v.co - c)
        if d.length < 1e-6:
            continue
        dn = d.normalized()
        # под подбородком (ремень) радиальная подгонка гаснет — ремень потом отодвигается от кожи по нормали
        k = max(0.0, min(1.0, (dn.z + 0.82) / 0.25))
        dd = sample(dn) * k
        mx = max(mx, abs(dd))
        v.co = v.co + dn * dd
    bm_f.free()
    bm_h.free()
    # ремень и всё, что оказалось под кожей лица, — наружу по нормали кожи на 3 мм (лицевой вырез/скулы)
    bm_f = bmesh.new()
    bm_f.from_mesh(face.data)
    tree = BVHTree.FromBMesh(bm_f)
    strap_idx = [i for i, m in enumerate(hg.data.materials) if m.name == "HeadgearStrap"]
    strap_v = {vi for p in hg.data.polygons if p.material_index in strap_idx for vi in p.vertices}
    fixed = 0
    for v in hg.data.vertices:
        hit = tree.find_nearest(v.co)
        if hit[0] is None:
            continue
        g = (v.co - hit[0]).dot(hit[1])
        want = 0.003 if v.index in strap_v else 0.0015
        if g < want and (v.co - hit[0]).length < 0.03:
            v.co += hit[1] * (want - g)
            fixed += 1
    bm_f.free()
    log("headgear: вытолкнуто из-под кожи", fixed)
    log("headgear radial fit: directions", hits, "max |δ| %.1f mm" % (mx * 1000))
    # контроль: внутренняя стенка не под кожей
    bm_h = bmesh.new()
    bm_h.from_mesh(hg.data)
    bm_f = bmesh.new()
    bm_f.from_mesh(face.data)
    tree = BVHTree.FromBMesh(bm_f)
    worst = 1.0
    where = None
    bad = 0
    for v in bm_h.verts:
        hit = tree.find_nearest(v.co)
        if hit[0] is not None:
            g = (v.co - hit[0]).dot(hit[1])
            if g < worst:
                worst, where = g, v.co.copy()
            if g < 0.0:
                bad += 1
    bm_h.free()
    bm_f.free()
    log("headgear min signed gap to skin %.1f mm at %s; verts under skin %d" % (
        worst * 1000, tuple(round(x, 3) for x in where) if where else None, bad))
    rigid(hg, "head")
    for p in hg.data.polygons:
        p.use_smooth = True
    return hg


# ------------------------------------------------------------------ экспорт
def bind(o, arm):
    o.parent = arm
    o.matrix_parent_inverse = Matrix.Identity(4)
    for m in list(o.modifiers):
        if m.type == "ARMATURE":
            o.modifiers.remove(m)
    m = o.modifiers.new("Armature", "ARMATURE")
    m.object = arm


def to_cm(arm, objs):
    """Кости и меши → сантиметры, объект арматуры — масштаб 0.01 (как look_kit.py)."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.parent = None
    for o in [arm] + objs:
        o.select_set(True)
        o.scale = (100.0, 100.0, 100.0)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for o in [arm] + objs:
        o.scale = (0.01, 0.01, 0.01)
    for o in objs:
        o.parent = arm
        o.matrix_parent_inverse = Matrix.Identity(4)
        o.scale = (1.0, 1.0, 1.0)
    bpy.context.view_layer.update()


def export(arm, obj, fname):
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=W + fname, use_selection=True, object_types={"ARMATURE", "MESH"},
        add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False,
        primary_bone_axis="Y", secondary_bone_axis="X", armature_nodetype="NULL",
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0,
        axis_forward="-Z", axis_up="Y", mesh_smooth_type="FACE", use_tspace=False,
        use_mesh_modifiers=False)
    log("export", fname, len(obj.data.vertices), "verts", tris(obj), "tris", [m.name for m in obj.data.materials])


COLS = {"Skin": (0.62, 0.45, 0.35, 1), "Gloves": (0.17, 0.21, 0.63, 1),
        "Shirt": (0.6, 0.66, 0.76, 1), "ShirtTrim": (0.55, 0.6, 0.7, 1), "BowTie": (0.02, 0.02, 0.02, 1),
        "Trousers": (0.06, 0.06, 0.07, 1), "Belt": (0.01, 0.01, 0.01, 1), "Shoes": (0.03, 0.03, 0.03, 1),
        "ShoesSole": (0.2, 0.2, 0.2, 1), "Vest": (0.75, 0.05, 0.05, 1), "VestTrim": (0.95, 0.95, 0.95, 1),
        "HeadgearShell": (0.75, 0.05, 0.05, 1), "HeadgearTrim": (0.3, 0.02, 0.02, 1),
        "HeadgearStrap": (0.05, 0.05, 0.05, 1), "HeadgearLogo": (0.95, 0.95, 0.95, 1),
        "GloveLeather": (0.75, 0.05, 0.05, 1), "GloveTrim": (0.3, 0.02, 0.02, 1), "GloveStrap": (0.6, 0.04, 0.04, 1),
        "GloveLogo": (0.95, 0.95, 0.95, 1), "GloveTape": (0.95, 0.95, 0.95, 1), "FaceSkin": (0.62, 0.45, 0.35, 1)}


def preview(sets, centre_z=0.95):
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
    allobjs = {o for objs in sets.values() for o in objs}
    for name, (objs, shots) in ((k, (v, None)) for k, v in sets.items()):
        for o in bpy.data.objects:
            if o.type == "MESH":
                o.hide_render = o not in objs
        for sname, loc, look, ortho in (shots or [
                ("front", (0, -4, 0.95), (0, 0, 0.95), 2.0), ("side", (4, 0, 0.95), (0, 0, 0.95), 2.0),
                ("back", (0, 4, 0.95), (0, 0, 0.95), 2.0),
                ("neck", (0.5, -1.2, 1.5), (0, 0, 1.42), 0.6), ("head", (0.9, -0.9, 1.7), (0, 0, 1.6), 0.45)]):
            co.location = Vector(loc)
            co.rotation_euler = (Vector(look) - co.location).to_track_quat("-Z", "Y").to_euler()
            cam.type = "ORTHO"
            cam.ortho_scale = ortho
            sc.render.filepath = W + "out_%s_%s.png" % (name, sname)
            bpy.ops.render.render(write_still=True)


def main():
    os.makedirs(W, exist_ok=True)
    reset()
    arm, full = import_body()
    face = import_face()
    fm = bpy.data.materials.new("FaceSkin")
    face.data.materials.clear()
    face.data.materials.append(fm)
    rig = Rig(arm)
    field = look_morphs.Field(full)        # S-60: ключи телосложения для формы любителя
    log("rig neck", tuple(round(x, 3) for x in rig.neck), "front_y", round(rig.front_y, 3))
    exports = []          # (obj, fname)
    sets = {}
    if not ONLY or "ref" in ONLY:
        shoes = make_shoes(full)
        transfer_weights(shoes, full)
        trousers = make_trousers(full, rig, "all")
        transfer_weights(trousers, full)
        push_out(trousers, [full], 0.010)
        exports += [(shoes, "ref_shoes.fbx"), (trousers, "ref_trousers.fbx")]
        for kind in ("am", "pro"):
            shirt = make_shirt(full, rig, kind)
            transfer_weights(shirt, full)
            push_out(shirt, [full, face], 0.006)
            parts = [shirt]
            if kind == "pro":
                z = neck_z(-0.08, NECK_PRO) + COLLAR_H[0] * 0.45
                cf = collar_front_point(shirt, z)
                if cf is not None:
                    bt = make_bowtie(cf, z)
                    rigid(bt, "neck_01")
                    bt.data.materials.append(bpy.data.materials.get("BowTie"))
                    shirt = join([shirt, bt], "shirt_pro")
                    log("bowtie at", tuple(round(x, 3) for x in cf))
            body = make_ref_body(full, rig, kind)
            exports += [(shirt, "ref_shirt_%s.fbx" % kind), (body, "ref_body_%s.fbx" % kind)]
            sets["ref_" + kind] = [shirt, body, trousers, shoes, face]
    if not ONLY or "am" in ONLY:
        vest, under_vest = make_vest(full, rig)
        body_am = make_boxer_body_am(full, under_vest)
        transfer_weights(vest, full)
        push_out(vest, [full, face], 0.004)
        hg = make_headgear(face)
        gloves = make_gloves_am(arm)
        vest_hg = join([dup(vest, "vest_hg_v"), dup(hg, "vest_hg_h")], "vest_headgear")
        # S-60: профи-женщины — спортивный топ (низ под грудью) и тело без кожи под ним
        top, under_top = make_vest(full, rig, bot=TOP_BOT, name="top", mat="Top")
        body_top = make_boxer_body_am(full, under_top)
        body_top.name = body_top.data.name = "boxer_body_top"
        transfer_weights(top, full)
        push_out(top, [full, face], 0.004)
        exports += [(top, "boxer_top.fbx"), (body_top, "boxer_body_top.fbx")]
        for o in (vest, body_am, vest_hg, top, body_top):  # шлем у шва лица — поле там 0, отдельный шлем — без ключей
            look_morphs.add_keys(o, field)
        exports += [(vest, "boxer_vest.fbx"), (hg, "boxer_headgear.fbx"), (vest_hg, "boxer_vest_headgear.fbx"),
                    (gloves, "boxer_gloves_am.fbx"), (body_am, "boxer_body_am.fbx")]
        torso = dup(full, "preview_body")           # превью: тело без головы (голова — лицо Kellan)
        bm = bmesh.new()
        bm.from_mesh(torso.data)
        uv = bm.loops.layers.uv.active
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if sum(l[uv].uv.x for l in f.loops) / len(f.loops) < 1.0],
                         context="FACES")
        bm.to_mesh(torso.data)
        bm.free()
        torso.data.materials.clear()
        torso.data.materials.append(bpy.data.materials.get("Skin") or bpy.data.materials.new("Skin"))
        sets["am"] = [vest, hg, gloves, face, body_am]
    objs = []
    for o, _ in exports:
        if o not in objs:
            objs.append(o)
    for o in objs:
        bind(o, arm)
        log(o.name, "tris", tris(o))
    # превью до перевода в сантиметры
    preview(sets)
    to_cm(arm, objs)
    for o, fn in exports:
        export(arm, o, fn)
    bpy.ops.wm.save_as_mainfile(filepath=W + "outfits.blend")
    log("done")


main()
