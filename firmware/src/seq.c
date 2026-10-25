/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport.
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by GLO > SYSTEM ROUT (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, channels 5..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track. Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to
 * the same track's modulation matrix (mod.c; the selected track: the one selected then).
 * A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). With the ARP on, the notes the arp
 * plays are recorded, not the keys held (what plays back is what was heard). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};

#define KB_SILENT 255u
static uint32_t kb_prev;
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t last_note = 60;
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */

/* WaveLoop Kit: the MIDI IN note-ons that played the selected track, for SEQ > STEP's step recording as the keys
 * (ui_input.c seq_entry). The audio ISR writes (midi_control.c midi_note_event), the main loop takes (mst_take) every
 * pass, so none waits for a step entry that comes later; full: dropped. In the pool: not loose ISR data */
#define MST_N 16u
static volatile uint8_t mst_q[MST_N] __attribute__((section(".pool")));
static volatile uint32_t mst_w __attribute__((section(".pool"))), mst_r __attribute__((section(".pool")));
static void mst_push(uint32_t note)              /* (the audio ISR) */
{
    uint32_t w = mst_w;
    if (w - mst_r >= MST_N)
        return;
    mst_q[w % MST_N] = (uint8_t)note;
    RING_PUBLISH();
    mst_w = w + 1u;
}
static uint32_t mst_take(uint8_t *out)           /* (the main loop) the notes since the last take, their count */
{
    uint32_t n = 0, r = mst_r;
    while (r != mst_w && n < MST_N) {
        RING_PUBLISH();
        out[n++] = mst_q[r % MST_N];
        r++;
    }
    RING_PUBLISH();
    mst_r = r;
    return n;
}

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i) { return i % NTRK; }   /* MIDI channel 0..15 of track i (keys -> MIDI out) */

static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10). The DRUM grid: white keys are steps, black keys 1..8 lanes, 9 ACC, 10 / 11 the page */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}
enum { GK_ACC = NLANE, GK_PGDN, GK_PGUP };       /* black keys 9..11 on the grid */

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

/* note n moved down onto the track's scale (ROOT, SCALE): QNT SNAP's keys and QNT SEQ's sequenced notes */
static int32_t scale_snap(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}

enum { QN_OFF, QN_SNAP, QN_WHITE, QN_SEQ };      /* P_QUANT */

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    int32_t n;
    if (e->keys && (n = e->keys(t, k)) >= 0)           /* the engine's own key map (DRUM's GM map, slices) */
        return (uint32_t)n;
    n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == QN_SNAP || t->p[P_QUANT] == QN_SEQ)   /* SNAP (and SEQ): every key, rounded down */
        return (uint32_t)clamp(scale_snap(t, n + 12 * song.octave + t->p[P_TRANS]), 0, 127);
    if (t->p[P_QUANT] == QN_WHITE) {                    /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], oct;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        oct = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            oct--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * song.octave + t->p[P_TRANS], 0, 127);
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
/* WaveLoop Kit: the ARP is SHAYPE, WaveLoop's Shape Arp (arp_shape.c, the core; ui_arp.c, its pages; project.c keeps
 * the pattern). The keys and MIDI notes held (held[], arp_add / arp_remove, HOLD latching as before) and the chord the
 * track's sequencer plays (seq_notes while seq_arp: seq_chord) are the chord; each step of the track's RATE (SWING,
 * the external clock as before) plays the pattern's notes on that step (arp_fire), every note for its length x the
 * step x GATE (arp_snd: up to ARP_SND at once). The chord gone (keys up, the step's notes over), the notes end.
 * Its steps: stopped, a chord from nothing starts the pattern at once from its first step. While the transport plays
 * (the internal clock or an external one), the arp steps on the song's grid from PLAY, chord or none (the toy's
 * shapeChords: recorded chords in, the notes out on a fixed grid); a chord coming in the first half of a step that
 * started with none plays that step late (shapeChords rounds a start to the nearest step), later it waits.
 * KEY ATTACH (ARP 3 ATTACH, arp_x; SHAYPE's, WaveLoop windows/shaype 4cbf5392): when a change of the chord reaches the
 * arp, the keys, MIDI notes and the sequencer's chords alike. NOW: at once; a change early in a step (its first half,
 * at most 120 ms) plays that step again with the new chord, what sounds cut first; the chord gone, the notes end. NOTE (the default):
 * at the next step; the notes sounding play to their end, the chord gone too. CYCLE: at the top of the pattern's next
 * pass; the pass playing keeps the chord it began with (arp_cyc), every key lifted too, then the last notes ring.
 * A chord from nothing starts the arp at once in every mode; STOP and ARP off end the notes */
