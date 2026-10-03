# S-69 (tech-artist): урезанная локомоция боксёра — AnimBP/chooser'ы GASP без прыжков, бега, спринта, приседа, паркура.
#
# Холодный «В бой» почти целиком был загрузкой BP_Boxer: копия SandboxCharacter_CMC тянула
#   * AC_VisualOverrideManager → GM_Sandbox → PC_Sandbox, SandboxCharacter_Mover (+ его AnimBP и базы), Echo, Twinblast,
#     Kellan, Manny/Quinn… (≈ 1200 пакетов, ~9 с) — а облик бойца у нас ставит C++ (ABoxerCharacter::ApplyVisualOverride);
#   * AC_PreCMCTick → ОРИГИНАЛ SandboxCharacter_CMC → всё то же самое;
#   * SandboxCharacter_CMC_ABP → CHT_PoseSearchDatabases (Dense/Sparse/ExtremeSparse: 71 база Motion Matching) и
#     CHT_CMCCharacterAnimations (экспериментальная стейт-машина, ~390 клипов; по умолчанию выключена).
# Боксёру (ядро всегда шлёт WantsToWalk + WantsToStrafe: Gait = Walk, Stance = Stand, на земле) из баз нужны только
# стойка, развороты на месте, старт/цикл/разворот/стоп шага (и остановка с бега — по скорости). Оригиналы GASP не
# трогаются: всё — копии, ссылки внутри копий перепривязываются (UBoxerAssetTools, C++). Подробно — Docs/ANIM_SETUP.md, S-69.
#
# Что делает (повторяемо; повторный прогон сохраняет только изменённое):
#   1. /Game/Boxing/Anim/Loco/AC_BoxerPreCMCTick — копия AC_PreCMCTick без ссылки на SandboxCharacter_CMC (владелец →
#      Character); AC_BoxerTraversalLogic — копия паркура с пустыми копиями chooser'ов монтажей (CHT_BoxerTraversal_*).
#   2. /Game/Boxing/Anim/ABP_Boxer (копия AnimBP GASP, anim_abp.py) — урезанный: chooser'ы баз → CHT_BoxerMM(+_Dense/
#      _Sparse/_ExtremeSparse) (строки те же, лишние базы обнулены), стейт-машина → CHT_BoxerStateMachine (клипы обнулены),
#      blend space'ы → копии. Нужные базы (17), их клипы (≈ 250), наборы нормализации — копии в /Game/BoxingLocal/Anim/Loco
#      (вне git, ~400 МБ): у GASP клипы ходьбы через нотифаи стейт-машины тянут оригинальный AnimBP со всеми базами.
#   3. BP_Boxer / BP_Referee / BP_CornerCrew (+ LOCO_EXTRA_BPS через «;»): компонент AC_VisualOverrideManager удалён
#      (граф его не использует), AC_PreCMCTick/AC_TraversalLogic → копии (класс компонента меняется на месте, связи графа
#      целы), AnimClass логического меша → ABP_Boxer. LOCO_SKIP_BPS=1 — только ассеты анимации.
#
# Запуск (после сборки модуля BoxingUE — нужен UBoxerAssetTools):
#   UnrealEditor-Cmd.exe <BoxingUE.uproject> -run=pythonscript -script=<абс.путь>/anim_loco.py -unattended -nosplash -nullrhi
# Новый BP-персонаж из копии SandboxCharacter_CMC (feel_*_bp.py) — после его скрипта прогнать этот (добавить путь в BPS
# или LOCO_EXTRA_BPS), иначе он один вернёт полную загрузку GASP (~20 с холодного старта).
# Откат: git checkout BP_*/ABP_Boxer (до S-69) — оригиналы GASP не менялись.
import os
import re
import unreal

