# Host acceptance — GlitchKit + LUTBlend (the two rebuildable bundles)

As of 2026-10-28 **all sixteen** of the fleet's namespaces are recovered - 166 of 166 symbols, every
effect at 100% instruction coverage (`RECOVERY-STATUS.md`). What still limits the rebuildable set is
not the DSP but the **written host layer** - `EffectMain`, parameter registration and the render
callback - which exists today only for **GlitchKit** and **LUTBlend**. Those two are therefore the
rebuildable bundles, and this packet is the host-side acceptance run for them: it exists so the
owner's session is ticking and signing rather than composing a record.

**Nothing below is pre-ticked. A blank box is an unobserved result.** No After Effects host has ever
loaded a rebuilt bundle, so every box starts empty and stays empty until someone runs the checks in
AE itself.

## Environment

Measured on this machine **2026-10-26** (`sw_vers`, `uname -m`,
`/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "/Applications/Adobe After Effects 2026/Adobe After Effects 2026.app/Contents/Info.plist"`).
Confirm each value on the machine you actually test.

| Field | Value |
|---|---|
| After Effects version | 26.5.0 (AE 2026) |
| macOS version | 27.0.1 (build 26A434) |
| Architecture | arm64 |
| Bundle under test | ☐ GlitchKit ☐ LUTBlend (one run each) |
| Bundle, minimum OS / plugin flags | from `specs/<namespace>.json` — record what the spec declares, not what the host accepts |
| Built from | `src/glitch/`, `src/lutblend/` at the commit named in the sign-off |

## Artifact under test

Rebuild on the source machine, then carry only the bundle (`.plugin`) to the test machine:

```bash
cd ae-plugins
npm test                     # clang++ codegen diff against the shipped bundles
npm run recover              # regenerates specs/ and build/ (never hand-edited)
shasum -a 256 build/GlitchKit.plugin/Contents/MacOS/GlitchKit   # and LUTBlend
```

The structural comparison runs as part of the build, so the rebuild cannot be produced
without answering it. What it measures, and what it does not, is spelled out below.

| Field | GlitchKit | LUTBlend |
|---|---|---|
| Bundle | ☐ GlitchKit | ☐ LUTBlend |
| sha256 of the built binary | `8db7e4503a4c75933d54535205fba6a3df9f4182f5fb53f26eb492d15e9aa633` | `87a41d9af85406c39eb65c8aac7a66a58dd5e27cf14b879d9ed29f181d0f67d4` |
| Size (bytes) | 117488 | 219536 |
| Architectures | `x86_64 arm64` | `x86_64 arm64` |
| Code signature | ad-hoc, `Identifier=com.williamsmedia.aftereffects.GlitchKit`, sealed resources v2 | ad-hoc, `Identifier=com.williamsmedia.aftereffects.LUTBlend`, sealed resources v2 |
| PiPL payload vs the shipped bundle | identical (576 bytes) | identical (576 bytes) |
| AE build string from the About box | ______________ | ______________ |

The two sha256 values are for the build produced on this machine by
`tools/build-plugin.sh <Name>` at the commit named below; a rebuild elsewhere embeds
its own paths and will not hash the same. The PiPL resource map (the bytes after the
576-byte payload) differs by one bookkeeping byte from the shipped bundles for
LUTBlend, because the shipped fleet was compiled by the original authors' Rez and this
machine's Rez writes the count differently. The resource SET and its ORDER are identical.
That byte is outside the flag contract and outside what `test/pipl.test.mjs` compares.

Built from commit: `________________________` (fill in before the run)

### What the build's own comparison proves

`tools/build-plugin.sh` compares the rebuilt bundle against the shipped one field by
field - architectures, creator code, the whole `Info.plist`, the signature's Identifier
and kind, the sealed-resources version, and the PiPL payload - and exits non-zero on any
mismatch. For both bundles above every field matched. It was negative-controlled: a build
with a wrong creator code fails the comparison and stops.

**That is still not host acceptance.** It says the bundle is built the way the one that
shipped was built. It says nothing about whether After Effects renders with it, which is
what the checks below are for.

**Installing into `/Applications/Adobe After Effects 2026/Plug-ins/` needs administrator rights**
(`RECOVERY-STATUS.md`). That is an owner action: an agent may not install, and may not remove or
touch the installed bundles or the root-owned `.install-backups` — those are the only remaining
artefacts of the shipped binaries (2026-10-13 incident).

## Host checks

Run each with a **disposable comp** (nothing client-facing open). Record what you saw, not what
should have happened.

| # | Check | Result |
|---|---|---|
| 1 | The effect appears under the expected AE menu group with the expected name | ☐ observed ☐ not observed |
| 2 | Applying it to a solid renders at **8 bpc** without error, and the frame is not black | ☐ ☐ |
| 3 | The same comp at **16 bpc** renders, and the result differs from 8 bpc only as the effect intends | ☐ ☐ |
| 4 | The same comp at **32 bpc** renders, and no clipping/NaN artefacts appear | ☐ ☐ |
| 5 | Every parameter in `PARAMETERS.md` for this effect is present in the Effect Controls panel | ☐ ☐ |
| 6 | Changing a parameter updates the render (no stale frame, no manual refresh) | ☐ ☐ |
| 7 | **Undo / redo** restores parameter values and the rendered result | ☐ ☐ |
| 8 | No **version-mismatch** dialog on load or on apply | ☐ ☐ |
| 9 | No crash on: apply → change every parameter → undo all → remove the effect → close the comp | ☐ ☐ |
| 10 | Render queue: the same comp renders through the queue to a file with no error | ☐ ☐ |
| 11 | Uninstall: quit AE, remove the bundle, relaunch — the effect is gone and AE shows no error | ☐ ☐ |

Anything unexpected goes in a sentence under the table, with the AE build string and the bundle
sha256. A failed check is a real result: say which one failed and what the host said.

### Notes on what these checks cannot prove

- A green `npm test` is a codegen comparison against the shipped binaries, **not** host behaviour;
  it says the recovered source compiles to the same instructions, not that AE accepts the bundle.
- The 16 installed bundles are untouched by this packet. If a rebuilt bundle is installed beside
  them, the installed ones must still be present afterwards.

## Sign-off

Nothing below is pre-ticked. Ticking is the owner's act, and it is the only thing that turns this
packet into acceptance evidence.

- Confirmed no prior host run of a rebuilt bundle: ☐ *(this line must stay true; if a rebuilt bundle
  has already been loaded in AE, say so in the notes instead of ticking it)*
- Tester: ___
- Date: ___
- AE build: ___
- Verdict: ☐ accepted ☐ accepted with notes ☐ rejected
- Notes: _________________________________________________________________________

This packet records host acceptance only. It carries **no** price, licence scope, or refund policy,
and nothing here is a shipping decision — the fleet's direction is the owner's call.
