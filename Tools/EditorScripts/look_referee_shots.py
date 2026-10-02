# -*- coding: utf-8 -*-
# S-56: снимки рефери в игре (Lumen) — проверка облика и того, что клипы GASP (стойка Motion Matching)
# садятся на BP_RefereeLook_* через тот же рантайм-ретаргет, что у бойцов. Два прохода (оба — этот файл):
#  1) КОММАНДЛЕТ собирает копию ринга /Game/BoxingLocal/Tmp/L_RingRefLook (вне git; L_Ring НЕ трогается):
#     по персонажу GASP (SandboxCharacter_CMC, тег RefereeLook + облик) на каждый облик рефери, его
#     ChildActorComponent «VisualOverride» → BP_RefereeLook_* (как ABoxerCharacter::ApplyVisualOverride):
#       UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#  2) ИГРА (бой идёт как обычно, рефери стоят сбоку в idle Motion Matching) снимает ракурсы:
#       UnrealEditor.exe <uproject> /Game/BoxingLocal/Tmp/L_RingRefLook -game -RenderOffscreen -windowed ^
#         -ResX=1600 -ResY=900 -unattended -nosound -BoxAutopilot -BoxSeed=7 -BoxQuitAfter=200 ^
#         -ExecCmds="DisableAllScreenMessages,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/look_referee_shots.py"
#     (из Git Bash — с MSYS_NO_PATHCONV=1, иначе /Game/... превращается в путь Windows)
# Ракурсы: во весь рост спереди/сбоку/сзади, торс/воротник, лицо, кисти, обувь.
# Файлы: Docs/screens/<LOOK_PREFIX>_<облик>_<ракурс>.png.
# Переменные: LOOK_PREFIX=look2_ref, LOOK_REFS=Pro,Amateur, LOOK_REF_WAIT=60 (кадров idle до снимков).
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Docs", "screens")
PREFIX = os.environ.get("LOOK_PREFIX", "look2_ref")
REFS = [x for x in os.environ.get("LOOK_REFS", "Pro,Amateur").split(",") if x]
WAIT = int(os.environ.get("LOOK_REF_WAIT", "60"))
SANDBOX = "/Game/Blueprints/SandboxCharacter_CMC.SandboxCharacter_CMC_C"
LOOK = "/Game/BoxingLocal/Characters/BP_RefereeLook_%s.BP_RefereeLook_%s_C"
SETTLE = 8


def log(m):
    unreal.log("LOOKREF " + str(m))


def V(x, y, z):
    return unreal.Vector(x, y, z)


def game_world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if unreal.GameplayStatics.get_player_controller(w, 0) and w.get_name() != "Untitled":
                if unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BoxerCharacter):
                    return w
        except Exception:  # noqa
            pass
    return None


def visual_body(actor):
    for c in actor.get_components_by_class(unreal.ChildActorComponent):
        a = c.get_editor_property("child_actor")
        if a:
            for sk in a.get_components_by_class(unreal.SkeletalMeshComponent):
                if sk.get_name().startswith("Body"):
                    return sk
    return None


LEVEL_SRC = "/Game/Boxing/Maps/L_Ring"
LEVEL = "/Game/BoxingLocal/Tmp/L_RingRefLook"
IS_GAME = "-game" in unreal.SystemLibrary.get_command_line().lower()
# места рефери в копии ринга: сбоку от линии выхода бойцов (они сходятся по оси X), лицом к центру
# оба — в центре ринга лицом к +X (камеры внутри канатов); на снимке виден только снимаемый, бойцы скрыты
SPOTS = {"Pro": (0.0, 70.0), "Amateur": (0.0, -70.0)}


def build_level():
    """Коммандлет: копия L_Ring (оригинал не трогаем) + по персонажу GASP на облик рефери."""
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(LEVEL):
        eal.delete_asset(LEVEL)
    if not eal.duplicate_asset(LEVEL_SRC, LEVEL):
        raise RuntimeError("не скопировался " + LEVEL_SRC)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)
    cls = unreal.load_class(None, SANDBOX)
    for kind in REFS:
        look = unreal.load_class(None, LOOK % (kind, kind))
        x, y = SPOTS.get(kind, (0.0, 215.0))
        yaw = 0.0
        a = eas.spawn_actor_from_class(cls, V(x, y, 96.0), unreal.Rotator(0, 0, yaw))
        a.tags = ["RefereeLook", kind]
        a.set_actor_label("RefereeLook_" + kind)
        for c in a.get_components_by_class(unreal.ChildActorComponent):
            if c.get_name().startswith("VisualOverride"):
                c.set_child_actor_class(look)
        log("поставлен %s: %s" % (kind, a.get_actor_location()))
    les.save_current_level()
    log("уровень %s сохранён" % LEVEL)