LOCO = "/Game/Boxing/Anim/Loco"           # chooser'ы, компоненты — малые, в git (ссылаются на GASP, как ABP_Boxer)
LOCAL = "/Game/BoxingLocal/Anim/Loco"     # копии баз и клипов GASP (~400 МБ) — вне git
MM = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/"
ESM = "/Game/Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData/"
SRC_ABP = "/Game/Blueprints/SandboxCharacter_CMC_ABP"
SRC_PRE = "/Game/Blueprints/AC_PreCMCTick"
SRC_VOM = "/Game/Blueprints/AC_VisualOverrideManager"
SRC_CMC = "/Game/Blueprints/SandboxCharacter_CMC"
SRC_TRAV = "/Game/Blueprints/AC_TraversalLogic"

# AnimBP бойца — прежний ABP_Boxer (копия AnimBP GASP из anim_abp.py; ручной шаг в нём так и не делался): теперь он
# урезанный. Его же грузит предзагрузка папки /Game/Boxing/Anim — полная копия тянула бы все базы обратно.
ABP = "/Game/Boxing/Anim/ABP_Boxer"
PRE = LOCO + "/AC_BoxerPreCMCTick"
CHT = {  # оригинал → копия
    MM + "CHT_PoseSearchDatabases": LOCO + "/CHT_BoxerMM",
    MM + "CHT_PoseSearchDatabases_Dense": LOCO + "/CHT_BoxerMM_Dense",
    MM + "CHT_PoseSearchDatabases_Sparse": LOCO + "/CHT_BoxerMM_Sparse",
    MM + "CHT_PoseSearchDatabases_ExtremeSparse": LOCO + "/CHT_BoxerMM_ExtremeSparse",
    ESM + "CHT_CMCCharacterAnimations": LOCO + "/CHT_BoxerStateMachine",
}
# Базы, до которых бокс доходит: Stance=Stand, Gait=Walk, на земле, без приземлений/паркура. Run_Stops — строка
# «Stand Idles» по скорости ≥ 100 см/с (боец остановился на скольжении/пивоте) — оставлена.
KEEP_DB = re.compile(r"_Stand_(Idles|TurnInPlace|Walk_(Starts|Loops|Pivots|Stops|SpinTransition)|Run_Stops)$")
BPS = ["/Game/Boxing/Blueprints/BP_Boxer", "/Game/Boxing/Blueprints/BP_Referee", "/Game/Boxing/Blueprints/BP_CornerCrew"] + \
    [p for p in os.environ.get("LOCO_EXTRA_BPS", "").split(";") if p]

eal = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
T = unreal.BoxerAssetTools
HARD = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True,
                                             include_searchable_names=False, include_soft_management_references=False,
                                             include_hard_management_references=False)
STATS = {}


def log(msg):
    unreal.log("LOCO " + msg)


def gen_class(path):
    n = path.rsplit("/", 1)[1]
    return unreal.load_object(None, "%s.%s_C" % (path, n))


def dup(src, dst):
    if not eal.does_asset_exist(dst):
        if not eal.duplicate_asset(src, dst):
            raise RuntimeError("не скопировать %s → %s" % (src, dst))
        log("копия %s → %s" % (src, dst))
    return unreal.load_asset(dst)


def deps(pkg):
    return [str(d) for d in (ar.get_dependencies(pkg, HARD) or []) if not str(d).startswith("/Script")]


def asset_class(pkg):
    ads = ar.get_assets_by_package_name(pkg)
    return str(ads[0].asset_class_path.asset_name) if ads else "?"


def replace(asset, pairs, what):
    pairs = [(a, b) for a, b in pairs if a is not None]
    if not pairs:
        return 0
    n = T.replace_references_in_asset(asset, [a for a, _ in pairs], [b for _, b in pairs])
    log("%s: заменено ссылок %d (%s)" % (asset.get_path_name().split(".")[0], n, what))
    return n


def compile_bp(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)


def save(path, force=False):
    # повторный прогон не переписывает неизменённое (каталог общий, файлы бывают заняты чужими -game)
    eal.save_asset(path, only_if_is_dirty=not force)


