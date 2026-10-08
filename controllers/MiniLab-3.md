# MiniLab 3 — standalone Felucca MIDI

Use the custom `felucca-1.0.5.2-midi9.fwsc` firmware and `Felucca.minilab3` preset together.

| Control | Mapping |
| --- | --- |
| Main encoder click | Cycle the displayed track 1 → 2 → 3 → 4 → 1 (CC115 Gate) |
| Main encoder | Previous/next preset in current engine, on keyboard channel (CC114 relative 64) |
| Shift + main encoder | Previous/next sound engine, on keyboard channel (CC112 relative 64); loads first preset |
| Top knob 1 | CC19; master DJ filter: left low-pass, centre bypass, right high-pass |
| Top knobs 2–4 | CC21–23; engine EDIT parameters 2–4 on keyboard channel |
| Bottom knobs 5–7 | CC28–30; FM-1 HOME knobs 1–3 for the current engine, on keyboard channel |
| Bottom knob 8 | CC76; LFO speed on keyboard channel |
| Faders 1–4 | CC7; fixed MIDI channels 1–4; track volumes with finer upper-range travel |
| Mod strip | CC1 on keyboard channel; vibrato |
| Pitch strip | Pitch bend on keyboard channel |
| A-bank pads 1–8 | Hold for repeat 1/8, repeat 1/16, repeat 1/32, reverse, tape stop, freeze, octave up, octave down |
| B-bank pads 1–4 | Factory notes on keyboard channel |
| B-bank pad 5 / Stop | CC106 Gate; stop transport |
| B-bank pad 6 / Play | CC107 Gate; start transport |
| B-bank pad 7 / Record | CC108 Gate; toggle recording for the displayed FM-1 track; start if stopped |
| B-bank pad 8 / Tap | CC109 Gate; set internal tempo from three or more taps |

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

Fader travel uses a smooth concave taper: level = round(value × (381 − value) / 254). The slope falls from 1.5 near the bottom to 0.5 near the top, giving about three times as much travel per upper-level step. MIDI 0 is silence and 127 remains full level. Stored levels and the synth’s dB scale are unchanged; this spreads the existing level steps across physical travel.

Moving a parameter knob or fader shows a 1.2-second popup with its track, actual parameter name and formatted value. The master filter shows LPF/HPF amount or BYPASS. The popup temporarily pauses page drawing to prevent LCD flicker, then restores the current page; menus, naming and confirmation dialogs take priority. Encoder click changes the displayed track only, not the MiniLab keyboard MIDI channel. Turned knobs and played notes still follow ROUT and the keyboard channel.

## Automatic session resume

Project 4 is the autosave slot. After the sequencer stops, five seconds of rendered silence (including effect tails) and five seconds without session edits, the firmware saves automatically. Held notes and queued MIDI prevent saving. Unchanged sessions cause no flash writes. The screen briefly says AUTOSAVED on success or AUTOSAVE ERROR if writing fails. An ordinary project already occupying Project 4 is protected: AUTOSAVE SLOT4 USED means move that project before using automatic resume.

On startup it restores sounds, parameters, four track levels, recorded patterns, automation, tempo/global settings, song chain configuration, selected track, view and master FX macros. Playback starts stopped; held notes, pitch bend, mod-strip position and momentary pads do not resume. Existing samples, presets and settings keep their normal persistent storage. The physical master-volume knob still sets hardware output level.

Keep Project 4 for autosave. A/B flash copies retain the last valid session if power fails during writing. Power off before AUTOSAVED and changes since the previous save can be lost. Flash writes briefly block the processor, so the save gate waits for silence and a stopped sequencer.

USB archive backup/restore temporarily reserves the save buffer; autosave resumes after that transfer lease expires. This does not affect standalone DIN use.

## Transport and tap tempo

The updated User preset puts Stop, Play, Record and Tap on **B-bank pads 5–8**, matching their printed labels. Switch banks with Shift + Pad 2, then press these pads without Shift. A-bank effects remain unchanged; B-bank pads 1–4 still play notes. Import and Store To the updated preset once before using these controls.

Firmware also accepts Arturia's CC106–109 transport messages on any MIDI channel, independently of ROUT. If Shift + the labeled pads sends these through DIN on your MiniLab firmware, those shortcuts work too. That shortcut's direct-DIN output has not been verified; the B-bank mapping is the supported standalone path. Arturia documents a separate USB MCU/HUI port for DAW transport; changing FM-1 firmware cannot create DIN messages the controller does not send. See [Arturia's MIDI ports and CC chart](https://support.arturia.com/hc/en-us/articles/6189475866396-MiniLab-3-General-Questions).

Record toggles only the FM-1 track selected when the press arrives, regardless of the keyboard's MIDI channel. It starts playback when arming from stopped. Play starts playback without toggling it off; on the SONG page it starts the configured song chain. Stop stops playback and the chain. Record respects menu/dialog protections and refuses recording during a song chain.

Tap uses the last four press timestamps, starts applying tempo from the third tap, and resets after a gap longer than two seconds. It respects the existing BPM limits and leaves external USB/TRS clock tempo unchanged. Releases and pad pressure do not retrigger transport.
