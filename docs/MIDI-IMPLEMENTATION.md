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
| Top knob 1 | Master DJ filter: left low-pass, centre bypass, right high-pass |
| Top knobs 2–4 | EDIT parameters 2–4 of the addressed track's engine |
| Bottom knobs 5–7 | FM-1 HOME knobs 1–3, following that engine's actual assignments |
| Bottom knob 8 | LFO speed, including mod-wheel vibrato speed |
| Faders 1–4 | Fixed volumes for tracks 1–4, with finer upper-range travel |
| Main encoder click | Cycle the displayed track 1 → 2 → 3 → 4 → 1; keyboard channel stays unchanged |
| Main encoder | Previous/next factory or saved user preset within the current engine |
| Shift + main encoder | Previous/next visible sound engine; loads its first factory preset |
| Modulation strip | Vibrato depth on the keyboard channel |
| Pitch strip | Existing pitch bend on the keyboard channel |
| A-bank pads 1–8 | Momentary repeat 1/8, 1/16, 1/32, reverse, tape stop, freeze, octave up/down |

See [the setup guide](../controllers/MiniLab-3.md) and import
[`Felucca.minilab3`](../controllers/Felucca.minilab3) in Arturia MIDI Control Center.
The preset can be stored in any enabled User slot; User 5 was used during development.
Use the correct DIN-to-TRS adapter for the FM-1 input and supply power to both devices.

## Firmware behavior

- CC19 controls the existing smoothed master FILTER macro without holding FX. Values 0–62
  sweep low-pass, 63–64 bypass, and 65–127 sweep high-pass. This is a live performance
  control, not a saved patch parameter; normal FX-layer resets also reset it.
- CC20–27 expose all eight engine EDIT parameters, scaled to their own signed or enum ranges.
- CC28–31 follow the engine's four HOME knob assignments. The supplied MiniLab preset uses
  CC28–30 for its first three bottom knobs and CC76 for its fourth.
- CC7 on channels 1–4 always targets the corresponding track's volume, including ROUT SEL.
  Other parameter controls follow the existing ROUT setting. CH1-4 ignores channels 5–16,
  except the dedicated channel 16 master-effect pad notes and pad panic/reset.
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
The target build passes code and RAM budgets. The midi6 MIDI integration and performance DSP
tests passed, including all eight momentary pads reaching/releasing the real performance stage,
latch independence, overlapping holds, zero-velocity releases and channel 16 panic/reset.

Firmware midi6 was installed on an FM-1 and its reported version verified. Live USB filter
and pad messages left all four track parameter dumps unchanged; the filter was centred and
all pads released afterwards. Effect sound through the physical DIN setup is not yet verified. Live USB tests
verified independent track volumes, parameter controls, HOME controls and preset/engine
browsing. Earlier MiniLab mappings were stored and read back successfully. The final bottom-row,
LFO-speed, master filter and momentary-pad preset is provided here; its hardware store/readback was blocked by editor UI
automation timeouts. Direct DIN/audio performance still needs hands-on verification.

## Build and install

Follow [BUILDING.md](../BUILDING.md) for the JieLi toolchain and SDK. For this build:

```sh
./build.sh --release 1.0.5.2-midi10
python3 tools/fm1_install.py build/felucca-1.0.5.2-midi10.fwsc
```

Back up projects and presets before flashing. The custom release archive includes the
firmware, MiniLab template, setup guide and upstream license/attribution files. Upstream's
web installer installs its own firmware; it does not install this fork's changes.

Original copyrights, GPL-3.0-only licensing and third-party attribution are retained.

### Momentary A-bank effects

Pads 1–8 hold repeat 1/8, repeat 1/16, repeat 1/32, reverse, tape stop, freeze, octave up and octave down. They send Gate notes 36–43 on fixed channel 16, intercepted before track routing. They never sound notes and ignore FX LATCH. A separate MIDI hold mask preserves local FX keys when pads release. The last held buffer effect wins; channel 16 panic/reset clears MIDI holds. B-bank retains ordinary notes.

