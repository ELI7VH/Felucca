// ── YAMA-BRUH Drum AudioWorklet ───────────────────────────────────────
// FM-based percussion synthesis matching PSS-170/470 drum chip
// Supports multiple drum banks and pitch-variable sounds

// ── Drum Banks ────────────────────────────────────────────────────────
// Each bank defines parameter multipliers/overrides for each sound type
// Base params: { carrierFreq, modFreq, modIndex, pitchSweep, pitchDecay,
//                decay, noiseAmt, clickAmt }
const DRUM_BANKS = [
  { // 0: Standard — PSS-170 default kit
    name: 'Standard',
    mods: {},
  },
  { // 1: Electronic — 808 / TR-style
    name: 'Electronic',
    mods: {
      kick:  { decay: 0.4, pitchSweep: 220, modIndex: 1.5, clickAmt: 0.1 },
      snare: { noiseAmt: 0.75, decay: 0.22, modIndex: 1.5, clickAmt: 0.05 },
      hihat_c: { decay: 0.025, carrierFreq: 1200, modFreq: 7800, modIndex: 5.0 },
      hihat_o: { decay: 0.35, carrierFreq: 1200, modFreq: 7800, modIndex: 5.0 },
      tom:   { modIndex: 1.0, pitchSweep: 40, decay: 0.35, noiseAmt: 0 },
      clap:  { decay: 0.25, noiseAmt: 0.9 },
    },
  },
  { // 2: Power — big rock kit
    name: 'Power',
    mods: {
      kick:  { decay: 0.35, pitchSweep: 200, modIndex: 4.0, clickAmt: 0.5 },
      snare: { decay: 0.25, modIndex: 3.5, clickAmt: 0.3, noiseAmt: 0.5 },
      tom:   { decay: 0.3, modIndex: 3.0, pitchSweep: 100, clickAmt: 0.2 },
      cymbal: { decay: 1.2 },
    },
  },
  { // 3: Brush — jazz/light
    name: 'Brush',
    mods: {
      kick:  { decay: 0.15, pitchSweep: 80, modIndex: 1.5, clickAmt: 0.1 },
      snare: { noiseAmt: 0.85, decay: 0.3, modIndex: 0.8, clickAmt: 0.0 },
      hihat_c: { decay: 0.06, noiseAmt: 0.7, modIndex: 2.5 },
      hihat_o: { decay: 0.3, noiseAmt: 0.7, modIndex: 2.5 },
      tom:   { modIndex: 1.2, decay: 0.2, noiseAmt: 0.1 },
      rimshot: { noiseAmt: 0.4, clickAmt: 0.3 },
    },
  },
  { // 4: Orchestra — timpani, concert perc
    name: 'Orchestra',
    mods: {
      kick:  { carrierFreq: 50, decay: 0.5, pitchSweep: 30, modIndex: 1.0, clickAmt: 0.05 },
      snare: { carrierFreq: 280, decay: 0.15, modIndex: 1.8, noiseAmt: 0.3, clickAmt: 0.2 },
      tom:   { modIndex: 1.0, pitchSweep: 20, decay: 0.45, clickAmt: 0.05 },
      cymbal: { decay: 1.5, carrierFreq: 700, modIndex: 6.0 },
    },
  },
  { // 5: Synth — digital, punchy
    name: 'Synth',
    mods: {
      kick:  { carrierFreq: 55, decay: 0.18, pitchSweep: 300, pitchDecay: 0.008, modIndex: 5.0, clickAmt: 0.6 },
      snare: { carrierFreq: 250, modIndex: 4.0, noiseAmt: 0.4, decay: 0.12, clickAmt: 0.4 },
      hihat_c: { decay: 0.02, carrierFreq: 1500, modFreq: 9000, modIndex: 6.0 },
      hihat_o: { decay: 0.15, carrierFreq: 1500, modFreq: 9000, modIndex: 6.0 },
      tom:   { modIndex: 3.5, pitchSweep: 120, decay: 0.15, clickAmt: 0.3 },
      clap:  { decay: 0.12, noiseAmt: 0.95, clapGap: 0.008 },
      cowbell: { carrierFreq: 700, modFreq: 1000, modIndex: 2.5 },
    },
  },
  { // 6: Latin — conga, bongo, timbale
    name: 'Latin',
    mods: {
      kick:  { carrierFreq: 80, decay: 0.2, pitchSweep: 50, modIndex: 1.5, clickAmt: 0.15 },
      snare: { carrierFreq: 300, decay: 0.1, modIndex: 1.5, noiseAmt: 0.2, clickAmt: 0.35 },
      tom:   { modIndex: 1.5, pitchSweep: 30, pitchDecay: 0.01, decay: 0.15, clickAmt: 0.25 },
      rimshot: { decay: 0.04, clickAmt: 0.7, noiseAmt: 0.1 },
      cowbell: { decay: 0.08 },
    },
  },
  { // 7: Lo-Fi — gritty, bit-crushed feel
    name: 'Lo-Fi',
    mods: {
      kick:  { decay: 0.2, modIndex: 6.0, clickAmt: 0.15 },
      snare: { modIndex: 5.0, noiseAmt: 0.7, decay: 0.15 },
      hihat_c: { modIndex: 7.0, decay: 0.03 },
      hihat_o: { modIndex: 7.0, decay: 0.18 },
      tom:   { modIndex: 4.0, decay: 0.2 },
    },
  },
];

class YamaBruhDrumProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.hits = [];
    this.noiseSeed = 1;
    this.currentBank = 0;
    this.chokeMode = false;
    this.port.onmessage = (e) => this._onMessage(e.data);
  }

  _nextNoise() {
    let s = this.noiseSeed;
    s ^= s << 13;
    s ^= s >>> 17;
    s ^= s << 5;
    if (s === 0) s = 1;
    this.noiseSeed = s;
    return (s & 0x7fffffff) / 0x7fffffff * 2 - 1;
  }

  _onMessage(msg) {
    if (msg.type === 'drum') {
      if (this.chokeMode) {
        // Kill all active hits — mono drum channel
        for (const h of this.hits) h.done = true;
      }
      const vel = msg.velocity || 0.8;
      const note = msg.note || 0;
      const bank = msg.bank !== undefined ? msg.bank : this.currentBank;
      const sound = this._makeDrum(msg.sound, vel, note, bank, msg.overrides || null);
      if (sound) this.hits.push(sound);
    } else if (msg.type === 'setBank') {
      this.currentBank = Math.max(0, Math.min(DRUM_BANKS.length - 1, msg.bank || 0));
    } else if (msg.type === 'drumChoke') {
      this.chokeMode = !!msg.on;
    }
  }

  // Convert MIDI note to frequency for pitched drums
  _noteToFreq(note) {
    return 440 * Math.pow(2, (note - 69) / 12);
  }

  _makeDrum(name, vel, midiNote, bankIdx, overrides) {
    const base = {
      t: 0, vel, cp: 0, mp: 0, done: false,
      carrierFreq: 0, modFreq: 0, modIndex: 0,
      pitchSweep: 0, pitchDecay: 0.01,
      decay: 0.2, noiseAmt: 0, clickAmt: 0,
      clapMode: false, clapCount: 3, clapGap: 0.012,
    };

    let params;

    switch (name) {
      case 'kick':
        params = { ...base,
          carrierFreq: 60, modFreq: 90, modIndex: 3.0,
          pitchSweep: 160, pitchDecay: 0.015,
          decay: 0.25, clickAmt: 0.3,
        };
        break;
      case 'snare':
        params = { ...base,
          carrierFreq: 200, modFreq: 340, modIndex: 2.5,
          pitchSweep: 60, pitchDecay: 0.01,
          decay: 0.18, noiseAmt: 0.6, clickAmt: 0.15,
        };
        break;
      case 'hihat_c':
        params = { ...base,
          carrierFreq: 800, modFreq: 5600, modIndex: 4.0,
          decay: 0.04, noiseAmt: 0.5,
        };
        break;
      case 'hihat_o':
        params = { ...base,
          carrierFreq: 800, modFreq: 5600, modIndex: 4.0,
          decay: 0.22, noiseAmt: 0.5,
        };
        break;
      case 'clap':
        params = { ...base,
          carrierFreq: 1200, modFreq: 2400, modIndex: 1.5,
          decay: 0.2, noiseAmt: 0.85,
          clapMode: true,
        };
        break;
      case 'tom': {
        // Pitched tom — derive frequency from MIDI note
        // Default to mid-tom if no note given
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 165;
        params = { ...base,
          carrierFreq: freq,
          modFreq: freq * 1.5,
          modIndex: 2.0,
          pitchSweep: freq * 0.5,
          pitchDecay: 0.02,
          decay: 0.22,
          clickAmt: 0.1,
        };
        break;
      }
      case 'rimshot':
        params = { ...base,
          carrierFreq: 500, modFreq: 1600, modIndex: 2.0,
          pitchSweep: 200, pitchDecay: 0.005,
          decay: 0.06, noiseAmt: 0.2, clickAmt: 0.5,
        };
        break;
      case 'cowbell':
        params = { ...base,
          carrierFreq: 587, modFreq: 829, modIndex: 1.8,
          decay: 0.12, clickAmt: 0.1,
        };
        break;
      case 'cymbal':
        params = { ...base,
          carrierFreq: 940, modFreq: 6580, modIndex: 5.0,
          decay: 0.8, noiseAmt: 0.4,
        };
        break;

      // ── Wild SFX / Extended Percussion ──────────────────────────────
      case 'zap': {
        // Laser zap — fast pitch sweep from high to low
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) * 2 : 2000;
        params = { ...base,
          carrierFreq: freq * 0.1, modFreq: freq * 0.3, modIndex: 8.0,
          pitchSweep: freq, pitchDecay: 0.008,
          decay: 0.15, clickAmt: 0.6,
        };
        break;
      }
      case 'riser': {
        // Rising sweep — reverse pitch envelope
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 400;
        params = { ...base,
          carrierFreq: freq * 0.25, modFreq: freq * 0.5, modIndex: 4.0,
          pitchSweep: -freq, pitchDecay: 0.3,  // negative = rises
          decay: 0.6, noiseAmt: 0.15,
        };
        break;
      }
      case 'glitch': {
        // Digital glitch — ultra-short FM burst
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 800;
        params = { ...base,
          carrierFreq: freq, modFreq: freq * 7.13, modIndex: 12.0,
          pitchSweep: freq * 2, pitchDecay: 0.003,
          decay: 0.04, clickAmt: 0.8,
        };
        break;
      }
      case 'bomb': {
        // Explosion — low rumble with noise
        params = { ...base,
          carrierFreq: 30, modFreq: 45, modIndex: 6.0,
          pitchSweep: 200, pitchDecay: 0.05,
          decay: 0.8, noiseAmt: 0.5, clickAmt: 0.4,
        };
        break;
      }
      case 'scratch': {
        // Vinyl scratch — fast modulated noise burst
        params = { ...base,
          carrierFreq: 600, modFreq: 100, modIndex: 15.0,
          pitchSweep: 400, pitchDecay: 0.02,
          decay: 0.1, noiseAmt: 0.3, clickAmt: 0.5,
        };
        break;
      }
      case 'chirp': {
        // Bird/synth chirp — fast rising
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 1200;
        params = { ...base,
          carrierFreq: freq * 0.5, modFreq: freq, modIndex: 3.0,
          pitchSweep: -freq * 0.8, pitchDecay: 0.015,
          decay: 0.08, clickAmt: 0.3,
        };
        break;
      }
      case 'metallic': {
        // Metallic ring — inharmonic FM
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 500;
        params = { ...base,
          carrierFreq: freq, modFreq: freq * 1.41, modIndex: 5.0,
          decay: 0.5, clickAmt: 0.15,
        };
        break;
      }
      case 'noise_burst': {
        // Pure noise hit
        params = { ...base,
          carrierFreq: 1000, modFreq: 3000, modIndex: 2.0,
          decay: 0.12, noiseAmt: 0.95, clickAmt: 0.2,
        };
        break;
      }
      case 'blip': {
        // Retro blip — short sine burst
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 880;
        params = { ...base,
          carrierFreq: freq, modFreq: freq * 2, modIndex: 0.5,
          decay: 0.03, clickAmt: 0.4,
        };
        break;
      }
      case 'whoosh': {
        // Whoosh / sweep noise
        params = { ...base,
          carrierFreq: 200, modFreq: 1500, modIndex: 8.0,
          pitchSweep: -800, pitchDecay: 0.15,
          decay: 0.4, noiseAmt: 0.6,
        };
        break;
      }
      case 'thud': {
        const freq = midiNote > 0 ? Math.max(30, this._noteToFreq(midiNote) * 0.33) : 70;
        params = { ...base,
          carrierFreq: freq, modFreq: freq * 1.2, modIndex: 1.1,
          pitchSweep: freq * 0.8, pitchDecay: 0.03,
          decay: 0.28, clickAmt: 0.18,
        };
        break;
      }
      case 'shaker': {
        params = { ...base,
          carrierFreq: 1800, modFreq: 4200, modIndex: 2.4,
          decay: 0.16, noiseAmt: 0.92, clickAmt: 0.05,
        };
        break;
      }
      case 'fm_pop': {
        const freq = midiNote > 0 ? this._noteToFreq(midiNote) : 520;
        params = { ...base,
          carrierFreq: freq, modFreq: freq * 2.8, modIndex: 5.5,
          pitchSweep: freq * 1.1, pitchDecay: 0.012,
          decay: 0.09, clickAmt: 0.45,
        };
        break;
      }
      case 'gen_perc': {
        const seed = ((this.noiseSeed >>> 0) ^ ((midiNote || 0) * 1103515245)) >>> 0;
        const randA = ((seed & 0xffff) / 0xffff);
        const randB = (((seed >>> 8) & 0xffff) / 0xffff);
        const randC = (((seed >>> 4) & 0xffff) / 0xffff);
        const baseFreq = midiNote > 0 ? this._noteToFreq(midiNote) : 180;
        params = { ...base,
          carrierFreq: 55 + baseFreq * (0.25 + randA * 0.55),
          modFreq: 120 + baseFreq * (0.7 + randB * 2.7),
          modIndex: 0.8 + randC * 8.5,
          pitchSweep: (80 + randA * 520) * (randB > 0.38 ? 1 : -1),
          pitchDecay: 0.006 + randC * 0.05,
          decay: 0.06 + randB * 0.45,
          noiseAmt: 0.08 + randA * 0.72,
          clickAmt: 0.05 + randC * 0.55,
        };
        break;
      }
      default:
        return null;
    }

    // Apply drum bank overrides
    const bank = DRUM_BANKS[bankIdx] || DRUM_BANKS[0];
    // For toms, use 'tom' key in bank mods
    const modKey = name.startsWith('tom') ? 'tom' : name;
    const mods = bank.mods[modKey];
    if (mods) {
      for (const k in mods) {
        if (mods.hasOwnProperty(k)) params[k] = mods[k];
      }
      // For pitched toms with bank overrides, re-apply note-based freq if bank didn't set carrierFreq
      if (name === 'tom' && !mods.carrierFreq && midiNote > 0) {
        const freq = this._noteToFreq(midiNote);
        params.carrierFreq = freq;
        params.modFreq = freq * 1.5;
        params.pitchSweep = freq * 0.5;
      }
    }

    if (overrides && typeof overrides === 'object') {
      for (const key in overrides) {
        if (Object.prototype.hasOwnProperty.call(overrides, key) && overrides[key] !== undefined && overrides[key] !== null) {
          params[key] = overrides[key];
        }
      }
    }

    if (overrides && typeof overrides === 'object' && Number.isFinite(overrides.pitchSemis) && overrides.pitchSemis !== 0) {
      const pitchMul = Math.pow(2, overrides.pitchSemis / 12);
      params.carrierFreq *= pitchMul;
      params.modFreq *= pitchMul;
      params.pitchSweep *= pitchMul;
    }

    return params;
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    if (!out) return true;

    const sr = sampleRate;
    const TAU = 6.283185307179586;
    const dt = 1 / sr;

    for (let i = 0; i < out.length; i++) {
      let s = 0;

      for (let hi = this.hits.length - 1; hi >= 0; hi--) {
        const h = this.hits[hi];

        // Envelope — exponential decay
        const env = Math.exp(-h.t / (h.decay * 0.4)) * h.vel;
        if (env < 0.001) {
          this.hits.splice(hi, 1);
          continue;
        }

        // Pitch sweep (kick, snare, toms)
        const sweep = h.pitchSweep * Math.exp(-h.t / Math.max(h.pitchDecay, 0.001));
        const cFreq = h.carrierFreq + sweep;
        const mFreq = h.modFreq + sweep * 0.5;

        // FM synthesis
        const mod = Math.sin(h.mp) * h.modIndex;
        const carrier = Math.sin(h.cp + mod);

        // Noise component
        let noiseVal = 0;
        if (h.noiseAmt > 0) {
          noiseVal = this._nextNoise() * h.noiseAmt * env;
        }

        // Click transient (first ~2ms)
        let click = 0;
        if (h.clickAmt > 0 && h.t < 0.002) {
          click = (1 - h.t / 0.002) * h.clickAmt * h.vel;
        }

        // Clap mode: re-trigger envelope 3 times
        let clapEnv = 1;
        if (h.clapMode) {
          const gap = h.clapGap;
          if (h.t < gap * h.clapCount) {
            const clapIdx = Math.floor(h.t / gap);
            const clapT = h.t - clapIdx * gap;
            clapEnv = Math.exp(-clapT / 0.008);
          }
        }

        s += (carrier * env * (1 - h.noiseAmt) + noiseVal + click) * clapEnv * 0.5;

        // Advance phases
        h.cp += TAU * cFreq / sr;
        h.mp += TAU * mFreq / sr;
        if (h.cp > TAU) h.cp -= TAU;
        if (h.mp > TAU) h.mp -= TAU;
        h.t += dt;
      }

      // NaN guard
      if (s !== s) {
        s = 0;
        for (let hi = this.hits.length - 1; hi >= 0; hi--) {
          const h = this.hits[hi];
          if (!isFinite(h.cp) || !isFinite(h.mp)) {
            this.hits.splice(hi, 1);
          }
        }
      }

      // Soft clip
      if (s > 0.95) s = 0.95;
      else if (s < -0.95) s = -0.95;

      out[i] = s;
    }

    return true;
  }
}

registerProcessor('yambruh-drums', YamaBruhDrumProcessor);
