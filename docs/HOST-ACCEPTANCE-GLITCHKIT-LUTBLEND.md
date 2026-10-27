# Host acceptance — GlitchKit + LUTBlend (the two rebuildable bundles)

Two of the fleet's sixteen effects have a recovered source namespace complete enough to rebuild a
bundle: **GlitchKit** and **LUTBlend**. Every other effect still exists only as an installed binary
(`RECOVERY-STATUS.md`). This packet is the host-side acceptance run for a rebuilt GlitchKit/LUTBlend
bundle: it exists so the owner's session is ticking and signing rather than composing a record.

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

| Field | Value |
|---|---|
| Bundle | ☐ GlitchKit ☐ LUTBlend |
| sha256 of the built binary | ____________________________________________________________ |
| Size (bytes) | ______________ |
| Code signature | ad-hoc (`codesign --sign -`) unless a Developer ID run is authorised — record which |
| AE build string from the About box | ______________ |

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
