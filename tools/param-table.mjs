#!/usr/bin/env node
// Decode an effect's parameter table from its EffectMain.
//
// Parameter types, IDs, names, bounds and defaults live in the BINARY, not in the
// PiPL: they are registered at runtime through PF_ADD_PARAM, which is a macro over
// a suite function pointer. So the only surviving record is the disassembly of the
// PF_Cmd_PARAMS_SETUP handler, and that is what this reads.
//
//   node tools/param-table.mjs              # every installed effect
//   node tools/param-table.mjs GlitchKit    # one effect
//   node tools/param-table.mjs --json       # machine-readable
//
// HOW THE BLOCKS ARE FOUND. PF_ADD_PARAM compiles to an indirect `blr` whose
// preceding instructions set `w1 = -1` (append after the last parameter) and
// `x2 = <&def>`. Each block REWRITES the whole PF_ParamDef, so reading the stores
// between two such calls recovers one parameter.
//
// PF_ParamDef's offsets are the SDK's (AE_Effect.h), and they are validated by the
// data itself: if the offset table were wrong the NAMES would come out as garbage
// rather than as readable strings, which is exactly what happened while this was
// being written.

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

// AE SDK 25.6, AE_Effect.h. Verified against the header by test/params.test.mjs.
const PF_DEF = {
  uu_id: 0x00,
  ui_flags: 0x04,
  ui_width: 0x08,
  ui_height: 0x0a,
  param_type: 0x0c,
  name: 0x10,
  name_len: 32,   // PF_MAX_EFFECT_PARAM_NAME_LEN + 1
  flags: 0x30,
  unused: 0x34,
  u: 0x38,
};

const PARAM_TYPE = {
  0: "LAYER", 1: "SLIDER", 2: "FIX_SLIDER", 3: "ANGLE", 4: "CHECKBOX", 5: "COLOR",
  6: "POINT", 7: "POPUP", 8: "CUSTOM", 9: "NO_DATA", 10: "FLOAT_SLIDER",
  11: "ARBITRARY_DATA", 12: "PATH", 13: "GROUP_START", 14: "GROUP_END",
  15: "BUTTON", 18: "POINT_3D",
};

function bundlesDir() {
  for (const year of ["2026", "2025"]) {
    const p = `/Applications/Adobe After Effects ${year}/Plug-ins/Williams Media`;
    if (existsSync(p)) return p;
  }
  return null;
}

function listPlugins() {
  const dir = bundlesDir();
  if (!dir) return [];
  return readdirSync(dir)
    .filter((d) => d.endsWith(".plugin"))
    .map((d) => d.replace(/\.plugin$/, ""))
    .sort();
}

function binaryFor(plugin) {
  const dir = bundlesDir();
  return dir ? path.join(dir, `${plugin}.plugin`, "Contents", "MacOS", plugin) : null;
}

/**
 * Track movz/movk into w registers and collect stores relative to the def base.
 *
 * The def is addressed TWO ways in the same block, and handling only one of them
 * silently yields a table of empty names:
 *
 *   str w8, [sp, #0x50]      ; absolute, because defBase is sp-relative
 *   stur w8, [x22, #0x10]    ; via a register holding sp + defBase, preloaded
 *                            ; once for the whole handler
 *
 * `baseRegs` is the set of registers known to hold sp + defBase.
 */
