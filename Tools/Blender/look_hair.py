# -*- coding: utf-8 -*-
# S-60 (бойцы разные): свои грумы волос и бороды под лицо Kellan — процедурно в Blender, кривыми волос (Alembic).
#
# Зачем: в проекте один грум волос (Hair_S_360Waves — короткие афро-волны Kellan) и одна щетина. Казахи/азиаты/
# европейцы с прямыми чёрными волосами, женские хвост и пучок, бороды — всего этого не было. Грумы строятся по
# скальпу и низу лица НАСТОЯЩЕГО меша лица Kellan (Saved/LookWork/face.fbx, поза привязки), поэтому садятся без
# подгонки; в UE к лицу привязываются ассетом привязки (GroomBindingAsset на Kellan_FaceMesh, look_groom_import.py).
#
# Волос растёт «по поверхности»: корень на коже → шаг вдоль направления укладки (поле «расчёски» по области) с
# перепроекцией на поверхность головы, приподнятую на h(s) (объём растёт к кончику), шум угла/длины, кучкование
# прядей. Вид — короткая стрижка/ёжик/«средние»/зачёс назад; хвост и пучок — зачёс к точке узла + сам хвост/пучок.
#
# Выход: Saved/LookWork/hair/GR_*.abc (сантиметры, Alembic Y-up как ждёт импорт грумов UE) + превью hair/prev_*.png.
# Запуск: "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python look_hair.py
#   LOOK_HAIR_ONLY=GR_Short,GR_Beard — собрать только часть.
import math
import os
import random

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = "C:/Users/user/Desktop/boxing-ue/"
W = ROOT + "Saved/LookWork/"
OUT = W + "hair/"
ONLY = [x for x in os.environ.get("LOOK_HAIR_ONLY", "").split(",") if x]

# --- голова Kellan (метры, поза привязки; перед — −Y, левая — +X, верх — +Z), см. look_inspect/probe ---
TOP_Z = 1.733
TIE = Vector((0.0, 0.088, 1.655))       # узел хвоста/пучка — затылок над серединой
EAR_BOX = (0.066, -0.040, 0.032, 1.520, 1.642)   # |x|>, y от/до, z от/до — уши без волос


def log(*a):
    print("HAIR", *a)


# ----------------------------------------------------------------------------------- голова
class Head:
    def __init__(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=W + "face.fbx")
        o = next(o for o in bpy.data.objects if o.type == "MESH")
        mw = o.matrix_world
        me = o.data
        counts = {}
        for p in me.polygons:
            counts[p.material_index] = counts.get(p.material_index, 0) + 1
        head_mat = max(counts, key=counts.get)
        verts = [mw @ v.co for v in me.vertices]
        tris = []
        for p in me.polygons:
            if p.material_index != head_mat:
                continue
            vi = list(p.vertices)
            for k in range(1, len(vi) - 1):
                tris.append((vi[0], vi[k], vi[k + 1]))
        used = sorted({i for t in tris for i in t})
        remap = {i: n for n, i in enumerate(used)}
        self.v = [verts[i] for i in used]
        self.t = [(remap[a], remap[b], remap[c]) for a, b, c in tris]
        self.bvh = BVHTree.FromPolygons(self.v, self.t)
        self.area = []
        self.nrm = []
        for a, b, c in self.t:
            e = (self.v[b] - self.v[a]).cross(self.v[c] - self.v[a])
            self.area.append(e.length * 0.5)
            self.nrm.append(e.normalized())
        for ob in list(bpy.data.objects):
            bpy.data.objects.remove(ob, do_unlink=True)
        log("голова: вершин %d, треугольников %d" % (len(self.v), len(self.t)))

    def sample(self, n, mask, rng):
        """n корней равномерно по площади в области mask(p, nrm) → [(p, nrm)]."""
        cand = [i for i, (a, b, c) in enumerate(self.t)
                if mask((self.v[a] + self.v[b] + self.v[c]) / 3.0, self.nrm[i])]
        if not cand:
            return []
        w = np.array([self.area[i] for i in cand])
        w /= w.sum()
        picks = rng_np(rng).choice(len(cand), size=n * 2, p=w)
        out = []
        for k in picks:
            a, b, c = self.t[cand[k]]
            r1, r2 = rng.random(), rng.random()
            s = math.sqrt(r1)
            p = self.v[a] * (1 - s) + self.v[b] * (s * (1 - r2)) + self.v[c] * (s * r2)
            if mask(p, self.nrm[cand[k]]):
                out.append((p, self.nrm[cand[k]]))
            if len(out) >= n:
                break
        return out

    def project(self, p):
        loc, nrm, _, _ = self.bvh.find_nearest(p)
        return loc, nrm