#include "arp_shape.c"
static arp_pat_t arp_pat[NTRK] __attribute__((section(".pool")));   /* the tracks' patterns: the main loop writes them
                                                 * with the ISR off (ui_arp.c, project.c, editor.c), the audio ISR reads */
#define ARP_SND 8u
static struct { uint8_t note[ARP_SND]; uint32_t off[ARP_SND]; } arp_snd[NTRK] __attribute__((section(".pool")));
                                                 /* sounding, oldest first: the note, arp_clk at its end; arp_n of them */
#define ARP_CHORD (16u + 4u)                    /* the chord the arp plays: the held notes and the sequencer's (arp_list) */
#define ARP_LATE_MS 120u                        /* KEY ATTACH NOW: how late in a step a new chord still plays it again */
static struct { uint8_t note[ARP_CHORD], vel; } arp_cyc[NTRK] __attribute__((section(".pool")));
                                                 /* KEY ATTACH CYCLE: the chord (voiced) and velocity its pass began with;
                                                  * arp_cn of them */
#define ARP_KEYS 0x1FFFu                        /* the keys the ARP pages edit with (F3..F4: the white ones are rows
                                                 * 1..8, the black ones OP1..OP5; ui_arp.c, song.grid 4): silent there */
static uint32_t arp_steps(const track_t *t) { return (uint32_t)clamp(t->p[P_APROB], 1, ARP_STEPS); }
static uint32_t arp_att(const track_t *t) { return (uint32_t)clamp(arp_x[trk_index(t)][ARPX_ATT], 0, AT_N - 1); }
static void arp_defaults(uint32_t k)            /* a fresh track's: SHAYPE (the toy's DEFAULT_PATTERN), ROOT OFF, OCT 0,
                                                 * ATTACH NOTE */
{
    ash_load(&arp_pat[k % NTRK], 0);
    arp_x[k % NTRK][ARPX_PAT] = 1;
    arp_x[k % NTRK][ARPX_ROOT] = 0;
    arp_x[k % NTRK][ARPX_OCT] = 0;
    arp_x[k % NTRK][ARPX_ATT] = AT_NOTE;
}
static uint32_t arp_period(const track_t *t)    /* a step of RATE in samples (the toy's RATES) */
{
    return beat_samples() * 2u / ARP_RATE2[clamp(t->p[P_ARATE], 0, 7)];
}
static void arp_restart(track_t *t)              /* the pattern from its first step, on this block */
{
    t->arp_pos = 0xFFFFFFF;
    t->arp_idx = 0xFFFFFFFFu;
    t->arp_miss = 0;
}
/* the arp has a chord: keys or MIDI notes held, or the sequencer's chord (seq_chord) */
static int arp_fed(const track_t *t) { return t->nheld || (t->seq_arp && t->seq_n); }
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u) {
        t->held[t->nheld++] = (uint8_t)note;
        t->arp_chg = 1;
    }
    if (t->nheld == 1u && !song.playing && !(t->seq_arp && t->seq_n) && !t->arp_cn)
        arp_restart(t);                             /* stopped, from nothing: its first step now (CYCLE: not while
                                                     * a pass plays its chord out) */
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note)
            t->held[k++] = t->held[i];
    t->arp_chg |= (uint8_t)(k != t->nheld);
    t->nheld = (uint8_t)k;
}

static void rec_note(track_t *t, uint32_t note, uint32_t vel);
static void rec_release(track_t *t, uint32_t note);
static int rec_on(const track_t *t) { return ((song.rec >> trk_index(t)) & 1u) && song.playing && !chain.running; }

#include "motion.c"

/* sounding note i ends; the others keep their order. Not inlined: once a note, kept out of the audio ISR's loop over
 * the tracks (tests/target_budget.py), as everything of the arp's but its clock. (What the arp plays is not recorded:
 * the chords played are, input_on) */