### Fader taper and controller feedback

CC7 uses a smooth concave taper with exact silence/full-level endpoints. Upper levels get more physical travel; stored parameter format and the existing dB scale stay unchanged. Parameter knobs/faders show the addressed track, engine-specific parameter name and formatted value in a 1.2-second popup. CC19 shows the master filter. Formatting/rendering run on the UI thread via a latest-value mailbox. Popup expiry invalidates the underlying cached screen.

CC115 rising edges queue displayed-track changes on the UI thread via the normal track-selection helper. Release/repeated held messages do not advance tracks. The supplied preset enables the main encoder’s click as Gate CC115; this requires importing/storing the updated MiniLab preset. Keyboard channel routing remains independent.

Live USB midi6 checks verified the nonlinear fader values at 0, 32, 64, 96, 112 and 127, and encoder-click track cycling/release across all four tracks. All track parameters and the original selection were restored afterwards. Host tests verify popup content, expiry and exact screen restoration; every formatted parameter value across all engines fits the popup. The new MiniLab encoder-click template still requires controller import/store; the MiniLab was not connected over USB during this update.

The midi6 host run passed audio regression (92 unchanged golden renders), MIDI integration, UI behavior/layout, persistence, DSP and target-cost checks. The only initial failure was the font-spacing reference under system Python without RAQM; rerunning that check with `.venv-build/bin/python` passed. Browser emulator and optional DaisySP reference checks remain unavailable.

### Popup LCD flicker fix (midi7)

The FM-1 LCD applies each region transfer immediately. Rendering the changing graph before repainting the popup exposed both images each frame, despite correct final draw order. While a popup is visible, only the header and changed popup card now draw; graph/layer/footer drawing pauses. Popup expiry restores the entire current page in the same frame. Transfer-level regression tests verify that live graph changes and forced redraws never write underneath the popup, unchanged popups do not redraw, changed values repaint only the card, and expiry restores the exact page. The controller preset is unchanged from midi6.

### Five-second silent autosave (midi9)

A detector observes the rendered master before DAC attenuation. Every stereo sample must remain within ±2 Q15 counts (about −84 dBFS) for five seconds; louder samples restart the interval. The UI-thread saver also requires a stopped transport/song chain, no held local/MIDI/gated voices or queued MIDI, and a project snapshot unchanged for five seconds. Continuous tweaking coalesces, unchanged packed snapshots skip flash, and errors retry after five seconds.

Project 4 uses its existing atomic A/B sector pair. An ordinary unmarked project in that slot is never overwritten. FUN8 reserved byte 65 marks resume snapshots; byte 67 retains the bipolar master filter. Eight unused bytes before the FM6 patch payload retain other master FX macros and UI view metadata, guarded by a compile-time overlap check. CRCs cover all metadata. No additional project-sized RAM is allocated.

Boot loads the newest valid flash copy after audio/MIDI initialization, restores the project and view, then leaves transport stopped and clears the silence counter. Momentary MIDI/local key ownership is not resumed. User samples and preferences retain their existing storage paths. A cut before the quiet interval/save completes retains the previous committed snapshot. Firmware updates retain Project 4, so a verified resume snapshot also survives reinstalling this custom firmware.

Persistence tests exercise silence timing/tails, edit debounce, unchanged-write suppression, held notes, running transport, PLAY races, interrupted program operations, boot restoration and protection of an ordinary Project 4.

The midi9 full host suite passed, including 92 unchanged golden audio renders. Additional persistence/backup tests passed after adding backup-staging protection and resume-metadata retention. Live USB verification restored every pre-update track parameter from a complete backup, observed Project 4 being created automatically with a valid CRC, and verified that sounds and the selected track stayed unchanged.

USB BACKUP_LIST/GET/PUT reserve shared staging for 15 seconds after the last request, preventing autosave from invalidating an active archive. Backup restore preserves the resume marker and extra metadata. Live verification checks the stored snapshot’s actual sound parameters, not only its marker/CRC; this allows the staging lease and stability window to finish before checking the save.

