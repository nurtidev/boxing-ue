# S-41 трек C, шаг 5: данные физреакции на попадание (для трека B, C++).
#   /Game/Boxing/Anim/DT_HitReaction_PhysAnim — DataTable со строками FPhysicalAnimationData.
#   Имя строки = кость; строки применяются ПО ПОРЯДКУ через
#   UPhysicalAnimationComponent::ApplyPhysicalAnimationSettingsBelow(Row, Data, /*bIncludeSelf*/true):
#   сначала общий «каркас» от pelvis, затем мягче — к голове, жёстче — руки (держат стойку).
#   Все пружины локальные (bIsLocalSimulation = true): тело тянется к позе анимации, импульс удара
#   выбивает его и пружина возвращает. Обоснование и импульсы по ударам — Docs/HIT_REACTION.md.
# PhysicsControlAsset из Python не заполнить (профили protected) — для Physics Control трек B
# создаёт контролы в рантайме (см. док), значения те же.
# Запуск: UnrealEditor-Cmd.exe BoxingUE.uproject -run=pythonscript -script=<абс.путь>/anim_hitreaction.py -unattended -nosplash -nullrhi
import json

import unreal

DST_DIR = "/Game/Boxing/Anim"
NAME = "DT_HitReaction_PhysAnim"

# кость (применяется к ней и ниже), ориент. сила, демпф. угл. скорости, макс. момент (0 — без лимита)
ROWS = [
    ("pelvis",     6000.0, 600.0, 0.0),   # таз/ноги: держат стойку (ноги — Motion Matching, физику ног не включать)
    ("spine_01",   2500.0, 250.0, 0.0),   # низ корпуса
    ("spine_03",   1500.0, 150.0, 0.0),   # грудь: заметно «качается» от удара в корпус
    ("clavicle_l", 2000.0, 200.0, 0.0),   # плечевой пояс и руки — жёстче корпуса: перчатки остаются у лица
    ("clavicle_r", 2000.0, 200.0, 0.0),
    ("hand_l",     3000.0, 300.0, 0.0),
    ("hand_r",     3000.0, 300.0, 0.0),
    ("neck_01",     600.0,  60.0, 0.0),   # шея и голова — мягче всего: «голова отлетает»
    ("head",        350.0,  35.0, 0.0),
]


def log(m):
    unreal.log("ANIMHIT " + m)


def main():
    eal = unreal.EditorAssetLibrary
    path = "%s/%s" % (DST_DIR, NAME)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    f = unreal.DataTableFactory()
    f.set_editor_property("struct", unreal.PhysicalAnimationData.static_struct())
    dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DST_DIR, unreal.DataTable, f)
    rows = [{"Name": b, "bIsLocalSimulation": True, "OrientationStrength": o, "AngularVelocityStrength": av,
             "PositionStrength": 0.0, "VelocityStrength": 0.0, "MaxLinearForce": 0.0, "MaxAngularForce": mt}
            for b, o, av, mt in ROWS]
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json.dumps(rows))
    eal.save_loaded_asset(dt)
    log("%s filled=%s rows=%s" % (path, ok, [str(r) for r in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]))


main()
