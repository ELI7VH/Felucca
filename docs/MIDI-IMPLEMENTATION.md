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
| Arturia Shift + main encoder | Previous/next visible sound engine; loads its first factory preset |
| Modulation strip | Vibrato depth on the keyboard channel |
| Pitch strip | Existing pitch bend on the keyboard channel |
| A-bank pad 1 | Alt button (held) |
| A-bank pads 2–8 | Momentary repeat 1/16, 1/32, reverse, tape stop, freeze, octave up/down |
| Alt + bottom knobs 5–8 | Synth ADSR; FM6 algorithm / feedback / modulator ratio / modulator envelope time; DRUM unassigned |
| Alt + top knob 1 | Shared track LFO rate for cutoff, vibrato and other LFO destinations |
| Alt + top knob 2 | Cutoff LFO depth; FM6 modulator level/brightness; MIDI 64 = off |
| Alt + top knobs 3–4 / pads 2–7 | Reserved |
| Alt + pad 8 | Tap tempo |
| Alt + faders 1–4 | Combined track delay + reverb amount |

See [the setup guide](../controllers/MiniLab-3.md) and import
[`Felucca.minilab3`](../controllers/Felucca.minilab3) in Arturia MIDI Control Center.
The preset can be stored in any enabled User slot; User 5 was used during development.
Use the correct DIN-to-TRS adapter for the FM-1 input and supply power to both devices.

## Firmware behavior

- Normal CC19 controls the existing smoothed master FILTER macro without holding FX. Values 0–62
  sweep low-pass, 63–64 bypass, and 65–127 sweep high-pass. This is a live performance
  control, not a saved patch parameter; normal FX-layer resets also reset it.
- CC20–27 expose all eight engine EDIT parameters, scaled to their own signed or enum ranges.
- CC28–31 follow the engine's four HOME knob assignments. The supplied MiniLab preset uses
  CC28–30 for its first three bottom knobs and CC76 for its fourth.
- While Alt (A-bank pad 1) is held, CC19 sets P_LRATE and CC21 sets P_LD_FLT
  on the MIDI-addressed synth track. The rate is shared with vibrato and other LFO
  destinations. Depth spans -64..63 with MIDI 64 = 0; negative values invert the modulation.
  FM6 applies its FLT destination to modulator level/brightness. WHEEL ignores FLT depth.
  DRUM leaves both unassigned.
- While A-bank pad 1 is held, those CC28/29/30/76 messages edit attack/decay/sustain/release
  on the MIDI-addressed synth track. FM6 instead maps them to algorithm/feedback/modulator
  ratio/modulator envelope time; higher envelope-time values make envelopes longer.
  DRUM leaves knobs with Alt unassigned.
