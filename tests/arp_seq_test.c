/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop Kit: SHAYPE on a track, against the real sources (ui_test.c's harness), the transport and the MIDI queue as
 * the firmware runs them (ported from WaveLoop's Switch Blade build, tests/arp_seq_test.c; the Kit's arp sends nothing
 * to MIDI OUT, so its notes are read from seq.c arp_snd block by block, and the voices checked):
 *   shapeChords parity   chords on the sequencer's steps, played through the arp: every note (its step, pitch, end,
 *                        velocity) as the toy's offline form shapeChords gives it (tests/arp_chords_vectors.h,
 *                        tools/arp_vectors.mjs --chords); the arp lets go with the chord
 *   edges                ARP off as before; the chord's end and STOP leave nothing sounding; ARP switched mid-chord;
 *                        a MIDI note joins the sequenced chord
 *   releases             a MIDI or key chord let go in every block of a step and at the edges of its gate, HOLD on /
 *                        off, the external clock stopped, the same pitch a key holds and the arp plays, CC123: every
 *                        note the arp starts ends once, every voice it started is released
 *   recording            ARP on: the chord played is recorded, not the arp's notes; MIDI step recording on SEQ > STEP
 *   soak                 random chords let go anywhere, HOLD, ARP, transport, RATE, GATE, patterns: nothing hangs */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "arp_chords_vectors.h"

static int bad;
/* ------------------------------------------------------- the arp's notes, captured --- */
typedef struct { uint32_t on, off; uint8_t pitch, vel; } cap_t;
#define CAP_N 2048u
static cap_t cap[CAP_N];
static uint32_t ncap, nblk, cap_lost;
static uint8_t prev_note[ARP_SND], prev_n;
static uint32_t prev_off[ARP_SND], prev_cap[ARP_SND];
static uint32_t voice_vel(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && t->v[i].note == note)
            return t->v[i].vel;
    return 0;
}
static void cap_reset(void)
{
    ncap = nblk = cap_lost = 0;
    prev_n = 0;
}
/* track 1's arp_snd against the block before: an entry gone is a note-off, a new one (or the same pitch with another
 * end: a retrigger) a note-on */
static void cap_scan(void)
{
    const track_t *t = &trk[0];
    uint32_t i, j, n = t->arp_n < ARP_SND ? t->arp_n : ARP_SND, cur_cap[ARP_SND];
    for (j = 0; j < prev_n; j++) {
        for (i = 0; i < n; i++)
            if (arp_snd[0].note[i] == prev_note[j] && arp_snd[0].off[i] == prev_off[j])
                break;
        if (i == n && prev_cap[j] < ncap)
            cap[prev_cap[j]].off = nblk * CTL;
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < prev_n; j++)
            if (arp_snd[0].note[i] == prev_note[j] && arp_snd[0].off[i] == prev_off[j])
                break;
        if (j < prev_n) {
            cur_cap[i] = prev_cap[j];
        } else if (ncap < CAP_N) {
            cap[ncap] = (cap_t){nblk * CTL, ~0u, arp_snd[0].note[i], (uint8_t)voice_vel(t, arp_snd[0].note[i])};
            cur_cap[i] = ncap++;
        } else {
            cur_cap[i] = ~0u;
            cap_lost++;
        }
    }
    for (i = 0; i < n; i++) {
        prev_note[i] = arp_snd[0].note[i];
        prev_off[i] = arp_snd[0].off[i];
        prev_cap[i] = cur_cap[i];
    }
    prev_n = (uint8_t)n;
}
static void blk1(void)                               /* one control block, then what the arp sounds */
{
    events_block(CTL);
    nblk++;
    cap_scan();
}
static void blks(uint32_t n) { while (n--) blk1(); }
static uint32_t cap_hanging(void)                    /* notes on with no off */
{
    uint32_t k, n = 0;
    for (k = 0; k < ncap; k++)
        n += cap[k].off == ~0u;
    return n;
}
static void mi(uint32_t st, uint32_t d1, uint32_t d2)   /* a USB MIDI message in (taken by the next block) */
{
    midi_enqueue((st >> 4) | st << 8 | d1 << 16 | d2 << 24, 1u);
}
static uint32_t track_gates(const track_t *t, uint32_t note)   /* t's voices with their gate on (note: any if 128) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].gate && (note > 127u || t->v[i].note == note);
    return n;
}

/* track 1 for the arp: the sequencer at 1/16, GATE 127, no swing, POLY; the arp: factory pattern k with its STEPS and
 * BOUND, then the rest */
