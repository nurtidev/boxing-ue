# -*- coding: utf-8 -*-
# S-54 (game feel): импорт звуков боя (CC0) из веба в /Game/Boxing/Audio.
#
# Источник — web/public/audio основного репо (MP3, моно 44.1 кГц, список и авторы — CREDITS.md рядом).
# MP3 сначала перекодируются ffmpeg в WAV 16 бит (импорт WAV в UE — самый надёжный путь), затем
# AssetImportTask → USoundWave. Петля зала (crowd_loop) помечается looping.
#
# Запуск (редактор закрыт или открыт — всё равно):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<этот файл> -unattended -nosplash -nullrhi
# Переменные окружения: BOX_AUDIO_SRC — папка с mp3 (по умолчанию ../boxing/web/public/audio рядом с
# проектом), FFMPEG — путь к ffmpeg (по умолчанию из PATH или C:/Users/<user>/Desktop/ffmpeg-*/bin).
# Маркер в логе: FEELAUDIO
import glob
import os
import shutil
import subprocess
import tempfile

import unreal

DEST = "/Game/Boxing/Audio"
LOOPING = {"crowd_loop"}


def log(*a):
    unreal.log("FEELAUDIO " + " ".join(str(x) for x in a))


def project_dir():
    return os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def find_ffmpeg():
    env = os.environ.get("FFMPEG")
    if env and os.path.isfile(env):
        return env
    w = shutil.which("ffmpeg")
    if w:
        return w
    home = os.path.expanduser("~")
    for c in glob.glob(os.path.join(home, "Desktop", "ffmpeg*", "bin", "ffmpeg.exe")):
        return c
    return None


def main():
    src = os.environ.get("BOX_AUDIO_SRC") or os.path.join(os.path.dirname(project_dir()), "boxing", "web", "public", "audio")
    mp3s = sorted(glob.glob(os.path.join(src, "*.mp3")))
    if not mp3s:
        log("нет mp3 в", src)
        return
    ff = find_ffmpeg()
    if not ff:
        log("нет ffmpeg — задай FFMPEG")
        return
    tmp = tempfile.mkdtemp(prefix="boxaudio_")
    wavs = []
    for m in mp3s:
        name = os.path.splitext(os.path.basename(m))[0]
        w = os.path.join(tmp, name + ".wav")
        subprocess.run([ff, "-loglevel", "error", "-y", "-i", m, "-ac", "1", "-ar", "44100", "-c:a", "pcm_s16le", w], check=True)
        wavs.append(w)
    tasks = []
    for w in wavs:
        t = unreal.AssetImportTask()
        t.filename = w
        t.destination_path = DEST
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    n = 0
    for w in wavs:
        name = os.path.splitext(os.path.basename(w))[0]
        path = DEST + "/" + name
        snd = unreal.load_asset(path)
        if not snd:
            log("не импортирован", name)
            continue
        n += 1
        if name in LOOPING:
            snd.set_editor_property("looping", True)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        log("ok", path, "%.2f s" % snd.get_editor_property("duration"))
    # Лицензии — рядом с ассетами (UE не-ассетные файлы в Content игнорирует).
    cred = os.path.join(src, "CREDITS.md")
    if os.path.isfile(cred):
        out = os.path.join(project_dir(), "Content", "Boxing", "Audio", "CREDITS.md")
        shutil.copyfile(cred, out)
        log("CREDITS ->", out)
    shutil.rmtree(tmp, ignore_errors=True)
    log("готово", n, "из", len(wavs))


main()
