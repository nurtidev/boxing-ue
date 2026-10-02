# -*- coding: utf-8 -*-
# S-41 (облик боксёра): снимки формы крупным планом В НАСТОЯЩЕМ БОЮ (-game, автопилот, Lumen).
# Работает в игре, а не в редакторе: -ExecCmds="py <этот файл>".
#
#   UnrealEditor.exe <uproject> /Game/Boxing/Maps/L_Ring -game -RenderOffscreen -windowed -ResX=1600 -ResY=900 ^
#     -unattended -nosound -BoxAutopilot -BoxSeed=7 -BoxQuitAfter=150 ^
#     -BoxVisual=/Game/BoxingLocal/Characters/BP_BoxerLook_Red.BP_BoxerLook_Red_C ^
#     -ExecCmds="DisableAllScreenMessages,py C:/Users/user/Desktop/boxing-ue/Tools/EditorScripts/look_shots.py"
#
# Переменные окружения:
#   LOOK_PREFIX=look        — файлы Docs/screens/<prefix>_<NN>_<событие>_<ракурс>.png
#   LOOK_BLUE=<класс>        — ПОДМЕНА визуала синего бойца (индекс 1) на лету: пока в GameMode один
#                              VisualOverridePath на обоих, синий угол ставится этим скриптом (только для проверки;
#                              в игре — отдельный параметр GameMode, см. отчёт S-41 look).
#   LOOK_MAX=40             — сколько снимков максимум
#   LOOK_HUD=0              — спрятать HUD
# Как снимает: на событии (контакт удара — фаза клипа 0.5, блок, приём в блок, уклон, попадание, нокдаун, подъём,
# финал, стойка) время мира замирает (global time dilation ≈ 0), камера боя (FightCamera контроллера; тик
# контроллера выключен) ставится в 2–3 ракурса крупного плана, на каждый — несколько кадров (Lumen/TAA сходятся) и
# HighResShot. Потом время идёт дальше.
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Docs", "screens")
PREFIX = os.environ.get("LOOK_PREFIX", "look")
BLUE = os.environ.get("LOOK_BLUE", "")
MAX_SHOTS = int(os.environ.get("LOOK_MAX", "40"))
HIDE_HUD = os.environ.get("LOOK_HUD", "1") == "0"
SETTLE = 6            # кадров на сходимость после переезда камеры
SLOT_NAMES = ["None", "Punch", "Block", "BlockHit", "Slip", "Hit", "Knockdown", "GetUp", "Finale", "Guard"]
PUNCH_NAMES = ["Jab", "Cross", "HookL", "HookR", "UpperL", "UpperR"]
# сколько раз снимать событие каждого вида (на бойца)
QUOTA = {"Guard": 1, "Block": 1, "BlockHit": 1, "Slip": 1, "Hit": 2, "Knockdown": 1, "GetUp": 1, "Finale": 1}
PUNCH_QUOTA = 1       # каждого типа удара на бойца
# Галерея поз (автопилот не блокирует, не уклоняется и редко роняет): LOOK_POSES="Block:0.45,SlipL:0.35,..."
# — монтаж трека C ставится на логический меш бойца в доле длины (end — последний кадр), скорость 0,
# через POSE_WAIT кадров бленда время замирает и идут снимки. Бойцы чередуются.
POSES = [x.split(":") for x in os.environ.get("LOOK_POSES", "").split(",") if x]
MONTAGE_DIR = "/Game/BoxingLocal/Anim"
POSE_WAIT = 30
EVENTS = os.environ.get("LOOK_EVENTS", "1") == "1"


def log(m):
    unreal.log("LOOKSHOT " + str(m))


def prop(obj, *names):
    for n in names:
        try:
            return obj.get_editor_property(n)
        except Exception:  # noqa
            pass
    return None


def enum_index(v):
    if v is None:
        return 0
    try:
        return int(v.value)
    except Exception:  # noqa
        try:
            return int(v)
        except Exception:  # noqa
            return 0


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


def visual_body(boxer):
    """Видимый меш тела (Body child actor'а визуала) — для сокетов кистей/головы."""
    for c in boxer.get_components_by_class(unreal.ChildActorComponent):
        a = c.get_editor_property("child_actor")
        if a:
            for sk in a.get_components_by_class(unreal.SkeletalMeshComponent):
                if sk.get_name().startswith("Body"):
                    return sk
    return boxer.get_components_by_class(unreal.SkeletalMeshComponent)[0]