static track_t *setup(uint32_t pat, uint32_t dir, uint32_t rate, uint32_t gate, uint32_t len)
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    t = &trk[0];
    song.sel = 0;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_ROUTE] = 0;
    for (i = 0; i < NTRK; i++) {                     /* (the other tracks: no pattern) */
        uint32_t j;
        for (j = 0; j < NSTEP; j++)
            trk[i].step[j] = (step_t){{0}, 0, ST_REST, 0, 0};
    }
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = 2;
    t->p[P_SGATE] = 127;
    t->p[P_SSWING] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_QUANT] = 0;
    t->p[P_TRANS] = 0;
    t->p[P_CHRD] = 0;
    t->p[P_AMODE] = 1;
    ash_load(&arp_pat[0], pat);
    t->p[P_APROB] = ARP_FACTORY[pat].steps;
    t->p[P_AORDER] = ARP_FACTORY[pat].bound;
    t->p[P_AOCT] = (int16_t)dir;
    t->p[P_ARATE] = (int16_t)rate;
    t->p[P_AGATE] = (int16_t)gate;
    t->p[P_ASWING] = 0;
    t->p[P_AHOLD] = 0;
    arp_x[0][ARPX_ROOT] = arp_x[0][ARPX_OCT] = 0;
    events_block(CTL);                               /* (the ISR sees ARP on) */
    cap_reset();
    return t;
}
static void chord_steps(track_t *t, const uint8_t *notes, uint32_t n, uint32_t a, uint32_t b, uint32_t vel)
{
    uint32_t i;
    n = n < 4u ? n : 4u;                             /* (a step holds 4) */
    t->step[a] = (step_t){{0}, (uint8_t)n, ST_NOTE, 0, (uint8_t)vel};
    for (i = 0; i < n; i++)
        t->step[a].note[i] = notes[i];
    for (i = a + 1u; i < b; i++)
        t->step[i] = (step_t){{0}, 0, ST_TIE, 0, 0};
}
static uint32_t factory(const char *name)
{
    uint32_t k;
    for (k = 0; k < ARP_NFACTORY; k++)
        if (!strcmp(ARP_FACTORY[k].name, name))
            return k;
    return 0;
}

