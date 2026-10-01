# S-41 трек C, шаг 4: AnimBP бойца = копия AnimBP GASP (оригинал НЕ трогаем).
#   /Game/Blueprints/SandboxCharacter_CMC_ABP  →  /Game/Boxing/Anim/ABP_Boxer
# Python не умеет добавлять узлы в AnimGraph (нет API создания узлов/связей пинов), поэтому слой
# верхней части тела (Slot «UpperBody» + Layered blend per bone от spine_01) — ручной шаг на 5 минут:
# Docs/ANIM_SETUP.md. Скрипт печатает, что в графе уже есть (слоты, узлы), и компилирует копию.
# Запуск: UnrealEditor-Cmd.exe BoxingUE.uproject -run=pythonscript -script=<абс.путь>/anim_abp.py -unattended -nosplash -nullrhi
# Повторный запуск НЕ перезаписывает ABP_Boxer (там будут ручные правки) — только отчёт; пересоздать: ANIM_ABP_RECREATE=1.
import os
from collections import Counter

import unreal

SRC = "/Game/Blueprints/SandboxCharacter_CMC_ABP"
DST = "/Game/Boxing/Anim/ABP_Boxer"
eal = unreal.EditorAssetLibrary
AL = unreal.AnimationLibrary


def log(m):
    unreal.log("ANIMABP " + m)


def main():
    if eal.does_asset_exist(DST) and os.environ.get("ANIM_ABP_RECREATE"):
        eal.delete_asset(DST)
    if not eal.does_asset_exist(DST):
        if not eal.does_directory_exist("/Game/Boxing/Anim"):
            eal.make_directory("/Game/Boxing/Anim")
        eal.duplicate_asset(SRC, DST)
        log("duplicated %s -> %s" % (SRC, DST))
    abp = unreal.load_asset(DST)
    log("skeleton: %s" % abp.get_editor_property("target_skeleton").get_path_name())
    nodes = AL.get_nodes_of_class(abp, unreal.AnimGraphNode_Base, True)
    log("nodes: %s" % dict(Counter(n.get_class().get_name() for n in nodes)))
    for n in AL.get_nodes_of_class(abp, unreal.AnimGraphNode_Slot, True):
        log("slot node: %s" % n.get_editor_property("node").get_editor_property("slot_name"))
    for n in AL.get_nodes_of_class(abp, unreal.AnimGraphNode_LayeredBoneBlend, True):
        log("layered blend: %s" % n.get_editor_property("node").get_editor_property("layer_setup"))
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    eal.save_loaded_asset(abp)
    log("compiled+saved %s" % DST)


main()
