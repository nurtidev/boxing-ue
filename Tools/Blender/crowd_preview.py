# -*- coding: utf-8 -*-
# S-77: контрольный снимок вариантов публики (Saved/CrowdWork/SM_Crowd_*.fbx) — EEVEE, цвет по зонам (как в M_Crowd,
# первые оттенки палитры) × «деталь»; слева направо все варианты, сверху — поза «болеют» (смещение из UV0/цвета).
#   blender.exe -b --factory-startup --python Tools\Blender\crowd_preview.py  → Saved/CrowdWork/crowd_preview.png
import glob
import os

import bpy
import numpy as np
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(ROOT, "Saved", "CrowdWork")
ZONE = [(0.78, 0.6, 0.45), (0.15, 0.3, 0.7), (0.12, 0.13, 0.16), (0.05, 0.04, 0.03), (0.9, 0.9, 0.9)]

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

mat = bpy.data.materials.new("Zone")
mat.use_nodes = True
nt = mat.node_tree
attr = nt.nodes.new("ShaderNodeAttribute")
attr.attribute_name = "Show"
nt.links.new(attr.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])
nt.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.8

files = sorted(glob.glob(os.path.join(WORK, "SM_Crowd_*.fbx")))
for i, f in enumerate(files):
    for cheer in (0, 1):
        bpy.ops.import_scene.fbx(filepath=f)
        o = bpy.context.selected_objects[0]
        me = o.data
        o.location = Vector((0, i * 0.9 - (len(files) - 1) * 0.45, cheer * 1.6))
        src = me.color_attributes[0]
        n = len(me.loops)
        c = np.zeros(n * 4, dtype=np.float32)
        src.data.foreach_get("color", c)
        c = c.reshape(-1, 4)
        reg = np.clip(np.round(c[:, 0] * 4), 0, 4).astype(int)
        out = np.array(ZONE, dtype=np.float32)[reg] * (2 * c[:, 1:2])
        show = me.color_attributes.new("Show", "FLOAT_COLOR", "CORNER")
        col = np.ones((n, 4), dtype=np.float32)
        col[:, :3] = np.clip(out, 0, 1)
        show.data.foreach_set("color", col.reshape(-1))
        if cheer:
            uv0 = np.zeros(n * 2, dtype=np.float32)
            me.uv_layers[0].data.foreach_get("uv", uv0)
            uv0 = uv0.reshape(-1, 2)
            vi = np.zeros(n, dtype=np.int32)
            me.loops.foreach_get("vertex_index", vi)
            co = np.zeros(len(me.vertices) * 3, dtype=np.float32)
            me.vertices.foreach_get("co", co)
            co = co.reshape(-1, 3)
            # dx = (A − 0.5)·1.5, dy_UE = (B − 0.5)·1.5 → Blender y = −dy_UE, dz = U0 (импорт FBX в Blender — исходные оси)
            d = np.zeros_like(co)
            d[vi, 0] = (c[:, 3] - 0.5) * 1.5
            d[vi, 1] = -(c[:, 2] - 0.5) * 1.5
            d[vi, 2] = uv0[:, 0]
            sc = o.scale[0] or 1.0
            me.vertices.foreach_set("co", (co + d / sc).reshape(-1))
            me.update()
        me.materials.clear()
        me.materials.append(mat)

sc = bpy.context.scene
sc.render.engine = "BLENDER_EEVEE"
sc.render.resolution_x, sc.render.resolution_y = 1800, 900
cam = bpy.data.cameras.new("C")
cam.type = "ORTHO"
cam.ortho_scale = max(4.0, len(files) * 0.95)
co = bpy.data.objects.new("C", cam)
bpy.context.collection.objects.link(co)
co.location = (6.0, -2.5, 1.3)
co.rotation_euler = (Vector((0, 0, 0.9)) - co.location).to_track_quat("-Z", "Y").to_euler()
sc.camera = co
sun = bpy.data.lights.new("S", "SUN")
sun.energy = 3.5
so = bpy.data.objects.new("S", sun)
bpy.context.collection.objects.link(so)
so.rotation_euler = (0.9, 0.2, 0.8)
sc.world = bpy.data.worlds.new("W")
sc.world.use_nodes = True
sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.35, 0.37, 0.4, 1)
sc.render.filepath = os.path.join(WORK, "crowd_preview.png")
bpy.ops.render.render(write_still=True)
print("CROWDPREVIEW " + sc.render.filepath)