/* ------------------------------------------------------- shapeChords parity --- */
static int by_tick_pitch(const void *a, const void *b)
{
    const uint32_t *x = a, *y = b;
    return x[0] != y[0] ? (int)x[0] - (int)y[0] : (int)x[1] - (int)y[1];
}
static int parity_case(uint32_t c, int verbose)
{
    const acv_t *v = &ACV[c];
    static uint32_t want[CAP_N][4], got[CAP_N][4];
    uint32_t parp, pseq, i, k, end, n;
    track_t *t = setup(factory(ACV_PAT[v->pat]), v->dir, v->rate, v->gate, v->len);
    int ok = 1;
    t->p[P_AORDER] = v->bound;                       /* (the case's, not the pattern's own) */
    t->p[P_APROB] = v->steps;
    for (i = 0; i < v->nchord; i++)
        chord_steps(t, v->chord[i].notes, v->chord[i].n, v->chord[i].start, v->chord[i].stop, v->chord[i].vel);
    pseq = div_samples(2);
    parp = arp_period(t);
    end = v->len * pseq;
    transport_req = 1;
    while (nblk * CTL < end)
        blk1();
    transport_req = 2;
    blks(4);
    for (k = 0; k < v->nev; k++) {                   /* shapeChords's notes: the end its own, or the chord's first */
        const uint16_t *e = v->ev + 4u * k;          /* (the end of the chords one after another from its step: the */
        uint32_t stop = e[2], ce = ~0u, at = ~0u, j;  /* arp lets go when nothing sounds) */
        for (i = 0; i < v->nchord; i++)
            if (v->chord[i].start * pseq <= e[0] * parp && e[0] * parp < v->chord[i].stop * pseq)
                at = v->chord[i].stop;
        for (j = 0; at != ~0u && j < v->nchord; j++)
            for (i = 0; i < v->nchord; i++)
                if (v->chord[i].start == at)
                    at = v->chord[i].stop;
        if (at != ~0u)
            ce = (uint32_t)((uint64_t)at * pseq * 96u / parp);
        want[k][0] = e[0];
        want[k][1] = e[1];
        want[k][2] = stop < ce ? stop : ce;
        want[k][3] = e[3];
    }
    for (k = 0; k < ncap; k++) {
        got[k][0] = (cap[k].on + parp / 2u) / parp;
        got[k][1] = cap[k].pitch;
        got[k][2] = cap[k].off == ~0u ? ~0u : (uint32_t)(((uint64_t)cap[k].off * 96u + parp / 2u) / parp);
        got[k][3] = cap[k].vel;
    }
    qsort(want, v->nev, sizeof want[0], by_tick_pitch);
    qsort(got, ncap, sizeof got[0], by_tick_pitch);
    n = ncap;
    if (n != v->nev) {
        ok = 0;
        if (verbose)
            printf("    case %u (%s): the firmware played %u notes, shapeChords %u\n", (unsigned)c, ACV_PAT[v->pat],
                   (unsigned)n, (unsigned)v->nev);
    }
    for (k = 0; ok && k < n; k++) {
        int32_t d = (int32_t)got[k][2] - (int32_t)want[k][2];
        if (got[k][0] != want[k][0] || got[k][1] != want[k][1] || got[k][3] != want[k][3] || d < -3 || d > 3) {
            ok = 0;
            if (verbose)
                printf("    case %u (%s) note %u: firmware tick %u pitch %u end %u vel %u, shapeChords %u %u %u %u\n",
                       (unsigned)c, ACV_PAT[v->pat], (unsigned)k, (unsigned)got[k][0], (unsigned)got[k][1],
                       (unsigned)got[k][2], (unsigned)got[k][3], (unsigned)want[k][0], (unsigned)want[k][1],
                       (unsigned)want[k][2], (unsigned)want[k][3]);
        }
    }
    if (cap_hanging() || cap_lost || t->arp_n || track_gates(t, 128))
        ok = 0;
    return ok;
}
static void parity(void)
{
    uint32_t c, fails = 0, notes = 0;
    char line[120];
    for (c = 0; c < ACV_N; c++) {
        notes += ACV[c].nev;
        fails += !parity_case(c, fails < 3u);
    }
    snprintf(line, sizeof line, "SEQ > ARP: %u cases, %u notes as shapeChords plays the same chords (step, pitch, end, vel)",
             (unsigned)ACV_N, (unsigned)notes);
    bad += check(line, !fails);
}