def rng_np(rng):
    return np.random.default_rng(rng.randrange(1 << 30))


# ----------------------------------------------------------------------------------- области
def smooth01(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def hairline_z(p, female=False):
    """Высота линии роста волос в точке (по y: лоб → виски → над ушами → затылок)."""
    ax = abs(p.x)
    y = p.y
    pts = [(-0.14, 1.700), (-0.075, 1.685), (-0.04, 1.640), (0.0, 1.618), (0.05, 1.580), (0.11, 1.540)]
    if female:
        pts = [(-0.14, 1.684), (-0.075, 1.662), (-0.04, 1.612), (0.0, 1.600), (0.05, 1.565), (0.11, 1.525)]
    z = pts[-1][1]
    for (y0, z0), (y1, z1) in zip(pts, pts[1:]):
        if y <= y1:
            t = smooth01(y0, y1, y)
            z = z0 + (z1 - z0) * t
            break
    if y < -0.04:                       # «залысины» у висков, мыс по центру лба
        z += 0.012 * smooth01(0.02, 0.06, ax) * (0.4 if female else 1.0)
    return z


def in_ear(p):
    ax, y0, y1, z0, z1 = EAR_BOX
    return abs(p.x) > ax and y0 < p.y < y1 and z0 < p.z < z1


def scalp_mask(female=False):
    def m(p, n):
        return p.z > hairline_z(p, female) and not in_ear(p) and p.z > 1.50
    return m


def beard_region(p, n, kind):
    """kind: beard / full / mustache / goatee."""
    ax = abs(p.x)
    if p.y > 0.025 or p.z < 1.455:
        return False
    if p.z > 1.62:
        return False
    # рот: губы без волос
    if ax < 0.030 and 1.531 < p.z < 1.566 and p.y < -0.095:
        return False
    musta = ax < 0.034 and 1.557 <= p.z < 1.576 and p.y < -0.105
    chin = ax < 0.030 and 1.480 < p.z <= 1.533 and p.y < -0.07
    if kind == "mustache":
        return musta
    if kind == "goatee":
        return musta or chin or (ax < 0.034 and 1.528 < p.z < 1.560 and p.y < -0.105 and ax > 0.026)
    # борода: щёки ниже линии «угол рта → мочка уха», челюсть, подбородок; бакенбарды до волос; не на шее сбоку
    t = smooth01(0.03, 0.075, ax)
    cheek_top = 1.566 + 0.05 * t
    if p.z > cheek_top or in_ear(p):
        return False
    if p.y > -0.015 + 0.03 * smooth01(1.57, 1.62, p.z):         # за углом челюсти — только бакенбарды
        return False
    jaw_low = 1.535 - 0.055 * (1.0 - smooth01(0.02, 0.06, ax))  # снизу: по линии челюсти, у подбородка ниже
    if kind == "full":
        jaw_low -= 0.03
    if p.z < jaw_low and n.z < -0.35:                           # нижняя сторона челюсти/шея
        return kind == "full" and ax < 0.05 and p.z > 1.465
    return p.z > jaw_low - 0.012


# ----------------------------------------------------------------------------------- рост волоса
def tangent_dir(n, d):
    t = d - n * d.dot(n)
    if t.length < 1e-6:
        t = n.orthogonal()
    return t.normalized()


def grow(head, root, nrm, length, comb, h0, h1, k_pts, rng, jitter=0.25, curl=0.0):
    """Волос по поверхности: шаги вдоль comb(p) с перепроекцией на поверхность + h(s)."""
    pts = [root + nrm * 0.0005]
    p = root
    n = nrm
    ang = (rng.random() * 2 - 1) * jitter
    step = length / (k_pts - 1)
    for k in range(1, k_pts):
        s = k / (k_pts - 1)
        d = tangent_dir(n, comb(p, n))
        side = n.cross(d)
        d = (d * math.cos(ang) + side * math.sin(ang)).normalized()
        if curl:
            d = (d + side * math.sin(s * 9.0 + ang * 5) * curl).normalized()
        p = p + d * step
        q, qn = head.project(p)
        if q is None:
            break
        n = qn
        h = h0 + h1 * s
        p = q + n * h
        pts.append(p.copy())
    return pts


def comb_short(p, n):
    """Короткая стрижка: вперёд-вниз со лба, вбок-вниз по бокам, вниз на затылке; макушка — от вихра."""
    crown = Vector((0.0, 0.035, 1.728))
    d = p - crown
    d.z = 0
    if d.length < 1e-4:
        d = Vector((0, -1, 0))
    d = d.normalized() + Vector((0, 0, -0.6))
    if p.y < -0.05:
        d += Vector((0, -0.6, -0.2))
    return d


def comb_back(target):
    def c(p, n):
        d = target - p
        return d.normalized() if d.length > 1e-4 else Vector((0, 1, 0))
    return c


def comb_down(p, n):
    return Vector((0.0, -0.25, -1.0))


# ----------------------------------------------------------------------------------- грумы
def scalp_hair(head, rng, n, length, female=False, comb=comb_short, h0=0.0015, h1=0.006, k_pts=8, jitter=0.3,
               len_var=0.25, clumps=0.25):
    strands = []
    roots = head.sample(n, scalp_mask(female), rng)
    for p, nn in roots:
        L = length * (1.0 + (rng.random() * 2 - 1) * len_var)
        # у линии роста — короче (не «шапка»)
        edge = smooth01(0.0, 0.012, p.z - hairline_z(p, female))
        if not female:
            L *= 0.45 + 0.55 * edge                # у линии роста короче (у зачёса назад — нет: пряди до узла)
        if not female and p.y < -0.06:          # чёлка короче (не «шторка» на лоб)
            L *= 0.65
        strands.append(grow(head, p, nn, L, comb, h0, h1, k_pts, rng, jitter))
    if clumps > 0:
        clump(strands, clumps, rng)
    return strands


def clump(strands, amount, rng, n_clumps=None):
    """Кучкование: кончики тянутся к среднему соседей (по корням)."""
    if not strands:
        return
    roots = np.array([s[0] for s in strands])
    n_clumps = n_clumps or max(8, len(strands) // 60)
    idx = rng_np(rng).choice(len(strands), size=min(n_clumps, len(strands)), replace=False)
    centers = roots[idx]
    owner = np.argmin(((roots[:, None, :] - centers[None, :, :]) ** 2).sum(2), axis=1)
    for ci in range(len(centers)):
        members = np.where(owner == ci)[0]
        if len(members) < 2:
            continue
        kmax = min(len(strands[m]) for m in members)
        for k in range(1, kmax):
            avg = sum((strands[m][k] for m in members), Vector()) / len(members)
            t = amount * (k / (kmax - 1)) ** 1.5
            for m in members:
                strands[m][k] = strands[m][k].lerp(avg, t)


def tail_strands(rng, n, base, path, radius_fn, k_pts=14):
    """Хвост: пучок вдоль кривой path(s) (s 0..1) с радиусом radius_fn(s)."""
    out = []
    for _ in range(n):
        a = rng.random() * 2 * math.pi
        r0 = math.sqrt(rng.random())
        L = 0.85 + rng.random() * 0.15
        ph = rng.random() * 6.28
        pts = []
        for k in range(k_pts):
            s = k / (k_pts - 1) * L
            c, fwd = path(s)
            side = fwd.cross(Vector((1, 0, 0))).normalized()
            up = fwd.cross(side).normalized()
            r = radius_fn(s) * r0
            aa = a + 0.25 * math.sin(s * 6 + ph)
            pts.append(c + side * (math.cos(aa) * r) + up * (math.sin(aa) * r))
        pts[0] = base + (pts[0] - base) * 0.3
        out.append(pts)
    return out


def bezier(p0, p1, p2, p3):
    def f(s):
        u = 1 - s
        c = p0 * u ** 3 + p1 * (3 * u * u * s) + p2 * (3 * u * s * s) + p3 * s ** 3
        d = (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * s) + (p3 - p2) * (3 * s * s)
        return c, d.normalized()
    return f


def bun_strands(rng, n, center, radius, k_pts=16):
    out = []
    for _ in range(n):
        # витки по сфере: спираль вокруг случайной оси
        axis = Vector((rng.random() - 0.5, rng.random() - 0.5, rng.random() - 0.5)).normalized()
        ref = axis.orthogonal().normalized()
        ref2 = axis.cross(ref)
        r = radius * (0.75 + 0.25 * rng.random())
        a0 = rng.random() * 6.28
        span = 2.5 + rng.random() * 2.0
        pts = []
        for k in range(k_pts):
            a = a0 + span * k / (k_pts - 1)
            tilt = 0.5 * math.sin(a * 0.7)
            v = (ref * math.cos(a) + ref2 * math.sin(a)) * math.cos(tilt) + axis * math.sin(tilt)
            pts.append(center + v * r)
        out.append(pts)
    return out


def beard(head, rng, n, kind, length, h1):
    roots = head.sample(n, lambda p, nn: beard_region(p, nn, kind), rng)
    out = []
    for p, nn in roots:
        L = length * (0.7 + 0.6 * rng.random())
        if kind in ("beard", "full") and p.z > 1.585:      # бакенбарды короче
            L *= 0.6
        out.append(grow(head, p, nn, L, comb_down, 0.0008, h1, 6, rng, jitter=0.45))
    clump(out, 0.2, rng)
    return out


# ----------------------------------------------------------------------------------- экспорт
def export(name, strands, radius_cm=0.0025):
    os.makedirs(OUT, exist_ok=True)
    strands = [s for s in strands if len(s) >= 3]
    cv = bpy.data.hair_curves.new(name)
    sizes = [len(s) for s in strands]
    cv.add_curves(sizes)
    # Оси: импорт грумов UE (конверсия по умолчанию) кладёт точку Blender-экспорта p
    # компонента лица в (p.x, p.z, −p.y) (замер S-60, look_gallery LOOK_DEBUG_IDENT; меш повёрнут на −90° к актёру). Нужна точка меша лица UE (x, −y, z) → пишем (x, −z, −y).
    pos = np.array([c for s in strands for p in s for c in (p.x, -p.z, -p.y)], np.float32)
    cv.attributes["position"].data.foreach_set("vector", pos)
    # ломаные (POLY): кубические кривые Blender пишет в Alembic с узлами, на которых импорт волос UE ломается
    try:
        cv.set_types(type="POLY")
    except Exception:  # noqa
        if "curve_type" not in cv.attributes:
            cv.attributes.new("curve_type", "INT8", "CURVE")
        cv.attributes["curve_type"].data.foreach_set("value", np.ones(len(strands), np.int8))
    if "radius" not in cv.attributes:
        cv.attributes.new("radius", "FLOAT", "POINT")
    rad = []
    for s in strands:
        for k in range(len(s)):
            t = k / (len(s) - 1)
            rad.append(radius_cm * (1.0 - 0.6 * t))     # к кончику тоньше; ширина в Alembic не масштабируется
    cv.attributes["radius"].data.foreach_set("value", np.array(rad, np.float32))
    ob = bpy.data.objects.new(name, cv)
    bpy.context.scene.collection.objects.link(ob)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    path = OUT + name + ".abc"
    bpy.ops.wm.alembic_export(filepath=path, selected=True, start=1, end=1, global_scale=100.0)
    log("%s: прядей %d, точек %d → %s (%.1f КБ)" % (name, len(strands), int(sum(sizes)), path,
                                                    os.path.getsize(path) / 1024))
    # для превью — те же пряди в осях Blender
    cv.attributes["position"].data.foreach_set("vector", np.array([c for s in strands for p in s for c in p],
                                                                   np.float32))
    return ob


def preview(objs, name):
    """Workbench: голова (лицо) + грумы, 3 ракурса."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.render.resolution_x = 520
    sc.render.resolution_y = 520
    if "PrevHead" not in bpy.data.objects:
        me = bpy.data.meshes.new("PrevHead")
        me.from_pydata([tuple(v) for v in HEAD.v], [], [tuple(t) for t in HEAD.t])
        ho = bpy.data.objects.new("PrevHead", me)
        sc.collection.objects.link(ho)
        for p in me.polygons:
            p.use_smooth = True
    for o in bpy.data.objects:
        if o.type == "CURVES":
            o.hide_render = o not in objs
    cam = bpy.data.objects.get("PrevCam")
    if cam is None:
        cd = bpy.data.cameras.new("PrevCam")
        cd.type = "ORTHO"
        cd.ortho_scale = 0.42
        cam = bpy.data.objects.new("PrevCam", cd)
        sc.collection.objects.link(cam)
    sc.camera = cam
    c = Vector((0, 0, 1.60))
    for vn, loc in (("front", (0.0, -1.0, 1.62)), ("side", (1.0, 0.1, 1.62)), ("back", (0.3, 1.0, 1.66))):
        cam.location = Vector(loc)
        cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = OUT + "prev_%s_%s.png" % (name, vn)
        bpy.ops.render.render(write_still=True)


GROOMS = {}


def groom(fn):
    GROOMS[fn.__name__.replace("make_", "GR_")] = fn
    return fn


@groom
def make_Short(rng):            # короткая мужская стрижка (прямые волосы)
    return scalp_hair(HEAD, rng, 26000, 0.040, h1=0.007, k_pts=8)


@groom
def make_Buzz(rng):             # «под машинку»
    return scalp_hair(HEAD, rng, 30000, 0.010, h0=0.0008, h1=0.0015, k_pts=5, jitter=0.6, clumps=0.0)


@groom
def make_Medium(rng):           # средние, зачёс набок/назад
    def comb(p, n):
        d = comb_short(p, n)
        return d + Vector((0.35, 0.25, 0))
    return scalp_hair(HEAD, rng, 24000, 0.085, comb=comb, h1=0.016, k_pts=12, clumps=0.35)


@groom
def make_Ponytail(rng):         # женский хвост: зачёс к затылку + хвост
    scalp = scalp_hair(HEAD, rng, 55000, 0.30, female=True, comb=comb_back(TIE), h0=0.0025, h1=0.003, k_pts=12,
                       jitter=0.06, len_var=0.0, clumps=0.12)
    # обрезать у узла
    cut = []
    for s in scalp:
        out = [s[0]]
        for p in s[1:]:
            out.append(p)
            if (p - TIE).length < 0.018:
                break
        cut.append(out)
    path = bezier(TIE, TIE + Vector((0, 0.045, -0.01)), TIE + Vector((0, 0.07, -0.11)), TIE + Vector((0, 0.045, -0.24)))
    tail = tail_strands(rng, 7000, TIE, path, lambda s: 0.012 + 0.012 * math.sin(min(1.0, s * 1.6) * math.pi * 0.8))
    tie = bun_strands(rng, 500, TIE + Vector((0, 0.008, 0)), 0.011, k_pts=10)
    return cut + tail + tie


@groom
def make_Bun(rng):              # пучок
    center = TIE + Vector((0, 0.016, 0.012))
    scalp = scalp_hair(HEAD, rng, 55000, 0.30, female=True, comb=comb_back(center), h0=0.0025, h1=0.003, k_pts=12,
                       jitter=0.06, len_var=0.0, clumps=0.12)
    cut = []
    for s in scalp:
        out = [s[0]]
        for p in s[1:]:
            out.append(p)
            if (p - center).length < 0.030:
                break
        cut.append(out)
    return cut + bun_strands(rng, 6000, center, 0.029)


@groom
def make_Beard(rng):            # короткая борода
    return beard(HEAD, rng, 9000, "beard", 0.010, 0.003)


@groom
def make_FullBeard(rng):        # окладистая
    return beard(HEAD, rng, 12000, "full", 0.024, 0.008)


@groom
def make_Goatee(rng):
    return beard(HEAD, rng, 4000, "goatee", 0.012, 0.004)


@groom
def make_Mustache(rng):
    return beard(HEAD, rng, 2500, "mustache", 0.010, 0.003)


HEAD = None


def main():
    global HEAD
    HEAD = Head()
    made = []
    for name, fn in GROOMS.items():
        if ONLY and name not in ONLY:
            continue
        rng = random.Random(name)
        strands = fn(rng)
        ob = export(name, strands, radius_cm={"GR_Ponytail": 0.007, "GR_Bun": 0.007, "GR_Short": 0.0035, "GR_Medium": 0.0035}.get(
            name, 0.004 if "Beard" in name or name in ("GR_Goatee", "GR_Mustache") else 0.003))
        preview([ob], name)
        made.append(name)
    log("готово:", made)


main()
