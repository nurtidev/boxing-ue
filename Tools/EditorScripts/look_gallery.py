# -*- coding: utf-8 -*-
# S-60: галерея «бойцы разные» — реальные бойцы ростера рядом, облик по Content/Boxing/Data/Appearance.json
# (эталонная функция look_apply.apply — то же, что должен делать C++). Два прохода (оба — этот файл):
#  1) КОММАНДЛЕТ собирает копию ринга /Game/BoxingLocal/Tmp/L_LookGallery (вне git; L_Ring не трогается):
#     по персонажу GASP (SandboxCharacter_CMC, тег LookGallery + индекс) на бойца, «VisualOverride» → облик
#     (профи угла или любительский — LOOK_KIT=pro|amateur):
#       UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
#  2) ИГРА: облик каждого бойца из Appearance.json, снимки ряда и портретов:
#       UnrealEditor.exe <uproject> /Game/BoxingLocal/Tmp/L_LookGallery -game -RenderOffscreen -windowed ^
#         -ResX=1920 -ResY=1080 -unattended -nosound -BoxAutopilot -BoxSeed=7 -BoxQuitAfter=400 ^
#         -ExecCmds="DisableAllScreenMessages,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/look_gallery.py"
#     (из Git Bash — MSYS_NO_PATHCONV=1)
# Файлы: Docs/screens/<LOOK_PREFIX>_row<N>_{front,34}.png, <LOOK_PREFIX>_<NN>_<фамилия>_{face,body}.png.
# Переменные: LOOK_PREFIX=look3, LOOK_FIGHTERS=<id или часть имени через «;»>, LOOK_ROW=5 (в ряду),
#   LOOK_APPLY=0 — без облика (снимки «до»), LOOK_KIT=pro|amateur (сборка уровня).
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import look_apply  # noqa: E402

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Docs", "screens")
PREFIX = os.environ.get("LOOK_PREFIX", "look3")
DEFAULT = ("amateur:Санжар Ташкенбай;amateur:Нурбек Оралбай;amateur:Ертуган Зейнулинов;legend:Геннадий Головкин;"
           "pro:Сиякхолва Кусе;amateur:Назым Кызайбай;amateur:Карина Ибрагимова;pro:Александр Усик;"
           "legend:Майк Тайсон;pro:Тайсон Фьюри")
FIGHTERS = [x for x in os.environ.get("LOOK_FIGHTERS", DEFAULT).split(";") if x]
ROW = int(os.environ.get("LOOK_ROW", "5"))
APPLY = os.environ.get("LOOK_APPLY", "1") != "0"
KIT = os.environ.get("LOOK_KIT", "pro")
SPACING = 88.0
ROW_X = 175.0
SANDBOX = "/Game/Blueprints/SandboxCharacter_CMC.SandboxCharacter_CMC_C"
LOOKS = {
    "pro": "/Game/BoxingLocal/Characters/BP_BoxerLook_Red.BP_BoxerLook_Red_C",
    "pro_f": "/Game/BoxingLocal/Characters/BP_BoxerLook_Red_Female.BP_BoxerLook_Red_Female_C",
    "amateur": "/Game/BoxingLocal/Characters/BP_BoxerLook_Red_AmateurElite.BP_BoxerLook_Red_AmateurElite_C",
    "amateur_f": "/Game/BoxingLocal/Characters/BP_BoxerLook_Red_Amateur.BP_BoxerLook_Red_Amateur_C",
}
LEVEL_SRC = "/Game/Boxing/Maps/L_Ring"
LEVEL = "/Game/BoxingLocal/Tmp/L_LookGallery"
IS_GAME = "-game" in unreal.SystemLibrary.get_command_line().lower()
SETTLE = 10


def log(m):
    unreal.log("LOOKGAL " + str(m))


def V(x, y, z):
    return unreal.Vector(x, y, z)


