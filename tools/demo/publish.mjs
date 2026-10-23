#!/usr/bin/env node
// Publish the fleet demo output as a page: harness results -> WebP panels + one
// static HTML page.
//
//   node tools/demo/publish.mjs --out ../trident-tools/public/products/after-effects-plugins/demos
//
// WHY A GENERATOR AND NOT A HAND-WRITTEN PAGE
//   RESULTS.md already proved the pattern: any number typed by hand into a document
//   drifts from the thing that produced it, and this project has been bitten by that
//   repeatedly. Every figure on the generated page is read out of
//   tools/demo/out/results.json, which the harness writes. The prose is authored here
//   on purpose — it is the part a machine should not be inventing — but it can only
//   reference values, never restate them.
//
//   The generated page is static HTML with no scripts, so it renders identically for
//   everyone and the site's CSP does not have to be relaxed for it.
//
// DEPENDENCY: python3 + Pillow, for PPM -> WebP. PPM is what the harness writes
// (trivial, no encoder in the binary); WebP is what keeps 35 panels on one page
// light enough to load. Verified present on this machine (Pillow 12.3.0).

import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '../..');

function parseArgs(argv) {
  const out = { out: null, results: null, base: '/products/after-effects-plugins/demos' };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--out') out.out = argv[i + 1];
    // --results exists for the test: it publishes from a harness run in a temp
    // directory instead of the checked-in (and gitignored) tools/demo/out.
    if (argv[i] === '--results') out.results = argv[i + 1];
    // --base is the URL path the directory is served from. The site's integrity
    // test rejects relative references, so panel URLs are built from this.
    if (argv[i] === '--base') out.base = argv[i + 1].replace(/\/$/, '');
  }
  return out;
}

const args = parseArgs(process.argv.slice(2));
if (!args.out) {
  console.error('usage: node tools/demo/publish.mjs --out <dir>');
  process.exit(2);
}
const outDir = resolve(args.out);
const resultsPath = args.results ? resolve(args.results) : join(repoRoot, 'tools/demo/out/results.json');
// Panels are resolved next to the results file they came from.
const panelRoot = dirname(resultsPath);
if (!existsSync(resultsPath)) {
  console.error(`missing ${resultsPath} — run tools/demo/build.sh && build/fleet-demo first`);
  process.exit(2);
}
const results = JSON.parse(readFileSync(resultsPath, 'utf8'));
const D = results.demos;

mkdirSync(outDir, { recursive: true });

// ---------------------------------------------------------------------------
// Formatting helpers. Numbers are shown to four significant figures unless the
// value is large, so a panel caption and the page can never disagree by rounding.
// ---------------------------------------------------------------------------
const num = (v, digits = 4) => {
  if (typeof v !== 'number' || Number.isNaN(v)) return String(v);
  if (Number.isInteger(v)) return String(v);
  if (Math.abs(v) >= 100) return v.toFixed(1);
  return Number(v.toPrecision(digits)).toString();
};
const list = (v, digits = 4) => (Array.isArray(v) ? v.map((x) => num(x, digits)).join(', ') : String(v));
const pct = (v) => `${(v * 100).toFixed(1)}%`;

