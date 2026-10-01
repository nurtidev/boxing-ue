# S-41 трек C, шаг 3: монтажи ударов/реакций + кадры контакта.
#  1) Зеркальный слип: A_BX_slipL из A_BX_slip (клип Mixamo уходит головой только ВПРАВО). Зеркалим
#     ТОЛЬКО осевую цепочку (root/pelvis/spine/neck/head) относительно сагиттальной плоскости (X → −X),
#     руки/ноги остаются из оригинала: боец в той же ортодоксальной стойке, а корпус уходит влево
#     (полное зеркало перевернуло бы стойку в левшу посреди боя).
#  2) Монтажи AM_* на скелетах UEFN (логика GASP: SandboxCharacter_CMC_ABP + Motion Matching) и Manny.
#     Слот задаётся при создании: Python не даёт править SlotAnimTracks у ассета, поэтому монтаж
#     собирается как динамический (CreateSlotAnimationAsDynamicMontage — любой слот) и дублируется
#     в ассет; длина секции пересчитывается при перезагрузке пакета.
#       UpperBody   — удары/блок/слипы/реакции/стойка (поверх ног Motion Matching, см. Docs/ANIM_SETUP.md)
#       DefaultSlot — полнотелые (нокдаун, подъём, победа…): этот слот УЖЕ есть в AnimBP GASP.
#  3) Кадр контакта (как keyTimes в web): AnimNotify_PlayMontageNotify с именем «Contact» (удары),
#     «GuardUp» (блок), «Peak» (слипы/реакции) → UAnimInstance::OnPlayMontageNotifyBegin в C++.
#     Таблица → Docs/ANIM_CONTACTS.md.
# Монтажи ссылаются на клипы из BoxingLocal (производные Mixamo) → по умолчанию тоже в BoxingLocal
# (ANIM_MONTAGE_DIR — переопределить корень, напр. /Game/Boxing/Anim).
# Запуск: UnrealEditor-Cmd.exe BoxingUE.uproject -run=pythonscript -script=<абс.путь>/anim_montages.py -unattended -nosplash -nullrhi
import json
import os
import sys
import unreal

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from anim_common import RTG_OUT, BoneSampler, key_times  # noqa: E402

PROJECT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ROOT = os.environ.get("ANIM_MONTAGE_DIR", "/Game/BoxingLocal/Anim")
SETS = {  # набор: (папка клипов, папка монтажей, IK Rig со всей референсной позой)
    "UEFN": (RTG_OUT + "/UEFN", ROOT, "/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin"),
    "Manny": (RTG_OUT + "/Manny", ROOT + "/Manny", "/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget"),
}
UB, FB = "UpperBody", "DefaultSlot"
# монтаж, клип, слот, бленд in/out (с), петля
MONTAGES = [
    ("AM_Jab", "jab", UB, 0.06, 0.15, 1), ("AM_Cross", "cross", UB, 0.06, 0.15, 1),
    ("AM_HookL", "hookL", UB, 0.06, 0.15, 1), ("AM_HookR", "hookR", UB, 0.06, 0.15, 1),
    ("AM_UpperL", "upperL", UB, 0.06, 0.15, 1), ("AM_UpperR", "upperR", UB, 0.06, 0.15, 1),
    ("AM_BodyHook", "bodyHook", UB, 0.06, 0.15, 1),
    ("AM_Block", "block", UB, 0.08, 0.2, 1), ("AM_BlockHit", "blockHit", UB, 0.05, 0.15, 1),
    ("AM_SlipL", "slipL", UB, 0.05, 0.15, 1), ("AM_SlipR", "slip", UB, 0.05, 0.15, 1),
    ("AM_HitHead", "hitHead", UB, 0.03, 0.2, 1), ("AM_HitBody", "hitBody", UB, 0.03, 0.2, 1),
    ("AM_Guard", "guard", UB, 0.2, 0.2, 1000),   # стойка-оверлей: «вечная» петля, гасить Montage_Stop
    ("AM_Knockdown", "knockdown", FB, 0.05, 0.3, 1), ("AM_Knockout", "knockout", FB, 0.05, 0.3, 1),
    ("AM_GetUp", "getUp", FB, 0.1, 0.3, 1), ("AM_Victory", "victory", FB, 0.25, 0.3, 1),
    ("AM_Defeat", "defeat", FB, 0.25, 0.3, 1),
]
NOTIFY_NAME = {"punch": "Contact", "guard_up": "GuardUp", "head_peak": "Peak"}
AXIAL = ["root", "pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02", "head"]

AL = unreal.AnimationLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(m):
    unreal.log("ANIMMONT " + m)


# --- кватернионы (x, y, z, w), произведение Гамильтона = порядок FQuat: q1*q2 — сперва q2 ------------
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def qrot(q, v):
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qinv(q))
    return (p[0], p[1], p[2])


def qt(q):
    return (q.x, q.y, q.z, q.w)


