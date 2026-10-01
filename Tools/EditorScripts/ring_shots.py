# -*- coding: utf-8 -*-
# S-41 трек A: скриншоты уровня L_Ring с камер PreviewCam / SideCam (нужен РЕНДЕР — не -nullrhi).
# Запуск (редактор с окном, сам закрывается по готовности):
#   "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" ^
#     C:\Users\user\Desktop\boxing-ue\BoxingUE.uproject /Game/Boxing/Maps/L_Ring ^
#     "-ExecCmds=py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/ring_shots.py" -nosplash
#   (НЕ -ExecutePythonScript и НЕ -unattended: с ними редактор закрывается сразу после старта скрипта.
#    Из Git Bash — с MSYS_NO_PATHCONV=1, иначе /Game/... превращается в путь Windows.)
# Камеры: PreviewCam, SideCam, ArenaCam. PNG → Docs/screens/ring_*.png. Маркеры лога — «RINGSHOT».
import os
import time
import unreal

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "Docs", "screens")
SHOTS = [("PreviewCam", "ring_preview.png"), ("SideCam", "ring_side.png"), ("ArenaCam", "ring_arena.png")]
WARMUP = float(os.environ.get("RING_SHOT_WARMUP", "45"))  # с: компиляция шейдеров, Lumen, объёмный туман
GAP = 12.0
RES = (1920, 1080)

os.makedirs(OUT, exist_ok=True)
state = {"t0": time.time(), "i": 0, "next": None, "handle": None, "loaded": False}


def log(m):
    unreal.log("RINGSHOT " + m)


def find_cam(label):
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def tick(dt):
    now = time.time()
    if not state["loaded"]:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if world is None or "L_Ring" not in world.get_path_name():
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Boxing/Maps/L_Ring")
        state["loaded"] = True
        state["next"] = now + WARMUP
        unreal.SystemLibrary.execute_console_command(None, "r.ScreenPercentage 100")
        log("уровень загружен, прогрев %.0f с" % WARMUP)
        return
    if now < state["next"]:
        return
    i = state["i"]
    if i < len(SHOTS):
        label, fname = SHOTS[i]
        # состояние — ДО снимка: вызов прокачивает тики Slate и колбэк входит повторно
        state["i"] = i + 1
        state["next"] = now + GAP
        cam = find_cam(label)
        path = os.path.join(OUT, fname)
        if cam is None:
            log("нет камеры " + label)
        else:
            # Вьюпорт — в камеру (чтобы Lumen/туман накопили историю), затем снимок с задержкой.
            loc, rot = cam.get_actor_location(), cam.get_actor_rotation()
            unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(loc, rot)
            unreal.AutomationLibrary.take_high_res_screenshot(RES[0], RES[1], path, camera=cam, delay=4.0)
            log("снимок %s → %s" % (label, path))
        return
    log("готово, выход")
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.SystemLibrary.quit_editor()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
log("скрипт запущен, вывод в " + OUT)