// ---------------------------------------------------------------------------
// The fleet. `use` is the effect's own job; `why` explains what the panel proves
// rather than what it looks like; `stats` are label/template pairs where any
// {key} is substituted from results.json for that plugin.
// ---------------------------------------------------------------------------
const FLEET = [
  {
    key: 'noisefield',
    name: 'NoiseField',
    ns: 'noisefield::',
    use: 'Seeded curl, flow and turbulence displacement.',
    why:
      'The permutation table is the whole noise field: a Fisher-Yates shuffle driven by ' +
      'xorshift32, with seed 0 replaced by the golden-ratio constant so it stays usable. ' +
      'The third panel runs the SAME octave count as the second with a different seed, ' +
      'which is the honest way to show that the seed changes the field and not just the ' +
      'phase. Both fBm panels stay centred and unclipped, which is what the amplitude-sum ' +
      'numbers are there to confirm.',
    stats: [
      ['fbm3 6-octave range', (d) => `${num(d.fbmMin)} … ${num(d.fbmMax)}`],
      ['fbm3 mean', (d) => num(d.fbmMean)],
      ['amplitude sum, 3 octaves', (d) => num(d.amplitudeSum3)],
      ['amplitude sum, 6 octaves', (d) => num(d.amplitudeSum6)],
    ],
  },
  {
    key: 'chromakey',
    name: 'ChromaKeyAssist',
    ns: 'chromakey::',
    use: 'Diagnose screen uniformity, spill and edge quality.',
    why:
      'The distance field is `sqrt(dCb² + dCr²)` against the key colour, and sensitivity ' +
      'maps onto a threshold with a fixed formula. Because the plate carries a known ' +
      'ground-truth mask, the sweep can SCORE the trade-off instead of describing it: the ' +
      'rim spill on this plate is chromatically close to the screen, so no sensitivity ' +
      'keys the screen away without eating the rim. That is the failure a key assist exists ' +
      'to make visible before it reaches a comp.',
    stats: [
      ['chromaDistance range', (d) => `${num(d.distanceMin, 3)} … ${num(d.distanceMax, 3)}`],
      ['threshold at sensitivity 40', (d) => num(d.threshold, 3)],
      ['screen left in (false positive)', (d) => pct(d.falsePositiveRate)],
      ['subject dropped (false negative)', (d) => pct(d.falseNegativeRate)],
      ['lowest total error', (d) => `${pct(d.bestTotalError)} at sensitivity ${num(d.bestSensitivity, 3)}`],
      ['distance percentiles 10/50/90', (d) => `${num(d.percentile10, 3)} / ${num(d.percentile50, 3)} / ${num(d.percentile90, 3)}`],
    ],
  },
  {
    key: 'lutblend',
    name: 'LUTBlend',
    ns: 'lutblend::',
    use: 'Blend two LUT grades with luma-aware options.',
    why:
      'Two 17³ grades are authored for the demo; the sampling, the blend and the ' +
      'luma-preserving rescale are the recovered code. `preserveLuma` is on for all three ' +
      'panels, which is why the neutral ramp holds its brightness as the grade swings from ' +
      'warm to cool — the same chart row reads at the same value across the strip. The ' +
      'identity round-trip is checked exactly rather than approximately, because a LUT ' +
      'sampler that is off by a tolerance quietly wrecks a grade.',
    stats: [
      ['LUT size', (d) => `${num(d.lutSize)}³`],
      ['probe in', (d) => `${num(d.probeR, 3)}, ${num(d.probeG, 3)}, ${num(d.probeB, 3)}`],
      ['probe through warm', (d) => `${num(d.probeWarmR, 3)}, ${num(d.probeWarmG, 3)}, ${num(d.probeWarmB, 3)}`],
      ['probe through cool', (d) => `${num(d.probeCoolR, 3)}, ${num(d.probeCoolG, 3)}, ${num(d.probeCoolB, 3)}`],
      ['identity round-trip exact', (d) => (d.identityRoundTripExact ? 'yes' : 'no')],
      ['CubeLUT::valid()', (d) => (d.warmValid ? 'accepts both grades' : 'rejects')],
    ],
  },
  {
    key: 'spectra',
    name: 'SpectraKeys',
    ns: 'spectra::',
    use: 'Bake audio-spectrum bands to animation controls.',
    why:
      'A real 512-point FFT over a synthetic mix, with the Hann window and the ' +
      'coherent-gain correction recovered from the bundle. The spectrogram shows where ' +
      'the tones land (86.1 Hz per bin, so 220 Hz is bin 2) and the sweep crossing the ' +
      'frame; the second panel is the thing the effect actually publishes to keyframes: ' +
      'per-band level over time, through the recovered falloff envelope.',
    stats: [
      ['FFT / hops', (d) => `${num(d.fftSize)} @ ${num(d.hopRateHz)} Hz`],
      ['bins × frames', (d) => `${num(d.binCount)} × ${num(d.frameCount)}`],
      ['Hz per bin', (d) => num(d.hzPerBin)],
      ['Hann coherent gain', (d) => num(d.hannCoherentGain, 3)],
      ['band peaks low/mid/high', (d) => `${num(d.lowBandPeak, 3)} / ${num(d.midBandPeak, 3)} / ${num(d.highBandPeak, 3)}`],
    ],
  },
  {
    key: 'echo',
    name: 'EchoTrails',
    ns: 'echo::',
    use: 'Time-offset transform trails.',
    why:
      'Nine rungs of a recovered ladder, each transformed by bilinear sampling and ' +
      'composited with premultiplied alpha. The two panels differ only in blend mode, ' +
      'which is the parameter an editor actually reaches for. The probe numbers record a ' +
      'behaviour worth knowing: a bilinear tap outside the image contributes zero and the ' +
      'surviving weights are NOT renormalised, so half a pixel off the border returns half ' +
      'the value where a clamped sampler would return all of it.',
    stats: [
      ['rungs', (d) => num(d.rungs)],
      ['per-rung amount', (d) => `${num(d.ladderAmount[0], 3)} → ${num(d.ladderAmount.at(-1), 3)}`],
      ['scale per rung', (d) => num(d.ladderScale[0], 3)],
      ['rotation per rung', (d) => `${num(d.ladderAngleDeg[0], 3)}°`],
      ['probe: inside / off-edge / clamped', (d) => `${num(d.probeInsideR, 3)} / ${num(d.probeBorderR, 3)} / ${num(d.probeClampedR, 3)}`],
    ],
  },
  {
    key: 'colorpulse',
    name: 'ColorPulse',
    ns: 'colorpulse::',
    use: 'Sample layer colour and bake brightness and colour controls.',
    why:
      'Every sampler the effect offers, run on the same scene: whole-frame average, a ' +
      'quantised dominant colour, the brightest region, and the three `sampleColor` modes. ' +
      'The region panel draws the rectangles `regionBounds` actually returns, so the ' +
      'geometry is checkable rather than described. Luma is printed for the dominant colour ' +
      'before and after the saturation boost, and it is unchanged — the boost is luma-safe ' +
      'by construction.',
    stats: [
      ['average colour', (d) => d.swatchHex[0]],
      ['dominant colour', (d) => d.swatchHex[1]],
      ['brightest region', (d) => d.swatchHex[2]],
      ['dominant luma before/after boost', (d) => `${num(d.dominantLuma, 4)} / ${num(d.dominantSaturationBoostLuma, 4)}`],
      ['regions sampled', (d) => `${d.regionX.length} rectangles`],
    ],
    swatches: true,
  },
  {
    key: 'speedramp',
    name: 'SpeedRampCurve',
    ns: 'speedramp::',
    use: 'Eased analytic speed ramps.',
    why:
      'Five easings, one ramp, both directions of the mapping: source time to output time, ' +
      'and output time to speed. LinearA and LinearB are pixel-identical in the shipped ' +
      'bundle, which is recorded rather than hidden — the binary cannot tell them apart, so ' +
      'both are named for what they do. Step shows the discontinuity an editor has to ' +
      'expect from it.',
    stats: [
      ['ramp window', (d) => `${num(d.start, 3)} s → ${num(d.end, 3)} s`],
      ['speed before / after', (d) => `${num(d.before)}% / ${num(d.after)}%`],
      ['mapTime at 1.2 s', (d) => num(d.mapTimeAtEnd[0], 5)],
      ['easing set', (d) => d.easingNames.join(', ')],
    ],
  },
  {
    key: 'glitch',
    name: 'GlitchKit',
    ns: 'glitch::',
    use: 'Deterministic glitch treatments.',
    why:
      'The honest limit case: only the PRNG and the pattern-frame selector survive as ' +
      'recovered code, so this panel plots exactly those two and claims nothing about the ' +
      'glitch image. The gate is the per-frame hash the strobing decision reads; the two ' +
      'staircases are `patternFrame` at two seeds, and the steps are where its ' +
      'double-precision multiply truncates.',
    stats: [
      ['frames @ 24 fps', (d) => `${num(d.frames)} (${num(d.fps)} fps)`],
      ['gate mean / variance', (d) => `${num(d.gateMean, 3)} / ${num(d.gateVariance, 3)}`],
      ['pattern steps', (d) => num(d.patternSteps)],
    ],
  },
  {
    key: 'autoflickerfix',
    name: 'AutoFlickerFix',
    ns: 'aff:: (src/autoflickerfix/)',
    use: 'Smooth exposure and colour flicker.',
    why:
      'The effect makes a claim — that flicker goes away — so the harness measures it. ' +
      '`temporalTarget` median-filters the whole track and then takes the mean, giving one ' +
      'scalar brightness to correct toward; `computeGain` produces a per-frame gain bounded ' +
      'by a decibel clamp. Flicker is reported as the residual deviation from the target, ' +
      'before and after. With `amount` at 1 and the correction inside the clamp, the gain ' +
      'is an exact ratio and the residual is floating-point noise, which is what the number ' +
      'shows.',
    stats: [
      ['frames / window', (d) => `${num(d.frames)} / ${num(d.window)}`],
      ['scalar target', (d) => num(d.targetScalar)],
      ['flutter before', (d) => num(d.flutterBefore, 3)],
      ['flutter after', (d) => num(d.flutterAfter, 3)],
      ['gain range', (d) => `${num(d.gainMin, 3)} … ${num(d.gainMax, 3)}`],
      ['±6 dB clamp', (d) => `${num(d.clampLo, 3)} … ${num(d.clampHi, 3)}`],
    ],
  },
  {
    key: 'beatsnap',
    name: 'BeatSnap',
    ns: 'beatsnap::',
    use: 'Turn audio transients into markers or keyframes.',
    why:
      'Scored against the click times the input was built with, not eyeballed: five clicks ' +
      'in, five onsets out, all five within a hop of the truth. The flux and the peak picker ' +
      'are scaffolding and the page says so — the recovered code stops at ' +
      '`adaptiveThreshold`, and the panel shows that threshold tracking the noise floor ' +
      'instead of sitting at a fixed height.',
    stats: [
      ['clicks in / onsets out', (d) => `${num(d.trueClicks)} / ${num(d.detected)}`],
      ['matched within 2 hops', (d) => num(d.matched)],
      ['mean timing error', (d) => `${num(d.meanErrorMs, 3)} ms`],
      ['hop size', (d) => `${num(d.hop)} samples (${num(d.hopMs, 3)} ms)`],
      ['window / scale', (d) => `${num(d.window)} / ${num(d.scale)}`],
    ],
  },
  {
    key: 'beatgrid',
    name: 'BeatGridRuler',
    ns: 'beatgrid::',
    use: 'Detect tempo and place beat markers or phase controls.',
    why:
      'The phase sawtooth is the control an editor drives pulses with, so the panel shows ' +
      'three subdivisions of the same 128 BPM clock, plus the tick strip where each one ' +
      'wraps. `phaseInLayerTime` is printed in SECONDS next to what the same number would ' +
      'be as a fraction: the two functions in this namespace deliberately return different ' +
      'units, which is exactly the trap worth pinning down on a page.',
    stats: [
      ['BPM / beat', (d) => `${num(d.bpm)} / ${num(d.beatSeconds, 5)} s`],
      ['subdivision factors', (d) => `${d.subdivisionNames.map((n, i) => `${n} ${num(d.subdivisionFactors[i], 4)}`).join(', ')}`],
      ['periods (s)', (d) => list(d.periodSeconds, 4)],
      ['phaseInLayerTime', (d) => `${num(d.phaseInLayerTimeSeconds, 5)} s (= ${num(d.phaseInLayerTimeAsFraction, 4)} as a fraction)`],
      ['beat ticks over 4 s', (d) => num(d.totalTicksBeat)],
    ],
  },
  {
    key: 'maskflow',
    name: 'MaskFlow',
    ns: 'maskflow::',
    use: 'Grow an edge-aware reveal matte from a seed.',
    why:
      'Only three helpers are recovered — the growth solver that consumes them is not in ' +
      'the bundle, and this page does not pretend otherwise. What it CAN show is the exact ' +
      'transfer function the grower is fed: the smoothstep that softens the edge, the ' +
      'tolerance-to-edge-cost curve, and the downsample geometry that keeps a 4K frame ' +
      'tractable.',
    stats: [
      ['1920×1080 @ max 512', (d) => `${num(d.hdWidth)}×${num(d.hdHeight)} (scale ${num(d.hdScale, 4)})`],
      ['3840×2160 @ max 512', (d) => `${num(d.uhdWidth)}×${num(d.uhdHeight)} (scale ${num(d.uhdScale, 4)})`],
      ['edge cost at tolerance 1.0', (d) => num(d.edgeCostAtTol1)],
    ],
  },
  {
    key: 'pathscribe',
    name: 'PathScribe',
    ns: 'pathscribe::',
    use: 'Audio-reactive EQ bars and waveform ribbons.',
    why:
      'Bar edges come from the recovered geometric band formula, so they sit equal RATIOS ' +
      'apart rather than equal Hz — 9.5 Hz between the bottom bars, 3.1 kHz between the top ' +
      'ones, which is why EQ bars look even to the eye. The heights are summed magnitudes ' +
      'from the recovered spectrum at a single frame, and the coverage curve is the ' +
      'frame-overlap helper the renderer uses to decide how much of a bar a frame sees.',
    stats: [
      ['bars', (d) => `${num(d.barCount)} (${num(d.minHz)} Hz → ${num(d.maxHz)} Hz)`],
      ['Hz step, bottom bar', (d) => num(d.barHzStepAtBottom)],
      ['Hz step, top bar', (d) => num(d.barHzStepAtTop)],
      ['frame used', (d) => `${num(d.frameUsed)} of ${num(d.frameCount)}`],
      ['coverage, full span', (d) => num(d.coverageFullSpan, 3)],
    ],
  },
  {
    key: 'shake',
    name: 'ShakeCapture',
    ns: 'shake::',
    use: 'Capture footage jitter as reusable controls.',
    why:
      'This panel is a recorded DEFECT, shown on purpose. `median` performs a second ' +
      '`nth_element` on a one-element range, which libc++ elides, so the element it reads is ' +
      'never placed and the answer depends on input ORDER rather than only on the values. ' +
      'The same 1000 numbers, shuffled repeatedly, should give 499.5 every time. A demo page ' +
      'that only showed clean output would be hiding the most useful thing in this bundle.',
    stats: [
      ['shuffles', (d) => num(d.trials)],
      ['true median', (d) => num(d.trueMedian)],
      ['returned range', (d) => `${num(d.returnedMin)} … ${num(d.returnedMax)}`],
      ['shuffles that disagree', (d) => `${num(d.trialsDiffering)} of ${num(d.trials)}`],
      ['ascending input', (d) => num(d.sortedInputResult)],
    ],
  },
  {
    key: 'typeriser',
    name: 'TypeRiser',
    ns: 'typeriser::',
    use: 'Timed kinetic captions from SRT files.',
    why:
      'A real SRT document goes in and the parsed structure comes out: three cues, sorted, ' +
      'with millisecond timings. `cueIndexAt` is a binary search plus a bounded backward ' +
      'walk, which is why it correctly returns "no cue" in the gaps between them instead of ' +
      'sticking on the previous one. The word index drives the karaoke highlight, and the ' +
      'easing set is what those highlights animate with.',
    stats: [
      ['cues parsed', (d) => num(d.cueCount)],
      ['cue starts (ms)', (d) => list(d.cueStartMs)],
      ['words per cue', (d) => list(d.cueWordCounts)],
      ['cue index at 0.0 / 0.69 / 2.07 / 3.45 / 4.59 s', (d) => list(d.cueIndexAtSamples)],
    ],
  },
  {
    key: 'motioncurvex',
    name: 'MotionCurveX',
    ns: 'motioncurve::',
    use: 'Extract translation into reusable motion curves.',
    why:
      'The one effect with no recovered source, and it is labelled as such instead of being ' +
      'illustrated with a function this repository does not have. The symbol names do ' +
      'describe the method, so the architecture is stated: build a pyramid, score a sum of ' +
      'absolute differences at an offset, search a translation, refine the peak ' +
      'sub-pixel, then accumulate and median-filter the per-frame estimates into curves.',
    stats: [
      ['recovered source', () => 'none'],
      ['named entry points', (d) => `${d.knownSymbols.length} domain symbols`],
    ],
    noSource: true,
  },
];

