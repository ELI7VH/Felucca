// SPDX-License-Identifier: GPL-3.0-only
// (WaveLoop Kit: from WaveLoop's Switch Blade build; ARP_NMAX is the Kit's 30, every case is checked against it.)
// The ARP's golden vectors: WaveLoop's Shape Arp core (web/src/arp/core/sequence.mjs in the WaveLoop repo) plays a set
// of cases, and what it plays is written as C for tests/arp_shape_test.c, which plays the same cases through
// firmware/src/arp_shape.c and wants exactly the same notes.
//   node tools/arp_vectors.mjs [path/to/sequence.mjs] > tests/arp_vectors.h
//   node tools/arp_vectors.mjs --chords [path/to/sequence.mjs] > tests/arp_chords_vectors.h
//   node tools/arp_vectors.mjs --zoom [path/to/sequence.mjs] > tests/arp_zoom_vectors.h
//   node tools/arp_vectors.mjs --shift [path/to/sequence.mjs] > tests/arp_shift_vectors.h
// --shift: the ARP pages' SHIFT (OP2 + OCT+ / OCT-, firmware/src/ui_arp.c) is the toy's shift(pattern, steps, by): the
// factory patterns and drawn ones (notes up to the end, so the wrap splits and joins), each shifted along a chain of
// steps right and left (every output the next input, so split notes come back and join), and what shift gives, in its
// order. tests/arp_shape_test.c runs ash_shift on each and wants exactly that array.
// --zoom: the ARP pages' ZOOM (OP1 + OCT+ / OCT-, firmware/src/ui_arp.c) is the toy's subdivide(pattern, steps, rate,
// factor): patterns, STEPS and every RATE the firmware has, x2 and /2, and what subdivide gives, null included. Its
// refusals are named for the firmware's message (why, in subdivide's order of checks; this script throws where the
// names and subdivide's null disagree). tests/arp_shape_test.c runs ash_subdivide on each and wants the same pattern,
// STEPS and RATE, or the same refusal; where the toy goes on to 1/64 (RATE 1/32 x2) the firmware's RATE stops at 1/32
// and says so (AZ_FAST).
// --chords: the sequencer feeding the ARP (tests/arp_seq_test.c): chords on the sequencer's steps (each a pitch set, its
// first step, the step after its last, a velocity) go through the toy's offline form, shapeChords, on the arp's grid from
// step 0 (stepTicks 96 a step of the arp; a sequencer step is 96 x arpPerSeq), and its notes are written: tick, pitch,
// stop tick, velocity. The firmware plays the same chords from its steps and must give the same notes (their ends: as
// shapeChords, or the chord's own end where it comes first: the firmware's arp lets go with the chord).
// The path defaults to $WAVELOOP/web/src/arp/core/sequence.mjs (WAVELOOP: ~/repos/waveloop). A case is a pattern, its
// steps, a held chord (as played: unsorted, repeats), a root (or none), a direction, a boundary and a number of passes;
// it plays as the toy's hosts play it (main.js schedule): tick by tick, the offset worked out on a pass's first step
// (passOffset), the step's events (eventsAt) merged one pitch once at its longest length, in the order they come.
// RAND draws from xorshift32 (firmware/src/libc.c rng), seeded per case, as a fraction of 2^32.
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const args = process.argv.slice(2), CHORDS = args.includes('--chords'), ZOOM = args.includes('--zoom'), SHIFT = args.includes('--shift');
const path = args.find(a => !a.startsWith('--'));
const src = resolve(path ?? `${process.env.WAVELOOP ?? homedir() + '/repos/waveloop'}/web/src/arp/core/sequence.mjs`);
const { DEFAULT_PATTERN, passOffset, eventsAt, paintNote, rootVoicing, shapeChords, subdivide, shift, RATES } =
  await import(pathToFileURL(src).href);
const sha = createHash('sha256').update(readFileSync(src)).digest('hex');
const NMAX = 30;   // firmware/src/arp_shape.c ARP_NMAX (the Kit's)
const fits = (what, ...ps) => { for (const p of ps) if (p && p.length > NMAX) throw new Error(`${what}: a pattern past ARP_NMAX (${p.length} notes)`); };

