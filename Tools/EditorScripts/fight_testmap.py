# S-41, трек B: тестовый уровень боя /Game/Boxing/Maps/L_FightTest — пол, разметка ринга 6.1 м
# (столбы и канаты-заглушки), свет, GameMode Override = BoxingFightGameMode. Настоящий ринг/арена —
# /Game/Boxing/Maps/L_Ring (трек A); этот уровень — только для проверки геймплея.
#
# Запуск: UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/fight_testmap.py -unattended -nosplash -nullrhi
import unreal

MAP = "/Game/Boxing/Maps/L_FightTest"
ROPE_HALF = 305.0   # см, внутри канатов (ringSize.ts ROPE_HALF = 3.05 м)
ROPES = [41.0, 71.0, 102.0, 132.0]


def log(msg):
    unreal.log("FIGHT_MAP " + msg)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
eal = unreal.EditorAssetLibrary

if eal.does_asset_exist(MAP):
    eal.delete_asset(MAP)
les.new_level(MAP, False)

plane = unreal.load_asset("/Engine/BasicShapes/Plane")
cube = unreal.load_asset("/Engine/BasicShapes/Cube")
cyl = unreal.load_asset("/Engine/BasicShapes/Cylinder")


def mesh_actor(label, mesh, loc, scale, rot=None):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), rot or unreal.Rotator(0, 0, 0))
    a.set_actor_label(label)
    a.static_mesh_component.set_static_mesh(mesh)
    a.set_actor_scale3d(unreal.Vector(*scale))
    return a


# Пол 30×30 м (Plane 1×1 м).
mesh_actor("Floor", plane, (0, 0, 0), (30, 30, 1))
# Столбы по углам и канаты (без коллизии — ядро само держит бойцов в ринге).
for sx in (-1, 1):
    for sy in (-1, 1):
        p = mesh_actor("Post", cyl, (sx * (ROPE_HALF + 15), sy * (ROPE_HALF + 15), 70), (0.12, 0.12, 1.4))
        p.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
L = (ROPE_HALF + 15) * 2 / 100.0
for z in ROPES:
    for i, (x, y, sx, sy) in enumerate([(0, ROPE_HALF + 15, L, 0.03), (0, -(ROPE_HALF + 15), L, 0.03),
                                        (ROPE_HALF + 15, 0, 0.03, L), (-(ROPE_HALF + 15), 0, 0.03, L)]):
        r = mesh_actor("Rope", cube, (x, y, z), (sx, sy, 0.03))
        r.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

center = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
center.set_actor_label("RingCenter")
center.tags = ["RingCenter"]

sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-35, -40, 0))
sun.set_actor_label("Sun")
sun.light_component.set_editor_property("atmosphere_sun_light", True)
sun.light_component.set_intensity(6.0)
eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300), unreal.Rotator(0, 0, 0))
sky.light_component.set_editor_property("real_time_capture", True)
sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ws = world.get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/BoxingUE.BoxingFightGameMode"))
les.save_current_level()
log("готово: %s, GameMode %s" % (MAP, ws.get_editor_property("default_game_mode")))