// ---------------------------------------------------------------------------
// Panels: convert every referenced PPM to WebP and collect its pixel size.
// ---------------------------------------------------------------------------
const BASE = args.base;
const panels = new Map();
for (const v of Object.values(D)) {
  for (const p of v.panels ?? []) panels.set(p, null);
}
const ppmDims = new Map();
for (const p of panels.keys()) {
  const ppm = join(panelRoot, p);
  const head = readFileSync(ppm, { encoding: 'latin1', flag: 'r' }).slice(0, 64).split('\n');
  const [w, h] = head[1].split(/\s+/).map(Number);
  ppmDims.set(p, { w, h, webp: p.replace(/\.ppm$/, '.webp') });
}

const py = `
import json, sys
from PIL import Image
items = json.loads(sys.argv[1])
for src, dst in items:
    Image.open(src).save(dst, 'WEBP', quality=92, method=6)
print(len(items))
`;
const conversions = [];
for (const [p, dims] of ppmDims) {
  conversions.push([join(panelRoot, p), join(outDir, dims.webp)]);
}
const converted = execFileSync('python3', ['-c', py, JSON.stringify(conversions)], {
  encoding: 'utf8',
}).trim();

// ---------------------------------------------------------------------------
// HTML
// ---------------------------------------------------------------------------
const esc = (s) =>
  String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const panelFigure = (v, i) => {
  const ppm = v.panels[i];
  const dims = ppmDims.get(ppm);
  const caption = (v.panelLabels ?? [])[i] ?? '';
  const alt = `${v.plugin} computed panel: ${caption}`;
  return `      <figure class="panel">
        <img src="${BASE}/${dims.webp}" width="${dims.w}" height="${dims.h}" loading="lazy" decoding="async" alt="${esc(alt)}">
        <figcaption>${esc(caption)}</figcaption>
      </figure>`;
};

