// Экспорт ростера web → UE (S-55): node Tools/RosterExport/export.mjs [--web <путь к boxing/web>] [--out <файл>]
// Бандлит entry.ts esbuild'ом из node_modules веба (алиас @web → <web>/src), импортирует и пишет
// Content/Boxing/Data/Roster.json. Статы — выведенные ДВИЖКОМ WEB (stats.ts/boxer.ts), не копия формул.
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { existsSync, mkdirSync, writeFileSync, rmSync } from "node:fs";

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const arg = (k, d) => {
  const i = args.indexOf(k);
  return i >= 0 && args[i + 1] ? args[i + 1] : d;
};
const web = resolve(arg("--web", process.env.BOXING_WEB ?? join(here, "../../../boxing/web")));
const out = resolve(arg("--out", join(here, "../../Content/Boxing/Data/Roster.json")));
if (!existsSync(join(web, "src/engine/boxer.ts"))) {
  console.error(`Не найден движок web: ${web} (укажи --web или BOXING_WEB)`);
  process.exit(1);
}
const require = createRequire(join(web, "package.json"));
const esbuild = require("esbuild");

const tmp = join(here, ".bundle.mjs");
await esbuild.build({
  entryPoints: [join(here, "entry.ts")],
  bundle: true,
  format: "esm",
  platform: "node",
  outfile: tmp,
  alias: { "@web": join(web, "src") },
  logLevel: "warning",
});
const { buildRoster } = await import(pathToFileURL(tmp).href + `?t=${Date.now()}`);
rmSync(tmp, { force: true });

const data = buildRoster();
const doc = {
  source: "boxing/web (движок web/src/engine: stats.ts + boxer.ts, ростеры web/src/data)",
  generated: new Date().toISOString(),
  count: data.boxers.length,
  boxers: data.boxers,
};
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, JSON.stringify(doc, null, 1) + "\n", "utf8");
const by = (k) => data.boxers.filter((b) => b.kind === k).length;
console.log(`Ростер: ${data.boxers.length} бойцов (любители ${by("amateur")}, профи ${by("pro")}, легенды ${by("legend")}) → ${out}`);