/* ---------------------------------------------------------------- edges --- */
static const uint8_t CEG[3] = {60, 64, 67};
static void edges(void)
{
    track_t *t;
    uint32_t k, ok, n;

    /* ARP off: the sequencer sounds its notes as before (voices at the step, no arp chord) */
    t = setup(0, AD_UP, 5, 75, 16);
    t->p[P_AMODE] = 0;
    events_block(CTL);
    chord_steps(t, CEG, 3, 0, 4, 90);
    transport_req = 1;
    blk1();
    ok = !t->seq_arp && t->seq_n == 3u && !t->arp_n && track_gates(t, 60) && track_gates(t, 64) && track_gates(t, 67);
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: ARP off, the step's chord sounds itself as before; STOP ends it", ok && !track_gates(t, 128));

    /* the arp plays the sequenced chord from PLAY; the chord over (a REST), nothing held: it stops, nothing sounds */
    t = setup(0, AD_UP, 5, 75, 16);
    chord_steps(t, CEG, 3, 0, 4, 90);
    transport_req = 1;
    blk1();
    ok = t->seq_arp && t->seq_n == 3u && t->arp_n == 2u && ncap == 2u;   /* (SHAYPE step 1: rows 1 and 3, C + C') */
    blks(4u * div_samples(2) / CTL + 4u);
    ok &= !t->arp_n && !track_gates(t, 128) && !cap_hanging();
    n = ncap;
    blks(div_samples(2) / CTL);
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: the sequenced chord is the arp's from PLAY; when it ends, nothing sounds or hangs",
                 ok && ncap == n && !cap_hanging());

    /* STOP mid-chord: the arp's notes end, its voices are released */
    t = setup(factory("UP 4"), AD_UP, 5, 75, 16);
    chord_steps(t, CEG, 3, 0, 16, 90);
    transport_req = 1;
    blks(3u * arp_period(t) / CTL + 3u);
    ok = t->arp_n != 0u;
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: STOP with the arp playing the sequence: its notes end, none held", ok && !t->arp_n &&
                 !track_gates(t, 128) && !cap_hanging() && !t->seq_n);

    /* ARP off mid-chord: the sequence sounds itself at its next step; on again, the arp takes it */
    t = setup(factory("UP 4"), AD_UP, 5, 75, 16);
    for (k = 0; k < 16u; k++)
        chord_steps(t, CEG, 3, k, k + 1u, 90);
    transport_req = 1;
    blks(2u * div_samples(2) / CTL);
    t->p[P_AMODE] = 0;
    blks(div_samples(2) / CTL + 2u);
    ok = !t->arp_n && !t->seq_arp && t->seq_n == 3u && track_gates(t, 60) && track_gates(t, 64) && track_gates(t, 67);
    t->p[P_AMODE] = 1;
    blks(div_samples(2) / CTL + 2u);
    ok &= t->seq_arp && t->arp_n != 0u && track_gates(t, 128) <= t->arp_n;
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: ARP off mid-chord, the sequence sounds itself next step; on again, the arp takes it",
                 ok && !track_gates(t, 128));

    /* a MIDI note held joins the sequenced chord: UP 4 over C E G + B reaches B on its 4th step; let go it leaves */
    t = setup(factory("UP 4"), AD_UP, 5, 75, 16);
    chord_steps(t, CEG, 3, 0, 16, 90);
    mi(0x90, 71, 100);
    transport_req = 1;
    blks(4u * arp_period(t) / CTL + 2u);
    for (k = 0, n = 0; k < ncap; k++)
        n += cap[k].pitch == 71;
    ok = n == 1u;
    mi(0x80, 71, 0);
    blks(8u * arp_period(t) / CTL);
    for (k = 0, n = 0; k < ncap; k++)
        n += cap[k].pitch == 71;
    transport_req = 2;
    blks(2);
    bad += check("SEQ > ARP: a MIDI note held joins the sequenced chord (UP 4 reaches it), let go it leaves",
                 ok && n == 1u && !cap_hanging() && !track_gates(t, 128));

    /* OCT (ARP 3, OP3): what the arp plays an octave up; past 127 not played */
    t = setup(0, AD_UP, 5, 75, 16);
    arp_x[0][ARPX_OCT] = 1;
    chord_steps(t, CEG, 3, 0, 16, 90);
    transport_req = 1;
    blk1();
    ok = t->arp_n == 2u && arp_sounds(t, 72) && arp_sounds(t, 79);   /* (step 1, rows 1 and 3: C + G, an octave up) */
    transport_req = 2;
    blks(2);
    arp_x[0][ARPX_OCT] = 3;
    {
        static const uint8_t HI[1] = {120};
        chord_steps(t, HI, 1, 0, 16, 90);
    }
    transport_req = 1;
    blks(2);
    ok &= !t->arp_n;                                 /* (120 + 36: past 127) */
    transport_req = 2;
    blks(2);
    arp_x[0][ARPX_OCT] = 0;
    bad += check("SEQ > ARP: OCT +1 moves what the arp plays an octave up; past 127: silent", ok);
}