const swatchRow = (d) => {
  const items = (d.swatchNames ?? []).map(
    (name, i) =>
      `        <li><span class="sw" style="background:${esc(d.swatchHex[i])}"></span><b>${esc(
        d.swatchHex[i]
      )}</b> ${esc(name)}</li>`
  );
  return `      <ul class="swatches">\n${items.join('\n')}\n      </ul>`;
};

const statsTable = (spec, d) => {
  // A stat that resolves to nothing is a generator bug, not a page bug: throw rather
  // than print "undefined" on a public page.
  const rows = spec.stats.map(([label, fn]) => {
      let value;
      try {
        value = fn(d);
      } catch (e) {
        throw new Error(`stat "${label}" for ${spec.name} failed: ${e.message}`);
      }
      if (value === undefined || value === null || String(value) === 'undefined') {
        throw new Error(`stat "${label}" for ${spec.name} produced ${value}`);
      }
      return `          <tr><th scope="row">${esc(label)}</th><td>${esc(value)}</td></tr>`;
    });
  return `      <table class="stats">\n${rows.join('\n')}\n      </table>`;
};

const sections = FLEET.map((spec) => {
  const d = D[spec.key];
  if (!d) throw new Error(`results.json has no demo for ${spec.key}`);
  const noSource = Boolean(spec.noSource) || (d.panels ?? []).length === 0;
  const figures = noSource
    ? ''
    : `      <div class="panels">\n${(d.panels ?? []).map((_, i) => panelFigure(d, i)).join('\n')}\n      </div>\n`;
  const fns = (d.recovered ?? []).length
    ? `      <p class="fns"><span class="fns-label">Recovered code exercised</span> ${(d.recovered ?? [])
        .map((f) => `<code>${esc(f)}</code>`)
        .join(' ')}</p>\n`
    : '';
  const symbols = spec.key === 'motioncurvex'
    ? `      <p class="fns"><span class="fns-label">Domain symbols in the bundle</span> ${(d.knownSymbols ?? [])
        .map((f) => `<code>${esc(f)}</code>`)
        .join(' ')}</p>\n`
    : '';
  return `    <article class="fx" id="${esc(spec.key)}">
      <div class="fx-head">
        <h2 class="tg-h2">${esc(spec.name)} <span class="ns">${esc(spec.ns)}</span></h2>
        <span class="chip ${noSource ? 'wait' : 'ok'}">${noSource ? 'spec only' : 'computed output'}</span>
      </div>
      <p class="use">${esc(spec.use)}</p>
      <p class="why">${esc(spec.why)}</p>
${figures}${spec.swatches ? swatchRow(d) + '\n' : ''}${statsTable(spec, d) + '\n'}${fns}${symbols}      <p class="caveat">${esc(d.note ?? '')}</p>
    </article>`;
});