def spot(i):
    """Ряды по ROW бойцов вдоль Y внутри канатов: нечётный ряд у −X лицом к +X, чётный — у +X лицом к −X
    (камера каждого ряда — внутри ринга, канаты не в кадре)."""
    r, k = divmod(i, ROW)
    n = min(ROW, len(FIGHTERS) - r * ROW)
    side = -1.0 if r % 2 == 0 else 1.0
    return V(side * ROW_X, (k - (n - 1) / 2.0) * SPACING, 96.0), (0.0 if side < 0 else 180.0)


def look_class(rec):
    female = rec["gender"] == "F"
    key = KIT + ("_f" if female else "")
    path = LOOKS[key]
    if not unreal.EditorAssetLibrary.does_asset_exist(path.split(".")[0]):
        path = LOOKS[KIT]
    return path


def build_level():
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(LEVEL):
        eal.delete_asset(LEVEL)
    if not eal.duplicate_asset(LEVEL_SRC, LEVEL):
        raise RuntimeError("не скопировался " + LEVEL_SRC)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)
    cls = unreal.load_class(None, SANDBOX)
    for i, key in enumerate(FIGHTERS):
        rec = look_apply.find(key)
        if not rec:
            log("нет бойца " + key)
            continue
        loc, yaw = spot(i)
        a = eas.spawn_actor_from_class(cls, loc, unreal.Rotator(0, 0, yaw))
        a.tags = ["LookGallery", rec["id"]]
        a.set_actor_label("LookGallery_%02d" % i)
        path = look_class(rec)
        for c in a.get_components_by_class(unreal.ChildActorComponent):
            if c.get_name().startswith("VisualOverride"):
                c.set_child_actor_class(unreal.load_class(None, path))
        log("поставлен %s → %s @ %s" % (rec["id"], path, loc))
    les.save_current_level()
    log("уровень %s сохранён" % LEVEL)


def game_world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if unreal.GameplayStatics.get_player_controller(w, 0) and w.get_name() != "Untitled":
                if unreal.GameplayStatics.get_all_actors_with_tag(w, "LookGallery"):
                    return w
        except Exception:  # noqa
            pass
    return None


def child_of(a):
    for c in a.get_components_by_class(unreal.ChildActorComponent):
        if c.get_name().startswith("VisualOverride"):
            return c, c.get_editor_property("child_actor")
    return None, None


def surname(name):
    parts = name.replace("«", "").replace("»", "").split()
    return parts[-1] if parts else name


TRANSLIT = dict(zip("абвгдеёжзийклмнопрстуфхцчшщъыьэюяәғқңөұүһі",
                    ["a", "b", "v", "g", "d", "e", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p", "r", "s",
                     "t", "u", "f", "kh", "ts", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya", "a", "g", "k", "n",
                     "o", "u", "u", "h", "i"]))


