#!/usr/bin/env node
// Differential codegen check: compile a recovered namespace, then compare each
// function's instruction stream against the SHIPPED BUNDLE's.
//
// This is the strongest verification available for recovered code. The self-check
// binaries prove the maths behaves as asserted, but "behaves as asserted" is only
// as good as the assertions. This compares against the artefact itself: if the
// transcription is right AND compiled by a close-enough clang, the instruction
// streams come out identical, constants and all.
//
// It is not a proof of correctness — a matching stream only means the source
// reproduces this compiler's output for this function, and codegen can coincide
// for different sources. But a MISMATCH is always meaningful, and for small
// integer routines like glitch::hashKey the match is exact and unforgeable: all
// 34 instructions and all 16 constants line up.
//
//   node tools/codegen-diff.mjs <Namespace> <SymbolFragment>
//   node tools/codegen-diff.mjs glitch hashKey
//   node tools/codegen-diff.mjs glitch            # every function in the namespace

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

import { BUNDLES, bundlePath, namespaceOf } from "./namespaces.mjs";

const ROOT = path.join(import.meta.dirname, "..");

/**
 * Resolve branch targets to instruction INDICES before comparing.
 *
 * This is the fix for the differ's biggest source of false positives. The shipped
 * binary branches to a resolved ADDRESS (`b.ls 0x768`) while freshly compiled
 * assembly branches to a LABEL (`b.ls LBB0_8`), and comparing those TEXTUALLY reports
 * a difference for every single branch. That drowned the real signal on every function
 * with control flow — it is why speedramp's four functions all looked wrong when only
 * register allocation actually differed.
 *
 * Reducing both sides to "branch to instruction N" makes the comparison structural:
 * the same jump to the same relative position, whatever the labels are called. A
 * target that cannot be resolved on either side is left alone so it still shows up.
 */
const BRANCH_TO = /^(b(?:\.\w+)?|cbz|cbnz|tbz|tbnz)\s+(.+)$/;

function resolveBranchTargets(entries) {
  // Index of the instruction each label precedes, for the fresh-assembly side.
  //
  // FIRST index wins, not last. A label in clang's output sticks to every instruction
  // that follows it until the next label, so an unconditional `set` leaves each label
  // pointing at the LAST instruction of its basic block. The shipped side indexes by
  // exact address, so it always points at the block's FIRST instruction — which meant
  // every branch into a block of more than one instruction was reported as a difference
  // even when both sides jumped to the same place. Keeping the first occurrence makes
  // the two sides mean the same thing.
  const labelIndex = new Map();
  entries.forEach((e, i) => { if (e.label && !labelIndex.has(e.label)) labelIndex.set(e.label, i); });
  // Address -> index, for the shipped side.
  const addrIndex = new Map();
  entries.forEach((e, i) => { if (e.addr !== null) addrIndex.set(e.addr, i); });

  return entries.map((e) => {
    const m = e.text.match(BRANCH_TO);
    if (!m) return e.text;
    // The target is the LAST comma-separated operand, because tbz/tbnz/cbz/cbnz carry a
    // register (and, for tbz/tbnz, a bit index) before it. An earlier version matched the
    // target with `(\S+)$`, which can never match a three-operand `tbnz w10, #0x2, 0xc88`
    // — so EVERY conditional branch of that family was left unresolved and compared as
    // label text against an address, i.e. as a guaranteed difference.
    const operands = m[2];
    const token = operands.slice(operands.lastIndexOf(",") + 1).trim();
    let idx = null;
    if (e.addr !== null) {
      const asAddr = Number(token);
      if (!Number.isNaN(asAddr)) idx = addrIndex.get(asAddr) ?? null;
    }
    if (idx === null && labelIndex.has(token)) idx = labelIndex.get(token);
    if (idx === null) return e.text;    // unresolved: leave it visible
    return `${m[1]} ->instr#${idx}`;
  });
}

/** Pull one function's instructions out of otool -tV output.
 *
 *  A symbol line is a bare `_name:` at column 0; instruction lines start with a
 *  hex address and a tab. */
