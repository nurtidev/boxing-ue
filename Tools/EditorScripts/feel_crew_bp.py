# S-71 (game feel): BP_CornerCrew — угловой (тренер/катмен) на AnimBP Game Animation Sample (ходьба — Motion Matching).
#
# Как BP_Boxer (fight_blueprints.py): AnimBP GASP берёт данные пешки через BPI_SandboxCharacter_Pawn, реализованный
# в BP SandboxCharacter_CMC, — из C++ его не реализовать. Поэтому BP_CornerCrew = КОПИЯ SandboxCharacter_CMC,
# перепривязанная к нативному AFightCrewMember (FightCrew.h): меш, AnimBP, компоненты GASP и интерфейс остаются,
# место/курс ведёт ABoxingCornerCrew, жесты — процедурно на видимом меше.
#
# Запуск (после сборки модуля BoxingUE):
#   UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/feel_crew_bp.py -unattended -nosplash -nullrhi
import unreal

SRC = "/Game/Blueprints/SandboxCharacter_CMC"
DST = "/Game/Boxing/Blueprints/BP_CornerCrew"


def log(msg):
    unreal.log("CREW_BP " + msg)


eal = unreal.EditorAssetLibrary
parent = unreal.load_class(None, "/Script/BoxingUE.FightCrewMember")
if parent is None:
    raise RuntimeError("нет класса /Script/BoxingUE.FightCrewMember — сначала собери модуль BoxingUE")

if not eal.does_asset_exist(DST):
    if not eal.duplicate_asset(SRC, DST):
        raise RuntimeError("не удалось скопировать %s → %s" % (SRC, DST))
    log("скопирован %s → %s" % (SRC, DST))

bp = unreal.load_asset(DST)
gen = unreal.load_object(None, DST + ".BP_CornerCrew_C")
if not isinstance(unreal.get_default_object(gen), unreal.FightCrewMember):
    unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent)
    log("перепривязан к FightCrewMember")
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
eal.save_asset(DST, only_if_is_dirty=False)

gen = unreal.load_object(None, DST + ".BP_CornerCrew_C")
cdo = unreal.get_default_object(gen)
mesh = cdo.get_editor_property("mesh")
log("готово: %s, родитель FightCrewMember=%s, меш %s, AnimClass %s" % (
    gen.get_name(), isinstance(cdo, unreal.FightCrewMember),
    mesh.get_editor_property("skeletal_mesh_asset").get_path_name(),
    mesh.get_editor_property("anim_class").get_path_name() if mesh.get_editor_property("anim_class") else None))