def latin(s):
    return "".join(TRANSLIT.get(ch, ch) for ch in s.lower() if ch.isalnum() or ch in TRANSLIT)


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

    def hide(a, h):
        a.set_actor_hidden_in_game(h)
        _, ch = child_of(a)
        if ch:
            ch.set_actor_hidden_in_game(h)

    def body_of(a):
        _, ch = child_of(a)
        if ch:
            for sk in ch.get_components_by_class(unreal.SkeletalMeshComponent):
                if sk.get_name().startswith("Body"):
                    return sk
        return None

    def queue_shots():
        g = ctx["gal"]
        rows = (len(g) + ROW - 1) // ROW
        for r in range(rows):
            members = g[r * ROW:(r + 1) * ROW]
            p0 = members[0][1].get_actor_location()
            fw = members[0][1].get_actor_forward_vector()
            rt = members[0][1].get_actor_right_vector()
            floor = p0.z - 96.0
            cy = sum(a.get_actor_location().y for _, a in members) / len(members)
            mid = V(p0.x, cy, floor + 98)
            dist = 2 * ROW_X + 70.0
            st["queue"].append(("row%d_front" % (r + 1), [a for _, a in members], mid + fw * dist + V(0, 0, 12), mid,
                                62.0))
            st["queue"].append(("row%d_34" % (r + 1), [a for _, a in members],
                                mid + fw * (dist * 0.85) + rt * 150 + V(0, 0, 45), mid + V(0, 0, -5), 62.0))
        for i, (rec, a) in enumerate(g):
            if os.environ.get("LOOK_PORTRAITS", "1") == "0":
                break
            b = body_of(a)
            head = b.get_socket_location("head") if b else a.get_actor_location() + V(0, 0, 70)
            p = a.get_actor_location()
            fw = a.get_actor_forward_vector()
            rt = a.get_actor_right_vector()
            mid = V(p.x, p.y, p.z - 96 + 95 * look_scale(rec))
            tag = "%02d_%s" % (i + 1, latin(surname(rec["name"])) or "x")
            st["queue"].append((tag + "_face", [a], head + fw * 62 + rt * 22 + V(0, 0, 6), head + V(0, 0, 3), 32.0))
            st["queue"].append((tag + "_back", [a], head - fw * 60 + rt * 45 + V(0, 0, 8), head + V(0, 0, -4), 36.0))
            st["queue"].append((tag + "_body", [a], mid + fw * 250 + rt * 70 + V(0, 0, 20), mid, 48.0))
            if os.environ.get("LOOK_FEET") == "1":
                foot = V(p.x, p.y, p.z - 96 + 10)
                st["queue"].append((tag + "_feet", [a], foot + fw * 90 + rt * 40 + V(0, 0, 25), foot, 40.0))

    def look_scale(rec):
        return float(rec["look"]["scale"]) if APPLY else 1.0

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
            if w is None or st["frames"] < 30:
                return
            ctx["world"] = w
            for b in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Character):
                if "LookGallery" in [str(t) for t in b.tags]:
                    continue
                b.set_actor_enable_collision(False)         # бойцы ринга и рефери — не в кадре
                hide(b, True)
                b.set_actor_location(V(0, 0, -3000), False, True)
            gal = []
            for a in unreal.GameplayStatics.get_all_actors_with_tag(w, "LookGallery"):
                rec = look_apply.find(str(a.tags[1])) if len(a.tags) > 1 else None
                mesh = a.get_editor_property("mesh")
                mesh.set_editor_property("visibility_based_anim_tick_option",
                                         unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
                mesh.set_visibility(False, False)
                if rec:
                    gal.append((rec, a))
            gal.sort(key=lambda x: x[1].get_actor_label() if hasattr(x[1], "get_actor_label") else "")
            gal.sort(key=lambda x: FIGHTERS.index(x[0]["id"]) if x[0]["id"] in FIGHTERS else 99)
            ctx["gal"] = gal
            unreal.SystemLibrary.execute_console_command(w, "ShowHUD")
            ctx["pc"] = unreal.GameplayStatics.get_player_controller(w, 0)
            st["phase"], st["frames"] = "apply", 0
            return
        if st["phase"] == "apply":
            if st["frames"] < 20:
                return
            for rec, a in ctx["gal"]:
                comp, ch = child_of(a)
                if not ch:
                    log("нет визуала у " + rec["id"])
                    continue
                if APPLY:
                    look_apply.apply(ch, rec["look"], comp)
                b = body_of(a)
                if b and st.get("morph_logged") is None:
                    st["morph_logged"] = True
                    try:
                        log("морфы тела: %s" % b.get_editor_property("skeletal_mesh_asset").get_all_morph_target_names())
                    except Exception as e:  # noqa
                        log("морфы ? %s" % e)
                log("%s: %s %s" % (rec["id"], rec["appearance"], rec["look"]["morph"]))
            st["phase"], st["frames"] = "idle", 0
            return
        if st["phase"] == "idle":
            if st["frames"] == 50:
                ctx["cam"] = ctx["pc"].get_view_target()
                ctx["pc"].set_actor_tick_enabled(False)
            if st["frames"] < 60:
                return
            unreal.GameplayStatics.set_global_time_dilation(ctx["world"], 0.0001)
            if os.environ.get("LOOK_DEBUG_GROOM") == "1":
                _, ch = child_of(ctx["gal"][0][1])
                for g in ch.get_components_by_class(unreal.GroomComponent):
                    if g.get_name() == "Hair":
                        log("DBG groom api %s" % [x for x in dir(g) if "lod" in x.lower() or "force" in x.lower()])
                        try:
                            g.set_forced_lod(0)
                        except Exception as e:  # noqa
                            log("DBG forced ? %s" % e)
                for c in ch.get_components_by_class(unreal.ActorComponent):
                    if "LODSync" in c.get_name():
                        log("DBG lodsync %s %s" % (c.get_name(), [x for x in dir(c) if not x.startswith("_")][:60]))
                        c.set_component_tick_enabled(False)
            if os.environ.get("LOOK_DEBUG") == "1":
                for rec, a in ctx["gal"][:3]:
                    _, ch = child_of(a)
                    for g in ch.get_components_by_class(unreal.GroomComponent):
                        ga = g.get_editor_property("groom_asset")
                        par = g.get_attach_parent()
                        log("DBG %s groom %s asset=%s vis=%s parent=%s socket=%s world=%s rel=%s" % (
                            rec["id"], g.get_name(), ga.get_name() if ga else None, g.is_visible(),
                            par.get_name() if par else None, g.get_attach_socket_name(),
                            g.get_world_location(), (g.get_relative_transform().translation, g.get_relative_transform().rotation, g.get_relative_transform().rotation.rotator(), unreal.SystemLibrary.get_component_bounds(g))))
                    fc = [x for x in ch.get_components_by_class(unreal.SkeletalMeshComponent) if x.get_name() == "Face"]
                    hg = [x for x in ch.get_components_by_class(unreal.GroomComponent)
                          if x.get_name() == os.environ.get("LOOK_DEBUG_COMP", "Hair")]
                    if fc and hg and os.environ.get("LOOK_DEBUG_IDENT") == "1":
                        hg[0].attach_to_component(fc[0], "", unreal.AttachmentRule.KEEP_RELATIVE,
                                                  unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE, False)
                        hg[0].set_relative_transform(unreal.Transform(), False, False)
                        log("DBG IDENT face world %s groom bounds %s" % (fc[0].get_world_location(),
                                                                        unreal.SystemLibrary.get_component_bounds(hg[0])))
                    if fc:
                        log("DBG face head socket world %s" % fc[0].get_socket_location("head"))
                    for sk in ch.get_components_by_class(unreal.SkeletalMeshComponent):
                        mats = [sk.get_material(i).get_name() if sk.get_material(i) else None
                                for i in range(sk.get_num_materials())]
                        lp = sk.get_editor_property("leader_pose_component")
                        log("DBG %s %s mesh=%s leader=%s mats=%s" % (rec["id"], sk.get_name(),
                            sk.get_editor_property("skeletal_mesh_asset").get_name() if sk.get_editor_property("skeletal_mesh_asset") else None,
                            lp.get_name() if lp else None, mats))
            queue_shots()
            st["phase"], st["frames"] = "shoot", 0
            return
        if st["phase"] == "shoot":
            if not st["queue"]:
                finish()
                return
            name, show, loc, look, fov = st["queue"][0]
            if st["frames"] == 1:
                for _, a in ctx["gal"]:
                    hide(a, a not in show)
                place(loc, look, fov)
            if st["frames"] == SETTLE:
                fn = os.path.join(OUT, "%s_%s.png" % (PREFIX, name)).replace("\\", "/")
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