def prepare(a):
    """Логический манекен GASP ведёт позу, но не рисуется (как ABoxerCharacter::ApplyVisualOverride)."""
    mesh = a.get_editor_property("mesh")
    mesh.set_editor_property("visibility_based_anim_tick_option",
                             unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    mesh.set_visibility(False, False)


def main():
    os.makedirs(OUT, exist_ok=True)
    st = {"phase": "wait", "frames": 0, "queue": [], "h": None, "shot": 0}
    ctx = {}

    def finish():
        unreal.unregister_slate_post_tick_callback(st["h"])
        log("готово: %d снимков" % st["shot"])
        unreal.SystemLibrary.execute_console_command(ctx.get("world"), "quit")

    def place(loc, look, fov):
        cam = ctx["cam"]
        cam.set_actor_location_and_rotation(loc, unreal.MathLibrary.find_look_at_rotation(loc, look), False, False)
        cc = cam.get_component_by_class(unreal.CameraComponent)
        if cc:
            cc.set_editor_property("field_of_view", fov)

    def views(kind, a):
        p = a.get_actor_location()
        f = a.get_actor_forward_vector()
        r = a.get_actor_right_vector()
        body = visual_body(a)
        floor = p.z - a.get_editor_property("capsule_component").get_scaled_capsule_half_height()
        mid = V(p.x, p.y, floor + 92)
        head = body.get_socket_location("head") if body else V(p.x, p.y, floor + 165)
        neck = body.get_socket_location("neck_01") if body else V(p.x, p.y, floor + 148)
        hand = body.get_socket_location("hand_r") if body else V(p.x, p.y, floor + 95)
        foot = body.get_socket_location("foot_l") if body else V(p.x, p.y, floor + 8)
        out = [
            ("front", mid + f * 290 + V(0, 0, 10), mid, 60.0),
            ("side", mid + r * 230 + V(0, 0, 10), mid, 70.0),
            ("back34", mid - f * 170 - r * 170 + V(0, 0, 30), mid, 68.0),
            ("torso", neck + f * 120 + r * 40 + V(0, 0, -10), neck + V(0, 0, -18), 34.0),
            ("face", head + f * 75 + r * 18 + V(0, 0, 4), head + V(0, 0, 2), 30.0),
            ("hands", hand + f * 70 + r * 45 + V(0, 0, 10), hand, 36.0),
            ("feet", V(foot.x, foot.y, floor) + f * 120 + r * 60 + V(0, 0, 45), V(p.x, p.y, floor + 12), 38.0),
        ]
        return [(kind, n, l, t, fov) for (n, l, t, fov) in out]

    def show_only(kind):
        """На снимке — только снимаемый рефери: бойцы и второй рефери скрыты (и без коллизий)."""
        for k, a in ctx["refs"]:
            hide(a, k != kind)
        for b in ctx["boxers"]:
            hide(b, True)

    def hide(a, h):
        a.set_actor_hidden_in_game(h)
        for c in a.get_components_by_class(unreal.ChildActorComponent):
            ch = c.get_editor_property("child_actor")
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
        if st["phase"] == "wait":
            w = game_world()
            if w is None or st["frames"] < 20:
                return
            ctx["world"] = w
            ctx["refs"] = []
            ctx["boxers"] = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BoxerCharacter)
            for b in ctx["boxers"]:
                b.set_actor_enable_collision(False)
            for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "RefereeLook"):
                kind = str(a.tags[1]) if len(a.tags) > 1 else "?"
                prepare(a)
                ctx["refs"].append((kind, a))
                log("рефери %s: %s, капсула %.1f" % (kind, a.get_actor_location(),
                    a.get_editor_property("capsule_component").get_scaled_capsule_half_height()))
            if not ctx["refs"]:
                log("на уровне нет рефери (тег RefereeLook) — сначала собрать " + LEVEL)
            unreal.SystemLibrary.execute_console_command(w, "ShowHUD")
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            ctx["pc"] = pc
            st["phase"], st["frames"] = "idle", 0
            return
        if st["phase"] == "idle":
            if st["frames"] == WAIT - 5:
                ctx["cam"] = ctx["pc"].get_view_target()
                ctx["pc"].set_actor_tick_enabled(False)
            if st["frames"] < WAIT:
                return
            unreal.GameplayStatics.set_global_time_dilation(ctx["world"], 0.0001)
            for kind, a in ctx["refs"]:
                body = visual_body(a)
                if body:
                    log("%s: тело %s, anim %s" % (kind, body.get_editor_property("skeletal_mesh_asset").get_name(),
                                                  body.get_anim_instance()))
                st["queue"] += views(kind, a)
            st["phase"], st["frames"] = "shoot", 0
            return
        if st["phase"] == "shoot":
            if not st["queue"]:
                finish()
                return
            kind, name, loc, look, fov = st["queue"][0]
            if st["frames"] == 1:
                show_only(kind)
                place(loc, look, fov)
            if st["frames"] == SETTLE:
                fn = os.path.join(OUT, "%s_%s_%s.png" % (PREFIX, kind.lower(), name)).replace("\\", "/")
                unreal.SystemLibrary.execute_console_command(ctx["world"], 'HighResShot 1 filename="%s"' % fn)
                st["shot"] += 1
                log("снимок %s" % fn)
            if st["frames"] >= SETTLE + 3:
                st["queue"].pop(0)
                st["frames"] = 0

    st["h"] = unreal.register_slate_post_tick_callback(tick)


if IS_GAME:
    main()
else:
    build_level()
