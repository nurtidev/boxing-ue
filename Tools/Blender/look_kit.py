# -*- coding: utf-8 -*-
# S-41 (облик боксёра), шаг 2 — Blender: форма боксёра на скелет MetaHuman (metahuman_base_skel).
#
# Вход  (Saved/LookWork, выгружает Tools/EditorScripts/look_export.py из UE):
#   body_full.fbx — полное тело m_med_nrw (мокап-набор MetaHuman, с головой; тело Kellan обрезано под худи)
#   + web/public/models/glove.glb — своя перчатка веба (tools/gloves/build_glove.py, без внешних ассетов)
# Выход (Saved/LookWork):
#   boxer_body.fbx  — тело БЕЗ головы (её даёт меш лица Kellan; шов — штатный шов MetaHuman), без кистей
#                     (их прячет перчатка) и стоп (их прячут боксёрки); материал Skin (UV тела — тайл U 1..2,
#                     как у тела Kellan → его текстуры ложатся как есть)
#   boxer_kit.fbx   — трусы + боксёрки: оболочки по телу (те же веса скиннинга), материалы
#                     Trunks / TrunksBand / Boots / BootsTrim / BootsSole
#   boxer_gloves.fbx — перчатки (glove.glb веба) на костях hand_l/hand_r, вес 1.0 (жёсткие), базис как в
#                     web glove.ts: +Y → основание среднего пальца, +X → большой палец, +Z = X×Y (ладонь);
#                     материалы GloveLeather / GloveTrim / GloveLaces / GloveTape (комплект профи: шнуровка + лента)
#   previews: Saved/LookWork/kit_*.png (workbench)
#
# Запуск:
#   "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup ^
#       --python C:\Users\user\Desktop\boxing-ue\Tools\Blender\look_kit.py
# Тело и форма — производные контента Epic (MetaHuman) → в UE кладутся в /Game/BoxingLocal (вне git);
# перчатки — своя геометрия → /Game/Boxing/Characters (в git).
import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(globals().get("__file__") or "C:/Users/user/Desktop/boxing-ue/Tools/Blender/x.py")))
import look_morphs  # noqa: E402  S-60: ключи телосложения Heavy/Lean/Muscular

ROOT = "C:/Users/user/Desktop/boxing-ue/"
W = ROOT + "Saved/LookWork/"
GLOVE_GLB = "C:/Users/user/Desktop/boxing/web/public/models/glove.glb"
GLOVE_SCALE = 1.0          # 10 oz (web: профи ×0.95)

# --- форма (метры, поза привязки MetaHuman A-pose; пол ≈ z −0.02) ---
TRUNKS_TOP = 1.075         # пояс — чуть выше пупка (боксёрские трусы высокие)
TRUNKS_HEM = 0.600         # низ штанин — середина бедра
BAND_H = 0.060             # широкая резинка-пояс
TRUNKS_OFF_TOP = 0.010     # отступ от кожи у пояса
TRUNKS_OFF_HEM = 0.024     # у низа штанин (свободные; больше — штанины сплавляются в «юбку»)
TRUNKS_THICK = 0.012       # толщина оболочки внутрь (для воксельного ремеша; внутренняя стенка скрыта)
BOOTS_TOP = 0.300          # верх боксёрок — середина голени
BOOTS_OFF = 0.010
BOOTS_THICK = 0.016        # сплавляет пальцы ног в гладкий носок
BOOTS_TOE_OFF = 0.009      # S-60: носок свободнее (пальцы не читаются)
BOOTS_TRIM_H = 0.035
BODY_FEET_CUT = 0.215      # кожа ниже — под боксёрками, удаляется
NECK_BAND_W = 0.05          # полоса кожи под краем меша лица (закрывает щель шва), м
NECK_BAND_SINK = 0.002
NECK_BAND_TAPER = 0.3          # +3 мм утапливания на каждый сантиметр от шва
VOXEL = 0.009             # шаг воксельного ремеша
TRUNKS_FACES = 3500        # бюджет (четырёхугольники до триангуляции)
BOOTS_FACES = 2400
MIN_GAP = 0.006            # наружная стенка формы не ближе 6 мм к коже (в позе привязки)

