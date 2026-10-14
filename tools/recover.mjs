#!/usr/bin/env node
// Recover the Williams Media AE plugin fleet from the only copies that survive:
// the compiled bundles installed in After Effects.
//
// The original C++ source was destroyed on 2026-10-13 and had no remote
// (see docs/INCIDENT-2026-10-13-accidental-project-deletion.md). The binaries
// are NOT stripped, so a great deal is mechanically recoverable:
//
//   PiPL resource  -> effect name, menu name, category, version, SDK version, flags
//   symbol table   -> the real C++ architecture: namespaces, types, function
//                     signatures (via demangling), and referenced source filenames
//   string table   -> parameter labels, units, mode names — i.e. the effect's UI spec
//
// This tool extracts all of that into specs/<Plugin>.json. It is read-only with
// respect to the installed bundles; it never modifies or copies them.
//
//   node tools/recover.mjs                     # every installed bundle
//   node tools/recover.mjs SpectraKeys         # one effect
//   node tools/recover.mjs --out /tmp/specs    # write elsewhere

import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync, readFileSync, readdirSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";

const PLUGINS_DIR = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media";

// Strings that come from the Adobe SDK rather than from this fleet. Filtering
// them out is what makes the remaining strings the *authored* UI spec.
const SDK_NOISE = [
  /^AEGP [A-Za-z ]*Suite\d*$/,
  /^AE Plugin Helper Suite/,
  /^PF_/,
  /^(ADBE|Adobe)\b/,
  /^artisan/i,
  /^SPBasic/,
  /^SuiteHandler$/,
  /^\d+\.\d+\.\d+$/,
  /^UTF-?8$/,
  /^[A-Za-z]+\.(cpp|h|hpp|c|mm|m)$/,
];