const xorshift = seed => { let s = seed >>> 0 || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s; }; };
const DIRS = ['up', 'down', 'pendulum', 'random'], BOUNDS = ['wrap', 'octave'];

function play({ pattern, steps, chord, root, direction, boundary, passes, seed }) {
  const r = xorshift(seed), random = () => r() / 4294967296, out = [];
  const held = rootVoicing(chord, root);
  let offset = 0;
  for (let tick = 0; tick < steps * passes; tick++) {
    const s = tick % steps, pass = Math.floor(tick / steps);
    if (s === 0) offset = passOffset(pass, held.length, direction, random);
    const merged = new Map();
    for (const e of eventsAt(pattern, s, held, offset, boundary)) merged.set(e.note, Math.max(merged.get(e.note) || 0, e.length));
    for (const [pitch, length] of merged) out.push([tick, pitch, length]);
  }
  return out;
}

if (SHIFT) {
  const FACTORY = [[DEFAULT_PATTERN, 4], ...[
    [[[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1]], 4],
    [[[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 2, 1], [5, 1, 1]], 6],
    [[[0, 0, 1], [0, 1, 1], [0, 2, 1], [3, 0, 1], [3, 1, 1], [3, 2, 1], [6, 0, 2], [6, 1, 2], [6, 2, 2]], 8],
    [[[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 4, 1], [5, 5, 1], [6, 6, 1], [7, 7, 1]], 8],
    [[[0, 0, 1], [1, 2, 1], [2, 0, 1], [3, 3, 1], [4, 0, 1], [5, 2, 1], [6, 0, 1], [7, 1, 1]], 8],
    [[[0, 0, 4], [1, 2, 1], [3, 1, 1]], 4],
    // the toy's own test: a long note over the end, one ending on it
    [[[0, 0, 1], [2, 1, 1], [1, 2, 2], [3, 2, 1]], 4],
  ].map(([n, steps]) => [n.map(([step, row, length]) => ({ step, row, length })), steps])];
  const rnd = xorshift(0x5F1F7);
  const draw = steps => {
    let pattern = [];
    for (let i = 0, n = 1 + rnd() % 14; i < n; i++) {
      const step = rnd() % steps, length = 1 + rnd() % (steps - step);
      pattern = paintNote(pattern, rnd() % 8, step, rnd() % 3 ? length : steps - step);   // (a third reach the end)
    }
    return pattern;
  };
  const cases = [];
  const chain = (pattern, steps, moves) => {
    let p = pattern;
    for (const by of moves) {
      const got = shift(p, steps, by);
      cases.push({ pattern: p, steps, by, got });
      p = got;
    }
  };
  for (const [pattern, steps] of FACTORY) {
    chain(pattern, steps, Array(steps).fill(1));                            // right all the way round
    chain(pattern, steps, [...Array(steps).fill(-1), 1, 1, -1, -1]);        // left round, and back and forth
  }
  for (const steps of [1, 2, 3, 5, 8, 16, 31, 32, 64]) {
    const n = Math.min(steps, 6);
    chain(draw(steps), steps, [...Array(n).fill(1), ...Array(n).fill(-1)]);
    chain(draw(steps), steps, [...Array(n).fill(-1), ...Array(n).fill(1)]);
  }
  chain([], 16, [1, -1]);
  for (let k = 0; k < 60; k++) {
    const steps = 1 + rnd() % 64;
    chain(draw(steps), steps, Array.from({ length: 1 + rnd() % 8 }, () => rnd() % 2 ? 1 : -1));
  }
  for (const c of cases) fits('shift', c.pattern, c.got);
  const pk = p => `{${p.map(n => `AV_NOTE(${n.step}, ${n.row}, ${n.length})`).join(', ') || '0'}}`;
  const lines = [`/* generated by tools/arp_vectors.mjs --shift from sequence.mjs sha256 ${sha} - do not edit */`];
  lines.push('#define AV_NOTE(s, r, l) ARP_NOTE(s, r, l)');
  lines.push('/* a pattern, its STEPS, by (+1 right, -1 left); what shift gave, in its order */');
  lines.push('typedef struct { uint8_t steps; int8_t by; uint8_t npat, nout; uint16_t pat[ARP_NMAX], out[ARP_NMAX]; } asv_t;');
  lines.push('static const asv_t ASV[] = {');
  for (const c of cases)
    lines.push(`    {${c.steps}, ${c.by}, ${c.pattern.length}, ${c.got.length}, ${pk(c.pattern)}, ${pk(c.got)}},`);
  lines.push('};');
  const split = cases.filter(c => c.got.length > c.pattern.length).length, join = cases.filter(c => c.got.length < c.pattern.length).length;
  lines.push(`#define ASV_N ${cases.length}u   /* ${split} split a note at the end, ${join} joined one */`);
  console.log(lines.join('\n'));
  process.exit(0);
}

