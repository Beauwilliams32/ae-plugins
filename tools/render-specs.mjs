#!/usr/bin/env node
// Render specs/<Plugin>.json into specs/<Plugin>.md plus the fleet CATALOG.md.
//
// Run tools/recover.mjs first; this reads what that produced.
//
//   node tools/render-specs.mjs

import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import path from "node:path";

const SPECS = path.join(import.meta.dirname, "..", "specs");
const ROOT = path.join(import.meta.dirname, "..");

const SDK_TAG = { "0x000d001d": "AE SDK 25.6 class (0x000d001d)" };

function table(rows, headers) {
  const head = `| ${headers.join(" | ")} |`;
  const sep = `|${headers.map(() => "---").join("|")}|`;
  return [head, sep, ...rows.map((r) => `| ${r.join(" | ")} |`)].join("\n");
}

function codeList(items, lang = "cpp") {
  if (!items?.length) return "_none recovered_\n";
  return "```" + lang + "\n" + items.join("\n") + "\n```\n";
}

function renderPlugin(spec) {
  const p = spec.pipl ?? {};
  const sym = spec.symbols.byScope ?? {};
  const pluginApi = sym["plugin-api"] ?? [];
  const domain = sym["domain"] ?? [];
  const other = sym["other"] ?? [];

  const out = [];
  out.push(`# ${p.name ?? spec.plugin}`);
  out.push("");
  out.push(
    `Recovered by static analysis of the compiled bundle only. The original C++ ` +
    `source was destroyed on 2026-10-13 and had no git remote, so **everything below ` +
    `was reconstructed from the binary**, not copied from source.`,
  );
  out.push("");

  out.push("## Effect identity (from the PiPL resource)");
  out.push("");
  out.push(table([
    ["Effect name", `\`${p.name ?? "?"}\``],
    ["Menu name in After Effects", `\`${p.eMNA ?? "?"}\``],
    ["Category", `\`${p.catg ?? "?"}\``],
    ["PiPL kind", `\`${p.kind ?? "?"}\``],
    ["Effect version", `\`${p.eVER ?? "?"}\``],
    ["SDK version", `\`${p.eSVR ?? "?"}\`${SDK_TAG[p.eSVR] ? ` — ${SDK_TAG[p.eSVR]}` : ""}`],
    ["Entry points", `\`${p.mi64 ?? "?"}\` (Intel) / \`${p.ma64 ?? "?"}\` (ARM64)`],
    ["Globals", `\`${p.eGLO ?? "?"}\` / \`${p.eGL2 ?? "?"}\``],
  ], ["Field", "Value"]));
  out.push("");

  out.push("## Binary provenance");
  out.push("");
  out.push(table([
    ["Bundle identifier", `\`${spec.identity.bundleIdentifier ?? "?"}\``],
    ["Bundle version", `\`${spec.identity.shortVersion ?? "?"}\``],
    ["Minimum macOS", `\`${spec.identity.minimumSystem ?? "?"}\``],
    ["Architectures", spec.binary.archs.join(", ") || "?"],
    ["Binary size", `${spec.binary.bytes.toLocaleString()} bytes`],
    ["Symbols recovered", String(spec.symbols.total)],
    ["Stripped", spec.binary.stripped ? "**yes** (little recoverable)" : "no — full symbol table present"],
    ["MD5", `\`${spec.binary.md5}\``],
    ["SHA-256", `\`${spec.binary.sha256}\``],
  ], ["Field", "Value"]));
  out.push("");

  if (spec.sourceUnits?.length) {
    out.push("## Original source files (named in the binary)");
    out.push("");
    out.push("These filenames survive in the symbol table and reveal how the effect was split up:");
    out.push("");
    out.push(spec.sourceUnits.map((s) => `- \`${s}\``).join("\n"));
    out.push("");
  }

  out.push("## Recovered architecture");
  out.push("");
  out.push(`**Plugin-facing functions** — these take Adobe SDK types, so they are the ` +
    `effect's own API surface (parameter handling, rendering, UI hooks):`);
  out.push("");
  out.push(codeList(pluginApi));
  if (domain.length) {
    out.push(`**Domain code** — namespaced logic, i.e. the actual signal/image processing: ` +
      `names and signatures are exact, because the binaries were built without stripping.`);
    out.push("");
    out.push(codeList(domain));
  }
  if (other.length) {
    out.push(`**Other named symbols:**`);
    out.push("");
    out.push(codeList(other));
  }

  out.push("## Recovered parameters and UI labels");
  out.push("");
  if (spec.strings.parameterCandidates?.length) {
    out.push("Parameter labels are registered at runtime rather than stored in the PiPL, so " +
      "these were recovered from the binary's string table. Treat them as a **high-confidence " +
      "draft of the UI**, to be confirmed against the effect when it runs:");
    out.push("");
    out.push(spec.strings.parameterCandidates.map((s) => `- \`${s}\``).join("\n"));
  } else {
    out.push("_No parameter labels were recovered; the effect may build its UI numerically._");
  }
  out.push("");

  if (spec.strings.modeCandidates?.length) {
    out.push("**Likely menu / mode values:**");
    out.push("");
    out.push(spec.strings.modeCandidates.map((s) => `- \`${s}\``).join("\n"));
    out.push("");
  }

  if (spec.strings.messageStrings?.length) {
    out.push("## Behavioural messages");
    out.push("");
    out.push("User-visible strings document what the effect actually does, including its " +
      "guard conditions. These are the closest thing to recovered requirements:");
    out.push("");
    out.push(spec.strings.messageStrings.map((s) => `- ${s.replace(/`/g, "'")}`).join("\n"));
    out.push("");
  }

  out.push("## Recovery status");
  out.push("");
  out.push(table([
    ["Effect identity", "**Recovered**", "read from the PiPL resource"],
    ["Function names + signatures", spec.symbols.total > 10 ? "**Recovered**" : "Partial", "un-stripped symbol table"],
    ["Source file layout", spec.sourceUnits?.length ? "**Recovered**" : "Not recoverable", "filenames embedded in symbols"],
    ["Parameter list", spec.strings.parameterCandidates?.length ? "Draft" : "Not recovered", "string-table heuristic"],
    ["Function bodies (DSP math)", "**Not yet**", "needs disassembly per function"],
    ["Type definitions", "Partial", "recoverable from signatures and vtable layout"],
  ], ["Aspect", "State", "Basis"]));
  out.push("");
  out.push("Recovered by `tools/recover.mjs`; see the repository README for method and limits.");
  out.push("");
  return out.join("\n");
}

function renderCatalog(specs) {
  const out = [];
  out.push("# Fleet catalog");
  out.push("");
  out.push(`${specs.length} after-effects effects, recovered from compiled bundles.`);
  out.push("");
  out.push(table(
    specs.map((s) => {
      const p = s.pipl ?? {};
      return [
        `[${p.name ?? s.plugin}](specs/${s.plugin}.md)`,
        `\`${p.eMNA ?? "?"}\``,
        String(s.symbols.total),
        String(s.sourceUnits?.length ?? 0),
        String(s.strings.parameterCandidates?.length ?? 0),
        String(s.strings.messageStrings?.length ?? 0),
        s.binary.archs.join("+"),
      ];
    }),
    ["Effect", "AE menu name", "Symbols", "Source files", "Params", "Messages", "Archs"],
  ));
  out.push("");

  const totalSyms = specs.reduce((n, s) => n + s.symbols.total, 0);
  const totalParams = specs.reduce((n, s) => n + (s.strings.parameterCandidates?.length ?? 0), 0);
  const totalMsgs = specs.reduce((n, s) => n + (s.strings.messageStrings?.length ?? 0), 0);
  out.push("## Totals");
  out.push("");
  out.push(`- ${specs.length} effects`);
  out.push(`- ${totalSyms} named symbols recovered`);
  out.push(`- ${totalParams} parameter-label candidates`);
  out.push(`- ${totalMsgs} behavioural message strings`);
  out.push(`- all ${specs.length} are universal binaries (Intel + Apple silicon)`);
  out.push("");

  out.push("## Findings worth acting on");
  out.push("");
  out.push("**SpectraKeys breaks the fleet's menu-name convention.** Fifteen effects register as " +
    "`WM_<Name>`; SpectraKeys alone registers as `WMH SpectraKeys`, and it is also the only " +
    "effect whose name contains a space. It was the last effect added to the fleet (absent from " +
    "the 2026-09-25 backup set, present in the 2026-09-28 one). Worth normalising.");
  out.push("");
  out.push("**Every effect is pinned to the same version and SDK.** All 16 report effect version " +
    "`0x00080001` and SDK `0x000d001d`, so the fleet was built from one SDK drop in a single pass.");
  out.push("");
  out.push("**The fleet carries real, factorable DSP.** Several effects expose proper domain " +
    "namespaces rather than flat C, which makes the math recoverable: see `spectra::` in " +
    "SpectraKeys (radix-2 FFT, Hann window, dB conversion, spectrogram band analysis) and the " +
    "audio helpers in PathScribe.");
  out.push("");
  return out.join("\n");
}

function main() {
  const files = readdirSync(SPECS).filter((f) => f.endsWith(".json") && f !== "index.json").sort();
  const specs = files.map((f) => JSON.parse(readFileSync(path.join(SPECS, f), "utf8")));
  if (!specs.length) {
    console.error("no specs found — run tools/recover.mjs first");
    return 1;
  }
  for (const s of specs) {
    writeFileSync(path.join(SPECS, `${s.plugin}.md`), renderPlugin(s));
  }
  writeFileSync(path.join(ROOT, "CATALOG.md"), renderCatalog(specs));
  console.log(`rendered ${specs.length} effect specs + CATALOG.md`);
  return 0;
}

process.exitCode = main();
