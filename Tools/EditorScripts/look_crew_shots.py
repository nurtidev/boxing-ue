# -*- coding: utf-8 -*-
# S-68: угловые (тренер и катмен у красного и синего углов, стул) — СТАТИЧНАЯ расстановка по маркерам L_Ring для
# снимков и замера FPS (поведение — game-feel, следующий спринт). Два прохода (оба — этот файл), как look_referee_shots:
#  1) КОММАНДЛЕТ собирает копию ринга /Game/BoxingLocal/Tmp/L_RingCrewLook (вне git; L_Ring НЕ трогается): по
#     персонажу GASP (SandboxCharacter_CMC, тег CornerCrewLook) на каждого углового в точке CornerCrew_<угол>_<роль>_Fight,
#     ChildActorComponent «VisualOverride» → BP_CornerCoach_<угол> / BP_Cutman_<угол>; стулья BP_CornerStool_<угол> —
#     в CornerStool_<угол>_Stow (убраны за столб):
#       UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#  2) ИГРА (бой идёт как обычно; угловые — в idle Motion Matching):
#       UnrealEditor.exe <uproject> /Game/BoxingLocal/Tmp/L_RingCrewLook -game -RenderOffscreen -windowed -ResX=1920 ^
#         -ResY=1080 -ForceRes -unattended -nosound -BoxAutopilot -BoxSeed=7 -BoxQuitAfter=120 ^
#         -ExecCmds="DisableAllScreenMessages,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/look_crew_shots.py"
#     (из Git Bash — с MSYS_NO_PATHCONV=1)
# Окружение (игра):
#   CREW_MODE=fight|rest — где угловые: fight — на полу у помоста (маркеры *_Fight), стулья убраны; rest — на апроне
#                          (маркеры *_Rest), стулья в углах (CornerStool_<угол>)
#   CREW_RED_ID / CREW_BLUE_ID — id бойца ростера: облик угловых — записи Appearance.json «crew:<id>:coach|cutman»
#                          (как сделает рантайм), иначе — облик по умолчанию, запечённый в BP
#   CREW_SHOTS=1 — свои ракурсы (угол целиком, крупно тренер/катмен, бутылка, полотенце, стул), CREW_GAME="20,40" —
#                  снимки игровой камерой (с от старта скрипта), CREW_PREFIX=crew_<mode>, CREW_QUIT=1 — выйти после снимков
#   CREW_LOD=N — принудительный LOD угловых (замер бюджета), CREW_SHADOW=0 — без теней
#   Замер FPS — ring_perf.py с PERF_CREW=fight|rest на этом же уровне (он зовёт place_crew отсюда).
import os
import sys
import time

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
HERE = os.path.join(PROJECT, "Tools", "EditorScripts")
if HERE not in sys.path:
    sys.path.insert(0, HERE)
OUT = os.path.join(PROJECT, "Docs", "screens")
MODE = os.environ.get("CREW_MODE", "fight")
PREFIX = os.environ.get("CREW_PREFIX", "crew_" + MODE)
SHOTS = os.environ.get("CREW_SHOTS", "1") == "1"
GAME_T = [float(x) for x in os.environ.get("CREW_GAME", "").split(",") if x]
QUIT = os.environ.get("CREW_QUIT", "1") == "1"
SANDBOX = "/Game/Blueprints/SandboxCharacter_CMC.SandboxCharacter_CMC_C"
LOOK = {"Coach": "/Game/BoxingLocal/Characters/BP_CornerCoach_%s.BP_CornerCoach_%s_C",
        "Cutman": "/Game/BoxingLocal/Characters/BP_Cutman_%s.BP_Cutman_%s_C"}
STOOL = "/Game/Boxing/Characters/BP_CornerStool_%s.BP_CornerStool_%s_C"
LEVEL_SRC = "/Game/Boxing/Maps/L_Ring"
LEVEL = "/Game/BoxingLocal/Tmp/L_RingCrewLook"
IS_GAME = "-game" in unreal.SystemLibrary.get_command_line().lower()
CORNERS = ("Red", "Blue")
ROLES = ("Coach", "Cutman")
SETTLE = 8


