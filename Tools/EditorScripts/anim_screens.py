# S-41 трек C, шаг 6: рендер-превью ретаргета — ключевые кадры (guard, контакт джеба/кросса/хуков/
# апперкотов, блок, слип) на манекене → Docs/screens/anim_<поза>_<вид>.png.
#
# Два прохода (оба — этот же файл):
#  1) РЕДАКТОР собирает пустой уровень /Game/BoxingLocal/Tmp/L_AnimPreview (вне git): свет, пол,
#     актёр с PoseableMeshComponent (Manny или UEFN), SceneCapture2D — и закрывается:
#       UnrealEditor.exe BoxingUE.uproject -ExecCmds="py <абс.путь>/anim_screens.py" -unattended -nosplash
#  2) ИГРА (-game -RenderOffscreen) грузит уровень, ставит позы и снимает:
#       UnrealEditor.exe BoxingUE.uproject /Game/BoxingLocal/Tmp/L_AnimPreview -game -RenderOffscreen
#           -ResX=960 -ResY=960 -unattended -nosound -ExecCmds="py <абс.путь>/anim_screens.py"
# Почему так: в мире редактора, запущенного из скрипта (окно в фоне), ни одиночный клип на
# SkeletalMeshComponent, ни SceneCapture не обновлялись (все снимки — одна и та же референсная поза,
# даже у родных клипов GASP и в Simulate). В -game мир и рендер тикают честно.
# Поза ставится НАПРЯМУЮ в PoseableMeshComponent: mesh-space кости клипа через AnimationLibrary
# (тот же сэмплер, что ищет кадры контакта) — без зависимости от тика анимации.
# ANIM_SCREENS_SET=UEFN — снимать UEFN-манекен (файлы anim_uefn_*), по умолчанию Manny.
import json
import os
import sys
import unreal

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from anim_common import RTG_OUT, MANNY_MESH, UEFN_MESH, BoneSampler, key_times, quit_when_idle  # noqa: E402

PROJECT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT_DIR = os.path.join(PROJECT, "Docs", "screens")
SET = os.environ.get("ANIM_SCREENS_SET", "Manny")
MESH = MANNY_MESH if SET == "Manny" else UEFN_MESH
LEVEL = "/Game/BoxingLocal/Tmp/L_AnimPreview"
W, H = 960, 960
IS_GAME = "-game" in unreal.SystemLibrary.get_command_line().lower()

# (метка, клип, время: число | "contact"); клип — имя web-клипа или полный путь ассета.
POSES = [("guard", "guard", 0.0), ("jab", "jab", "contact"), ("cross", "cross", "contact"),
         ("hookL", "hookL", "contact"), ("hookR", "hookR", "contact"), ("upperL", "upperL", "contact"),
         ("upperR", "upperR", "contact"), ("bodyHook", "bodyHook", "contact"), ("block", "block", "contact"),
         ("slipR", "slip", "contact"), ("slipL", "slipL", "contact"), ("hitHead", "hitHead", "contact"),
         ("idle", "idle", 0.0)]
if os.environ.get("ANIM_SCREENS_POSES"):  # отладка: "метка=клип@время|contact;..."
    POSES = []
    for item in os.environ["ANIM_SCREENS_POSES"].split(";"):
        lab, rest = item.split("=")
        pth, tt = rest.split("@")
        POSES.append((lab, pth, "contact" if tt == "contact" else float(tt)))
# Меш смотрит вдоль +Y. Виды: 3/4 спереди со стороны левой (передней) руки и строго сбоку справа.
VIEWS = {"front34": unreal.Vector(230, 330, 150), "side": unreal.Vector(-420, 30, 120)}
LOOK_AT = unreal.Vector(0, 25, 100)


def log(m):
    unreal.log("ANIMSCREENS " + m)


