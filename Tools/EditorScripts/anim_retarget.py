# S-41 трек C, шаг 2: IK Rig для скелета Mixamo + IK Retargeter Mixamo → UEFN Mannequin (логический
# скелет GASP: на нём крутится SandboxCharacter_CMC_ABP и Motion Matching) и → UE5 Manny (SK_Mannequin),
# затем пакетный ретаргет всех клипов A_MX_*.
#   /Game/BoxingLocal/Mixamo/IK_MixamoBoxer, RTG_Mixamo_to_UEFN, RTG_Mixamo_to_Manny
#   /Game/BoxingLocal/Retargeted/UEFN/A_BX_<clip>    — для GASP ABP (скелет SK_UEFN_Mannequin)
#   /Game/BoxingLocal/Retargeted/Manny/A_BX_<clip>   — для «голого» Manny (скелет SK_Mannequin)
# Всё в BoxingLocal (вне git): производные клипов Mixamo.
# Запуск (нужен Slate — ПОЛНЫЙ редактор, скрипт сам его закроет):
#   UnrealEditor.exe BoxingUE.uproject -ExecutePythonScript=<абс.путь>/anim_retarget.py -unattended -nosplash
# Повторяемый: ассеты пересоздаются.
import os
import sys
import unreal

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from anim_common import quit_when_idle  # noqa: E402

MX = "/Game/BoxingLocal/Mixamo"
MX_MESH = MX + "/SKM_MixamoBoxer"
OUT = "/Game/BoxingLocal/Retargeted"

TARGETS = {
    # имя: (IK Rig цели, меш цели)
    "UEFN": ("/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin",
             "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"),
    "Manny": ("/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget",
              "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple"),
}

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("ANIMRTG " + msg)


def recreate(path, cls, factory):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    pkg, name = path.rsplit("/", 1)
    return tools.create_asset(name, pkg, cls, factory)


# --- IK Rig Mixamo ---------------------------------------------------------------------------
# Цепочки названы как в IK_UE5_Mannequin_Retarget — авто-маппинг сведёт их по имени.
def chains_for(side, s):  # side: "Left"/"Right", s: "left"/"right"
    return [
        (side + "Clavicle", s + "shoulder", s + "shoulder", ""),
        (side + "Arm", s + "arm", s + "hand", side + "HandIK"),
        (side + "Thumb", s + "handthumb1", s + "handthumb3", ""),
        (side + "Index", s + "handindex1", s + "handindex3", ""),
        (side + "Middle", s + "handmiddle1", s + "handmiddle3", ""),
        (side + "Ring", s + "handring1", s + "handring3", ""),
        (side + "Pinky", s + "handpinky1", s + "handpinky3", ""),
        (side + "Leg", s + "upleg", s + "foot", side + "FootIK"),
        (side + "Toe", s + "toebase", s + "toebase", ""),
    ]


