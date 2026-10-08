# MiniLab 3 — standalone Felucca MIDI

Use the custom `felucca-1.0.5.2-midi5.fwsc` firmware and `Felucca.minilab3` preset together.

| Control | Mapping |
| --- | --- |
| Main encoder | Previous/next preset in current engine, on keyboard channel (CC114 relative 64) |
| Shift + main encoder | Previous/next sound engine, on keyboard channel (CC112 relative 64); loads first preset |
| Top knob 1 | CC19; master DJ filter: left low-pass, centre bypass, right high-pass |
| Top knobs 2–4 | CC21–23; engine EDIT parameters 2–4 on keyboard channel |
| Bottom knobs 5–7 | CC28–30; FM-1 HOME knobs 1–3 for the current engine, on keyboard channel |
| Bottom knob 8 | CC76; LFO speed on keyboard channel |
| Faders 1–4 | CC7; fixed MIDI channels 1–4; track volumes |
| Mod strip | CC1 on keyboard channel; vibrato |
| Pitch strip | Pitch bend on keyboard channel |
| A-bank pads 1–8 | Hold for repeat 1/8, repeat 1/16, repeat 1/32, reverse, tape stop, freeze, octave up, octave down |
| B-bank pads | Factory notes on keyboard channel |

In Arturia MIDI Control Center, Import `Felucca.minilab3`, then Store To an unused User slot.
On MiniLab, hold Shift and tap Pad 3 (Prog) to select Felucca.
Hold Shift and press the keyboard key labelled MIDI CH 1, 2, 3 or 4 to choose the track.
On FM-1, set GLO > SYSTEM > ROUT to CH1-4. Connect MiniLab DIN MIDI OUT to FM-1 TRS MIDI IN with the correct adapter. The Mac is needed only for initial setup.

The bottom row's first three knobs follow the current engine's HOME knob assignments. Its fourth knob always controls LFO speed, including the mod-wheel vibrato speed. Top knob 1 controls the master filter; top knobs 2–4 control EDIT parameters 2–4. The master filter uses the existing FX macro smoothing and affects all four tracks. MIDI values 63 and 64 bypass it. It works without holding FX, and follows the existing performance-effect reset behavior when the FX layer closes or effects are cleared. Values span the full range, including signed/enum parameters. The main encoder wraps through the current engine's factory sounds followed by saved user sounds for that engine. Shift + turn wraps through the visible engines and loads their first factory sound. Both follow ROUT, preserve the mixer and sequencer, and replace unsaved sound edits. Controls are absolute; switching channels or sounds can cause a value jump when a knob moves.

CC1 adds up to ±0.5 semitone of vibrato using the track's LFO rate, waveform and fade. An explicit active MODW matrix assignment overrides this default. DRUM ignores vibrato. The wheel does not overwrite saved LFO pitch depth.

A-bank pads use Gate notes 36–43 on fixed MIDI channel 16. They affect the whole mix, bypass track routing, and always release on lift regardless of FX LATCH. Buffer effects share memory: the last held pad takes priority; releasing it returns to the previous held effect. Repeats/reverse start on the next 1/16 while running, immediately while stopped. Channel 16 CC120/121/123 clears held pads. Other notes on channel 16 remain subject to normal ROUT.

Additional direct CCs: pan 10; sustain level 70; release 72; attack 73; decay 75; LFO rate 76; distortion 90; reverb 91; chorus 93; delay 94. All follow ROUT, except CC7 on channels 1–4 always controls the corresponding track volume. CH1-4 ignores channels 5–16 except the dedicated channel 16 pad notes and pad panic/reset.

Save a sound/project normally to retain edits. MIDI edits participate in existing motion recording. Pitch bend, sustain and panic retain their existing behavior.

Source base: hugelton/Felucca commit 7414269c4392cde8f4a4351c5f566314903b9116 (1.0.5.2). This is a local custom build. Back up projects/presets before flashing. See ../BUILDING.md for building and installation.

Pad Gate behavior is documented in [Arturia’s MiniLab 3 MIDI Control Center manual](https://downloads.arturia.net/products/minilab-3/manual/minilab-3-mcc_Manual_1_14_1_EN.pdf).
