# -*- coding: utf-8 -*-
# S-64: замер производительности арены В ИГРЕ (-game, автопилот) — CSV-профайлер + ProfileGPU в лог.
# Работает в игре: -ExecCmds="py <этот файл>". Результат: Saved/Profiling/CSV/*.csv (FrameTime, GameThreadTime,
# RenderThreadTime, GPUTime, RHI/DrawCalls, RHI/PrimitivesDrawn) и в логе «RINGPERF» + дерево ProfileGPU.
#
#   UnrealEditor.exe <uproject> /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 ^
#     -ForceRes -unattended -nosound -BoxAutopilot -BoxSeed=60 -BoxShots=30 -BoxShotPrefix=perf3_x -BoxQuitAfter=60 ^
#     -ExecCmds="DisableAllScreenMessages,stat unit,stat fps,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/ring_perf.py"
#
# Окружение: PERF_SP=100 — r.ScreenPercentage (по умолчанию не трогать), PERF_T0/PERF_T1 — окно CSV (с от старта
# скрипта, 18/48), PERF_GPU=50 — момент ProfileGPU (0 — не снимать), PERF_CMDS="a;b" — доп. команды консоли на старте.
import os
import time

import unreal

T0 = float(os.environ.get("PERF_T0", "18"))
T1 = float(os.environ.get("PERF_T1", "48"))
TG = float(os.environ.get("PERF_GPU", "50"))
SP = os.environ.get("PERF_SP", "")
CMDS = [c for c in os.environ.get("PERF_CMDS", "").split(";") if c.strip()]
NOSHADOW = [x for x in os.environ.get("PERF_NOSHADOW", "").split(",") if x]   # подстроки имён светильников — без теней
OFF = [x for x in os.environ.get("PERF_OFF", "").split(",") if x]           # подстроки имён — выключить свет
FG = os.environ.get("PERF_FG", "")                                              # Lumen final gather quality в PPV
PRO = os.environ.get("PERF_PRO", "") == "1"
CONTACT = os.environ.get("PERF_CONTACT", "").split(":") if os.environ.get("PERF_CONTACT") else None  # подстрока:длина                                     # оформление профи (как сделает GameMode)
st = {"t0": time.time(), "step": 0, "h": None, "frames": 0, "acc": 0.0, "worst": 0.0}


def log(m):
    unreal.log("RINGPERF " + m)


def world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if unreal.GameplayStatics.get_player_controller(w, 0):
                return w
        except Exception:  # noqa
            pass
    return None


def cmd(c):
    log("> " + c)
    unreal.SystemLibrary.execute_console_command(world(), c)


def tick(dt):
    t = time.time() - st["t0"]
    if st["step"] == 0 and world():
        if SP:
            cmd("r.ScreenPercentage " + SP)
        for c in CMDS:
            cmd(c.strip())
        w = world()
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Light):
            nm = a.get_actor_label() if hasattr(a, "get_actor_label") else a.get_name()
            if CONTACT and CONTACT[0] in nm:
                a.light_component.set_editor_property("contact_shadow_length", float(CONTACT[1]))
                a.light_component.set_cast_shadows(not a.light_component.cast_shadows)   # пересоздать состояние
                a.light_component.set_cast_shadows(not a.light_component.cast_shadows)
                log("контактные тени %s: %s" % (nm, CONTACT[1]))
            if any(x in nm for x in OFF):
                a.light_component.set_visibility(False)
                log("выключен: " + nm)
            if any(x in nm for x in NOSHADOW):
                a.light_component.set_cast_shadows(False)
                log("без теней: " + nm)
        if FG:
            for v in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PostProcessVolume):
                ps = v.settings
                ps.set_editor_property("override_lumen_final_gather_quality", True)
                ps.set_editor_property("lumen_final_gather_quality", float(FG))
                v.settings = ps
                log("Lumen final gather " + FG)
        if PRO:
            n = 0
            for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "ArenaAmateur"):
                a.set_actor_hidden_in_game(True)
                n += 1
            for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "ArenaPro"):
                a.set_actor_hidden_in_game(False)
                n += 1
            log("оформление профи: акторов %d" % n)
        st["step"] = 1
    elif st["step"] == 1 and t >= T0:
        cmd("csvprofile start")
        st["step"] = 2
    elif st["step"] == 2:
        st["frames"] += 1
        st["acc"] += dt
        st["worst"] = max(st["worst"], dt)
        if t >= T1:
            cmd("csvprofile stop")
            log("кадров %d, средн. %.2f мс (%.1f FPS), худший %.1f мс" % (
                st["frames"], 1000 * st["acc"] / max(1, st["frames"]), st["frames"] / max(1e-3, st["acc"]),
                1000 * st["worst"]))
            st["step"] = 3
    elif st["step"] == 3 and TG > 0 and t >= TG:
        cmd("ProfileGPU")
        st["step"] = 4


st["h"] = unreal.register_slate_post_tick_callback(tick)
log("старт: окно CSV %.0f..%.0f с, ProfileGPU %.0f с, ScreenPercentage %s" % (T0, T1, TG, SP or "как есть"))
