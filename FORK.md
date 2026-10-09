# WaveLoop Kit — MiniLab 3

An unofficial Felucca firmware mod for the **M-VAVE FM-1 + Arturia MiniLab 3**.
Direct DIN MIDI control; no computer needed while playing.

## My problems → solutions

| Problem | Solution |
| --- | --- |
| Needed more songs for a performance | 12 complete songs, in a reorderable setlist; two user sample slots remain. |
| Wanted songs to evolve without manual saves | Current song and setlist order autosave after five seconds of silence. |
| Needed a second controller layer | Hold A-bank pad 1 for Shift; shifted pad 8 taps tempo; shifted faders control track delay + reverb. |
| Wanted more sound controls | Hold A-bank pad 1; bottom knobs 5–8 shape synth ADSR or FM6 algorithm, feedback, ratio and envelope time. |
| Wanted a controller cheat sheet | Editor → MiniLab shows normal and shifted assignments. |
| Wanted developer readouts | Dev tab: CPU, RAM, flash, audio, MIDI/USB and autosave; export a snapshot. |
| Wanted a web view for this mod | Dedicated mod installer, setlist editor and full backups. |
| Wanted the mod clearly identified | WaveLoop branding in web Settings and the FM-1 menu/About page. |
| Wanted separate kick/snare edits and mixing | Each drum lane keeps its own sound, volume, pan and effect sends. |
| Wanted playing a drum to select its sequencer lane | Hit a drum, then edit that lane with the step buttons. |
| Needed patterns longer than 16 steps | Up to 64 steps: PATTERN → LEN, then page keys. |
| Wanted transport and tap without a computer | B-bank 1–2: previous/next song; 5–8: Stop, Play, selected-track Record, Tap. |
| Never wanted to press Save or rebuild a session | Autosave after five seconds of silence; restore at power-on. |
| Needed a standalone setup | MiniLab plugs directly into FM-1 over DIN MIDI. |
| Wanted four independent track volumes | Normal faders 1–4 mix tracks 1–4. |
| Faders felt too sensitive near the top | More travel for fine upper-level mixing; bottom reaches silence. |
| Wanted knobs to follow the MIDI channel | Channels 1–4 control the corresponding tracks. |
| Wanted the bottom row to match FM-1’s main knobs | Bottom knobs 5–7 follow HOME controls 1–3. |
| Needed quick vibrato-speed control | Bottom knob 8 controls LFO speed on synths; drum volume on DRUM. |
| Wanted mod-wheel pitch modulation | Mod strip adds vibrato; LFO controls its speed and shape. |
| Wanted fuller classic drum kits | Revoiced 808 with a deeper kick and fuller snare/clap; dry, woody CR-78-style percussion. |
| Wanted Yama-bruh drums here too | Eight Yama-bruh FM kits in the DRUM bank, with per-drum editing and mixing. |
| Thin sound banks | 32 mixed factory patches in each of ten synth engines; existing patch IDs retained. |
| Needed preset browsing without the editor | Turn the main encoder to browse the addressed track’s presets. |
| Wanted Shift to change sound engines | Shift + encoder browses engines and loads their first preset. |
| Wanted a DJ filter for the whole mix | Top knob 1: left low-pass, centre clean, right high-pass. |
| Wanted effects that only engage while held | A-bank pads engage on press and disengage on release. |
| Couldn’t see what a MIDI knob was changing | Brief popup shows track, parameter name and value. |
| Needed quick screen-track switching | Encoder click cycles the displayed track 1 → 2 → 3 → 4 → 1. |

## A-bank pads

- **1:** hold for Shift.
- **2–3:** repeat 1/16, 1/32.
- **4:** reverse.
- **5:** tape stop.
- **6:** freeze.
- **7–8:** octave up/down.

Hold effect pads to engage; release to stop. Works even with FX latch enabled.
**Shift + pad 8:** tap tempo. **Shift + faders:** track delay + reverb.
**Shift + bottom knobs 5–8:** synth ADSR or FM6 macros on the keyboard MIDI channel.
Top shifted knobs and shifted pads 2–7 remain unassigned.

## Drum kits

- **808:** longer deep kick, fuller snare and clap, smoother metallic hats.
- **CR78:** short, dry kick and snare; softer maracas, woody conga, short claves and a rounded bell.
- **Yama-bruh:** Standard, Electronic, Power, Brush, Orchestra, Synth, Latin and Lo-Fi.

These are synthesized eight-lane kits with separate edits and mixing for each drum.
Choose them in the **DRUM** preset bank. Yama-bruh kits use the **YB** prefix.

## Final knob layout

- **Top 1:** master DJ filter.
- **Top 2–4:** engine EDIT parameters 2–4.
- **Bottom 5–7:** engine HOME controls 1–3.
- **Bottom 8:** LFO/vibrato speed on synths.

Hold **A-bank pad 1** for the bottom row’s second layer:

| Knob | Synths | FM6 |
| --- | --- | --- |
| 5 | Attack | Algorithm |
| 6 | Decay | Feedback |
| 7 | Sustain | Modulator ratio |
| 8 | Release | Modulator envelope time |

Higher FM6 envelope time values mean slower envelopes. DRUM shifted knobs are unassigned.

On **DRUM**, play the drum first:

- **Top 2–4:** pan, reverb, delay.
- **Bottom 5–8:** pitch, tone, decay, volume.

Each of eight drum lanes remembers its edits. Faders still mix whole tracks.
Chorus, distortion, snap, accent and drive are available on FM-1 pages or additional CCs.
Set **SEQ → PATTERN → LEN** up to 64; page keys move between groups of 16.

Encoder click changes the screen’s track; the keyboard MIDI channel stays independent.

## Get the mod

[Web installer](https://eli7vh.github.io/Felucca/) · [Web editor](https://eli7vh.github.io/Felucca/webapp/editor/) · [Controls](https://eli7vh.github.io/Felucca/mod/).

[Download firmware + controller preset](https://github.com/ELI7VH/Felucca/releases/latest).
Import the MiniLab preset, Store To an enabled User slot, and set FM-1 **ROUT = CH1-4**.
Autosave updates the **current setlist song** and saves order. **Project 4** remains session recovery.
Use the correct DIN-to-TRS adapter. A computer is needed for initial setup.

[Full setup guide](controllers/MiniLab-3.md) · [Technical details and validation](docs/MIDI-IMPLEMENTATION.md)

**Setlist:** SEQ → SETLIST. Knob 1 selects; knob 2 moves; knobs 3/4 select Load/Save, then OCT+ confirms. EDIT renames. New songs need an initial Save into an empty entry; a recovery-only session upgrades into the first empty entry automatically. Load waits for stopped transport and five seconds of silence, saving the previous song first. Press Play after loading. There is no automatic song advance.

**Validation:** Host persistence tests cover evolving songs, saved order, restart identity, save failure/retry and saving before a song switch. Original DSP golden renders and direct-DIN/controller checks are recorded in the technical guide.

Based on [hugelton/Felucca](https://github.com/hugelton/Felucca) 1.0.5.2, commit `7414269c4392cde8f4a4351c5f566314903b9116`. Original credits and GPL-3.0-only licensing retained.