ARM_BONES = ("clavicle", "upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky", "wrist")
HAND_BONES = ("hand", "thumb", "index", "middle", "ring", "pinky", "wrist_inner", "wrist_outer")


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_body():
    bpy.ops.import_scene.fbx(filepath=W + "body_full.fbx")
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.type == "MESH")
    top = arm.parent
    # Корневой Empty (масштаб 0.01, см → м) отцепляем с сохранением трансформа и применяем масштаб:
    # в FBX уходят арматура «root» (= кость root) + меш, всё в метрах, без лишнего узла сверху.
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
    print("KIT armature", arm.name, arm.matrix_world.to_scale(), "mesh", mesh.name, len(mesh.data.vertices))
    return arm, mesh


def group_sums(obj, prefixes):
    """Сумма весов вершины по группам, чьи имена начинаются с prefixes (по каждой вершине)."""
    idx = {g.index for g in obj.vertex_groups if g.name.startswith(prefixes)}
    out = []
    for v in obj.data.vertices:
        out.append(sum(g.weight for g in v.groups if g.group in idx))
    return out


def dup(obj, name):
    o = obj.copy()
    o.data = obj.data.copy()
    o.name = name
    o.data.name = name
    bpy.context.scene.collection.objects.link(o)
    for m in o.modifiers:
        pass
    return o


def strip_head(body):
    """Голову/шею/плечевой «хомут» даёт меш лица (UV-тайл U<1) — оставляем только тело (U 1..2).
    Ногти ног тоже в тайле 0, но стопы всё равно уходят под боксёрки."""
    from mathutils.kdtree import KDTree
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bm.normal_update()
    uv = bm.loops.layers.uv.active
    is0 = {f: sum(l[uv].uv.x for l in f.loops) / len(f.loops) < 1.0 for f in bm.faces}
    keep_v = {v for f in bm.faces if not is0[f] for v in f.verts}
    # Шов с мешем лица: края мешей тела и лица Kellan расходятся на миллиметры → тонкая тёмная щель
    # («шнурок» на плечах). Поэтому оставляем полосу NECK_BAND_RINGS колец головы за швом, утапливаем её
    # на NECK_BAND_SINK под кожу лица и красим UV ближайшей точки шва — щель закрыта кожей того же тона.
    # тайлы не делят вершин (разрезаны по шву UV) — шов = граничные вершины тела выше плеч
    seam = [v for v in keep_v if v.co.z > 1.2 and any(e.is_boundary for e in v.link_edges)]
    seam_uv = {}
    for v in seam:
        for l in v.link_loops:
            if not is0[l.face]:
                seam_uv[v] = l[uv].uv.copy()
                break
    kd = KDTree(len(seam))
    for i, v in enumerate(seam):
        kd.insert(v.co, i)
    kd.balance()
    band = {f for f in bm.faces if is0[f] and f.calc_center_median().z > 1.2
            and max(kd.find(v.co)[2] for v in f.verts) < NECK_BAND_W}
    band_v = {v for f in band for v in f.verts} - keep_v
    for f in band:
        for l in f.loops:
            v = l.vert
            if v in seam_uv:
                l[uv].uv = seam_uv[v]
            elif v in band_v:
                l[uv].uv = seam_uv.get(seam[kd.find(v.co)[1]], l[uv].uv)
    # утапливаем глубже с удалением от шва: шея лица Kellan местами тоньше шеи мокап-тела
    for v in band_v:
        d = kd.find(v.co)[2]
        v.co -= v.normal * (NECK_BAND_SINK + NECK_BAND_TAPER * d)
    kill = [f for f in bm.faces if is0[f] and f not in band]
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    bm.to_mesh(body.data)
    bm.free()
    print("KIT neck band faces", len(band), "seam verts", len(seam))


def smooth_shell(bm, iters, factor=0.5, pin_boundary=True):
    for _ in range(iters):
        verts = [v for v in bm.verts if not (pin_boundary and v.is_boundary)]
        bmesh.ops.smooth_vert(bm, verts=verts, factor=factor, use_axis_x=True, use_axis_y=True, use_axis_z=True)


def cut_keep(bm, z, keep_below):
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=(0, 0, z), plane_no=(0, 0, 1))
    kill = [f for f in bm.faces if (f.calc_center_median().z > z) == keep_below]
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")


def weld(bm, dist=1e-4):
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=dist)