BLUE_CC = unreal.LinearColor(*[float(x) for x in os.environ.get("LOOK_BLUE_SKIN", "1.6,1.5,1.4,1").split(",")])
BODY_MATCH = [float(x) for x in os.environ.get("LOOK_BODY_MATCH", "0.85,0.82,0.74").split(",")]
SKIN_BLUE = {
    "MI_BodySynthesized_Simplified": "MI_Skin_Body_Blue",
    "MI_Skin_Body_Red": "MI_Skin_Body_Blue",
    "MI_HeadSynthesized_Simplified_LOD1": "MI_Skin_FaceLOD1_Blue",
    "MI_HeadSynthesized_Simplified_LOD3": "MI_Skin_FaceLOD3_Blue",
    "MI_HeadSynthesized_Simplified_LOD5": "MI_Skin_FaceLOD57_Blue",
}


def recolor_blue(boxer):
    """LOOK_BLUE=recolor: синий угол перекрашивается на месте (материалы BP_BoxerLook_Blue, без причёски) —
    child actor тот же, процедурный слой game-feel остаётся. Только для проверки, пока у GameMode один визуал."""
    n = 0
    for c in boxer.get_components_by_class(unreal.ChildActorComponent):
        a = c.get_editor_property("child_actor")
        if not a:
            continue
        for sk in a.get_components_by_class(unreal.SkeletalMeshComponent):
            for i in range(sk.get_num_materials()):
                m = sk.get_material(i)
                if not m:
                    continue
                nm = m.get_name()
                if isinstance(m, unreal.MaterialInstanceDynamic) and ("Body" in nm):
                    m.set_vector_parameter_value("BaseColor_ColorCorrect", unreal.LinearColor(
                        BLUE_CC.r * BODY_MATCH[0], BLUE_CC.g * BODY_MATCH[1], BLUE_CC.b * BODY_MATCH[2], 1.0))
                    n += 1
                    continue
                if isinstance(m, unreal.MaterialInstanceDynamic) and ("HeadSynthesized" in nm or "Skin_Face" in nm):
                    # лицо: MID морщин (создаёт BP/AnimBP лица) — тон кожи ставим прямо в него
                    m.set_vector_parameter_value("BaseColor_ColorCorrect", BLUE_CC)
                    n += 1
                    continue
                path = None
                if nm in SKIN_BLUE:
                    path = "/Game/BoxingLocal/Characters/" + SKIN_BLUE[nm]
                elif nm.endswith("_Red"):
                    path = "/Game/Boxing/Characters/Materials/%s_Blue" % nm[:-4]
                nm2 = unreal.load_asset(path) if path else None
                if nm2:
                    sk.set_material(i, nm2)
                    n += 1
        for g in a.get_components_by_class(unreal.GroomComponent):
            if g.get_name().startswith("Hair"):
                g.set_visibility(False)
    log("синий угол перекрашен: %d материалов" % n)