Live restart verification passed: after a firmware reinstall/restart, all four complete track dumps and the selected track matched the verified pre-update session exactly. The host cold-boot test also discards retained project RAM before restoring from flash. Physical power removal is not automated here; interrupted-write and lost-RAM behavior are covered by the persistence tests.

## midi9 transport and tap

CC106 Stop, CC107 Play, CC108 Record, CC109 Tap are consumed before track routing and modulation mapping on every channel. A bounded 16-slot queue defers work to the UI thread; rising edges suppress repeated on values and releases. Record snapshots `song.sel` at receipt. Tap snapshots `fm1_ms` and shares the local GLO tap algorithm. Queue saturation prioritizes Stop. CC120/121/123 clear transport button state for their channel.

The controller preset assigns B-bank pads 5–8 to Gate CC106–109 on channel 16. A-bank effects and B-bank pads 1–4 remain intact. Shift transport over DIN depends on MiniLab firmware and is not claimed as verified.

Integration tests cover deferred record, selected-track snapshot, press/release edges, start/stop ordering, non-toggling Play, tap timestamps, external clock protection, modal/chain recording guards and Stop at queue saturation.

Validation: midi9 builds within target flash/RAM/pool limits and is installed with INFO readback. All firmware tests and 92 unchanged golden renders passed. The full run had one web mock `PING keeps WATCH on` timing failure; rerunning `node web/test_web.mjs` passed. Live USB confirmed non-toggling Play, Stop, Record on the displayed track independent of channel, and three taps producing 119 BPM for a 120 BPM input. Original tempo/clock were restored after the check. Complete four-track parameter dumps and selected track matched after firmware restart; the pre-update runtime musical payload matched the saved resume project.

## midi10: per-drum controls

Live MIDI/local drum hits update a remembered lane per track and defer screen selection to the UI thread. Sequencer note events never steal editing focus. Eight GM-derived lanes share kit/model selection but have optional overrides for tune, tone, decay, snap, accent, kick variant, drive, volume, pan, distortion and three sends. Unedited controls inherit kit values; volume starts at unity (112 on the existing dB scale), pan at zero offset. Track level and pan remain final kit controls.

DRUM uses CC21/22/23 for lane pan/reverb/delay and CC76 for lane volume. CC28–30 remain tune/tone/decay. CC17 is an alternate drum-volume control. Non-drum mappings are unchanged. Eligible FM-1 page/editor edits also address the selected lane, except normal track LEVEL. A drum-only DRUM MIX page exposes lane LEVEL/PAN/CHOR/REV. Popups include the drum name. Per-drum edits skip the global motion recording path; independent lane automation is not added.

A track with custom mixing renders to eight small lane buses, applies lane distortion/volume/pan/sends, then the track fader. Chorus/delay/reverb remain shared returns. SLICER remains one kit insert; its dry mono correction uses track pan, with lane sends tapped before the insert. Synths and kits with no mix overrides retain the original mixing path.

Projects reuse the unused 128-byte FM6 payload of each DRUM track: `DRM1`, selected lane, eight 13-bit masks (two 7-bit bytes each), then eight × 13 values. Signed pan is biased by 64; bytes 125–127 are reserved. Older projects without the signature inherit kit settings. User presets reuse the tagged secondary patch object internally; the public FM6 editor protocol still rejects DRUM payloads. Legacy FM6 bank migration remains FM6-only. Project archives, autosave and sound undo/redo include lane overrides.

Existing sequencer storage already supports 64 steps. The change selects the lane from live hits; it does not extend pattern storage or reduce compatibility.