# ---------------------------------------------------------------------------------------------------------------
# 1. AC_PreCMCTick без ссылки на персонажа GASP
# ---------------------------------------------------------------------------------------------------------------
pre = dup(SRC_PRE, PRE)
replace(pre, [(gen_class(SRC_PRE), gen_class(PRE)), (gen_class(SRC_CMC), unreal.Character.static_class())], "владелец → Character")
compile_bp(pre)
save(PRE)
left = [d for d in deps(PRE) if d.startswith("/Game/Blueprints/Sandbox")]
log("AC_BoxerPreCMCTick: зависимости %s" % deps(PRE))
if left:
    raise RuntimeError("AC_BoxerPreCMCTick всё ещё ссылается на %s" % left)

# 1б. Паркур (AC_TraversalLogic): граф персонажа зовёт его на прыжок (у боксёра прыжка нет), а chooser'ы монтажей
# паркура тянут клипы → базы PSD_SM_* → BP_NotifyState_EarlyTransition → ОРИГИНАЛЬНЫЙ AnimBP GASP со всеми базами.
# Копия компонента с копиями chooser'ов, где все монтажи/клипы/базы обнулены (паркур никогда не найдёт анимацию).
TRAV = LOCO + "/AC_BoxerTraversalLogic"
TRAV_CHT = {"/Game/Characters/UEFN_Mannequin/Animations/Traversal/CHT_TraversalMontages_CMC": LOCO + "/CHT_BoxerTraversal_CMC",
            "/Game/Characters/UEFN_Mannequin/Animations/Traversal/CHT_TraversalMontages_Mover": LOCO + "/CHT_BoxerTraversal_Mover"}
trav = dup(SRC_TRAV, TRAV)
trav_pairs = [(gen_class(SRC_TRAV), gen_class(TRAV))]
for src, dst in TRAV_CHT.items():
    c = dup(src, dst)
    trav_pairs.append((unreal.load_asset(src), c))
for src, dst in TRAV_CHT.items():
    nulls = [(unreal.load_asset(d), None) for d in deps(src)
             if asset_class(d) in ("AnimMontage", "AnimSequence", "PoseSearchDatabase", "AnimComposite", "BlendSpace")]
    replace(unreal.load_asset(dst), trav_pairs + nulls, "паркур: обнулено %d" % len(nulls))
    save(dst)
replace(trav, trav_pairs, "chooser'ы паркура → пустые копии")
compile_bp(trav)
for s in T.relink_orphan_pins(trav):
    log("AC_BoxerTraversalLogic: контакт " + s)
compile_bp(trav)
save(TRAV)

# ---------------------------------------------------------------------------------------------------------------
# 2. AnimBP и chooser'ы
# ---------------------------------------------------------------------------------------------------------------
abp = dup(SRC_ABP, ABP)
src_abp_c, abp_c = gen_class(SRC_ABP), gen_class(ABP)
chts = {src: dup(src, dst) for src, dst in CHT.items()}
cht_pairs = [(unreal.load_asset(src), chts[src]) for src in CHT]
ANIM_KINDS = ("AnimSequence", "AnimMontage", "BlendSpace", "BlendSpace1D", "AnimComposite", "PoseAsset")

# 2а. Какие базы нужны и что они тянут. Сами данные GASP перепутаны: каждая база ссылается на набор нормализации
# (PSN_*_All — ВСЕ базы своего уровня, с прыжками и паркуром), клипы ходьбы — через нотифаи экспериментальной
# стейт-машины на базы PSD_SM_CMC_* (там клипы бега с BP_NotifyState_EarlyTransition → ОРИГИНАЛЬНЫЙ AnimBP GASP →
# все chooser'ы и 71 база). Поэтому нужные базы, их клипы, наборы нормализации и blend space'ы AnimBP копируются
# (вне git — BoxingLocal), а в копиях ссылки на лишние базы обнуляются.
NULL = {}       # оригинал → None
COPY = {}       # путь оригинала → путь копии
kept_db = set()
for src in CHT:
    for d in deps(src):
        if asset_class(d) == "PoseSearchDatabase":
            (kept_db.add(d) if KEEP_DB.search(d.rsplit("/", 1)[1]) else NULL.setdefault(d, None))
        elif src.endswith("CHT_CMCCharacterAnimations") and asset_class(d) in ANIM_KINDS:
            NULL.setdefault(d, None)  # экспериментальная стейт-машина выключена — её клипы не нужны