static __attribute__((noinline)) void arp_end(track_t *t, uint32_t i)
{
    uint32_t k = trk_index(t), note = arp_snd[k].note[i];
    for (t->arp_n--; i < t->arp_n; i++) {
        arp_snd[k].note[i] = arp_snd[k].note[i + 1u];
        arp_snd[k].off[i] = arp_snd[k].off[i + 1u];
    }
    trk_note_off(t, note);
}
static __attribute__((noinline)) void arp_silence(track_t *t)   /* the sounding arp notes end */
{
    while (t->arp_n)
        arp_end(t, t->arp_n - 1u);
}
static void arp_off(track_t *t) { arp_silence(t); }
static int arp_sounds(const track_t *t, uint32_t note)   /* the arp sounds this pitch now */
{
    uint32_t i, k = trk_index(t);
    for (i = 0; i < t->arp_n && i < ARP_SND; i++)
        if (arp_snd[k].note[i] == note)
            return 1;
    return 0;
}
static __attribute__((noinline)) void arp_gates(track_t *t)     /* the notes whose gate is over end */
{
    uint32_t i = 0, k = trk_index(t);
    while (i < t->arp_n)
        if ((int32_t)(t->arp_clk - arp_snd[k].off[i]) >= 0)
            arp_end(t, i);
        else
            i++;
}
/* a note starts for dur samples at vel: the same pitch still sounding from an earlier step ends first (a retrigger),
 * the oldest when ARP_SND sound */
static void arp_play(track_t *t, uint32_t note, uint32_t dur, uint32_t vel)
{
    uint32_t i, k = trk_index(t);
    for (i = 0; i < t->arp_n; i++)
        if (arp_snd[k].note[i] == note) {
            arp_end(t, i);
            break;
        }
    if (t->arp_n >= ARP_SND)
        arp_end(t, 0);
    arp_snd[k].note[t->arp_n] = (uint8_t)note;
    arp_snd[k].off[t->arp_n++] = t->arp_clk + (dur ? dur : 1u);
    trk_note_on(t, note, vel);
}

static volatile uint32_t beat_pos, beat_n;      /* samples into the beat, the beat of the bar (0..3) */

/* the chord the arp plays (raw, its count; 0: nothing to play): the held notes, each once, and the sequencer's chord
 * (seq_chord: as the step plays them, QNT SEQ done). ash_voicing orders them. raw: ARP_CHORD */
static uint32_t arp_list(const track_t *t, uint8_t *raw)
{
    uint32_t i, j, n = 0, x;
    for (i = 0; i < t->nheld && i < 16u; i++) {
        x = t->held[i];
        for (j = 0; j < n && raw[j] != x; j++)
            ;
        if (j == n)
            raw[n++] = (uint8_t)x;
    }
    for (i = 0; t->seq_arp && i < t->seq_n && i < 4u; i++) {
        x = t->seq_notes[i];
        for (j = 0; j < n && raw[j] != x; j++)
            ;
        if (j == n)
            raw[n++] = (uint8_t)x;
    }
    return n;
}

/* an arp step starts (period: its length): the next step of the pattern over the chord (the toy's schedule: arp_shape.c
 * ash_tick; adv 0: the step that started a moment ago with no chord, late: arp_block). ROOT (arp_x, a note name): the
 * lowest held note of that name is the root (the toy's latched root key); OCT (arp_x) moves every note, one past 0..127
 * not played. The velocity: the sequencer's step's when its chord plays (shapeChords: the loudest sounding), 100 for the
 * keys alone. Not inlined: once a step, kept out of the audio ISR's loop over the tracks (tests/target_budget.py) */
