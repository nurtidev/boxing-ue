// Точка входа экспорта ростера (S-55). Импортирует ДВИЖОК WEB как есть — статы выводятся
// той же формулой (stats.ts/boxer.ts: регалии → pedigree → base → стиль/возраст/reach), здесь
// ничего не пересчитывается. Собирается esbuild'ом из export.mjs (алиас @web → boxing/web/src).
import { Boxer } from "@web/engine/boxer";
import { STAT_NAMES } from "@web/engine/stats";
import { ALL_AMATEURS } from "@web/data/amateurRoster";
import { buildProRoster } from "@web/data/proRoster";
import { buildLegends } from "@web/data/legends";
import { accSummary, STYLE_LABELS } from "@web/ui/labels";

// Эмодзи-флаг → ISO-код ("🇰🇿" → "KZ"): шрифты UE эмодзи не рисуют.
function isoOf(flag: string): string {
  const cps = Array.from(flag).map((c) => c.codePointAt(0) ?? 0);
  if (cps.length === 2 && cps.every((c) => c >= 0x1f1e6 && c <= 0x1f1ff)) {
    return cps.map((c) => String.fromCharCode(c - 0x1f1e6 + 65)).join("");
  }
  return flag.includes("🏴") ? "GB" : "";
}

const FLAG_RE = /\p{Regional_Indicator}{2}|🏴[\u{E0061}-\u{E007A}\u{E007F}]*/gu;

function countryOf(b: Boxer): { code: string; country: string; city: string } {
  const flag = b.countryFlag();
  const code = isoOf(flag);
  const region = (b.region ?? "").trim();
  const hasFlag = FLAG_RE.test(region);
  FLAG_RE.lastIndex = 0;
  if (hasFlag) return { code, country: region.replace(FLAG_RE, "").trim(), city: "" };
  // Домашний ростер: region — город, страна — Казахстан.
  return { code: code || "KZ", country: "Казахстан", city: region };
}

const r1 = (x: number) => Math.round(x * 10) / 10;

function statsOf(s: Record<string, number>) {
  return {
    power: r1(s.power), handSpeed: r1(s.hand_speed), footwork: r1(s.footwork), stamina: r1(s.stamina),
    chin: r1(s.chin), technique: r1(s.technique), defense: r1(s.defense),
  };
}

function row(b: Boxer, kind: "amateur" | "pro" | "legend") {
  const c = countryOf(b);
  // Профи-проекция на своём весе (идея №11): для любителя с amateurBase — скидка на
  // необстрелянность; у профи/легенд совпадает с базовыми статами (seasoning 1).
  const pro = b.fightProfile(b.weight_kg, true);
  const overall = b.overall();
  const proOverall = r1(STAT_NAMES.reduce((a, s) => a + pro.stats[s], 0) / STAT_NAMES.length);
  return {
    id: `${kind}:${b.name}`,
    name: b.name,
    kind,
    gender: b.gender,
    countryCode: c.code,
    country: c.country,
    city: c.city,
    age: b.age,
    weightKg: b.weight_kg,
    division: b.division ?? "",
    heightCm: Math.round(b.height_cm),
    reachCm: Math.round(b.reach_cm),
    stance: b.stance ?? "",
    style: b.style,
    styleLabel: STYLE_LABELS[b.style],
    estimated: b.estimated,
    stats: statsOf(b.stats),
    overall,
    proStats: statsOf(pro.stats),
    proOverall,
    proSeasoning: r1(pro.seasoning * 100) / 100,
    badge: b.badge ?? "",
    proRecord: b.proRecord ?? "",
    accolades: kind === "amateur" ? accSummary(b.accolades) : "",
    notes: b.notes ?? "",
  };
}

export function buildRoster() {
  const amateurs = ALL_AMATEURS.map((d) => row(Boxer.fromData(d), "amateur"));
  const pros = buildProRoster().map((b) => row(b, "pro"));
  const legends = buildLegends().map((b) => row(b, "legend"));
  const seen = new Set<string>();
  const boxers = [...amateurs, ...pros, ...legends].filter((r) => !seen.has(r.id) && !!seen.add(r.id));
  return { boxers };
}
