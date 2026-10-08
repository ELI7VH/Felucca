# Why this fork exists

Felucca turns the M-VAVE FM-1 into a four-track, multi-engine synthesizer. At the upstream
1.0.5.2 base used here, standard MIDI input supports notes, clock and performance controls,
but does not directly expose the engine's editable parameters or preset/engine browsing to
an ordinary controller. Full parameter editing is available through the computer-based
SysEx web editor. That leaves a practical gap for playing the FM-1 from a MiniLab 3 over
DIN MIDI, with no computer in the performance setup.

This fork fills that gap with a shared USB/TRS CC implementation and a MiniLab user preset.
It is an unofficial extension of [hugelton/Felucca](https://github.com/hugelton/Felucca),
based on commit `7414269c4392cde8f4a4351c5f566314903b9116` (1.0.5.2).

## MiniLab mapping

With FM-1 **ROUT = CH1-4**, keyboard MIDI channels 1–4 address tracks 1–4.

| MiniLab control | Behavior |
| --- | --- |
| Top knobs 1–4 | First four EDIT parameters of the addressed track's engine |
| Bottom knobs 5–7 | FM-1 HOME knobs 1–3, following that engine's actual assignments |
| Bottom knob 8 | LFO speed, including mod-wheel vibrato speed |
| Faders 1–4 | Fixed volumes for tracks 1–4, independent of keyboard channel |
| Main encoder | Previous/next factory or saved user preset within the current engine |
| Shift + main encoder | Previous/next visible sound engine; loads its first factory preset |
| Modulation strip | Vibrato depth on the keyboard channel |
| Pitch strip | Existing pitch bend on the keyboard channel |

See [the setup guide](controllers/MiniLab-3.md) and import
[`Felucca.minilab3`](controllers/Felucca.minilab3) in Arturia MIDI Control Center.
The preset can be stored in any enabled User slot; User 5 was used during development.
Use the correct DIN-to-TRS adapter for the FM-1 input and supply power to both devices.

## Firmware behavior

- CC20–27 expose all eight engine EDIT parameters, scaled to their own signed or enum ranges.
- CC28–31 follow the engine's four HOME knob assignments. The supplied MiniLab preset uses
  CC28–30 for its first three bottom knobs and CC76 for its fourth.
- CC7 on channels 1–4 always targets the corresponding track's volume, including ROUT SEL.
  Other parameter controls follow the existing ROUT setting. CH1-4 ignores channels 5–16.
- CC114 browses presets; CC112 browses engines. Both use MiniLab relative values centred
  at 64 (63 = previous, 65 = next). These are custom CCs, not MIDI Program Change or Bank Select.
- Preset loads are queued for the UI thread, outside the audio callback. They preserve track
  mixing and sequencer patterns, and replace unsaved sound edits. Rapid turns use a bounded queue.
- Parameter changes use the existing motion/automation capture path.
- CC1 adds up to approximately ±0.5 semitone of pitch modulation using the track's LFO
  rate, waveform and fade. An explicit active MODW matrix assignment overrides this fallback;
  DRUM ignores it. The wheel does not overwrite saved LFO pitch depth.
- Additional CCs cover pan, envelope, LFO rate and effect sends; the setup guide lists them.

This is a focused controller extension, not a claim that every synth parameter is exposed
through standard MIDI CC. Existing pitch bend, sustain and panic handling remain in place.

## Validation and limits

The full host suite passed during development, including 92 unchanged golden audio renders.
Additional integration tests cover all engines' parameter ranges, HOME knob assignments,
channel isolation, USB/TRS parsing, automation, mod-wheel behavior and preset/engine browsing.
The target build passes code and RAM budgets.

Custom firmware was installed on an FM-1 and its reported version verified. Live USB tests
verified independent track volumes, parameter controls, HOME controls and preset/engine
browsing. Earlier MiniLab mappings were stored and read back successfully. The final bottom-row
and LFO-speed preset is provided here; its hardware store/readback was blocked by editor UI
automation timeouts. Direct DIN/audio performance still needs hands-on verification.

## Build and install

Follow [BUILDING.md](BUILDING.md) for the JieLi toolchain and SDK. For this build:

```sh
./build.sh --release 1.0.5.2-midi4
python3 tools/fm1_install.py build/felucca-1.0.5.2-midi4.fwsc
```

Back up projects and presets before flashing. The custom release archive includes the
firmware, MiniLab template, setup guide and upstream license/attribution files. Upstream's
web installer installs its own firmware; it does not install this fork's changes.

Original copyrights, GPL-3.0-only licensing and third-party attribution are retained.
