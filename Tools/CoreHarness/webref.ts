// S-57: эталон веба для харнесса — доля побед фаворита / досрочек / нокдауны за бой на парах interactive-parity.test.ts
// (+2 профи-пары): simulateFight и интерактив веба (автопилот, раунд 55 с, dt 0.05, фаворит через бой в синем углу).
// Пишет WebRef.inc (профили бойцов уже с проекцией веса fightProfile + цифры веба) — его читает sim_main.cpp.
// Запуск (web рядом: ..\boxing\web; тесты веба — Node 22):
//   cd <boxing>\web && npx -y -p node@22.23.2 node node_modules/vite-node/vite-node.mjs <boxing-ue>\Tools\CoreHarness\webref.ts
// Переменные: N (боёв на пару, 400), OUT (путь .inc).
import { Boxer } from "../../../boxing/web/src/engine/boxer";
import { simulateFight } from "../../../boxing/web/src/engine/simulate";
import { InteractiveFight } from "../../../boxing/web/src/engine/interactive";
import { writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

type St = "technical" | "volume" | "puncher" | "pressure" | "counter" | "speed" | "balanced";
const mk = (base: number, style: St = "balanced", weight = 75, gender: "M" | "F" = "M", height = 180) =>
  Boxer.createCustom({ name: `x${base}${style}${weight}`, gender, weight_kg: weight, age: 27, height_cm: height, style, region: "x", base });
const seedOf = (i: number) => (i * 2654435761 + 17) >>> 0;
const PAIRS = [
  { name: "равные 70/70, 3р", a: mk(70), b: mk(70), ring: 75, rounds: 3 },
  { name: "лёгкий перевес 76/70, 3р", a: mk(76), b: mk(70), ring: 75, rounds: 3 },
  { name: "крупный перевес 88/62, 3р", a: mk(88), b: mk(62), ring: 75, rounds: 3 },
  { name: "панчер 82 vs технарь 78, 3р", a: mk(82, "puncher"), b: mk(78, "technical"), ring: 75, rounds: 3 },
  { name: "технарь 82 vs панчер 78, 3р", a: mk(82, "technical"), b: mk(78, "puncher"), ring: 75, rounds: 3 },
  { name: "прессинг 78 vs технарь 74, 3р", a: mk(78, "pressure"), b: mk(74, "technical"), ring: 75, rounds: 3 },
  { name: "сгонка 81→75 vs натуральный 75", a: mk(74, "balanced", 81), b: mk(74, "balanced", 75), ring: 75, rounds: 3 },
  { name: "женщины 72/68, 3р", a: mk(72, "speed", 60, "F", 168), b: mk(68, "pressure", 60, "F", 168), ring: 60, rounds: 3 },
  { name: "равные 75/75, 12р", a: mk(75), b: mk(75), ring: 75, rounds: 12 },
  { name: "объём 74 vs панчер 74, 10р", a: mk(74, "volume"), b: mk(74, "puncher"), ring: 75, rounds: 10 },
  { name: "прессинг 80 vs контра 74, 12р", a: mk(80, "pressure"), b: mk(74, "counter"), ring: 75, rounds: 12 },
  { name: "крупный перевес 90/64, 12р", a: mk(90, "puncher"), b: mk(64), ring: 75, rounds: 12 },
  { name: "панчер 88 vs объём 70, 6р", a: mk(88, "puncher"), b: mk(70, "volume"), ring: 75, rounds: 6 },
  { name: "прессинг 72 vs 72, 4р", a: mk(72, "pressure"), b: mk(72), ring: 75, rounds: 4 },
];
const N = Number(process.env.N ?? 400);
const STYLE: Record<St, string> = {
  technical: "Technical", volume: "Volume", puncher: "Puncher", pressure: "Pressure", counter: "Counter", speed: "Speed", balanced: "Balanced",
};
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const prof = (x: any) => {
  const s = x.stats;
  const f = (v: number) => `${+v.toFixed(4)}`;
  return `{{${[s.power, s.hand_speed, s.footwork, s.stamina, s.chin, s.technique, s.defense].map(f).join(", ")}}, ${f(x.reach)}, ${f(x.ringWeight)}, ` +
    `${f(x.massForPower)}, ${f(x.durabilityMass)}, ${f(x.seasoning)}, EBoxStyle::${STYLE[x.style as St]}}`;
};
const lines: string[] = [];
for (const p of PAIRS) {
  const pro = p.rounds > 3;
  const A = p.a.fightProfile(p.ring, pro), B = p.b.fightProfile(p.ring, pro);
  const sim = { w: 0, stop: 0, kd: 0 }, ia = { w: 0, stop: 0, kd: 0 };
  for (let i = 0; i < N; i++) {
    const seed = seedOf(i);
    const r = simulateFight(A, B, { rounds: p.rounds, seed, allowDraw: pro });
    sim.w += r.winnerIndex === 0 ? 1 : 0;
    sim.stop += r.method === "KO" || r.method === "RSC" ? 1 : 0;
    sim.kd += r.knockdowns[0] + r.knockdowns[1];
    const swap = i % 2 === 1;
    const f = new InteractiveFight(swap ? B : A, swap ? A : B, { rounds: p.rounds, seed, allowDraw: pro, autopilot: true });
    for (let k = 0; k < 2000000; k++) {
      const s = f.snapshot();
      if (s.phase === "over") break;
      if (s.phase === "between") f.proceed();
      else f.tick(0.05);
    }
    const ri = f.snapshot().result!;
    let w = ri.winnerIndex;
    if (swap && w !== null) w = (1 - w) as 0 | 1;
    ia.w += w === 0 ? 1 : 0;
    ia.stop += ri.method === "KO" || ri.method === "RSC" ? 1 : 0;
    ia.kd += ri.knockdowns[0] + ri.knockdowns[1];
  }
  const v = (o: typeof sim) => `${(o.w / N).toFixed(4)}, ${(o.stop / N).toFixed(4)}, ${(o.kd / N).toFixed(4)}`;
  lines.push(`\t{"${p.name}", ${p.rounds}, ${pro}, ${prof(A)}, ${prof(B)}, {${v(sim)}}, {${v(ia)}}},`);
  console.log(p.name.padEnd(34), "sim", v(sim), "| web", v(ia));
}
const here = dirname(fileURLToPath(import.meta.url));
const out =
  `// СГЕНЕРИРОВАНО Tools/CoreHarness/webref.ts (S-57) — не править руками. N = ${N} боёв на пару.\n` +
  `// {имя, раунды, профи, A, B, simulate {победы A, досрочки, нокдауны/бой}, интерактив веба {то же}}\n` +
  `// Профиль: {статы P/HS/FW/St/Chin/Tech/Def}, размах, вес, massForPower, durabilityMass, seasoning, стиль.\n` +
  lines.join("\n") + "\n";
writeFileSync(process.env.OUT ?? join(here, "WebRef.inc"), out);