/* ------------------------------------------------------------- releases --- */
enum { RL_MIDI, RL_KEYS };
static uint32_t key_of(const track_t *t, uint32_t note)   /* the key that plays note now */
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (kb_map(t, k) == note)
            return k;
    return 0;
}
static int rel_quiet(const track_t *t) { return !t->arp_n && !track_gates(t, 128) && !cap_hanging(); }
/* C E G held from block 0 (MIDI ch 1 or the keys), the arp stopped (the internal clock: free; or the external one,
 * selected and never started), let go at rel samples: the arp's notes end in that block, every voice released; HOLD:
 * latched, it plays on, HOLD off ends it */
static int rel_case(uint32_t rel, uint32_t hold, uint32_t via, uint32_t ext, int verbose)
{
    track_t *t = setup(0, AD_UP, 3, 75, 16);         /* SHAYPE, 1/8, UP, GATE 75 % (WRAP, 4 steps: its own) */
    uint32_t i, n0, keys = 0, ok = 1;
    const char *why = "";
    t->p[P_AHOLD] = (int16_t)hold;
    song.g[G_CLOCK] = (int16_t)ext;
    events_block(CTL);
    cap_reset();
    for (i = 0; i < 3u; i++)
        if (via == RL_KEYS)
            keys |= 1u << key_of(t, CEG[i]);
        else
            mi(0x90, CEG[i], 100);
    fm1_in.notes = keys;
    while (nblk * CTL < rel)
        blk1();
    if (!ncap) {
        ok = 0;
        why = "the arp played nothing";
    }
    if (via == RL_KEYS)
        fm1_in.notes = 0;
    else
        for (i = 0; i < 3u; i++)
            mi(0x80, CEG[i], 0);
    blk1();
    if (!hold && ok && !rel_quiet(t)) {
        ok = 0;
        why = "the chord let go, the arp's notes still sound";
    }
    if (hold) {                                      /* latched: it plays on; HOLD off ends it at once */
        n0 = ncap;
        blks(2u * arp_period(t) / CTL + 2u);
        if (ok && (!t->nheld || (!ext && ncap == n0))) {   /* (a stopped external clock: latched, still) */
            ok = 0;
            why = "HOLD: the latched chord did not play on";
        }
        t->p[P_AHOLD] = 0;
        blk1();
        if (ok && !rel_quiet(t)) {
            ok = 0;
            why = "HOLD off, the arp's notes still sound";
        }
    }
    n0 = ncap;
    blks(FS / CTL);
    if (ok && (ncap != n0 || !rel_quiet(t) || t->nheld)) {
        ok = 0;
        why = "a note after the chord was gone, or one left hanging";
    }
    if (cap_lost)
        ok = 0;
    if (!ok && verbose)
        printf("    release at %u samples, HOLD %u, %s%s: %s (arp_n %u, gates %u, hanging %u)\n", (unsigned)rel,
               (unsigned)hold, via == RL_KEYS ? "keys" : "MIDI", ext ? ", external clock stopped" : "", why,
               (unsigned)t->arp_n, (unsigned)track_gates(t, 128), (unsigned)cap_hanging());
    song.g[G_CLOCK] = 0;
    fm1_in.notes = 0;
    keyboard_block();
    return ok;
}
static void releases(void)
{
    track_t *t = setup(0, AD_UP, 3, 75, 16);
    uint32_t p = arp_period(t), h, v, off, fails = 0, cases = 0, ok, j;
    /* every block of a step (from its start to the next one's), MIDI, HOLD off: on the step boundary too */
    for (off = 0; off <= p + CTL; off += CTL) {
        fails += !rel_case(p + off, 0, RL_MIDI, 0, fails < 3u);
        cases++;
    }
    /* at the edges of a step and its gate: HOLD on / off, MIDI / keys */
    for (h = 0; h < 2u; h++)
        for (v = 0; v < 2u; v++) {
            uint32_t g = p * 3u / 4u, at[8] = {0, CTL, g - CTL, g, g + CTL, p / 2u, p - CTL, p};
            for (j = 0; j < 8u; j++) {
                fails += !rel_case(5u * p + at[j], h, v, 0, fails < 3u);
                cases++;
            }
        }
    bad += check("ARP release mid-step: every block of a step and the edges of its gate, HOLD on / off, MIDI / the "
                 "keys: the arp's notes end with the chord, every voice released, nothing hangs", !fails);
    printf("    (%u release cases)\n", (unsigned)cases);

    /* the external clock selected, stopped: the chord plays its first step, the gate still ends in real time */
    fails = 0;
    for (h = 0; h < 2u; h++) {
        fails += !rel_case(p / 4u, h, RL_MIDI, 1, fails < 3u);
        fails += !rel_case(2u * p + p / 3u, h, RL_MIDI, 1, fails < 3u);
    }
    bad += check("ARP release, the external clock stopped: its first step's notes end at the gate or the release", !fails);

    /* the same pitch a key holds and the arp plays: SHAYPE's step 2 over C E G is E + C (rows 2 and 4, C wrapping);
     * E's key let go while the arp sounds E: the arp's E ends with its gate, not cut short by the key, and once */
    t = setup(0, AD_UP, 3, 75, 16);
    fm1_in.notes = 1u << key_of(t, 60) | 1u << key_of(t, 64) | 1u << key_of(t, 67);
    for (j = 0; j < 4000u && !arp_sounds(t, 64); j++)
        blk1();
    ok = arp_sounds(t, 64) && track_gates(t, 64);
    fm1_in.notes &= ~(1u << key_of(t, 64));
    blk1();
    ok &= arp_sounds(t, 64) && track_gates(t, 64) == 1u;   /* (the arp's E is the arp's to end) */
    for (j = 0; j < 400u && arp_sounds(t, 64); j++)
        blk1();
    ok &= !track_gates(t, 64) && t->nheld == 2u;
    fm1_in.notes = 0;
    blk1();
    blks(FS / CTL);
    bad += check("ARP: a key let go while the arp sounds its pitch: the arp's note ends at its gate, once; the rest "
                 "play on; all let go, nothing hangs", ok && rel_quiet(t));

    /* All Notes Off (CC123) mid-gate, the chord held by MIDI or latched: the arp's notes end at once */
    fails = 0;
    for (h = 0; h < 2u; h++) {
        uint32_t i;
        t = setup(0, AD_UP, 3, 75, 16);
        t->p[P_AHOLD] = (int16_t)h;
        for (i = 0; i < 3u; i++)
            mi(0x90, CEG[i], 100);
        blks(p / CTL + 3u);
        if (h)
            for (i = 0; i < 3u; i++)
                mi(0x80, CEG[i], 0);
        mi(0xB0, 123, 0);
        blk1();
        t->p[P_AHOLD] = 0;
        blk1();
        blks(FS / CTL);
        fails += !rel_quiet(t) || t->nheld;
    }
    bad += check("ARP: All Notes Off (CC123) mid-gate, held or latched: the arp's notes end, nothing hangs", !fails);
}

