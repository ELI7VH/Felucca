/* SPDX-License-Identifier: GPL-3.0-only */
/* The ARP (SHAYPE, firmware/src/arp_shape.c) against WaveLoop's Shape Arp core: every case of tests/arp_vectors.h
 * (tools/arp_vectors.mjs ran sequence.mjs on them) played through the C core, tick by tick as seq.c arp_fire plays it,
 * must give exactly the notes, steps and lengths the JS core gave, in its order. Then the toy's own unit tests
 * (sequence.test.mjs) as C, and the editing helpers. ZOOM (ash_subdivide) against the toy's subdivide on every case of
 * tests/arp_zoom_vectors.h (tools/arp_vectors.mjs --zoom), its refusals included; SHIFT (ash_shift) against the toy's
 * shift on every case of tests/arp_shift_vectors.h (--shift), in its order; MIRROR and MUTATE's helpers. DIR against
 * SHAYPE's Windows core (WYNDO and SHYFT are its own; UP DOWN PEND again): every case of tests/arp_window_vectors.h
 * (tools/arp_windows_vectors.c ran shaype_core.c sh_build_table on them) for two cycles. Run by tests/run_tests.sh. */
#include <stdio.h>
#include <string.h>
#include "arp_shape.c"
#include "arp_vectors.h"
#include "arp_zoom_vectors.h"
#include "arp_shift_vectors.h"
#include "arp_window_vectors.h"