function sh(cmd, args) {
  try {
    return execFileSync(cmd, args, { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  } catch (err) {
    return err.stdout ?? "";
  }
}

/** Parse the PiPL. Adobe stores metadata as a flat run of 8BIM-tagged chunks. */
function parsePipl(rsrcPath) {
  if (!existsSync(rsrcPath)) return null;
  const buf = readFileSync(rsrcPath);
  const out = {};
  let chunks = 0;
  let i = 0;
  // The PiPL body opens with version(4) + chunk count(4) before the first
  // '8BIM', so scan for the tag rather than assuming an offset.
  while (i < buf.length - 16) {
    if (buf.toString("latin1", i, i + 4) !== "8BIM") {
      i++;
      continue;
    }
    const tag = buf.toString("latin1", i + 4, i + 8);
    // 16-byte chunk header, not the 12 the plain 8BIM convention implies:
    // '8BIM' + key(4) + reserved(4, always zero here) + length(4) + data.
    // Reading the reserved word as the length silently yields empty values.
    const len = buf.readUInt32BE(i + 12);
    if (len > buf.length - (i + 16)) break;
    const data = buf.subarray(i + 16, i + 16 + len);
    // Adobe encodes PiPL names as Pascal strings: length byte + characters.
    const asPascal = data.length > 1 && data[0] === data.length - 1
      ? data.subarray(1).toString("latin1")
      : null;
    let value;
    const printable = data.length >= 2 && /^[\x20-\x7e]+$/.test(data.toString("latin1"));
    if (asPascal && /^[\x20-\x7e]+$/.test(asPascal)) value = asPascal;
    else if (printable) value = data.toString("latin1"); // e.g. kind = eFKT
    else if (len === 4) value = `0x${data.readUInt32BE(0).toString(16).padStart(8, "0")}`;
    else if (len === 2) value = `0x${data.readUInt16BE(0).toString(16).padStart(4, "0")}`;
    else value = data.toString("latin1").replace(/[^\x20-\x7e]/g, "");
    if (tag && !(tag in out)) out[tag] = value;
    chunks++;
    i += 16 + len;
  }
  return chunks ? { ...out, _chunks: chunks } : null;
}

/** Split nm output into authored API vs domain code vs std library. */
function classifySymbols(binary) {
  const raw = sh("nm", ["-C", binary]);
  const syms = [];
  for (const line of raw.split("\n")) {
    const m = line.match(/^[0-9a-f]+\s+([TtWwDdBbSs])\s+(.+)$/);
    if (!m) continue;
    const [, kind, rawName] = m;
    // Compiler/assembler artefacts carry no design information.
    if (/^(GCC_|_GLOBAL__sub_I_$|l_OBJC|\.|___|_Unwind|__cxx|__clang)/.test(rawName)) continue;
    if (/^(_?)(GCC_except_table|l_OBJC)/.test(rawName)) continue;
    if (/^(std|__1|swift)::/.test(rawName)) continue;

    if (rawName.startsWith("__GLOBAL__sub_I_")) {
      syms.push({ kind, scope: "source-unit", name: rawName.replace(/^__GLOBAL__sub_I_/, "") });
      continue;
    }
    // Mach-O prefixes C symbols with one underscore; that is a linker detail,
    // and dropping it is what preserves the real name _EffectMain.
    const name = rawName.startsWith("_") && !rawName.startsWith("__") ? rawName.slice(1) : rawName;

    let scope;
    if (/^(std|__cxx|_Unwind)/.test(name) || name.startsWith("operator new") || name.startsWith("operator delete"))
      scope = "stdlib";
    else if (/(PF_InData|PF_OutData|PF_ParamDef|PF_EventExtra|PF_LayerDef|PF_EffectWorld|AEGP_|SPBasic|DRAWBOT_)/.test(name))
      scope = "adobe-sdk";
    else if (/^[A-Za-z][A-Za-z0-9]*_[A-Za-z0-9_]+\s*\(/.test(name)) scope = "plugin-api";
    else if (/^[a-zA-Z_][A-Za-z0-9_]*::/.test(name)) scope = "domain";
    else scope = "other";
    syms.push({ kind, scope, name });
  }
  const sourceUnits = [...new Set(
    syms.filter((s) => s.scope === "source-unit" && /\.(cpp|cc|cxx|c|mm)$/.test(s.name))
      .map((s) => s.name),
  )].sort();
  const byScope = {};
  for (const s of syms) {
    if (s.scope === "source-unit") continue;
    (byScope[s.scope] ??= []).push(s.name);
  }
  for (const k of Object.keys(byScope)) byScope[k] = [...new Set(byScope[k])].sort();
  return { sourceUnits, byScope, total: syms.length };
}

/** Strings that read like code or raw assembly, not authored UI text. */
function looksLikeNoise(s) {
  if (/^[A-Z0-9$_.]{3,}$/.test(s)) return true;              // AWAVAUATSH, $ffffff.
  if (/^[!-~]{0,4}$/.test(s) && !/[aeiou]/i.test(s)) return true;
  if (/^[\W\d]+$/.test(s)) return true;
  if (/^[a-z]{1,3}[A-Z]/.test(s) && !s.includes(" ")) return true;
  if (/^(basic_string|char_traits|allocator|__|_)/.test(s)) return true;
  return false;
}

function authoredStrings(binary) {
  const raw = sh("strings", ["-a", binary]);
  const keep = new Set();
  for (const s of raw.split("\n")) {
    const t = s.replace(/\s+$/, "");
    if (t.length < 3 || t.length > 200) continue;
    if (SDK_NOISE.some((re) => re.test(t))) continue;
    if (!/[A-Za-z]{3}/.test(t)) continue;
    if (/^[A-Za-z0-9+/=]{40,}$/.test(t)) continue;
    if (looksLikeNoise(t)) continue;
    keep.add(t);
  }
  return [...keep].sort();
}

/**
 * Categorise authored strings into the effect's UI vocabulary.
 * These are candidates recovered by shape, not by reading the param setup code,
 * so the spec labels them as such.
 */
function categoriseStrings(authored) {
  const params = [];
  const messages = [];
  const modes = [];
  for (const s of authored) {
    if (s.includes("%") && /[.:]/.test(s) && s.split(" ").length > 6) { messages.push(s); continue; }
    if (/[.!?]$/.test(s) && s.split(" ").length > 5) { messages.push(s); continue; }
    if (s.includes("&") || /^[A-Z][a-z]+( [A-Za-z0-9()%#-]+){1,3}$/.test(s)) { params.push(s); continue; }
    if (/^[A-Z][a-z]+$/.test(s) || /^[A-Z][a-z]+ [A-Z][a-z]+$/.test(s)) { modes.push(s); continue; }
  }
  return { params, messages, modes };
}

function recoverOne(name, dir, opts) {
  const bundle = path.join(dir, `${name}.plugin`);
  const binary = path.join(bundle, "Contents", "MacOS", name);
  if (!existsSync(binary)) return null;

  const plistJson = sh("plutil", ["-convert", "json", "-o", "-", path.join(bundle, "Contents", "Info.plist")]);
  let plist = {};
  try { plist = JSON.parse(plistJson); } catch { /* leave empty */ }

  const rsrc = path.join(bundle, "Contents", "Resources", `${name}.rsrc`);
  const pipl = parsePipl(rsrc);

  const bytes = readFileSync(binary);
  const archs = sh("lipo", ["-archs", binary]).trim();
  const { sourceUnits, byScope, total } = classifySymbols(binary);

  const authored = authoredStrings(binary);
  const { params, messages, modes } = categoriseStrings(authored);

  return {
    plugin: name,
    bundle,
    recoveredAt: new Date().toISOString(),
    binary: {
      bytes: bytes.length,
      sha256: createHash("sha256").update(bytes).digest("hex"),
      md5: createHash("md5").update(bytes).digest("hex"),
      archs: archs ? archs.split(/\s+/) : [],
      stripped: total < 8,
    },
    identity: {
      bundleIdentifier: plist.CFBundleIdentifier ?? null,
      bundleName: plist.CFBundleName ?? null,
      shortVersion: plist.CFBundleShortVersionString ?? null,
      minimumSystem: plist.LSMinimumSystemVersion ?? null,
    },
    pipl,
    sourceUnits,
    symbols: { total, byScope },
    strings: {
      authored: authored.length,
      parameterCandidates: params,
      messageStrings: messages,
      modeCandidates: modes,
    },
    ...(opts.keepAllStrings ? { allStrings: authored } : {}),
  };
}

function main(argv) {
  const outIdx = argv.indexOf("--out");
  const outDir = outIdx >= 0 ? argv[outIdx + 1] : path.join(import.meta.dirname, "..", "specs");
  const named = argv.filter((a) => !a.startsWith("--") && a !== outDir);
  const keepAllStrings = argv.includes("--all-strings");

  if (!existsSync(PLUGINS_DIR)) {
    console.error(`No fleet installed at ${PLUGINS_DIR}`);
    return 1;
  }
  const names = named.length
    ? named
    : readdirSync(PLUGINS_DIR).filter((f) => f.endsWith(".plugin")).map((f) => f.replace(/\.plugin$/, "")).sort();

  mkdirSync(outDir, { recursive: true });
  const index = [];
  for (const name of names) {
    const spec = recoverOne(name, PLUGINS_DIR, { keepAllStrings });
    if (!spec) { console.error(`  skip ${name}: no binary`); continue; }
    writeFileSync(path.join(outDir, `${name}.json`), JSON.stringify(spec, null, 2) + "\n");
    index.push({
      plugin: name,
      name: spec.pipl?.name ?? spec.identity.bundleName,
      menuName: spec.pipl?.eMNA ?? null,
      category: spec.pipl?.catg ?? null,
      symbols: spec.symbols.total,
      sourceUnits: spec.sourceUnits.length,
      paramCandidates: spec.strings.parameterCandidates.length,
      messages: spec.strings.messageStrings.length,
      archs: spec.binary.archs.join("+"),
    });
    console.log(
      `  ${name.padEnd(18)} syms=${String(spec.symbols.total).padStart(3)} ` +
      `srcs=${spec.sourceUnits.length} params=${String(spec.strings.parameterCandidates.length).padStart(2)} msgs=${String(spec.strings.messageStrings.length).padStart(2)} ` +
      `pipl=${spec.pipl ? Object.keys(spec.pipl).length + " tags" : "none"}`,
    );
  }
  writeFileSync(path.join(outDir, "index.json"), JSON.stringify(index, null, 2) + "\n");
  console.log(`\nwrote ${index.length} specs + index.json to ${outDir}`);
  return 0;
}

process.exitCode = main(process.argv.slice(2));
