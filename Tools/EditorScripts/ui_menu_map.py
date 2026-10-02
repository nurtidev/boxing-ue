# S-55: карта меню /Game/Boxing/Maps/L_Menu — пустой уровень с GameMode Override = BoxingMenuGameMode.
# Фон (арену) MenuGameMode подгружает сам: L_Ring как экземпляр уровня (одна правда об арене, без копии).
# Здесь — только уровень и GameMode (без арены — тёмный фон виджетов).
#
# Запуск: UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/ui_menu_map.py -unattended -nosplash -nullrhi
import unreal

MAP = "/Game/Boxing/Maps/L_Menu"


def log(msg):
    unreal.log("UI_MENU_MAP " + msg)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eal = unreal.EditorAssetLibrary

if eal.does_asset_exist(MAP):
    eal.delete_asset(MAP)
les.new_level(MAP, False)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ws = world.get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/BoxingUE.BoxingMenuGameMode"))
les.save_current_level()
log("готово: %s, GameMode %s" % (MAP, ws.get_editor_property("default_game_mode")))
