# S-69: какие состояния AnimBP локомоции реально бывают в бою (чтобы урезанные chooser'ы не теряли нужных строк).
# Раз в LOCO_PROBE_DT с (0.25) снимает у логических мешей бойцов/рефери/угловых (AnimInstance = ABP_Boxer или
# SandboxCharacter_CMC_ABP) Gait / Stance / MovementState / MovementMode / MMDatabaseLOD / Speed2D / флаги строк
# chooser'ов (IsStarting, IsPivoting, ShouldTurnInPlace, ShouldSpinTransition, JustLanded_*, JustTraversed) и
# текущую базу Motion Matching, в конце — сводку в лог («LOCOPROBE …»).
#
#   UnrealEditor.exe <uproject> /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -ResX=960 -ResY=540 -unattended -nosound ^
#     -BoxAutopilot -BoxSeed=60 -BoxQuitAfter=70 -ExecCmds="py <абс.путь>/anim_loco_probe.py"
import collections
import os
import re
import time

import unreal

DT = float(os.environ.get("LOCO_PROBE_DT", "0.25"))
DUR = float(os.environ.get("LOCO_PROBE_DUR", "60"))
KEYS = ["Gait", "Stance", "MovementState", "MovementMode", "MMDatabaseLOD", "RotationMode", "IsStarting", "IsPivoting",
        "ShouldTurnInPlace", "ShouldSpinTransition", "JustLanded_Light", "JustLanded_Heavy", "JustTraversed"]
st = {"t0": time.time(), "last": 0.0, "h": None, "n": 0, "dumped": False,
      "c": collections.defaultdict(collections.Counter), "speed": collections.defaultdict(float), "db": collections.Counter()}
T = unreal.BoxerAssetTools


def log(m):
    unreal.log("LOCOPROBE " + m)


def world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if unreal.GameplayStatics.get_player_controller(w, 0):
                return w
        except Exception:  # noqa
            pass
    return None


def props(obj):
    out = {}
    for line in T.export_properties_text(obj).splitlines():
        k, _, v = line.partition("=")
        out[k] = v
    return out


def short_db(v):
    m = re.findall(r"(PSD_[A-Za-z_]+)", v or "")
    return ",".join(sorted(set(m))) if m else ""


def sample():
    w = world()
    if not w:
        return
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Character):
        mesh = a.get_editor_property("mesh")
        ai = mesh.get_anim_instance() if mesh else None
        if ai is None or "ABP" not in ai.get_class().get_name():
            continue
        p = props(ai)
        if not st["dumped"]:
            st["dumped"] = True
            log("класс %s, свойства с базами: %s" % (ai.get_class().get_name(),
                [k for k in p if "atabase" in k or "Chooser" in k or "MotionMatch" in k]))
        who = a.get_class().get_name().replace("_C", "")
        for k in KEYS:
            if k in p:
                st["c"][who + "." + k][p[k].split("::")[-1]] += 1
        try:
            sp = float(p.get("Speed2D", "0") or 0)
        except ValueError:
            sp = 0.0
        st["speed"][who] = max(st["speed"][who], sp)
        for k, v in p.items():
            if "atabase" in k:
                d = short_db(v)
                if d:
                    st["db"][d] += 1
    st["n"] += 1


def tick(dt):
    t = time.time() - st["t0"]
    if t - st["last"] >= DT:
        st["last"] = t
        try:
            sample()
        except Exception as e:  # noqa
            log("ошибка: %s" % e)
    if t > DUR:
        unreal.unregister_slate_post_tick_callback(st["h"])
        log("снимков %d" % st["n"])
        for k in sorted(st["c"]):
            log("%s: %s" % (k, dict(st["c"][k])))
        for k, v in st["speed"].items():
            log("%s: Speed2D макс %.0f см/с" % (k, v))
        for k, v in st["db"].most_common(40):
            log("база %s: %d" % (k, v))


st["h"] = unreal.register_slate_post_tick_callback(tick)
log("старт: шаг %.2f с, %d с" % (DT, DUR))