anim_src = set()
for db in kept_db:
    COPY[db] = LOCAL + "/DB/" + db.rsplit("/", 1)[1]
    for d in deps(db):
        k = asset_class(d)
        if k == "PoseSearchNormalizationSet":
            COPY[d] = LOCAL + "/DB/" + d.rsplit("/", 1)[1].replace("PSN_", "PSN_Boxer_")
            for x in deps(d):  # базы набора нормализации, которые бокс не ищет
                if asset_class(x) == "PoseSearchDatabase" and x not in kept_db:
                    NULL.setdefault(x, None)
        elif k in ANIM_KINDS:
            anim_src.add(d)
for d in deps(SRC_ABP):  # blend space'ы AnimBP (прицел, наклон) — на клипы стойки
    if asset_class(d) in ("BlendSpace", "BlendSpace1D"):
        COPY[d] = LOCAL + "/Seq/" + d.rsplit("/", 1)[1]
        anim_src.update(x for x in deps(d) if asset_class(x) in ANIM_KINDS)
names = {}
for s in sorted(anim_src):
    n = s.rsplit("/", 1)[1]
    names[n] = names.get(n, 0) + 1
    COPY[s] = LOCAL + "/Seq/" + (n if names[n] == 1 else "%s_%d" % (n, names[n]))
# клипы шлют нотифаи на базы стейт-машины PSD_SM_* (и др.) — их в копиях обнуляем
for s in sorted(anim_src):
    for d in deps(s):
        if asset_class(d) == "PoseSearchDatabase" and d not in kept_db:
            NULL.setdefault(d, None)
for k in list(NULL):
    NULL.pop(k) if k in COPY else None
log("копируется: баз %d, клипов/blend space %d, наборов нормализации %d; обнуляется ссылок на %d ассетов" % (
    len(kept_db), sum(1 for k in COPY if asset_class(k) in ANIM_KINDS),
    sum(1 for k in COPY if asset_class(k) == "PoseSearchNormalizationSet"), len(NULL)))

copies = {}
# Сначала клипы и blend space'ы (сжать и сохранить), потом базы: сохранение копии базы строит индекс поиска по
# клипам, и несжатый свежий клип роняет редактор (ассерт bEnforceCompressedDataSampling).
null_pairs = [(unreal.load_asset(s), None) for s in NULL]
done = 0
pairs_all = []
changed = set()
for stage in ("anim", "db"):
    batch = sorted((s, d) for s, d in COPY.items() if (asset_class(s) in ANIM_KINDS) == (stage == "anim"))
    for src, dst in batch:
        copies[src] = dup(src, dst)
        done += 1
        if done % 50 == 0:
            log("  копий %d/%d" % (done, len(COPY)))
    pairs_all = [(src_abp_c, abp_c)] + cht_pairs + [(unreal.load_asset(s), c) for s, c in copies.items()] + null_pairs
    for src, _ in batch:
        replace(copies[src], pairs_all, "→ копии / лишние базы → null")
    if stage == "anim":
        # Нотифай «PoseSearchBranchIn» (вход в базу стейт-машины PSD_SM_*) после обнуления базы пуст — индекс базы
        # ругается в лог («improperly setup … null Database»); стейт-машина выключена — нотифай убираем.
        nb = 0
        changed = set()
        for src, _ in batch:
            c = copies[src]
            if not isinstance(c, unreal.AnimSequence):
                continue
            ev = [e.get_editor_property("notify_state_class") for e in unreal.AnimationLibrary.get_animation_notify_events(c)]
            bi = [s for s in ev if s is not None and s.get_class().get_name() == "AnimNotifyState_PoseSearchBranchIn"]
            if bi and all(s.get_editor_property("database") is None for s in bi):
                unreal.AnimationLibrary.remove_animation_notify_events_by_name(c, "PoseSearchBranchIn")
                nb += 1
                changed.add(src)
        log("убран пустой PoseSearchBranchIn в клипах: %d" % nb)
        log("сжатие клипов-копий: %d" % T.finish_anim_compression([copies[s] for s, _ in batch]))
    for src, dst in batch:
        save(dst, force=src in changed)  # правка нотифаев не помечает пакет изменённым

