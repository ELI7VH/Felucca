/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Real sequencer/project/UI sources: motion lifecycle and compact migration. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int motion_recording(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 23; song.sel = 0; song.rec = 1;
    seq_start(); seq_tick(t, CTL); /* first real step */
    t->p[P_REV] = 92;
    bad += check("motion records the selected armed track at its current step", !motion_capture(t, P_REV, 92) &&
        motion.count == 1u && motion.event[0].place == 0u && motion.event[0].value == 92);
    t->p[P_REV] = 110; motion_capture(t, P_REV, 110);
    bad += check("same step/parameter overwrites rather than consuming capacity", motion.count == 1u && motion.event[0].value == 110);
    bad += check("sounding values never replace the original patch base", t->p[P_REV] == 110 && motion_base_value(t, P_REV) == 23);
    project_t q; project_store_t packed;
    project_capture(&q);
    bad += check("project snapshot saves base + independent events while sounding", q.t[0].p[P_REV] == 23 && q.motion.event[0].value == 110 &&
        proj_pack(&packed, &q) && sizeof packed == 3840u);
    seq_stop();
    bad += check("stop before another step restores the original parameter", t->p[P_REV] == 23);
    seq_start(); seq_tick(t, CTL);
    bad += check("recorded motion plays back at step zero", t->p[P_REV] == 110);
    motion_set_enabled(t, 0);
    bad += check("bypass restores base without deleting automation", t->p[P_REV] == 23 && motion_count(t) == 1u && !motion_enabled(t));
    motion_set_enabled(t, 1); motion_step(t, 0, &motion);
    song.rec = 0; t->p[P_REV] = 37; motion_capture(t, P_REV, 37); seq_stop();
    bad += check("manual edits outside recording become the new base", t->p[P_REV] == 37);
    song.rec = 3; seq_start(); seq_tick(t, CTL);
    trk[1].p[P_REV] = 45; motion_capture(&trk[1], P_REV, 45);
    bad += check("only the selected track captures knob motion", motion_count(&trk[1]) == 0u);
    bad += check("transport and engine-switch parameters cannot be captured", !motion_param(P_SLEN) && !motion_param(P_AMODE) &&
        motion_set_event(t, 0, P_SLEN, 8) == 1);
    seq_stop(); motion_clear(t);
    t->p[P_REV] = 42; song.rec = 1; seq_start(); seq_tick(t, CTL);
    motion_clear(t); t->p[P_REV] = 70; motion_capture(t, P_REV, 70); seq_stop();
    bad += check("clearing then recording mid-play keeps the correct new base", t->p[P_REV] == 42);
    return bad;
}
static int motion_capacity(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t i = 0; i < MOTION_MAX; i++) bad += motion_set_event(&trk[0], i, P_REV, (int16_t)i);
    motion_store_t saved = motion;
    bad += check("full event pool refuses append without overwriting earlier events", motion_set_event(&trk[1], 0, P_REV, 90) == 2 &&
        !memcmp(&motion, &saved, sizeof saved));
    bad += check("full pool still permits a targeted overwrite", !motion_set_event(&trk[0], 12, P_REV, 100) && motion.count == MOTION_MAX);
    motion_delete_event(&trk[0], 12, P_REV);
    bad += check("deleting one event releases one slot", motion.count == MOTION_MAX - 1u && !motion_set_event(&trk[1], 0, P_REV, 90));
    saved = motion; motion_store_t invalid = motion; invalid.count = MOTION_MAX + 1;
    bad += check("invalid restore leaves the pool intact", motion_replace_track(&trk[0], &invalid) != 0 && !memcmp(&motion, &saved, sizeof saved));
    motion_clear(&trk[0]);
    bad += check("track clear keeps every other track's events", motion.count == 1u && motion.event[0].place >> 6 == 1u);
    return bad;
}
static int probability_playback(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    step_t s = {{60, 64, 0, 0}, 2, ST_NOTE, 0, 90, 1u << DV_KICK, 0, 0};
    bad += check("zero-initialized probability remains legacy 100 percent", step_chance(&s) == 100u);
    step_set_chance(&s, 0); seq_step(t, &s, div_samples(2), 0);
    bad += check("zero percent suppresses the whole chord and drum hits", step_chance(&s) == 0u && !t->seq_n);
    step_set_chance(&s, 100); seq_step(t, &s, div_samples(2), 0);
    bad += check("100 percent plays all chord notes and drum hits", t->seq_n == 3u);
    seq_release(t); step_set_chance(&s, 50); uint32_t heard = 0;
    for (uint32_t i = 0; i < 1000u; i++) { seq_step(t, &s, div_samples(2), 0); heard += t->seq_n != 0; seq_release(t); }
    bad += check("chance is evaluated each repeat with one decision per step", heard > 350u && heard < 650u);
    return bad;
}
static int compact_project(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->step[3] = (step_t){{60, 67}, 2, ST_NOTE, SF_ACCENT, 110, 0x81, 0x80, 0};
    ash_load(&arp_pat[0], 3); ash_paint(&arp_pat[0], 7, 5, 3);   /* (SHAYPE: STABS and a note: a pattern of its own) */
    arp_x[0][ARPX_PAT] = 0; arp_x[0][ARPX_ROOT] = 5; arp_x[0][ARPX_OCT] = -2;
    step_set_chance(&t->step[3], 25); t->p[P_FM1_LEVEL] = 80; t->p[P_ED_FLT] = -50;
    motion_set_event(t, 3, P_REV, 110);
    project_t before, after; project_store_t packed, corrupt;
    project_capture(&before);
    bad += check("FUN9 fits the retained and flash extent", sizeof(proj_slot) == 4u * 3840u && sizeof packed <= 4096u - 256u &&
        proj_pack(&packed, &before));
    bad += check("FUN9 round trip preserves signed values/FM params/probability/motion/SHAYPE", proj_import(&after, &packed, sizeof packed) &&
        !memcmp(&before, &after, sizeof before));
    corrupt = packed; corrupt.raw[112] ^= 1u;
    bad += check("FUN7 torn or corrupted payload is refused", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; corrupt.raw[68] = 255; uint32_t sum = proj_hash(corrupt.raw, sizeof corrupt - 4u);
    memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("compact parameter range is validated even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; uint32_t probability_byte = 68u + P_COUNT + 2u + 8u; corrupt.raw[probability_byte] = 127;
    sum = proj_hash(corrupt.raw, sizeof corrupt - 4u); memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("invalid probability is refused even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    project_v6_t old; memset(&old, 0, sizeof old); old.magic = PROJ_MAGIC_V6; old.size = sizeof old;
    memcpy(old.g, before.g, sizeof old.g); old.parts = NPART; old.phys = PROJ_PHYS;
    for (uint32_t k = 0; k < NTRK; k++) {
        for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = before.t[k].p[j];
        for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = before.t[k].p[P_E0 + j];
        old.t[k].engine = before.t[k].engine; old.t[k].preset = before.t[k].preset;
        for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &before.t[k].step[j], sizeof(step10_t));
    }
    old.chain = before.chain; old.sum = proj_hash(&old, sizeof old - 4u);
    bad += check("real FUN6 disk image migrates with FM defaults/100% chance/no motion", proj_import(&after, &old, sizeof old) &&
        after.t[0].p[P_E0] == before.t[0].p[P_E0] && after.t[0].p[P_ED_FLT] == -50 &&
        after.t[0].p[P_FM1_LEVEL] == 127 && !after.motion.count && step_chance(&after.t[0].step[3]) == 100u);
    bad += check("migrated FUN6 can be written as fixed-size FUN7", proj_pack(&packed, &after));
    project_save(1); motion_clear(t); t->p[P_FM1_LEVEL] = 127; step_set_chance(&t->step[3], 100);
    {
        arp_pat_t mine = arp_pat[0];
        arp_defaults(0);
        project_load(1);
        bad += check("actual project save/load restores motion, chance, operator settings and SHAYPE", motion_count(t) == 1u &&
            step_chance(&t->step[3]) == 25u && t->p[P_FM1_LEVEL] == 80 && !memcmp(&arp_pat[0], &mine, sizeof mine) &&
            arp_x[0][ARPX_PAT] == 0 && arp_x[0][ARPX_ROOT] == 5 && arp_x[0][ARPX_OCT] == -2);
    }
    {   /* FUN8 (midi24 and before: 3584 bytes, no SHAYPE blocks) loads: SHAYPE on every track, the old ARP mapped */
        project_store_t v8;
        uint32_t h;
        int ok;
        before.t[1].p[P_AMODE] = 3; before.t[1].p[P_ARATE] = 2; before.t[1].p[P_AOCT] = 3; before.t[1].p[P_AGATE] = 96;
        before.t[1].p[P_APROB] = 100; before.t[1].p[P_AORDER] = 1; before.t[1].p[P_AHOLD] = 1; before.t[1].p[P_ASWING] = 30;
        before.t[2].p[P_AMODE] = 0; before.t[2].p[P_ARATE] = 8;   /* (2BAR: the slowest) */
        ok = proj_pack(&packed, &before);
        memset(&v8, 0, sizeof v8);
        memcpy(v8.raw, packed.raw, PROJ_STORE_V8 - 16u);                   /* (FUN9 keeps FUN8's bytes up to its name) */
        memcpy(v8.raw + PROJ_STORE_V8 - 16u, packed.raw + PROJ_NAME_OFF, PROJ_NAME_LEN);
        h = PROJ_MAGIC_V8; memcpy(v8.raw, &h, 4); h = PROJ_STORE_V8; memcpy(v8.raw + 4, &h, 4);
        h = proj_hash(v8.raw, PROJ_STORE_V8 - 4u); memcpy(v8.raw + PROJ_STORE_V8 - 4u, &h, 4);
        ok &= proj_import(&after, &v8, PROJ_STORE_V8);
        {
            arp_pat_t p; int16_t x[ARPX_N];
            proj_arp_get(after.arp[0], &p, x);
            ok &= p.n == 8u && p.note[1] == ARP_NOTE(0, 2, 1) && x[ARPX_PAT] == 1 && x[ARPX_ROOT] == 0 && x[ARPX_OCT] == 0;
        }
        ok &= after.t[1].p[P_AMODE] == 1 && after.t[1].p[P_ARATE] == 5 && after.t[1].p[P_AOCT] == AD_UP &&
              after.t[1].p[P_AGATE] == 75 && after.t[1].p[P_APROB] == 4 && after.t[1].p[P_AORDER] == AB_WRAP &&
              after.t[1].p[P_AHOLD] == 1 && after.t[1].p[P_ASWING] == 30 && after.t[2].p[P_AMODE] == 0 &&
              after.t[2].p[P_ARATE] == 0 && !strcmp(after.name, before.name) && after.t[0].p[P_E0] == before.t[0].p[P_E0];
        bad += check("a FUN8 project (midi24) loads: SHAYPE on every track, MODE UPDN -> ON, 1/16 kept, GATE 96 -> 75 %, "
                     "DIR UP, STEPS 4, WRAP; HOLD and SWING kept", ok);
        memcpy(&proj_slot[2], &v8, PROJ_STORE_V8);               /* (a retained slot of midi24: its own size inside) */
        memset(proj_slot[2].raw + PROJ_STORE_V8, 0, PROJ_STORE_SIZE - PROJ_STORE_V8);
        bad += check("  a retained FUN8 slot (midi24's RAM) loads too, and saves back as FUN9",
                     proj_import(&after, &proj_slot[2], sizeof proj_slot[2]) && proj_pack(&packed, &after) &&
                     ((uint32_t *)packed.raw)[0] == PROJ_MAGIC);
        packed.raw[PROJ_ARP_OFF] = 31u;                          /* (a block of 31 notes: damaged) */
        h = proj_hash(packed.raw, PROJ_STORE_SIZE - 4u); memcpy(packed.raw + PROJ_STORE_SIZE - 4u, &h, 4);
        ok = proj_import(&after, &packed, sizeof packed);
        {
            arp_pat_t p; int16_t x[ARPX_N];
            proj_arp_get(after.arp[0], &p, x);
            ok &= p.n == 8u && x[ARPX_PAT] == 1;
        }
        bad += check("  a damaged SHAYPE block: that track gets SHAYPE, the project still loads", ok);
    }
    return bad;
}
/* a FUN7 image as the firmware of 89 parameters (before the chord keys P_CHRD / P_VOIC) wrote it: the engine's
 * values at 81..88, motion ids from 81 on for E0..E7 (m: its events as that firmware numbered them) */
static void pack_fun7_89(project_store_t *out, const project_t *q, const motion_store_t *m)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i, magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = 89;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < 89u; i++) b[pos++] = (uint8_t)(q->t[t].p[i < 81u ? i : i + 2u] + 64);
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            memcpy(b + pos, s->note, 4); pos += 4;
            b[pos++] = (uint8_t)(s->n | s->time << 3 | s->flags << 5);
            b[pos++] = s->vel; b[pos++] = s->hit; b[pos++] = s->acc; b[pos++] = s->probability;
        }
    }
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, m, sizeof *m);
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
}
static int fun7_89(void)
{
    int bad = 0, ok; ui_power_on(); track_t *t = &trk[0];
    project_t before, after; project_store_t old; motion_store_t m;
    uint32_t i, k;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < 8u; i++) trk[k].p[P_E0 + i] = (int16_t)(ENGINES[trk[k].eng_req]->edit[i].min + (int16_t)(k + i) %
            (ENGINES[trk[k].eng_req]->edit[i].max - ENGINES[trk[k].eng_req]->edit[i].min + 1));
    t->p[P_FM1_ATK] = 33; t->p[P_REV] = 20;
    t->step[2] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 100};
    project_capture(&before);
    memset(&m, 0, sizeof m);                                       /* as the 89-parameter firmware numbered them */
    m.count = 3; m.on = 1;
    m.event[0] = (motion_event_t){3, P_REV, 90};
    m.event[1] = (motion_event_t){5, 81, 40};                      /* its P_E0 (81) */
    m.event[2] = (motion_event_t){6, 61, 20};                      /* FM OP1 ATK: 61 then and now */
    pack_fun7_89(&old, &before, &m);
    ok = proj_import(&after, &old, sizeof old);
    for (k = 0; ok && k < NTRK; k++) {
        for (i = 0; i < 8u; i++) ok &= after.t[k].p[P_E0 + i] == before.t[k].p[P_E0 + i];
        for (i = 0; i < 81u; i++) ok &= after.t[k].p[i] == before.t[k].p[i];
        ok &= after.t[k].p[P_CHRD] == 0 && after.t[k].p[P_VOIC] == 0;
    }
    bad += check("FUN7 of 89 parameters: E0..E7 at 83..90, the chord keys OFF / CLOSE, the rest in place", ok &&
        after.t[0].p[P_FM1_ATK] == 33 && !memcmp(after.t[0].step, before.t[0].step, sizeof before.t[0].step));
    bad += check("  its motion: E0 (81) -> 83, REV and FM OP1 ATK (61) kept",
        after.motion.count == 3u && after.motion.event[0].param == P_REV && after.motion.event[1].param == P_E0 &&
        after.motion.event[1].value == 40 && after.motion.event[2].param == P_FM1_ATK);
    bad += check("  written again as FUN7 of 91: the same project", proj_pack(&old, &after) && old.raw[66] == P_COUNT &&
        proj_import(&before, &old, sizeof old) && !memcmp(&before, &after, sizeof before));
    pack_fun7_89(&old, &before, &m);                                /* SONG: a slot of the old firmware */
    memcpy(&proj_slot[2], &old, sizeof old);
    chain_config.count = 1; chain_config.row[0] = (chain_row_t){2, 1};
    ok = chain_prepare() == 0;
    bad += check("  SONG: an 89-parameter slot's motion plays at today's ids (E0 at 83)", ok &&
        chain.source[2].motion.count == 3u && chain.source[2].motion.event[1].param == P_E0);
    seq_stop(); chain_config.count = 0; chain.armed = 0;
    m.event[1].param = 82;                                          /* (any id P_E0 .. P_E7 of then moves by 2) */
    pack_fun7_89(&old, &before, &m);
    bad += check("  E1 (82) -> 84", proj_import(&after, &old, sizeof old) && after.motion.event[1].param == P_E1);
    return bad;
}
static int loads_and_song(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 21; motion_set_event(t, 0, P_REV, 100); t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    apply_preset_to(t, 1);
    bad += check("sound load clears incompatible motion and keeps pattern", !motion_count(t) && t->step[0].note[0] == 60);
    undo_swap(); bad += check("sound undo restores the original motion pool and base", motion_count(t) == 1u && t->p[P_REV] == 21);
    undo_swap(); bad += check("sound redo restores the loaded motion state", !motion_count(t));
    undo_swap(); project_save(0);
    t->p[P_REV] = 43; t->step[0].note[0] = 72; chain_config.count = 1; chain_config.row[0] = (chain_row_t){0, 1};
    bad += check("song preparation imports saved motion alongside steps", chain_prepare() == 0 && chain.source[0].motion.count == 1u);
    seq_start(); seq_tick(t, CTL);
    bad += check("song plays saved automation with current instruments", chain.running && t->p[P_REV] == 100 && t->step[0].note[0] == 72);
    seq_stop(); bad += check("song stop restores current base and editable pattern", t->p[P_REV] == 43 && t->step[0].note[0] == 72);
    return bad;
}
/* WaveLoop Kit: the ARP is SHAYPE (seq.c arp_fire over arp_shape.c; the core's parity with the toy: tests/arp_shape_test.c).
 * Here the host: the keys held as the chord, the steps on the track's clock, the notes and their gates, HOLD, ROOT,
 * the rate, recording. arp_next runs blocks until the next step plays; with GATE under 100 % and length-1 notes the
 * notes sounding then are exactly that step's */
