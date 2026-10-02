# -*- coding: utf-8 -*-
# S-60 (бойцы разные): текстуры кожи по тонам 1..6 из текстур Kellan (лицо LOD1/LOD3/LOD5 + тело).
#
# Почему не множитель цвета: текстуры Kellan тёмные (тон ~5–6); BaseColor_ColorCorrect ×4–10 для светлых тонов
# раздувает пигментные пятна (колени, локти, вокруг губ — «грязь»), а на шее лица и теле (разные текстуры) копится
# разный оттенок — по ключицам видна полоса. Здесь — перекраска в логарифме яркости по каналам:
#   out = цель_тона · exp(γL·(низкие частоты − log A) + γH·высокие частоты)
# A — средний цвет кожи у ШВА лица и тела (полоса шеи — у каждой текстуры своя) → на шве обе текстуры дают ровно
# цель тона, ступеньки нет. Низкие частоты (пятна) сжимаются сильнее у светлых тонов (γL), поры/детали (γH) — меньше.
#
# Вход (Saved/LookWork): skin/src_{face_lod1,face_lod3,face_lod5,body}.tga (UE: look_skin.py LOOK_SKIN_STEP=export),
#   face.fbx, body_full.fbx (look_export.py) — для UV полосы шва.
# Выход: skin/out/T_SkinFace_LOD{1,3,5}_T<n>.tga, T_SkinBody_T<n>.tga + skin/calib.json; превью skin/preview_*.png.
# Запуск: "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python look_skin_tones.py
import json
import os

import bpy
import numpy as np
from mathutils.kdtree import KDTree

ROOT = "C:/Users/user/Desktop/boxing-ue/"
W = ROOT + "Saved/LookWork/"
SK = W + "skin/"

# Цель (средний цвет кожи тона, sRGB) — та же таблица, что в Tools/EditorScripts/look_appearance.py SKIN_TARGET_SRGB.
TARGET = {1: (222, 182, 156), 2: (206, 160, 128), 3: (188, 140, 104), 4: (160, 112, 80), 5: (122, 82, 60),
          6: (90, 58, 43)}
# сжатие пятен (γL) и деталей (γH) по тонам
GAMMA = {1: (0.35, 0.75), 2: (0.38, 0.78), 3: (0.45, 0.82), 4: (0.6, 0.88), 5: (0.85, 0.95), 6: (1.0, 1.0)}
BLUR_SIGMA = 0.022          # доля размера текстуры: разделение «пятна / детали»
SEAM_BAND_M = 0.03          # полоса шва на теле, м


def log(*a):
    print("SKINTONE", *a)


# ----------------------------------------------------------------------------------- TGA/PNG
def read_tga(p):
    b = open(p, "rb").read()
    idl = b[0]
    w = b[12] | b[13] << 8
    h = b[14] | b[15] << 8
    bpp = b[16]
    desc = b[17]
    a = np.frombuffer(b, np.uint8, offset=18 + idl, count=w * h * (bpp // 8)).reshape(h, w, bpp // 8)
    a = a[:, :, [2, 1, 0] + ([3] if bpp == 32 else [])]
    if not desc & 0x20:
        a = a[::-1]
    return a.copy()


def write_tga(p, a):
    h, w, c = a.shape
    hdr = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 8 * c, (8 if c == 4 else 0) | 0x20])
    px = a[:, :, [2, 1, 0] + ([3] if c == 4 else [])]
    with open(p, "wb") as f:
        f.write(hdr)
        f.write(np.ascontiguousarray(px).tobytes())


def save_png(p, a):
    h, w, _ = a.shape
    img = bpy.data.images.new("tmp", w, h, alpha=True)
    rgba = np.concatenate([a[:, :, :3].astype(np.float32) / 255.0, np.ones((h, w, 1), np.float32)], 2)[::-1]
    img.pixels = rgba.ravel()
    img.filepath_raw = p
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def to_lin(s):
    s = s / 255.0
    return np.where(s <= 0.04045, s / 12.92, ((s + 0.055) / 1.055) ** 2.4)


def to_srgb(lin):
    lin = np.clip(lin, 0.0, 1.0)
    return np.where(lin <= 0.0031308, lin * 12.92, 1.055 * lin ** (1 / 2.4) - 0.055) * 255.0


def blur(img, sigma):
    """Гаусс через FFT (по каналам, тор — UV-атлас всё равно разрезан)."""
    h, w = img.shape[:2]
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    out = np.empty_like(img)
    for c in range(img.shape[2]):
        out[:, :, c] = np.real(np.fft.ifft2(np.fft.fft2(img[:, :, c]) * g))
    return out


def sample(img, uvs):
    """Средний линейный цвет по UV (окно 5×5 вокруг каждой точки)."""
    h, w = img.shape[:2]
    lin = to_lin(img[:, :, :3].astype(np.float64))
    acc = []
    for u, v in uvs:
        x = int(min(w - 1, max(0, u % 1.0 * w)))
        y = int(min(h - 1, max(0, (1.0 - v % 1.0) * h)))
        acc.append(lin[max(0, y - 2):y + 3, max(0, x - 2):x + 3].reshape(-1, 3).mean(0))
    return np.median(np.array(acc), 0)