if (ZOOM) {
  // the firmware's RATE (P_ARATE 0..7: 1/2 .. 1/32) as the toy's steps per beat
  const FW = [0.5, 1, 1.5, 2, 3, 4, 6, 8];
  const AZ = ['AZ_OK', 'AZ_FAST', 'AZ_SLOW', 'AZ_NORATE', 'AZ_MAX', 'AZ_ODD', 'AZ_BETWEEN'];
  const values = RATES.map(r => r.value);
  // why subdivide says no, in its order of checks (the firmware's messages); checked against its null below
  const why = (pattern, steps, rate, factor) => {
    if (!values.includes(rate * factor))
      return factor === 2 ? (rate === Math.max(...values) ? 'AZ_FAST' : 'AZ_NORATE') : (rate === Math.min(...values) ? 'AZ_SLOW' : 'AZ_NORATE');
    if (factor === 2) return steps * 2 > 64 ? 'AZ_MAX' : 'AZ_OK';
    if (steps % 2) return 'AZ_ODD';
    return pattern.some(n => n.step % 2 || n.length % 2) ? 'AZ_BETWEEN' : 'AZ_OK';
  };
  const FACTORY = [[DEFAULT_PATTERN, 4], ...[
    [[[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1]], 4],
    [[[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 2, 1], [5, 1, 1]], 6],
    [[[0, 0, 1], [0, 1, 1], [0, 2, 1], [3, 0, 1], [3, 1, 1], [3, 2, 1], [6, 0, 2], [6, 1, 2], [6, 2, 2]], 8],
    [[[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 4, 1], [5, 5, 1], [6, 6, 1], [7, 7, 1]], 8],
    [[[0, 0, 1], [1, 2, 1], [2, 0, 1], [3, 3, 1], [4, 0, 1], [5, 2, 1], [6, 0, 1], [7, 1, 1]], 8],
    [[[0, 0, 4], [1, 2, 1], [3, 1, 1]], 4],
  ].map(([n, steps]) => [n.map(([step, row, length]) => ({ step, row, length })), steps])];
  const cases = [];
  const add = (pattern, steps, rate, factor) => {
    const got = subdivide(pattern, steps, FW[rate], factor), code = why(pattern, steps, FW[rate], factor);
    if ((got === null) !== (code !== 'AZ_OK')) throw new Error(`why() disagrees with subdivide: ${steps} ${FW[rate]} ${factor}`);
    cases.push({ pattern, steps, rate, factor, got, code });
  };
  const rnd = xorshift(0x200A);
  const draw = (steps, even) => {
    let pattern = [];
    for (let i = 0, n = 1 + rnd() % 14; i < n; i++) {
      let step = rnd() % steps, length = 1 + rnd() % (steps - step);
      if (even) { step &= ~1; length = Math.max(2, length & ~1); if (step + length > steps) length = steps - step; }
      pattern = paintNote(pattern, rnd() % 8, step, length);
    }
    return pattern;
  };
  for (const [pattern, steps] of FACTORY) for (let rate = 0; rate < FW.length; rate++) for (const factor of [2, 0.5]) {
    add(pattern, steps, rate, factor);                                     // (as loaded: a join mostly between steps)
    const z = subdivide(pattern, steps, 4, 2);                             // (zoomed in once: every join works)
    add(z.pattern, z.steps, rate, factor);
  }
  for (const steps of [1, 2, 3, 31, 32, 33, 34, 62, 63, 64]) for (const factor of [2, 0.5]) {
    add(draw(steps, false), steps, 5, factor);
    if (steps % 2 === 0) add(draw(steps, true), steps, 5, factor);
  }
  add([], 16, 3, 2); add([], 16, 3, 0.5); add([], 64, 3, 2); add([], 15, 3, 0.5);
  for (let k = 0; k < 120; k++) {
    const even = k % 2 === 1, steps = even ? 2 * (1 + rnd() % 32) : 1 + rnd() % 64;
    add(draw(steps, even), steps, rnd() % FW.length, rnd() % 2 ? 2 : 0.5);
  }
  for (const c of cases) fits('zoom', c.pattern, c.got && c.got.pattern);
  const pk = p => `{${p.map(n => `AV_NOTE(${n.step}, ${n.row}, ${n.length})`).join(', ') || '0'}}`;
  const lines = [`/* generated by tools/arp_vectors.mjs --zoom from sequence.mjs sha256 ${sha} - do not edit */`];
  lines.push('#define AV_NOTE(s, r, l) ARP_NOTE(s, r, l)');
  lines.push('/* a pattern, its STEPS, RATE (P_ARATE), x2 (in) or /2; what subdivide gave (its refusal named), the steps, the');
  lines.push(' * rate (steps per two beats: ARP_RATE2; 32 = the toy\'s 1/64) and the pattern */');
  lines.push('typedef struct { uint8_t steps, rate, in, code, nsteps, nrate2, npat; uint16_t pat[ARP_NMAX], out[ARP_NMAX]; } azv_t;');
  lines.push('static const azv_t AZV[] = {');
  for (const c of cases)
    lines.push(`    {${c.steps}, ${c.rate}, ${c.factor === 2 ? 1 : 0}, ${c.code}, ${c.got ? c.got.steps : 0}, ${c.got ? c.got.rate * 2 : 0}, ` +
      `${c.pattern.length}, ${pk(c.pattern)}, ${pk(c.got ? c.got.pattern : [])}},`);
  lines.push('};');
  const n = code => cases.filter(c => c.code === code).length;
  lines.push(`#define AZV_N ${cases.length}u   /* ${AZ.map(a => `${a.slice(3)} ${n(a)}`).join(', ')} */`);
  console.log(lines.join('\n'));
  process.exit(0);
}