static __attribute__((noinline)) void arp_fire(track_t *t, uint32_t period, int adv)
{
    uint8_t raw[ARP_CHORD], live[ARP_CHORD], pitch[ARP_ROWS], len[ARP_ROWS];
    const uint8_t *held = live;
    uint32_t n = arp_list(t, raw), i, j, k = trk_index(t), gate = (uint32_t)clamp(t->p[P_AGATE], 10, 100), vel = 100u;
    uint32_t att = arp_att(t);
    int32_t ofs = t->arp_ofs, root = -1, rk = arp_x[k][ARPX_ROOT];
    if (t->seq_arp && t->seq_n)
        vel = t->nheld && t->seq_avel < 100u ? 100u : t->seq_avel;
    if (rk > 0)
        for (i = 0; i < n; i++)
            if (raw[i] % 12u == (uint32_t)rk - 1u && (root < 0 || raw[i] < root))
                root = raw[i];
    n = ash_voicing(raw, n, root, live);
    if (adv && ++t->arp_idx == 0u && t->arp_n && att == AT_NOW)
        arp_silence(t);                              /* a fresh chord (arp_restart): NOW, the notes of the one before end */
    if (att == AT_CYCLE) {                           /* a pass begins (or the arp starts): the chord as it is now */
        if (!t->arp_cn || t->arp_idx % arp_steps(t) == 0u) {
            memcpy(arp_cyc[k].note, live, n);
            arp_cyc[k].vel = (uint8_t)vel;
            t->arp_cn = (uint8_t)n;
            if (!n)
                return;                              /* (every key lifted: done; the last notes ring to their end) */
        }
        held = arp_cyc[k].note;
        n = t->arp_cn;
        vel = arp_cyc[k].vel;
    }
    j = ash_tick(&arp_pat[k], arp_steps(t), t->arp_idx, held, n, (uint32_t)clamp(t->p[P_AOCT], 0, AD_N - 1),
                 (uint32_t)clamp(t->p[P_AORDER], 0, 1), rng, &ofs, pitch, len);
    t->arp_ofs = (int8_t)ofs;
    ofs = 12 * clamp(arp_x[k][ARPX_OCT], -3, 3);
    for (i = 0; i < j; i++)
        if ((uint32_t)(pitch[i] + ofs) <= 127u)
            arp_play(t, (uint32_t)(pitch[i] + ofs), period * len[i] / 100u * gate, vel);
}

/* the arp's block (arp_step: only while it sounds, is on or the transport plays). The chord gone, its notes end (NOW,
 * or the ARP off; NOTE and CYCLE let them ring to their end); with no chord and the transport stopped nothing moves.
 * Else the steps of RATE (SWING) go by, the remainder kept so the grid stays the transport's; a step with a chord plays
 * (arp_fire), one without is passed (its pass's offset 0, as shapeChords works it out over no chord) and marked
 * (arp_miss) for a chord coming in its first half. CYCLE: the pass keeps its chord (arp_cn), the keys lifted too.
 * NOW: the chord changed in a step's first half (at most ARP_LATE_MS), that step again with it */
static __attribute__((noinline)) void arp_block(track_t *t, uint32_t n)
{
    uint32_t period, len, att = arp_att(t), chg = t->arp_chg;
    int32_t sw;
    int live = t->p[P_AMODE] && arp_fed(t), fed;
    t->arp_chg = 0;
    if (!t->p[P_AMODE] || att != AT_CYCLE)
        t->arp_cn = 0;
    fed = live || t->arp_cn;
    if (t->arp_n)
        arp_gates(t);
    if (!fed && t->arp_n && (att == AT_NOW || !t->p[P_AMODE]))
        arp_silence(t);
    if (!fed && !song.playing) {
        t->arp_miss = 0;
        return;
    }
    period = arp_period(t);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    len = period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw);
    t->arp_pos += n;
    if (t->arp_pos < len && t->arp_pos != 0xFFFFFFF + n) {
        if (t->arp_miss && live) {                   /* a chord into a step that started with none */
            t->arp_miss = 0;
            if (t->arp_pos < len / 2u)
                arp_fire(t, period, 0);              /* (in its first half: that step, late) */
        } else if (chg && att == AT_NOW && live && !t->arp_miss && t->arp_pos < len / 2u &&
                   t->arp_pos < ARP_LATE_MS * (FS / 1000u)) {
            arp_silence(t);                          /* NOW: the chord moved early in the step (its first half, at most
                                                      * ARP_LATE_MS: shaype_engine.c LATE_S): it again, with it */
            arp_fire(t, period, 0);
        }
        return;
    }
    t->arp_pos = t->arp_pos >= 0xFFFFFFF ? 0u : t->arp_pos - len;   /* (the remainder: on the grid) */
    if (t->arp_pos >= period)
        t->arp_pos = 0;                              /* (a tempo jump: no burst of steps) */
    t->arp_miss = (uint8_t)!fed;
    if (fed) {
        arp_fire(t, period, 1);
        if (att == AT_CYCLE && !t->arp_cn)
            t->arp_miss = 1;                         /* (CYCLE, a pass's top with no keys: it ended) */
    } else if (++t->arp_idx % arp_steps(t) == 0u)
        t->arp_ofs = 0;
}