function decodeBlock(instructions, defBase, baseRegs, readWord64, readWord128) {
  // 64-bit register values, for names stored as an 8-byte load out of a literal
  // pool rather than as movz/movk immediates. The longer parameter names take this
  // path, which is why an implementation that only tracks w-registers produces
  // readable names for the short ones and empty strings for the rest.
  const xregs = new Map();
  const qregs = new Map();   // 128-bit name fragments
  // q-registers known to be all-zero, so a SIMD fill can be recorded as zeroing the
  // fields it covers. This is how the def is CLEARED: PF_Param_LAYER is 0, so a
  // layer parameter never stores its param_type at all — the clear establishes it.
  // Reading only explicit stores therefore reports "type not found" for every
  // layer parameter in the fleet.
  const qzero = new Set();
  const offOf = (base, off) => {
    if (base === "sp") return off;
    if (baseRegs.has(base)) return defBase + off;
    return null;
  };
  const regs = new Map();
  // wzr/xzr read as 0. They are how a ZERO field is stored — PF_Param_LAYER is 0,
  // so a layer parameter's param_type is `str wzr, [...]`, and a tracer that only
  // knows named w-registers reports the type as "not found" instead of 0.
  // regs is keyed by the bare register NUMBER ("10"), while the operand text is
  // "w10" — so the leading letter has to come off. Getting this wrong makes every
  // lookup miss, which reads as "every field absent" rather than as an error.
  const regVal = (operand) => {
    if (operand === "wzr" || operand === "xzr") return 0;
    return regs.get(operand.replace(/^[wx]/, ""));
  };
  const stores = new Map();      // off -> 32-bit value (id, type, flags, union)
  const nameBytes = new Map();   // byte offset within PF_DEF_NAME -> byte value
  /** Write `n` little-endian bytes of `v` into the name buffer at `off`. */
  const putName = (off, v, n) => {
    if (off < defBase + 0x10 || off >= defBase + 0x10 + 32) return;
    for (let k = 0; k < n; k++) nameBytes.set(off + k, (v >>> (8 * k)) & 0xff);
  };

  for (const { op, rest, note } of instructions) {
    let m;
    if (op === "mov" && (m = rest.match(/^w(\d+), #(-?0x[0-9a-f]+)$/))) {
      regs.set(m[1], Number(m[2]) >>> 0);
      continue;
    }
    if (op === "movk" && (m = rest.match(/^w(\d+), #(0x[0-9a-f]+), lsl #(\d+)$/))) {
      const r = m[1];
      const v = Number(m[2]) << Number(m[3]);
      regs.set(r, ((regs.get(r) ?? 0) & ~(0xffff << Number(m[3]))) | (v >>> 0));
      continue;
    }
    if (op === "mov" && (m = rest.match(/^w(\d+), (w\d+)$/))) {
      if (regs.has(m[2])) regs.set(m[1], regs.get(m[2]));
      continue;
    }
    // adrp xN, <encoded>  with the ABSOLUTE page in the annotation. The printed
    // immediate is PC-relative, so the annotation is the one to use; without it the
    // page is wrong and every literal read through it is wrong.
    if (op === "adrp" && (m = rest.match(/^(x\d+), -?\d+$/))) {
      if (/^0x[0-9a-f]+$/.test(note)) xregs.set(m[1], BigInt(note));
      continue;
    }
    if (op === "add" && (m = rest.match(/^(x\d+), (x\d+), #(0x[0-9a-f]+)$/))) {
      if (xregs.has(m[2])) xregs.set(m[1], xregs.get(m[2]) + BigInt(m[3]));
      continue;
    }
    // ldr xM, [xN]  /  ldur xM, [xN, #off]  -> load the WORD AT the address.
    //
    // Copying the address instead of dereferencing it is a subtle and silent
    // error: the register still holds a plausible-looking value, so the follow-on
    // byte extraction produces name fragments that are wrong rather than empty.
    if (op === "ldr" && (m = rest.match(/^(x\d+), \[(x\d+)\]$/))) {
      const addr = xregs.get(m[2]);
      if (addr === undefined) continue;
      const w = readWord64(Number(addr));
      if (w !== null) xregs.set(m[1], w);
      continue;
    }
    if (op === "ldur" && (m = rest.match(/^(x\d+), \[(x\d+), #(0x[0-9a-f]+)\]$/))) {
      const addr = xregs.get(m[2]);
      if (addr === undefined) continue;
      const w = readWord64(Number(addr + BigInt(m[3])));
      if (w !== null) xregs.set(m[1], w);
      continue;
    }
    if (op === "movi.2d" && /^v(\d+), #0+$/.test(rest)) {
      qzero.add(`q${rest.match(/^v(\d+)/)[1]}`);
      continue;
    }
    // stp qN, qN, [base, #off] -> 32 zeroed bytes
    if (op === "stp" && (m = rest.match(/^(q\d+), (q\d+), \[(\w+), #(0x[0-9a-f]+)\]$/))) {
      if (qzero.has(m[1]) && qzero.has(m[2])) {
        const off = offOf(m[3], Number(m[4]));
        if (off !== null) for (let k = 0; k < 32; k += 4) stores.set(off + k, 0);
      }
      continue;
    }
    // str qN, [base, #off] -> 16 zeroed bytes
    if (op === "str" && (m = rest.match(/^(q\d+), \[(\w+), #(0x[0-9a-f]+)\]$/))) {
      if (qzero.has(m[1])) {
        const off = offOf(m[2], Number(m[3]));
        if (off !== null) for (let k = 0; k < 16; k += 4) stores.set(off + k, 0);
      }
      continue;
    }
    // ldr qN, [xM]  -> sixteen bytes of name, loaded from a literal.
    if (op === "ldr" && (m = rest.match(/^(q\d+), \[(x\d+)\]$/))) {
      const addr = xregs.get(m[2]);
      if (addr === undefined) continue;
      const bytes = readWord128(Number(addr));
      if (bytes) qregs.set(m[1], Buffer.from(bytes));
      continue;
    }
    // stur qN, [base, #off]  -> sixteen name bytes at once
    if (op === "stur" && (m = rest.match(/^(q\d+), \[(\w+), #(0x[0-9a-f]+)\]$/))) {
      const off = offOf(m[2], Number(m[3]));
      if (off === null) continue;
      const bytes = qregs.get(m[1]);
      if (!bytes) continue;
      for (let k = 0; k < bytes.length; k++) nameBytes.set(off + k, bytes[k]);
      continue;
    }
    // stur xM, [base, #off]  -> eight name bytes at once
    if (op === "stur" && (m = rest.match(/^(x\d+), \[(\w+), #(0x[0-9a-f]+)\]$/))) {
      const off = offOf(m[2], Number(m[3]));
      if (off === null) continue;
      const addr = xregs.get(m[1]);
      if (addr === undefined) continue;
      const word = readWord64(Number(addr));
      if (word === null) continue;
      for (let k = 0; k < 8; k++) {
        const byte = Number((word >> BigInt(8 * k)) & 0xffn);
        nameBytes.set(off + k, byte);
      }
      continue;
    }
    // stp wA, wB, [base, #off]  -> two adjacent 4-byte fields
    if ((m = rest.match(/^(w\d+), (w\d+), \[(\w+), #(0x[0-9a-f]+)\]$/))) {
      const off = offOf(m[3], Number(m[4]));
      if (off === null) continue;
      const va = regVal(m[1]) ?? null;
      const vb = regVal(m[2]) ?? null;
      stores.set(off, va);
      stores.set(off + 4, vb);
      if (va !== null && va !== undefined) putName(off, va, 4);
      if (vb !== null && vb !== undefined) putName(off + 4, vb, 4);
      continue;
    }
    // str/strh/stur/sturh wA, [base, #off]
    if ((m = rest.match(/^(?:h\s+)?(w\d+), \[(\w+), #(0x[0-9a-f]+)\]$/))) {
      const off = offOf(m[2], Number(m[3]));
      if (off === null) continue;
      const v = regVal(m[1]) ?? null;
      stores.set(off, v);
      // A halfword store writes two name bytes; a word store writes four.
      if (v !== null && v !== undefined) putName(off, v, /^h\s/.test(m[0]) ? 2 : 4);
      continue;
    }
  }
  return { stores, nameBytes };
}

/**
 * Copy the arm64 slice out of a universal binary.
 *
 * Once thinned, every __TEXT section's virtual address equals its file offset, so
 * a literal pool entry can be read by seeking to the address the code computed.
 * Doing this against the fat binary instead gives x86_64 bytes that disassemble to
 * nonsense — which is exactly the trap that produced garbage "strings" earlier.
 */
function thinArm64(bin, into) {
  const out = path.join(into, "thin");
  execFileSync("lipo", ["-thin", "arm64", bin, "-output", out], { stdio: "pipe" });
  return readFileSync(out);
}

function parseParams(plugin, workdir) {
  const bin = binaryFor(plugin);
  if (!bin || !existsSync(bin)) return null;
  let image;
  try {
    image = thinArm64(bin, workdir);
  } catch {
    return null;
  }
  // VA -> bytes, valid inside __TEXT where addr == file offset.
  const readWord64 = (va) => {
    if (va < 0 || va + 8 > image.length) return null;
    return image.readBigUInt64LE(va);
  };
  // Names of nine characters or more are stored with a single 128-bit SIMD move,
  // so a reader that stops at 64 bits silently yields empty names for exactly the
  // longer, more descriptive parameters.
  const readWord128 = (va) => {
    if (va < 0 || va + 16 > image.length) return null;
    return image.subarray(va, va + 16);
  };

  let dump;
  try {
    dump = execFileSync("otool", ["-tV", "-arch", "arm64", bin],
                        { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  } catch {
    return null;
  }

  // Isolate EffectMain.
  const lines = dump.split("\n");
  const body = [];
  let on = false;
  for (const line of lines) {
    if (/^_[A-Za-z0-9_$.]*:$/.test(line)) {
      if (on) break;
      on = line.startsWith("_EffectMain:");
      continue;
    }
    if (!on) continue;
    const m = line.match(/^([0-9a-f]+)\t(\S+)\t?(.*)$/);
    if (!m) continue;
    // Split otool's trailing annotation off the operand list. Leaving it attached
    // breaks every `$`-anchored operand regex on exactly the instructions otool
    // chose to annotate, which is a silent, partial failure: `add x9, x9, #0x56e ;
    // literal pool for: "Blend (%)"` was skipped, so the register kept the raw
    // page address and the following literal load read the wrong word entirely.
    const raw = m[3].trim();
    const cut = raw.indexOf(" ; ");
    body.push({
      addr: parseInt(m[1], 16),
      op: m[2],
      rest: cut >= 0 ? raw.slice(0, cut).trim() : raw,
      note: cut >= 0 ? raw.slice(cut + 3).trim() : "",
    });
  }
  if (!body.length) return null;

  // Find each PF_ADD_PARAM: a `blr` with `mov w1, #-0x1` shortly before it.
  const blocks = [];
  let start = 0;
  for (let i = 0; i < body.length; i++) {
    if (body[i].op !== "blr") continue;
    const window = body.slice(Math.max(0, i - 10), i);
    const append = window.some((x) => x.op === "mov" && x.rest === "w1, #-0x1");
    if (!append) continue;
    // The def base comes from `add x2, sp, #off` in the same window.
    let defBase = null;
    let defReg = null;
    for (let j = window.length - 1; j >= 0; j--) {
      const m = window[j].rest.match(/^(x\d+), sp, #(0x[0-9a-f]+)$/);
      if (window[j].op === "add" && m) { defReg = m[1]; defBase = Number(m[2]); break; }
    }
    if (defBase === null) continue;
    blocks.push({ start, end: i, defReg, defBase });
    start = i + 1;
  }

  const params = [];
  for (const b of blocks) {
    // Every register holding the def pointer, not just the one used as the call
    // argument. Clang loads it once and then COPIES it into other registers for the
    // individual blocks (`mov x21, x22`), so scanning only for `add xN, sp, #base`
    // misses those — which is why some parameters decoded with empty names while
    // their neighbours in the same handler were fine. The closure below follows
    // those copies.
    const baseRegs = new Set();
    for (const ins of body) {
      const m = ins.op === "add" && ins.rest.match(/^(x\d+), sp, #(0x[0-9a-f]+)$/);
      if (m && Number(m[2]) === b.defBase) baseRegs.add(m[1]);
    }
    for (let pass = 0; pass < 4; pass++) {
      for (const ins of body) {
        const m = ins.op === "mov" && ins.rest.match(/^(x\d+), (x\d+)$/);
        if (m && baseRegs.has(m[2])) baseRegs.add(m[1]);
      }
    }
    const { stores, nameBytes } =
      decodeBlock(body.slice(b.start, b.end), b.defBase, baseRegs, readWord64, readWord128);
    const rel = (off) => off - b.defBase;
    const at = (field) => stores.get(b.defBase + PF_DEF[field]);
    const id = at("uu_id");
    const ptype = at("param_type");
    // The name is a char array; collect its bytes in order.
    let name = "";
    for (let i = 0; i < PF_DEF.name_len; i++) {
      const c = nameBytes.get(b.defBase + PF_DEF.name + i);
      if (c === undefined || c === 0) break;      // NUL terminates a PF_Str
      name += c >= 32 && c < 127 ? String.fromCharCode(c) : "";
    }

    // Fallback for names that arrive as a wide literal load.
    //
    // otool already annotates the address computation with the literal it points
    // at (`; literal pool for: "Blend (%)"`), and the name is the literal loaded
    // immediately before the store into the name field. Reading that annotation is
    // far more robust than tracing adrp/add/ldr/stur through registers, which is
    // what this replaced after several rounds of it going subtly wrong.
    if (!name) {
      const blk = body.slice(b.start, b.end);
      for (let i = 0; i + 2 < blk.length; i++) {
        const lit = blk[i].note.match(/^literal pool for: "(.*)"$/);
        if (!lit) continue;
        // Walk forward over the load and confirm the store targets the name field.
        for (let j = i + 1; j < Math.min(i + 4, blk.length); j++) {
          const st = blk[j].rest.match(/^[xq]\d+, \[(\w+), #(0x[0-9a-f]+)\]$/);
          if (!st) continue;
          // Inline the base-relative rule; offOf lives inside decodeBlock.
          const abs = st[1] === "sp" ? Number(st[2])
                                     : (baseRegs.has(st[1]) ? b.defBase + Number(st[2]) : null);
          if (abs === b.defBase + PF_DEF.name) {
            name = lit[1];
          }
          break;
        }
        if (name) break;
      }
    }


    // Union fields that are plain immediates, for sliders and checkboxes.
    const union = {};
    for (const [off, v] of stores) {
      const r = rel(off);
      if (r < PF_DEF.u || v === null || v === undefined) continue;
      union[`u+0x${(r - PF_DEF.u).toString(16)}`] = `0x${v.toString(16)}`;
    }
    params.push({
      id: id ?? null,
      type: PARAM_TYPE[ptype] ?? (ptype === undefined ? "?" : `?${ptype}`),
      name,
      union,
    });
  }
  return params;
}

function main() {
  const args = process.argv.slice(2);
  const asJson = args.includes("--json");
  const named = args.filter((a) => !a.startsWith("--"));

  const plugins = named.length ? named : listPlugins();
  if (!plugins.length) {
    console.error("no effect bundles found — is the fleet installed?");
    return 1;
  }

  const out = {};
  const workdir = mkdtempSync(path.join(tmpdir(), "params-"));
  try {
    for (const p of plugins) {
      const params = parseParams(p, workdir);
      if (params) out[p] = params;
      else console.error(`  ${p}: no EffectMain parameter blocks found`);
    }
  } finally {
    rmSync(workdir, { recursive: true, force: true });
  }

  if (asJson) {
    console.log(JSON.stringify(out, null, 2));
    return 0;
  }

  for (const [plugin, params] of Object.entries(out)) {
    console.log(`\n${plugin}  (${params.length} registered parameters)`);
    console.log(`  ${"id".padEnd(4)} ${"type".padEnd(14)} name`);
    for (const p of params) {
      const extra = Object.keys(p.union).length ? `   ${JSON.stringify(p.union)}` : "";
      // An empty name is shown as undecoded rather than as "", so the table never
      // implies a parameter was deliberately left unnamed.
      const shown = p.name ? `"${p.name}"` : "<name not decoded>";
      console.log(`  ${String(p.id).padEnd(4)} ${p.type.padEnd(14)} ${shown}${extra}`);
    }
  }
  console.log(`\n${Object.keys(out).length} effects decoded`);
  return 0;
}

process.exitCode = main();
