#!/usr/bin/env node
// Decode each effect's recovered eGLO / eGL2 / eINF PiPL atoms into named
// PF_OutFlag values, and emit CAPABILITIES.md.
//
// WHY THIS MATTERS. Those three atoms are what AE's plugin host reads to decide
// HOW to call an effect. They are not documentation — they are the contract:
//
//   * PF_OutFlag_DEEP_COLOR_AWARE  -> the host may supply 16-bit worlds, so a
//                                     16-bit render path MUST exist.
//   * PF_OutFlag2_FLOAT_COLOR_AWARE -> the host may supply 32-bpc float data.
//                                     An effect that sets this and reads 8-bit
//                                     pixels misreads every frame.
//   * PF_OutFlag2_SUPPORTS_SMART_RENDER -> obliges a PF_Cmd_SMART_RENDER handler;
//                                     there is no sensible no-op.
//   * PF_OutFlag2_SUPPORTS_THREADED_RENDERING -> renders may run concurrently, so
//                                     any shared state must be locked.
//
// LUTBlend shipped with three of these invented rather than recovered, which is
// why the whole fleet is now decoded rather than trusted.
//
//   node tools/decode-flags.mjs
//
// The flag values below are the AE SDK's own, transcribed from AE_Effect.h. When
// an SDK is present, test/capabilities.test.mjs re-derives them from the header
// and fails if this table has drifted.

import { readFileSync, writeFileSync, readdirSync, existsSync } from "node:fs";
import path from "node:path";

const SPECS = path.join(import.meta.dirname, "..", "specs");
const OUT = path.join(import.meta.dirname, "..", "CAPABILITIES.md");

// AE SDK 25.6, AE_Effect.h. Verified against the header by the test.
const OUT_FLAGS = {
  PF_OutFlag_KEEP_RESOURCE_OPEN: 1 << 0,
  PF_OutFlag_WIDE_TIME_INPUT: 1 << 1,
  PF_OutFlag_NON_PARAM_VARY: 1 << 2,
  PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING: 1 << 4,
  PF_OutFlag_I_DO_DIALOG: 1 << 5,
  PF_OutFlag_USE_OUTPUT_EXTENT: 1 << 6,
  PF_OutFlag_SEND_DO_DIALOG: 1 << 7,
  PF_OutFlag_DISPLAY_ERROR_MESSAGE: 1 << 8,
  PF_OutFlag_I_EXPAND_BUFFER: 1 << 9,
  PF_OutFlag_PIX_INDEPENDENT: 1 << 10,
  PF_OutFlag_I_WRITE_INPUT_BUFFER: 1 << 11,
  PF_OutFlag_I_SHRINK_BUFFER: 1 << 12,
  PF_OutFlag_WORKS_IN_PLACE: 1 << 13,
  PF_OutFlag_CUSTOM_UI: 1 << 15,
  PF_OutFlag_REFRESH_UI: 1 << 17,
  PF_OutFlag_NOP_RENDER: 1 << 18,
  PF_OutFlag_I_USE_SHUTTER_ANGLE: 1 << 19,
  PF_OutFlag_I_USE_AUDIO: 1 << 20,
  PF_OutFlag_I_AM_OBSOLETE: 1 << 21,
  PF_OutFlag_FORCE_RERENDER: 1 << 22,
  PF_OutFlag_I_HAVE_EXTERNAL_DEPENDENCIES: 1 << 24,
  PF_OutFlag_DEEP_COLOR_AWARE: 1 << 25,
  PF_OutFlag_SEND_UPDATE_PARAMS_UI: 1 << 26,
  PF_OutFlag_AUDIO_FLOAT_ONLY: 1 << 27,
  PF_OutFlag_AUDIO_IIR: 1 << 28,
  PF_OutFlag_I_SYNTHESIZE_AUDIO: 1 << 29,
  PF_OutFlag_AUDIO_EFFECT_TOO: 1 << 30,
  PF_OutFlag_AUDIO_EFFECT_ONLY: 1 << 31,
};

