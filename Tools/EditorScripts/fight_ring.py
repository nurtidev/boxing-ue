# S-41, трек B: бой на уровне трека A /Game/Boxing/Maps/L_Ring.
# Правит ТОЛЬКО WorldSettings (GameMode Override = BoxingFightGameMode) и, если нет, добавляет
# TargetPoint «RingCenter» (тег RingCenter) в центр канваса (0, 0, 0). Геометрию трека A не трогает.
#
# Запуск: UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/fight_ring.py -unattended -nosplash -nullrhi
import unreal

MAP = "/Game/Boxing/Maps/L_Ring"


def log(msg):
    unreal.log("FIGHT_RING " + msg)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(MAP):
    raise RuntimeError("не загрузился " + MAP)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ws = world.get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/BoxingUE.BoxingFightGameMode"))

centers = [a for a in eas.get_all_level_actors() if a.actor_has_tag("RingCenter")]
if centers:
    log("RingCenter уже есть: %s в %s" % (centers[0].get_actor_label(), centers[0].get_actor_location()))
else:
    tp = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    tp.set_actor_label("RingCenter")
    tp.tags = ["RingCenter"]
    log("добавлен RingCenter (0, 0, 0)")

ropes = [a.get_actor_label() for a in eas.get_all_level_actors() if a.get_actor_label().startswith(("Ring_Rope", "Ring_Post", "Ring_Pad"))]
log("канатов/столбов/подушек (их прячет камера у ближней стороны): %d" % len(ropes))
les.save_current_level()
log("готово: %s, GameMode %s" % (MAP, ws.get_editor_property("default_game_mode")))
