# S-41, трек B: BP_Boxer — визуальный боец на AnimBP Game Animation Sample.
#
# AnimBP GASP (SandboxCharacter_CMC_ABP) берёт данные пешки через интерфейс BPI_SandboxCharacter_Pawn
# (Get_PropertiesForAnimation и т.п.), реализованный в BP SandboxCharacter_CMC. Из C++ BP-интерфейс
# не реализовать, поэтому BP_Boxer = КОПИЯ SandboxCharacter_CMC, перепривязанная (reparent) к нативному
# ABoxerCharacter: меш, AnimBP, компоненты GASP (PreCMCTick, MotionWarping…) и реализация интерфейса
# остаются, а позицию/курс/монтажи ведёт C++ из ядра боя.
#
# Запуск (редактор закрыт или открыт — не важно, пишет только /Game/Boxing/Blueprints):
#   UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/fight_blueprints.py -unattended -nosplash -nullrhi
import unreal

SRC = "/Game/Blueprints/SandboxCharacter_CMC"
DST = "/Game/Boxing/Blueprints/BP_Boxer"


def log(msg):
    unreal.log("FIGHT_BP " + msg)


eal = unreal.EditorAssetLibrary
parent = unreal.load_class(None, "/Script/BoxingUE.BoxerCharacter")
if parent is None:
    raise RuntimeError("нет класса /Script/BoxingUE.BoxerCharacter — сначала собери модуль BoxingUE")

if not eal.does_asset_exist(DST):
    if not eal.duplicate_asset(SRC, DST):
        raise RuntimeError("не удалось скопировать %s → %s" % (SRC, DST))
    log("скопирован %s → %s" % (SRC, DST))

bp = unreal.load_asset(DST)
gen = unreal.load_object(None, DST + ".BP_Boxer_C")
cur_parent = None
try:
    cur_parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp) if hasattr(unreal.BlueprintEditorLibrary, "get_blueprint_parent_class") else None
except Exception:
    cur_parent = None
log("текущий родитель: %s" % cur_parent)
unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
eal.save_asset(DST, only_if_is_dirty=False)

gen = unreal.load_object(None, DST + ".BP_Boxer_C")
cdo = unreal.get_default_object(gen)
mesh = cdo.get_editor_property("mesh")
log("готово: %s, родитель BoxerCharacter=%s, меш %s, AnimClass %s" % (
    gen.get_name(), isinstance(cdo, unreal.BoxerCharacter),
    mesh.get_editor_property("skeletal_mesh_asset").get_path_name(),
    mesh.get_editor_property("anim_class").get_path_name() if mesh.get_editor_property("anim_class") else None))