- Normal CC7 on channels 1–4 targets the corresponding track's volume, including ROUT SEL.
  While A-bank pad 1 is held, it writes that track’s delay and reverb amounts together.
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
./build.sh --release 1.0.5.2-midi19
python3 tools/fm1_install.py build/felucca-1.0.5.2-midi19.fwsc
```

Back up projects and presets before flashing. The custom release archive includes the
firmware, MiniLab template, setup guide and upstream license/attribution files. Upstream's
web installer installs its own firmware; it does not install this fork's changes.

Original copyrights, GPL-3.0-only licensing and third-party attribution are retained.

### Momentary A-bank effects

Pad 1 is the Alt button (hold for the second layer). Normal pads 2–8 hold repeat 1/16, repeat 1/32, reverse, tape stop, freeze, octave up and octave down. The eight pads send Gate notes 36–43 on fixed channel 16, intercepted before track routing. They never sound notes and ignore FX LATCH. A separate MIDI hold mask preserves local FX keys when pads release. The last held buffer effect wins; channel 16 panic/reset clears MIDI holds. B-bank pads 1–2 switch setlist songs, 3–4 retain ordinary notes and 5–8 provide transport/tap.

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

The controller preset assigns B-bank pads 5–8 to Gate CC106–109 on channel 16. A-bank effects and B-bank pads 1–4 remain intact. Arturia Shift transport over DIN depends on MiniLab firmware and is not claimed as verified.

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

## midi14 classic drum bank

DRUM factory presets append **808** (index 1, KIT 80) and **CR78** (index 2, new KIT value 9). Original preset 0 and stored KIT values 0–8 are unchanged. CR78 is an original synthesized approximation using the existing fixed-point voice renderers: kick, snare, maracas, closed hat, longer hat, conga, claves, cowbell. Maracas occupy the clap lane; conga the tom lane; claves the rim lane. Per-lane sound, volume, pan and sends continue to apply. No samples or additional sample slots are needed. This adapts the CR-78's eleven-instrument layout to eight lanes; it is not a sample-exact replica. Reference instrument list: https://support.roland.com/hc/en-us/articles/201934399-CR-78-Technical-Specifications .

Validation: all C host checks passed, including drum controls, clipping/DC/retrigger/choke, persistence, backup, editor, UI and target costs. Regression covers 353 golden renders: all 350 prior hashes retained and three new hashes appended. Web tests were updated for the expanded kit list. Target image 447256 bytes; static RAM 94384/98304 bytes and pool 330208/344064 bytes, unchanged from midi13.

### midi15 pad modifier

A-bank pad 1 is the Alt button: a held modifier on channel 16 note 36, colored cyan (MiniLab color 13).
Alt + pad 8 (note 43) queues the existing tap-tempo command. midi15 initially reserved Alt + pads 2–7 and all eight mapped knobs; midi17 assigns the bottom four knobs below; midi22 adds the first two top knobs.
Alt + fixed-channel CC7 faders set that track’s delay and reverb sends together, without changing its volume.
On DRUM these are track defaults; existing per-lane send overrides remain independent.
A track popup labels the combined control DELAY + REVERB. No new effect DSP, project format or sample storage is required.
Each pad remembers its press layer until release, including zero-velocity note-on. Panic/reset on channel 16 and MIDI input overflow release Alt and pad ownership.
The editor MiniLab tab documents the layout offline; it does not program the controller.

Validation: midi15 full host suite passed, including modifier ownership, reserved controls, combined sends, drum lane preservation, popup rendering, panic/overflow and web navigation. All 353 sound renders remained unchanged. Static RAM and audio pool allocations remain unchanged. MiniLab User 5 was read back across all 319 parameters; only A-bank pad 1 color changed to cyan.


### midi16 drum voicing

KIT 80 keeps its 49.5 Hz bass fundamental but extends the default kick t-30 from 0.30 to 0.51 seconds, with a fast pitch transient and mild upper harmonics. The snare has more shell body and a brief wire strike; the clap has three 8 ms-spaced bursts and a fuller tail. Hats retain the six-square metal source with reduced raw sizzle, and the tom gets a longer rounded body. Rim and cowbell are retained.

CR78 has a 60 Hz / 0.19 second kick, a short 235 Hz snare shell, broader maracas with a 2.5 ms rise, shorter noise hats, a bent 190 Hz conga, short wooden claves and a two-sine 750/1050 Hz bell. These are original synthesized voicings, not measured circuit replicas. Defaults are balanced to the existing lane loudness targets. Per-drum controls still shape the voices.

Preset IDs, KIT IDs, lane assignments, project formats, sample slots and the MiniLab preset are unchanged. Saved songs using KIT 80 or CR78 play the updated synthesis with their existing per-lane overrides.

Rim-drive and cowbell-strike envelope products now widen before shifting, preventing signed overflow at maximum SNAP/accent without altering default renders.

New independent sound checks require the 808 kick to be lower and longer than CR78, CR78 maracas to be darker than its closed hat, and the bell spectra to differ after gain normalization. Dry single-lane demos and complete beat renders are generated in `build/drum_demo/`. Only the two preset and two model-kit golden hashes change; all 349 other renders stay bit-identical.

Validation: full host suite passed, followed by final drum, 353-render regression and target-cost checks after the arithmetic fixes. The 808/CR78 signed-overflow sanitizer passes all tested control corners. Target drum loop cost is 83 against budget 86; image 447464 bytes, static RAM 94384/98304 and pool 330208/344064. Emscripten and DaisySP reference checks were unavailable. Separate legacy validation debt: the unchanged KIT66 snare can overflow its noise-gain product at extreme settings; the release-scoped sanitizer does not claim all other kits are overflow-free.

### midi17 Alt sound controls

With Alt (A-bank pad 1) held, the MiniLab bottom row uses the addressed track’s existing
parameters through the normal MIDI routing and parameter-edit path:

| Knob / CC | Standard synth | FM6 |
| --- | --- | --- |
| 5 / CC28 | P_ATK: attack | P_E0: algorithm |
| 6 / CC29 | P_DEC: decay | P_E1: feedback |
| 7 / CC30 | P_SUS: sustain | P_E3: modulator ratio |
| 8 / CC76 | P_REL: release | P_E4: modulator envelope time |

Higher FM6 envelope time values lengthen the modulator envelopes. These are existing
FM6 macros; the mapping adds no new DSP or envelopes. DRUM knobs with Alt are unassigned.
Releasing pad 1 restores HOME 1–3 and LFO speed (or selected-drum volume).
In midi17, Alt + top knobs 1–4 and pads 2–7 remained reserved; midi22 assigns top 1–2.
Alt + faders still set track delay +
reverb, and Alt + pad 8 still taps tempo.
The MiniLab preset file and its stored controller assignments are unchanged. The editor’s
MiniLab page separates normal knobs from the Alt layer.

Validation: midi17 full host suite passed. USB/DIN tests cover channel isolation, SEL routing, ADSR endpoints, FM6 signed/enum ranges and neutral values, exact popup targets, normal-layer restoration, DRUM no-ops, motion capture and project roundtrip. All 353 golden sound renders are unchanged. Target image 447596 bytes; main RAM 94384/98304 and pool 330208/344064 remain unchanged. No controller preset update is needed.

### midi18 settings branding

Web Settings now shows WaveLoop FM-1 with controls, mapping and support links, including while disconnected. Device settings retain their connection readiness gate. The device menu and About page show WaveLoop; the About QR opens https://eli7vh.github.io/Felucca/. Original Felucca and third-party credits remain available. USB/MIDI identity, mappings, DSP and storage formats are unchanged.

Validation: UI tests and 120 rendered screens across ten palettes plus style/large-text sweeps passed with zero layout/color findings and no alignment errors at one pixel or more. The About QR decoded correctly from eight preview variants. Web tests passed; offline/connected/disconnected Settings and Japanese labels were inspected. Target cost checks passed; image 447752 bytes, main RAM 94384/98304 and pool 330208/344064.


### midi19 Yama-bruh drum bank

The eight Yama-bruh banks append DRUM presets 3–10 and KIT values 10–17 in this order:
YB STANDARD, YB ELECTRO, YB POWER, YB BRUSH, YB ORCH, YB SYNTH, YB LATIN, YB LOFI.
Prior preset and KIT IDs are retained. The editor mock exposes the same IDs and labels.

`firmware/src/drum_yama.c` is a fixed-point adaptation of Lucian Labs’
[`www/drum-worklet.js`](https://github.com/lucian-labs/yama-bruh/blob/3eca861383e63e6c507c30402a204499faf79887/www/drum-worklet.js),
pinned to commit `3eca861383e63e6c507c30402a204499faf79887`. It uses the worklet’s core
drum defaults and eight bank overrides. The original MIT license is retained in
`LICENSES/MIT-YamaBruh.txt`; Lucian Labs is included in the device credits.

Nine core voices occupy eight existing lanes: kick, snare, clap, closed/open hats, tom,
rimshot, cowbell and cymbal. GM cymbal notes share the BELL lane with cowbell, including
its per-drum settings. TUNE moves both oscillators and the pitch sweep; TONE controls FM
index; DECY controls amplitude decay; SNAP controls pitch sweep, click and clap burst width.
Per-drum mixing, sends, saved overrides and the MiniLab mapping retain their existing paths.
The kits use synthesis and do not occupy user sample slots.

Validation:

- All 72 voices pass comparison against the pinned JavaScript worklet. Maximum
  normalized energy-distribution differences (total variation) are 0.000567 over
  time and 0.000065 across frequency; maximum gain difference is 0.00238 dB.
  These measure agreement with Yama-bruh, not Yamaha hardware.
- Signed-overflow/divide-by-zero UBSan checks pass the tested control corners.
  All 353 existing golden renders remain unchanged; 16 new renders bring the
  total to 369, with zero health, voice/routing, CPU-budget or crash failures.
- Available host-suite sections passed. Emscripten browser-emulator and DaisySP
  reference checks were skipped because their dependencies were unavailable;
  the optional official-V15 restore check also lacked its firmware fixture.
- The target scanner initially mistook a cold backward jump for a loop. It now
  checks control-flow reachability, including predicated branches/returns, while
  treating unresolved indirect branches conservatively. Existing budgets pass:
  `drum_render` costs 77 against 86; new `dv_yama_run` measures 194. These are
  weighted static instruction estimates, not cycle counts.
- Image: 450288/581564 bytes (131276 free). Main RAM: 94384/98304 (3920 free);
  pool: 330208/344064 (13856 free). Both RAM allocations are unchanged.
- Actual FM-1 USB MIDI readback confirms midi19 and all 11 DRUM presets. Each
  Yama kit passed 24 eight-note bursts at 125 ms intervals: maximum CPU 46%,
  boot render peak 1667 µs, at most eight voices and zero new audio overruns.
  Every sampled MIDI-overflow flag was zero; that flag is transient, so this
  does not establish a lifetime zero-drop count. Original KIT 0, all track
  engines/presets/parameters and displayed track were verified restored.
  Drum focus ends on BELL. No DIN or recorded-audio verification was performed.

Sequencer assessment: bounded generators compiled for the FM-1 could fill its
existing 64-step patterns and reuse playback and persistence. Yama-bruh's full
JavaScript sequencing and per-sample `noteAlgo` require a dedicated port. No new
algorithmic sequencer ships in midi19.


### midi22 Alt cutoff LFO controls

Hold Alt (A-bank pad 1) while turning the MiniLab top row:

| Knob / CC | Parameter | Range |
| --- | --- | --- |
| 1 / CC19 | P_LRATE: track LFO rate | 0..127, existing LFO rate scale |
| 2 / CC21 | P_LD_FLT: cutoff/brightness LFO depth | -64..63; MIDI 64 = 0 |

These controls follow the keyboard MIDI channel and existing ROUT rules. The existing
track LFO supplies its rate, waveform and fade; changing rate also changes vibrato and
other active LFO destinations. Positive and negative depth apply opposite modulation
polarity. FM6's FLT destination changes modulator level/brightness rather than a literal
filter cutoff. VOICE modulates vowel, PHASE modulates its depth, and PHYS modulates
brightness. WHEEL ignores the FLT destination, so its amount has no audible effect.
DRUM leaves all Alt knobs unassigned. Alt + top knobs 3–4 stay reserved.

Releasing Alt restores top knob 1 to the master DJ filter and top knob 2 to EDIT parameter 2
(or selected-drum pan). The parameter popup uses the existing RATE and FLT labels.
Edits use the existing motion capture and saved sound paths. No new LFO, DSP buffers,
controller preset assignments or storage formats are introduced.

Validation: firmware build and all available host tests passed, including Alt routing, signed depth endpoints and neutral, popups, normal-control restoration, motion recording and project restore. All 369 sound regression renders are unchanged. Image size is 450676 bytes; main RAM remains 94384 / 98304 bytes and the audio/display pool 330208 / 344064 bytes. Optional DaisySP reference and browser-emulator checks were unavailable. These are host/build results; physical-device checks are recorded separately.