static uint32_t arp_next(track_t *t, uint8_t *got)
{
    uint32_t idx = t->arp_idx, g = 0, i;
    while (t->arp_idx == idx && g++ < 200000u)
        arp_tick(t, CTL);
    for (i = 0; i < t->arp_n; i++)
        got[i] = arp_snd[trk_index(t)].note[i];
    return t->arp_n;
}
static int arp_seq(track_t *t, const uint8_t *want, uint32_t steps, uint32_t per)   /* the next steps play these */
{
    uint8_t got[ARP_SND];
    uint32_t s, n;
    int ok = 1;
    for (s = 0; s < steps; s++) {
        n = arp_next(t, got);
        if (n != per || memcmp(got, want + s * per, per)) {
            ok = 0;
            printf("    step %u: %u notes (%u %u), want %u %u\n", (unsigned)s, (unsigned)n, got[0], n > 1 ? got[1] : 0u,
                   want[s * per], per > 1 ? want[s * per + 1] : 0u);
        }
    }
    return ok;
}
static void arp_chord(track_t *t, const uint8_t *notes, uint32_t n)
{
    uint32_t i;
    arp_silence(t);
    t->nheld = 0; t->arp_phys = 0;
    for (i = 0; i < n; i++)
        arp_add(t, notes[i]);
}
static int shape_arp(void)
{
    static const uint8_t CEGBD[5] = {60, 64, 67, 71, 74}, CEG[3] = {60, 64, 67};
    uint8_t got[ARP_SND];
    uint32_t i, k, c0;
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = &trk[0];
    bad += check("ARP: the power-on pattern is SHAYPE (the toy's DEFAULT_PATTERN), 4 steps, 1/8, GATE 75 %, ARP off",
                 arp_pat[0].n == 8u && arp_pat[0].note[1] == ARP_NOTE(0, 2, 1) && t->p[P_APROB] == 4 && t->p[P_ARATE] == 3 &&
                 t->p[P_AGATE] == 75 && !t->p[P_AMODE] && arp_x[0][ARPX_PAT] == 1);
    t->p[P_AMODE] = 1;
    arp_chord(t, CEGBD, 5);
    bad += check("  C E G B D held: pass 1 C+G E+B G+B C+B, pass 2 a place up (the toy's example)",
                 arp_seq(t, (const uint8_t[]){60, 67, 64, 71, 67, 71, 60, 71, 64, 71, 67, 74, 71, 74, 64, 74}, 8, 2));
    t->p[P_AOCT] = AD_DOWN;
    arp_chord(t, CEGBD, 5);
    bad += check("  DOWN: the second pass a place down, wrapping round the top (D+E)",
                 arp_seq(t, (const uint8_t[]){60, 67, 64, 71, 67, 71, 60, 71, 74, 64}, 5, 2));
    t->p[P_AOCT] = AD_UP; t->p[P_AORDER] = AB_OCT;
    arp_chord(t, CEG, 3);
    bad += check("  BOUND OCT over C E G: row 4 is C an octave up", arp_seq(t, (const uint8_t[]){60, 67, 64, 72}, 2, 2));
    t->p[P_AORDER] = AB_WRAP; arp_x[0][ARPX_ROOT] = 8;   /* ROOT G */
    arp_chord(t, CEG, 3);
    bad += check("  ROOT G over C E G: G C E, step 1 plays G and E an octave up", arp_seq(t, (const uint8_t[]){67, 76}, 1, 2));
    arp_x[0][ARPX_ROOT] = 0;
    /* the rate: a step of 1/8 at 120 BPM is 11025 samples, 1/16T 3675 */
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    c0 = t->arp_clk;
    arp_next(t, got);
    k = t->arp_clk - c0;
    t->p[P_ARATE] = 6;
    arp_next(t, got);
    c0 = t->arp_clk;
    arp_next(t, got);
    bad += check("  RATE: 1/8 and 1/16T steps their length (within a block)",
                 k >= 11025u && k < 11025u + CTL && t->arp_clk - c0 >= 3675u && t->arp_clk - c0 < 3675u + CTL);
    t->p[P_ARATE] = 3;
    /* gates: a note of length 2 over GATE 75 % sounds 1.5 steps: on at the next step, gone before the one after */
    fm1_irq_off(); arp_pat[0].n = 0; ash_paint(&arp_pat[0], 0, 0, 2); fm1_irq_on();
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    ok = t->arp_n == 1u && got[0] == 60;
    arp_next(t, got);
    ok &= t->arp_n == 1u && got[0] == 60;
    arp_next(t, got);
    ok &= t->arp_n == 0u;
    bad += check("  a note two steps long sounds 1.5 steps at GATE 75 %", ok);
    ash_load(&arp_pat[0], 0);
    /* let go: the notes end; HOLD: the chord stays, the next press replaces it from step 1 */
    arp_chord(t, CEG, 3);
    arp_next(t, got);
    for (i = 0; i < 3u; i++) arp_remove(t, CEG[i]);
    arp_tick(t, CTL);
    bad += check("  the keys let go: nothing sounds, nothing held", !t->nheld && !t->arp_n);
    t->p[P_AHOLD] = 1;
    arp_chord(t, CEG, 3);
    for (i = 0; i < 3u; i++) arp_remove(t, CEG[i]);
    ok = t->nheld == 3u && arp_seq(t, (const uint8_t[]){60, 67, 64, 60}, 2, 2);   /* (C E G: row 3 wraps to C) */
    arp_add(t, 62); arp_add(t, 65); arp_add(t, 69);
    ok &= t->nheld == 3u && arp_seq(t, (const uint8_t[]){62, 69}, 1, 2);
    for (i = 0; i < 3u; i++) arp_remove(t, (uint32_t[]){62, 65, 69}[i]);
    bad += check("  HOLD: the chord stays after the keys; a new one replaces it from the first step", ok && t->nheld == 3u);
    t->p[P_AHOLD] = 0;
    t->nheld = 0; arp_tick(t, CTL);
    /* recording: the chord played is written, not what the arp plays: C E G pressed into step 1, the arp
     * stepping meanwhile (SHAYPE: C+G, E+C, ..), one step holds C E G and nothing else is written */
    song.sel = 0; song.rec = 1;
    t->p[P_VOICE] = V_POLY;
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){0};
    seq_start();
    seq_tick(t, CTL);
    for (i = 0; i < 3u; i++)
        input_on(t, CEG[i], 100);
    for (k = 0; k < 3u * 5512u / CTL; k++) {           /* three steps of 1/16, the arp at 1/8 playing */
        seq_tick(t, CTL);
        arp_tick(t, CTL);
    }
    ok = t->arp_n != 0u;
    for (i = 0; i < 3u; i++)
        input_off(t, CEG[i]);
    for (i = 0, k = 0; i < NSTEP; i++)
        k += t->step[i].time == ST_NOTE && t->step[i].n;
    ok &= k == 1u && t->step[0].n == 3u && t->step[0].note[0] == 60 && t->step[0].note[1] == 64 && t->step[0].note[2] == 67 &&
          t->step[1].time == ST_TIE && t->step[2].time == ST_TIE;
    seq_stop();
    song.rec = 0;
    bad += check("  recording: the chord played (C E G) into one step, held as TIEs; not the arp's notes", ok);
    t->nheld = 0; t->p[P_AMODE] = 0; arp_tick(t, CTL);
    ash_load(&arp_pat[0], 0);
    return bad;
}
int main(void)
{
    int bad = motion_recording() + motion_capacity() + probability_playback() + compact_project() + fun7_89() + loads_and_song() + shape_arp();
    printf("%s\n", bad ? "MOTION TEST FAILED" : "motion/chance/compact storage tests passed"); return bad != 0;
}