if (CHORDS) {
  const P = Object.fromEntries([['SHAYPE', DEFAULT_PATTERN.map(n => [n.step, n.row, n.length])], ...Object.entries({
    'UP 4': [[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1]],
    STABS: [[0, 0, 1], [0, 1, 1], [0, 2, 1], [3, 0, 1], [3, 1, 1], [3, 2, 1], [6, 0, 2], [6, 1, 2], [6, 2, 2]],
    'OCT RUN': [[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 4, 1], [5, 5, 1], [6, 6, 1], [7, 7, 1]],
    PEDAL: [[0, 0, 1], [1, 2, 1], [2, 0, 1], [3, 3, 1], [4, 0, 1], [5, 2, 1], [6, 0, 1], [7, 1, 1]],
  })]);
  const STEPS = { SHAYPE: 4, 'UP 4': 4, STABS: 8, 'OCT RUN': 8, PEDAL: 8 };
  // a song of chords on the sequencer's steps (a step holds 4 notes at most): a pass's change mid-way, gaps, one note alone
  const SONG = [[[60, 64, 67, 71], 0, 8, 90], [[62, 65, 69], 10, 13, 70], [[64, 67, 71, 72], 13, 16, 100],
    [[60], 16, 17, 110], [[48, 55, 60, 64], 20, 30, 80]];
  const ODD = [[[60, 64, 67], 1, 4, 90], [[62, 65, 69], 4, 7, 100], [[57, 60, 64, 67], 9, 12, 70], [[65, 69, 72], 13, 16, 80]];
  const cases = [];
  const add = (pat, direction, boundary, arpPerSeq, chords, gate, len) =>
    cases.push({ pat, direction, boundary, arpPerSeq, chords, gate, len });
  add('SHAYPE', 'up', 'wrap', 1, SONG, 0.75, 32);
  add('UP 4', 'down', 'octave', 1, SONG, 0.75, 32);
  add('STABS', 'pendulum', 'wrap', 1, SONG, 0.5, 32);
  add('OCT RUN', 'up', 'octave', 1, SONG, 0.75, 32);
  add('PEDAL', 'up', 'wrap', 2, SONG, 0.75, 32);
  add('SHAYPE', 'pendulum', 'octave', 2, ODD, 0.75, 16);
  add('UP 4', 'up', 'wrap', 0.5, ODD, 0.75, 16);
  add('SHAYPE', 'down', 'wrap', 0.5, SONG, 0.75, 32);
  const RATE = { 0.5: 3, 1: 5, 2: 7 };               // the arp's RATE against the sequencer's 1/16: 1/8, 1/16, 1/32
  const lines = [`/* generated by tools/arp_vectors.mjs --chords from sequence.mjs sha256 ${sha} - do not edit */`];
  lines.push('typedef struct { uint8_t n, notes[4], start, stop, vel; } acv_chord_t;');
  lines.push('typedef struct { uint8_t pat, steps, dir, bound, rate, gate, len, nchord; acv_chord_t chord[5]; uint16_t nev; const uint16_t *ev; } acv_t;');
  const pats = Object.keys(P);
  cases.forEach((c, i) => {
    const seqTicks = 96 * c.arpPerSeq;
    const notes = c.chords.flatMap(([ps, a, b, v]) => ps.map(pitch => ({ pitch, start: a * seqTicks, stop: b * seqTicks, velocity: v })));
    const out = shapeChords(notes, { pattern: P[c.pat].map(([step, row, length]) => ({ step, row, length })), steps: STEPS[c.pat],
      stepTicks: 96, gate: c.gate, direction: c.direction, boundary: c.boundary, from: 0, to: c.len * seqTicks });
    c.ev = out.map(n => [n.start / 96, n.pitch, n.stop, n.velocity]);
    lines.push(`static const uint16_t ACV_EV${i}[] = {${c.ev.flat().join(', ') || '0'}};   /* tick, pitch, stop (1/96 tick), velocity */`);
  });
  lines.push('static const acv_t ACV[] = {');
  cases.forEach((c, i) => lines.push(`    {${pats.indexOf(c.pat)}, ${STEPS[c.pat]}, ${DIRS.indexOf(c.direction)}, ${BOUNDS.indexOf(c.boundary)}, ` +
    `${RATE[c.arpPerSeq]}, ${Math.round(c.gate * 100)}, ${c.len}, ${c.chords.length}, {${c.chords.map(([ps, a, b, v]) =>
      `{${ps.length}, {${ps.join(', ')}}, ${a}, ${b}, ${v}}`).join(', ')}}, ${c.ev.length}, ACV_EV${i}},   /* ${c.pat} ${c.direction} ${c.boundary} x${c.arpPerSeq} */`));
  lines.push('};');
  lines.push(`static const char *const ACV_PAT[] = {${pats.map(p => `"${p}"`).join(', ')}};`);
  lines.push(`#define ACV_N ${cases.length}u`);
  console.log(lines.join('\n'));
  process.exit(0);
}