/* n: samples the arp advances by (the external clock's while it runs, 0 stopped); gate_n: samples its gate counts
 * down by (real time when the external transport stops: a tapped or latched note still ends). Inline once a block
 * per track: the clock, and a call only while there is something to do */
static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    t->arp_clk += gate_n;
    if (t->arp_n | t->p[P_AMODE] | song.playing)
        arp_block(t, n);
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    return swing_step_len(t, period, idx);   /* core.h: own + global, at most 100 */
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or
 * the next one when it is past the middle of the playing one). Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only). */
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len, k;
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t next = t->seq_pos > step_samples(t, period, t->seq_idx) / 2u;
    step_t *s;
    if (next)
        idx = (idx + 1u) % len;
    s = &t->step[idx];
    if (drum_track(t)) {                            /* DRUM: into the grid, no holds (hits) */
        uint32_t l = drum_lane(note);
        if (s->time != ST_NOTE || (!s->n && !s->hit)) {
            s->n = s->hit = s->acc = 0;
            s->flags = 0;
            s->vel = 0;
        }
        s->time = ST_NOTE;
        if (note == DRUM_LANE_NOTE[l]) {            /* a lane's note: its hit */
            s->hit |= (uint8_t)(1u << l);
            if (vel > 110)
                s->acc |= (uint8_t)(1u << l);
        } else {                                    /* another GM drum (a low tom, a crash): a note */
            for (k = 0; k < s->n && s->note[k] != note; k++)
                ;
            if (k == s->n) {
                if (s->n < 4u)
                    s->n++;
                s->note[s->n - 1u] = (uint8_t)note;
            }
            if (vel > 110)
                s->flags |= SF_ACCENT;
        }
        if (vel > s->vel)
            s->vel = (uint8_t)vel;
        t->seq_active = 1;
        if (next) {
            if (t->rskip_idx != idx)
                t->rskip_n = 0;
            t->rskip_idx = (uint8_t)idx;
            if (t->rskip_n < NLANE)
                t->rskip[t->rskip_n++] = (uint8_t)note;
        }
        return;
    }
    if (s->time != ST_NOTE || !s->n || trk_vmode(t) != V_POLY) {
        s->n = 0;                                   /* a fresh step */
        s->flags = 0;
        s->vel = 0;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        s->note[s->n - 1u] = (uint8_t)note;
    }
    s->time = ST_NOTE;
    if (vel > 110)
        s->flags |= SF_ACCENT;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    t->seq_active = 1;
    if (next) {                                     /* it sounds now: the step must not trigger it again */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* (recorded ahead into the step now starting) */
    s = &t->step[idx];
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
    s->hit = s->acc = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (t->rh_ties && t->seq_idx == t->rh_last &&
        t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    if (rec_on(t))                                  /* (the ARP on too: the chord played, not what it plays) */
        rec_note(t, note, vel);
    if (t->p[P_AMODE])
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the other mode (ARP switched
                                                     * while held); a pitch the arp sounds now is the arp's to end */
    if (!t->p[P_AMODE] || !arp_sounds(t, note))     /* (arp_end), its gate or the chord's end */
        trk_note_off(t, note);
}

/* the effect of key k in the FX layer (PF_N none): the first PF_M1 white keys (F3 .. A4) in order, black keys
 * 1..4 (F#3 G#3 A#3 C#4) the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_M1 ? p : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n = chord_build(t, kb_note[k], kb_chord[k]), i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, 100);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | 100u << 24);
    }
    kb_chn[k] = (uint8_t)n;
    drum_focus_note(t, kb_note[k]);
    last_note = kb_note[k];                         /* (step entry, the SAMPLE zone: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
}

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    uint32_t lay, fx;
    ch = cur ^ kb_prev;                           /* keys also sound while entering steps */
    if (!ch)
        return;
    lay = (fm1_in.buttons & kb_mask) || kb_lock;  /* (#83: kb_lock, a layer locked open with no button held) */
    fx = (fm1_in.buttons & perf_mask) || (kb_lock & 2u);
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            if (lay) {                            /* a layer's button held (or locked): the key is the layer's */
                kb_note[k] = KB_SILENT;           /* (ui_layer.c), with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if (fx)
                    perf_press(perf_key(k), 1);
                continue;
            }
            /* NAME (ui_name.c): every key types, none sounds; 4: the ARP pages' row and OP keys (ui_arp.c) */
            if (song.grid == 2u || (song.grid == 4u && ((ARP_KEYS >> k) & 1u)))
                kb_note[k] = KB_SILENT;
            else if (song.grid == 4u)
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            else if (song.grid)                   /* the DRUM grid: a lane key plays its lane, the rest are the UI's */
                kb_note[k] = key_black(k) && key_place(k) < NLANE ? DRUM_LANE_NOTE[key_place(k)] : KB_SILENT;
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_press(perf_key(k), 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            key_off(k, &trk[kb_trk[k] % NTRK]);
        }
    }
    kb_prev = cur;
}