def log(m):
    unreal.log("CREWSHOT " + str(m))


def V(x, y, z):
    return unreal.Vector(x, y, z)


def markers(world_or_none, eas=None):
    """{label: (location, yaw)} маркеров CornerCrew_* / CornerStool_* (TargetPoint по тегам)."""
    out = {}
    if eas is not None:
        acts = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.TargetPoint)]
    else:
        acts = unreal.GameplayStatics.get_all_actors_of_class(world_or_none, unreal.TargetPoint)
    for a in acts:
        tags = [str(t) for t in a.tags]
        if "CornerCrew" in tags or "CornerStool" in tags:
            out[tags[-1]] = (a.get_actor_location(), a.get_actor_rotation().yaw)
    return out


def build_level():
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(LEVEL):
        eal.delete_asset(LEVEL)
    if not eal.duplicate_asset(LEVEL_SRC, LEVEL):
        raise RuntimeError("не скопировался " + LEVEL_SRC)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)
    mk = markers(None, eas)
    if len(mk) < 12:
        raise RuntimeError("в L_Ring нет маркеров угловых (%d) — пересобрать build_ring.py" % len(mk))
    cls = unreal.load_class(None, SANDBOX)
    for c in CORNERS:
        for r in ROLES:
            loc, yaw = mk["CornerCrew_%s_%s_Fight" % (c, r)]
            a = eas.spawn_actor_from_class(cls, V(loc.x, loc.y, loc.z + 96.0), unreal.Rotator(0, 0, yaw))
            a.tags = ["CornerCrewLook", c, r]
            a.set_actor_label("CrewLook_%s_%s" % (c, r))
            look = unreal.load_class(None, LOOK[r] % (c, c))
            for comp in a.get_components_by_class(unreal.ChildActorComponent):
                if comp.get_name().startswith("VisualOverride"):
                    comp.set_child_actor_class(look)
            log("угловой %s %s: %s" % (c, r, a.get_actor_location()))
        loc, yaw = mk["CornerStool_%s_Stow" % c]
        s = eas.spawn_actor_from_class(unreal.load_class(None, STOOL % (c, c)), V(loc.x, loc.y, loc.z), unreal.Rotator(0, 0, yaw))
        s.tags = ["CornerStoolLook", c]
        s.set_actor_label("StoolLook_" + c)
    les.save_current_level()
    log("уровень %s сохранён" % LEVEL)


# ------------------------------------------------------------------ игра
def game_world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if unreal.GameplayStatics.get_player_controller(w, 0) and w.get_name() != "Untitled":
                if unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BoxerCharacter):
                    return w
        except Exception:  # noqa
            pass
    return None


def visual(a):
    for c in a.get_components_by_class(unreal.ChildActorComponent):
        ch = c.get_editor_property("child_actor")
        if ch:
            return c, ch
    return None, None


def visual_body(a):
    _, ch = visual(a)
    if ch:
        for sk in ch.get_components_by_class(unreal.SkeletalMeshComponent):
            if sk.get_name().startswith("Body"):
                return sk
    return None


def apply_looks(w, crew):
    """Облик угловых под конкретного бойца (как сделает рантайм: ApplyBoxerLook записью «crew:<id>:<роль>»)."""
    import look_apply
    for c, env in (("Red", "CREW_RED_ID"), ("Blue", "CREW_BLUE_ID")):
        fid = os.environ.get(env)
        if not fid:
            continue
        for r in ROLES:
            rec = look_apply.find("crew:%s:%s" % (fid, r.lower()))
            a = crew.get((c, r))
            if not rec or not a:
                log("нет облика crew:%s:%s" % (fid, r.lower()))
                continue
            comp, ch = visual(a)
            look_apply.apply(ch, rec["look"], comp)
            log("облик %s %s ← %s" % (c, r, rec["id"]))


