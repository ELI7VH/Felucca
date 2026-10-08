# Felucca Mod — MiniLab 3

An unofficial Felucca firmware mod for the **M-VAVE FM-1 + Arturia MiniLab 3**.
Direct DIN MIDI control; no computer needed while playing.

## My problems → solutions

| Problem | Solution |
| --- | --- |
| Needed a standalone setup | MiniLab plugs directly into FM-1 over DIN MIDI. |
| Wanted four independent track volumes | Faders 1–4 always mix tracks 1–4. |
| Faders felt too sensitive near the top | More travel for fine upper-level mixing; bottom reaches silence. |
| Wanted knobs to follow the MIDI channel | Channels 1–4 control the corresponding tracks. |
| Wanted the bottom row to match FM-1’s main knobs | Bottom knobs 5–7 follow HOME controls 1–3. |
| Needed quick vibrato-speed control | Bottom knob 8 always controls LFO speed. |
| Wanted mod-wheel pitch modulation | Mod strip adds vibrato; LFO controls its speed and shape. |
| Needed preset browsing without the editor | Turn the main encoder to browse the addressed track’s presets. |
| Wanted Shift to change sound engines | Shift + encoder browses engines and loads their first preset. |
| Wanted a DJ filter for the whole mix | Top knob 1: left low-pass, centre clean, right high-pass. |
| Wanted effects that only engage while held | A-bank pads engage on press and disengage on release. |
| Couldn’t see what a MIDI knob was changing | Brief popup shows track, parameter name and value. |
| Needed quick screen-track switching | Encoder click cycles the displayed track 1 → 2 → 3 → 4 → 1. |

## Eight effect pads

- **1–3:** repeat 1/8, 1/16, 1/32.
- **4:** reverse.
- **5:** tape stop.
- **6:** freeze.
- **7–8:** octave up/down.

Hold to engage; release to stop. Works even with FX latch enabled.

## Final knob layout

- **Top 1:** master DJ filter.
- **Top 2–4:** engine EDIT parameters 2–4.
- **Bottom 5–7:** engine HOME controls 1–3.
- **Bottom 8:** LFO/vibrato speed.

Encoder click changes the screen’s track; the keyboard MIDI channel stays independent.

## Get the mod

[Download firmware + controller preset](https://github.com/ELI7VH/Felucca/releases/latest).
Import the MiniLab preset, Store To an enabled User slot, and set FM-1 **ROUT = CH1-4**.
Use the correct DIN-to-TRS adapter. A computer is needed for initial setup.

[Full setup guide](controllers/MiniLab-3.md) · [Technical details and validation](docs/MIDI-IMPLEMENTATION.md)

**Status:** midi7 installed; host tests and live USB checks completed. The latest MiniLab preset still needs hardware store/readback; direct DIN/audio play and the hardware popup need hands-on verification.

Based on [hugelton/Felucca](https://github.com/hugelton/Felucca) 1.0.5.2, commit `7414269c4392cde8f4a4351c5f566314903b9116`. Original credits and GPL-3.0-only licensing retained.