/* ------------------------------------------------------------ recording --- */
static void recording(void)
{
    track_t *t;
    uint32_t i, k, ok;
    /* REC with the ARP on: the MIDI chord played is written (one step, TIEs), not the arp's notes */
    t = setup(0, AD_UP, 5, 75, 16);
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){{0}, 0, ST_REST, 0, 0};
    song.rec = 1;
    transport_req = 1;
    blk1();
    for (i = 0; i < 3u; i++)
        mi(0x90, CEG[i], 100);
    blks(3u * div_samples(2) / CTL);
    ok = ncap != 0u;
    for (i = 0; i < 3u; i++)
        mi(0x80, CEG[i], 0);
    blk1();
    for (i = 0, k = 0; i < NSTEP; i++)
        k += t->step[i].time == ST_NOTE && t->step[i].n;
    ok &= k == 1u && t->step[0].n == 3u && t->step[1].time == ST_TIE;
    transport_req = 2;
    blks(2);
    song.rec = 0;
    bad += check("REC with the ARP on: the MIDI chord played is written (one step, TIEs), not the arp's notes", ok);

    /* MIDI step recording on SEQ > STEP (armed, stopped): a chord held on the controller is one step, let go it moves */
    ui_power_on();
    mst_r = mst_w;                                   /* (the notes of the cases before: not this step's) */
    song.sel = 0;
    t = &trk[0];
    t->p[P_VOICE] = V_POLY;
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){{0}, 0, ST_REST, 0, 0};
    song.rec = 1;
    go_page(GR_ROLL);
    ui.cursor = 0;
    frame();
    for (i = 0; i < 3u; i++)
        mi(0x90, CEG[i], 100);
    events_block(CTL);
    frame();
    ok = t->step[0].n == 3u && t->step[0].note[0] == 60 && t->step[0].note[2] == 67 && ui.cursor == 0u;
    for (i = 0; i < 3u; i++)
        mi(0x80, CEG[i], 0);
    events_block(CTL);
    frame();
    ok &= ui.cursor == 1u;
    mi(0x90, 62, 100);
    events_block(CTL);
    frame();
    mi(0x80, 62, 0);
    events_block(CTL);
    frame();
    ok &= t->step[1].n == 1u && t->step[1].note[0] == 62 && ui.cursor == 2u;
    mi(0x91, 65, 100);                               /* (channel 2: track 2 plays it, the step is T1's) */
    events_block(CTL);
    frame();
    mi(0x81, 65, 0);
    events_block(CTL);
    frame();
    ok &= t->step[2].n == 0u && ui.cursor == 2u;
    song.rec = 0;
    bad += check("MIDI step recording on STEP: the track's channel writes the cursor step, a chord held is one step, "
                 "let go it moves on; another track's channel only plays", ok);
}