function fromOtool(text, symbolFragment) {
  const out = [];
  let on = false;
  for (const line of text.split("\n")) {
    if (isLabel(line)) {
      if (on) break;                       // the next symbol starts
      on = line.includes(symbolFragment);
      continue;
    }
    if (!on) continue;
    const m = line.match(/^([0-9a-f]+)\t(\S+)\t?(.*)$/);
    if (m) out.push({ addr: parseInt(m[1], 16), label: null, text: normalize(`${m[2]} ${otoolOperands(m[3])}`) });
  }
  return out;
}

/**
 * Reduce an otool operand column to just the operands.
 *
 * otool annotates instruction lines with `; ...` comments, and one of them is not a
 * comment at all: a call through the lazy-binding stubs prints as
 *
 *     bl  0x38b8 ; symbol stub for: ___exp10f
 *
 * The fresh side of the comparison says `bl ___exp10f`, because that is what the
 * assembler prints for a direct call. Leaving the annotation in made the two sides
 * differ at EVERY call site — the operand looked like a raw address instead of a
 * symbol — which inflated the "differing" tally for every function that calls anything.
 * Resolving the stub to its symbol makes the two sides comparable; every other `;`
 * annotation is a genuine comment (`; =0x3f800000`, `; literal pool for: "..."`) and is
 * dropped, which also stops an immediate's convenience comment from masking a real
 * difference in the register or the addressing mode.
 *
 * `fromAsm` already stripped `;` for the same reason; this side simply never did.
 */
function otoolOperands(raw) {
  const stub = raw.match(/;\s*symbol stub for:\s*(\S+)/);
  if (stub) return stub[1];
  return raw.split(";")[0].trim();
}

/** Pull one function's instructions out of clang -S output, normalised into the
 *  same shape as otool's: no addresses, no comments, hex immediates.
 *
 *  The label test is deliberately a single clean predicate. An earlier version
 *  wrote `/^\t\.p2align|^[A-Za-z_].*:$/.test(line) && !line.startsWith("\t")`,
 *  where `|` binds looser than `&&` — so it evaluated as a bitwise OR of a regex
 *  test and a boolean, which happened to be truthy for every line and made the
 *  scanner swallow the whole file instead of one function. */
function fromAsm(text, symbolFragment) {
  const out = [];
  let on = false;
  let label = null;
  for (const line of text.split("\n")) {
    if (isLabel(line)) {
      // `isLabel` matches FUNCTION symbols only (Mach-O underscore prefix), so a hit
      // here while already inside one means the next function has started.
      if (on) break;
      on = line.includes(symbolFragment);
      label = on ? line.replace(/:$/, "") : null;
      continue;
    }
    if (!on) continue;
    // Local labels (LBB0_1:) are branch TARGETS, not boundaries. Recording them lets
    // resolveBranchTargets turn a label into an instruction index.
    const localLabel = line.match(/^([A-Za-z_$][A-Za-z0-9_$.]*):/);
    if (localLabel) { label = localLabel[1]; continue; }
    if (!line.startsWith("\t")) continue;
    const body = line.slice(1).split(";")[0];   // drop "; =0x.." annotations
    const norm = normalize(body);
    if (!norm || norm.startsWith(".")) continue;
    out.push({ addr: null, label, text: norm });
  }
  return out;
}

/**
 * Reduce an instruction to a comparable form.
 *
 * otool separates mnemonic and operands with a SPACE ("fcmp s0, s2") while clang
 * emits a TAB ("fcmp\ts0, s2"), and clang prints a shift amount as decimal
 * ("lsl #16") where the shipped binary disassembles to hex ("lsl #0x10"). Neither
 * is a semantic difference, so both are normalised away rather than reported as a
 * mismatch.
 */
function normalize(instr) {
  return instr
    .replace(/\s+/g, " ")
    .trim()
    .replace(/#(-?\d+)\b/g, (_, d) => {
      const n = Number(d);
      return n < 0 ? `#-0x${(-n).toString(16)}` : `#0x${n.toString(16)}`;
    });
}

