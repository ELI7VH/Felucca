// SPDX-License-Identifier: GPL-3.0-only
// Independent audio identity reference: execute the pinned MIT Yama-bruh worklet.
// node tests/yama_reference.mjs OUTDIR [C_WAV_DIR]
// Input/output filenames: bank0-kick.wav, ... bank7-cymbal.wav (72 hits).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const sr = 44100, frames = sr * 5;
const sounds = ['kick', 'snare', 'clap', 'hihat_c', 'hihat_o', 'tom', 'rimshot', 'cowbell', 'cymbal'];
const banks = ['Standard', 'Electronic', 'Power', 'Brush', 'Orchestra', 'Synth', 'Latin', 'Lo-Fi'];
const fixture = fileURLToPath(new URL('./reference/yama-bruh/drum-worklet.js', import.meta.url));
const source = fs.readFileSync(fixture, 'utf8');
const sourceHash = createHash('sha256').update(source).digest('hex');
if (sourceHash !== 'fbd348754caf0682654b625069bd28706d81265fca825086192d41fefed0a752')
  throw new Error('Pinned worklet changed: review provenance and reference identity before accepting it.');
let Processor;
vm.runInNewContext(source, {
  AudioWorkletProcessor: class { constructor() { this.port = {}; } },
  sampleRate: sr,
  registerProcessor(name, value) {
    if (name !== 'yambruh-drums') throw new Error(`Unexpected processor ${name}`);
    Processor = value;
  },
});

function render(bank, sound, overrides) {
  const processor = new Processor();
  processor.port.onmessage({ data: { type: 'drum', bank, sound, velocity: 1, note: 0, overrides } });
  const y = new Float32Array(frames);
  for (let pos = 0; pos < frames; pos += 128) {
    const out = y.subarray(pos, Math.min(pos + 128, frames));
    processor.process([], [[out]]);
  }
  if (processor.hits.length) throw new Error(`${bank}/${sound}: reference did not finish`);
  return y;
}

function writeWav(filename, y) {
  const b = Buffer.alloc(44 + y.length * 2);
  b.write('RIFF', 0); b.writeUInt32LE(b.length - 8, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22);
  b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28);
  b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(y.length * 2, 40);
  for (let i = 0; i < y.length; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(y[i] * 32768))), 44 + i * 2);
  fs.writeFileSync(filename, b);
}

function readWav(filename) {
  const b = fs.readFileSync(filename);
  if (b.toString('ascii', 0, 4) !== 'RIFF' || b.toString('ascii', 8, 12) !== 'WAVE') throw new Error(`Not WAV: ${filename}`);
  let format, channels, bits, rate, start, size;
  for (let off = 12; off + 8 <= b.length;) {
    const id = b.toString('ascii', off, off + 4), n = b.readUInt32LE(off + 4);
    if (id === 'fmt ') {
      format = b.readUInt16LE(off + 8); channels = b.readUInt16LE(off + 10);
      rate = b.readUInt32LE(off + 12); bits = b.readUInt16LE(off + 22);
    }
    if (id === 'data') { start = off + 8; size = n; }
    off += 8 + n + (n & 1);
  }
  if (format !== 1 || bits !== 16 || rate !== sr || ![1, 2].includes(channels) || start === undefined || start + size > b.length)
    throw new Error(`Expected mono/stereo PCM16 at 44100 Hz: ${filename}`);
  const y = new Float32Array(size / (channels * 2));
  for (let i = 0; i < y.length; i++) {
    let sum = 0;
    for (let c = 0; c < channels; c++) sum += b.readInt16LE(start + (i * channels + c) * 2);
    y[i] = sum / (32768 * channels);
  }
  if (y.length < sr * 4) throw new Error(`Need at least four seconds including the decay tail: ${filename}`);
  return y;
}

function fftPower(y) {
  // 131072 samples (2.97 s) cover the audible part of even the Orchestra cymbal.
  const n = 131072, re = new Float64Array(n), im = new Float64Array(n);
  re.set(y.subarray(0, Math.min(y.length, n)));
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) [re[i], re[j]] = [re[j], re[i]];
  }
  for (let len = 2; len <= n; len *= 2) {
    const ar = Math.cos(-2 * Math.PI / len), ai = Math.sin(-2 * Math.PI / len);
    for (let i = 0; i < n; i += len) {
      let wr = 1, wi = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2, vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
        const next = wr * ar - wi * ai; wi = wr * ai + wi * ar; wr = next;
      }
    }
  }
  const edges = [0, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, sr / 2];
  const bands = new Array(edges.length - 1).fill(0);
  let power = 0, weighted = 0, dominant = 0, strongest = 0;
  for (let i = 1, band = 0; i < n / 2; i++) {
    const f = i * sr / n, p = re[i] * re[i] + im[i] * im[i];
    while (f >= edges[band + 1]) band++;
    bands[band] += p; power += p; weighted += f * p;
    if (p > strongest) { strongest = p; dominant = f; }
  }
  return { edgesHz: edges, fraction: bands.map(p => p / power), centroidHz: weighted / power, dominantHz: dominant };
}

