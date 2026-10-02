# -*- coding: utf-8 -*-
# S-60: импорт своих грумов (Tools/Blender/look_hair.py → Saved/LookWork/hair/GR_*.abc) + привязка к лицу Kellan.
#
# ПОЛНЫЙ редактор (импорт грума в коммандлете падает на Slate) и плагин импорта Alembic-волос, включённый только
# на этот запуск (.uproject не трогаем; в игре он не нужен — ассет грума уже готов):
#   UnrealEditor.exe <uproject> -EnablePlugins=AlembicHairImporter -ExecCmds="py <этот файл>" -unattended -nosplash
# Создаёт /Game/BoxingLocal/Characters/Grooms/GR_* (грум в пространстве меша лица Kellan, сантиметры; материал —
# волосы/борода Kellan). Без привязки к коже: рантайм крепит грум к кости head лица (смещение = обратная поза
# привязки head) — пряди жёстко следуют голове. LOOK_GROOM_BIND=1 — ещё и GB_*_Kellan (медленно). Производные: геометрия построена по мешу лица Kellan → BoxingLocal.
# Скрипт сам закрывает редактор. Маркер в логе: LOOKGROOM
import glob
import os

import unreal

eal = unreal.EditorAssetLibrary
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT, "Saved", "LookWork", "hair")
DEST = "/Game/BoxingLocal/Characters/Grooms"
FACE = "/Game/MetaHumans/Kellan/Face/Kellan_FaceMesh"
MI_HAIR = "/Game/MetaHumans/Kellan/Materials/MI_Hair"
MI_FACIAL = "/Game/MetaHumans/Kellan/Materials/MI_Facial_Hair"
FACIAL = ("GR_Beard", "GR_FullBeard", "GR_Goatee", "GR_Mustache")
ONLY = [x for x in os.environ.get("LOOK_GROOM_ONLY", "").split(",") if x]
BIND = os.environ.get("LOOK_GROOM_BIND", "0") == "1"


def log(*a):
    unreal.log("LOOKGROOM " + " ".join(str(x) for x in a))


def import_one(path):
    name = os.path.splitext(os.path.basename(path))[0]
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = DEST
    t.destination_name = name
    t.replace_existing = True
    t.automated = True
    t.save = True
    t.factory = unreal.HairStrandsFactory()
    t.options = unreal.GroomImportOptions()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    g = eal.load_asset("%s/%s" % (DEST, name))
    if not g:
        log("НЕ импортирован", path)
        return None
    mi = eal.load_asset(MI_HAIR)       # и бороды: MI_Facial_Hair на длинных прядях светлит (рассчитан на щетину)
    try:
        mats = g.get_editor_property("hair_groups_materials")
        if not mats:
            m = unreal.HairGroupsMaterial()
            m.set_editor_property("slot_name", "Hair")
            mats = [m]
        for m in mats:
            m.set_editor_property("material", mi)
        g.set_editor_property("hair_groups_materials", mats)
    except Exception as e:  # noqa
        log("материал ?", name, e)
    eal.save_loaded_asset(g)
    b = None
    if BIND:
        # Привязка к коже лица: строится ~10–30 мин на грум (проекция на все LOD лица). Не нужна: лицо не
        # анимируется, рантайм крепит грум к кости head (look_apply / ApplyBoxerLook) — см. Docs/LOOK.md.
        face = eal.load_asset(FACE)
        bpath = "%s/GB_%s_Kellan" % (DEST, name[3:])
        if eal.does_asset_exist(bpath):
            eal.delete_asset(bpath)
        b = unreal.GroomLibrary.create_new_groom_binding_asset_with_path(bpath, g, face, 100)
        if b:
            eal.save_loaded_asset(b)
    try:
        info = g.get_editor_property("hair_groups_info")
        info = [(i.get_editor_property("num_curves"), i.get_editor_property("num_guides")) for i in info]
    except Exception as e:  # noqa
        info = "?%s" % e
    log("грум", name, "группы", info, "привязка", b.get_path_name() if b else None)
    return g


def run():
    if not eal.does_directory_exist(DEST):
        eal.make_directory(DEST)
    for f in sorted(glob.glob(os.path.join(SRC, "GR_*.abc"))):
        if ONLY and os.path.splitext(os.path.basename(f))[0] not in ONLY:
            continue
        try:
            import_one(f)
        except Exception as e:  # noqa
            log("ERROR", f, e)
    log("готово")


st = {"n": 0}


def tick(dt):
    st["n"] += 1
    if st["n"] == 60:          # модуль импорта волос регистрирует переводчики не сразу
        run()
        unreal.unregister_slate_post_tick_callback(st["h"])
        unreal.SystemLibrary.quit_editor()


st["h"] = unreal.register_slate_post_tick_callback(tick)
