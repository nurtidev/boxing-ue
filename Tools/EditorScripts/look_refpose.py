# -*- coding: utf-8 -*-
# S-41 (облик боксёра): сверка позы привязки (ref pose) наших мешей с телом Kellan — кости должны совпадать
# (иначе ретаргет/копия позы уводят меш). Коммандлет, только читает. Маркер: LOOKREF
import unreal

MESHES = {
    "kellan": "/Game/MetaHumans/Kellan/Male/Medium/NormalWeight/Body/m_med_nrw_body",
    "mocap": "/Game/MetaHumans/Common/Common/Mocap/m_med_nrw_body_mocap",
    "body": "/Game/BoxingLocal/Characters/SKM_BoxerBody",
    "kit": "/Game/BoxingLocal/Characters/SKM_BoxerKit",
    "gloves": "/Game/Boxing/Characters/Meshes/SKM_BoxerGloves",
}
BONES = ["root", "pelvis", "spine_05", "head", "upperarm_l", "hand_l", "hand_r", "foot_l"]


def fmt(t):
    r = t.rotation.rotator()
    return "T(%.1f %.1f %.1f) R(%.1f %.1f %.1f) S(%.3f)" % (t.translation.x, t.translation.y, t.translation.z,
                                                          r.pitch, r.yaw, r.roll, t.scale3d.x)


for key, path in MESHES.items():
    m = unreal.load_asset(path)
    if not m:
        continue
    mod = unreal.SkeletonModifier()
    mod.set_skeletal_mesh(m)
    names = [str(n) for n in mod.get_all_bone_names()]
    unreal.log("LOOKREF %s: костей %d, первые %s" % (key, len(names), names[:3]))
    for b in BONES:
        if b in names:
            unreal.log("LOOKREF %s %-10s local %s | global %s" % (key, b, fmt(mod.get_bone_transform(b, False)),
                                                                   fmt(mod.get_bone_transform(b, True))))