def region_object(body, name, keep_face):
    """Копия тела только с гранями, где keep_face(center, max_arm_weight); вершины сварены (швы UV)."""
    o = dup(body, name)
    arm_w = group_sums(o, ARM_BONES)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    kill = [f for f in bm.faces if not keep_face(f.calc_center_median(), max(arm_w[v.index] for v in f.verts))]
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    weld(bm)
    # дырки (ногти ног — в тайле головы и уже удалены) закрываем
    bmesh.ops.holes_fill(bm, edges=bm.edges[:], sides=40)
    bm.to_mesh(o.data)
    bm.free()
    return o


def apply_mod(o, m):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier=m.name)


def remesh(o, voxel, decimate_faces):
    """Сплошная оболочка: толщина → воксельный ремеш (сплавляет пальцы ног/промежность, без щелей по
    швам) → прореживание до бюджета."""
    m = o.modifiers.new("rm", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.adaptivity = 0.0
    apply_mod(o, m)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    smooth_shell(bm, 4, 0.5, pin_boundary=False)
    bm.to_mesh(o.data)
    bm.free()
    n = len(o.data.polygons)
    if n > decimate_faces:
        m = o.modifiers.new("dc", "DECIMATE")
        m.ratio = decimate_faces / n
        apply_mod(o, m)


def transfer_weights(dst, src):
    """Веса скиннинга с кожи (ближайшая грань, интерполяция) — форма деформируется как тело."""
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
    # нормализация + не больше 8 влияний
    bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=8)
    bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)


def push_out(o, skin, gap):
    """Вершины, оказавшиеся ближе gap к коже снаружи или под ней, выталкиваем по нормали кожи.
    Внутреннюю стенку оболочки (смотрит на кожу) не трогаем — она и так скрыта."""
    from mathutils.bvhtree import BVHTree
    bm_s = bmesh.new()
    bm_s.from_mesh(skin.data)
    tree = BVHTree.FromBMesh(bm_s)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.normal_update()
    moved = 0
    for v in bm.verts:
        hit = tree.find_nearest(v.co)
        if hit[0] is None:
            continue
        p, n = hit[0], hit[1]
        if v.normal.dot(n) < 0.2:
            continue                    # внутренняя стенка
        s = (v.co - p).dot(n)
        if s < gap:
            v.co += n * (gap - s)
            moved += 1
    bm.to_mesh(o.data)
    bm.free()
    bm_s.free()
    print("KIT push_out", o.name, moved)


def offset_shell(o, dist_fn, horiz=1.0):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.normal_update()
    for v in bm.verts:
        n = v.normal.copy()
        n.z *= horiz
        if n.length > 1e-6:
            n.normalize()
        v.co += n * dist_fn(v.co)
    bm.to_mesh(o.data)
    bm.free()


def split_at(bm, z):
    """Ровный стык материалов: разрез по плоскости без удаления."""
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=(0, 0, z), plane_no=(0, 0, 1))


def close_rims(bm):
    """После среза у оболочки два края (наружная и внутренняя стенки) — сшиваем попарно в кромку."""
    loops = []
    seen = set()
    for e in bm.edges:
        if not e.is_boundary or e in seen:
            continue
        comp, stack = [], [e]
        seen.add(e)
        while stack:
            x = stack.pop()
            comp.append(x)
            for v in x.verts:
                for y in v.link_edges:
                    if y.is_boundary and y not in seen:
                        seen.add(y)
                        stack.append(y)
        c = sum((v.co for x in comp for v in x.verts), Vector()) / (2 * len(comp))
        loops.append((comp, c))
    used = set()
    bridged = 0
    for i, (a, ca) in enumerate(loops):
        if i in used:
            continue
        best, bd = None, 1e9
        for j, (b, cb) in enumerate(loops):
            if j == i or j in used:
                continue
            d = (ca - cb).length
            if d < bd:
                best, bd = j, d
        if best is None or bd > 0.05:
            continue
        try:
            bmesh.ops.bridge_loops(bm, edges=a + loops[best][0])
            used |= {i, best}
            bridged += 1
        except Exception as ex:  # noqa
            print("KIT bridge fail", ex)
    print("KIT rims bridged", bridged, "of loops", len(loops))