# --- Проход 1: редактор собирает уровень ------------------------------------------------------
def build_level():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    def spawn(cls, loc, rot=unreal.Rotator(0, 0, 0)):
        return eas.spawn_actor_from_class(cls, loc, rot)

    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorAssetLibrary.delete_asset(LEVEL)
    les.new_level(LEVEL)
    # Ключевой свет «из-за камеры» (доворачивается на каждый вид) + контровой + небо (ambient)
    key = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(roll=0, pitch=-35, yaw=-130))
    key.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    key.light_component.set_intensity(30.0)
    key.tags = ["key"]
    rim = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(roll=0, pitch=-30, yaw=50))
    rim.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    rim.light_component.set_intensity(12.0)
    rim.light_component.set_editor_property("cast_shadows", False)
    rim.tags = ["rim"]
    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 300))
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(4.0)
    sky.light_component.set_editor_property("lower_hemisphere_is_black", False)
    floor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    floor.set_actor_scale3d(unreal.Vector(8, 8, 1))
    grid = unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial")
    if grid:
        floor.static_mesh_component.set_material(0, grid)
    holder = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    holder.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    holder.static_mesh_component.set_static_mesh(None)
    holder.static_mesh_component.set_visibility(False, False)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = sds.k2_gather_subobject_data_for_instance(holder)[0]
    handle, _ = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root,
                                                                    new_class=unreal.PoseableMeshComponent))
    pose = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(
        unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))
    pose.set_skinned_asset_and_update(unreal.load_asset(MESH))
    holder.tags = ["boxer"]
    cap = spawn(unreal.SceneCapture2D, VIEWS["front34"])
    cc = cap.capture_component2d
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property("capture_every_frame", False)
    cc.set_editor_property("fov_angle", 45.0)
    pp = cc.get_editor_property("post_process_settings")
    pp.set_editor_property("override_auto_exposure_method", True)
    pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    pp.set_editor_property("override_auto_exposure_bias", True)
    pp.set_editor_property("auto_exposure_bias", 10.0)
    cc.set_editor_property("post_process_settings", pp)
    cc.set_editor_property("post_process_blend_weight", 1.0)
    build_jobs()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("level built: %s (%s)" % (LEVEL, MESH))
    quit_when_idle(3.0)


# --- Проход 2: игра ставит позы и снимает ------------------------------------------------------
POSE_JSON = os.path.join(PROJECT, "Saved", "AnimPreview", "poses.json")


def sample_pose(seq, t):
    """Mesh-space трансформы всех анимированных костей клипа (от корня к листьям) — для Poseable."""
    bones = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(seq)]
    s = BoneSampler(seq, bones)
    cs = s.cs(t)
    # центрируем по тазу (горизонталь): в слоте UpperBody таз ведёт Motion Matching, а горизонтальный
    # ход таза Mixamo (выпады, шаги) в бою не виден
    pel = cs.get("pelvis")
    ox, oy = (pel.translation.x, pel.translation.y) if pel else (0.0, 0.0)
    out = []
    for b in sorted(bones, key=lambda n: len(s.paths[n])):
        tr = cs[b]
        q = tr.rotation
        out.append([b, tr.translation.x - ox, tr.translation.y - oy, tr.translation.z, q.x, q.y, q.z, q.w])
    return out


def build_jobs():
    """Редактор: позы всех снимков -> Saved/AnimPreview/poses.json (в -game нет AnimationLibrary)."""
    jobs = []
    for label, clip, t in POSES:
        path = clip if clip.startswith("/") else "%s/%s/A_BX_%s" % (RTG_OUT, SET, clip)
        seq = unreal.load_asset(path)
        if seq is None:
            log("нет клипа %s" % path)
            continue
        if t == "contact":
            t = key_times(seq, clip)["t"]
        jobs.append({"label": label, "t": t, "bones": sample_pose(seq, t)})
    os.makedirs(os.path.dirname(POSE_JSON), exist_ok=True)
    with open(POSE_JSON, "w", encoding="utf-8") as f:
        json.dump({"set": SET, "jobs": jobs}, f)
    log("poses: %d -> %s" % (len(jobs), POSE_JSON))


def apply_pose(pose_comp, bones):
    for b, x, y, z, qx, qy, qz, qw in bones:
        tr = unreal.Transform(unreal.Vector(x, y, z), unreal.Quat(qx, qy, qz, qw).rotator(), unreal.Vector(1, 1, 1))
        pose_comp.set_bone_transform_by_name(b, tr, unreal.BoneSpaces.COMPONENT_SPACE)