# --- 1) зеркальный слип ------------------------------------------------------------------------
def mirror_axial(clips_dir, ik_rig_path, src_name="slip", dst_name="slipL"):
    src_path = "%s/A_BX_%s" % (clips_dir, src_name)
    dst_path = "%s/A_BX_%s" % (clips_dir, dst_name)
    src = unreal.load_asset(src_path)
    if eal.does_asset_exist(dst_path):
        eal.delete_asset(dst_path)
    dst = eal.duplicate_asset(src_path, dst_path)
    rig = unreal.IKRigController.get_controller(unreal.load_asset(ik_rig_path))
    bones = [b for b in AXIAL if AL.does_bone_name_exist(src, b)]
    ref = {b: qt(rig.get_ref_pose_transform_of_bone(b).rotation) for b in bones}
    nkeys = AL.get_num_keys(dst)
    ln = AL.get_sequence_length(src)
    sampler = BoneSampler(src, bones)
    pos = {b: [] for b in bones}
    rot = {b: [] for b in bones}
    for k in range(nkeys):
        t = min(ln, ln * k / max(1, nkeys - 1))
        cs = sampler.cs(t)
        loc = dict(zip(bones, AL.get_bone_poses_for_time(src, bones, t, False)))
        m_rot, m_pos = {}, {}
        for b in bones:
            R = qt(cs[b].rotation)
            D = qmul(R, qinv(ref[b]))            # дельта от референса в mesh-space
            Dm = (D[0], -D[1], -D[2], D[3])      # отражение X → −X
            m_rot[b] = qmul(Dm, ref[b])
            p = cs[b].translation
            m_pos[b] = (-p.x, p.y, p.z)
        for i, b in enumerate(bones):
            parent = bones[i - 1] if i > 0 else None
            if parent is None:
                lr, lp = m_rot[b], m_pos[b]
            else:
                lr = qmul(qinv(m_rot[parent]), m_rot[b])
                if b == "pelvis":
                    d = tuple(m_pos[b][j] - m_pos[parent][j] for j in range(3))
                    lp = qrot(qinv(m_rot[parent]), d)
                else:  # длина кости — из оригинала
                    tl = loc[b].translation
                    lp = (tl.x, tl.y, tl.z)
            rot[b].append(unreal.Quat(*lr))
            pos[b].append(unreal.Vector(*lp))
    ctrl = dst.controller
    ctrl.open_bracket("Mirror axial chain", False)
    for b in bones:
        ctrl.set_bone_track_keys(b, pos[b], rot[b], [unreal.Vector(1, 1, 1)] * nkeys, False)
    ctrl.close_bracket(False)
    eal.save_loaded_asset(dst)
    log("mirrored %s -> %s (%d keys, bones %s)" % (src_path, dst_path, nkeys, bones))


# --- 2) монтажи ----------------------------------------------------------------------------------
def blend(t):
    s = unreal.MontageBlendSettings()
    b = s.get_editor_property("blend")
    b.set_editor_property("blend_time", t)
    s.set_editor_property("blend", b)
    return s


