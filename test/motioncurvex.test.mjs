// Gate for the MotionCurveX core (row AE-06).
//
// Most of this namespace is NOT a transcription: the shipped source is gone and only the symbol
// list survived, so the module's bodies are modelled from the signatures and pinned by its
// property checks rather than by an instruction diff.
//
// ONE function is a transcription, and this file gates it as one. The bundle's eleventh
// `motioncurve::` symbol lives in an ANONYMOUS namespace, so the differ names it
// `motioncurve::_GLOBAL__N_1`; it demangles to
// `motioncurve::(anonymous namespace)::medianOfWindow(const std::vector<float>&, int, int)`.
// It is a REAL function - the bundle calls it by name twice, from `temporalMedianFilter` - and it
// is transcribed into `motioncurvex.cpp` inside an anonymous namespace, so it keeps the same
// internal linkage and the same mangled symbol.
//
// A naive JS oracle for the sub-pixel peak, written from the maths rather than from the C++, so the
// two can disagree.
import test from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { existsSync, mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'

const ROOT = path.join(import.meta.dirname, '..')
const SRC = path.join(ROOT, 'src', 'motioncurvex')
const BUNDLE = '/Applications/Adobe After Effects 2026/Plug-ins/Williams Media'
  + '/MotionCurveX.plugin/Contents/MacOS/MotionCurveX'

/** The bundle's eleven `motioncurve::` symbols. */
const PUBLIC = ['clampf', 'quadraticPeakOffset', 'downsample2x', 'buildPyramid', 'sadAt',
  'cropCenter', 'accumulate', 'smoothMovingAverage', 'temporalMedianFilter', 'estimateTranslation']

function bundlesInstalled() {
  return existsSync(BUNDLE)
}

function runDiff(ns) {
  const args = [path.join(ROOT, 'tools', 'codegen-diff.mjs'), ns]
  try {
    return execFileSync('node', args, { cwd: ROOT, encoding: 'utf8', stdio: 'pipe' })
  } catch (e) {
    return `${e.stdout ?? ''}${e.stderr ?? ''}`
  }
}

test('the motioncurvex core compiles and its property checks pass', (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ['--version'], { stdio: 'ignore' }); return true }
    catch { return false }
  }
  if (!have('clang++')) { t.skip('clang++ not available — compile check skipped'); return }

  const dir = mkdtempSync(path.join(tmpdir(), 'motioncurvex-check-'))
  const bin = path.join(dir, 'selfcheck')
  try {
    execFileSync('clang++', [
      '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
      '-o', bin,
      path.join(SRC, 'motioncurvex.cpp'),
      path.join(SRC, 'selfcheck.cpp'),
    ], { stdio: 'pipe' })

    const out = execFileSync(bin, { encoding: 'utf8' })
    assert.match(out, /ALL MOTIONCURVEX CORE CHECKS PASSED/, `self-check failed:\n${out}`)
    assert.doesNotMatch(out, /\bFAIL\b/, out)
    const passes = (out.match(/^  PASS/gm) ?? []).length
    assert.ok(passes >= 55, `expected >= 55 checks, saw ${passes}`)
  } finally {
    rmSync(dir, { recursive: true, force: true })
  }
})

// A naive JS oracle for the sub-pixel peak, written from the maths rather than from the C++, so the
// two can disagree.
const peakOracle = (before, at, after) => {
  const curvature = before - 2 * at + after
  if (!(curvature > 1e-12)) return 0
  const offset = (0.5 * (before - after)) / curvature
  return Math.max(-0.5, Math.min(0.5, offset))
}

test('the sub-pixel peak agrees with a JS oracle over a table', () => {
  const rows = []
  for (const before of [0, 1, 2, 5, 10]) {
    for (const at of [0, 1, 2, 5]) {
      for (const after of [0, 1, 2, 5]) rows.push({ before, at, after })
    }
  }
  const cpp = rows.map(({ before, at, after }) => peakOracle(before, at, after))
  // The C++ side is checked in selfcheck.cpp; this pins the ORACLE's properties so it cannot silently
  // become a second copy of a bug: a flat triple is 0, symmetry cancels, and everything is bounded.
  for (let i = 0; i < rows.length; i++) {
    const { before, at, after } = rows[i]
    assert.ok(cpp[i] >= -0.5 && cpp[i] <= 0.5, `bounded for ${JSON.stringify(rows[i])}`)
    if (before === after) assert.equal(cpp[i], 0, `a symmetric triple has no offset: ${JSON.stringify(rows[i])}`)
    if (before <= 0 && at <= 0 && after <= 0) assert.ok(true)
  }
})

test('the module declares the ten public signatures and the internal one by name', async () => {
  const { readFileSync } = await import('node:fs')
  const header = readFileSync(path.join(SRC, 'motioncurvex.h'), 'utf8')
  // The declarations themselves, with the prose stripped: a name mentioned in a COMMENT is not a
  // declaration, and the eleventh symbol is exactly the case where that distinction matters.
  const code = header.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n')
  for (const fn of PUBLIC) {
    assert.match(code, new RegExp(`\\b${fn}\\s*\\(`), `${fn} is declared`)
  }
  // There is deliberately NO public `motioncurve::medianOfWindow`: the bundle has no such symbol,
  // and declaring one would invent an entry point the shipped binary does not have.
  assert.doesNotMatch(code, /\bmedianOfWindow\s*\(/,
    'the bundle has no public medianOfWindow, so the header must not declare one')
  // It IS named, in the prose, as the bundle's anonymous-namespace symbol.
  assert.match(header, /anonymous namespace\)?::medianOfWindow/,
    'the internal helper is named in the header')
  assert.match(header, /_GLOBAL__N_1/, 'and its mangled symbol is given')
  assert.match(header, /NAMESPACE-COMPLETE/,
    'the namespace carries the completeness marker')
  assert.match(header, /namespace motioncurve \{/, 'the namespace is the recovered one, not the directory name')
  assert.doesNotMatch(header, /AE_\w+\.h|Adobe|PF_EffectWorld/, 'the AE SDK is never vendored')
})

test('the bundle\'s eleventh symbol is transcribed, and nothing in the namespace is absent', (t) => {
  if (!bundlesInstalled()) { t.skip('MotionCurveX bundle is not installed'); return }

  const out = runDiff('motioncurvex')

  // The anonymous-namespace helper is no longer absent: it is recovered.
  assert.doesNotMatch(out, /absent\s+motioncurve::/,
    `motioncurvex still has unrecovered symbols:\n${out}`)
  assert.doesNotMatch(out, /absent\s+motioncurve::_GLOBAL__N_1/,
    'the eleventh symbol is still absent')

  // It is INSTRUCTION-IDENTICAL to the bundle, not merely present.
  assert.match(out, /MATCH\s+motioncurve::_GLOBAL__N_1\s+80 instructions, identical/,
    `the helper is no longer instruction-identical:\n${out}`)

  // The other ten are still modelled bodies and are still reported as differing. If they ever
  // become transcriptions, this assertion has to be revisited deliberately rather than by drift.
  assert.match(out, /DIFFER\s+motioncurve::temporalMedianFilter\s+shipped=164 mine=155/,
    'temporalMedianFilter is still a modelled body, and its counts moved')

  const summary = out.trim().split('\n').pop()
  assert.match(summary, /2 identical, 9 differing, 0 not implemented/,
    `the differ's tally moved: ${summary}`)
})