def build_ik_rig():
    mesh = unreal.load_asset(MX_MESH)
    rig = recreate(MX + "/IK_MixamoBoxer", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    c.set_retarget_root("hips")
    chains = [("Spine", "spine", "spine2", ""), ("Neck", "neck", "neck", ""), ("Head", "head", "head", "")]
    chains += chains_for("Left", "left") + chains_for("Right", "right")
    goals = []
    for name, a, b, goal in chains:
        if goal:
            c.add_new_goal(goal, b)
            goals.append(goal)
        c.add_retarget_chain(name, a, b, goal)
    # FBIK-решатель — нужен IK-цепочкам ретаргетера (ступни на полу, кисти по месту)
    si = c.add_solver("/Script/IKRig.IKRigFullBodyIKSolver")  # в 5.6+ решатели — структуры, тип строкой
    try:
        c.set_start_bone("hips", si)
    except Exception as e:  # noqa
        log("set_start_bone: %s" % e)
    for g in goals:
        c.connect_goal_to_solver(g, si)
    eal.save_loaded_asset(rig)
    log("IK rig chains: %s" % [str(ch.chain_name) for ch in c.get_retarget_chains()])
    return rig


# --- Ретаргетер ------------------------------------------------------------------------------
# Клипы Mixamo несут горизонтальный ход в hips (джеб — выпад ~40 см, hitHead — шаг назад ~1 м).
# Корень генерируем из таза ЦЕЛИ (по горизонтали, на полу), а в клипах включаем Force Root Lock —
# клипы становятся «на месте»: позицию бойца ведёт ядро (трек B), не анимация.
def setup_root_motion_op(r):
    for i in range(r.get_num_retarget_ops()):
        if str(r.get_op_name(i)) != "Root Motion":
            continue
        oc = r.get_op_controller(i)
        st = oc.get_settings()
        st.set_editor_property("root_motion_source", unreal.RootMotionSource.GENERATE_FROM_TARGET_PELVIS)
        st.set_editor_property("root_height_source", unreal.RootMotionHeightSource.SNAP_TO_GROUND)
        st.set_editor_property("rotate_with_pelvis", False)
        oc.set_settings(st)


def build_retargeter(tag, src_rig, tgt_rig_path, tgt_mesh_path):
    tgt_rig = unreal.load_asset(tgt_rig_path)
    tgt_mesh = unreal.load_asset(tgt_mesh_path)
    tc = unreal.IKRigController.get_controller(tgt_rig)
    log("%s target chains: %s" % (tag, [str(ch.chain_name) for ch in tc.get_retarget_chains()]))
    rtg = recreate(MX + "/RTG_Mixamo_to_" + tag, unreal.IKRetargeter, unreal.IKRetargetFactory())
    r = unreal.IKRetargeterController.get_controller(rtg)
    S, T = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    r.set_ik_rig(S, src_rig)
    r.set_ik_rig(T, tgt_rig)
    r.set_preview_mesh(S, unreal.load_asset(MX_MESH))
    r.set_preview_mesh(T, tgt_mesh)
    r.remove_all_ops()
    r.add_default_ops()
    setup_root_motion_op(r)
    r.assign_ik_rig_to_all_ops(S, src_rig)
    r.assign_ik_rig_to_all_ops(T, tgt_rig)
    r.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Mixamo — T-поза, манекены — A-поза: выравниваем позу ЦЕЛИ под источник (цепочка к цепочке)
    r.auto_align_all_bones(T, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    try:
        r.snap_bone_to_ground("ball_l", T)
    except Exception as e:  # noqa
        log("snap: %s" % e)
    for i in range(r.get_num_retarget_ops()):
        log("%s op[%d] %s enabled=%s" % (tag, i, r.get_op_name(i), r.get_retarget_op_enabled(i)))
    eal.save_loaded_asset(rtg)
    return rtg


def retarget_clips(tag, rtg, tgt_mesh_path):
    dest = "%s/%s" % (OUT, tag)
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    eal.make_directory(dest)
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    clips = [a for a in reg.get_assets_by_path(MX, recursive=False)
             if str(a.asset_class_path.asset_name) == "AnimSequence"]
    res = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        clips, unreal.load_asset(MX_MESH), unreal.load_asset(tgt_mesh_path), rtg,
        "A_MX_", "A_BX_", "", "_" + tag, False, True)
    n = 0
    for a in res:
        src = str(a.package_name)
        name = str(a.asset_name)
        if not name.startswith("A_BX_"):
            continue
        new_name = name[: -len("_" + tag)] if name.endswith("_" + tag) else name
        dst = "%s/%s" % (dest, new_name)
        if src != dst:
            eal.rename_asset(src, dst)
        seq = unreal.load_asset(dst)
        unreal.AnimationLibrary.set_is_root_motion_lock_forced(seq, True)
        unreal.AnimationLibrary.set_root_motion_lock_type(seq, unreal.RootMotionRootLock.REF_POSE)
        n += 1
    log("%s: retargeted %d clips -> %s" % (tag, n, dest))
    eal.save_directory(dest)


def main():
    rig = build_ik_rig()
    for tag, (rig_path, mesh_path) in TARGETS.items():
        rtg = build_retargeter(tag, rig, rig_path, mesh_path)
        retarget_clips(tag, rtg, mesh_path)
    log("done")


try:
    main()
finally:
    # Пакетному ретаргету нужен Slate: скрипт гоняется в ПОЛНОМ редакторе (-ExecutePythonScript),
    # закрываем его сами, дав досжаться анимациям.
    quit_when_idle(6.0)