def game_world():
    for w in unreal.ObjectIterator(unreal.World):
        try:
            if w.get_name() == LEVEL.rsplit("/", 1)[1] and unreal.GameplayStatics.get_player_controller(w, 0):
                return w
        except Exception:  # noqa
            pass
    return None


def shoot():
    os.makedirs(OUT_DIR, exist_ok=True)
    st = {"i": 0, "phase": -1, "wait": 0, "h": None}
    ctx = {}

    def finish():
        unreal.unregister_slate_post_tick_callback(st["h"])
        unreal.SystemLibrary.execute_console_command(ctx.get("world"), "quit")

    def tick(dt):
        try:
            step()
        except Exception as e:  # не виснуть при ошибке
            log("ERROR %s" % e)
            finish()

    def step():
        if st["phase"] == -1:  # дождаться мира и найти свои актёры
            st["wait"] += 1
            w = game_world()
            if w is None or st["wait"] < 30:
                return
            gs = unreal.GameplayStatics
            ctx["world"] = w
            pawn = gs.get_player_pawn(w, 0)  # DefaultPawn-сфера игрового режима — в кадре у ног
            if pawn:
                pawn.set_actor_hidden_in_game(True)
            ctx["pose"] = gs.get_all_actors_with_tag(w, "boxer")[0].get_component_by_class(unreal.PoseableMeshComponent)
            ctx["key"] = gs.get_all_actors_with_tag(w, "key")[0]
            ctx["rim"] = gs.get_all_actors_with_tag(w, "rim")[0]
            ctx["cap"] = gs.get_all_actors_of_class(w, unreal.SceneCapture2D)[0]
            ctx["rt"] = unreal.RenderingLibrary.create_render_target2d(w, W, H, unreal.TextureRenderTargetFormat.RTF_RGBA8)
            ctx["cap"].capture_component2d.set_editor_property("texture_target", ctx["rt"])
            with open(POSE_JSON, encoding="utf-8") as f:
                data = json.load(f)
            ctx["set"] = data["set"]
            ctx["jobs"] = [(j["label"], j["bones"], j["t"], v) for j in data["jobs"] for v in VIEWS]
            st["phase"], st["wait"] = 0, 0
            return
        jobs = ctx["jobs"]
        if st["i"] >= len(jobs):
            log("done %d shots -> %s" % (len(jobs), OUT_DIR))
            finish()
            return
        label, bones, t, view = jobs[st["i"]]
        cap, pose = ctx["cap"], ctx["pose"]
        if st["phase"] == 0:  # поза, камера, свет
            apply_pose(pose, bones)
            loc = VIEWS[view]
            rot = unreal.MathLibrary.find_look_at_rotation(loc, LOOK_AT)
            cap.set_actor_location_and_rotation(loc, rot, False, False)
            ctx["key"].set_actor_rotation(unreal.Rotator(roll=0, pitch=-35, yaw=rot.yaw + 30), False)
            ctx["rim"].set_actor_rotation(unreal.Rotator(roll=0, pitch=-25, yaw=rot.yaw + 200), False)
            st["phase"], st["wait"] = 1, 0
        elif st["phase"] == 1:  # пара кадров на рендер-состояние (тени, небо), затем снимок
            st["wait"] += 1
            if st["wait"] >= 4:
                cap.capture_component2d.capture_scene()
                st["phase"], st["wait"] = 2, 0
        elif st["phase"] == 2:
            st["wait"] += 1
            if st["wait"] >= 2:
                prefix = "anim_" if ctx["set"] == "Manny" else "anim_uefn_"
                name = "%s%s_%s.png" % (prefix, label, view)
                unreal.RenderingLibrary.export_render_target(ctx["world"], ctx["rt"], OUT_DIR, name)
                hl = pose.get_socket_location("hand_l")
                log("%s t=%.3f -> %s  hand_l=(%.0f,%.0f,%.0f)" % (label, t, name, hl.x, hl.y, hl.z))
                st["i"] += 1
                st["phase"] = 0

    st["h"] = unreal.register_slate_post_tick_callback(tick)


try:
    if IS_GAME:
        shoot()
    else:
        build_level()
except Exception as e:  # noqa
    log("ERROR %s" % e)
    if IS_GAME:
        unreal.SystemLibrary.execute_console_command(None, "quit")
    else:
        quit_when_idle(1.0)
