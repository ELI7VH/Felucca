# MiniLab 3 — standalone Felucca MIDI

Use the custom `felucca-1.0.5.2-midi.fwsc` firmware and `Felucca.minilab3` preset together.

| Control | Mapping |
| --- | --- |
| Knobs 1–8 | CC20–27; engine EDIT parameters 1–8 on the keyboard channel |
| Faders 1–4 | CC7; fixed MIDI channels 1–4; track volumes |
| Mod strip | CC1 on keyboard channel; vibrato |
| Pitch strip | Pitch bend on keyboard channel |
| Pads | Factory notes on keyboard channel |

In Arturia MIDI Control Center, Import `Felucca.minilab3`, then Store To an unused User slot.
On MiniLab, hold Shift and tap Pad 3 (Prog) to select Felucca.
Hold Shift and press the keyboard key labelled MIDI CH 1, 2, 3 or 4 to choose the track.
On FM-1, set GLO > SYSTEM > ROUT to CH1-4. Connect MiniLab DIN MIDI OUT to FM-1 TRS MIDI IN with the correct adapter. The Mac is needed only for initial setup.

The eight engine knobs use the engine's eight EDIT parameters, with its names and ranges. Values span the full range, including signed/enum parameters. Controls are absolute; switching channels or sounds can cause a value jump when a knob moves.

CC1 adds up to ±0.5 semitone of vibrato using the track's LFO rate, waveform and fade. An explicit active MODW matrix assignment overrides this default. DRUM ignores vibrato. The wheel does not overwrite saved LFO pitch depth.

Additional direct CCs: pan 10; sustain level 70; release 72; attack 73; decay 75; LFO rate 76; distortion 90; reverb 91; chorus 93; delay 94. All follow ROUT, except CC7 on channels 1–4 always controls the corresponding track volume. CH1-4 ignores channels 5–16.

Save a sound/project normally to retain edits. MIDI edits participate in existing motion recording. Pitch bend, sustain and panic retain their existing behavior.

Source base: hugelton/Felucca commit 7414269c4392cde8f4a4351c5f566314903b9116 (1.0.5.2). This is a local custom build. Back up projects/presets before flashing. See ../BUILDING.md for building and installation.