const OUT_FLAGS2 = {
  PF_OutFlag2_SUPPORTS_QUERY_DYNAMIC_FLAGS: 1 << 0,
  PF_OutFlag2_I_USE_3D_CAMERA: 1 << 1,
  PF_OutFlag2_I_USE_3D_LIGHTS: 1 << 2,
  PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG: 1 << 3,
  PF_OutFlag2_I_AM_THREADSAFE: 1 << 4,
  PF_OutFlag2_CAN_COMBINE_WITH_DESTINATION: 1 << 5,
  PF_OutFlag2_DOESNT_NEED_EMPTY_PIXELS: 1 << 6,
  PF_OutFlag2_REVEALS_ZERO_ALPHA: 1 << 7,
  PF_OutFlag2_PRESERVES_FULLY_OPAQUE_PIXELS: 1 << 8,
  PF_OutFlag2_SUPPORTS_SMART_RENDER: 1 << 10,
  PF_OutFlag2_FLOAT_COLOR_AWARE: 1 << 12,
  PF_OutFlag2_I_USE_COLORSPACE_ENUMERATION: 1 << 13,
  PF_OutFlag2_I_AM_DEPRECATED: 1 << 14,
  PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT: 1 << 17,
  PF_OutFlag2_I_USE_TIMECODE: 1 << 18,
  PF_OutFlag2_DEPENDS_ON_UNREFERENCED_MASKS: 1 << 19,
  PF_OutFlag2_OUTPUT_IS_WATERMARKED: 1 << 20,
  PF_OutFlag2_I_MIX_GUID_DEPENDENCIES: 1 << 21,
  PF_OutFlag2_AE13_5_THREADSAFE: 1 << 22,
  PF_OutFlag2_SUPPORTS_GET_FLATTENED_SEQUENCE_DATA: 1 << 23,
  PF_OutFlag2_CUSTOM_UI_ASYNC_MANAGER: 1 << 24,
  PF_OutFlag2_SUPPORTS_GPU_RENDER_F32: 1 << 25,
  PF_OutFlag2_SUPPORTS_THREADED_RENDERING: 1 << 27,
  PF_OutFlag2_MUTABLE_RENDER_SEQUENCE_DATA_SLOWER: 1 << 28,
};

// eGLO / eGL2 are 32-bit unsigned. The PiPL stores them as hex strings.
const bits32 = (v) => (v >>> 0);
const bitAt = (v, n) => (bits32(v) & (1 << n)) !== 0;

function nameFlags(value, table) {
  const named = [];
  let unclaimed = bits32(value);
  for (const [name, bit] of Object.entries(table)) {
    if (bits32(value) & bit) {
      named.push(name);
      unclaimed &= ~bit;
    }
  }
  return { named, unclaimed: bits32(unclaimed) };
}

function decode(value, table) {
  const { named, unclaimed } = nameFlags(value, table);
  return {
    raw: `0x${bits32(value).toString(16).padStart(8, "0")}`,
    named,
    unclaimed,
  };
}

function loadSpecs() {
  return readdirSync(SPECS)
    .filter((f) => f.endsWith(".json") && f !== "index.json")
    .sort()
    .map((f) => JSON.parse(readFileSync(path.join(SPECS, f), "utf8")));
}