function metrics(y) {
  let peak = 0, energy = 0, sum = 0, early = 0;
  const envelope = [];
  for (let i = 0; i < y.length; i++) {
    if (!Number.isFinite(y[i])) throw new Error('Non-finite audio');
    peak = Math.max(peak, Math.abs(y[i])); energy += y[i] ** 2; sum += y[i];
    if (i < sr / 5) early += y[i] ** 2;
  }
  for (let a = 0; a < y.length; a += 441) {
    let e = 0;
    for (let i = a; i < Math.min(a + 441, y.length); i++) e += y[i] ** 2;
    envelope.push(e / energy);
  }
  const strongest = envelope.indexOf(Math.max(...envelope));
  let end30 = strongest;
  while (end30 < envelope.length && envelope[end30] >= envelope[strongest] * 0.001) end30++;
  const quantiles = [0.1, 0.5, 0.9, 0.99];
  const energyTimes = [];
  for (let i = 0, total = 0, q = 0; i < y.length && q < quantiles.length; i++) {
    total += y[i] ** 2 / energy;
    while (q < quantiles.length && total >= quantiles[q]) { energyTimes.push(i / sr); q++; }
  }
  return { peak, rms200Db: 10 * Math.log10(early / (sr / 5)), dcShare: Math.abs(sum / y.length) / peak,
    t30: end30 / 100, energyTimes, envelope, spectrum: fftPower(y) };
}

function compare(reference, candidate, pitched = false) {
  // Gain is intentionally independent: Felucca balances dry lane levels separately.
  // These limits are declared before measuring the port. Temporal and octave-band
  // total variation compare where energy lives, not an implementation-derived hash.
  const a = reference, b = candidate;
  const temporalDistance = a.envelope.reduce((s, x, i) => s + Math.abs(x - (b.envelope[i] || 0)), 0) / 2;
  const spectralDistance = a.spectrum.fraction.reduce((s, x, i) => s + Math.abs(x - b.spectrum.fraction[i]), 0) / 2;
  const failures = [];
  if (b.peak < 0.01 || b.peak >= 0.999) failures.push('silent or clipping');
  if (b.dcShare > 0.01) failures.push('DC > 1% of peak');
  if (Math.abs(a.t30 - b.t30) > a.t30 * 0.15 + 0.01) failures.push('decay differs');
  if (temporalDistance > 0.10) failures.push('energy envelope differs');
  if (spectralDistance > 0.12) failures.push('spectral identity differs');
  // Compare the strongest actual FM partial, not an assumed carrier frequency.
  // A 2% + 2 Hz window tolerates quantized sweeps and neighboring FFT bins but
  // catches a musically significant tuning shift hidden within an octave band.
  if (pitched && Math.abs(a.spectrum.dominantHz - b.spectrum.dominantHz) > a.spectrum.dominantHz * 0.02 + 2)
    failures.push('dominant FM partial shifted');
  return { passed: failures.length === 0, failures, temporalDistance, spectralDistance,
    t30Reference: a.t30, t30Candidate: b.t30, gainDb: b.rms200Db - a.rms200Db,
    dominantReferenceHz: a.spectrum.dominantHz, dominantCandidateHz: b.spectrum.dominantHz };
}

const out = process.argv[2], candidateDir = process.argv[3];
if (!out) throw new Error('Usage: node tests/yama_reference.mjs OUTDIR [C_WAV_DIR]');
fs.mkdirSync(out, { recursive: true });
const result = { sourceHash, sampleRate: sr, seconds: frames / sr, velocity: 1, midiNote: 0, sounds: [] };
let failures = 0;
const identities = [];
for (let bank = 0; bank < banks.length; bank++) for (const sound of sounds) {
  const name = `bank${bank}-${sound}`, y = render(bank, sound), reference = metrics(y);
  writeWav(path.join(out, `${name}.wav`), y);
  identities.push(reference);
  const row = { name, bankName: banks[bank], reference };
  if (candidateDir) {
    const candidate = metrics(readWav(path.join(candidateDir, `${name}.wav`)));
    row.candidate = candidate; row.comparison = compare(reference, candidate, ['kick', 'tom', 'cowbell'].includes(sound));
    if (!row.comparison.passed) { failures++; console.error(`${name}: FAIL ${JSON.stringify(row.comparison)}`); }
  }
  result.sounds.push(row);
}
// Negative control: the comparison must reject a different sound and a changed decay.
if (compare(identities[0], identities[3]).passed) throw new Error('Comparator accepted kick as closed hat');
const wrongDecay = structuredClone(identities[0]); wrongDecay.t30 *= 2;
if (compare(identities[0], wrongDecay).passed) throw new Error('Comparator accepted doubled decay');
for (const pitchSemis of [-12, 12]) {
  const wrongPitch = metrics(render(4, 'kick', { pitchSemis }));
  const result = compare(identities[4 * sounds.length], wrongPitch, true);
  if (!result.failures.includes('dominant FM partial shifted')) throw new Error('Pitch guard accepted octave-shifted audio');
}
fs.writeFileSync(path.join(out, 'metrics.json'), JSON.stringify(result, null, 2) + '\n');
console.log(`yama_reference: actual pinned worklet, ${banks.length * sounds.length} hits at ${sr} Hz; ` +
  (candidateDir ? `${failures ? 'FAIL' : 'PASS'} (${failures} identity differences)` : 'reference WAVs and metrics written'));
if (failures) process.exitCode = 1;