def make_trunks(body):
    o = region_object(body, "trunks", lambda c, aw: TRUNKS_HEM - 0.04 < c.z < TRUNKS_TOP + 0.04
                      and abs(c.x) < 0.26 and aw < 0.05)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    cut_keep(bm, TRUNKS_TOP, keep_below=True)
    cut_keep(bm, TRUNKS_HEM, keep_below=False)
    smooth_shell(bm, 6, 0.5)
    bm.to_mesh(o.data)
    bm.free()

    def dist(co):
        t = min(1.0, max(0.0, (TRUNKS_TOP - co.z) / (TRUNKS_TOP - TRUNKS_HEM)))
        return TRUNKS_OFF_TOP + (TRUNKS_OFF_HEM - TRUNKS_OFF_TOP) * (t ** 1.5)

    offset_shell(o, dist, horiz=0.5)
    m = o.modifiers.new("sol", "SOLIDIFY")
    m.thickness = TRUNKS_THICK
    m.offset = -1.0
    m.use_rim = True
    apply_mod(o, m)
    remesh(o, VOXEL, TRUNKS_FACES)
    # срез сверху/снизу после ремеша снова ровный
    bm = bmesh.new()
    bm.from_mesh(o.data)
    cut_keep(bm, TRUNKS_TOP, keep_below=True)
    cut_keep(bm, TRUNKS_HEM, keep_below=False)
    close_rims(bm)
    split_at(bm, TRUNKS_TOP - BAND_H)
    bm.to_mesh(o.data)
    bm.free()
    o.data.materials.clear()
    for n in ("Trunks", "TrunksBand"):
        o.data.materials.append(bpy.data.materials.get(n) or bpy.data.materials.new(n))
    for p in o.data.polygons:
        p.material_index = 1 if p.center.z > TRUNKS_TOP - BAND_H else 0
    return o


def make_boots(body):
    o = region_object(body, "boots", lambda c, aw: c.z < BOOTS_TOP + 0.04 and aw < 0.05)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    cut_keep(bm, BOOTS_TOP, keep_below=True)
    bm.to_mesh(o.data)
    bm.free()
    # S-60: носок шире кожи пальцев (зазоры между пальцами сплавляются ремешем) — иначе «таби» с пальцами
    def boot_off(co):
        toe = min(1.0, max(0.0, (0.07 - co.z) / 0.04)) * min(1.0, max(0.0, (0.02 - co.y) / 0.06))
        return BOOTS_OFF + BOOTS_TOE_OFF * toe
    offset_shell(o, boot_off)
    hull_feet(o, 0.075, 0.11)
    m = o.modifiers.new("sol", "SOLIDIFY")
    m.thickness = BOOTS_THICK          # толщина внутрь (до кожи): сплавляет пальцы ног в носок
    m.offset = -1.0
    m.use_rim = True
    apply_mod(o, m)
    remesh(o, VOXEL, BOOTS_FACES)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    # S-60: носок — сгладить рельеф пальцев (только перед стопы, наружная форма; вмятина заполняется)
    toe = [v for v in bm.verts if v.co.z < 0.075 and v.co.y < 0.0]
    for _ in range(30):
        new = {}
        for v in toe:
            nb = [e.other_vert(v).co for e in v.link_edges]
            if nb:
                avg = sum(nb, Vector()) / len(nb)
                d = avg - v.co
                new[v] = v.co + d * 0.6 if d.dot(v.normal) > 0 else v.co + d * 0.15   # вмятины — сильнее
        for v, c in new.items():
            v.co = c
        bm.normal_update()
    cut_keep(bm, BOOTS_TOP, keep_below=True)
    close_rims(bm)
    split_at(bm, BOOTS_TOP - BOOTS_TRIM_H)
    bm.to_mesh(o.data)
    bm.free()
    o.data.materials.clear()
    for n in ("Boots", "BootsTrim", "BootsSole"):
        o.data.materials.append(bpy.data.materials.get(n) or bpy.data.materials.new(n))
    for p in o.data.polygons:
        if p.normal.z < -0.55 and p.center.z < 0.05:
            p.material_index = 2
        elif p.center.z > BOOTS_TOP - BOOTS_TRIM_H:
            p.material_index = 1
        else:
            p.material_index = 0
    return o