MiniLab User 5 was backed up, stored over the controller configuration SysEx interface and read back parameter-by-parameter (319 values). The command framing was checked against the original [MiniLab 3 Configurator implementation](https://minilab3.klangsoft.com/). Controller gestures still need hands-on testing over direct DIN.

Validation: final midi10 full host suite passed, including persistence, backup, editor, UI, USB/TRS input, modulation and target cost checks. All 92 golden renders stayed unchanged. Target image is 436068 bytes, static RAM 92576/98304 bytes, pool 330208/344064 bytes. Shared audio IRQ estimate is 38659 against the existing 37502 baseline (within its 10% tolerance); modulation budgets remain unchanged.

Installed INFO identity is FELUCCA v1.0.5.2-midi10. Live USB verified separate kick/snare tune, selected-drum pan/reverb/delay and drum-hit screen selection. A complete backup protected the session. New user edits arrived during verification, so cleanup removed only still-identical temporary test overrides, preserving every other current value. The latest musical payload and per-drum overrides matched the CRC-verified Project 4 autosave after silence. On firmware restart, four full track parameter dumps matched that save; subsequent live notes changed the screen selection, and later tweaks changed sounds. Physical DIN gestures, popup appearance and a quiet screen-selection check remain hands-on validation.


## midi11: evolving 12-song setlist and mod website

Twelve complete songs use projects A–C plus nine A/B pairs at 0xC8000–0xD9FFF. The order has its own A/B pair at 0xDA000/0xDB000. Project 4 remains recovery; sample slots 1/2 and existing settings/preset addresses stay unchanged. Legacy SLICE source 3 becomes a USR2 display alias; PIANO remains source 4.

One additional serialized project cache and nine name/used entries avoid a 13-project RAM allocation. Recovery metadata byte `PROJ_FM6_OFF-1` stores active project slot + 1 (zero means old/unbound). Recovery-only sessions bind to the first empty setlist entry on upgrade; existing named songs are preserved.

After stopped transport, five seconds of rendered silence and five settled seconds, main-loop autosave compares canonical current-song bytes, writes only changed music, saves dirty order and repacks the recovery journal. Queued loads save the previous song before restoring target tempo, tracks, patterns and automation. PLAY cancels a queued load; flash failure blocks it. No auto-advance or writes from the MIDI/audio ISR.

CC110/111 rising edges on channel 16 queue previous/next full song. MiniLab B-bank pads 1/2 send these; synth knobs, four faders, effects and transport mappings stay unchanged. Controller program selection still matters: activate the stored User preset.

Editor command 74: `[0]` read, `[1,position]` select, `[2,position]` move selected entry, `[3,position]` queue load, `[4,position]` save current song, `[5,position,ASCII name...]` rename (1–12 printable characters). Positions are 0–11. Reply: status, selected position, active song rank (127 unbound), pending-load slot+1, then 12 records: song rank, used, NUL-terminated name. INFO appends `4C 01 0C`. Status 1 invalid, 2 empty, 3 playing, 4 save failed. Deferred load reports pending, not completed.

Backup retains IDs 0–9 and 32–34; appends IDs 10–18 for new songs and 19 for order. Retired sample ID34 lists empty. Third-sample restores are refused before writes. Legacy 11/12/13-object archives still read; new archives have 23 objects. Live-runtime archives retain active-song identity. Private backups are never published.

The fork website packages the exact mod firmware with its SHA-256, controller preset, setlist editor and brief problem/solution guide. Operational persistence details and release metadata are in its support page. Upstream remains separately credited.

Validation: full host suite passed, including new setlist persistence and web protocol tests, UI layout/alignment and 92 unchanged golden renders. Image 439252 bytes; static RAM 94384/98304; pool 330208/344064. Audio IRQ cost 38727 against baseline 37502 remains inside the existing 10% tolerance. A complete pre-install backup matched the saved recovery musical payload. Live INFO confirms midi11; command 74 returns 12 unique entries. The first empty entry received the recovery-only session automatically. All musical bytes matched before/after installation and the actual saved Song 1. The CRC-verified new archive has 23 objects and retains active-song identity. MiniLab was not connected by USB during this installation; its new previous/next pads require importing the updated preset. Existing direct DIN mappings remain unchanged.
