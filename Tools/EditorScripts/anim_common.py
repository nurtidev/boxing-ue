# Общие хелперы скриптов трека C (anim_*.py): пути, выход из полного редактора, сэмплинг костей.
import unreal

MX = "/Game/BoxingLocal/Mixamo"                 # сырые импорты Mixamo (вне git)
RTG_OUT = "/Game/BoxingLocal/Retargeted"        # ретаргетнутые клипы (вне git)
ANIM = "/Game/Boxing/Anim"                      # производные монтажи/ABP (зависят от BoxingLocal!)
UEFN_SKEL = "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin"
UEFN_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"
MANNY_SKEL = "/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin"
MANNY_MESH = "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple"

AL = getattr(unreal, "AnimationLibrary", None)  # в -game редакторных библиотек нет
eal = getattr(unreal, "EditorAssetLibrary", None)


def is_commandlet():
    return "-run=pythonscript" in unreal.SystemLibrary.get_command_line().lower()


def quit_when_idle(seconds=4.0, then=None):
    """Полный редактор: дождаться фоновых задач (сжатие анимаций, сохранение) и закрыть его.
    Сразу после сохранения quit_editor роняет редактор на ассерте кэша сжатия
    (CacheTasksByKeyHash) — поэтому выходим через несколько секунд тиков."""
    if is_commandlet():
        return
    state = {"t": 0.0, "h": None}

    def tick(dt):
        state["t"] += dt
        if state["t"] < seconds:
            return
        unreal.unregister_slate_post_tick_callback(state["h"])
        if then:
            then()
        unreal.SystemLibrary.quit_editor()

    state["h"] = unreal.register_slate_post_tick_callback(tick)


class BoneSampler:
    """Компонентные (mesh-space) позиции костей клипа по времени через AnimationLibrary."""

    def __init__(self, seq, bones):
        self.seq = seq
        self.paths = {b: [str(n) for n in AL.find_bone_path_to_root(seq, b)] for b in bones}
        self.names = sorted({n for p in self.paths.values() for n in p})

    def cs(self, t):
        loc = dict(zip(self.names, AL.get_bone_poses_for_time(self.seq, self.names, t, False)))
        out = {}
        for b, path in self.paths.items():
            tr = unreal.Transform()
            for n in reversed(path):  # от корня к кости: Child_cs = Child_local * Parent_cs
                tr = unreal.MathLibrary.compose_transforms(loc[n], tr)
            out[b] = tr
        return out

    def frames(self):
        ln = AL.get_sequence_length(self.seq)
        nf = max(1, AL.get_num_frames(self.seq))
        # клип knockdown импортирован с 330 fps — сэмплим не чаще 60 Гц
        step = max(1, int(round(nf / (ln * 60.0))))
        return [min(ln, ln * i / nf) for i in range(0, nf + 1, step)], ln


# --- Ключевые кадры (как keyTimes в web/src/ui/three/SkinnedFighter.tsx) ----------------------
# Удар: контакт = максимальный вынос кисти бьющей руки ВПЕРЁД от таза (меш смотрит вдоль +Y).
# Для справки считаем и максимум |кисть − плечо| (на хуках/апперкотах он раньше контакта —
# это замах/разгон, поэтому основной критерий — вынос вперёд, как в web).
PUNCH_ARM = {"jab": "l", "cross": "r", "hookL": "l", "hookR": "r", "upperL": "l", "upperR": "r",
             "bodyHook": "l"}
KEY_BONES = ["root", "pelvis", "head", "upperarm_l", "hand_l", "upperarm_r", "hand_r"]


def key_times(seq, clip):
    """-> dict(kind, t, value, t_ext) для клипа (имя web-клипа: jab, block, slip, ...)."""
    s = BoneSampler(seq, KEY_BONES)
    times, ln = s.frames()
    poses = [(t, s.cs(t)) for t in times]
    p = lambda pose, b: pose[b].translation  # noqa: E731
    if clip in PUNCH_ARM:
        a = PUNCH_ARM[clip]
        win = [x for x in poses if x[0] <= ln * 0.8]
        up = 1.0 if clip.startswith("upper") else 0.0  # апперкот: «вперёд + вверх» (вперёд пикует на замахе)
        fwd = max(win, key=lambda x: (p(x[1], "hand_" + a).y - p(x[1], "pelvis").y)
                  + up * (p(x[1], "hand_" + a).z - p(x[1], "pelvis").z))
        ext = max(win, key=lambda x: (p(x[1], "hand_" + a) - p(x[1], "upperarm_" + a)).length())
        return {"kind": "punch", "arm": a, "t": fwd[0],
                "value": p(fwd[1], "hand_" + a).y - p(fwd[1], "pelvis").y,
                "t_ext": ext[0], "len": ln}
    if clip in ("block", "blockHit"):
        best = max(poses, key=lambda x: min(p(x[1], "hand_l").z, p(x[1], "hand_r").z))
        return {"kind": "guard_up", "t": best[0], "len": ln}
    # слип/уклон, реакции: пик смещения головы от первого кадра (в сторону + вниз)
    def head_rel(pose):  # голова относительно таза: в слоте UpperBody таз и ноги — от Motion Matching
        return p(pose, "head") - p(pose, "pelvis")

    h0 = head_rel(poses[0][1])

    def head_score(x):
        d = head_rel(x[1]) - h0
        return (d.x ** 2 + d.y ** 2) ** 0.5 - d.z

    best = max(poses, key=head_score)
    d = head_rel(best[1]) - h0
    return {"kind": "head_peak", "t": best[0], "dx": d.x, "dy": d.y, "dz": d.z, "len": ln}
