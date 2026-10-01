# Дымовая проверка проекта: ассеты GASP на месте, стартовый уровень грузится.
# Запуск: UnrealEditor-Cmd.exe BoxingUE.uproject -run=pythonscript -script=Tools/EditorScripts/smoke_check.py -nullrhi -unattended
import unreal

reg = unreal.AssetRegistryHelpers.get_asset_registry()
reg.search_all_assets(True)
assets = reg.get_assets_by_path("/Game", recursive=True)
unreal.log("SMOKE assets under /Game: %d" % len(assets))
for cls in ("PoseSearchDatabase", "AnimSequence", "SkeletalMesh", "World"):
    n = sum(1 for a in assets if str(a.asset_class_path.asset_name) == cls)
    unreal.log("SMOKE %s: %d" % (cls, n))
world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Levels/DefaultLevel")
unreal.log("SMOKE DefaultLevel loaded: %s" % (world is not None))
