# -*- coding: utf-8 -*-
# S-77: подготовка CC0-текстур материалов (Poly Haven / ambientCG) для наших MI — одна текстура на материал.
#
#   "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Tools\Blender\mat_textures.py
#
# Вход: Saved/AssetCache/{polyhaven,ambientcg}/<id>/ (скачанные карты 1K, см. Docs/ASSETS.md — ссылки и лицензии).
# Выход: Saved/MatWork/T_<Имя>_NR.png — RGBA 8 бит:
#   R, G — нормаль (DirectX, как в UE) x/y в 0..1; Z восстанавливает материал;
#   B — шероховатость, НОРМИРОВАННАЯ: 0.5 = средняя шероховатость скана, ±0.5 = ±2σ (материал задаёт свою среднюю
#       шероховатость параметром Roughness и размах RoughVar — один и тот же мастер на кожу, атлас и ткань);
#   A = 1.
# Плюс T_Folds_N.png (512) — «складки»: нормаль мятой ткани (denim с заломами), размытая до мягких волн без
# переплетения (макро-слой поверх фактуры: складки атласа трусов, майки, брюк).
# Статистика (средняя/σ шероховатости, размеры) — в Saved/MatWork/mat_textures.json.
import json
import os

import bpy
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, "Saved", "AssetCache")
OUT = os.path.join(ROOT, "Saved", "MatWork")
SIZE = 1024
NORM_P95 = 0.35

# имя → (каталог в кэше, нормаль DX, шероховатость)
SETS = {
    "Leather": ("polyhaven/leather_red_03", "leather_red_03_nor_dx_1k.jpg", "leather_red_03_rough_1k.jpg"),
    "LeatherCreased": ("polyhaven/leather_red_02", "leather_red_02_nor_dx_1k.jpg", "leather_red_02_rough_1k.jpg"),
    "Satin": ("polyhaven/crepe_satin", "crepe_satin_nor_dx_1k.jpg", "crepe_satin_rough_1k.jpg"),
    "Jersey": ("polyhaven/cotton_jersey", "cotton_jersey_nor_dx_1k.jpg", "cotton_jersey_rough_1k.jpg"),
    "Poplin": ("polyhaven/stretch_poplin", "stretch_poplin_nor_dx_1k.jpg", "stretch_poplin_rough_1k.jpg"),
    "Fleece": ("polyhaven/jogging_melange", "jogging_melange_nor_dx_1k.jpg", "jogging_melange_rough_1k.jpg"),
    "Terry": ("polyhaven/terry_cloth", "terry_cloth_nor_dx_1k.jpg", "terry_cloth_rough_1k.jpg"),
    "Canvas": ("polyhaven/rough_linen", "rough_linen_nor_dx_1k.jpg", "rough_linen_rough_1k.jpg"),
    "SportMesh": ("ambientcg/Fabric070", "Fabric070_1K-JPG_NormalDX.jpg", "Fabric070_1K-JPG_Roughness.jpg"),
}
FOLDS = ("polyhaven/denmin_fabric_02", "denmin_fabric_02_nor_dx_1k.jpg")


def load(path, size=None):
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = "Non-Color"
    if size and (img.size[0] != size or img.size[1] != size):
        img.scale(size, size)
    w, h = img.size
    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    bpy.data.images.remove(img)
    return a


def save(arr, path):
    h, w = arr.shape[:2]
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = "Non-Color"
    img.pixels[:] = np.clip(arr, 0, 1).reshape(-1).tolist()
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def blur(ch, sigma):
    """Гауссово размытие с циклическими краями (текстуры тайлятся) через БПФ."""
    h, w = ch.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(ch) * g)).astype(np.float32)


def main():
    os.makedirs(OUT, exist_ok=True)
    stats = {}
    for name, (d, nor, rough) in SETS.items():
        n = load(os.path.join(CACHE, d, nor), SIZE)
        r = load(os.path.join(CACHE, d, rough), SIZE)[..., 0]
        m, s = float(r.mean()), float(r.std()) or 1e-3
        rn = np.clip(0.5 + (r - m) / (4.0 * s), 0, 1)
        nx, ny = n[..., 0] * 2 - 1, n[..., 1] * 2 - 1
        amp = float(np.percentile(np.hypot(nx, ny), 95))
        # фактура нормирована: p95 |xy| = NORM_P95 (иначе тонкая кожа — ±5 уровней 8 бит и полосы при усилении в
        # материале); сила в MI — NormalStrength ~ 0.5..1.5 для всех текстур одинаково
        k = NORM_P95 / max(1e-3, amp)
        nx, ny = nx * k, ny * k
        ln = np.maximum(1.0, np.hypot(nx, ny) / 0.98)
        nx, ny = nx / ln, ny / ln
        out = np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, rn, np.ones_like(rn)], axis=-1)
        save(out, os.path.join(OUT, "T_%s_NR.png" % name))
        stats[name] = {"src": d, "rough_mean": round(m, 4), "rough_std": round(s, 4), "normal_xy_p95": round(amp, 4), "normal_gain": round(k, 3)}
        print("MATTEX %s: rough %.3f±%.3f, |xy| p95 %.3f" % (name, m, s, amp))
    # складки: мягкие волны заломов без переплетения
    n = load(os.path.join(CACHE, FOLDS[0], FOLDS[1]), SIZE)
    nx = blur(n[..., 0] * 2 - 1, 9.0)
    ny = blur(n[..., 1] * 2 - 1, 9.0)
    k = 0.6 / max(1e-3, float(np.percentile(np.hypot(nx, ny), 99)))
    nx, ny = nx * k, ny * k
    half = np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, np.full_like(nx, 0.5), np.ones_like(nx)], axis=-1)
    half = half.reshape(512, 2, 512, 2, 4).mean(axis=(1, 3))       # 1K → 512 (усреднение 2×2)
    save(half, os.path.join(OUT, "T_Folds_N.png"))
    stats["Folds"] = {"src": FOLDS[0], "blur_sigma_px": 9.0, "gain": round(k, 3)}
    with open(os.path.join(OUT, "mat_textures.json"), "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False, indent=1)
    print("MATTEX готово → " + OUT)


main()