function main() {
  const specs = loadSpecs();
  if (!specs.length) {
    console.error("no specs found — run tools/recover.mjs first");
    return 1;
  }

  const rows = specs.map((s) => ({
    plugin: s.plugin,
    glo: decode(Number(s.pipl.eGLO), OUT_FLAGS),
    gl2: decode(Number(s.pipl.eGL2), OUT_FLAGS2),
  }));

  const short = (f) =>
    f.named.map((n) => n.replace(/^PF_OutFlag2?_/, "")).join(", ") || "none";

  const wantDepth = [];
  const wantFloat = [];
  const wantSmart = [];
  const wantThreaded = [];
  const claimButNoFloat = [];

  for (const r of rows) {
    const deep = r.glo.named.includes("PF_OutFlag_DEEP_COLOR_AWARE");
    const float = r.gl2.named.includes("PF_OutFlag2_FLOAT_COLOR_AWARE");
    const smart = r.gl2.named.includes("PF_OutFlag2_SUPPORTS_SMART_RENDER");
    const threaded = r.gl2.named.includes("PF_OutFlag2_SUPPORTS_THREADED_RENDERING");
    if (deep) wantDepth.push(r.plugin);
    if (float) wantFloat.push(r.plugin);
    if (smart) wantSmart.push(r.plugin);
    if (threaded) wantThreaded.push(r.plugin);
  }

  const lines = [];
  lines.push("# Capability contract per effect");
  lines.push("");
  lines.push("GENERATED by `tools/decode-flags.mjs` from `specs/*.json`. Do not edit.");
  lines.push("");
  lines.push(
    "`eGLO`, `eGL2` and `eINF` are the PiPL atoms AE's host reads to decide *how* to",
    "call an effect. They are a contract, not a description: each named flag below is",
    "an obligation the render path has to meet. See `plugins/LUTBlend/README.md` for",
    "what happens when one is claimed and not honoured — LUTBlend shipped with three",
    "of these invented rather than recovered."
  );
  lines.push("");
  lines.push("## Obligations across the fleet");
  lines.push("");
  lines.push("| Obligation | Effects | Consequence for a rebuild |");
  lines.push("|---|---|---|");
  lines.push(
    `| \`PF_OutFlag_DEEP_COLOR_AWARE\` | ${wantDepth.length}/${rows.length} | a **16-bit** render path is required |`
  );
  lines.push(
    `| \`PF_OutFlag2_FLOAT_COLOR_AWARE\` | ${wantFloat.length}/${rows.length} | a **32-bpc float** render path is required |`
  );
  lines.push(
    `| \`PF_OutFlag2_SUPPORTS_SMART_RENDER\` | ${wantSmart.length}/${rows.length} | a \`PF_Cmd_SMART_RENDER\` handler is required |`
  );
  lines.push(
    `| \`PF_OutFlag2_SUPPORTS_THREADED_RENDERING\` | ${wantThreaded.length}/${rows.length} | shared render state must be thread-safe |`
  );
  lines.push("");

  if (wantFloat.length) {
    lines.push(`**Float-capable:** ${wantFloat.join(", ")} — these cannot share a render`);
    lines.push("path with the 8/16-bit effects.");
    lines.push("");
  }
  if (wantSmart.length) {
    lines.push(`**Smart-render:** ${wantSmart.join(", ")} — these need a`);
    lines.push("`PF_Cmd_SMART_RENDER` handler, which is a substantially larger piece of");
    lines.push("host integration than a plain `PF_Cmd_RENDER` path.");
    lines.push("");
  }

  lines.push("## Per-effect flags");
  lines.push("");
  lines.push("| Effect | `eGLO` | `eGL2` | `eINF` |");
  lines.push("|---|---|---|---|");
  for (let i = 0; i < rows.length; i++) {
    const r = rows[i];
    lines.push(
      `| ${r.plugin} | \`${r.glo.raw}\` ${short(r.glo)} | \`${r.gl2.raw}\` ${short(r.gl2)} | \`${specs[i].pipl.eINF}\` |`
    );
  }
  lines.push("");

  const stray = rows.filter((r) => r.glo.unclaimed || r.gl2.unclaimed);
  if (stray.length) {
    lines.push("## Unrecognised bits");
    lines.push("");
    lines.push("Bits set in the recovered atoms that this decoder cannot name. Either the");
    lines.push("flag table above is incomplete or the value is not a flag word:");
    lines.push("");
    for (const r of stray) {
      if (r.glo.unclaimed)
        lines.push(`* ${r.plugin} \`eGLO\` leaves 0x${r.glo.unclaimed.toString(16)} unclaimed`);
      if (r.gl2.unclaimed)
        lines.push(`* ${r.plugin} \`eGL2\` leaves 0x${r.gl2.unclaimed.toString(16)} unclaimed`);
    }
    lines.push("");
  } else {
    lines.push(
      "Every bit in every recovered atom is accounted for, so the flag table covers the"
    );
    lines.push("whole fleet with nothing left over.");
    lines.push("");
  }

  writeFileSync(OUT, lines.join("\n"));
  console.log(`decoded ${rows.length} effects -> CAPABILITIES.md`);
  console.log(`  deep: ${wantDepth.length}  float: ${wantFloat.length}  smart: ${wantSmart.length}  threaded: ${wantThreaded.length}`);
  if (stray.length) console.log(`  WARNING: ${stray.length} effects have unrecognised bits`);
  return 0;
}

process.exitCode = main();