/**
 * True for a function-symbol label at column 0.
 *
 * Two platform traps here, both found by watching the counts go wrong:
 *
 *   * Clang emits basic blocks as `LBB0_1:` on Darwin — NOT `.LBB0_1:` as on ELF
 *     targets. Excluding only dot-prefixed labels therefore still splits every
 *     function at its first branch, which made makeIdentity read as 269 shipped
 *     instructions against a 14-instruction "transcription".
 *   * Mach-O prefixes every external symbol with an underscore, so requiring a
 *     leading `_` is both necessary and sufficient: it accepts `__ZN...:` and
 *     `_EffectMain:` while rejecting `LBB0_1:` and `.p2align`.
 */
function isLabel(line) {
  return /^_[A-Za-z0-9_$.]*:/.test(line);
}

/**
 * Read `__ZN<len><ns><len><name>...` using the mangling's explicit lengths.
 *
 * Deliberately NOT a regex over the whole tail: the length prefixes exist exactly
 * so a name can contain otherwise-ambiguous characters, and ignoring them is how
 * `apply` came back as `applyIhEEvRKNS_4ViewIKT_...`, which then matched nothing.
 */
function parseNested(mangled) {
  if (!mangled.startsWith("__ZN")) return null;
  let i = 4;
  const readLen = () => {
    let acc = "";
    while (i < mangled.length && mangled[i] >= "0" && mangled[i] <= "9") acc += mangled[i++];
    return acc ? Number(acc) : null;
  };
  const nsLen = readLen();
  if (!nsLen) return null;
  const ns = mangled.slice(i, i + nsLen);
  i += nsLen;
  const nameLen = readLen();
  if (!nameLen) return null;
  const name = mangled.slice(i, i + nameLen);
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(ns)) return null;
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) return null;
  return { ns, name };
}

/**
 * A suffix that tells one overload of a name from another, or "" when the name is unique.
 *
 * The suffix is the mangled PARAMETER encoding taken from the symbol itself rather than a
 * hand-written signature, so it cannot drift from what the binary actually declares. The
 * leading `E` closes the nested name and is dropped.
 */
function distinguish(mangled, name, nameCounts) {
  if ((nameCounts.get(name) ?? 0) < 2) return "";
  const marker = `${name.length}${name}`;
  const at = mangled.indexOf(marker);
  if (at < 0) return "";
  const params = mangled.slice(at + marker.length).replace(/^E/, "");
  return `(${params.length > 60 ? `${params.slice(0, 57)}...` : params})`;
}

