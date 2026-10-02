// S-61: выборка ростера UE для харнесса — пары плейтеста QA и профи-пары по дивизионам (калибровка досрочек).
// Читает Content/Boxing/Data/Roster.json (экспорт веба), пишет RosterRef.inc — его включает sim_main.cpp.
//   node Tools/CoreHarness/rosterref.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const roster = JSON.parse(readFileSync(join(here, "../../Content/Boxing/Data/Roster.json"), "utf8")).boxers;
const STYLE = { technical: "Technical", volume: "Volume", puncher: "Puncher", pressure: "Pressure", counter: "Counter", speed: "Speed", balanced: "Balanced" };
const KIND = { amateur: 0, pro: 1, legend: 2 };
const st = (s) => [s.power, s.handSpeed, s.footwork, s.stamina, s.chin, s.technique, s.defense].join(", ");
// Чемпион мира — как FRosterBoxer::IsWorldChampion: WBC/WBA/IBF/WBO в бейдже без «#»/«интерим»/«рег.»/«топ»/«экс»/«Ring».
const isChamp = (b) => !!b.badge && !["#", "интерим", "рег.", "топ", "экс", "ring"].some((x) => b.badge.toLowerCase().includes(x)) && /WBC|WBA|IBF|WBO/.test(b.badge);

const used = [];
const idx = new Map();
const ref = (b) => {
  if (!idx.has(b.id)) {
    idx.set(b.id, used.length);
    used.push(b);
  }
  return idx.get(b.id);
};
const byName = (n) => roster.find((b) => b.name.includes(n));

// Пары плейтеста QA (bal.sh): красный — бот, синий — ИИ.
const QA = [["Ташкенбай", "Бибосинов"], ["Оралбай", "Крус"], ["Крус", "Муандзе"], ["Шилдс", "Маршалл"], ["Головкин", "Тайсон"], ["Гадфа", "Вейтия"], ["Балкибекова", "Кызайбай"]];
const qa = QA.map(([r, b]) => `\t{"${r}–${b}", ${ref(byName(r))}, ${ref(byName(b))}},`);

// Профи-пары по дивизионам: топ дивизиона по уровню, соседи (близкие) и через одного/двух (перевес).
const pros = roster.filter((b) => b.kind === "pro");
const divs = new Map();
for (const b of pros) {
  const k = `${b.gender} ${b.weightKg}`;
  if (!divs.has(k)) divs.set(k, []);
  divs.get(k).push(b);
}
const pairs = [];
for (const [k, list] of [...divs.entries()].sort()) {
  const top = list.sort((a, b) => b.proOverall - a.proOverall).slice(0, 10);
  const P = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [0, 4], [2, 6], [1, 7], [3, 8]];
  for (const [i, j] of P) {
    if (i >= top.length || j >= top.length) continue;
    const a = top[i], b = top[j];
    const rounds = isChamp(a) && isChamp(b) ? 12 : 10;
    pairs.push(`\t{"${a.gender} ${a.division} ${a.weightKg}", ${ref(a)}, ${ref(b)}, ${rounds}},`);
  }
}

const proW = (g) => [...new Set(roster.filter((b) => b.kind !== "amateur" && b.gender === g).map((b) => b.weightKg))].sort((x, y) => x - y);
const boxers = used.map((b) =>
  `\t{"${b.name}", ${b.gender === "F" ? "true" : "false"}, ${KIND[b.kind]}, ${b.weightKg}, ${b.reachCm}, {${st(b.stats)}}, {${st(b.proStats)}}, ` +
  `${b.proSeasoning}, ${b.proOverall}, EBoxStyle::${STYLE[b.style]}},`);
const out = [
  "// СГЕНЕРИРОВАНО Tools/CoreHarness/rosterref.mjs (S-61) из Content/Boxing/Data/Roster.json — не править руками.",
  "#define ROSTER_REF_BOXERS \\",
  ...boxers.map((l) => l + " \\"),
  "",
  "#define ROSTER_REF_QA \\",
  ...qa.map((l) => l + " \\"),
  "",
  "#define ROSTER_REF_PRO_PAIRS \\",
  ...pairs.map((l) => l + " \\"),
  "",
  `#define ROSTER_REF_PRO_CLASSES_M ${proW("M").join(", ")}`,
  `#define ROSTER_REF_PRO_CLASSES_F ${proW("F").join(", ")}`,
  "",
];
writeFileSync(join(here, "RosterRef.inc"), out.join("\n"));
console.log(`RosterRef.inc: бойцов ${used.length}, пар QA ${qa.length}, профи-пар ${pairs.length}`);