// the cases: the toy's own example, the factory patterns, then random ones drawn by paintNote (as a user draws them)
const cases = [];
const add = c => cases.push({ root: null, passes: 6, seed: cases.length + 1, ...c });
const FACTORY = {
  'UP 4': [[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1]],
  UPDOWN: [[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 2, 1], [5, 1, 1]],
  STABS: [[0, 0, 1], [0, 1, 1], [0, 2, 1], [3, 0, 1], [3, 1, 1], [3, 2, 1], [6, 0, 2], [6, 1, 2], [6, 2, 2]],
  'OCT RUN': [[0, 0, 1], [1, 1, 1], [2, 2, 1], [3, 3, 1], [4, 4, 1], [5, 5, 1], [6, 6, 1], [7, 7, 1]],
  PEDAL: [[0, 0, 1], [1, 2, 1], [2, 0, 1], [3, 3, 1], [4, 0, 1], [5, 2, 1], [6, 0, 1], [7, 1, 1]],
  DRONE: [[0, 0, 4], [1, 2, 1], [3, 1, 1]],
};
const chords = [[60, 64, 67, 71, 74], [60], [64, 60], [67, 60, 64, 60], [48, 55, 60, 64, 67, 72, 76, 79], [120, 124, 127],
  [0, 3, 7], [62, 65, 69, 72], [60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75]];
