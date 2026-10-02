# -*- coding: utf-8 -*-
# S-60 (бойцы разные): ключи формы ТЕЛОСЛОЖЕНИЯ для тела боксёра и всего, что лежит на коже (трусы, боксёрки,
# майка), — модуль, его зовут look_kit.py и look_outfits.py перед экспортом. В UE это морфы
#   Heavy    — тяж/супертяж: жир на животе/боках/груди, толще шея-плечи-бёдра (web look.ts bodyMorph.heavy)
#   Lean     — мухач/минимальный вес: тоньше конечности и талия, уже торс (bodyMorph.lean)
#   Muscular — средние веса: дельты, грудь, широчайшие, бицепс/трицепс, бёдра (bodyMorph.muscular)
#   Female   — женская фигура на том же теле (лицо, скелет и вся форма — одни): грудь, уже плечи и талия, шире
#              бёдра/ягодицы, тоньше руки, без «мужского» рельефа паха под трусами. Женского лица у нас нет (Kellan).
# Веса выставляет рантайм (C++ ApplyBoxerLook, см. Docs/LOOK.md «S-60») из Content/Boxing/Data/Appearance.json.
#
# Как считается: поле смещений строится ОДИН раз на полном теле мокап-набора (m_med_nrw, поза привязки, метры) —
# вдоль сглаженной нормали, амплитуда по весам костей (торс/грудь/плечо/предплечье/бедро/голень), живот — вперёд
# по колоколу высоты; кисти/стопы — 0; у шва с мешем лица Kellan (UV-тайл 0: голова/шея/«хомут») смещение гаснет
# (у лица своих морфов нет — иначе на шее ступенька); внутренняя сторона бёдер/рук — слабее (не врастают друг в
# друга). Поле сглаживается по рёбрам. Любой меш (обрезанное тело, тело любителя, трусы, майка, шлем) получает
# ключи интерполяцией поля по ближайшим вершинам кожи — одежда «толстеет» ровно вместе с кожей под ней.
import math

import bmesh
from mathutils import Vector
from mathutils.kdtree import KDTree

KEYS = ("Heavy", "Lean", "Muscular", "Female")

# амплитуды, м (вдоль нормали, при весе ключа 1)
AMP = {
    "Heavy": {"torso": 0.024, "chest": 0.016, "belly": 0.055, "upperarm": 0.013, "lowerarm": 0.006,
              "thigh": 0.020, "calf": 0.008, "neck": 0.010, "shoulder": 0.008, "glute": 0.007},
    "Lean": {"torso": -0.011, "chest": -0.008, "belly": -0.010, "upperarm": -0.008, "lowerarm": -0.004,
             "thigh": -0.011, "calf": -0.005, "neck": -0.004, "shoulder": -0.006, "glute": -0.008},
    "Muscular": {"torso": 0.003, "chest": 0.016, "belly": 0.0, "upperarm": 0.013, "lowerarm": 0.006,
                 "thigh": 0.010, "calf": 0.006, "neck": 0.006, "shoulder": 0.019, "glute": 0.002, "lat": 0.018},
}
SEAM_FADE = (0.015, 0.09)     # от шва с лицом: 0 → полная амплитуда, м
SMOOTH_ITERS = 8
INNER_DAMP = 0.65             # внутренняя сторона бёдер/рук (к средней линии/торсу)
BELLY_Z = (0.96, 1.07, 1.22)  # колокол живота по высоте: низ, пик, верх (поза привязки, метры)

# Female (метры, поза привязки; перед — −Y, левая сторона — +X)
FEM_BREAST_C = (0.092, 1.245)      # центр груди: |x|, z
FEM_BREAST_R = (0.074, 0.066, 0.085)   # полуоси: по x, вниз, вверх
FEM_BREAST_A = 0.058
FEM_SHOULDER = (0.18, 0.02, 1.366, 0.14, 0.024)   # плечевой сустав x,y,z; радиус спада; сдвиг к оси
FEM_WAIST = (0.97, 1.08, 1.19, -0.026)
FEM_HIPS = (0.78, 0.89, 1.00, 0.032, 0.016)       # колокол z; бока; ягодицы
FEM_ARMS = (-0.007, -0.004)
FEM_CROTCH = (0.06, 0.78, 0.86, 0.93, -0.013)

