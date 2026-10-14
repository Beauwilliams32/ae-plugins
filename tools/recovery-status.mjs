#!/usr/bin/env node
// Generate RECOVERY-STATUS.md: what is recovered, effect by effect, MEASURED.
//
// Every number in that document comes from `tools/codegen-diff.mjs` — the tool that
// compares the recovered source against the shipped binary — or from the bundle's own
// symbol table. Nothing is copied from a README, because the point of the document is to
// be the one place a reader can trust without re-deriving it.
//
//   node tools/recovery-status.mjs            # writes RECOVERY-STATUS.md
//   node tools/recovery-status.mjs --check    # exits non-zero if the file is stale
//
// `test/status.test.mjs` runs it with --check, so the document cannot drift from the
// measurement the way a hand-written table does.

import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import path from "node:path";

import { BUNDLES, bundlePath } from "./namespaces.mjs";

const ROOT = path.join(import.meta.dirname, "..");
const SPECS = path.join(ROOT, "specs");
const OUT = path.join(ROOT, "RECOVERY-STATUS.md");

/** Every effect in the fleet, from the recovered specifications. */
function fleet() {
  return readdirSync(SPECS)
    .filter((f) => f.endsWith(".json") && f !== "index.json")
    .map((f) => f.slice(0, -".json".length))
    .sort();
}

/** dir -> plugin, inverted, so a plugin can find its recovered namespace. */
function dirFor(plugin) {
  for (const [dir, plug] of Object.entries(BUNDLES)) {
    if (plug === plugin && existsSync(path.join(ROOT, "src", dir, `${dir}.h`))) return dir;
  }
  return null;
}

/**
 * One line per symbol, from the differ.
 *
 * The differ is the source of truth because it is the only thing here that compares the
 * recovered source against the ACTUAL bundle. Its output shapes are:
 *   `  MATCH   ns::name  N instructions, identical`
 *   `  DIFFER  ns::name  shipped=N mine=M`
 *   `  absent  ns::name (not implemented — N instrs in the bundle)`
 */