/* ---------------------------------------------------------------- soak --- */
static uint32_t sk_rnd(void)
{
    static uint32_t s = 0x5EEDu;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
static void soak(void)
{
    track_t *t = setup(0, AD_UP, 5, 75, 16);
    uint32_t b, i, held[4] = {0}, nh = 0, fails = 0, maxn = 0;
    for (i = 0; i < 16u; i++)
        if (sk_rnd() % 3u == 0u)
            chord_steps(t, CEG, 3, i, i + 1u, 90);
    for (b = 0; b < 40u * FS / CTL; b++) {           /* 40 s */
        uint32_t r = sk_rnd();
        if (r % 97u == 0u && nh < 4u) {
            held[nh] = 48u + sk_rnd() % 36u;
            mi(0x90, held[nh++], 100);
        } else if (r % 89u == 0u && nh) {
            mi(0x80, held[--nh], 0);
        } else if (r % 1999u == 0u) {
            t->p[P_AHOLD] = (int16_t)!t->p[P_AHOLD];
        } else if (r % 2503u == 0u) {
            t->p[P_AMODE] = (int16_t)!t->p[P_AMODE];
        } else if (r % 3001u == 0u) {
            transport_req = song.playing ? 2u : 1u;
        } else if (r % 1601u == 0u) {
            t->p[P_ARATE] = (int16_t)(sk_rnd() % 8u);
            t->p[P_AGATE] = (int16_t)(10u + sk_rnd() % 91u);
        } else if (r % 4099u == 0u) {
            fm1_irq_off();
            ash_load(&arp_pat[0], sk_rnd() % ARP_NFACTORY);
            fm1_irq_on();
        }
        blk1();
        maxn = t->arp_n > maxn ? t->arp_n : maxn;
        if (track_gates(t, 128) > NVOICE)
            fails++;
    }
    while (nh)
        mi(0x80, held[--nh], 0);
    t->p[P_AHOLD] = 0;
    transport_req = 2;
    blks(2u * FS / CTL);
    bad += check("soak: 40 s of MIDI chords, HOLD, ARP, transport, RATE, GATE, patterns: nothing hangs at the end",
                 !fails && rel_quiet(t) && !t->nheld && maxn > 0u && !cap_lost);
}

int main(void)
{
    parity();
    edges();
    releases();
    recording();
    soak();
    printf("arp_seq: %s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