/* -------------------------------------------------------- sequencer --- */
static void seq_start(void)
{
    uint32_t i;
    motion_begin();
    chain_start();
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
        t->rat_left = 0;
        arp_restart(t);                            /* the arp's grid is the song's, from PLAY */
    }
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

static void seq_release(track_t *t)
{
    uint32_t i;
    t->arp_chg |= (uint8_t)(t->seq_arp && t->seq_n);   /* (the arp's chord changes: KEY ATTACH) */
    for (i = 0; !t->seq_arp && i < t->seq_n; i++)   /* (the arp's chord sounds no voice: seq_chord) */
        trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_arp = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
    for (i = 0; i < NPART; i++)                    /* external clock: the arp stops with the transport, its sounding
                                                    * notes too (HOLD keeps the latched chord); nothing held, the
                                                    * sequencer's chord ends with STOP and so do its notes (KEY ATTACH
                                                    * NOTE / CYCLE let a chord let go ring; STOP does not), CYCLE's pass */
        if (song.g[G_CLOCK] || !trk[i].nheld) {
            if (trk[i].arp_n)
                arp_silence(&trk[i]);
            trk[i].arp_cn = 0;
        }
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
        trk[i].rat_left = 0;
    }
    chain_stop();
    motion_end();
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers its notes, then its lane hits (their GM
 * notes, DRUM_LANE_NOTE; an accented hit at 127); a SLIDE on the previous step makes this one legato with a
 * glide (acid style). skip: bit k = note k, bit 8 + l = the hit of lane l already sounds from live
 * recording (not triggered, not released here); SEQ_REP: a RATCH repeat (seq_ratchet).
 * RATCH x2..x4 (core.h step_ratchet): a NOTE step plays all of it again in equal parts of its swung length, each
 * part retriggered with GATE of the part; the chance is rolled once for the whole step (a failed roll: no part),
 * a slide into it glides into the first part only, and it never slides or ties out (its last part ends at its
 * gate). x1, what every older pattern holds, is the step exactly as before. */
/* the ARP on, a step's notes (nn, m, as they would sound: QNT SEQ done) are the arp's chord, as if held: no voice, no
 * note-off; it lasts as they would (GATE, TIEs, a slide; seq_tick and seq_release end it) and the arp plays it
 * (arp_list). Notes the sequencer sounded before the ARP came on end first. Stopped, a chord from nothing starts the
 * pattern at once. Not inlined: once a step */