def hull_feet(o, z_full, z_fade):
    """S-60: стопа боксёрки — выпуклая оболочка (носок без пальцев): каждая вершина ниже z_fade тянется к ближайшей
    точке выпуклой оболочки своей стопы (вмятины между пальцами заполняются), выше — плавно гаснет."""
    from mathutils.bvhtree import BVHTree
    me = o.data
    for side in (1, -1):
        ids = [v.index for v in me.vertices if v.co.z < z_fade + 0.02 and v.co.x * side > 0]
        if len(ids) < 8:
            continue
        bm = bmesh.new()
        for i in ids:
            bm.verts.new(me.vertices[i].co)
        res = bmesh.ops.convex_hull(bm, input=bm.verts[:])
        kill = {g for g in res.get("geom_interior", []) + res.get("geom_unused", []) if isinstance(g, bmesh.types.BMVert)}
        bmesh.ops.delete(bm, geom=list(kill), context="VERTS")
        bm.normal_update()
        bvh = BVHTree.FromBMesh(bm)
        cx = sum(me.vertices[i].co.x for i in ids) / len(ids)
        cy = sum(me.vertices[i].co.y for i in ids) / len(ids)
        for i in ids:
            v = me.vertices[i]
            if v.co.z > z_fade:
                continue
            # луч от оси стопы наружу (по горизонтали) до оболочки: вмятина между пальцами выталкивается
            d = Vector((v.co.x - cx, v.co.y - cy, 0.0))
            if d.length < 1e-5:
                continue
            d.normalize()
            o0 = Vector((cx, cy, v.co.z))
            hit = bvh.ray_cast(o0, d)[0]
            if hit is None or (hit - o0).length <= (v.co - o0).length:
                continue
            t = 1.0 - min(1.0, max(0.0, (v.co.z - z_full) / (z_fade - z_full)))
            v.co = v.co.lerp(hit, t)
        bm.free()


def drop_islands(o, min_verts):
    bm = bmesh.new()
    bm.from_mesh(o.data)
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
    bmesh.ops.delete(bm, geom=kill, context="VERTS")
    bm.to_mesh(o.data)
    bm.free()
    print("KIT islands removed", o.name, len(kill))


def solidify(o, t):
    m = o.modifiers.new("solid", "SOLIDIFY")
    m.thickness = t
    m.offset = -1.0
    m.use_rim = True
    m.use_even_offset = True
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.ops.object.modifier_apply(modifier=m.name)


def trim_body(body):
    """Кисти (вес кисти/пальцев ≥ 0.5 — до запястья) и стопы (ниже BODY_FEET_CUT) удаляем: их прячут
    перчатки и боксёрки, а так кожа не проткнёт их ни в одном клипе."""
    hand_w = group_sums(body, HAND_BONES)
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bm.verts.ensure_lookup_table()
    kill = set()
    for f in bm.faces:
        if f.calc_center_median().z < BODY_FEET_CUT:
            kill.add(f)
        elif sum(hand_w[v.index] for v in f.verts) / len(f.verts) >= 0.5:
            kill.add(f)
    bmesh.ops.delete(bm, geom=list(kill), context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(body.data)
    bm.free()
    body.data.materials.clear()
    body.data.materials.append(bpy.data.materials.get("Skin") or bpy.data.materials.new("Skin"))


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
        x = -x                          # как web glove.ts: зеркальная модель, её −X — большой палец
    z = x.cross(y).normalized()
    return wrist, x, y, z


def make_gloves(arm):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=GLOVE_GLB)
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    parts = [o for o in new if not o.name.startswith(("glove_strap", "glove_logo"))]   # комплект профи
    for o in new:
        if o not in parts:
            bpy.data.objects.remove(o, do_unlink=True)
    rename = {"leather": "GloveLeather", "trim": "GloveTrim", "laces": "GloveLaces", "tape": "GloveTape"}
    sides = []
    for side in ("r", "l"):
        wrist, x, y, z = hand_basis(arm, side)
        objs = []
        for p in parts:
            o = p.copy()
            o.data = p.data.copy()
            bpy.context.scene.collection.objects.link(o)
            me = o.data
            mw = p.matrix_world.copy()
            for v in me.vertices:
                bw = mw @ v.co
                g = Vector((bw.x, bw.z, -bw.y)) * GLOVE_SCALE     # Blender → оси glTF перчатки
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
            vg = o.vertex_groups.new(name="hand_" + side)
            vg.add(list(range(len(me.vertices))), 1.0, "REPLACE")
            objs.append(o)
        sides += objs
    for p in parts:
        bpy.data.objects.remove(p, do_unlink=True)
    for o in list(bpy.data.objects):
        if o not in before and o not in sides and o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    j = join(sides, "gloves")
    return j


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