function measure(plugin) {
  const dir = dirFor(plugin);
  const binary = bundlePath(plugin);
  if (!dir || !binary) {
    return { dir, symbols: [], identical: 0, differing: 0, absent: 0, bundleInstrs: 0, recoveredInstrs: 0 };
  }

  let out;
  try {
    out = execFileSync("node", [path.join(ROOT, "tools", "codegen-diff.mjs"), dir],
      { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    // The tool exits non-zero when it finds differences, which is not an error here.
    out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }

  const symbols = [];
  for (const line of out.split("\n")) {
    let m = line.match(/^\s{2}MATCH\s+\S+::(\S+)\s+(\d+) instructions, identical/);
    if (m) { symbols.push({ name: m[1], status: "identical", instrs: Number(m[2]) }); continue; }
    m = line.match(/^\s{2}DIFFER\s+\S+::(\S+)\s+shipped=(\d+) mine=(\d+)/);
    if (m) { symbols.push({ name: m[1], status: "differing", instrs: Number(m[2]) }); continue; }
    m = line.match(/^\s+absent\s+\S+::(\S+)\s+\(not implemented — (\d+) instrs/);
    if (m) { symbols.push({ name: m[1], status: "absent", instrs: Number(m[2]) }); continue; }
  }

  const identical = symbols.filter((s) => s.status === "identical").length;
  const differing = symbols.filter((s) => s.status === "differing").length;
  const absent = symbols.filter((s) => s.status === "absent").length;
  const bundleInstrs = symbols.reduce((n, s) => n + s.instrs, 0);
  const recoveredInstrs = symbols.filter((s) => s.status !== "absent").reduce((n, s) => n + s.instrs, 0);
  return { dir, symbols, identical, differing, absent, bundleInstrs, recoveredInstrs };
}

/** A recovered namespace's source, for the file counts in the document. */
function sourceStats(dir) {
  if (!dir) return { files: 0, lines: 0 };
  const base = path.join(ROOT, "src", dir);
  const files = readdirSync(base).filter((f) => !f.startsWith("."));
  const lines = files.reduce((n, f) => {
    const p = path.join(base, f);
    return n + readFileSync(p, "utf8").split("\n").length;
  }, 0);
  return { files: files.length, lines };
}

function report() {
  const effects = fleet().map((plugin) => {
    const m = measure(plugin);
    return { plugin, ...m, source: sourceStats(m.dir) };
  });
  const totals = effects.reduce((acc, e) => ({
    identical: acc.identical + e.identical,
    differing: acc.differing + e.differing,
    absent: acc.absent + e.absent,
    bundleInstrs: acc.bundleInstrs + e.bundleInstrs,
    recoveredInstrs: acc.recoveredInstrs + e.recoveredInstrs,
    lines: acc.lines + e.source.lines,
  }), { identical: 0, differing: 0, absent: 0, bundleInstrs: 0, recoveredInstrs: 0, lines: 0 });
  return { effects, totals };
}

function render({ effects, totals }) {
  const recovered = effects.filter((e) => e.dir);
  const unopened = effects.filter((e) => !e.dir);
  const pct = (n, d) => (d === 0 ? "—" : `${((100 * n) / d).toFixed(1)}%`);

  const rows = effects.map((e) => {
    const symbols = e.identical + e.differing + e.absent;
    const recoveredSymbols = e.identical + e.differing;
    const namespace = e.dir ? `\`${e.dir}::\`` : "—";
    return `| ${e.plugin} | ${namespace} | ${recoveredSymbols}/${symbols} | ${e.identical} | `
         + `${e.differing} | ${e.absent} | ${e.bundleInstrs === 0 ? "—" : `${e.recoveredInstrs}/${e.bundleInstrs}`} | `
         + `${pct(e.recoveredInstrs, e.bundleInstrs)} |`;
  }).join("\n");

  return `# Recovery status

GENERATED by \`tools/recovery-status.mjs\`. **Do not edit.** \`test/status.test.mjs\` re-runs
the generator and fails if this file no longer matches the measurement, because a
hand-written coverage table goes stale silently and this one is the document a reader is
most likely to trust without re-deriving it.

Every figure is measured, and the measurement is \`tools/codegen-diff.mjs\` — the tool that
compiles the recovered source and compares each function's instructions against the
SHIPPED BUNDLE. "Recovered" therefore means "transcribed", never "looks right":
a function can be recovered and still differ from the binary in its instruction schedule.

## The fleet

| | |
|---|---|
| Effects in the fleet | ${effects.length} |
| With a recovered source namespace | ${recovered.length} |
| Without one | ${unopened.length} |
| Named symbols recovered | ${totals.identical + totals.differing} of ${totals.identical + totals.differing + totals.absent} |
| — instruction-identical to the shipped binary | ${totals.identical} |
| — recovered but differing in codegen | ${totals.differing} |
| Authored instructions recovered | ${totals.recoveredInstrs} of ${totals.bundleInstrs} (${pct(totals.recoveredInstrs, totals.bundleInstrs)}) |
| Lines of recovered source | ${totals.lines} |

## Per effect

\`Symbols\` counts the \`namespace::\` symbols the differ compares, which is not always the
count of authored functions: a template instantiation is one symbol per instantiation, and
a class member is named after its class.

| Effect | Namespace | Symbols | Identical | Differing | Absent | Instructions | Recovered |
|---|---|---|---|---|---|---|---|
${rows}

## What "absent" means

The differ reports a symbol as absent when the shipped bundle defines it and the recovered
source does not. Every one of those is a function that has NOT been transcribed. They are
absent rather than stubbed: a stub would make a namespace look complete, and
\`test/codegen.test.mjs\` asserts the completeness marker and the measurement agree in both
directions.

## What this document does NOT measure

- **Whether After Effects loads a rebuilt plugin.** No AE host has ever loaded one. Two
  effects build into loadable bundles (\`tools/build-plugin.sh\`), and the build compares
  the rebuilt bundle against the shipped one field by field — architectures, \`PkgInfo\`,
  the ad-hoc signature and its \`Identifier\`, sealed resources, every \`Info.plist\` key —
  but that is structural fidelity, not host acceptance. Installing into
  \`/Applications/Adobe After Effects 2026/Plug-ins/\` needs admin rights.
- **Whether any recovered function is CORRECT**, beyond its own self-check and, where it
  exists, instruction-identity. Instruction-identity is the strongest evidence here and it
  is not proof: codegen can coincide for different sources, which is why every namespace
  also carries a self-check with assertions anchored to the disassembly.
- **The effects without a namespace** are measured only through their specifications
  (\`specs/*.json\`, \`PARAMETERS.md\`, \`CAPABILITIES.md\`), which ARE complete for all
  sixteen. Their DSP is machine code.
`;
}

const { effects, totals } = report();
const text = render({ effects, totals });

if (process.argv.includes("--check")) {
  const current = existsSync(OUT) ? readFileSync(OUT, "utf8") : "";
  if (current !== text) {
    console.error("RECOVERY-STATUS.md is STALE — run: node tools/recovery-status.mjs");
    process.exit(1);
  }
  console.log("RECOVERY-STATUS.md matches the measurement");
} else {
  writeFileSync(OUT, text);
  console.log(`wrote RECOVERY-STATUS.md: ${effects.length} effects, `
    + `${totals.identical + totals.differing} of `
    + `${totals.identical + totals.differing + totals.absent} symbols recovered`);
}