static __attribute__((noinline)) void seq_chord(track_t *t, const uint8_t *nn, uint32_t m, uint32_t vel, uint32_t gate,
                                                uint32_t hold)
{
    uint32_t i, fresh;
    if (!t->seq_arp)
        seq_release(t);
    fresh = !t->seq_n && !t->nheld && !song.playing && !t->arp_cn;
    m = m < 4u ? m : 4u;
    t->arp_chg |= (uint8_t)(m != t->seq_n || memcmp(t->seq_notes, nn, m) != 0);   /* (KEY ATTACH) */
    for (i = 0; i < m && i < 4u; i++)
        t->seq_notes[i] = nn[i];
    t->seq_n = (uint8_t)i;
    t->seq_arp = 1;
    t->seq_avel = (uint8_t)vel;
    t->slide_glide = 0;
    t->seq_off = gate;
    t->seq_hold = (uint8_t)hold;
    if (fresh && i)
        arp_restart(t);
}
#define SEQ_REP (1u << 16)
static __attribute__((noinline)) void seq_step(track_t *t, const step_t *s, uint32_t period, uint32_t skip)
{
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold && t->seq_n && !(skip & SEQ_REP);
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = seq_steps(t)[(t->seq_idx + 1u) % len].time == ST_TIE;
    uint32_t hits = step_ratchet(s);
    uint8_t nn[4 + NLANE], vv[4 + NLANE];           /* the notes it plays: its notes, then its hits */
    uint32_t m = 0, sk = 0;
    /* QNT SEQ: the step's notes (not the lane hits) snap to the scale as they play; never on a drum kit, the slices
     * or another engine that maps the keys itself. seq_notes keeps the notes that sound: their note-offs match */
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t qseq = t->p[P_QUANT] == QN_SEQ && !(e->keys && e->keys(t, 0) >= 0);
    if (!(skip & SEQ_REP)) {
        t->rat_left = 0;
        if (step_chance(s) < 100u && rng() % 100u >= step_chance(s)) {
            seq_release(t);
            return;
        }
    }
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + period / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (s->time == ST_REST || (!s->n && !s->hit)) {
        seq_release(t);
        return;
    }
    if (hits > 1u) {                                /* RATCH: the gate of a part, no slide or tie out */
        gate = step_samples(t, period, t->seq_idx) / hits * (uint32_t)t->p[P_SGATE] / 128u;
        if (!(skip & SEQ_REP))
            t->rat_left = (uint8_t)(hits - 1u);
    }
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t x = s->note[i];
        if (qseq) {                                 /* QNT SEQ: onto the scale now; two notes snapping */
            x = (uint32_t)clamp(scale_snap(t, (int32_t)x), 0, 127);   /* together play once */
            for (j = 0; j < m && nn[j] != x; j++)
                ;
            if (j < m)
                continue;
        }
        nn[m] = (uint8_t)x;
        vv[m] = (uint8_t)vel;
        sk |= ((skip >> i) & 1u) << m;
        m++;
    }
    for (i = 0; i < NLANE; i++)
        if ((s->hit >> i) & 1u) {
            nn[m] = DRUM_LANE_NOTE[i];
            vv[m] = (uint8_t)((s->acc >> i) & 1u ? 127u : vel);
            sk |= ((skip >> (8u + i)) & 1u) << m;
            m++;
        }
    if (t->p[P_AMODE] && !drum_track(t)) {          /* WaveLoop Kit: the ARP plays the step (its live-recorded notes
                                                     * too: held anyway); a kit's hits play as ever */
        seq_chord(t, nn, m, vel, gate, hits == 1u && ((s->flags & SF_SLIDE) != 0 || next_tie));
        return;
    }
    if (t->seq_arp) {                               /* (the ARP went off: its chord was no voice) */
        t->seq_n = t->seq_arp = 0;
        slide_in = 0;
    }
    t->slide_glide = (uint8_t)slide_in;
    if (!slide_in)
        seq_release(t);
    for (i = 0; i < m; i++)
        if (!((sk >> i) & 1u))
            trk_note_on(t, nn[i], vv[i]);
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < m && nn[j] != t->seq_notes[i]; j++)
                ;
            if (j == m)
                trk_note_off(t, t->seq_notes[i]);
        }
    t->seq_n = 0;
    for (i = 0; i < m; i++)
        if (!((sk >> i) & 1u))
            t->seq_notes[t->seq_n++] = nn[i];
    t->seq_off = gate;
    t->seq_hold = hits == 1u && ((s->flags & SF_SLIDE) != 0 || next_tie);   /* next step a TIE: keep the notes to it */
}

/* RATCH: the next part of the playing step once the sequencer is that far into it (seq_step played the first).
 * The step is read again: an edit to fewer parts (or another source in a song) ends the repeats */
