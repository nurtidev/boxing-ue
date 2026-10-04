# -*- coding: utf-8 -*-
# S-77: снимки зала (публика, судьи, материалы ринга) В ИГРЕ (-game, автопилот, Lumen) с фиксированных ракурсов —
# одинаковых «до/после». Работает в игре: -ExecCmds="py <этот файл>".
#
#   UnrealEditor.exe <uproject> /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -ForceRes ^
#     -unattended -nosound -BoxAutopilot -BoxSeed=60 -BoxQuitAfter=200 ^
#     -ExecCmds="DisableAllScreenMessages,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/ring_crowd_shots.py"
#
# Окружение: CROWD_PREFIX=arena6_before — Docs/screens/<prefix>_<ракурс>.png; CROWD_EXCITE=1 — после обычной серии ещё
# раз те же ракурсы с MPC_Crowd.Excite = 1 (реакция зала на нокдаун, суффикс _excite); CROWD_WARM=8 — с прогрева
# (компиляция шейдеров/Lumen) до первого снимка; CROWD_PRO=1 — оформление профи (как делает GameMode).
# Камера: view target контроллера (камера боя), тик контроллера выключается на время снимков.
import os
import time

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Docs", "screens")
PREFIX = os.environ.get("CROWD_PREFIX", "arena6")
EXCITE = os.environ.get("CROWD_EXCITE", "0") == "1"
WARM = float(os.environ.get("CROWD_WARM", "8"))
PRO = os.environ.get("CROWD_PRO", "0") == "1"
SETTLE = 24          # кадров на сходимость после переезда камеры (Lumen/TSR)
MPC = "/Game/Boxing/Environment/Crowd/MPC_Crowd"

# (имя, камера, цель, fov) — UE см; канвас Z = 0, пол зала −110, первый ряд трибун на 840 см от центра
VIEWS = [
    ("stands", (-150, -60, 175), (1300, 150, 60), 70.0),        # из ринга на трибуну +X на уровне глаз бойца
    ("rows", (480, -420, 40), (1000, 120, 10), 45.0),          # первые ряды крупно (из-за канатов у края помоста)
    ("closeup", (690, 260, 30), (900, 300, 0), 35.0),          # 2–3 человека вблизи: силуэт, одежда, лица
    ("judges", (-230, 330, 140), (540, -40, -40), 55.0),       # судьи у помоста
    ("arena", (-1500, -1500, 900), (0, 0, -40), 60.0),         # общий план зала
    ("canvas", (-140, -180, 60), (40, 40, 0), 50.0),           # канвас и подушки угла (фактура ткани)
]


def log(m):
    unreal.log("CROWDSHOT " + str(m))


def game_world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if unreal.GameplayStatics.get_player_controller(w, 0):
                return w
        except Exception:  # noqa
            pass
    return None


def V(t):
    return unreal.Vector(float(t[0]), float(t[1]), float(t[2]))


def main():
    os.makedirs(OUT, exist_ok=True)
    st = {"phase": "wait", "t0": time.time(), "i": 0, "frames": 0, "h": None, "pass": 0}
    ctx = {}

    def finish():
        unreal.unregister_slate_post_tick_callback(st["h"])
        log("готово")
        unreal.SystemLibrary.execute_console_command(ctx.get("world"), "quit")

    def set_excite(v):
        mpc = unreal.load_asset(MPC)
        if mpc is None:
            log("нет %s — реакция зала не снимается" % MPC)
            return False
        unreal.MaterialLibrary.set_scalar_parameter_value(ctx["world"], mpc, "Excite", float(v))
        return True

    def place(loc, look, fov):
        cam = ctx["cam"]
        rot = unreal.MathLibrary.find_look_at_rotation(V(loc), V(look))
        cam.set_actor_location_and_rotation(V(loc), rot, False, False)
        cc = cam.get_component_by_class(unreal.CameraComponent)
        if cc:
            cc.set_editor_property("field_of_view", fov)

    def step(dt):
        if st["phase"] == "wait":
            w = game_world()
            if w is None or time.time() - st["t0"] < WARM:
                return
            ctx["world"] = w
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            ctx["pc"] = pc
            if PRO:
                for tag, hide in (("ArenaAmateur", True), ("ArenaPro", False)):
                    for a in unreal.GameplayStatics.get_all_actors_with_tag(w, tag):
                        a.set_actor_hidden_in_game(hide)
            unreal.SystemLibrary.execute_console_command(w, "r.ScreenPercentage 100")
            ctx["cam"] = pc.get_view_target()
            pc.set_actor_tick_enabled(False)
            log("камера %s" % ctx["cam"].get_name())
            st["phase"], st["frames"] = "shoot", 0
            return
        if st["phase"] == "shoot":
            if st["i"] >= len(VIEWS):
                if EXCITE and st["pass"] == 0 and set_excite(1.0):
                    st["pass"], st["i"], st["frames"] = 1, 0, -60   # пару секунд на «вставание»
                    log("Excite = 1")
                    return
                finish()
                return
            name, loc, look, fov = VIEWS[st["i"]]
            if st["frames"] == 0:
                place(loc, look, fov)
            st["frames"] += 1
            if st["frames"] == SETTLE:
                fn = os.path.join(OUT, "%s_%s%s.png" % (PREFIX, name, "_excite" if st["pass"] else "")).replace("\\", "/")
                unreal.SystemLibrary.execute_console_command(ctx["world"], 'HighResShot 1 filename="%s"' % fn)
                log("снимок %s" % fn)
            if st["frames"] >= SETTLE + 4:
                st["i"] += 1
                st["frames"] = 0

    def tick(dt):
        try:
            step(dt)
        except Exception as e:  # noqa
            log("ERROR %s" % e)
            finish()

    st["h"] = unreal.register_slate_post_tick_callback(tick)


main()