function main() {
  // `--cpp=<path>` compiles a DIFFERENT translation unit against the same bundle and the
  // same header. It exists so the differ can be negative-controlled: the test suite
  // feeds it a deliberately wrong transcription of a function that otherwise verifies
  // identical, and requires the mismatch to be reported. Without it the only way to test
  // the comparison would be to corrupt a file inside src/, which a test must not do.
  const argv = process.argv.slice(2);
  const cppArg = argv.find((a) => a.startsWith("--cpp="));
  const [dir, frag] = argv.filter((a) => !a.startsWith("--"));
  if (!dir) {
    console.error("usage: codegen-diff.mjs <RecoveredDir> [SymbolFragment] [--cpp=<path>]");
    return 2;
  }
  const plugin = BUNDLES[dir];
  if (!plugin) {
    console.error(`no bundle mapped for recovered namespace ${dir}`);
    return 2;
  }
  // The DIRECTORY and the C++ NAMESPACE are different names. They coincide for most of
  // the recovered set, which is exactly why assuming they were one thing went unnoticed
  // until AutoFlickerFix (`src/autoflickerfix/`, `namespace aff`), where the assumption
  // made this tool report "no symbols" for a namespace of ten transcribed functions.
  const ns = namespaceOf(ROOT, dir);
  if (!ns) {
    console.error(`could not read a namespace declaration from src/${dir}/${dir}.h`);
    return 2;
  }
  const bin = bundlePath(plugin);
  if (!bin) {
    console.error(`bundle for ${plugin} is not installed`);
    return 2;
  }
  const cpp = cppArg ? cppArg.slice("--cpp=".length)
                     : path.join(ROOT, "src", dir, `${dir}.cpp`);
  if (!existsSync(cpp)) {
    console.error(`missing ${cpp}`);
    return 2;
  }

  const tmp = mkdtempSync(path.join(tmpdir(), "codegen-"));
  try {
    const asmPath = path.join(tmp, `${dir}.s`);
    execFileSync("clang++", [
      // -I so that a `--cpp` override outside src/ can still include the namespace's
      // header. Harmless for the in-tree case, where the quoted include already resolves
      // relative to the source file.
      "-I", path.join(ROOT, "src", dir),
      "-std=c++17", "-O2", "-S", "-arch", "arm64", "-o", asmPath, cpp,
    ], { stdio: "pipe" });

    const shipped = execFileSync("otool", ["-tV", "-arch", "arm64", bin],
                                 { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
    const mine = readFileSync(asmPath, "utf8");

    // Discover the functions to compare from the SHIPPED binary, so the check is
    // driven by what is actually there rather than by a list I maintain.
    //
    // The comparison unit is the FULL MANGLED SYMBOL, not the bare function name. Keying
    // on names silently collapsed OVERLOADS into one entry, and then compared only
    // whichever overload the shipped dump listed first — so `aff::trimmedMean(const
    // float*, size_t, float)` was checked and its five-instruction tail-call wrapper
    // `aff::trimmedMean(const std::vector<float>&, float)` was never looked at, while
    // the summary still read "trimmedMean: not implemented" or "differing" as if the
    // name had been covered. The mangled name is also the exact label both sides print,
    // so it doubles as the search fragment.
    //
    // Parsing uses the mangling's own length prefixes. A regex like
    // /__ZN\d+(\w+?)(\d+)(\w+)E/ looks reasonable and is wrong: for
    // `__ZN6glitch5applyIhEEv...` the final (\w+) runs past the first `E` and
    // swallows the whole parameter list, so the fragment never matches in the
    // assembly and every implemented function reports as "absent".
    const symbols = [...shipped.matchAll(/^__ZN.+$/gm)]
      .map((m) => m[0].replace(/:$/, ""))
      .filter((sym) => {
        const p = parseNested(sym);
        return p && p.ns === ns;
      });
    const targets = frag ? symbols.filter((s) => s.includes(frag)) : symbols;

    if (!targets.length) {
      console.error(`no ${ns}:: symbols matching ${frag ?? "<all>"} in ${plugin}`);
      return 1;
    }

    // Names carried by more than one symbol. Only those get a disambiguating suffix, so
    // the common case still reads as `glitch::hashKey`.
    const nameCounts = new Map();
    for (const sym of targets) {
      const { name } = parseNested(sym);
      nameCounts.set(name, (nameCounts.get(name) ?? 0) + 1);
    }

    let matched = 0, differed = 0, absent = 0;
    for (const mangled of targets) {
      const { name } = parseNested(mangled);
      // The fragment is the mangled symbol itself, so an overload is matched to the
      // overload and not to whichever one happened to be listed first.
      const a = resolveBranchTargets(fromOtool(shipped, mangled));
      const b = resolveBranchTargets(fromAsm(mine, mangled));
      const label = `${ns}::${name}${distinguish(mangled, name, nameCounts)}`;
      if (!b.length) {
        console.log(`  absent  ${label} (not implemented — ${a.length} instrs in the bundle)`);
        ++absent;
        continue;
      }
      if (a.length === b.length && a.every((x, i) => x === b[i])) {
        console.log(`  MATCH   ${label}  ${a.length} instructions, identical`);
        ++matched;
      } else {
        const firstDiff = a.findIndex((x, i) => x !== b[i]);
        console.log(`  DIFFER  ${label}  shipped=${a.length} mine=${b.length}`);
        for (let i = Math.max(0, firstDiff - 2); i < Math.min(a.length, firstDiff + 3); i++) {
          console.log(`            shipped: ${a[i] ?? ""}`);
          console.log(`            mine   : ${b[i] ?? ""}`);
        }
        ++differed;
      }
    }
    console.log(`\n${matched} identical, ${differed} differing, ${absent} not implemented`);
    return differed === 0 ? 0 : 1;
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
}

process.exitCode = main();
