# S-41, трек B: ассеты Enhanced Input боя — /Game/Boxing/Input/IA_* + IMC_Fight.
# Раскладка — как в вебе (CLAUDE.md «Интерактивный бой»). ТАБЛИЦА ДУБЛИРУЕТ GActionSpecs в
# Source/BoxingUE/Private/BoxingFightPlayerController.cpp — менять оба места. Если ассетов нет,
# контроллер создаёт ту же раскладку в рантайме.
# Модификаторы (Negate/Swizzle) не нужны: каждое направление — своё булево действие, стики — Axis1D,
# пороги и «экранные» оси считает контроллер.
#
# Запуск: UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/fight_input.py -unattended -nosplash -nullrhi
import unreal

FOLDER = "/Game/Boxing/Input"
SPECS = [
    # имя,        ось?,  клавиши
    ("Jab",      False, ["J", "Gamepad_FaceButton_Left"]),
    ("Cross",    False, ["K", "Gamepad_FaceButton_Top"]),
    ("HookL",    False, ["U", "Gamepad_LeftShoulder"]),
    ("HookR",    False, ["I", "Gamepad_RightShoulder"]),
    ("UpperL",   False, ["N", "Gamepad_FaceButton_Bottom"]),
    ("UpperR",   False, ["M", "Gamepad_FaceButton_Right"]),
    ("Block",    False, ["SpaceBar", "Gamepad_LeftTrigger"]),
    ("SlipL",    False, ["Q"]),
    ("SlipR",    False, ["E"]),
    ("SlipAxis", True,  ["Gamepad_RightX"]),
    ("StepFwd",  False, ["D", "Right"]),
    ("StepBack", False, ["A", "Left"]),
    ("StepUp",   False, ["W", "Up"]),
    ("StepDown", False, ["S", "Down"]),
    ("MoveX",    True,  ["Gamepad_LeftX"]),
    ("MoveY",    True,  ["Gamepad_LeftY"]),
    ("Mod",      False, ["LeftShift", "RightShift"]),
    ("BodyPad",  False, ["Gamepad_RightTrigger"]),
    ("PivotPad", False, ["Gamepad_LeftThumbstick"]),
    ("Proceed",  False, ["Enter", "Gamepad_Special_Right"]),
]


def log(msg):
    unreal.log("FIGHT_INPUT " + msg)


eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def factory(name):
    cls = getattr(unreal, name, None)
    return cls() if cls else None


def get_or_create(name, cls, factory_name):
    path = "%s/%s" % (FOLDER, name)
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    a = tools.create_asset(name, FOLDER, cls, factory(factory_name))
    if a is None:
        raise RuntimeError("не создан " + path)
    return a


def make_key(name):
    k = unreal.Key()
    k.set_editor_property("key_name", name)
    return k


actions = {}
for name, axis, _ in SPECS:
    ia = get_or_create("IA_" + name, unreal.InputAction, "InputAction_Factory")
    ia.set_editor_property("value_type", unreal.InputActionValueType.AXIS1D if axis else unreal.InputActionValueType.BOOLEAN)
    actions[name] = ia

imc = get_or_create("IMC_Fight", unreal.InputMappingContext, "InputMappingContext_Factory")
imc.unmap_all()
n = 0
for name, _, keys in SPECS:
    for key in keys:
        imc.map_key(actions[name], make_key(key))
        n += 1

for name in actions:
    eal.save_asset("%s/IA_%s" % (FOLDER, name), only_if_is_dirty=False)
eal.save_asset(FOLDER + "/IMC_Fight", only_if_is_dirty=False)
log("готово: действий %d, привязок %d" % (len(actions), n))