def place_crew(w, mode):
    """Угловые и стулья — в точки режима (fight/rest). Возвращает {(угол, роль): актёр}."""
    mk = markers(w)
    crew = {}
    for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "CornerCrewLook"):
        c, r = str(a.tags[1]), str(a.tags[2])
        crew[(c, r)] = a
        mesh = a.get_editor_property("mesh")
        mesh.set_editor_property("visibility_based_anim_tick_option",
                                 unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        mesh.set_visibility(False, False)
        loc, yaw = mk["CornerCrew_%s_%s_%s" % (c, r, "Rest" if mode == "rest" else "Fight")]
        half = a.get_editor_property("capsule_component").get_scaled_capsule_half_height()
        a.set_actor_location_and_rotation(V(loc.x, loc.y, loc.z + half + 2.0), unreal.Rotator(0, 0, yaw), False, True)
    for s in unreal.GameplayStatics.get_all_actors_with_tag(w, "CornerStoolLook"):
        c = str(s.tags[1])
        loc, yaw = mk["CornerStool_%s%s" % (c, "" if mode == "rest" else "_Stow")]
        s.set_actor_location_and_rotation(V(loc.x, loc.y, loc.z), unreal.Rotator(0, 0, yaw), False, True)
        s.set_actor_hidden_in_game(mode != "rest")
    lod = int(os.environ.get("CREW_LOD", "0"))           # 0 — авто (LODSync MetaHuman), N — принудительно LOD N
    shadow = os.environ.get("CREW_SHADOW", "1") != "0"
    if lod or not shadow:
        for a in crew.values():
            _, ch = visual(a)
            if not ch:
                continue
            for comp in ch.get_components_by_class(unreal.PrimitiveComponent):
                if not shadow:
                    comp.set_cast_shadow(False)
                if lod and isinstance(comp, unreal.SkinnedMeshComponent):
                    comp.set_forced_lod(lod + 1)
            for ls in ch.get_components_by_class(unreal.LODSyncComponent):
                if lod:
                    ls.set_editor_property("forced_lod", lod)
    log("расстановка %s: угловых %d, LOD %s, тени %s" % (mode, len(crew), lod or "авто", shadow))
    return crew


def main():
    os.makedirs(OUT, exist_ok=True)
    st = {"phase": "wait", "frames": 0, "queue": [], "h": None, "shot": 0, "t0": time.time()}
    ctx = {}

    def finish():
        unreal.unregister_slate_post_tick_callback(st["h"])
        log("готово: %d снимков" % st["shot"])
        if QUIT:
            unreal.SystemLibrary.execute_console_command(ctx.get("world"), "quit")

    def shoot(name):
        fn = os.path.join(OUT, "%s_%s.png" % (PREFIX, name)).replace("\\", "/")
        unreal.SystemLibrary.execute_console_command(ctx["world"], 'HighResShot 1 filename="%s"' % fn)
        st["shot"] += 1
        log("снимок %s" % fn)

    def place_cam(loc, look, fov):
        cam = ctx["cam"]
        cam.set_actor_location_and_rotation(loc, unreal.MathLibrary.find_look_at_rotation(loc, look), False, False)
        cc = cam.get_component_by_class(unreal.CameraComponent)
        if cc:
            cc.set_editor_property("field_of_view", fov)

    def views():
        out = []
        for c in CORNERS:
            sx = -1.0 if c == "Red" else 1.0
            corner = V(sx * 263, sx * 263, 0)
            n = V(-sx, -sx, 0) * (1 / 1.4142)          # из угла к центру ринга
            side = V(sx, -sx, 0) * (1 / 1.4142)
            if MODE == "rest":
                # как web restShot: из ринга на угол, чуть сбоку; и общий план угла сверху
                out.append((c.lower() + "_corner", corner + n * 290 + side * 50 + V(0, 0, 162), corner + n * 25 + V(0, 0, 88), 60.0))
            else:
                # бой: угловые на полу за помостом — вид из ринга на высоте глаз бойца и снаружи с пола
                out.append((c.lower() + "_corner", corner + n * 330 + V(0, 0, 165), corner - n * 60 + V(0, 0, 20), 60.0))
                out.append((c.lower() + "_outside", corner - n * 420 + side * 160 + V(0, 0, 60),
                            corner - n * 120 + V(0, 0, -20), 55.0))
            for r in ROLES:
                a = ctx["crew"].get((c, r))
                body = visual_body(a) if a else None
                if not body:
                    continue
                head = body.get_socket_location("head")
                neck = body.get_socket_location("neck_01")
                f = a.get_actor_forward_vector()
                rt = a.get_actor_right_vector()
                # в бою угловой у помоста лицом к рингу: камера — над апроном, сверху-спереди
                up = 0.0 if MODE == "rest" else 1.0
                out.append(("%s_%s" % (c.lower(), r.lower()), neck + f * (210 - 80 * up) + rt * 60 + V(0, 0, -25 + 150 * up),
                            neck + V(0, 0, -45), 45.0))
                out.append(("%s_%s_face" % (c.lower(), r.lower()), head + f * 70 + rt * 22 + V(0, 0, 25 * up), head, 32.0))
                if r == "Cutman":
                    hand = body.get_socket_location("hand_l")
                    out.append(("%s_cutman_bottle" % c.lower(), hand + f * 60 - rt * 40 + V(0, 0, 15), hand, 34.0))
                    sh = body.get_socket_location("clavicle_l")
                    out.append(("%s_cutman_towel" % c.lower(), sh + f * 80 - rt * 55 + V(0, 0, 20), sh + V(0, 0, -10), 36.0))
            if MODE == "rest" and c == "Red":
                st_loc = V(sx * 263, sx * 263, 0)
                out.append(("red_stool", st_loc + n * 140 + side * 60 + V(0, 0, 90), st_loc + V(0, 0, 30), 40.0))
        return out

    def hide_boxers(h):
        for b in ctx["boxers"]:
            b.set_actor_hidden_in_game(h)
            _, ch = visual(b)
            if ch:
                ch.set_actor_hidden_in_game(h)

    def tick(dt):
        try:
            step()
        except Exception as e:  # noqa
            log("ERROR %s" % e)
            finish()

    def step():
        st["frames"] += 1
        t = time.time() - st["t0"]
        if st["phase"] == "wait":
            w = game_world()
            if w is None or st["frames"] < 20:
                return
            ctx["world"] = w
            ctx["boxers"] = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BoxerCharacter)
            ctx["crew"] = place_crew(w, MODE)
            apply_looks(w, ctx["crew"])
            ctx["pc"] = unreal.GameplayStatics.get_player_controller(w, 0)
            st["phase"], st["frames"] = "game", 0
            return
        if st["phase"] == "game":
            if GAME_T and t >= GAME_T[0]:
                shoot("game_%02d" % int(GAME_T.pop(0)))
                return
            if GAME_T:
                return
            if not SHOTS:
                finish()
                return
            ctx["cam"] = ctx["pc"].get_view_target()
            ctx["pc"].set_actor_tick_enabled(False)
            unreal.GameplayStatics.set_global_time_dilation(ctx["world"], 0.0001)
            hide_boxers(True)
            st["queue"] = views()
            st["phase"], st["frames"] = "shoot", 0
            return
        if st["phase"] == "shoot":
            if not st["queue"]:
                finish()
                return
            name, loc, look, fov = st["queue"][0]
            if st["frames"] == 1:
                place_cam(loc, look, fov)
            if st["frames"] == SETTLE:
                shoot(name)
            if st["frames"] >= SETTLE + 3:
                st["queue"].pop(0)
                st["frames"] = 0

    st["h"] = unreal.register_slate_post_tick_callback(tick)
    log("старт: режим %s, префикс %s" % (MODE, PREFIX))


if __name__ != "look_crew_shots":     # ring_perf.py импортирует place_crew — без запуска
    if IS_GAME:
        main()
    else:
        build_level()