for src, dst in CHT.items():
    replace(chts[src], pairs_all, "контекст, вложенные chooser'ы и базы → копии, лишние базы → null")
    STATS[dst.rsplit("/", 1)[1]] = sorted(d.rsplit("/", 1)[1] for d in deps(src) if d in kept_db)
    save(dst)

replace(abp, pairs_all, "chooser'ы и blend space'ы → копии")
compile_bp(abp)
# Вход контекста у узлов Evaluate Chooser назван по классу AnimBP контекста: у копий он ABP_Boxer_C — старый
# контакт осиротел, связи (Self) переносятся на новый.
for s in T.relink_orphan_pins(abp):
    log("ABP_Boxer: контакт " + s)
compile_bp(abp)
save(ABP)

# ---------------------------------------------------------------------------------------------------------------
# 3. Персонажи
# ---------------------------------------------------------------------------------------------------------------
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
SDL = unreal.SubobjectDataBlueprintFunctionLibrary


def components(bp):
    out = {}
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        d = SDL.get_data(h)
        obj = SDL.get_associated_object(d) if hasattr(SDL, "get_associated_object") else SDL.get_object(d)
        if obj is not None:
            out[str(SDL.get_variable_name(d))] = (h, obj)
    return out


def slim_bp(path):
    if not eal.does_asset_exist(path):
        log("нет %s — пропуск" % path)
        return
    bp = unreal.load_asset(path)
    comps = components(bp)
    root = sds.k2_gather_subobject_data_for_blueprint(bp)[0]
    vom_c = gen_class(SRC_VOM)
    swaps = [(gen_class(SRC_PRE), gen_class(PRE)), (gen_class(SRC_TRAV), gen_class(TRAV))]
    for name, (h, obj) in comps.items():
        if obj.get_class() == vom_c:
            n = sds.delete_subobjects(root, [h], bp)
            log("%s: удалён компонент %s (AC_VisualOverrideManager): %s" % (path, name, n))
    for name, (h, obj) in components(bp).items():
        for old_c, new_c in swaps:
            if obj.get_class() == old_c:
                # Класс компонента меняется на месте: удаление+добавление через SubobjectDataSubsystem рвёт связи
                # узлов графа, читающих переменную компонента (AddDelegate теряет Target → ошибка компиляции).
                if not T.swap_component_class(bp, name, new_c):
                    raise RuntimeError("%s: не сменить класс компонента %s" % (path, name))
                log("%s: компонент %s → %s" % (path, name, new_c.get_name()))
    replace(bp, swaps + [(src_abp_c, abp_c)], "компоненты/AnimBP → копии")
    compile_bp(bp)
    gen = gen_class(path)
    cdo = unreal.get_default_object(gen)
    mesh = cdo.get_editor_property("mesh")
    mesh.set_editor_property("anim_class", abp_c)
    save(path)
    # Проверка по памяти (реестр в коммандлете обновляется не сразу): ссылок на оригиналы не осталось?
    bad = T.find_references_in_asset(bp, [vom_c, src_abp_c, gen_class(SRC_CMC)] + [o for o, _ in swaps])
    log("%s: AnimClass %s; ссылок на оригиналы GASP: %d" % (
        path, mesh.get_editor_property("anim_class").get_name(), len(bad)))
    for s in bad:
        log("   ! " + s)


# LOCO_SKIP_BPS=1 — только ассеты Loco (проверить компиляцию AnimBP, не трогая BP_Boxer, которым пользуются другие).
if os.environ.get("LOCO_SKIP_BPS") != "1":
    for p in BPS:
        slim_bp(p)

for k, kept in STATS.items():
    log("%s: базы оставлены (%d) %s" % (k, len(kept), kept))
log("готово")