static __attribute__((noinline)) void seq_ratchet(track_t *t, uint32_t period)
{
    const step_t *s = &seq_steps(t)[t->seq_idx];
    uint32_t hits = step_ratchet(s);
    if (t->rat_left >= hits) {
        t->rat_left = 0;
        return;
    }
    if (t->seq_pos < (hits - t->rat_left) * (step_samples(t, period, t->seq_idx) / hits))
        return;
    t->rat_left--;
    seq_step(t, s, period, SEQ_REP);
}

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period, len;
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= n)
            seq_release(t);
        else
            t->seq_off -= n;
    }
    if (!song.playing)
        return;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? (chain.running ? chain.carry : 0u) : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % (len ? len : 1u));
        motion_step(t, t->seq_idx, chain.running ? &chain.source[chain.slot].motion : &motion);
        rec_hold(t, t->seq_idx, len ? len : 1u);
        {
            const step_t *s = &seq_steps(t)[t->seq_idx];
            uint32_t skip = 0, i, k;
            if (t->rskip_n && t->rskip_idx == t->seq_idx) {
                for (k = 0; k < t->rskip_n; k++) {
                    for (i = 0; i < s->n; i++)
                        if (s->note[i] == t->rskip[k])
                            skip |= 1u << i;
                    for (i = 0; i < NLANE; i++)
                        if (((s->hit >> i) & 1u) && DRUM_LANE_NOTE[i] == t->rskip[k])
                            skip |= 1u << (8u + i);
                }
                t->rskip_n = 0;
            }
            seq_step(t, s, period, skip);
        }
    }
    if (t->rat_left)
        seq_ratchet(t, period);
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-4 (0) channels 1..4 their parts (5..16 never get
 * here: midi_event drops them), SEL (1) every channel the selected track */
static uint8_t midi_route;                 /* the G_ROUTE events_block last saw (a change to CH1-4: midi_route_ch14) */
static track_t *midi_track(uint32_t ch) { return ch < NPART && !song.g[G_ROUTE] ? &trk[ch] : TSEL; }

#include "midi_control.c"
#include "midi_clock.c"

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.mode != clock_mode) {
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    } else if (transport_req == 3u) {                 /* GLO + PLAY: from the top without stopping (INT clock) */
        if (!clock_mode) {
            seq_stop();
            seq_start();
        }
        transport_req = 0;
    }
    if (midi_route != (uint8_t)song.g[G_ROUTE]) {     /* ROUT changed: CH1-4 lets go of channels 5..16 */
        midi_route = (uint8_t)song.g[G_ROUTE];
        if (!midi_route)
            midi_route_ch14();
    }
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        midi_pad_shift=midi_pad_down=midi_pad_layer=0;
        perf_midi_held=0;
        memset(midi_transport_held,0,sizeof midi_transport_held);
        midi_click_held=0;
        midi_setlist_held[0]=midi_setlist_held[1]=0;
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        midi_hint = 0;
        for (i = 0; i < NTRK; i++) {
            trk[i].rh_n = trk[i].rskip_n = 0;
            trk[i].seq_n = trk[i].seq_arp = trk[i].seq_hold = trk[i].slide_glide = trk[i].rat_left = 0;
        }
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            t->arp_n = 0;                             /* (its notes ended: trk_all_off) */
            t->arp_cn = 0;
            t->seq_n = t->seq_arp = 0;
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
        if (t->armp != t->p[P_AMODE] && t->seq_n)
            seq_release(t);                           /* the sequencer's notes were voices or the arp's chord: the next
                                                       * step plays them the other way */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = t->arp_cn = 0;
            if (t->arp_n && (!t->p[P_AMODE] || arp_att(t) == AT_NOW))
                arp_silence(t);                       /* (HOLD let go: a chord let go, KEY ATTACH as the keys) */
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else
            midi_event(st, ch, d1, d2);
    }
    if (clock_mode) {
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_n = midi_clock_advance(fm1_ms);
        }
    }
    chain_tick(seq_n);
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], seq_n);
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], clock_mode ? seq_n : n, clock_mode && song.playing ? seq_n : n);
    beat_pos += clock_mode ? seq_n : n;               /* the beat the ARP LED flashes on (ui_leds) */
    if (beat_pos >= beat_samples()) {
        beat_pos -= beat_samples();
        beat_pos = beat_pos < beat_samples() ? beat_pos : 0u;
        beat_n = (beat_n + 1u) & 3u;
    }
    if (song.playing)
        song.tick++;
}