def main():
    os.makedirs(OUT, exist_ok=True)
    st = {"phase": "wait", "n": 0, "frames": 0, "h": None, "queue": [], "count": {}, "prev": {}, "swapped": False,
          "t": 0.0, "shot": 0}
    ctx = {}

    def finish():
        unreal.unregister_slate_post_tick_callback(st["h"])
        log("готово: %d снимков" % st["shot"])
        unreal.SystemLibrary.execute_console_command(ctx.get("world"), "quit")

    def dilate(on):
        unreal.GameplayStatics.set_global_time_dilation(ctx["world"], 0.0001 if on else 1.0)

    def place(cam_loc, look_at, fov=40.0):
        cam = ctx["cam"]
        rot = unreal.MathLibrary.find_look_at_rotation(cam_loc, look_at)
        cam.set_actor_location_and_rotation(cam_loc, rot, False, False)
        cc = cam.get_component_by_class(unreal.CameraComponent)
        if cc:
            cc.set_editor_property("field_of_view", fov)

    def views(kind, who):
        """Ракурсы крупного плана: [(имя, камера, цель, fov)]."""
        b = ctx["boxers"]
        me, foe = b[who], b[1 - who]
        p, q = me.get_actor_location(), foe.get_actor_location()
        p.z = 0.0                      # высоты ракурсов — от канваса (Z = 0), не от центра капсулы
        q.z = 0.0
        u = q - p
        u.z = 0
        u = u.normal()
        v = V(-u.y, u.x, 0)
        mid = (p + q) * 0.5
        mid.z = 0
        body = visual_body(me)
        hl = body.get_socket_location("hand_l")
        hr = body.get_socket_location("hand_r")
        head = body.get_socket_location("head")
        out = []
        # сбоку от пары, по плечо — оба бойца целиком выше колен
        side = 1.0 if (st["shot"] // 3) % 2 == 0 else -1.0
        out.append(("side", mid + v * (side * 260) + V(0, 0, 130), mid + V(0, 0, 115), 42.0))
        # из-за плеча соперника — лицом к бойцу (перчатки/торс/трусы спереди)
        out.append(("front", q + u * 150 + v * (side * 75) + V(0, 0, 150), p + V(0, 0, 115), 44.0))
        # перчатки крупно: между кистями
        hm = (hl + hr) * 0.5
        if kind.startswith("Punch"):
            hm = hr if kind.endswith("_r") else hl
        out.append(("gloves", hm + v * (side * 95) + u * (-30) + V(0, 0, 25), hm, 38.0))
        if os.environ.get("LOOK_CHEST") == "1":
            # торс/шея крупно: со стороны соперника, сбоку от линии боя
            out.append(("chest", p + u * 95 + v * (side * 55) + V(0, 0, 150), head * 0.5 + (p + V(0, 0, 120)) * 0.5, 40.0))
        if kind.replace("Pose-", "") in ("Knockdown", "Finale", "GetUp", "Victory", "Defeat"):
            out.append(("low", mid + v * (-side * 300) + u * 60 + V(0, 0, 60), p + V(0, 0, 60), 45.0))
        return out

    def enqueue(kind, who):
        if st["shot"] + 3 > MAX_SHOTS:
            return
        st["queue"] = [(kind, who, name, loc, look, fov) for (name, loc, look, fov) in views(kind, who)]
        st["n"] += 1
        dilate(True)
        st["phase"] = "shoot"
        st["frames"] = 0
        log("событие %d: %s боец %d" % (st["n"], kind, who))

    def poll_events():
        for who, b in enumerate(ctx["boxers"]):
            slot = SLOT_NAMES[enum_index(prop(b, "active_montage_slot", "ActiveMontageSlot"))]
            ph = prop(b, "punch_phase_anim", "PunchPhaseAnim") or 0.0
            punching = bool(prop(b, "punching", "b_punching", "bPunching"))
            pk = "ph%d" % who
            sk = "slot%d" % who
            prev_ph = st["prev"].get(pk, 0.0)
            prev_slot = st["prev"].get(sk, "None")
            st["prev"][pk] = ph if punching else 0.0
            st["prev"][sk] = slot
            if punching and prev_ph < 0.5 <= ph:
                pt = PUNCH_NAMES[enum_index(prop(b, "current_punch", "CurrentPunch"))]
                key = "%s_%d" % (pt, who)
                if st["count"].get(key, 0) < PUNCH_QUOTA:
                    st["count"][key] = st["count"].get(key, 0) + 1
                    arm = "_r" if pt in ("Cross", "HookR", "UpperR") else "_l"
                    enqueue("Punch-%s%s" % (pt, arm), who)
                    return
            if slot != prev_slot and slot in QUOTA:
                key = "%s_%d" % (slot, who)
                if st["count"].get(key, 0) < QUOTA[slot]:
                    st["count"][key] = st["count"].get(key, 0) + 1
                    ctx["pending"] = (slot, who, 6 if slot != "Guard" else 30)   # дать позе развиться
                    return

    def assert_pose():
        if not ctx.get("pose"):
            return
        name, who, m, t = ctx["pose"]
        ai = ctx["boxers"][who].get_editor_property("mesh").get_anim_instance()
        if not ai.montage_is_playing(m):   # перезапуск — только если бой снял монтаж (иначе бленд не дойдёт)
            ai.montage_play(m, 1.0, unreal.MontagePlayReturnType.MONTAGE_LENGTH, t, True)
            ai.montage_set_play_rate(m, 0.0)

    def start_pose():
        i = st.get("pose_i", 0)
        st["pose_i"] = i + 1
        name, when = POSES[i]
        who = i % 2
        m = unreal.load_asset("%s/AM_%s" % (MONTAGE_DIR, name))
        if not m:
            log("нет монтажа AM_%s" % name)
            return
        ln = m.get_play_length()
        t = max(0.0, ln - 0.05) if when == "end" else float(when) * ln
        for i, b in enumerate(ctx["boxers"]):      # стойка-монтаж не перебивает позу (и слой верха тоже)
            for n in ("play_guard_montage", "bPlayGuardMontage"):
                try:
                    b.set_editor_property(n, i != who)
                    break
                except Exception:  # noqa
                    pass
        ctx["pose"] = (name, who, m, t)
        assert_pose()
        st["phase"], st["frames"] = "pose", 0
        log("поза %s (%.2f из %.2f с) боец %d" % (name, t, ln, who))

    def tick(dt):
        try:
            step(dt)
        except Exception as e:  # noqa
            log("ERROR %s" % e)
            finish()

    def step(dt):
        st["t"] += dt
        if st["phase"] == "wait":
            w = game_world()
            if w is None:
                return
            st["frames"] += 1
            if st["frames"] < 20:
                return
            ctx["world"] = w
            boxers = sorted(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BoxerCharacter),
                            key=lambda b: prop(b, "fighter_index", "FighterIndex") or 0)
            ctx["boxers"] = boxers
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            ctx["pc"] = pc
            if BLUE and not st["swapped"]:
                if BLUE == "recolor":
                    recolor_blue(boxers[1])
                else:
                    # подмена класса: новый child actor — БЕЗ процедурного слоя game-feel (он цепляется к визуалу
                    # на старте боя); так монтажи галереи поз видны как есть. LOOK_RED=<класс> — то же для красного.
                    for idx, path in ((1, BLUE), (0, os.environ.get("LOOK_RED", ""))):
                        if not path:
                            continue
                        cls = unreal.load_class(None, path)
                        for c in boxers[idx].get_components_by_class(unreal.ChildActorComponent):
                            c.set_child_actor_class(cls)
                        log("визуал бойца %d → %s" % (idx, path))
                st["swapped"] = True
            # S-60: облик по данным бойца (Appearance.json) на визуал боя — LOOK_RED_ID / LOOK_BLUE_ID (id или часть имени)
            for idx, key in ((0, os.environ.get("LOOK_RED_ID", "")), (1, os.environ.get("LOOK_BLUE_ID", ""))):
                if not key:
                    continue
                import sys as _sys
                _sys.path.insert(0, os.path.join(PROJECT, "Tools", "EditorScripts"))
                import look_apply
                rec = look_apply.find(key)
                for c in boxers[idx].get_components_by_class(unreal.ChildActorComponent):
                    ch = c.get_editor_property("child_actor")
                    if rec and ch:
                        look_apply.apply(ch, rec["look"], c)
                        log("облик бойца %d → %s" % (idx, rec["id"]))
            if os.environ.get("LOOK_FIGHTCAM") == "1":
                # только перекраска: снимки делает сам GameMode (-BoxHitShots/-BoxShots) камерой боя
                unreal.unregister_slate_post_tick_callback(st["h"])
                log("режим камеры боя: скрипт отключился")
                return
            if HIDE_HUD:
                unreal.SystemLibrary.execute_console_command(w, "ShowHUD")
            st["phase"], st["frames"] = "run", 0
            return
        if st["phase"] == "run":
            st["frames"] += 1
            if st["frames"] == 90:            # фокус камеры отдаём себе (контроллер боя больше её не двигает)
                ctx["cam"] = ctx["pc"].get_view_target()
                ctx["pc"].set_actor_tick_enabled(False)
                log("камера %s" % ctx["cam"].get_name())
            if st["frames"] < 100:
                return
            if st["shot"] >= MAX_SHOTS:
                finish()
                return
            pend = ctx.get("pending")
            if pend:
                kind, who, wait = pend
                if wait <= 0:
                    ctx["pending"] = None
                    enqueue(kind, who)
                else:
                    ctx["pending"] = (kind, who, wait - 1)
                return
            if POSES and st.get("pose_i", 0) < len(POSES):
                start_pose()
                return
            if POSES and not EVENTS:
                finish()
                return
            poll_events()
            return
        if st["phase"] == "pose":
            st["frames"] += 1
            name, who, m, t = ctx["pose"]
            assert_pose()                                  # бой перебил своим монтажом — ставим снова
            if st["frames"] >= POSE_WAIT:
                st["phase"] = "run"
                enqueue("Pose-" + name, who)
            return
        if st["phase"] == "shoot":
            if not st["queue"]:
                if ctx.get("pose"):
                    name, who, m, t = ctx["pose"]
                    ctx["boxers"][who].get_editor_property("mesh").get_anim_instance().montage_stop(0.2, m)
                    ctx["pose"] = None
                dilate(False)
                st["phase"], st["frames"] = "run", 100
                return
            assert_pose()
            kind, who, name, loc, look, fov = st["queue"][0]
            if st["frames"] == 0:
                place(loc, look, fov)
            st["frames"] += 1
            if st["frames"] == SETTLE:
                st["shot"] += 1
                fn = os.path.join(OUT, "%s_%02d_%s_%s_%s.png" % (PREFIX, st["n"], kind.replace("-", "_"),
                                                                ("red", "blue")[who], name)).replace("\\", "/")
                unreal.SystemLibrary.execute_console_command(ctx["world"], 'HighResShot 1 filename="%s"' % fn)
                log("снимок %s" % fn)
            if st["frames"] >= SETTLE + 3:
                st["queue"].pop(0)
                st["frames"] = 0

    st["h"] = unreal.register_slate_post_tick_callback(tick)


main()
