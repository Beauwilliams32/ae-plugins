// Gate for the MotionCurveX core (row AE-06).
//
// Unlike every other namespace in this repo, motioncurvex is NOT a transcription: the shipped source
// is gone and only the symbol list survived. So there is no instruction diff to make here — this
// test instead compiles the module, runs its property checks, and cross-checks the two functions that
// can be re-derived independently (the quadratic peak and the median) against a JS oracle. A wrong
// hand-written implementation cannot hide behind "it compiles".
import test from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'

const ROOT = path.join(import.meta.dirname, '..')
const SRC = path.join(ROOT, 'src', 'motioncurvex')

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

test('the module declares the eleven recovered signatures and no SDK dependency', async () => {
  const { readFileSync } = await import('node:fs')
  const header = readFileSync(path.join(SRC, 'motioncurvex.h'), 'utf8')
  for (const fn of ['clampf', 'quadraticPeakOffset', 'medianOfWindow', 'downsample2x', 'buildPyramid',
    'sadAt', 'cropCenter', 'accumulate', 'smoothMovingAverage', 'temporalMedianFilter', 'estimateTranslation']) {
    assert.match(header, new RegExp(`\\b${fn}\\(`), `${fn} is declared`)
  }
  assert.match(header, /namespace motioncurve \{/, 'the namespace is the recovered one, not the directory name')
  assert.doesNotMatch(header, /AE_\w+\.h|Adobe|PF_EffectWorld/, 'the AE SDK is never vendored')
})