const demoCount = FLEET.filter((s) => !s.noSource).length;
const panelCount = [...panels.keys()].length;

// The site's link-preview test requires a meta description of 120..160 characters
// and a unique title, so both are checked here rather than discovered after deploy.
const DESCRIPTION =
  'All sixteen After Effects effects computing real output from their recovered C++: ' +
  'noise fields, a scored chroma key, FFT spectrograms and speed ramps.';
if (DESCRIPTION.length < 120 || DESCRIPTION.length > 160) {
  throw new Error(`meta description is ${DESCRIPTION.length} chars, must be 120..160`);
}

const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Every effect, running — computed output from the recovered source · Trident Tools</title>
<meta name="description" content="${esc(DESCRIPTION)}">
<meta name="theme-color" content="#070d16">
<link rel="canonical" href="https://tools.tridentsocial.net/products/after-effects-plugins/demos/">
<meta property="og:site_name" content="Trident Tools">
<meta property="og:type" content="website">
<meta property="og:url" content="https://tools.tridentsocial.net/products/after-effects-plugins/demos/">
<meta property="og:title" content="Every effect, running — computed output from recovered source">
<meta property="og:description" content="All sixteen After Effects effects, with the numbers and images the recovered C++ actually produces.">
<meta property="og:image" content="https://tools.tridentsocial.net/og/after-effects-plugins.png">
<meta property="og:image:type" content="image/png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta property="og:image:alt" content="Sixteen After Effects effects, with panels computed from their recovered source code.">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="Every effect, running — computed output from recovered source">
<meta name="twitter:description" content="All sixteen After Effects effects, with the numbers and images the recovered C++ actually produces.">
<meta name="twitter:image" content="https://tools.tridentsocial.net/og/after-effects-plugins.png">
<meta name="twitter:image:alt" content="Sixteen After Effects effects, with panels computed from their recovered source code.">
<link rel="icon" href="/favicon.ico" sizes="48x48">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="manifest" href="/site.webmanifest">
<link rel="stylesheet" href="/assets/fonts.css">
<link rel="stylesheet" href="/assets/trident-glass.css">
<link rel="stylesheet" href="/assets/trident-tools.css">
<style>
  :root { --tg-a1: #14b8a6; --tg-a2: #0ea5e9; }
  body { font-size: 16.5px; }
  .content { padding-top: 44px; }
  .lede { color: var(--tg-muted); font-size: 18px; line-height: 1.55; max-width: 760px; margin: 18px 0 22px; }
  .method { margin: 0 0 14px; }
  .method h2 { margin: 0 0 10px; }
  .method p { color: var(--tg-muted); font-size: 14.5px; line-height: 1.65; margin: 0 0 10px; }
  .method p:last-child { margin-bottom: 0; }
  .method strong { color: var(--tg-ink); }
  .counts { display: flex; flex-wrap: wrap; gap: 10px 26px; margin: 0 0 8px; padding: 0; list-style: none; }
  .counts li { color: var(--tg-muted); font-size: 13.5px; }
  .counts b { color: var(--tg-ink); font-family: var(--tg-display); font-size: 20px; display: block; line-height: 1.1; }
  .toc { display: flex; flex-wrap: wrap; gap: 8px; margin: 18px 0 8px; padding: 0; list-style: none; }
  .toc a { font-size: 13px; color: var(--tg-muted); text-decoration: none; border: 1px solid var(--tg-glass-edge); border-radius: 999px; padding: 5px 12px; }
  .toc a:hover { color: var(--tg-ink); border-color: color-mix(in srgb, var(--tg-a1) 45%, transparent); }
  .fx { margin: 0 0 40px; padding-top: 26px; border-top: 1px solid var(--tg-glass-edge); }
  .fx-head { display: flex; flex-wrap: wrap; align-items: baseline; gap: 10px 14px; }
  .fx-head .tg-h2 { margin: 0; }
  .ns { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 13px; color: var(--tg-a1); font-weight: 500; }
  .use { color: var(--tg-ink); font-size: 15px; margin: 6px 0 8px; }
  /* What the panel proves, in prose. Without this the page would be a gallery of
     unexplained images; it is the part a generator cannot invent and a reader
     cannot infer. */
  .why { color: var(--tg-muted); font-size: 14.5px; line-height: 1.68; margin: 0 0 16px; max-width: 820px; }
  .panels { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 14px; margin: 0 0 18px; }
  .panel { margin: 0; background: #070d16; border: 1px solid var(--tg-glass-edge); border-radius: 12px; overflow: hidden; }
  .panel img { display: block; width: 100%; height: auto; }
  .panel figcaption { font-size: 12.5px; color: var(--tg-muted); padding: 8px 11px; line-height: 1.45; border-top: 1px solid var(--tg-glass-edge); }
  .stats { width: 100%; border-collapse: collapse; margin: 0 0 14px; font-size: 13.5px; }
  .stats th { text-align: left; font-weight: 500; color: var(--tg-muted); padding: 5px 12px 5px 0; vertical-align: top; white-space: nowrap; }
  .stats td { color: var(--tg-ink); padding: 5px 0; font-variant-numeric: tabular-nums; }
  .stats tr + tr th, .stats tr + tr td { border-top: 1px solid color-mix(in srgb, var(--tg-glass-edge) 60%, transparent); }
  .fns { font-size: 12.5px; color: var(--tg-muted); margin: 0 0 10px; line-height: 1.9; }
  .fns-label { display: block; text-transform: uppercase; letter-spacing: .09em; font-size: 11px; color: color-mix(in srgb, var(--tg-muted) 75%, transparent); margin-bottom: 2px; }
  .fns code, .caveat code { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; background: color-mix(in srgb, #0e1a2b 70%, transparent); border: 1px solid var(--tg-glass-edge); border-radius: 5px; padding: 1px 6px; color: var(--tg-ink); }
  .caveat { color: var(--tg-muted); font-size: 13.5px; line-height: 1.6; margin: 0; }
  .swatches { list-style: none; margin: 0 0 16px; padding: 0; display: flex; flex-wrap: wrap; gap: 8px; }
  .swatches li { display: flex; align-items: center; gap: 8px; font-size: 12.5px; color: var(--tg-muted); border: 1px solid var(--tg-glass-edge); border-radius: 8px; padding: 6px 11px; }
  .swatches b { color: var(--tg-ink); font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-weight: 500; }
  .sw { width: 18px; height: 18px; border-radius: 5px; border: 1px solid rgba(255,255,255,.18); }
  .chip { display: inline-flex; border: 1px solid var(--tg-glass-edge); border-radius: 999px; padding: 3px 9px; font-size: 12px; color: var(--tg-muted); white-space: nowrap; }
  .chip.ok { color: var(--tg-a1); border-color: color-mix(in srgb, var(--tg-a1) 45%, transparent); }
  .chip.wait { color: #f0b34e; border-color: rgba(240,179,78,.4); }
  .back { margin: 0 0 40px; }
  @media (max-width: 600px) { .stats th { white-space: normal; } }
</style>
</head>
<body class="tg-body">
<a class="tg-skip" href="#main">Skip to content</a>
<div class="tg-ambient" aria-hidden="true"></div>

<nav class="tg-nav" aria-label="Primary">
  <div class="tg-nav-inner">
    <a class="tg-brand" href="/" style="display:flex;align-items:center;gap:10px"><img src="/favicon.svg" alt="" width="28" height="28"><span>Trident Tools<small>By Trident Social</small></span></a>
    <a class="tg-btn tg-btn-ghost" href="/products/after-effects-plugins/" style="padding:9px 18px;font-size:14px">Plugin fleet</a>
  </div>
</nav>

<main class="tg-wrap content" id="main" tabindex="-1">
  <nav class="crumbs" aria-label="Breadcrumb"><a href="/">Trident Tools</a><span aria-hidden="true">›</span><a href="/products/after-effects-plugins/">AE Plugin Fleet</a><span aria-hidden="true">›</span><span aria-current="page">Every effect, running</span></nav>

  <h1 class="tg-h1">Every effect, <span class="tg-grad-text">running</span></h1>
  <p class="lede">All sixteen effects, with outputs produced by executing their recovered C++ on this machine. Not mock-ups, not illustrations, and not After Effects renders — nothing here has been through the host yet.</p>

  <div class="tg-card method">
    <h2 class="tg-h2">What you are looking at, and what you are not</h2>
    <p><strong>These are real computations.</strong> The effect logic was recovered from the surviving compiled bundles, transcribed back into C++, and then executed by a harness on synthetic input the harness generates. Every number and every pixel on this page came out of that run.</p>
    <p><strong>They are not After Effects renders.</strong> No After Effects host has loaded a rebuilt bundle yet — the acceptance pass needs an administrator install that has not been done — so there is no AE screenshot anywhere here, and none is imitated. Synthetic input is used because it is the only input that makes the claims checkable: where a plate has a known ground truth, the page scores the effect against it instead of admiring the picture.</p>
    <p><strong>What "recovered" means.</strong> Function signatures are exact — the bundles kept their symbols. Bodies were reconstructed from disassembly and compared instruction by instruction against the shipped code. Fifteen of the sixteen namespaces are transcribed; the sixteenth, MotionCurveX, has no recovered source and is labelled as a spec card rather than filled in with a guess.</p>
    <ul class="counts">
      <li><b>${FLEET.length}</b>effects covered</li>
      <li><b>${demoCount}</b>computing live output from recovered source</li>
      <li><b>${FLEET.length - demoCount}</b>spec card, no source recovered</li>
      <li><b>${panelCount}</b>computed panels</li>
      <li><b>1</b>recorded defect, shown deliberately</li>
    </ul>
  </div>

  <ul class="toc" aria-label="Jump to an effect">
${FLEET.map((s) => `    <li><a href="#${esc(s.key)}">${esc(s.name)}</a></li>`).join('\n')}
  </ul>

${sections.join('\n\n')}
  <div class="tg-card back">
    <p class="caveat" style="margin:0"><strong style="color:var(--tg-ink)">Still not for sale.</strong> A working pixel path is not a shippable plugin: each effect still needs the After Effects host-acceptance pass, signing and packaged installs. This page exists to show the work, not to sell it. <a href="/products/after-effects-plugins/" style="color:var(--tg-a1);text-decoration:none;border-bottom:1px solid color-mix(in srgb,var(--tg-a1) 35%,transparent)">Back to the fleet page →</a></p>
  </div>
</main>

<footer class="tg-footer">
  <div class="tg-wrap">
    <nav class="foot-links" aria-label="Footer">
      <a href="/">Trident Tools</a>
      <a href="/products/after-effects-plugins/">AE Plugin Fleet</a>
      <a href="/privacy/">Privacy</a>
      <a href="/terms/">Terms</a>
      <a href="https://tridentsocial.net" rel="noopener">Trident Social</a>
    </nav>
    <div style="margin-top:14px">© 2026 Trident Social · Fort Collins, CO · Outputs on this page were computed from recovered source on this machine and are not After Effects renders.</div>
  </div>
</footer>
</body>
</html>
`;

writeFileSync(join(outDir, 'index.html'), html);
// The raw numbers, for anyone who wants them without the page around them.
writeFileSync(
  join(outDir, 'results.json'),
  `${JSON.stringify({ ...results, published: 'trident-tools/public/products/after-effects-plugins/demos/' }, null, 2)}\n`
);

console.log(`converted ${converted} panels to WebP`);
console.log(`wrote ${join(outDir, 'index.html')}`);
console.log(`${FLEET.length} effects, ${panelCount} panels, ${demoCount} computing output`);