def bind(o, arm):
    o.parent = arm
    o.matrix_parent_inverse = Matrix.Identity(4)
    for m in list(o.modifiers):
        if m.type == "ARMATURE":
            o.modifiers.remove(m)
    m = o.modifiers.new("Armature", "ARMATURE")
    m.object = arm
    # смягчённые нормали
    for p in o.data.polygons:
        p.use_smooth = True


def to_cm(arm, objs):
    """Кости и меши → сантиметры, объект арматуры — масштаб 0.01 (мир тот же). Иначе экспорт даёт кость
    root с масштабом 100 и костями в метрах (UE: ref pose root S=100 — меш улетает при ретаргете)."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in [arm] + objs:
        o.parent = None if o is not arm else o.parent
        o.select_set(True)
    for o in [arm] + objs:
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
    print("KIT export", fname, len(obj.data.vertices), "verts", len(obj.data.polygons), "faces",
          [m.name for m in obj.data.materials])


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def preview(objs, arm):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.color_type = "MATERIAL"
    sc.render.resolution_x = 900
    sc.render.resolution_y = 1100
    cols = {"Skin": (0.78, 0.6, 0.48, 1), "Trunks": (0.75, 0.05, 0.05, 1), "TrunksBand": (0.95, 0.95, 0.95, 1),
            "Boots": (0.08, 0.08, 0.09, 1), "BootsTrim": (0.75, 0.05, 0.05, 1), "BootsSole": (0.9, 0.9, 0.9, 1),
            "GloveLeather": (0.75, 0.05, 0.05, 1), "GloveTrim": (0.3, 0.02, 0.02, 1),
            "GloveLaces": (0.95, 0.95, 0.95, 1), "GloveTape": (0.95, 0.95, 0.95, 1)}
    for n, c in cols.items():
        m = bpy.data.materials.get(n)
        if m:
            m.diffuse_color = c
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    sc.camera = co

    def shot(name, loc, look, ortho):
        co.location = Vector(loc)
        co.rotation_euler = (Vector(look) - co.location).to_track_quat("-Z", "Y").to_euler()
        cam.type = "ORTHO"
        cam.ortho_scale = ortho
        sc.render.filepath = W + name
        bpy.ops.render.render(write_still=True)

    shot("kit_front.png", (0, -4, 0.95), (0, 0, 0.95), 2.0)
    shot("kit_back.png", (0, 4, 0.95), (0, 0, 0.95), 2.0)
    shot("kit_side.png", (4, 0, 0.95), (0, 0, 0.95), 2.0)
    wrist = arm.matrix_world @ arm.data.bones["hand_r"].head_local
    shot("kit_hand_r.png", (wrist.x - 0.6, wrist.y - 0.6, wrist.z + 0.3), (wrist.x, wrist.y, wrist.z), 0.5)
    shot("kit_hips.png", (0.8, -2.0, 0.8), (0, 0, 0.8), 0.9)


def main():
    os.makedirs(W, exist_ok=True)
    reset()
    arm, body = import_body()
    field = look_morphs.Field(body)        # S-60: поле телосложения — по полному телу, до обрезки
    strip_head(body)
    drop_islands(body, 60)
    skin = dup(body, "skin_src")           # полная кожа — источник весов и «кожа» для выталкивания
    trunks = make_trunks(body)
    boots = make_boots(body)
    kit = join([trunks, boots], "kit")
    drop_islands(kit, 200)
    transfer_weights(kit, skin)
    push_out(kit, skin, MIN_GAP)
    trim_body(body)
    bpy.data.objects.remove(skin, do_unlink=True)
    gloves = make_gloves(arm)
    for o in (body, kit):                  # перчатки жёсткие — без ключей
        look_morphs.add_keys(o, field)
    for o in (body, kit, gloves):
        bind(o, arm)
        print("KIT", o.name, "tris", tris(o))
    to_cm(arm, [body, kit, gloves])
    export(arm, body, "boxer_body.fbx")
    export(arm, kit, "boxer_kit.fbx")
    export(arm, gloves, "boxer_gloves.fbx")
    preview([body, kit, gloves], arm)
    bpy.ops.wm.save_as_mainfile(filepath=W + "boxer_kit.blend")


main()