static uint32_t xs;
static uint32_t xs_next(void)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return xs;
}
static int fails;
static void check(const char *what, int ok)
{
    printf("arp: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int play_vec(uint32_t k, int verbose)
{
    const arp_vec_t *v = &ARP_VEC[k];
    arp_pat_t p;
    uint8_t held[16], pitch[ARP_ROWS], len[ARP_ROWS];
    uint32_t n, tick, e = 0, i, j;
    int32_t offset = 0;
    memset(&p, 0, sizeof p);
    p.n = v->npat;
    memcpy(p.note, v->pat, sizeof p.note);
    xs = v->seed ? v->seed : 1u;
    n = ash_voicing(v->chord, v->nchord, v->root, held);
    for (tick = 0; tick < (uint32_t)v->steps * v->passes; tick++) {
        j = ash_tick(&p, v->steps, tick, held, n, v->dir, v->bound, xs_next, &offset, pitch, len);
        for (i = 0; i < j; i++, e++) {
            const uint16_t *w = v->ev + 3u * e;
            if (e >= v->nev || w[0] != tick || w[1] != pitch[i] || w[2] != len[i]) {
                if (verbose)
                    printf("arp:   case %u tick %u: C plays %u x%u, JS %u %u x%u\n", (unsigned)k, (unsigned)tick,
                           (unsigned)pitch[i], (unsigned)len[i], e < v->nev ? (unsigned)w[0] : 0u,
                           e < v->nev ? (unsigned)w[1] : 0u, e < v->nev ? (unsigned)w[2] : 0u);
                return 0;
            }
        }
    }
    if (e != v->nev && verbose)
        printf("arp:   case %u: C played %u notes, JS %u\n", (unsigned)k, (unsigned)e, (unsigned)v->nev);
    return e == v->nev;
}

/* DIR case k (tests/arp_window_vectors.h) through ash_tick for two of SHAYPE's cycles: tick t plays row t % cycle of the
 * Windows core's table, the same notes in the same order */
static int window_vec(uint32_t k, int verbose)
{
    const arp_wvec_t *v = &ARP_WVEC[k];
    arp_pat_t p;
    uint8_t pitch[ARP_ROWS], len[ARP_ROWS];
    uint32_t tick, e, i, j, round;
    int32_t offset = 0;
    memset(&p, 0, sizeof p);
    p.n = v->npat;
    memcpy(p.note, v->pat, sizeof p.note);
    for (round = 0; round < 2u; round++)
        for (tick = 0, e = 0; tick < v->cycle; tick++) {
            j = ash_tick(&p, v->steps, round * v->cycle + tick, v->chord, v->nchord, v->dir, v->bound, xs_next, &offset,
                         pitch, len);
            for (i = 0; i < j; i++, e++) {
                const uint16_t *w = v->ev + 3u * e;
                if (e >= v->nev || w[0] != tick || w[1] != pitch[i] || w[2] != len[i]) {
                    if (verbose)
                        printf("arp:   window case %u (dir %u) tick %u: C plays %u x%u, SHAYPE %u %u x%u\n", (unsigned)k,
                               (unsigned)v->dir, (unsigned)(round * v->cycle + tick), (unsigned)pitch[i], (unsigned)len[i],
                               e < v->nev ? (unsigned)w[0] : 0u, e < v->nev ? (unsigned)w[1] : 0u,
                               e < v->nev ? (unsigned)w[2] : 0u);
                    return 0;
                }
            }
            if (tick + 1u == v->cycle && e != v->nev) {
                if (verbose)
                    printf("arp:   window case %u: C played %u notes a cycle, SHAYPE %u\n", (unsigned)k, (unsigned)e,
                           (unsigned)v->nev);
                return 0;
            }
        }
    return 1;
}

/* ZOOM case k through ash_subdivide: the toy's pattern, STEPS and RATE, or its refusal (and nothing changed). The toy's
 * 1/64 (RATE 1/32 x2) is not a RATE here: AZ_FAST */
static uint32_t zoom_fast;
static int zoom_vec(uint32_t k, int verbose)
{
    const azv_t *v = &AZV[k];
    arp_pat_t p, was;
    uint32_t steps = v->steps, rate = v->rate, code, want = v->code, j, ok;
    memset(&p, 0, sizeof p);
    p.n = v->npat;
    memcpy(p.note, v->pat, sizeof p.note);
    was = p;
    for (j = 0; j < 8u && ARP_RATE2[j] != v->nrate2; j++)
        ;
    if (want == AZ_OK && j == 8u) {
        want = AZ_FAST;
        zoom_fast++;
    }
    code = ash_subdivide(&p, &steps, &rate, v->in);
    if (want != AZ_OK)
        ok = code == want && !memcmp(&p, &was, sizeof p) && steps == v->steps && rate == v->rate;
    else
        ok = code == AZ_OK && steps == v->nsteps && rate == j && p.n == v->npat &&
             !memcmp(p.note, v->out, 2u * v->npat);
    if (!ok && verbose)
        printf("arp:   zoom case %u (%u steps, RATE %u, %s): C %u, %u steps, RATE %u; JS %u, %u steps, x2 rate %u\n",
               (unsigned)k, (unsigned)v->steps, (unsigned)v->rate, v->in ? "in" : "out", (unsigned)code,
               (unsigned)steps, (unsigned)rate, (unsigned)want, (unsigned)v->nsteps, (unsigned)v->nrate2);
    return ok;
}

/* SHIFT case k through ash_shift: exactly the toy's notes, in its order */
static int shift_vec(uint32_t k, int verbose)
{
    const asv_t *v = &ASV[k];
    arp_pat_t p;
    int ok;
    memset(&p, 0, sizeof p);
    p.n = v->npat;
    memcpy(p.note, v->pat, sizeof p.note);
    ok = ash_shift(&p, v->steps, v->by) && p.n == v->nout && !memcmp(p.note, v->out, sizeof p.note);
    if (!ok && verbose)
        printf("arp:   shift case %u (%u steps, by %d): C %u notes, JS %u\n", (unsigned)k, (unsigned)v->steps, v->by,
               (unsigned)p.n, (unsigned)v->nout);
    return ok;
}
/* a pattern as cells (row r on step s: bit s of c[r]), the notes' lengths over them */
static void cells(const arp_pat_t *p, uint64_t *c)
{
    uint32_t i, s;
    memset(c, 0, 8u * sizeof *c);
    for (i = 0; i < p->n; i++)
        for (s = ARP_STEP(p->note[i]); s < ARP_STEP(p->note[i]) + ARP_LEN(p->note[i]); s++)
            c[ARP_ROW(p->note[i])] |= (uint64_t)1 << s;
}

static int same(const uint8_t *a, uint32_t n, const uint8_t *b, uint32_t m) { return n == m && !memcmp(a, b, n); }

int main(void)
{
    uint32_t k, bad = 0, events = 0, pw = 0, pw_same = 0, pw_bad = 0;
    char line[96];
    for (k = 0; k < ARP_NVEC; k++) {
        if (ARP_VEC[k].dir == AD_PEND && ARP_VEC[k].bound == AB_WRAP) {
            /* (retired from the JS gate: the toy's PEND wraps round the chord, SHAYPE's climbs in octaves whatever
             * BOUND says, WaveLoop a205e3a6; the Windows vectors hold PEND. Here: the case plays exactly as with OCT) */
            arp_vec_t o = ARP_VEC[k];
            arp_pat_t p;
            uint8_t held[16], pa[ARP_ROWS], la[ARP_ROWS], pb[ARP_ROWS], lb[ARP_ROWS];
            uint32_t n, t, ja, jb;
            int32_t oa = 0, ob = 0;
            pw++;
            pw_same += play_vec(k, 0);
            memset(&p, 0, sizeof p);
            p.n = o.npat;
            memcpy(p.note, o.pat, sizeof p.note);
            n = ash_voicing(o.chord, o.nchord, o.root, held);
            for (t = 0; t < (uint32_t)o.steps * o.passes; t++) {
                ja = ash_tick(&p, o.steps, t, held, n, AD_PEND, AB_WRAP, xs_next, &oa, pa, la);
                jb = ash_tick(&p, o.steps, t, held, n, AD_PEND, AB_OCT, xs_next, &ob, pb, lb);
                if (ja != jb || memcmp(pa, pb, ja) || memcmp(la, lb, ja)) {
                    pw_bad++;
                    break;
                }
            }
            continue;
        }
        events += ARP_VEC[k].nev;
        if (!play_vec(k, bad < 5u))
            bad++;
    }
    snprintf(line, sizeof line, "JS parity: %u cases, %u notes, every one as sequence.mjs plays it",
             (unsigned)(ARP_NVEC - pw), (unsigned)events);
    check(line, !bad);
    snprintf(line, sizeof line, "PEND WRAP: %u toy cases retired (%u never wrap: still the toy's); all play as PEND OCT",
             (unsigned)pw, (unsigned)pw_same);
    check(line, !pw_bad);
    {
        uint32_t wbad = 0, wev = 0, nd[AD_N] = {0}, bd[AD_N] = {0};
        for (k = 0; k < ARP_NWVEC; k++) {
            wev += ARP_WVEC[k].nev;
            nd[ARP_WVEC[k].dir]++;
            if (!window_vec(k, wbad < 5u)) {
                wbad++;
                bd[ARP_WVEC[k].dir]++;
            }
        }
        snprintf(line, sizeof line, "SHAYPE parity: %u cases, %u notes a cycle, two cycles each", (unsigned)ARP_NWVEC,
                 (unsigned)wev);
        check(line, !wbad);
        printf("arp:   UP %u DOWN %u PEND %u WYNDO %u SHYFT %u cases; failed %u %u %u %u %u\n", (unsigned)nd[AD_UP],
               (unsigned)nd[AD_DOWN], (unsigned)nd[AD_PEND], (unsigned)nd[AD_WYNDO], (unsigned)nd[AD_SHYFT],
               (unsigned)bd[AD_UP], (unsigned)bd[AD_DOWN], (unsigned)bd[AD_PEND], (unsigned)bd[AD_WYNDO],
               (unsigned)bd[AD_SHYFT]);
    }
    {   /* SHAYPE's README: F A C E held under a 3-row group, WYNDO plays F A C, then A C E, then again; on rows 3..5 the
         * same; SHYFT over it (a group no taller than the chord) stays put, as UP's first pass */
        static const uint8_t FACE[4] = {53, 57, 60, 64}, W[9] = {53, 57, 60, 57, 60, 64, 53, 57, 60};
        arp_pat_t p = {3, {0}, {ARP_NOTE(0, 0, 1), ARP_NOTE(1, 1, 1), ARP_NOTE(2, 2, 1)}};
        arp_pat_t q = {3, {0}, {ARP_NOTE(0, 2, 1), ARP_NOTE(1, 3, 1), ARP_NOTE(2, 4, 1)}};
        uint8_t pitch[ARP_ROWS], len[ARP_ROWS];
        uint32_t t, ok = 1;
        int32_t o = 0;
        for (t = 0; t < 9u; t++) {
            ok &= ash_tick(&p, 3, t, FACE, 4, AD_WYNDO, AB_WRAP, xs_next, &o, pitch, len) == 1u && pitch[0] == W[t];
            ok &= ash_tick(&q, 3, t, FACE, 4, AD_WYNDO, AB_OCT, xs_next, &o, pitch, len) == 1u && pitch[0] == W[t];
            ok &= ash_tick(&q, 3, t, FACE, 4, AD_SHYFT, AB_WRAP, xs_next, &o, pitch, len) == 1u && pitch[0] == W[t % 3u];
        }
        check("WYNDO: F A C E under a 3-row group plays F A C, then A C E (off row 0 too); SHYFT stays put", ok);
    }
    {   /* SHYFT: a 6-row group (rows 1..6) over C E G climbs it: rows 1-3, 2-4, 3-5, 4-6, the rest silent */
        static const uint8_t CEG[3] = {60, 64, 67};
        arp_pat_t p = {6, {0}, {ARP_NOTE(0, 1, 1), ARP_NOTE(1, 2, 1), ARP_NOTE(2, 3, 1), ARP_NOTE(3, 4, 1), ARP_NOTE(4, 5, 1),
                                ARP_NOTE(5, 6, 1)}};
        static const int16_t W[4][6] = {{60, 64, 67, -1, -1, -1}, {-1, 60, 64, 67, -1, -1}, {-1, -1, 60, 64, 67, -1},
                                        {-1, -1, -1, 60, 64, 67}};
        uint8_t pitch[ARP_ROWS], len[ARP_ROWS];
        uint32_t t, j, ok = 1;
        int32_t o = 0;
        for (t = 0; t < 30u; t++) {
            j = ash_tick(&p, 6, t, CEG, 3, AD_SHYFT, AB_OCT, xs_next, &o, pitch, len);
            ok &= W[(t / 6u) % 4u][t % 6u] < 0 ? j == 0u : j == 1u && pitch[0] == W[(t / 6u) % 4u][t % 6u];
        }
        check("SHYFT: a chord-high window climbs a 6-row group a row a pass (4 passes), the rows outside it rest", ok);
    }

    {   /* sequence.test.mjs: requested chord shapes move through C E G B D */
        static const uint8_t C[5] = {60, 64, 67, 71, 74};
        static const uint8_t W0[8] = {60, 67, 64, 71, 67, 71, 60, 71}, W1[8] = {64, 71, 67, 74, 71, 74, 64, 74};
        arp_pat_t p;
        uint8_t pitch[ARP_ROWS], len[ARP_ROWS], got[2][8];
        uint32_t s, o, ok = 1;
        ash_load(&p, 0);
        for (o = 0; o < 2u; o++)
            for (s = 0; s < 4u; s++) {
                ok &= ash_events(&p, s, C, 5, (int32_t)o, AB_WRAP, 0, pitch, len) == 2u;
                got[o][2 * s] = pitch[0];
                got[o][2 * s + 1] = pitch[1];
            }
        check("the toy's example: SHAYPE over C E G B D, passes 1 and 2", ok && !memcmp(got[0], W0, 8) && !memcmp(got[1], W1, 8));
    }
    {   /* boundary is independent from direction; negative positions wrap correctly */
        static const uint8_t CEG[3] = {60, 64, 67};
        static const int32_t PEND[5] = {0, 1, 2, 1, 0};
        uint32_t p, ok = 1;
        for (p = 0; p < 5u; p++)
            ok &= ash_pass_offset(p, 3, AD_PEND, xs_next) == PEND[p];
        check("WRAP / OCT, a negative place, PEND 0 1 2 1 0, DOWN -1",
              ash_resolve(CEG, 3, 3, AB_WRAP) == 60 && ash_resolve(CEG, 3, 3, AB_OCT) == 72 &&
              ash_resolve(CEG, 3, -1, AB_OCT) == 55 && ash_resolve(CEG, 0, 0, AB_WRAP) == -1 && ok &&
              ash_pass_offset(1, 3, AD_DOWN, xs_next) == -1);
    }
    {   /* PEND climbs to the top and back in octaves, BOUND WRAP or OCT (WaveLoop a205e3a6): UP 4 over C E G, passes
         * 0 1 2 1 (offsets): C E G C', E G C' E', G C' E' G', E G C' E'; UP with WRAP still wraps (C E G C) */
        static const uint8_t CEG[3] = {60, 64, 67};
        static const uint8_t W[16] = {60, 64, 67, 72, 64, 67, 72, 76, 67, 72, 76, 79, 64, 67, 72, 76};
        arp_pat_t p;
        uint8_t pitch[ARP_ROWS], len[ARP_ROWS];
        uint32_t t, b, ok = 1;
        int32_t o = 0;
        ash_load(&p, 1);
        for (b = 0; b < 2u; b++)
            for (t = 0; t < 16u; t++)
                ok &= ash_tick(&p, 4, t, CEG, 3, AD_PEND, b, xs_next, &o, pitch, len) == 1u && pitch[0] == W[t];
        ok &= ash_tick(&p, 4, 3, CEG, 3, AD_UP, AB_WRAP, xs_next, &o, pitch, len) == 1u && pitch[0] == 60;
        check("PEND: up to the top and back in octaves, WRAP as OCT (C E G C', ... G C' E' G'); UP WRAP still wraps", ok);
    }
    {   /* painting replaces intersecting notes but keeps adjacent notes and other rows */
        arp_pat_t p = {3, {0}, {ARP_NOTE(0, 0, 3), ARP_NOTE(4, 0, 1), ARP_NOTE(1, 1, 2)}};
        ash_paint(&p, 0, 1, 3);
        check("paint: a note over others in its row replaces them, the rest stay in order",
              p.n == 3u && p.note[0] == ARP_NOTE(4, 0, 1) && p.note[1] == ARP_NOTE(1, 1, 2) && p.note[2] == ARP_NOTE(1, 0, 3));
    }
    {   /* chosen root becomes the lowest pitch while other notes keep their pitch classes */
        static const uint8_t A[3] = {60, 64, 67}, WA[3] = {67, 72, 76}, B[3] = {60, 72, 74}, WB[2] = {72, 74};
        uint8_t o[16];
        check("ROOT: the root the lowest, the others up octaves, each pitch once",
              same(o, ash_voicing(A, 3, 67, o), WA, 3) && same(o, ash_voicing(B, 3, 72, o), WB, 2));
    }
    {   /* the steps cut, a full pattern, the stored form */
        arp_pat_t p = {2, {0}, {ARP_NOTE(1, 0, 6), ARP_NOTE(5, 1, 1)}};
        uint32_t i, ok;
        ash_fit(&p, 4);
        check("STEPS shorter: notes past it go, a note over it is cut", p.n == 1u && p.note[0] == ARP_NOTE(1, 0, 3));
        p.n = 0;
        for (i = 0; i < ARP_NMAX; i++)
            ash_paint(&p, i & 7u, i / 8u, 1);
        ok = p.n == ARP_NMAX && !ash_paint(&p, 7, 63, 1) && ash_valid(&p);
        p.note[0] = ARP_NOTE(60, 0, 8);
        check("a full pattern refuses one more; a note past step 64 is not a pattern", ok && !ash_valid(&p));
    }
    {   /* every factory pattern fits its steps and its notes do not overlap in a row */
        uint32_t f, i, j, ok = 1;
        for (f = 0; f < ARP_NFACTORY; f++) {
            arp_pat_t p;
            ash_load(&p, f);
            ok &= ash_valid(&p) && strlen(ARP_FACTORY[f].name) <= 7u;
            for (i = 0; i < p.n; i++) {
                ok &= ARP_STEP(p.note[i]) + ARP_LEN(p.note[i]) <= ARP_FACTORY[f].steps;
                for (j = 0; j < i; j++)
                    ok &= ARP_ROW(p.note[i]) != ARP_ROW(p.note[j]) ||
                          ARP_STEP(p.note[i]) >= ARP_STEP(p.note[j]) + ARP_LEN(p.note[j]) ||
                          ARP_STEP(p.note[j]) >= ARP_STEP(p.note[i]) + ARP_LEN(p.note[i]);
            }
        }
        check("factory patterns: inside their steps, rows without overlaps, short names", ok);
    }
    {   /* ZOOM: the toy's subdivide, case by case; and in then out is where it started */
        uint32_t zbad = 0, nref = 0, i, steps = 4, rate = 3, ok;
        arp_pat_t p, was;
        for (k = 0; k < AZV_N; k++) {
            nref += AZV[k].code != AZ_OK;
            if (!zoom_vec(k, zbad < 5u))
                zbad++;
        }
        snprintf(line, sizeof line, "ZOOM parity: %u cases (%u refused by the toy, %u at 1/32: AZ_FAST)", (unsigned)AZV_N,
                 (unsigned)nref, (unsigned)zoom_fast);
        check(line, !zbad && zoom_fast > 0u);
        ash_load(&p, 0);
        was = p;
        ok = ash_subdivide(&p, &steps, &rate, 1) == AZ_OK && steps == 8u && rate == 5u;
        for (i = 0; i < p.n; i++)
            ok &= ARP_STEP(p.note[i]) == 2u * ARP_STEP(was.note[i]) && ARP_LEN(p.note[i]) == 2u;
        ok &= ash_subdivide(&p, &steps, &rate, 0) == AZ_OK && steps == 4u && rate == 3u && !memcmp(&p, &was, sizeof p);
        steps = 33; rate = 3;
        ok &= ash_subdivide(&p, &steps, &rate, 1) == AZ_MAX && ash_subdivide(&p, &steps, &rate, 0) == AZ_ODD;
        steps = 4; rate = 7;
        ok &= ash_subdivide(&p, &steps, &rate, 1) == AZ_FAST && rate == 7u;
        rate = 0;
        ok &= ash_subdivide(&p, &steps, &rate, 0) == AZ_SLOW;
        rate = 2;
        ok &= ash_subdivide(&p, &steps, &rate, 0) == AZ_NORATE;
        rate = 6;
        ok &= ash_subdivide(&p, &steps, &rate, 1) == AZ_NORATE;
        rate = 3;
        ok &= ash_subdivide(&p, &steps, &rate, 0) == AZ_BETWEEN && !memcmp(&p, &was, sizeof p) && steps == 4u;
        check("ZOOM: SHAYPE in (8 steps 1/16) and out again exactly; 64 MAX, ODD, FASTEST, SLOWEST, no 1/2T 1/32T, BETWEEN", ok);
    }
    {   /* SHIFT: the toy's shift, case by case; right all the way round is where it started (as cells) */
        uint32_t sbad = 0, nsplit = 0, njoin = 0, i, ok;
        uint64_t c0[8], c1[8];
        arp_pat_t p, was;
        for (k = 0; k < ASV_N; k++) {
            nsplit += ASV[k].nout > ASV[k].npat;
            njoin += ASV[k].nout < ASV[k].npat;
            if (!shift_vec(k, sbad < 5u))
                sbad++;
        }
        snprintf(line, sizeof line, "SHIFT parity: %u cases (%u split a note at the end, %u joined one)", (unsigned)ASV_N,
                 (unsigned)nsplit, (unsigned)njoin);
        check(line, !sbad && nsplit > 0u && njoin > 0u);
        ash_load(&p, 6);                             /* DRONE: a long note */
        cells(&p, c0);
        ok = 1;
        for (i = 0; i < 4u; i++)
            ok &= ash_shift(&p, 4, 1);
        cells(&p, c1);
        ok &= !memcmp(c0, c1, sizeof c0);
        ash_load(&p, 6);
        was = p;
        ok &= ash_shift(&p, 4, 1) && ash_shift(&p, 4, -1) && p.n == was.n;
        cells(&p, c1);
        ok &= !memcmp(c0, c1, sizeof c0);
        memset(&p, 0, sizeof p);                     /* (ARP_NMAX notes, 8 of them over the end with nothing on step 0 to */
        for (i = 0; i < 8u; i++)                     /* join: shifted right they split, 8 more) */
            p.note[p.n++] = ARP_NOTE(62u, i, 2);
        for (i = 0; p.n < ARP_NMAX; i++)
            p.note[p.n++] = ARP_NOTE(1u + i / 8u, i % 8u, 1);
        was = p;
        ok &= !ash_shift(&p, 64, 1) && !memcmp(&p, &was, sizeof p);
        check("SHIFT: DRONE right 4 times round is DRONE; right and left loses nothing; past ARP_NMAX notes: refused", ok);
    }
    {   /* MIRROR: reverse and flip, each its own inverse; MUTATE's empty cells */
        arp_pat_t p, was;
        uint32_t i, ok = 1, n;
        ash_load(&p, 3);                             /* STABS, 8 steps: 0 3 6(2) */
        was = p;
        ash_reverse(&p, 8);
        for (i = 0; i < p.n; i++)
            ok &= ARP_STEP(p.note[i]) == 8u - ARP_STEP(was.note[i]) - ARP_LEN(was.note[i]) &&
                  ARP_ROW(p.note[i]) == ARP_ROW(was.note[i]) && ARP_LEN(p.note[i]) == ARP_LEN(was.note[i]);
        ash_reverse(&p, 8);
        ok &= !memcmp(&p, &was, sizeof p);
        check("MIRROR reverse: step s length l -> STEPS - s - l (STABS: 0 3 6 -> 7 4 0), twice is itself", ok);
        ok = 1;
        ash_load(&p, 1);                             /* UP 4: rows 0..3 */
        was = p;
        ash_flip(&p);
        for (i = 0; i < p.n; i++)
            ok &= ARP_ROW(p.note[i]) == 3u - ARP_ROW(was.note[i]) && ARP_STEP(p.note[i]) == ARP_STEP(was.note[i]);
        ash_flip(&p);
        ok &= !memcmp(&p, &was, sizeof p);
        memset(&p, 0, sizeof p);
        p.note[p.n++] = ARP_NOTE(0, 2, 1);
        p.note[p.n++] = ARP_NOTE(1, 5, 1);
        p.note[p.n++] = ARP_NOTE(2, 3, 1);
        ash_flip(&p);
        ok &= ARP_ROW(p.note[0]) == 5u && ARP_ROW(p.note[1]) == 2u && ARP_ROW(p.note[2]) == 4u;
        check("MIRROR flip: the rows turn over inside their span (UP 4: 1..4 -> 4..1; rows 3..6 stay 3..6), twice is itself",
              ok);
        ash_load(&p, 0);                             /* SHAYPE, 4 steps: 8 notes of 32 cells */
        ok = ash_empty(&p, 4, -1) == 24u && p.n == 8u;
        n = ash_empty(&p, 4, 0);                     /* (the first: step 0, row 1) */
        ok &= n == 24u && p.n == 9u && p.note[8] == ARP_NOTE(0, 1, 1) && ash_empty(&p, 4, -1) == 23u;
        ash_load(&p, 6);                             /* DRONE: row 0 is one note over all 4 steps */
        ok &= ash_empty(&p, 4, -1) == 32u - 6u;
        memset(&p, 0, sizeof p);
        for (i = 0; i < 8u; i++)
            p.note[p.n++] = ARP_NOTE(0, i, 2);
        ok &= ash_empty(&p, 2, -1) == 0u;
        check("MUTATE: empty cells counted under long notes (SHAYPE 24, DRONE 26, full 0); the k-th gets a note", ok);
    }
    printf("arp: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