def create_montage(clips_dir, dst_dir, name, clip, slot, bin_, bout, loops):
    seq = unreal.load_asset("%s/A_BX_%s" % (clips_dir, clip))
    if seq is None:
        log("ERROR нет клипа %s/%s" % (clips_dir, clip))
        return None
    path = "%s/%s" % (dst_dir, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    dyn = unreal.AnimMontage.create_slot_animation_as_dynamic_montage_with_blend_settings(
        seq, slot, blend(bin_), blend(bout), 1.0, loops, -1.0)
    m = tools.duplicate_asset(name, dst_dir, dyn)
    eal.save_loaded_asset(m)
    return path


def reload(paths):
    pkgs = [unreal.load_asset(p).get_outermost() for p in paths]
    unreal.EditorLoadingAndSavingUtils.reload_packages(pkgs)


def add_notify(montage, kind, t):
    name = NOTIFY_NAME[kind]
    track = "Contact" if kind == "punch" else "Keys"
    if not AL.is_valid_anim_notify_track_name(montage, track):
        AL.add_animation_notify_track(montage, track)
    AL.remove_animation_notify_events_by_name(montage, name)
    n = AL.add_animation_notify_event(montage, track, t, unreal.AnimNotify_PlayMontageNotify)
    if n is None:
        log("ERROR notify %s @%.3f не добавлен в %s" % (name, t, montage.get_name()))
        return
    n.set_editor_property("notify_name", name)


def main():
    rows = []
    for tag, (clips_dir, dst_dir, rig) in SETS.items():
        mirror_axial(clips_dir, rig)
        if not eal.does_directory_exist(dst_dir):
            eal.make_directory(dst_dir)
        made = []
        for name, clip, slot, bin_, bout, loops in MONTAGES:
            p = create_montage(clips_dir, dst_dir, name, clip, slot, bin_, bout, loops)
            if p:
                made.append((p, name, clip, slot, loops))
        reload([p for p, *_ in made])  # после перезагрузки у дубликата правильная длина секции
        for p, name, clip, slot, loops in made:
            m = unreal.load_asset(p)
            seq = unreal.load_asset("%s/A_BX_%s" % (clips_dir, clip))
            kt = key_times(seq, "slip" if clip == "slipL" else clip)
            if clip != "guard":
                add_notify(m, kt["kind"], kt["t"])
            eal.save_loaded_asset(m)
            slots = [str(s) for s in AL.get_montage_slot_names(m)]
            log("%s %s slot=%s len=%.3f key=%s@%.3f" % (tag, name, slots, m.get_play_length(), kt["kind"], kt["t"]))
            if tag == "UEFN":
                rows.append({"montage": name, "clip": clip, "slot": slot, "length": round(kt["len"], 3),
                             "kind": kt["kind"], "notify": "" if clip == "guard" else NOTIFY_NAME[kt["kind"]],
                             "t": round(kt["t"], 3), "frac": round(kt["t"] / kt["len"], 3),
                             "t_ext": round(kt.get("t_ext", -1), 3), "arm": kt.get("arm", ""),
                             "head": [round(kt[k], 1) for k in ("dx", "dy", "dz")] if "dx" in kt else None,
                             "loops": loops})
    write_doc(rows)
    log("done")


def write_doc(rows):
    path = os.path.join(PROJECT, "Docs", "ANIM_CONTACTS.md")
    L = ["# Кадры контакта монтажей (S-41, трек C)", "",
         "Сгенерировано `Tools/EditorScripts/anim_montages.py` — не править руками, перезапустить скрипт.", "",
         "Клипы — боксёрские Mixamo, ретаргет на UEFN Mannequin (логический скелет GASP) и UE5 Manny;",
         "времена одинаковые для обоих наборов (ретаргет не меняет тайминг). Монтажи: `%s/<Имя>` (UEFN)," % ROOT,
         "`%s/Manny/<Имя>` (Manny). Время — от начала монтажа при PlayRate = 1." % ROOT, "",
         "**Как найден кадр** (как `keyTimes` в web `SkinnedFighter.tsx`): удар — максимальный вынос кисти бьющей руки",
         "вперёд от таза (меш смотрит вдоль +Y) в первых 80% клипа; апперкот — максимум «вперёд + вверх»",
         "(у апперкота вынос вперёд пиков раньше — на замахе); блок — обе перчатки выше всего; слип/реакции —",
         "пик смещения головы относительно таза (в сторону + вниз). `t_ext` — для справки: максимум |кисть − плечо| (на хуках/апперкотах приходится на",
         "замах, поэтому не используется).", "",
         "**Синхронизация с ядром (трек B):** ядро резолвит удар в момент `contact` своей фазы; чтобы кадр контакта",
         "монтажа совпал, `PlayRate = t / T_core_contact` (t — время ниже, T_core_contact — сколько секунд в ядре от",
         "старта удара до резолюции). Нотифай `Contact` (AnimNotify_PlayMontageNotify) приходит в",
         "`UAnimInstance::OnPlayMontageNotifyBegin` (NotifyName = \"Contact\") — для проверки/эффектов.", "",
         "| Монтаж | Клип | Слот | Длина, с | Нотифай | Время, с | Доля | t_ext, с | Рука / смещение головы, см |",
         "|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        extra = ("рука %s" % r["arm"]) if r["arm"] else (("голова dx=%.0f dy=%.0f dz=%.0f" % tuple(r["head"])) if r["head"] else "")
        if r["clip"] == "guard":
            extra = "петля ×%d (стойка-оверлей)" % r["loops"]
        L.append("| %s | %s | %s | %.3f | %s | %s | %s | %s | %s |" % (
            r["montage"], r["clip"], r["slot"], r["length"], r["notify"] or "—",
            "—" if r["clip"] == "guard" else "%.3f" % r["t"], "—" if r["clip"] == "guard" else "%.2f" % r["frac"],
            "%.3f" % r["t_ext"] if r["t_ext"] >= 0 else "—", extra))
    L += ["", "Заметки:",
          "- `AM_UpperR` в исходном клипе Mixamo — апперкот **в корпус** (кисть на уровне пояса, корпус в наклоне);",
          "  `AM_UpperL` — в голову. Ретаргет это не меняет (сверено с исходником).",
          "- `AM_SlipL` — производный: осевая цепочка `slip` отзеркалена (корпус уходит влево), руки — как в оригинале.",
          "- Полнотелые (`DefaultSlot`) — нокдаун/подъём/победа/поражение: играют через уже существующий слот AnimBP GASP.",
          "- Клипы «на месте» (Force Root Lock): горизонтальный ход таза Mixamo (выпад джеба ~40 см, шаг назад",
          "  hitHead ~1 м) убран — позицию бойца ведёт ядро.", "",
          "```json", json.dumps(rows, ensure_ascii=False, indent=1), "```", ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L))
    log("doc -> %s" % path)


main()
