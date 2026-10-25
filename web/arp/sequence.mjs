// Vendored unchanged from WaveLoop's Shape Arp toy (SHAYPE): waveloop web/src/arp/core/sequence.mjs at d7860921.
// sha256 of the source (every line after these three): 7339bd363afb61e46c34ac8414282df2860517d5e6dc410351500396974cfb9f
// editor.html carries it inline (ARP-CORE, exports dropped); node web/test_web.mjs checks both copies.
// Host-independent pattern interpretation. Positions are zero-based internally.
export const DEFAULT_PATTERN = [
  { step: 0, row: 0, length: 1 }, { step: 0, row: 2, length: 1 },
  { step: 1, row: 1, length: 1 }, { step: 1, row: 3, length: 1 },
  { step: 2, row: 2, length: 1 }, { step: 2, row: 3, length: 1 },
  { step: 3, row: 0, length: 1 }, { step: 3, row: 3, length: 1 },
];
export function passOffset(pass, size, direction = 'up', random = Math.random) {
  if (size < 2) return 0;
  if (direction === 'down') return -(pass % size);
  if (direction === 'random') return Math.floor(random() * size);
  if (direction === 'pendulum') { const p = pass % (2 * (size - 1)); return p < size ? p : 2 * (size - 1) - p; }
  return pass % size;
}
export function resolvePosition(notes, position, boundary = 'wrap') {
  const held = [...new Set(notes)].sort((a, b) => a - b);
  if (!held.length) return null;
  const index = ((position % held.length) + held.length) % held.length;
  const pitch = held[index] + (boundary === 'octave' ? Math.floor(position / held.length) * 12 : 0);
  return pitch >= 0 && pitch <= 127 ? pitch : null;
}
export function eventsAt(pattern, step, notes, offset, boundary) {
  return pattern.filter(n => n.step === step).map(n => ({
    note: resolvePosition(notes, n.row + offset, boundary), length: n.length,
  })).filter(n => n.note !== null);
}
export function paintNote(pattern, row, step, length) {
  const end = step + length;
  return [...pattern.filter(n => n.row !== row || n.step + n.length <= step || n.step >= end), { row, step, length }];
}

// Invert a held voicing so the chosen root is its lowest pitch.
export function rootVoicing(notes, root = null) {
  const pitches = notes.map(note => root !== null && note < root
    ? note + 12 * Math.ceil((root - note) / 12) : note);
  return [...new Set(pitches)].filter(note => note >= 0 && note <= 127).sort((a,b)=>a-b);
}

// Offline: recorded chords in, the arp's notes out, on a fixed step grid from `from`.
// A note holds a step when its start and stop, rounded to the nearest step, cover it, so a chord
// played a little late or released a little early still lands on the beat it meant.
// notes: [{ pitch, start, stop, velocity }] in ticks. Returns the same shape.
export function shapeChords(notes, { pattern, steps, stepTicks, gate = 0.75, direction = 'up', boundary = 'wrap', from, to, random = Math.random }) {
  if (!notes.length || !pattern.length || stepTicks <= 0) return [];
  const begin = from ?? Math.min(...notes.map(n => n.start));
  const end = to ?? Math.max(...notes.map(n => n.stop));
  const grid = t => Math.round((t - begin) / stepTicks);
  const held = notes.map(n => ({ ...n, a: grid(n.start), b: grid(n.stop) })).filter(n => n.b > n.a);
  const out = [];
  let offset = 0;
  for (let tick = 0; begin + tick * stepTicks < end; tick++) {
    const t = begin + tick * stepTicks, s = tick % steps, pass = Math.floor(tick / steps);
    const sounding = held.filter(n => n.a <= tick && tick < n.b);
    const chord = rootVoicing(sounding.map(n => n.pitch));
    if (s === 0) offset = passOffset(pass, chord.length, direction, random);
    if (!chord.length) continue;
    const velocity = Math.max(...sounding.map(n => n.velocity ?? 90));
    const merged = new Map();
    for (const e of eventsAt(pattern, s, chord, offset, boundary)) merged.set(e.note, Math.max(merged.get(e.note) || 0, e.length));
    for (const [pitch, length] of merged) {
      const stop = Math.min(end, t + Math.max(1, Math.round(stepTicks * length * gate)));
      if (stop > t) out.push({ pitch, start: t, stop, velocity });
    }
  }
  return out;
}

// Steps per beat, slow to fast: the rate knob, and what subdividing steps through.
export const RATES = [
  { value: 0.5, short: '1/2', label: 'Half' }, { value: 1, short: '1/4', label: 'Quarter' },
  { value: 1.5, short: '1/4T', label: 'Quarter triplet' }, { value: 2, short: '1/8', label: 'Eighth' },
  { value: 3, short: '1/8T', label: 'Eighth triplet' }, { value: 4, short: '1/16', label: 'Sixteenth' },
  { value: 6, short: '1/16T', label: 'Sixteenth triplet' }, { value: 8, short: '1/32', label: 'Thirty-second' }, { value: 16, short: '1/64', label: 'Sixty-fourth' },
];

// Split every step in two (factor 2) or join pairs (factor 0.5) at twice / half the rate, so every note
// stays exactly where it sounds. Null when it can't: no such rate, past 64 steps, or (joining) a note that
// starts or ends inside a pair.
export function subdivide(pattern, steps, rate, factor) {
  const next = rate * factor;
  if (!RATES.some(r => r.value === next)) return null;
  if (factor === 2) {
    if (steps * 2 > 64) return null;
    return { pattern: pattern.map(n => ({ ...n, step: n.step * 2, length: n.length * 2 })), steps: steps * 2, rate: next };
  }
  if (factor !== 0.5 || steps % 2 || pattern.some(n => n.step % 2 || n.length % 2)) return null;
  return { pattern: pattern.map(n => ({ ...n, step: n.step / 2, length: n.length / 2 })), steps: steps / 2, rate: next };
}
// Rotate the pattern one step left (-1) or right (+1); a note over the end carries on from step 0.
// The two halves of a note split at the end are joined first, so shifting back and forth loses nothing.
export function shift(pattern, steps, by) {
  const notes = pattern.map(n => ({ ...n })), heads = new Set(), out = [];
  for (const n of notes) {
    if (n.step + n.length !== steps) continue;
    const head = notes.find(m => m !== n && m.row === n.row && m.step === 0 && !heads.has(m));
    if (head && n.length + head.length <= steps) { heads.add(head); n.length += head.length; }
  }
  for (const n of notes) {
    if (heads.has(n)) continue;
    const step = ((n.step + by) % steps + steps) % steps;
    if (step + n.length <= steps) out.push({ ...n, step });
    else out.push({ ...n, step, length: steps - step }, { ...n, step: 0, length: step + n.length - steps });
  }
  return out;
}