for (const direction of DIRS) for (const boundary of BOUNDS) for (const chord of chords)
  add({ pattern: DEFAULT_PATTERN, steps: 4, chord, direction, boundary, passes: 9 });
for (const [, notes] of Object.entries(FACTORY)) for (const direction of DIRS) for (const boundary of BOUNDS)
  add({ pattern: notes.map(([step, row, length]) => ({ step, row, length })), steps: Math.max(...notes.map(n => n[0] + n[2])),
    chord: [60, 64, 67], direction, boundary });
for (const root of [60, 64, 67, 70, 72, 30, 127]) for (const boundary of BOUNDS)
  add({ pattern: DEFAULT_PATTERN, steps: 4, chord: [60, 64, 67, 71], root, direction: 'up', boundary });
const rnd = xorshift(0xA5A5);
for (let k = 0; k < 60; k++) {
  const steps = 1 + rnd() % (k < 40 ? 16 : 64);
  let pattern = [];
  for (let i = 0, n = 1 + rnd() % 14; i < n; i++) {
    const step = rnd() % steps;
    pattern = paintNote(pattern, rnd() % 8, step, 1 + rnd() % (steps - step));
  }
  const chord = Array.from({ length: 1 + rnd() % 7 }, () => 30 + rnd() % 70);
  add({ pattern, steps, chord, root: rnd() % 3 ? null : chord[rnd() % chord.length], direction: DIRS[rnd() % 4],
    boundary: BOUNDS[rnd() % 2], passes: 2 + rnd() % 8 });
}

for (const c of cases) fits('play', c.pattern);
const lines = [];
const cnote = n => `AV_NOTE(${n.step}, ${n.row}, ${n.length})`;
let events = 0;
lines.push(`/* generated by tools/arp_vectors.mjs from sequence.mjs sha256 ${sha} - do not edit */`);
lines.push('#define AV_NOTE(s, r, l) ARP_NOTE(s, r, l)');
lines.push('typedef struct { uint8_t steps, passes, dir, bound; int16_t root; uint32_t seed; uint8_t nchord, chord[16]; uint8_t npat; uint16_t pat[ARP_NMAX]; uint16_t nev; const uint16_t *ev; } arp_vec_t;');
cases.forEach((c, i) => {
  const ev = play(c);
  events += ev.length;
  lines.push(`static const uint16_t AV_EV${i}[] = {${ev.flat().join(', ') || '0'}};   /* tick, pitch, length */`);
});
lines.push('static const arp_vec_t ARP_VEC[] = {');
cases.forEach((c, i) => {
  const ev = play(c);
  lines.push(`    {${c.steps}, ${c.passes}, ${DIRS.indexOf(c.direction)}, ${BOUNDS.indexOf(c.boundary)}, ${c.root ?? -1}, ${c.seed}u, ${c.chord.length}, {${c.chord.join(', ')}}, ` +
    `${c.pattern.length}, {${c.pattern.map(cnote).join(', ')}}, ${ev.length}, AV_EV${i}},`);
});
lines.push('};');
lines.push(`#define ARP_NVEC ${cases.length}u   /* ${events} events */`);
console.log(lines.join('\n'));