# ----------------------------------------------------------------------------------- UV шва
def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    return next(o for o in new if o.type == "MESH")


def face_seam_uvs():
    o = import_fbx(W + "face.fbx")
    me = o.data
    mw = o.matrix_world
    counts = {}
    for p in me.polygons:
        counts[p.material_index] = counts.get(p.material_index, 0) + 1
    head_mat = max(counts, key=counts.get)
    head_polys = [p for p in me.polygons if p.material_index == head_mat]
    edge_count = {}
    for p in head_polys:
        for ek in p.edge_keys:
            edge_count[ek] = edge_count.get(ek, 0) + 1
    bverts = {v for ek, c in edge_count.items() if c == 1 for v in ek}
    zs = [(mw @ me.vertices[i].co).z for i in bverts]
    zcut = min(zs) + 0.12                      # нижний край (шея/«хомут»), не веки/ноздри
    low = {i for i in bverts if (mw @ me.vertices[i].co).z < zcut}
    uv = me.uv_layers.active.data
    uvs = []
    for p in head_polys:
        for li in p.loop_indices:
            if me.loops[li].vertex_index in low:
                uvs.append(tuple(uv[li].uv))
    log("лицо: материал %d, граничных %d, у шва %d (z < %.3f), uv %d" % (head_mat, len(bverts), len(low), zcut, len(uvs)))
    bpy.data.objects.remove(o, do_unlink=True)
    return uvs


def body_seam_uvs():
    o = import_fbx(W + "body_full.fbx")
    me = o.data
    mw = o.matrix_world
    uv = me.uv_layers.active.data
    head_v, body_polys = set(), []
    for p in me.polygons:
        if sum(uv[li].uv.x for li in p.loop_indices) / p.loop_total < 1.0:
            head_v.update(p.vertices)
        else:
            body_polys.append(p)
    kd = KDTree(len(head_v))
    for i in head_v:
        kd.insert(mw @ me.vertices[i].co, i)
    kd.balance()
    uvs = []
    for p in body_polys:
        for li in p.loop_indices:
            vi = me.loops[li].vertex_index
            if vi not in head_v and kd.find(mw @ me.vertices[vi].co)[2] < SEAM_BAND_M:
                u, v = uv[li].uv
                uvs.append((u - 1.0, v))
    log("тело: у шва uv %d" % len(uvs))
    bpy.data.objects.remove(o, do_unlink=True)
    return uvs


# ----------------------------------------------------------------------------------- перекраска
def recolor(src, anchor_lin, tone):
    rgb = to_lin(src[:, :, :3].astype(np.float64))
    lg = np.log(np.maximum(rgb, 1e-4))
    h, w = rgb.shape[:2]
    low = blur(lg, BLUR_SIGMA * w)
    high = lg - low
    gl, gh = GAMMA[tone]
    tgt = np.log(to_lin(np.array(TARGET[tone], np.float64)))
    out = tgt + gl * (low - np.log(anchor_lin)) + gh * high
    res = to_srgb(np.exp(out))
    o = src.copy()
    o[:, :, :3] = np.clip(np.round(res), 0, 255).astype(np.uint8)
    return o


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(SK + "out", exist_ok=True)
    src = {k: read_tga(SK + "src_%s.tga" % k) for k in ("face_lod1", "face_lod3", "face_lod5", "body")}
    fuv = face_seam_uvs()
    buv = body_seam_uvs()
    anchor = {k: sample(src[k], fuv) for k in ("face_lod1", "face_lod3", "face_lod5")}
    anchor["body"] = sample(src["body"], buv)
    for k, a in anchor.items():
        log("якорь %s (лин.) = %s, среднее текстуры = %s" % (k, np.round(a, 4),
                                                            np.round(to_lin(src[k][:, :, :3].astype(float)).reshape(-1, 3).mean(0), 4)))
    names = {"face_lod1": "T_SkinFace_LOD1_T%d", "face_lod3": "T_SkinFace_LOD3_T%d", "face_lod5": "T_SkinFace_LOD5_T%d",
             "body": "T_SkinBody_T%d"}
    for tone in TARGET:
        for k, pat in names.items():
            o = recolor(src[k], anchor[k], tone)
            write_tga(SK + "out/" + (pat % tone) + ".tga", o)
            if k in ("face_lod1", "body"):
                save_png(SK + "preview_%s_T%d.png" % (k, tone), o[::2, ::2])
        log("тон %d готов" % tone)
    with open(SK + "calib.json", "w", encoding="utf-8") as f:
        json.dump({"anchor": {k: list(map(float, v)) for k, v in anchor.items()}, "target": TARGET, "gamma": GAMMA,
                   "blur_sigma": BLUR_SIGMA}, f, indent=1)
    log("готово")


main()