GROUPS = {
    "torso": ("pelvis", "spine_01", "spine_02", "spine_03"),
    "chest": ("spine_04", "spine_05", "clavicle_pec"),
    "lat": ("spine_04_latissimus",),
    "shoulder": ("clavicle_out", "clavicle_scap", "clavicle_l", "clavicle_r", "upperarm_out", "upperarm_fwd",
                 "upperarm_bck"),
    "upperarm": ("upperarm_l", "upperarm_r", "upperarm_twist", "upperarm_bicep", "upperarm_tricep", "upperarm_in"),
    "lowerarm": ("lowerarm",),
    "thigh": ("thigh",),
    "calf": ("calf", "ankle"),
    "neck": ("neck",),
}
ZERO = ("hand", "thumb", "index", "middle", "ring", "pinky", "wrist", "foot", "ball", "toe", "bigtoe", "head",
        "FACIAL")


def _smooth01(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def _region_weights(obj):
    """{регион: [сумма весов по вершинам]} + [вес «нулевых» костей]."""
    names = {g.index: g.name for g in obj.vertex_groups}
    reg_of = {}
    for gi, gn in names.items():
        for reg, prefs in GROUPS.items():
            if gn.startswith(prefs):
                reg_of[gi] = reg
                break
        else:
            if gn.startswith(ZERO):
                reg_of[gi] = "_zero"
    out = {r: [0.0] * len(obj.data.vertices) for r in list(GROUPS) + ["_zero"]}
    for v in obj.data.vertices:
        for g in v.groups:
            r = reg_of.get(g.group)
            if r:
                out[r][v.index] += g.weight
    return out


def _bell(lo, pk, hi, z):
    return _smooth01(lo, pk, z) * (1.0 - _smooth01(pk, hi, z))


def _female(p, nn, w, i):
    d = Vector()
    sx = 1.0 if p.x > 0 else -1.0
    torso = w["torso"][i] + w["chest"][i] + w["lat"][i]
    arm = w["upperarm"][i] + w["lowerarm"][i]
    # грудь: вперёд по нормали, только передняя поверхность торса
    cx, cz = FEM_BREAST_C
    rx, rdn, rup = FEM_BREAST_R
    dx = (abs(p.x) - cx) / rx
    dz = (p.z - cz) / (rdn if p.z < cz else rup)
    front = max(0.0, -nn.y)
    if front > 0 and p.y < -0.04 and arm < 0.5:
        b = FEM_BREAST_A * math.exp(-1.6 * (dx * dx + dz * dz)) * front ** 0.5 * (1.0 - arm)
        d += (nn + Vector((0, -1, 0))).normalized() * b
    # плечи уже: плечевой «шар» к оси
    jx, jy, jz, jr, js = FEM_SHOULDER
    dist = (Vector((abs(p.x), p.y, p.z)) - Vector((jx, jy, jz))).length
    sh = math.exp(-(dist / jr) ** 2) * min(1.0, w["shoulder"][i] + w["upperarm"][i] + w["chest"][i])
    d += Vector((-sx * js * sh, 0, -0.004 * sh))
    # талия уже (бока сильнее)
    lo, pk, hi, a = FEM_WAIST
    d += nn * (a * _bell(lo, pk, hi, p.z) * torso * (0.35 + 0.65 * abs(nn.x)))
    # бёдра шире, ягодицы
    lo, pk, hi, a_side, a_back = FEM_HIPS
    hip_w = min(1.0, w["torso"][i] + w["thigh"][i])
    bell = _bell(lo, pk, hi, p.z)
    d += nn * (hip_w * bell * (a_side * abs(nn.x) + a_back * max(0.0, nn.y)))
    # руки тоньше
    d += nn * (FEM_ARMS[0] * w["upperarm"][i] + FEM_ARMS[1] * w["lowerarm"][i])
    # пах: без рельефа
    cxr, lo, pk, hi, a = FEM_CROTCH
    if abs(p.x) < cxr * 1.6 and nn.y < 0:
        d += nn * (a * _bell(lo, pk, hi, p.z) * max(0.0, 1.0 - (abs(p.x) / cxr) ** 2) * (-nn.y))
    return d


class Field:
    """Поле смещений ключей формы на полном теле (координаты мира Blender, метры)."""

    def __init__(self, full):
        me = full.data
        mw = full.matrix_world
        rot = mw.to_3x3().normalized()
        self.co = [mw @ v.co for v in me.vertices]
        nrm = [(rot @ v.normal).normalized() for v in me.vertices]
        n = len(self.co)
        # шов с лицом: вершины граней UV-тайла 0 (голова/шея — их рисует лицо Kellan)
        uv = me.uv_layers.active.data
        head = set()
        for p in me.polygons:
            if sum(uv[li].uv.x for li in p.loop_indices) / p.loop_total < 1.0:
                head.update(p.vertices)
        kd_head = KDTree(len(head))
        for i in head:
            kd_head.insert(self.co[i], i)
        kd_head.balance()
        w = _region_weights(full)
        self.deltas = {}
        for key in KEYS:
            if key == "Female":
                d = []
                for i in range(n):
                    if i in head:
                        d.append(Vector())
                        continue
                    fade = _smooth01(SEAM_FADE[0], SEAM_FADE[1], kd_head.find(self.co[i])[2])
                    zero = min(1.0, w["_zero"][i])
                    d.append(_female(self.co[i], nrm[i], w, i) * (fade * (1.0 - zero)))
                self.deltas[key] = d
                continue
            amp = AMP[key]
            d = []
            for i in range(n):
                p, nn = self.co[i], nrm[i]
                if i in head:
                    d.append(Vector())
                    continue
                fade = _smooth01(SEAM_FADE[0], SEAM_FADE[1], kd_head.find(p)[2])
                zero = min(1.0, w["_zero"][i])
                s = 0.0
                for reg in GROUPS:
                    a = amp.get(reg, 0.0)
                    if a and w[reg][i] > 0:
                        wr = w[reg][i]
                        if reg in ("thigh", "upperarm", "lowerarm", "calf"):
                            # внутренняя сторона: к средней линии (бёдра) / к торсу (руки)
                            inward = max(0.0, -nn.x * (1.0 if p.x > 0 else -1.0))
                            wr *= 1.0 - INNER_DAMP * inward
                        s += a * wr
                # живот вперёд + бока (Heavy), талия (Lean)
                tor = w["torso"][i]
                if tor > 0 and amp.get("belly"):
                    lo, pk, hi = BELLY_Z
                    bell = _smooth01(lo, pk, p.z) * (1.0 - _smooth01(pk, hi, p.z))
                    front = max(0.0, -nn.y)        # перед — −Y
                    side = abs(nn.x)
                    s += amp["belly"] * tor * bell * (front ** 1.5 + 0.15 * side)
                # ягодицы
                if amp.get("glute") and w["torso"][i] + w["thigh"][i] > 0 and nn.y > 0.2 and 0.75 < p.z < 1.0:
                    s += amp["glute"] * nn.y * _smooth01(0.75, 0.88, p.z) * (1.0 - _smooth01(0.92, 1.0, p.z))
                d.append(nn * (s * fade * (1.0 - zero)))
            self.deltas[key] = d
        self._smooth(me)
        self.kd = KDTree(n)
        for i, p in enumerate(self.co):
            self.kd.insert(p, i)
        self.kd.balance()
        for key in KEYS:
            mx = max(v.length for v in self.deltas[key])
            print("MORPH field %s: max %.1f мм" % (key, mx * 1000))

    def _smooth(self, me):
        nb = [[] for _ in me.vertices]
        for e in me.edges:
            a, b = e.vertices
            nb[a].append(b)
            nb[b].append(a)
        for key in KEYS:
            d = self.deltas[key]
            for _ in range(SMOOTH_ITERS):
                nd = []
                for i, lst in enumerate(nb):
                    if not lst:
                        nd.append(d[i])
                        continue
                    avg = sum((d[j] for j in lst), Vector()) / len(lst)
                    nd.append(d[i] * 0.5 + avg * 0.5)
                d = nd
            self.deltas[key] = d

    def at(self, p, k=4):
        """Смещения всех ключей в точке p (ОИВ по k ближайшим вершинам кожи)."""
        hits = self.kd.find_n(p, k)
        ws = [1.0 / max(1e-4, dist) ** 2 for (_, _, dist) in hits]
        tot = sum(ws)
        return {key: sum((self.deltas[key][i] * wi for (_, i, _), wi in zip(hits, ws)), Vector()) / tot
                for key in KEYS}


def add_keys(obj, field, keys=KEYS, scale_fn=None):
    """Ключи формы на obj (координаты — мир Blender, метры; ДО to_cm и экспорта). scale_fn(p) → множитель
    (например 0 для шлема, если он склеен с майкой)."""
    mw = obj.matrix_world
    inv = mw.inverted()
    rot_inv = inv.to_3x3()
    if obj.data.shape_keys is None:
        obj.shape_key_add(name="Basis", from_mix=False)
    pts = [mw @ v.co for v in obj.data.vertices]
    ds = [field.at(p) for p in pts]
    for key in keys:
        sk = obj.data.shape_keys.key_blocks.get(key) or obj.shape_key_add(name=key, from_mix=False)
        mx = 0.0
        for i, v in enumerate(obj.data.vertices):
            dv = ds[i][key]
            if scale_fn:
                dv = dv * scale_fn(pts[i])
            sk.data[i].co = v.co + rot_inv @ dv
            mx = max(mx, dv.length)
        sk.value = 0.0
        print("MORPH %s.%s: max %.1f мм" % (obj.name, key, mx * 1000))
